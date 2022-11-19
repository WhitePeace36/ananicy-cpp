#include "utility/singleton_process.hpp"

#include <cerrno>
#include <fcntl.h> /* For O_* constants */
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <unistd.h>

#include <spdlog/spdlog.h>

SingletonProcess::~SingletonProcess() {
  if (m_handle != -1) {
    ::close(m_handle);
    m_handle = -1;
  }
}

bool SingletonProcess::try_create() {
  const mode_t unix_perm = 0644;
  m_mode = O_RDWR | O_CREAT | O_EXCL;

  // Try to create shared memory
  m_handle = shm_open(m_filename.data(), m_mode, unix_perm);
  if (m_handle == -1) {
    if (errno != EEXIST)
      spdlog::error("Could not create: {}", std::strerror(errno));
    return false;
  }
  // If successful change real permissions
  ::fchmod(m_handle, unix_perm);
  return true;
}

bool SingletonProcess::try_open() {
  if (m_handle != -1) {
    spdlog::error("Already opened!");
    return false;
  }

  const mode_t unix_perm = 0644;
  m_mode = O_RDONLY;

  m_handle = shm_open(m_filename.data(), m_mode, unix_perm);
  if (m_handle == -1) {
    spdlog::error("Could not open: {}", std::strerror(errno));
    return false;
  }
  return true;
}

bool SingletonProcess::remove(std::string_view name) noexcept {
  return 0 == shm_unlink(name.data());
}
