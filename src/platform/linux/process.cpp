//
// Created by aviallon on 13/04/2021.
//

#include "core/process.hpp"
#include "process_helpers.hpp"
#include "utility/synchronized_queue.hpp"
#include "utility/utils.hpp"

#if !defined(USE_BPF_PROC_IMPL)
#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <filesystem>
#include <format>
#include <fstream>
#include <thread>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

#if defined(USE_BPF_PROC_IMPL)
static volatile std::atomic<bool>   g_exiting;
static synchronized_queue<Process> *g_process_queue;

void ProcessQueue::init() noexcept {
  g_process_queue = &this->process_queue;
  this->full_scan();

  g_exiting.store(false, std::memory_order_relaxed);
  if (!m_bpf_program.initialize()) {
    spdlog::error("Failed to initialize the BPF program");
  }
}

extern "C" void handle_event(void *ctx, int32_t cpu, void *data,
                             uint32_t data_sz) {
  static constexpr auto TASK_COMM_LEN = 16;

  struct event {
    pid_t pid;
    pid_t prev_pid;
    __u64 delta_us;
    char  task[TASK_COMM_LEN];
  };

  auto *e = static_cast<struct event *>(data);

  if (e->pid == 0 || !g_process_queue || e->pid == e->prev_pid) {
    return;
  }

  std::string &&command = get_command_from_pid(e->pid);
  if (command.ends_with(".exe")) {
    std::replace(command.begin(), command.end(), '\\', '/');
    fs::path cmd_path{command.c_str()};
    command = cmd_path.filename().string();
  }

  Process process{
      .delta_us = e->delta_us, .pid = e->pid, .name = std::move(command)};
  spdlog::trace("Pushing new process: pid = {}", e->pid);
  g_process_queue->push(process);
}

extern "C" void handle_lost_events(void *ctx, int cpu, __u64 lost_cnt) {
  fmt::print(stderr, "Lost {} events on CPU {}!\n", lost_cnt, cpu);
}

static std::int32_t handle_events(BPFProgram                  *bpf_program,
                                  synchronized_queue<Process> *queue,
                                  const std::stop_token       &stop_token) {

  std::int32_t status{};
  // while (!stop_token.stop_requested()) {
  while (!g_exiting.load(std::memory_order_consume)) {
    status = bpf_program->pool_event();
    if (status < 0 && status != -EINTR) {
      spdlog::error("Error received ({}): {}", status,
                    get_error_string(-status));
      return EXIT_FAILURE;
    }
    /* reset err to return 0 if exiting */
    status = 0;
  }

  return status;
}

void ProcessQueue::start() {
  this->event_thread =
      std::jthread([this](const std::stop_token &stop_token) -> void {
        int ret = EXIT_FAILURE;
        while (ret == EXIT_FAILURE && !stop_token.stop_requested()) {
          spdlog::trace("Starting handle_events");

          ret = handle_events(&m_bpf_program, &this->process_queue, stop_token);

          if (ret == EXIT_SUCCESS || stop_token.stop_requested())
            break;

          spdlog::warn("restarting socket connection!");
          g_exiting.store(true, std::memory_order_relaxed);
          m_bpf_program.destroy();
          this->init();
        }
      });
}

#else

static int connect_netlink_socket() {

  errno = 0;
  int nl_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
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
  int rc =
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

static int set_event_subscription(int nl_sock, bool enable) {
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

  return static_cast<int>(
      send(nl_sock, &netlink_connection_message, sizeof(nl_cn_msg), 0));
}

static int handle_events(int nl_sock, synchronized_queue<Process> *queue,
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
    const int errnum = errno;
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
          queue->push(std::move(process));
        }
      }
    } catch (std::exception &e) {
      spdlog::error("{}: unhandled error: {}", __func__, e.what());
    }
  }

  return static_cast<int>(status);
}

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

void ProcessQueue::start() {
  this->event_thread =
      std::jthread([this](const std::stop_token &stop_token) -> void {
        int ret = EXIT_FAILURE;
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

#endif

#ifndef COMMAND_NAME_HEURISTIC_SKIP_EXE_FAILURES
#define COMMAND_NAME_HEURISTIC_SKIP_EXE_FAILURES 5
#endif

std::string get_command_from_pid(pid_t pid) {
  static uint exe_fail_count = 0;

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

void ProcessQueue::full_scan() {
  spdlog::info("Doing a full scan");
  std::uint32_t n_process_found{};
  for (const fs::path &process_path : fs::directory_iterator("/proc")) {
    const std::string pid_str = process_path.filename().string();
    if (isdigit(pid_str[0])) {
      const std::int32_t pid = to_int(pid_str);
      // spdlog::trace("Pushing existing process: {}", pid);
      auto &&command = get_command_from_pid(pid);

      this->process_queue.push(Process{
          .pid = pid,
          .name = std::move(command),
      });
      n_process_found++;
    }
  }

  spdlog::debug("Full scan found {:d} processes", n_process_found);
}

void ProcessQueue::stop() noexcept {
#if defined(USE_BPF_PROC_IMPL)
  g_exiting.store(true, std::memory_order_relaxed);
#endif
  if (!this->event_thread.get_stop_token().stop_requested()) {
    this->event_thread.request_stop();
    this->event_thread.join();
  }
#if defined(USE_BPF_PROC_IMPL)
  m_bpf_program.destroy();
#else
  set_event_subscription(this->sock->socket, false);
  close(this->sock->socket);
#endif
}

pid_t get_pid() { return getpid(); }
