#ifndef ANANICY_CPP_SINGLETON_PROCESS_HPP
#define ANANICY_CPP_SINGLETON_PROCESS_HPP

#include <cstdint>
#include <string_view>
#include <sys/stat.h> // mode_t

class SingletonProcess {
public:
  // Default constructor.
  SingletonProcess() noexcept = default;

  SingletonProcess(SingletonProcess &&) noexcept = default;
  SingletonProcess &operator=(SingletonProcess &&) noexcept = default;

  // Constructs with the name.
  // Used as semaphore name.
  SingletonProcess(std::string_view name) : m_filename(std::move(name)) {}

  // Destroys *this and indicates that the calling process is finished
  ~SingletonProcess();

  // Returns the name of the shared memory object.
  constexpr mode_t get_mode() const noexcept { return m_mode; }

  // Returns the name of the shared memory object.
  constexpr std::string_view get_name() const noexcept { return m_filename; }

  // Creates a shared memory object.
  bool try_create();
  // Opens a shared memory object.
  bool try_open();

  // Erases a shared memory object from the system.
  // @returns false on error.
  static bool remove(std::string_view name) noexcept;

  // Deleted
  SingletonProcess(const SingletonProcess &) noexcept = delete;
  SingletonProcess &operator=(const SingletonProcess &) noexcept = delete;

private:
  std::int32_t     m_handle{-1};
  mode_t           m_mode{};
  std::string_view m_filename{};
};

#endif // ANANICY_CPP_SINGLETON_PROCESS_HPP
