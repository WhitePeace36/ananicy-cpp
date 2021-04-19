#define _XOPEN_SOURCE 700

#include <iostream>
#include <csignal>
#include <mutex>
#include <queue>
#include <thread>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "utils.hpp"
#include "process_listener.hpp"
#include "Rules.hpp"

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

using std::filesystem::path;

int main() {
    std::cout << "Ananicy Cpp" << std::endl;

    InterruptHandler ih ({
        SIGINT,
        SIGTERM
    });

    ProcessListener processListener(&ih);

    processListener.start();

    Rules rules("/etc/ananicy.d");

    rules.load_rules_from_file(path("/etc/ananicy.d/00-cgroups.cgroups"));

    // load_rules_from_file(std::filesystem::path("/home/aviallon/Programming/Personnal/ananicy-cpp/test.program_rules"), &program_rules);

    rules.show_rules();


    while (!ih.should_exit()) {
        ih.wait_for(5s);
        if (ih.should_exit())
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
