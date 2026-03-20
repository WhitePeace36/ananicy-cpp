#pragma once

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
std::optional<X3DTopology> detect() noexcept;

enum class X3DMode { cache, frequency };

/// Find the sysfs path for the amd_x3d_vcache driver device.
std::string find_driver_sysfs_path() noexcept;

/// Read current X3D mode from the kernel driver.
std::optional<X3DMode> get_driver_mode() noexcept;

/// Set X3D mode via the kernel driver.
bool set_driver_mode(X3DMode mode) noexcept;

} // namespace x3d
