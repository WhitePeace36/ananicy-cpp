#define _XOPEN_SOURCE 700 // NOLINT(bugprone-reserved-identifier)

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
#include "utility/debug.hpp"
#include "utility/process_info.hpp"
#include "utility/utils.hpp"
#include "version.hpp"
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>

using namespace std::chrono_literals;

using std::filesystem::path;

int main(const int argc, const char **argv) {
  if constexpr (std::string_view(BUILD_TYPE) == "Debug") {
    spdlog::set_level(spdlog::level::trace);
  } else {
    spdlog::set_level(spdlog::level::info);
  }

  auto start_time = std::chrono::system_clock::now();

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

  parser.add_argument().name("action").description(
      "What action to perform. Valid actions are:\n"
      "dump [sub-action]\n"
      "start");

  parser.add_argument()
      .name("sub-action")
      .description("What sub-action to perform. Valid sub-actions are:\n"
                   "rules, types, cgroups, proc, autogroup\n");

  argparse::argparse_error err = parser.parse_args(argc, argv);
  if (err) {
    spdlog::critical("Argument parsing error: {}", err.what());
    parser.print_help();
    return EXIT_FAILURE;
  }

  const std::string config_dir_path =
      get_env("ANANICY_CPP_CONFDIR").value_or("/etc/ananicy.d");
  const std::string config_path =
      get_env("ANANICY_CPP_CONF").value_or("/etc/ananicy.d/ananicy.conf");

  Config conf(config_path);
  conf.show();

  spdlog::set_level(conf.loglevel());

  if (parser["help"]) {
    parser.print_help();
    return EXIT_SUCCESS;
  }

  if (parser["verbose"]) {
    spdlog::debug("Verbose flag set!");
    const int loglevel =
        std::max(0, (spdlog::get_level() - 1)) % spdlog::level::n_levels;
    spdlog::set_level(static_cast<spdlog::level::level_enum>(loglevel));
  }

  if (parser["benchmark"]) {
    spdlog::warn("Benchmark enabled!");
  }

  if (parser["manual-scanning"]) {
    spdlog::debug("Manual scanning enabled! Increasing Ananicy Nice value to "
                  "prevent lag.");
    priority::set_priority(get_pid(), 19);
  }

  service::set_status(service::STARTING);

  constexpr std::string_view version(VERSION);

  std::cout << std::format("Ananicy Cpp {}", version) << std::endl;

  InterruptHandler ih({SIGINT, SIGTERM});

  Rules rules(config_dir_path, &conf);

  const std::optional<std::string> action = parser["action"];
  const std::optional<std::string> sub_action = parser["sub-action"];
  if (!action.has_value()) {
    spdlog::critical("No action requested!");
    parser.print_help();
    std::exit(EXIT_FAILURE);
  } else if (action == "start") {
    spdlog::info("Starting Ananicy");
  } else if (action == "dump") {
    if (!sub_action.has_value()) {
      spdlog::error("A sub-action must be specified for {}.", action.value());
      std::exit(EXIT_FAILURE);
    } else if (sub_action == "rules") {
      rules.show_rules(Rules::rules);
    } else if (sub_action == "types") {
      rules.show_rules(Rules::types);
    } else if (sub_action == "cgroups") {
      rules.show_rules(Rules::cgroups);
    } else if (sub_action == "proc") {
      std::cout << process_info::get_process_info_map().dump(4) << std::endl;
    } else if (sub_action == "autogroup") {
      std::cout << process_info::get_autogroup_map().dump(4) << std::endl;
    } else {
      spdlog::critical("Unknown sub-action for dump: {}", sub_action.value());
      std::exit(EXIT_FAILURE);
    }
    std::exit(EXIT_SUCCESS);
  } else if (action == "debug") {

    if (!sub_action.has_value()) {
      spdlog::error("A sub-action must be specified for {}.", action.value());
      std::exit(EXIT_FAILURE);
    } else if (sub_action == "cgroups") {
      ananicy_debug::print_debug_for_issue<21>();
    }
    std::exit(EXIT_SUCCESS);
  } else {
    spdlog::critical("Unknown action requested: {}", action.value());
  }

  rules.show_all_rules();

  control_groups::get_cgroup_version();
  spdlog::info("Creating Cgroups...");
  rules.create_cgroups();

  ProcessQueue processListener;

  processListener.start();

  Worker worker(&rules, &conf, &processListener.process_queue);

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
  spdlog::info("Stopping Ananicy Cpp...");
  service::set_status(service::STOPPING);

  processListener.stop();
  spdlog::debug("Stopped process listener");

  worker.stop();
  spdlog::debug("Stopped process worker");

  ih.stop();

  std::chrono::duration<double, std::ratio<1, 1>> duration =
      std::chrono::system_clock::now() - start_time;

  spdlog::info("Summary:\n{} processes processed, ran for {:%H:%M:%S} seconds",
               worker.processed_processes(), duration);

  return EXIT_SUCCESS;
}
