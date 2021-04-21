# Ananicy Cpp
[Ananicy](https://github.com/Nefelim4ag/Ananicy) rewritten in C++ for much lower CPU and memory usage.

_Alpha status_

## What works

- [X] Rule and configuration loading
- [X] Process detection and scanning
- [X] Renicing
- [X] Changing CPU scheduler
- [X] Changing IO class and IO nice
- [ ] Setting OOM score
- [ ] Cgroups
- [ ] Systemd integration
  - [X] Unit file
  - [ ] Working sd_notify
- [ ] Something else... ?

## Installation
Almost a drop-in replacement fro Ananicy.
Install ananicy, and then:
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

## Why rewritting ananicy?
I mostly used Ananicy on older computers to improve interactivity. However, having Ananicy use megabytes of RAM
and a decent amount of CPU time troubled me. Thus I decided to rewrite it in C++, using an event based approach.
RAM usage is much lower (only a few thousands of bytes !), and CPU usage is almost always zero thanks to its
event-based implementation. 