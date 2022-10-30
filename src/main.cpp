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
#include "utility/singleton_process.hpp"
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
  if constexpr (BUILD_TYPE == "Debug") {
    spdlog::set_level(spdlog::level::trace);
  } else {
    spdlog::set_level(spdlog::level::info);
  }

  const auto start_time = std::chrono::system_clock::now();

  argparse::argument_parser parser(
      "ananicy-cpp" /*, "ANother Auto NIce daemon, fully written in C++"*/);

  parser.add_argument()
      .name("--verbose")
      .name("-v")
      .description("Increase verbosity level");
  parser.add_argument().name("--help").name("-h").description(
      "Print this help and exit");
  parser.add_argument().name("--version").description(
      "Print version information");
  parser.add_argument().name("--force-remove-semaphore").description(
      "Don't use it, unless you know what you are doing.");
  parser.add_argument()
      .name("--benchmark")
      .description("Should we activate benchmark mode or not");
  parser.add_argument()
      .name("--benchmark-count")
      .type(typeid(uint))
      .description("How many pids to process before stopping");

#ifdef USE_BPF_PROC_IMPL
  parser.add_argument()
      .name("--bpf-min-us")
      .type(typeid(uint64_t))
      .description("Trace latency");
#endif

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

  if (parser["help"] || (argc < 2)) {
    parser.print_help();
    return EXIT_SUCCESS;
  }

  if (parser["version"]) {
    std::cout << std::format("Ananicy Cpp {}\n", VERSION);
    return EXIT_SUCCESS;
  }

  SingletonProcess singleton("/AnanicyCppMutex");
  if (parser["force-remove-semaphore"]) {
    if (!SingletonProcess::remove(singleton.get_name())) {
        std::cerr << std::format("Failed to remove semaphore! msg '{}'\n", strerror(errno));
        return EXIT_FAILURE;
    }
    std::cerr << "Semaphore was successfully removed!\n";
    return EXIT_SUCCESS;
  }

  const std::string config_dir_path =
      get_env("ANANICY_CPP_CONFDIR").value_or("/etc/ananicy.d");
  const std::string config_path =
      get_env("ANANICY_CPP_CONF").value_or("/etc/ananicy.d/ananicy.conf");

  Config conf(config_path);
  conf.show();

  spdlog::set_level(conf.loglevel());


  if (parser["verbose"]) {
    spdlog::debug("Verbose flag set!");
    const int loglevel =
        std::max(0, (spdlog::get_level() - 1)) % spdlog::level::n_levels;
    spdlog::set_level(static_cast<spdlog::level::level_enum>(loglevel));
  }

  if (parser["benchmark"]) {
    spdlog::warn("Benchmark enabled!");
  }
  std::optional<uint> benchmark_count = parser["benchmark-count"].value<uint>();
  if (benchmark_count.has_value()) {
    spdlog::warn("Benchmark count: {}", benchmark_count.value());
  }

  if (parser["manual-scanning"]) {
    spdlog::info("Manual scanning enabled! Increasing Ananicy Nice value to "
                 "prevent lag.");
    priority::set_priority(get_pid(), 19);

    spdlog::info("Checking frequency set to {}", conf.check_freq());
  }

  std::cout << std::format("Ananicy Cpp {}\n", VERSION) << std::flush;

  InterruptHandler ih({SIGINT, SIGTERM});

  Rules rules(config_dir_path, &conf);

  const std::optional<std::string> action = parser["action"];
  const std::optional<std::string> sub_action = parser["sub-action"];
  if (!action.has_value()) {
    spdlog::critical("No action requested!");
    parser.print_help();
    std::exit(EXIT_FAILURE);
  } else if (action == "start") {
    service::set_status(service::STARTING);
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
    spdlog::set_level(spdlog::level::trace);
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

  if (!singleton.try_create()) {
    std::cerr << "Ananicy Cpp is already running!\n";
    ih.stop();
    return EXIT_FAILURE;
  }

  rules.show_all_rules();

  control_groups::get_cgroup_version();
  rules.create_cgroups();

#ifdef USE_BPF_PROC_IMPL
  const uint64_t bpf_min_us = parser["bpf-min-us"].value<uint64_t>().value_or(30);
  ProcessQueue processListener(bpf_min_us, parser["verbose"]);
#else
  ProcessQueue processListener;
#endif

  processListener.start();

  Worker worker(&rules, &conf, &processListener.process_queue);

  worker.start();

  if (conf.cgroup_realtime_workaround()) {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(100ms);
    spdlog::debug("Cgroup realtime workaround requested, re-checking cgroups");
    control_groups::get_cgroup_version(true);
    rules.create_cgroups();
  }

  service::set_status(service::STARTED);

  while (!ih.should_exit()) {
    if (parser["benchmark"]) {
      std::this_thread::sleep_for(30s);
    } else
      ih.wait_for(conf.check_freq());

    if (ih.should_exit() || parser["benchmark"] ||
        (benchmark_count.has_value() &&
         worker.processed_processes() >= benchmark_count))
      break;

    if (parser["manual-scanning"]) {
      spdlog::info("Starting full-scan");
      processListener.full_scan();
    }
    spdlog::debug("Processed processes: {}", worker.processed_processes());
  }
  spdlog::info("Stopping Ananicy Cpp...");
  service::set_status(service::STOPPING);

  SingletonProcess::remove(singleton.get_name());

  processListener.stop();
  spdlog::debug("Stopped process listener");

  worker.stop();
  spdlog::debug("Stopped process worker");

  ih.stop();

  const std::chrono::duration<double, std::ratio<1, 1>> duration =
      std::chrono::system_clock::now() - start_time;

  spdlog::info("Summary:\n{} processes processed, ran for {:%H:%M:%S} seconds",
               worker.processed_processes(), duration);

  return EXIT_SUCCESS;
}
