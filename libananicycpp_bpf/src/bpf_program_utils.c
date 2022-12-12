#include "ananicy_cpp.skel.h"
#include "bpf_program_utils.h"
//#include "btf_helpers.h"

#include <stdint.h>
#include <sys/types.h>

#include <bpf/libbpf.h>
//#include <bpf/bpf.h>

#include "process_utils.h"
#include "trace_helpers.h"

static bool g_verbose = false;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args) {
    if (!g_verbose)
        return 0;
    return vfprintf(stderr, format, args);
}

ananicy_cpp_bpf_t* initialize_bpf_program(uint64_t min_us, bool verbose) {
    g_verbose = verbose;

    //LIBBPF_OPTS(bpf_object_open_opts, open_opts);
    libbpf_set_print(libbpf_print_fn);

    /*int32_t err = ensure_core_btf(&open_opts);
    if (err) {
        fprintf(stderr, "failed to fetch necessary BTF for CO-RE: %s\n", strerror(-err));
        return NULL;
    }*/

    ananicy_cpp_bpf_t* obj = NULL;

    //obj = ananicy_cpp_bpf__open_opts(&open_opts);
    obj = ananicy_cpp_bpf__open();
    if (!obj) {
        fprintf(stderr, "failed to open BPF object\n");
        //cleanup_core_btf(&open_opts);
        return NULL;
    }

    /* initialize global data (filtering options) */
    obj->rodata->targ_uid = -1;
    obj->rodata->targ_tgid = 0;
    obj->rodata->targ_pid = 0;
    obj->rodata->min_us = min_us;

    //if (probe_tp_btf("sched_process_wait")) {
    //    bpf_program__set_autoload(obj->progs.handle_sched_wakeup, false);
    //    bpf_program__set_autoload(obj->progs.handle_sched_wakeup_new, false);
    //    bpf_program__set_autoload(obj->progs.handle_sched_switch, false);
    //}

    /*if (probe_tp_btf("sched_wakeup")) {
        bpf_program__set_autoload(obj->progs.handle_sched_wakeup, false);
        bpf_program__set_autoload(obj->progs.handle_sched_wakeup_new, false);
        bpf_program__set_autoload(obj->progs.handle_sched_switch, false);
    } else {
        bpf_program__set_autoload(obj->progs.sched_wakeup, false);
        bpf_program__set_autoload(obj->progs.sched_wakeup_new, false);
        bpf_program__set_autoload(obj->progs.sched_switch, false);
    }*/

    int32_t err = ananicy_cpp_bpf__load(obj);
    if (err) {
        fprintf(stderr, "failed to load BPF object: %d\n", err);
        ananicy_cpp_bpf__destroy(obj);
        //cleanup_core_btf(&open_opts);
        return NULL;
    }

    err = ananicy_cpp_bpf__attach(obj);
    if (err) {
        fprintf(stderr, "failed to attach BPF programs\n");
        ananicy_cpp_bpf__destroy(obj);
        //cleanup_core_btf(&open_opts);
        return NULL;
    }

    return obj;
}

void destroy_bpf_program(ananicy_cpp_bpf_t* obj, struct perf_buffer* pb) {
    perf_buffer__free(pb);
    ananicy_cpp_bpf__destroy(obj);
    //cleanup_core_btf(&open_opts);
}

struct perf_buffer* bpf_program_init_events(ananicy_cpp_bpf_t* obj) {
    return perf_buffer__new(bpf_map__fd(obj->maps.events), 64,
                handle_event, handle_lost_events, NULL, NULL);
}
