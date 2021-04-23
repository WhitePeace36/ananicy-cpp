//
// Created by aviallon on 20/04/2021.
//

#ifndef ANANICY_CPP_PRIORITY_HPP
#define ANANICY_CPP_PRIORITY_HPP

#include <string>

[[maybe_unused]] bool set_priority(unsigned pid, int nice_value);

[[maybe_unused]] bool set_io_priority(unsigned pid, const std::string &io_class,
                                      int value);

[[maybe_unused]] bool set_sched(unsigned pid, const std::string &sched_name);

[[maybe_unused]] bool set_oom_score_adjust(unsigned pid, int value);

#endif // ANANICY_CPP_PRIORITY_HPP
