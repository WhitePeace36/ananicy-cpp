//
// Created by aviallon on 20/04/2021.
//

#include <ctime>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <cerrno>
#include <exception>
#include <stdexcept>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <sched.h>
#include <filesystem>
#include "priority.hpp"


void test_errno(const std::string& func_name, unsigned pid) {
    if (errno == 0) {
        spdlog::info("{}: Successfully applied to {}", func_name, pid);
        return;
    }

    if (errno == ESRCH) {
        spdlog::warn("{}: Process not found: {}", func_name, pid);
    } else if (errno == EACCES || errno == EPERM) {
        throw std::runtime_error(fmt::format("{}: Insufficient permission to set io-priority of process {}", func_name, pid));
    } else if (errno != 0) {
        spdlog::error("{}: Unknown error, errno is {}({})", func_name, strerror(errno), errno);
    }
}

void set_priority(unsigned pid, int nice_value) {
    errno = 0;
    for(const auto& path : std::filesystem::directory_iterator(fmt::format("/proc/{}/task", pid))) {
        int tid = std::stoi(path.path().filename().string());
        if (tid > 0)
            setpriority(PRIO_PROCESS, tid, nice_value);
    }

    test_errno(__func__, pid);
}

/** BEGIN io priority - extracted from kernel headers **/

#define IOPRIO_CLASS_SHIFT	(13)
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)

#define IOPRIO_PRIO_CLASS(mask)	((mask) >> IOPRIO_CLASS_SHIFT)
#define IOPRIO_PRIO_DATA(mask)	((mask) & IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class, data)	(((class) << IOPRIO_CLASS_SHIFT) | data)

#define ioprio_valid(mask)	(IOPRIO_PRIO_CLASS((mask)) != IOPRIO_CLASS_NONE)

enum {
    IOPRIO_CLASS_NONE,
    IOPRIO_CLASS_RT,
    IOPRIO_CLASS_BE,
    IOPRIO_CLASS_IDLE,
};

/*
 * 8 best effort priority levels are supported
 */
#define IOPRIO_BE_NR	(8)

enum {
    IOPRIO_WHO_PROCESS = 1,
    IOPRIO_WHO_PGRP,
    IOPRIO_WHO_USER,
};

/*
 * Fallback BE priority
 */
#define IOPRIO_NORM	(4)


int ioprio_set(__priority_which_t _which, id_t _who, int _prio) {
    return (int) syscall(SYS_ioprio_set, _which, _who, _prio);
}

void set_io_priority(unsigned pid, const std::string& io_class, int value) {
    int io_class_value = 0;
    if (io_class == "best-effort") {
        io_class_value = IOPRIO_CLASS_BE;
    } else if (io_class == "realtime") {
        io_class_value = IOPRIO_CLASS_RT;
    } else if (io_class == "idle") {
        io_class_value = IOPRIO_CLASS_IDLE;
    } else if (io_class == "none") {
        io_class_value = IOPRIO_CLASS_NONE;
    } else {
        spdlog::error("Unknown io class {}, skipping...", io_class);
        return;
    }

    int io_prio = IOPRIO_PRIO_VALUE(io_class_value, value);

    if (!ioprio_valid(io_prio)) {
        spdlog::error("IO priority is invalid, skipping...");
        return;
    }

    errno = 0;
    ioprio_set(IOPRIO_WHO_PROCESS, pid, io_prio);

    test_errno(__func__, pid);
}

/** END io priority - extracted from kernel headers **/

void set_sched(unsigned pid, const std::string& sched_name) {
    int sched = 0;
    if (sched_name == "idle") {
        sched = SCHED_IDLE;
    } else if (sched_name == "normal" || sched_name == "other") {
        sched = SCHED_OTHER;
    } else if (sched_name == "rr") {
        sched = SCHED_RR;
    } else if (sched_name == "fifo") {
        sched = SCHED_FIFO;
    } else if (sched_name == "deadline") {
        sched = SCHED_DEADLINE;
    } else if (sched_name == "batch") {
        sched = SCHED_BATCH;
    }
    errno = 0;
    const sched_param param = {};
    sched_setscheduler(pid, sched, &param);

    test_errno(__func__, pid);
}