#define _XOPEN_SOURCE 700

#include <iostream>
#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <unistd.h>
#include <csignal>
#include <mutex>
#include <queue>
#include <atomic>
#include <thread>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <condition_variable>

using json = nlohmann::json;

using namespace std::chrono_literals;

template <typename T>
class atomic_queue {
public:
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(value);
    }

    T pop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        T front = m_queue.front();
        m_queue.pop();
        return front;
    }

    size_t size() {
        return m_queue.size();
    }

private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
};

/*
 * connect to netlink
 * returns netlink socket, or -1 on error
 */
int nl_connect() {

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

int listen_to_process_events(int nl_sock, bool enable) {
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

volatile std::atomic<bool> need_exit = false;
std::condition_variable exit_condition;
std::mutex exit_mutex;

struct Process {
    pid_t pid;
    std::string name;
};

int handle_events(int nl_sock, atomic_queue<Process> *queue) {
    struct alignas(NLMSG_ALIGNTO) nl_cn_msg {
        struct nlmsghdr nl_hdr;
        struct __attribute__ ((packed)) {
            struct cn_msg cn_msg;
            struct proc_event proc_ev;
        };
    };

    int status = 0;
    nl_cn_msg netlink_connection_message = {};
    while (!need_exit) {
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

            std::ifstream pid_info;
            std::string command = "<unknown>";

            // Building path
            try {
                std::filesystem::path proc_path;
                proc_path.append("/proc");
                proc_path.append(std::to_string(pid));
                proc_path.append("comm");

                // std::cout << "DEBUG: Proc path: " << proc_path << std::endl;

                pid_info.open(proc_path);

                command.clear();
                pid_info >> command;
            } catch (const std::exception& e) {
                std::cerr << e.what() << std::endl;
            }


            queue->push(Process {
                .pid = pid,
                .name = command
            });
        }

    }

    return status;
}

void renicer_worker(atomic_queue<Process> *process_queue) {
    while (!need_exit) {
        std::unique_lock<std::mutex> lock(exit_mutex);
        exit_condition.wait_for(lock, 10ms);
        if (need_exit)
            break;

        while (process_queue->size() > 0) {
            Process pid = process_queue->pop();
        }
    }
}

/**
 * @throws std::runtime_error
 * @param path
 * @param rules
 */
void load_rules_from_file(const std::filesystem::path& path, std::unordered_map<std::string, json> *rules) {
    json rule;
    std::ifstream rule_file;
    rule_file.open(path);
    std::string line;

    while (rule_file.good()) {
        std::getline(rule_file, line);

        try {
            rule = json::parse(line);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing JSON: " << e.what() << std::endl;
        }

        if (rule.contains("name") && rule["name"].is_string()) {
            rules->insert_or_assign(rule["name"], rule);
        } else {
            std::cerr << "Rule does not have a name, or name is not a string! "  << rule.dump() << std::endl;
            // throw std::runtime_error("Rule does not have a name, or name is not a string!");
        }
    }


}

void on_sigint(int unused) {
    need_exit = true;
    exit_condition.notify_all();
}

int main() {
    std::cout << "Hello, World!" << std::endl;
    const int nl_sock = nl_connect();
    if (nl_sock == -1) {
        return EXIT_FAILURE;
    }

    signal(SIGINT, &on_sigint);
    signal(SIGTERM, &on_sigint);

    const int listen_status = listen_to_process_events(nl_sock, true);
    if (listen_status == 0) {
        close(nl_sock);
        return EXIT_FAILURE;
    }

    atomic_queue<Process> process_queue;
    std::unordered_map<std::string, json> rules;

    load_rules_from_file(std::filesystem::path("/home/aviallon/Programming/Personnal/ananicy-cpp/test.rules"), &rules);

    for (const std::pair<std::string, json> rule : rules) {
        std::cout << "Rule: " << rule.first << ", " << rule.second.dump() << std::endl;
    }

    std::jthread event_thread(&handle_events, nl_sock, &process_queue);

    while (!need_exit) {
        std::unique_lock<std::mutex> lock(exit_mutex);
        exit_condition.wait_for(lock, 5s);
        if (need_exit)
            break;

        std::stringstream ss;
        ss << "Queue length: " << process_queue.size() << std::endl;
        std::cout << ss.str() << std::flush;
    }
    event_thread.join();

    listen_to_process_events(nl_sock, false);
    close(nl_sock);

    std::stringstream ss;
    ss << "Processes: ";
    while (process_queue.size() > 0) {
        Process p = process_queue.pop();
        ss << p.name << "(" << p.pid << ")";
        if (process_queue.size() > 0) {
            ss << ", ";
            if (process_queue.size() % 4 == 0){
                ss << std::endl << "\t";
            }
        }
    }
    std::cout << ss.str() << std::endl;


    return EXIT_SUCCESS;
}
