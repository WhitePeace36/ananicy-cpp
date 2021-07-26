#ifndef ANANICY_CPP_SYSCALLS_H
#define ANANICY_CPP_SYSCALLS_H

#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

/**
 * ioprio_(get|set)
 */

#define IOPRIO_CLASS_SHIFT (13)
#define IOPRIO_PRIO_MASK   ((1UL << IOPRIO_CLASS_SHIFT) - 1)

#define IOPRIO_PRIO_CLASS(mask) ((mask) >> IOPRIO_CLASS_SHIFT)
#define IOPRIO_PRIO_DATA(mask)  ((mask)&IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class, data)                                         \
  (((class) << IOPRIO_CLASS_SHIFT) | (data))

#define ioprio_valid(mask) (IOPRIO_PRIO_CLASS((mask)) != IOPRIO_CLASS_NONE)

enum {
  IOPRIO_CLASS_NONE,
  IOPRIO_CLASS_RT,
  IOPRIO_CLASS_BE,
  IOPRIO_CLASS_IDLE,
};

/*
 * 8 best effort priority levels are supported
 */
#define IOPRIO_BE_NR (8)

enum {
  IOPRIO_WHO_PROCESS = 1,
  IOPRIO_WHO_PGRP,
  IOPRIO_WHO_USER,
};

/*
 * Fallback BE priority
 */
#define IOPRIO_NORM (4)

static int ioprio_set(__priority_which_t _which, id_t _who, int _prio) {
  return (int)syscall(SYS_ioprio_set, _which, _who, _prio);
}

static int ioprio_get(__priority_which_t _which, id_t _who) {
  return (int)syscall(SYS_ioprio_get, _which, _who);
}

/**
 * sched_(set|get)attr
 */

struct [[gnu::packed]] sched_attr {
  uint32_t size;

  uint32_t sched_policy; // SCHED_(FIFO,RR,DEADLINE,OTHER,BATCH,IDLE, etc.)
  uint64_t sched_flags;

  int32_t sched_nice; // For non-realtime policies

  uint32_t sched_priority; // For realtime policies

  /**
   * SCHED_DEADLINE specific stuff
   */
  uint64_t sched_runtime;
  uint64_t sched_deadline;
  uint64_t sched_period;
};

static int sched_setattr(pid_t pid, const struct sched_attr *attr,
                         unsigned int flags) {
  return (int)syscall(__NR_sched_setattr, pid, attr, flags);
}

static int sched_getattr(pid_t pid, struct sched_attr *attr, unsigned int size,
                         unsigned int flags) {
  return (int)syscall(__NR_sched_getattr, pid, attr, size, flags);
}

#endif // ANANICY_CPP_SYSCALLS_H