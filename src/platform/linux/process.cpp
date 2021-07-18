//
// Created by aviallon on 13/04/2021.
//

#include "core/process.hpp"
#include "utility/utils.hpp"
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <mutex>
#include <queue>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

int connect_netlink_socket() {

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
  int rc = bind(nl_sock, (struct sockaddr *)&sa_nl, sizeof(sa_nl));
  if (rc == -1) {
    [[unlikely]] spdlog::error("Couldn't bind to socket: {}",
                               get_error_string(errno));
    close(nl_sock);
    return -1;
  }

  return nl_sock;
}

int set_event_subscription(int nl_sock, bool enable) {
  struct alignas(NLMSG_ALIGNTO) nl_cn_msg {
    struct nlmsghdr nl_hdr;
    struct [[gnu::packed]] {
      struct cn_msg         cn_msg;
      enum proc_cn_mcast_op cn_mcast;
    };
  };

  nl_cn_msg netlink_connection_message = {};
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

  return send(nl_sock, &netlink_connection_message, sizeof(nl_cn_msg), 0);
}

std::string get_command_from_pid(int pid) {
  std::ifstream pid_info;
  std::string   command = "<unknown>";

  // Building path
  try {
    std::filesystem::path proc_path(std::format("/proc/{}/comm", pid));

    if (!exists(proc_path))
      return command;

    pid_info.open(proc_path);

    command.clear();
    pid_info >> command;
  } catch (const std::filesystem::filesystem_error &e) {
    spdlog::error("{}: filesystem error: {} (path: {})", __func__, e.what(),
                  e.path1().string());
  }

  return command;
}

int handle_events(int nl_sock, atomic_queue<Process> *queue,
                  InterruptHandler *interruptHandler) {
  struct alignas(NLMSG_ALIGNTO) nl_cn_msg {
    struct nlmsghdr nl_hdr;
    struct [[gnu::packed]] {
      struct cn_msg     cn_msg;
      struct proc_event proc_ev;
    };
  };

  ssize_t   status = 0;
  nl_cn_msg netlink_connection_message = {};
  while (!interruptHandler->should_exit()) {
    errno = 0;
    status = recv(nl_sock, &netlink_connection_message,
                  sizeof(netlink_connection_message), 0);
    if (status == 0) {
      return EXIT_SUCCESS;
    } else if (status == -1) {
      if (errno == EINTR) {
        continue;
      }
      spdlog::error("Bad netlink status: {}", get_error_string(errno));
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
      if (pid != 0)
        queue->push(Process{.pid = pid, .name = get_command_from_pid(pid)});
    } catch (std::exception &e) {
      spdlog::error("{}: unhandled error: {}", __func__, e.what());
    }
  }

  return status;
}

struct ProcessQueue::Socket {
  int socket;
};

int ProcessQueue::init() {
  full_scan();

  int socket = connect_netlink_socket();

  if (socket == -1) {
    [[unlikely]] throw std::runtime_error("Couldn't open netlink socket!");
  }

  const int listen_status = set_event_subscription(socket, true);
  if (listen_status == 0) {
    [[unlikely]] close(socket);
    throw std::runtime_error("Couldn't subscribe to process events!");
  }

  this->sock = std::make_unique<ProcessQueue::Socket>(
      ProcessQueue::Socket{.socket = socket});

  return socket;
}

ProcessQueue::ProcessQueue(InterruptHandler *interruptHandler)
    : interruptHandler(interruptHandler) {
  init();
}

void ProcessQueue::start() {
  this->event_thread =
      std::jthread([this](const std::stop_token &stop_token) -> void {
        int ret = EXIT_FAILURE;
        while (ret == EXIT_FAILURE && !stop_token.stop_requested()) {
          ret = handle_events(this->sock->socket, &this->process_queue,
                              this->interruptHandler);

          if (ret == EXIT_SUCCESS || stop_token.stop_requested())
            break;

          spdlog::warn("restarting socket connection!");
          set_event_subscription(this->sock->socket, false);
          close(this->sock->socket);
          this->init();
        };
      });
}

ProcessQueue::~ProcessQueue() {
  this->event_thread.request_stop();
  this->event_thread.join();

  set_event_subscription(this->sock->socket, false);
  close(this->sock->socket);
}

void ProcessQueue::full_scan() {
  for (const std::filesystem::path &process_path :
       std::filesystem::directory_iterator("/proc")) {
    const std::string pid_str = process_path.filename().string();
    if (isdigit(pid_str[0])) {
      const int pid = std::stoi(pid_str);
      spdlog::trace("Existing process: {}", pid);
      const std::string command = get_command_from_pid(pid);

      process_queue.push(Process{
          .pid = pid,
          .name = command,
      });
    }
  };
}

pid_t get_pid() { return getpid(); }
