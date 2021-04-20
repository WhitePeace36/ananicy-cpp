//
// Created by aviallon on 20/04/2021.
//

#ifndef ANANICY_CPP_PRIORITY_HPP
#define ANANICY_CPP_PRIORITY_HPP

void set_priority(unsigned pid, int nice_value);

void set_io_priority(unsigned pid, const std::string& io_class, int value) ;

void set_sched(unsigned pid, const std::string& sched_name);

#endif //ANANICY_CPP_PRIORITY_HPP
