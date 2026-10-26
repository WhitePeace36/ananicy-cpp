// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>       /* most used helpers: SEC, __always_inline, etc */
#include <bpf/bpf_core_read.h>     /* for BPF CO-RE helpers */
#include <bpf/bpf_tracing.h>       /* for getting kprobe arguments */
#include "core_fixes.bpf.h"

#define TASK_COMM_LEN 16
#define INVALID_UID ((uid_t)-1)

struct event {
    pid_t pid;
    pid_t prev_pid;
    __u64 delta_us;
    char task[TASK_COMM_LEN];
};

const volatile __u64 min_us = 0;
const volatile pid_t targ_pid = 0;
const volatile pid_t targ_tgid = 0;
const volatile uid_t targ_uid = INVALID_UID;

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, u32);
    __type(value, u64);
} start SEC(".maps");


struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, int);
    __type(value, struct event);
} heap SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u32));
} events SEC(".maps");

static __always_inline bool valid_uid(uid_t uid) {
    return uid != INVALID_UID;
}

static __always_inline struct event* handle_event(pid_t pid, pid_t* prev_pid, u64* prev_ts) {
    struct event *e;
    int zero = 0;
    e = bpf_map_lookup_elem(&heap, &zero);
    if (!e) /* can't happen */
        return NULL;

    u64 ts = bpf_ktime_get_ns();

    // Compute delta_us
    u64 delta_us = (ts - *prev_ts) / 1000;
    //if (min_us && delta_us <= min_us)
    //    return 0;

    // Assign variables to event struct
    e->pid = pid;
    e->prev_pid = *prev_pid;
    e->delta_us = delta_us;

    // Update previous process's timestamp
    *prev_pid = e->pid;
    *prev_ts = ts;

    return e;
}

SEC("tp/sched/sched_process_exec")
int handle_exec(struct trace_event_raw_sched_process_exec* ctx)
{
    static pid_t prev_pid;
    static u64 prev_ts;
    struct event *e;

    u32 pid = bpf_get_current_pid_tgid();
    e = handle_event(pid, &prev_pid, &prev_ts);
    if (!e) /* can't happen */
        return 0;
    bpf_get_current_comm(&e->task, sizeof(e->task));
    /*
     * Never suppress an exec as a duplicate: two execs in a row by the same
     * pid (e.g. "nice -n 5 game", "env FOO=1 game", launcher scripts) load a
     * different program each time, and only the last one has the name that
     * the rules should match.
     */
    e->prev_pid = 0;

    /* output */
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
                  e, sizeof(*e));
    return 0;
}

/*
 * The fork tracepoint runs in the context of the *parent*, so
 * bpf_get_current_pid_tgid()/bpf_get_current_comm() describe the parent.
 * Report the newly created child instead. Otherwise a process that forks
 * without exec'ing afterwards (daemons such as wineserver) is never seen and
 * keeps the priority inherited from its parent.
 *
 * Only report new processes (thread-group leaders): set_priority() and
 * set_latency_nice() already apply to every thread in /proc/<pid>/task, and
 * threads created later inherit the value, so per-thread events would only
 * add load.
 */
SEC("raw_tp/sched_process_fork")
int BPF_PROG(handle_fork, struct task_struct *parent, struct task_struct *child)
{
    static pid_t prev_pid;
    static u64 prev_ts;
    struct event *e;

    pid_t pid = BPF_CORE_READ(child, pid);
    pid_t tgid = BPF_CORE_READ(child, tgid);
    if (pid != tgid) /* new thread, not a new process */
        return 0;

    e = handle_event(pid, &prev_pid, &prev_ts);
    if (!e) /* can't happen */
        return 0;
    BPF_CORE_READ_STR_INTO(&e->task, child, comm);

    /* output */
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
                  e, sizeof(*e));
    return 0;
}

/*
 * A process renamed itself with prctl(PR_SET_NAME). Wine/Proton does this
 * right after rewriting its argv in place, so the game only becomes
 * "game.exe" in /proc/<pid>/cmdline *after* its exec event was handled.
 * The netlink backend gets the same information from PROC_EVENT_COMM.
 *
 * Skip thread names (pthread_setname_np) and the rename done by exec itself,
 * which handle_exec already reports.
 */
SEC("raw_tp/task_rename")
int BPF_PROG(handle_rename, struct task_struct *task, const char *comm)
{
    static pid_t prev_pid;
    static u64 prev_ts;
    struct event *e;

    pid_t pid = BPF_CORE_READ(task, pid);
    pid_t tgid = BPF_CORE_READ(task, tgid);
    if (pid != tgid)
        return 0;
    if (BPF_CORE_READ_BITFIELD_PROBED(task, in_execve))
        return 0;

    e = handle_event(pid, &prev_pid, &prev_ts);
    if (!e) /* can't happen */
        return 0;
    bpf_probe_read_kernel_str(&e->task, sizeof(e->task), comm);
    e->prev_pid = 0; /* a rename is never a duplicate */

    /* output */
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
                  e, sizeof(*e));
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
