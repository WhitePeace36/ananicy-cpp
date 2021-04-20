//
// Created by aviallon on 20/04/2021.
//

#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <exception>
#include <stdexcept>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include "priority.hpp"

void set_priority(unsigned pid, int nice_value) {
    errno = 0;
    setpriority(PRIO_PROCESS, pid, nice_value);

    if (errno == 0) {
        spdlog::info("Successfully set priority to {}", pid);
        return;
    }

    if (errno == ESRCH) {
        spdlog::warn("{}: Process not found: {}", __func__, pid);
    } else if (errno == EACCES || errno == EPERM) {
        throw std::runtime_error(fmt::format("Insufficient permission to set priority of process {}", pid));
    } else if (errno != 0) {
        throw std::runtime_error(fmt::format("Unknown error, errno is {}", errno));
    }
}