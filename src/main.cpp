#define _XOPEN_SOURCE 700

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "config.hpp"
#include "process.hpp"
#include "rules.hpp"
#include "utils.hpp"
#include "worker.hpp"
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <argparse.h>

#ifdef ENABLE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

using json = nlohmann::json;

using namespace std::chrono_literals;

using std::filesystem::path;

int main(int argc, char **argv) {
  spdlog::set_level(spdlog::level::debug);

  auto start_time = std::chrono::system_clock::now();

  argparse::ArgumentParser parser("ananicy-cpp", "ANother Auto NIce daemon, fully written in C++");
  parser.add_argument().names({"-v", "--verbose"}).description("Increase verbosity level");
  parser.add_argument().names({"-h", "--help"}).description("Print this help and exit");
  parser.add_argument().name("--benchmark").description("Should we activate benchmark mode or not");

  auto err = parser.parse(argc, argv);
  if (err) {
      spdlog::critical("Argument parsing error: {}", err.what());
      parser.print_help();
      return EXIT_FAILURE;
  }

  if (parser.exists("help")) {
      parser.print_help();
      return EXIT_SUCCESS;
  }

  if (parser.exists("verbose")){
      spdlog::info("Verbose flag set!");
  }

  if (parser.exists("benchmark")) {
      spdlog::warn("Benchmark enabled!");
  }


  std::cout << "Ananicy Cpp" << std::endl;

  InterruptHandler ih({SIGINT, SIGTERM});

  const std::string config_dir_path =
      get_env("ANANICY_CPP_CONFDIR").value_or("/etc/ananicy.d");
  const std::string config_path =
      get_env("ANANICY_CPP_CONF").value_or("/etc/ananicy.d/ananicy.conf");

  Config conf(config_path);
  conf.show();

  ProcessQueue processListener(&ih);

  processListener.start();

  Rules rules(config_dir_path, &conf);

  rules.show_rules();

  Worker worker(&ih, &rules, &conf, &processListener.process_queue);

  worker.start();

#ifdef ENABLE_SYSTEMD
  sd_notify(0, "READY=1");
#endif

  while (!ih.should_exit()) {
    ih.wait_for(1min);
    if (ih.should_exit() || parser.exists("benchmark"))
      break;

    spdlog::debug("Processed processes: {}", worker.processed_processes());
  }
  ih.stop();

  std::chrono::duration<double, std::ratio<1, 1>> duration =
      std::chrono::system_clock::now() - start_time;

  spdlog::info("Summary:\n{} processes processed, ran for {:%H:%M:%S} seconds",
               worker.processed_processes(), duration);

  return EXIT_SUCCESS;
}
