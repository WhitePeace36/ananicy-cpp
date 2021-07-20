# Ananicy Cpp
[Ananicy](https://github.com/Nefelim4ag/Ananicy) rewritten in C++ for much lower CPU and memory usage.

_Alpha+ status_

## What works

- [X] Rule and configuration loading
- [X] Process detection and scanning
- [X] Renicing
- [X] Changing CPU scheduler
- [X] Changing IO class and IO nice
- [X] Setting OOM score
- [X] Change default configuration file and rules directory using environment variables (`ANANICY_CPP_{CONF,CONFDIR}`)
- [X] Cgroups
- [ ] Autogroup
- [ ] Systemd integration
  - [X] Unit file
  - [X] Hardening unit file
  - [X] Working sd_notify
- [X] CLI compatibility with Ananicy
- [ ] Something else... ?

## Installation

### Linux
Almost a drop-in replacement from Ananicy.

If you want pre-made community rules, install the original ananicy, and then:
```
mkdir build
cd build
cmake -DCMAKE_RELEASE_TYPE=Release ..
cmake --build .
sudo cmake --install .
```

To enable the systemd service, just run:
```
systemctl enable --now ananicy-cpp.service
```
and it should be done.

### Distro-specific
#### Arch-based distros

There is an AUR package named `ananicy-cpp`, which is maintained by me.
You can simply use your preferred AUR-helper to install it.

There is also binaries in `chaotic-aur`, although `ananicy-cpp` is quite fast to build.


## Configuration

Add rules in `/etc/ananicy.d`. This path can be overridden with `ANANICY_CPP_CONFDIR`
environment variable.  
Rules are defined in files ending with `.rules`.
For instance, to add a rule for GCC, you could do the following:

- Create the `/etc/ananicy.d/10-compilers` folder.
- Create the `/etc/ananicy.d/10-compilers/gcc.rules` file
- Add `{"name": "gcc", "nice": 19, "sched": "batch", "ioclass": "idle"}` to the file.

You can then (re)start `ananicy-cpp.service`.

### Types

To avoid repeating yourself, you can add types.
It must be defined in a `.types` file.
The syntax is the following:
`{"type": "my_type", "nice": 19, "other_parameter": "value"}`

It can then be used in any rule by simply adding the `type` property to the rule.
For instance, `{"name": "gcc", "type": "compiler"}`

## Why rewritting ananicy?
I mostly used Ananicy on older computers to improve interactivity. However, having Ananicy use megabytes of RAM
and a decent amount of CPU time troubled me. Thus I decided to rewrite it in C++, using an event based approach.
RAM usage is much lower (only a few thousands of bytes !), and CPU usage is almost always zero thanks to its
event-based implementation. 