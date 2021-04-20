#define _XOPEN_SOURCE 700

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <fmt/chrono.h>

#include <iostream>
#include <csignal>
#include <mutex>
#include <queue>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "utils.hpp"
#include "process_listener.hpp"
#include "rules.hpp"
#include "worker.hpp"
#include "config.hpp"




using json = nlohmann::json;


using namespace std::chrono_literals;

using std::filesystem::path;

int main() {
    spdlog::set_level(spdlog::level::debug);

    auto start_time = std::chrono::system_clock::now();

    std::cout << "Ananicy Cpp" << std::endl;

    InterruptHandler ih ({
        SIGINT,
        SIGTERM
    });

    Config conf("/etc/ananicy.d/ananicy.conf");
    conf.show();

    ProcessListener processListener(&ih);

    processListener.start();

    Rules rules("/etc/ananicy.d", &conf);

    // rules.load_rules_from_file(path("/etc/ananicy.d/00-cgroups.cgroups"));

    // load_rules_from_file(std::filesystem::path("/home/aviallon/Programming/Personnal/ananicy-cpp/test.program_rules"), &program_rules);

    rules.show_rules();

    Worker worker(&ih, &rules, &conf, &processListener.process_queue);

    worker.start();


    while (!ih.should_exit()) {
        ih.wait_for(1min);
        if (ih.should_exit())
            break;

        spdlog::debug("Processed processes: {}", worker.processed_processes());
    }

    std::chrono::duration<double, std::ratio<1,1>> duration = std::chrono::system_clock::now() - start_time;

    spdlog::info("Summary:\n{} processes processed, ran for {:%H:%M:%S} seconds", worker.processed_processes(), duration);


    return EXIT_SUCCESS;
}
