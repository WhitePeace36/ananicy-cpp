#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace x3d {

struct X3DTopology {
  std::string cache_cores_str;
  std::string frequency_cores_str;
  bool        driver_present{false};
};

/// Detect AMD X3D topology at startup.
/// Returns nullopt if not an X3D CPU or detection fails.
[[nodiscard]] auto detect() noexcept -> std::optional<X3DTopology>;

enum class X3DMode : std::uint8_t { cache, frequency };

/// Find the sysfs path for the amd_x3d_vcache driver device.
[[nodiscard]] auto find_driver_sysfs_path() noexcept -> std::string;

/// Read current X3D mode from the kernel driver.
[[nodiscard]] auto get_driver_mode() noexcept -> std::optional<X3DMode>;

/// Set X3D mode via the kernel driver.
[[nodiscard]] auto set_driver_mode(X3DMode mode) noexcept -> bool;

} // namespace x3d
