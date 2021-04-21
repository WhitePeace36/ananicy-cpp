//
// Created by aviallon on 20/04/2021.
//

#include <sys/resource.h>
#include <cerrno>
#include <stdexcept>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <sched.h>
#include <filesystem>
#include <fstream>
#include "priority.hpp"
#include "ioprio.h"


bool test_errno(const std::string& func_name, unsigned pid) {
    if (errno == 0) {
        spdlog::info("{}: Successfully applied to {}", func_name, pid);
        return true;
    }

    if (errno == ESRCH) {
        spdlog::warn("{}: Process not found: {}", func_name, pid);
        return false;
    } else if (errno == EACCES || errno == EPERM) {
        throw std::runtime_error(fmt::format("{}: Insufficient permission to set io-priority of process {}", func_name, pid));
    } else if (errno != 0) {
        spdlog::error("{}: Unknown error, errno is {}({})", func_name, strerror(errno), errno);
        return true;
    }

    return false;
}

bool set_priority(unsigned pid, int nice_value) {
    errno = 0;
    try {
        for(const auto& path : std::filesystem::directory_iterator(fmt::format("/proc/{}/task", pid))) {
            int tid = std::stoi(path.path().filename().string());
            if (tid > 0)
                setpriority(PRIO_PROCESS, tid, nice_value);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::error("{}: filesystem_error: {} (path: {})", __func__, e.what(), e.path1().string());
        return false;
    }


    return test_errno(__func__, pid);
}


bool set_io_priority(unsigned pid, const std::string& io_class, int value) {
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
        return true;
    }

    int io_prio = IOPRIO_PRIO_VALUE(io_class_value, value);

    if (!ioprio_valid(io_prio)) {
        spdlog::error("IO priority is invalid, skipping...");
        return true;
    }

    errno = 0;
    ioprio_set(IOPRIO_WHO_PROCESS, pid, io_prio);

    return test_errno(__func__, pid);
}

bool set_sched(unsigned pid, const std::string& sched_name) {
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

    return test_errno(__func__, pid);
}

bool set_oom_score_adjust(unsigned pid, int value) {

    try {
        std::filesystem::path oom_score_path(fmt::format("/proc/{}/oom_score_adj", pid));

        std::ofstream oom_score_file;
        oom_score_file.open(oom_score_path);
        if (oom_score_file.fail()) {
            spdlog::error("{}: Couldn't set OOM score adjustment to {} for {}", __func__, value, pid);
            return false;
        }

        oom_score_file << value;
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::error("{}: Unknwon filesystem error: {} (path: {})", __func__, e.what(), e.path1().string());
        return false;
    }

    return true;
}