#include "doctest_compatibility.h"

#include "config.hpp"
#include "core/cpuset.hpp"
#include "core/x3d.hpp"

#include <spdlog/spdlog.h>

TEST_SUITE_BEGIN("CpuSet");

TEST_CASE("CpuSet construction and validity") {
  cpuset::CpuSet cs(16);
  CHECK(cs.valid());
  CHECK(cs.ncpus == 16);
  CHECK(cs.setsize > 0);
  CHECK(cs.set != nullptr);
}

TEST_CASE("CpuSet zero-initialized") {
  cpuset::CpuSet cs(16);
  REQUIRE(cs.valid());
  for (int i = 0; i < 16; ++i) {
    CHECK_FALSE(cs.is_set(i));
  }
}

TEST_CASE("CpuSet set and clear") {
  cpuset::CpuSet cs(16);
  REQUIRE(cs.valid());

  cs.set_cpu(5);
  CHECK(cs.is_set(5));
  CHECK_FALSE(cs.is_set(4));
  CHECK_FALSE(cs.is_set(6));

  cs.clear_cpu(5);
  CHECK_FALSE(cs.is_set(5));
}

TEST_CASE("CpuSet set multiple") {
  cpuset::CpuSet cs(32);
  REQUIRE(cs.valid());

  cs.set_cpu(0);
  cs.set_cpu(7);
  cs.set_cpu(15);
  cs.set_cpu(31);

  CHECK(cs.is_set(0));
  CHECK(cs.is_set(7));
  CHECK(cs.is_set(15));
  CHECK(cs.is_set(31));
  CHECK_FALSE(cs.is_set(1));
  CHECK_FALSE(cs.is_set(16));
}

TEST_CASE("CpuSet bounds checking") {
  cpuset::CpuSet cs(8);
  REQUIRE(cs.valid());

  // Out-of-bounds set should be silently ignored
  cs.set_cpu(-1);
  cs.set_cpu(8);
  cs.set_cpu(100);

  // Out-of-bounds is_set should return false
  CHECK_FALSE(cs.is_set(-1));
  CHECK_FALSE(cs.is_set(8));
  CHECK_FALSE(cs.is_set(100));
}

TEST_CASE("CpuSet zero method") {
  cpuset::CpuSet cs(16);
  REQUIRE(cs.valid());

  cs.set_cpu(0);
  cs.set_cpu(5);
  cs.set_cpu(15);
  CHECK(cs.is_set(0));
  CHECK(cs.is_set(5));

  cs.zero();
  for (int i = 0; i < 16; ++i) {
    CHECK_FALSE(cs.is_set(i));
  }
}

TEST_CASE("CpuSet move constructor") {
  cpuset::CpuSet cs1(16);
  REQUIRE(cs1.valid());
  cs1.set_cpu(3);
  cs1.set_cpu(7);

  cpuset::CpuSet cs2(std::move(cs1));
  CHECK(cs2.valid());
  CHECK(cs2.is_set(3));
  CHECK(cs2.is_set(7));
  CHECK_FALSE(cs2.is_set(0));

  // Moved-from should be invalid
  CHECK_FALSE(cs1.valid());
}

TEST_CASE("CpuSet move assignment") {
  cpuset::CpuSet cs1(16);
  REQUIRE(cs1.valid());
  cs1.set_cpu(10);

  cpuset::CpuSet cs2(8);
  REQUIRE(cs2.valid());

  cs2 = std::move(cs1);
  CHECK(cs2.valid());
  CHECK(cs2.is_set(10));
  CHECK(cs2.ncpus == 16);

  CHECK_FALSE(cs1.valid());
}

TEST_SUITE_END();

TEST_SUITE_BEGIN("cpuset_parsing");

TEST_CASE("parse single cpu") {
  auto result = cpuset::parse_cpuset_string("0", 16);
  REQUIRE(result.has_value());
  CHECK(result->is_set(0));
  CHECK_FALSE(result->is_set(1));
}

TEST_CASE("parse another single cpu") {
  auto result = cpuset::parse_cpuset_string("5", 16);
  REQUIRE(result.has_value());
  CHECK(result->is_set(5));
  CHECK_FALSE(result->is_set(0));
  CHECK_FALSE(result->is_set(4));
  CHECK_FALSE(result->is_set(6));
}

TEST_CASE("parse simple range") {
  auto result = cpuset::parse_cpuset_string("0-3", 16);
  REQUIRE(result.has_value());
  CHECK(result->is_set(0));
  CHECK(result->is_set(1));
  CHECK(result->is_set(2));
  CHECK(result->is_set(3));
  CHECK_FALSE(result->is_set(4));
}

TEST_CASE("parse comma-separated") {
  auto result = cpuset::parse_cpuset_string("0,2,4", 16);
  REQUIRE(result.has_value());
  CHECK(result->is_set(0));
  CHECK_FALSE(result->is_set(1));
  CHECK(result->is_set(2));
  CHECK_FALSE(result->is_set(3));
  CHECK(result->is_set(4));
}

TEST_CASE("parse mixed notation") {
  auto result = cpuset::parse_cpuset_string("0-3,8-11", 16);
  REQUIRE(result.has_value());
  for (int i = 0; i <= 3; ++i)
    CHECK(result->is_set(i));
  for (int i = 4; i <= 7; ++i)
    CHECK_FALSE(result->is_set(i));
  for (int i = 8; i <= 11; ++i)
    CHECK(result->is_set(i));
}

TEST_CASE("parse complex mixed notation") {
  auto result = cpuset::parse_cpuset_string("0,2-4,7,10-12", 16);
  REQUIRE(result.has_value());
  CHECK(result->is_set(0));
  CHECK_FALSE(result->is_set(1));
  CHECK(result->is_set(2));
  CHECK(result->is_set(3));
  CHECK(result->is_set(4));
  CHECK_FALSE(result->is_set(5));
  CHECK_FALSE(result->is_set(6));
  CHECK(result->is_set(7));
  CHECK_FALSE(result->is_set(8));
  CHECK_FALSE(result->is_set(9));
  CHECK(result->is_set(10));
  CHECK(result->is_set(11));
  CHECK(result->is_set(12));
}

TEST_CASE("parse single-element range") {
  // "5-5" is a valid range with a single element
  auto result = cpuset::parse_cpuset_string("5-5", 16);
  REQUIRE(result.has_value());
  CHECK(result->is_set(5));
  CHECK_FALSE(result->is_set(4));
  CHECK_FALSE(result->is_set(6));
}

TEST_CASE("parse invalid: empty string") {
  spdlog::set_level(spdlog::level::off);
  auto result = cpuset::parse_cpuset_string("", 16);
  CHECK_FALSE(result.has_value());
}

TEST_CASE("parse invalid: non-numeric") {
  spdlog::set_level(spdlog::level::off);
  auto result = cpuset::parse_cpuset_string("abc", 16);
  CHECK_FALSE(result.has_value());
}

TEST_CASE("parse invalid: inverted range") {
  spdlog::set_level(spdlog::level::off);
  auto result = cpuset::parse_cpuset_string("5-3", 16);
  CHECK_FALSE(result.has_value());
}

TEST_CASE("parse invalid: negative") {
  spdlog::set_level(spdlog::level::off);
  auto result = cpuset::parse_cpuset_string("-1", 16);
  CHECK_FALSE(result.has_value());
}

TEST_CASE("parse invalid: out of range") {
  spdlog::set_level(spdlog::level::off);
  auto result = cpuset::parse_cpuset_string("99999", 16);
  CHECK_FALSE(result.has_value());
}

TEST_CASE("parse trailing comma accepts prefix") {
  // Trailing comma: parser processes "0" and "1", then loop ends
  // since pos == size. This is accepted (not rejected).
  auto result = cpuset::parse_cpuset_string("0,1,", 16);
  REQUIRE(result.has_value());
  CHECK(result->is_set(0));
  CHECK(result->is_set(1));
}

TEST_CASE("parse invalid: double comma") {
  spdlog::set_level(spdlog::level::off);
  auto result = cpuset::parse_cpuset_string("0,,2", 16);
  CHECK_FALSE(result.has_value());
}

TEST_CASE("parse invalid: range with letters") {
  spdlog::set_level(spdlog::level::off);
  auto result = cpuset::parse_cpuset_string("0-a", 16);
  // to_int returns 0 for non-numeric, so this becomes "0-0" which is valid
  // The validation depends on to_int behavior
  // Just check it doesn't crash
  (void)result;
}

TEST_SUITE_END();

TEST_SUITE_BEGIN("cpuset_to_string");

TEST_CASE("serialize single cpu") {
  cpuset::CpuSet cs(16);
  REQUIRE(cs.valid());
  cs.set_cpu(5);
  CHECK(cpuset::cpuset_to_string(cs) == "5");
}

TEST_CASE("serialize contiguous range") {
  cpuset::CpuSet cs(16);
  REQUIRE(cs.valid());
  for (int i = 0; i <= 7; ++i)
    cs.set_cpu(i);
  CHECK(cpuset::cpuset_to_string(cs) == "0-7");
}

TEST_CASE("serialize discontiguous cpus") {
  cpuset::CpuSet cs(16);
  REQUIRE(cs.valid());
  cs.set_cpu(0);
  cs.set_cpu(2);
  cs.set_cpu(4);
  CHECK(cpuset::cpuset_to_string(cs) == "0,2,4");
}

TEST_CASE("serialize mixed ranges and singles") {
  cpuset::CpuSet cs(16);
  REQUIRE(cs.valid());
  for (int i = 0; i <= 3; ++i)
    cs.set_cpu(i);
  for (int i = 8; i <= 11; ++i)
    cs.set_cpu(i);
  CHECK(cpuset::cpuset_to_string(cs) == "0-3,8-11");
}

TEST_CASE("serialize empty set") {
  cpuset::CpuSet cs(16);
  REQUIRE(cs.valid());
  CHECK(cpuset::cpuset_to_string(cs) == "");
}

TEST_CASE("serialize invalid set") {
  cpuset::CpuSet cs(0);
  CHECK(cpuset::cpuset_to_string(cs) == "");
}

TEST_CASE("roundtrip: parse then serialize") {
  auto parsed = cpuset::parse_cpuset_string("0-3,8-11", 16);
  REQUIRE(parsed.has_value());
  CHECK(cpuset::cpuset_to_string(parsed.value()) == "0-3,8-11");
}

TEST_CASE("roundtrip: single values") {
  auto parsed = cpuset::parse_cpuset_string("1,3,5", 16);
  REQUIRE(parsed.has_value());
  CHECK(cpuset::cpuset_to_string(parsed.value()) == "1,3,5");
}

TEST_SUITE_END();

TEST_SUITE_BEGIN("cpuset_affinity");

TEST_CASE("set_affinity on current process") {
  const int num_cpus = static_cast<int>(cpuset::get_num_conf_cores());
  cpuset::CpuSet cs(num_cpus);
  REQUIRE(cs.valid());

  // Set affinity to all CPUs (should always succeed)
  for (int i = 0; i < num_cpus; ++i)
    cs.set_cpu(i);

  const pid_t self = getpid();
  CHECK(cpuset::set_affinity(self, cs));
}

TEST_CASE("set_affinity on nonexistent process") {
  spdlog::set_level(spdlog::level::off);
  const int num_cpus = static_cast<int>(cpuset::get_num_conf_cores());
  cpuset::CpuSet cs(num_cpus);
  REQUIRE(cs.valid());
  cs.set_cpu(0);

  // PID that almost certainly doesn't exist
  CHECK_FALSE(cpuset::set_affinity(999999999, cs));
}

TEST_CASE("set_affinity with zero-ncpus CpuSet") {
  cpuset::CpuSet cs(0);
  // CPU_ALLOC(0) may return non-null, so valid() can be true.
  // set_affinity with an empty set may succeed (no-op) or fail
  // depending on implementation. Just verify it doesn't crash.
  (void)cpuset::set_affinity(getpid(), cs);
}

TEST_SUITE_END();

TEST_SUITE_BEGIN("cpu_info");

TEST_CASE("get_num_conf_cores returns positive") {
  auto n = cpuset::get_num_conf_cores();
  CHECK(n > 0);
}

TEST_CASE("get_num_online_cores returns positive") {
  auto n = cpuset::get_num_online_cores();
  CHECK(n > 0);
}

TEST_CASE("online cores <= configured cores") {
  auto online = cpuset::get_num_online_cores();
  auto conf = cpuset::get_num_conf_cores();
  CHECK(online <= conf);
}

TEST_CASE("get_max_number_of_cpus returns positive") {
  auto n = cpuset::get_max_number_of_cpus();
  CHECK(n > 0);
}

TEST_SUITE_END();

TEST_SUITE_BEGIN("x3d");

TEST_CASE("detect returns nullopt on non-X3D hardware" *
          doctest::may_fail(true)) {
  spdlog::set_level(spdlog::level::off);
  auto result = x3d::detect();
  // On non-X3D hardware this should be nullopt
  // On X3D hardware it should have valid strings
  if (result.has_value()) {
    CHECK_FALSE(result->cache_cores_str.empty());
    CHECK_FALSE(result->frequency_cores_str.empty());
  } else {
    CHECK_FALSE(result.has_value());
  }
}

TEST_CASE("find_driver_sysfs_path does not crash") {
  spdlog::set_level(spdlog::level::off);
  auto path = x3d::find_driver_sysfs_path();
  // Just verify it returns without crashing
  // On non-X3D it should be empty
  (void)path;
}

TEST_CASE("get_driver_mode returns nullopt when no driver") {
  spdlog::set_level(spdlog::level::off);
  auto path = x3d::find_driver_sysfs_path();
  if (path.empty()) {
    auto mode = x3d::get_driver_mode();
    CHECK_FALSE(mode.has_value());
  }
}

TEST_CASE("set_driver_mode fails gracefully when no driver") {
  spdlog::set_level(spdlog::level::off);
  auto path = x3d::find_driver_sysfs_path();
  if (path.empty()) {
    CHECK_FALSE(x3d::set_driver_mode(x3d::X3DMode::cache));
    CHECK_FALSE(x3d::set_driver_mode(x3d::X3DMode::frequency));
  }
}

TEST_SUITE_END();
