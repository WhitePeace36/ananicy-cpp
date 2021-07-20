#define _XOPEN_SOURCE 700

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <format>
#include <spdlog/spdlog.h>

#include "config.hpp"
#include "core/cgroups.hpp"
#include "core/priority.hpp"
#include "core/process.hpp"
#include "core/rules.hpp"
#include "core/worker.hpp"
#include "service.hpp"
#include "utility/argument_parser.hpp"
#include "utility/utils.hpp"
#include "version.hpp"
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

using namespace std::chrono_literals;

using std::filesystem::path;

int main(const int argc, const char **argv) {
  if constexpr (std::string_view(BUILD_TYPE) == "Debug") {
    spdlog::set_level(spdlog::level::debug);
  } else {
    spdlog::set_level(spdlog::level::info);
  }

  auto start_time = std::chrono::system_clock::now();

  service::set_status(service::STARTING);

  argparse::argument_parser parser(
      "ananicy-cpp" /*, "ANother Auto NIce daemon, fully written in C++"*/);

  parser.add_argument()
      .name("--verbose")
      .name("-v")
      .description("Increase verbosity level");
  parser.add_argument().name("--help").name("-h").description(
      "Print this help and exit");
  parser.add_argument()
      .name("--benchmark")
      .description("Should we activate benchmark mode or not");

  parser.add_argument()
      .name("--manual-scanning")
      .name("--manualscanning")
      .description(
          "Scan for all processes at a regular interval by reading the "
          "procfs.\n"
          "Not recommended unless Ananicy-Cpp doesn't work as expected.");

  argparse::argparse_error err = parser.parse_args(argc, argv);
  if (err) {
    spdlog::critical("Argument parsing error: {}", err.what());
    parser.print_help();
    return EXIT_FAILURE;
  }

  if (parser["help"]) {
    parser.print_help();
    return EXIT_SUCCESS;
  }

  if (parser["verbose"]) {
    spdlog::debug("Verbose flag set!");
    spdlog::set_level(spdlog::level::debug);
  }

  if (parser["benchmark"]) {
    spdlog::warn("Benchmark enabled!");
  }

  if (parser["manual-scanning"]) {
    spdlog::debug("Manual scanning enabled! Increasing Ananicy Nice value to "
                  "prevent lag.");
    set_priority(get_pid(), 19);
  }

  std::cout << std::format("Ananicy Cpp v{}.{}.{}", MAJOR_VERSION,
                           MINOR_VERSION, PATCH_VERSION)
            << std::endl;

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

  control_groups::get_cgroup_version();
  spdlog::info("Creating Cgroups...");
  rules.create_cgroups();

  Worker worker(&ih, &rules, &conf, &processListener.process_queue);

  worker.start();

  service::set_status(service::STARTED);

  while (!ih.should_exit()) {
    ih.wait_for(1min);
    if (ih.should_exit() || parser["benchmark"])
      break;

    if (parser["manual-scanning"]) {
      spdlog::info("Starting full-scan");
      processListener.full_scan();
    }
    spdlog::debug("Processed processes: {}", worker.processed_processes());
  }
  service::set_status(service::STOPPING);
  worker.stop();
  processListener.stop();
  ih.stop();

  std::chrono::duration<double, std::ratio<1, 1>> duration =
      std::chrono::system_clock::now() - start_time;

  spdlog::info("Summary:\n{} processes processed, ran for {:%H:%M:%S} seconds",
               worker.processed_processes(), duration);

  return EXIT_SUCCESS;
}
