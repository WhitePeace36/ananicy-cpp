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
#include "utils.hpp"
#include "process_listener.hpp"

using json = nlohmann::json;

using namespace std::chrono_literals;

void renicer_worker(atomic_queue<Process> *process_queue, InterruptHandler* interruptHandler) {
    while (!interruptHandler->should_exit()) {
        interruptHandler->wait_for(10ms);
        if (interruptHandler->should_exit())
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

int main() {
    std::cout << "Ananicy Cpp" << std::endl;

    InterruptHandler ih ({
        SIGINT,
        SIGTERM
    });

    ProcessListener processListener(&ih);

    processListener.start();

    std::unordered_map<std::string, json> rules;

    // load_rules_from_file(std::filesystem::path("/home/aviallon/Programming/Personnal/ananicy-cpp/test.rules"), &rules);

    for (const std::pair<std::string, json> rule : rules) {
        std::cout << "Rule: " << rule.first << ", " << rule.second.dump() << std::endl;
    }

    while (!ih.need_exit) {
        ih.wait_for(5s);
        if (ih.need_exit)
            break;

        std::stringstream ss;
        ss << "Queue length: " << processListener.process_queue.size() << std::endl;
        std::cout << ss.str() << std::flush;
    }

    std::stringstream ss;
    ss << "Processes: ";
    while (processListener.process_queue.size() > 0) {
        Process p = processListener.process_queue.pop();
        ss << p.name << "(" << p.pid << ")";
        if (processListener.process_queue.size() > 0) {
            ss << ", ";
            if (processListener.process_queue.size() % 4 == 0){
                ss << std::endl << "\t";
            }
        }
    }
    std::cout << ss.str() << std::endl;


    return EXIT_SUCCESS;
}
