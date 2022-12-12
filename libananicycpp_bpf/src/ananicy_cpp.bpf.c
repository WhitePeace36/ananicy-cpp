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


static const struct event empty_event = {};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, pid_t);
    __type(value, struct event);
} execs SEC(".maps");

#define TASK_RUNNING    0

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

/* record enqueue timestamp */
static int trace_enqueue(u32 tgid, u32 pid)
{
    u64 ts;

    if (!pid)
        return 0;
    if (targ_tgid && targ_tgid != tgid)
        return 0;
    if (targ_pid && targ_pid != pid)
        return 0;

    ts = bpf_ktime_get_ns();
    bpf_map_update_elem(&start, &pid, &ts, 0);
    return 0;
}

static int handle_switch(void *ctx, struct task_struct *prev, struct task_struct *next)
{
    struct event event = {};
    u64 *tsp, delta_us;
    u32 pid;

    /* ivcsw: treat like an enqueue event and store timestamp */
    if (get_task_state(prev) == TASK_RUNNING)
        trace_enqueue(BPF_CORE_READ(prev, tgid), BPF_CORE_READ(prev, pid));

    pid = BPF_CORE_READ(next, pid);

    /* fetch timestamp and calculate delta */
    tsp = bpf_map_lookup_elem(&start, &pid);
    if (!tsp)
        return 0;   /* missed enqueue */

    delta_us = (bpf_ktime_get_ns() - *tsp) / 1000;
    if (min_us && delta_us <= min_us)
        return 0;

    event.pid = pid;
    event.prev_pid = BPF_CORE_READ(prev, pid);
    event.delta_us = delta_us;
    bpf_probe_read_kernel_str(&event.task, sizeof(event.task), next->comm);

    /* output */
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
                  &event, sizeof(event));

    bpf_map_delete_elem(&start, &pid);
    return 0;
}


static __always_inline bool valid_uid(uid_t uid) {
    return uid != INVALID_UID;
}

int handle_event(struct trace_event_raw_sys_enter* ctx)
{
    static pid_t prev_pid;
    static pid_t prev_tgid;
    struct task_struct *task;

    struct event event = {};
    u64 *tsp, delta_us;

    u32 uid = (u32)bpf_get_current_uid_gid();

    if (valid_uid(targ_uid) && targ_uid != uid)
        return 0;

    u64 id = bpf_get_current_pid_tgid();
    /* use kernel terminology here for tgid/pid: */
    u32 tgid = id >> 32;
    u32 pid = (pid_t)id;

    trace_enqueue(tgid, pid);

    /* fetch timestamp and calculate delta */
    tsp = bpf_map_lookup_elem(&start, &pid);
    if (!tsp)
        return 0;   /* missed enqueue */

    delta_us = (bpf_ktime_get_ns() - *tsp) / 1000;
    //if (min_us && delta_us <= min_us)
    //    return 0;

    event.pid = tgid;
    event.prev_pid = prev_pid;
    event.delta_us = delta_us;
    bpf_get_current_comm(&event.task, sizeof(event.task));

    prev_tgid = pid;
    prev_pid = event.pid;

    /* output */
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
                  &event, sizeof(event));

    bpf_map_delete_elem(&start, &pid);
    return 0;
}

//SEC("tracepoint/syscalls/sys_enter_execve")
//int tracepoint__syscalls__sys_enter_execve(struct trace_event_raw_sys_enter* ctx)
//{
//    return handle_event(ctx);
//}

SEC("tp/sched/sched_process_exec")
int handle_exec(struct trace_event_raw_sched_process_exec* ctx)
{
    static pid_t prev_pid;
    struct event *e;
    int zero = 0;

    e = bpf_map_lookup_elem(&heap, &zero);
    if (!e) /* can't happen */
        return 0;

    e->pid = bpf_get_current_pid_tgid() >> 32;
    e->prev_pid = prev_pid;
    bpf_get_current_comm(&e->task, sizeof(e->task));
    prev_pid = e->pid;

    /* output */
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
                  e, sizeof(*e));
    return 0;
}

SEC("tp/sched/sched_process_fork")
int handle_fork(struct trace_event_raw_sched_process_fork* ctx)
{
    static pid_t prev_pid;
    struct event *e;
    int zero = 0;

    e = bpf_map_lookup_elem(&heap, &zero);
    if (!e) /* can't happen */
        return 0;

    e->pid = bpf_get_current_pid_tgid() >> 32;
    e->prev_pid = prev_pid;
    bpf_get_current_comm(&e->task, sizeof(e->task));
    prev_pid = e->pid;

    /* output */
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU,
                  e, sizeof(*e));
    return 0;
}


/*
SEC("tp_btf/sched_wakeup")
int BPF_PROG(sched_wakeup, struct task_struct *p)
{
    return trace_enqueue(p->tgid, p->pid);
}

SEC("tp_btf/sched_wakeup_new")
int BPF_PROG(sched_wakeup_new, struct task_struct *p)
{
    return trace_enqueue(p->tgid, p->pid);
}

SEC("tp_btf/sched_switch")
int BPF_PROG(sched_switch, bool preempt, struct task_struct *prev, struct task_struct *next)
{
    return handle_switch(ctx, prev, next);
}

SEC("raw_tp/sched_wakeup")
int BPF_PROG(handle_sched_wakeup, struct task_struct *p)
{
    return trace_enqueue(BPF_CORE_READ(p, tgid), BPF_CORE_READ(p, pid));
}

SEC("raw_tp/sched_wakeup_new")
int BPF_PROG(handle_sched_wakeup_new, struct task_struct *p)
{
    return trace_enqueue(BPF_CORE_READ(p, tgid), BPF_CORE_READ(p, pid));
}

SEC("raw_tp/sched_switch")
int BPF_PROG(handle_sched_switch, bool preempt, struct task_struct *prev, struct task_struct *next)
{
    return handle_switch(ctx, prev, next);
}*/

char LICENSE[] SEC("license") = "GPL";
