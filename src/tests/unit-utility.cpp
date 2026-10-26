#include "doctest_compatibility.h"

#include "utility/argument_parser.hpp"
#include "utility/mounts.hpp"
#include "utility/process_info.hpp"
#include "utility/synchronized_queue.hpp"
#include "utility/utils.hpp"

#ifdef ENABLE_REGEX_SUPPORT
#include "utility/regex_utils.hpp"
#endif

#include <chrono>
#include <filesystem>

using namespace std::chrono_literals;

namespace fs = std::filesystem;

/* clang-format off */
TEST_CASE("Argument Parser")
{
  SECTION("Default Value")
  {
    argparse::argument_parser program("test");
    program.add_argument()
        .name("input")
        .type(typeid(std::string))
        .set_value("bar");

    std::optional<std::string> input = program["input"];

    REQUIRE(input.has_value());
    REQUIRE(input.value() == "bar");
  }

  SECTION("Parse unknown optional argument")
  {
    argparse::argument_parser bfm("bfm");
    bfm.add_argument().name("-l").name("--load").description(
        "load a VMM into the kernel");

    bfm.add_argument().name("-x").name("--start").description(
        "start a previously loaded VMM");

    bfm.add_argument().name("-d").name("--dump").description(
        "output the contents of the VMM's debug buffer");

    bfm.add_argument().name("-s").name("--stop").description(
        "stop a previously started VMM");

    bfm.add_argument()
        .name("-u")
        .name("--unload")
        .description("unload a previously loaded VMM");

    REQUIRE(bfm.parse_args({"./test", "-om"}));
    REQUIRE_THROWS_WITH_AS(bfm["-om"], "Unknown argument: -om",
                           std::runtime_error);
  }
  SECTION("Parse a string argument with value") {
    argparse::argument_parser program("test");
    program.add_argument().name("config");
    program.parse_args({"test", "--config", "config.yml"});

    std::optional<std::string> config = program["config"];

    REQUIRE(config.has_value());
    REQUIRE(config.value() == "config.yml");
  }
  SECTION("Parse a string argument without default value")
  {
    argparse::argument_parser program("test");
    program.add_argument().name("config").type(typeid(std::string));

    WHEN("no value provided") {
      program.parse_args({"test"});

      THEN("the option is nullopt") {
        std::optional<std::string> config = program["config"];
        REQUIRE_FALSE(config.has_value());
        REQUIRE(config == std::nullopt);
      }
    }

    WHEN("a value is provided") {
      program.parse_args({"test", "--config", ""});

      THEN("the option has a value") {
        std::optional<std::string> config = program["config"];
        REQUIRE(config.has_value());
        REQUIRE(config.value().empty());
      }
    }
  }
}

TEST_CASE("Mounts")
{
    static const std::string_view MTAB_SYSTEMD_SYSTEM_TEST = R"(
proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0

# test string
cgroup2 /sys/fs/cgroup cgroup2 rw,nosuid,nodev,noexec,relatime,nsdelegate,memory_recursiveprot 0 0
 # test something
systemd-1 /proc/sys/fs/binfmt_misc autofs rw,relatime,fd=39,pgrp=1,timeout=0,minproto=5,maxproto=5,direct,pipe_ino=4748 0 0
tmpfs /tmp tmpfs rw,nosuid,nodev,nr_inodes=1048576,inode64 0 0
run /run/firejail/firejail.ro.file tmpfs ro,nosuid,nodev,relatime,mode=755,inode64 0 0
)";
    static const std::string_view MTAB_OPENRC_SYSTEM_TEST = R"(
none /sys/fs/cgroup cgroup2 rw,nosuid,nodev,noexec,relatime,nsdelegate,memory_recursiveprot 0 0
)";


  SECTION("Running systemd system")
  {
    const auto& mounts = mounts::parse_mtab_content(MTAB_SYSTEMD_SYSTEM_TEST);
    REQUIRE_FALSE(mounts.empty());
    REQUIRE_EQ(mounts.size(), 5);

    REQUIRE_EQ(mounts[0].device, "proc");
    REQUIRE_EQ(mounts[0].mountpoint, "/proc");
    REQUIRE_EQ(mounts[0].fs_type, "proc");

    REQUIRE_EQ(mounts[1].device, "cgroup2");
    REQUIRE_EQ(mounts[1].mountpoint, "/sys/fs/cgroup");
    REQUIRE_EQ(mounts[1].fs_type, "cgroup2");

    REQUIRE_EQ(mounts[2].device, "systemd-1");
    REQUIRE_EQ(mounts[2].mountpoint, "/proc/sys/fs/binfmt_misc");
    REQUIRE_EQ(mounts[2].fs_type, "autofs");

    REQUIRE_EQ(mounts[3].device, "tmpfs");
    REQUIRE_EQ(mounts[3].mountpoint, "/tmp");
    REQUIRE_EQ(mounts[3].fs_type, "tmpfs");

    REQUIRE_EQ(mounts[4].device, "run");
    REQUIRE_EQ(mounts[4].mountpoint, "/run/firejail/firejail.ro.file");
    REQUIRE_EQ(mounts[4].fs_type, "tmpfs");
  }
  SECTION("Cgroups on openrc")
  {
    const auto& mounts = mounts::parse_mtab_content(MTAB_OPENRC_SYSTEM_TEST);
    REQUIRE_FALSE(mounts.empty());
    REQUIRE_EQ(mounts.size(), 1);

    REQUIRE_EQ(mounts[0].device, "none");
    REQUIRE_EQ(mounts[0].mountpoint, "/sys/fs/cgroup");
    REQUIRE_EQ(mounts[0].fs_type, "cgroup2");
  }
}

TEST_CASE("Process Info")
{
  SECTION("Get process info map")
  {
    REQUIRE_FALSE(process_info::get_process_info_map().empty());
  }
  SECTION("Get autogroup map")
  {
    REQUIRE_FALSE(process_info::get_autogroup_map().empty());
  }
}

TEST_CASE("Synchronized Queue")
{
  static constexpr size_t TEST_QNUM_ELEMENTS = 10;

#ifdef USE_EXPERIMENTAL_IMPL
  synchronized_queue<int32_t> test_queue{INT16_MAX};
#else
  synchronized_queue<int32_t> test_queue;
#endif

  SECTION("Basic test")
  {
    // Expect the queue is empty.
    REQUIRE(test_queue.size() == 0);

    // Push some elements.
    for (size_t i = 0; i < TEST_QNUM_ELEMENTS; ++i) {
      test_queue.push(i);
      REQUIRE_FALSE(test_queue.size() == 0);
      REQUIRE(test_queue.size() == (i + 1));
    }

    // Pull those elements.
    size_t exec_count = 0;
    for (size_t i = 0; i < TEST_QNUM_ELEMENTS; ++i) {
      const auto element = test_queue.poll(500ms);
      REQUIRE(element.has_value());
      REQUIRE(element == i);
      ++exec_count;
    }

    // Expect equal number of elements.
    REQUIRE(exec_count == TEST_QNUM_ELEMENTS);

    // Expect the queue is empty.
    REQUIRE(test_queue.size() == 0);

    // Push one more element.
    test_queue.push(TEST_QNUM_ELEMENTS);

    {
      const auto element = test_queue.poll(500ms);
      REQUIRE(element.has_value());
      REQUIRE(element == TEST_QNUM_ELEMENTS);
      ++exec_count;
    }

    // Expect one more element.
    REQUIRE(exec_count == (TEST_QNUM_ELEMENTS + 1));

    // Expect the queue is empty.
    REQUIRE(test_queue.size() == 0);
  }
}

TEST_CASE("Utils") {
  const std::string testfile = R"(## Ananicy 2.X configuration
# Ananicy run full system scan every "check_freq" seconds
# supported values 0.01..86400
# values which have sense: 1..60
check_freq=5

# Verbose msg: true/false
cgroup_load=true
type_load=true
rule_load=true

apply_nice=true
apply_ioclass=true
apply_ionice=true
apply_sched=true
apply_oom_score_adj=true
apply_cgroup=true

check_disks_schedulers=true
)";

  SECTION("Get Environment") {
    const auto env_pwd = get_env("PWD");
    REQUIRE(env_pwd.has_value());
    REQUIRE(env_pwd.value() == fs::current_path().string());
  }
  SECTION("Get error string") {
    static constexpr auto TESTCASE_ERR_CODE = 2;
    const auto            err_msg = get_error_string(TESTCASE_ERR_CODE);
    REQUIRE_FALSE(err_msg.empty());
    REQUIRE(err_msg == std::string_view{strerror(TESTCASE_ERR_CODE)});
  }
  SECTION("Read file") {
    REQUIRE(read_file(TEST_DIR "test-readfile.txt") == testfile);
  }
  SECTION("Convert to int") {
    REQUIRE(to_int<int8_t>("500") == 0);
    REQUIRE(to_int<int8_t>("2") == 2);
    REQUIRE(to_int<uint8_t>("-5") == 0);
    REQUIRE(to_int<int32_t>("1234") == 1234);
    REQUIRE(to_int<int32_t>("15 foo") == 15);
    REQUIRE(to_int<int32_t>("40") == 40);
    REQUIRE(to_int<int32_t>("5000000000") == 0);
    REQUIRE(to_int<uint32_t>("-500") == 0);
    REQUIRE(to_int<int64_t>("5000000000") == 5000000000);
    REQUIRE(to_int<uint64_t>("-5000000000") == 0);
  }

#ifdef ENABLE_REGEX_SUPPORT
  SECTION("Matching regex") {
    REQUIRE(utils::regex::is_str_matching_regex("gcc-aarch64", "gcc-.*") == true);
    REQUIRE(utils::regex::is_str_matching_regex("gcc-aarch64", "gfc-.*") == false);
  }
#endif
}
/* clang-format on */
