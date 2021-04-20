//
// Created by aviallon on 13/04/2021.
//

#include "utils.hpp"
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>
#include <iostream>
#include <spdlog/spdlog.h>
#include <utility>
#include "process_listener.hpp"


int connect_netlink_socket() {

    int nl_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
    if (nl_sock == -1) {
        perror("socket");
        return -1;
    }

    struct sockaddr_nl sa_nl = {
            .nl_family = AF_NETLINK,
            .nl_pid = static_cast<__u32>(getpid()),
            .nl_groups = CN_IDX_PROC,
    };

    int rc = bind(nl_sock, (struct sockaddr *)&sa_nl, sizeof(sa_nl));
    if (rc == -1) {
        std::cerr << "bind failed" << std::endl;
        close(nl_sock);
        return -1;
    }

    return nl_sock;
}

int set_event_subscription(int nl_sock, bool enable) {
    struct alignas(NLMSG_ALIGNTO) nl_cn_msg {
        struct nlmsghdr nl_hdr;
        struct __attribute__ ((packed)) {
            struct cn_msg cn_msg;
            enum proc_cn_mcast_op cn_mcast;
        };
    };

    nl_cn_msg netlink_connection_message = {};
    netlink_connection_message.nl_hdr = {
            .nlmsg_len = sizeof(nl_cn_msg),
            .nlmsg_type = NLMSG_DONE,
            .nlmsg_pid = static_cast<__u32>(getpid()),
    };

    netlink_connection_message.cn_msg = {
            .id = {
                    .idx = CN_IDX_PROC,
                    .val = CN_VAL_PROC,
            },
            .len = sizeof (enum proc_cn_mcast_op)
    };

    netlink_connection_message.cn_mcast = enable ? PROC_CN_MCAST_LISTEN : PROC_CN_MCAST_IGNORE;

    return send(nl_sock, &netlink_connection_message, sizeof(nl_cn_msg), 0);
}

std::string get_command_from_pid(int pid) {
    std::ifstream pid_info;
    std::string command = "<unknown>";

    // Building path
    try {
        std::filesystem::path proc_path;
        proc_path.append("/proc");
        proc_path.append(std::to_string(pid));
        proc_path.append("comm");

        pid_info.open(proc_path);

        command.clear();
        pid_info >> command;
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    return command;
}

int handle_events(int nl_sock, atomic_queue<Process> *queue, InterruptHandler* interruptHandler) {
    struct alignas(NLMSG_ALIGNTO) nl_cn_msg {
        struct nlmsghdr nl_hdr;
        struct __attribute__ ((packed)) {
            struct cn_msg cn_msg;
            struct proc_event proc_ev;
        };
    };

    ssize_t status = 0;
    nl_cn_msg netlink_connection_message = {};
    while (!interruptHandler->should_exit()) {
        errno = 0;
        status = recv(nl_sock, &netlink_connection_message, sizeof(netlink_connection_message), 0);
        if (status == 0) {
            return EXIT_SUCCESS;
        } else if (status == -1) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "netlink received" << std::endl;
            return EXIT_FAILURE;
        }

        if (netlink_connection_message.proc_ev.what == proc_event::PROC_EVENT_EXEC) {
            std::stringstream ss;
            // ss << "exec: tid=" << netlink_connection_message.proc_ev.event_data.exec.process_pid << " pid=" <<
            //   netlink_connection_message.proc_ev.event_data.exec.process_tgid << std::endl;
            // std::cout << ss.str() << std::flush;

            pid_t pid = netlink_connection_message.proc_ev.event_data.exec.process_pid;

            const std::string command = get_command_from_pid(pid);


            queue->push(Process {
                .pid = pid,
                .name = command
            });
        }

    }

    return status;
}

struct ProcessListener::Socket {
    int socket;
};

int ProcessListener::init() {
    for (const std::filesystem::path& process_path : std::filesystem::directory_iterator("/proc")) {
        const std::string pid_str = process_path.filename().string();
        if (isdigit(pid_str[0])) {
            const int pid = std::stoi(pid_str);
            spdlog::trace("Existing process: {}", pid);
            const std::string command = get_command_from_pid(pid);

            process_queue.push(Process {
                .pid = pid,
                .name = command,
            });
        }
    };

    int socket = connect_netlink_socket();

    if (socket == -1) {
    }

    const int listen_status = set_event_subscription(socket, true);
    if (listen_status == 0) {
        close(socket);
        throw std::runtime_error("Couldn't subscribe to process events!");
    }

    this->sock = std::make_unique<ProcessListener::Socket>(
            ProcessListener::Socket { .socket = socket }
    );

    return socket;
}


ProcessListener::ProcessListener(InterruptHandler* interruptHandler) : interruptHandler(interruptHandler) {
    init();
}

void ProcessListener::start() {
    this->event_thread = std::jthread(&handle_events, this->sock->socket, &this->process_queue, this->interruptHandler);
}



ProcessListener::~ProcessListener() {
    this->event_thread.join();

    set_event_subscription(this->sock->socket, false);
    close(this->sock->socket);
}