//
// Created by aviallon on 13/04/2021.
//

#include "core/process.hpp"
#include "utility/synchronized_queue.hpp"
#include "utility/utils.hpp"

#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>

#include <cctype>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <queue>
#include <thread>

#include <spdlog/spdlog.h>

#include "process_helpers.hpp"

namespace fs = std::filesystem;

static std::int32_t connect_netlink_socket() noexcept(false) {

  errno = 0;
  std::int32_t nl_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
  if (nl_sock == -1) {
    [[unlikely]] spdlog::error("Couldn't create netlink socket, errno = {}",
                               get_error_string(errno));
    return -1;
  }

  struct sockaddr_nl sa_nl = {
      .nl_family = AF_NETLINK,
      .nl_pid = static_cast<__u32>(getpid()),
      .nl_groups = CN_IDX_PROC,
  };

  errno = 0;
  std::int32_t rc =
      bind(nl_sock, reinterpret_cast<struct sockaddr *>(&sa_nl), sizeof(sa_nl));
  if (rc == -1) {
    [[unlikely]] spdlog::error("Couldn't bind to socket: {}",
                               get_error_string(errno));
    close(nl_sock);
    return -1;
  }

  /* Timeout after some time, to avoid blocking forever */
  struct timeval tv {
    .tv_sec = 0, .tv_usec = 500 * 1000
  };
  setsockopt(nl_sock, SOL_SOCKET, SO_RCVTIMEO,
             reinterpret_cast<const char *>(&tv), sizeof(tv));

  return nl_sock;
}

static std::int32_t set_event_subscription(std::int32_t nl_sock, bool enable) {
  struct alignas(NLMSG_ALIGNTO) nl_cn_msg {
    struct nlmsghdr nl_hdr{};
    struct [[gnu::packed]] {
      struct cn_msg         cn_msg{};
      enum proc_cn_mcast_op cn_mcast{};
    };
  };

  nl_cn_msg netlink_connection_message{};
  netlink_connection_message.nl_hdr = {
      .nlmsg_len = sizeof(nl_cn_msg),
      .nlmsg_type = NLMSG_DONE,
      .nlmsg_pid = static_cast<__u32>(getpid()),
  };

  netlink_connection_message.cn_msg = {.id =
                                           {
                                               .idx = CN_IDX_PROC,
                                               .val = CN_VAL_PROC,
                                           },
                                       .len = sizeof(enum proc_cn_mcast_op)};

  netlink_connection_message.cn_mcast =
      enable ? PROC_CN_MCAST_LISTEN : PROC_CN_MCAST_IGNORE;

  return static_cast<std::int32_t>(
      send(nl_sock, &netlink_connection_message, sizeof(nl_cn_msg), 0));
}

static std::int32_t handle_events(std::int32_t nl_sock, synchronized_queue<Process> *queue,
                         const std::stop_token &stop_token) {
  struct alignas(NLMSG_ALIGNTO) nl_cn_msg {
    struct nlmsghdr nl_hdr{};
    struct [[gnu::packed]] {
      struct cn_msg     cn_msg{};
      struct proc_event proc_ev{};
    };
  };

  ssize_t   status = 0;
  nl_cn_msg netlink_connection_message{};
  while (!stop_token.stop_requested()) {
    errno = 0;
    status = recv(nl_sock, &netlink_connection_message,
                  sizeof(netlink_connection_message), 0);
    const std::int32_t errnum = errno;
    if (status == 0) {
      return EXIT_SUCCESS;
    } else if (status == -1) {
      if (errnum == EINTR || errnum == EAGAIN) {
        continue;
      }
      if (errnum == ENOBUFS) {
        spdlog::warn("Received message overrun - ({})",
                      get_error_string(errnum));
        return EXIT_FAILURE;
      }
      spdlog::error("Bad netlink status ({}): {}", errnum,
                    get_error_string(errnum));
      return EXIT_FAILURE;
    }

    pid_t pid = 0;

    switch (netlink_connection_message.proc_ev.what) {
    case proc_event::PROC_EVENT_FORK:
      pid = netlink_connection_message.proc_ev.event_data.fork.child_pid;
      break;
    case proc_event::PROC_EVENT_EXEC:
      pid = netlink_connection_message.proc_ev.event_data.exec.process_pid;
      break;
    case proc_event::PROC_EVENT_COMM:
      pid = netlink_connection_message.proc_ev.event_data.comm.process_pid;
      break;
    default:
      break; // we don't care
    }

    try {
      if (pid != 0) {
        static Process last_process{};

        Process process{.pid = pid, .name = get_command_from_pid(pid)};
        if (process !=
            last_process) { // Fix
                            // https://gitlab.com/ananicy-cpp/ananicy-cpp/-/issues/20
          spdlog::trace("Pushing new process: pid = {}", pid);
          queue->push(process);
        }
      }
    } catch (std::exception &e) {
      spdlog::error("{}: unhandled error: {}", __func__, e.what());
    }
  }

  return static_cast<std::int32_t>(status);
}

#ifndef COMMAND_NAME_HEURISTIC_SKIP_EXE_FAILURES
#define COMMAND_NAME_HEURISTIC_SKIP_EXE_FAILURES 5
#endif

std::string get_command_from_pid(pid_t pid) {
  static std::uint32_t exe_fail_count = 0;

  // Cmdline method
  try {
    std::vector<std::string> cmdline = process_info::get_cmdline_from_pid(pid);
    if (!cmdline.empty()) {
      auto exe_name_begin = cmdline[0].find_last_of('/');

      spdlog::trace("{}: exe_name_begin: {}, cmdline: {}", __func__,
                    exe_name_begin, cmdline);

      if (exe_name_begin != std::string::npos) {
        return cmdline[0].substr(exe_name_begin + 1);
      }
      return cmdline[0];
    }

  } catch (const fs::filesystem_error &e) {
    spdlog::warn("{}: filesystem error: {} (path: {})", __func__, e.what(),
                 e.path1().string());
  }

  // Exe method
  /**
   * If reading the exe file doesn't work several times in a row,
   * it probably means we don't have the permissions for that.
   * We should skip it entirely for performance reasons.
   */
  if (exe_fail_count < COMMAND_NAME_HEURISTIC_SKIP_EXE_FAILURES) {
    try {
      const fs::path proc_path{std::format("/proc/{}/exe", pid)};

      const fs::path exe_path = read_symlink(proc_path);
      auto           exe_name = exe_path.filename().string();
      const auto     exe_name_end = exe_name.find(" (deleted)");
      if (exe_name_end != std::string::npos) {
        exe_name = exe_name.substr(0, exe_name_end + 1);
      }
      spdlog::trace("{}: exe filename: {}", __func__, exe_name);

      exe_fail_count = 0;

      return exe_name;
    } catch (const fs::filesystem_error &e) {
      const auto errcode = e.code().value();
      if (errcode == EEXIST)
        return "<unknown>";
      else if (errcode == EACCES || errcode == EPERM)
        exe_fail_count++;

      spdlog::trace("{}: read symlink failed: {}, code: {}", __func__,
                    e.code().message(), errcode);
    }
  }

  // Comm method
  try {
    std::ifstream comm_file(std::format("/proc/{}/comm", pid));
    std::string   comm;

    std::getline(comm_file, comm);
    return comm;
  } catch (const fs::filesystem_error &e) {
    spdlog::error("{}: filesystem error: {} (path: {})", __func__, e.what(),
                  e.path1().string());
  }

  return {"<unknown>"};
}

struct ProcessQueue::Socket {
    std::int32_t socket;
};

std::int32_t ProcessQueue::init() noexcept(false) {
  full_scan();

  const std::int32_t socket = connect_netlink_socket();

  if (socket == -1) {
    [[unlikely]] throw std::runtime_error("Couldn't open netlink socket!");
  }

  const std::int32_t listen_status = set_event_subscription(socket, true);
  if (listen_status == 0) {
    [[unlikely]] close(socket);
    throw std::runtime_error("Couldn't subscribe to process events!");
  }

  this->sock = std::make_unique<ProcessQueue::Socket>(
      ProcessQueue::Socket{.socket = socket});

  return socket;
}

ProcessQueue::ProcessQueue() { init(); }

void ProcessQueue::start() {
  this->event_thread =
      std::jthread([this](const std::stop_token &stop_token) -> void {
        std::int32_t ret = EXIT_FAILURE;
        while (ret == EXIT_FAILURE && !stop_token.stop_requested()) {
          spdlog::trace("Starting handle_events");
          ret = handle_events(this->sock->socket, &this->process_queue,
                              stop_token);

          if (ret == EXIT_SUCCESS || stop_token.stop_requested())
            break;

          spdlog::warn("restarting socket connection!");
          set_event_subscription(this->sock->socket, false);
          close(this->sock->socket);
          this->init();
        };
      });
}

ProcessQueue::~ProcessQueue() noexcept { stop(); }

void ProcessQueue::full_scan() {
  spdlog::info("Doing a full scan");
  unsigned n_process_found = 0;
  for (const fs::path &process_path : fs::directory_iterator("/proc")) {
    const std::string pid_str = process_path.filename().string();
    if (isdigit(pid_str[0])) {
      const auto pid = to_int<std::int32_t>(pid_str);
      // spdlog::trace("Pushing existing process: {}", pid);
      const auto &command = get_command_from_pid(pid);

      process_queue.push(Process{
          .pid = pid,
          .name = command,
      });
      n_process_found++;
    }
  }

  spdlog::debug("Full scan found {:d} processes", n_process_found);
}

void ProcessQueue::stop() noexcept {
  if (!this->event_thread.get_stop_token().stop_requested()) {
    this->event_thread.request_stop();
    this->event_thread.join();
  }
  set_event_subscription(this->sock->socket, false);
  close(this->sock->socket);
}

pid_t get_pid() { return getpid(); }
