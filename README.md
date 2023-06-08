![Ananicy Cpp Logo](./assets/ananicy_logo.svg)

# Ananicy Cpp
[Ananicy](https://github.com/Nefelim4ag/Ananicy) rewritten in C++ for much lower CPU and memory usage.

_Beta status_

[![Packaging status](https://repology.org/badge/vertical-allrepos/ananicy-cpp.svg)](https://repology.org/project/ananicy-cpp/versions)

## What works

- [X] Rule and configuration loading
- [X] Process detection and scanning
- [X] Renicing
- [X] Changing CPU scheduler
- [X] Changing IO class and IO nice
- [X] Setting OOM score
- [X] Change default configuration file and rules directory using environment variables (`ANANICY_CPP_{CONF,CONFDIR}`)
- [X] Cgroups
  - [X] V1
  - [X] V2 (_Limited support, see #21_)
- [X] Autogroup
- [X] Systemd integration
  - [X] Unit file
  - [X] Hardening unit file
  - [X] Working sd_notify
- [X] CLI compatibility with Ananicy
- [ ] Something else... ?

## Installation

#### Dependencies

- [CMake](https://cmake.org/download/) 3.17+
- [fmtlib](https://github.com/fmtlib/fmt) 8.0+ _(can be automatically downloaded)_
- [spdlog](https://github.com/gabime/spdlog) 1.9+ _(can be automatically downloaded)_
- [nlohmann_json](https://github.com/nlohmann/json) 3.9+ _(can be automatically downloaded)_
- C++ compiler compatible with C++20 (`g++ >= 10` or `clang++ >= 10`).
  Notably, `<concepts>` and `std::jthread` are required. Work is in progress to add support to `g++ ~= 9`, see https://gitlab.com/ananicy-cpp/ananicy-cpp/-/issues/28 for more info.
- _Optional_:
    - libsystemd (if you want systemd support)
    - [bpf](https://www.kernel.org) _(required for bpf implementation)_
    - [libbpf](https://github.com/libbpf/libbpf) _(required for bpf implementation)_
    - [libelf](https://sourceware.org/elfutils/) _(required for bpf implementation)_
    - [clang](https://clang.llvm.org/) clang++ >= 10 _(required for bpf implementation)_

#### Debian/Ubuntu
Install the prerequisites: `sudo apt install git cmake build-essential g++ libsystemd-dev`

### Linux
It should be a fully drop-in replacement from Ananicy.
If you detect a difference in behavior (except for bugs in Ananicy that are fixed in `ananicy-cpp`),
please create an issue.

If you want pre-made community rules, you can use the rules from the original Ananicy project.
Simply copy them to your rules directory (by default, `/etc/ananicy.d`).

#### Available CMake options

- `CMAKE_BUILD_TYPE:STRING`: Either `None` (default), `Release`, or `Debug`.
The effects are mainly on the default compiler flags `ananicy-cpp` is built with.
- `USE_EXTERNAL_JSON:BOOL`: Use system nlohmann_json library,
- `USE_EXTERNAL_SPDLOG:BOOL`: Use system spdlog library,
- `USE_EXTERNAL_FMTLIB:BOOL`: Use system fmtlib,
- `ENABLE_SYSTEMD:BOOL`: Add systemd support,
- `USE_BPF_PROC_IMPL:BOOL`: Use BPF for processing,
- `BPF_BUILD_LIBBPF:BOOL`: Build libbpf instead of using system libary,
- `STATIC:BOOL`: Build a static binary,

#### Building

In order to actually build `ananicy-cpp`, open a terminal,
clone the repository (or download a release archive), and do the following commands:

- If using the `git` repo (development version):
  - `git clone https://gitlab.com/ananicy-cpp/ananicy-cpp.git`
  - `cd ananicy-cpp`
- If using release archive:
  - `wget https://gitlab.com/ananicy-cpp/ananicy-cpp/-/archive/v1.1.0/ananicy-cpp-v1.1.0.tar.gz`
  - `tar -xvf ananicy-cpp-v1.1.0.tar.gz`
  - `cd ananicy-cpp-v1.1.0`

```
cmake -B build \
  -DCMAKE_RELEASE_TYPE=Release \
  -D[Your Option Here] -D[Another Option] \
  -S .
cmake --build build --target ananicy-cpp
sudo cmake --install build --component Runtime
```

- If using install-deps.sh and configure.sh (example using BPF_PROC_IMPL)
  - `git clone https://gitlab.com/ananicy-cpp/ananicy-cpp.git`
  - `cd ananicy-cpp`
  - `env USE_BPF=ON ./install-deps.sh && ./configure.sh --use_bpf_proc && ./build.sh`
  - `sudo cmake --install build/RelWithDebInfo --component Runtime`

#### Service

To enable the systemd service,
enable Systemd support with `-DENABLE_SYSTEMD=ON` on `cmake` invocation, and run this command _after_ installing `ananicy-cpp`:
```
systemctl enable --now ananicy-cpp.service
```
If you want to stop or disable it later, you can use:
```
systemctl (stop|disable) ananicy-cpp.service
```
You can have more information in `systemd`'s man page (`man systemd` on Linux)

### Distro-specific
#### Arch-based distros

There is an AUR package named `ananicy-cpp`, which is maintained by me.
You can simply use your preferred AUR-helper to install it.

There is also binaries in `cachyos` and `chaotic-aur`, although `ananicy-cpp` is relatively fast to build.


## Configuration

### Global configuration

### Rules

Add rules in `/etc/ananicy.d`. This path can be overridden with `ANANICY_CPP_CONFDIR`
environment variable.
Rules are defined in files ending with `.rules`.
For instance, to add a rule for GCC, you could do the following:

- Create the `/etc/ananicy.d/10-compilers` folder.
- Create the `/etc/ananicy.d/10-compilers/gcc.rules` file
- Add `{"name": "gcc", "nice": 19, "latency_nice": 19, "sched": "batch", "ioclass": "idle"}` to the file.

You can then (re)start `ananicy-cpp.service`.

#### Supported attributes

- `nice: [-20-19]`: Set the nice value of the process.
A process with a higher nice value will be more "polite", and will get less
cpu time than processes with a lower nice value.
- `latency_nice: [-20-19]`: Set the latency_nice value of the process.
A process with a lower latency_nice value indicates the task to have the least
latency as compared to the task having a higher latency_nice.
A additonal kernel patch is needed see [latency_nice](https://lore.kernel.org/lkml/20221110175009.18458-1-vincent.guittot@linaro.org/)
- `sched: {"fifo", "rr", "normal", "batch", "idle"}`:
Set the scheduling policy.
  - `fifo` and `rr` (for round-robin) are realtime scheduling policies, and must only be used for
latency critical programs, like `Xorg` or `pulseaudio` for instance. Nice values are ignored,
  `rtprio` should be used instead.
  - `deadline`: Special realtime scheduling policy which _can't_ be set by `ananicy-cpp`,
  but can be reported by it.
  - `normal` is well... normal, the default behavior for the current OS.
  Specifying this option can be useful if you want to force the child of a
  realtime process to have a normal scheduling policy.
  - `batch`: Very useful for compilers or other CPU-hungry, non-interactive programs,
  like compilers for instance. It can actually improve their performance with almost no cost
  on the rest of the system.
  - `idle`: Very, very low priority, even lower than a nice value of `19`.
  Useful for background, low priority stuff, like file indexer for instance.
- `rtprio: [0, 99]`: Sets the static priority of a process. Only relevant if the actual scheduling policy
of a process is a realtime one, i.e. `fifo`, `rr` or `deadline`.
A higher value means a higher priority.
- `ioclass: {"best-effort", "realtime", "idle", "none"}`:
Define the IO scheduling policy. By default, it is `best-effort`.
**Only the CFQ I/O scheduler supports `ioclass` and `ionice`, see [ioprio_set](https://man7.org/linux/man-pages/man2/ioprio_set.2.html)**.
  - `realtime` is to be used cautiously, as the process will have the absolute priority
  above all `best-effort` processes, and can [starve][wikipedia:starvation] them.
  This could prevent you from starting a shell, for instance.
    - `ionice: [0, 7]`. Lower value is higher priority.
  - `idle`: Process gets I/O resources after all other processes.
  This could [starve][wikipedia:starvation] the process.
    - `ionice` is completely ignored.
  - `none`: Reset I/O policy to system default, `ionice` must be `0`.
  - `best-effort`: Try to fairly share I/O resources between processes.
    - `ionice: [0, 7]`. Lower value is higher priority.
- `oom_score_adj: [-999, 999]`: Adjust the **O**ut **O**f **M**emory killer score of a process.
  Negative value decrease the score of the process, making it _less_ likely to be killed if available memory
  gets very low. It is recommended to use it on critical programs which must be killed last if you lack memory.
- `cgroup`: Put the process in the specified cgroup. This can be any cgroup, including those created outside `ananicy-cpp`.
- `type`: Set the type of the rule. All options defined in the type will be used as if written explicitly in the rule,
although you can override each option if needed.


[wikipedia:starvation]: https://en.wikipedia.org/wiki/Starvation_(computer_science)

### Types

To avoid repeating yourself, you can add types.
It must be defined in a `.types` file.

The syntax is the following:
~~~json
{"type": "my_type", "nice": 19, "other_parameter": "value"}
~~~

It can then be used in any rule by simply adding the `type` property to the rule.
For instance, `{"name": "gcc", "type": "compiler"}`

Parameters can be overridden, for instance:
~~~json
{"type": "compiler", "nice": 19, "sched": "batch", "ioclass": "idle"}
~~~

~~~json
{"name": "gcc", "type": "compiler", "ioclass": "none", "ionice": 0}
~~~

### Cgroups

They are defined in `.cgroups` files.

`cgroup_v1` are recommended for proper functionality (see issue #21), they can be forced by
passing `systemd.unified_cgroup_hierarchy=0` to your kernel cmdline (by editing `/etc/default/grub` for instance).

Only one attribute is supported, `CPUQuota`:

~~~json
{"cgroup": "cpu80", "CPUQuota": 80}
~~~

One major limitations of `cgroups` right now is that they can only be created by the `root` user.
You can _not_ use a `CAP_SYS_CGROUP` capability thing like for other things, because it simply does not exist yet.
If it is ever implemented in the Linux kernel, please create an issue.

### Community rules

#### From the original project

You can use the original Ananicy's rules by copying the `00-default`, `00-types.types` and `00-cgroups.cgroups` to you `ananicy.d` directory (by default, in `/etc`).

## Why rewriting ananicy?
I mostly used Ananicy on older computers to improve interactivity. However, having Ananicy use megabytes of RAM
and a decent amount of CPU time troubled me. Thus I decided to rewrite it in C++, using an event based approach.
RAM usage is much lower (only a few thousands of bytes !), and CPU usage is almost always zero thanks to its
event-based implementation.
