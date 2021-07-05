#include "service.hpp"

#include <spdlog/spdlog.h>
#include <systemd/sd-daemon.h>

namespace service {
status current_status = STARTING;

void set_status(service::status status) {
  current_status = status;
  switch (status) {
  case STARTING:
    break;
  case STARTED:
    sd_notify(0, "READY=1");
    break;
  case STOPPING:
    sd_notify(0, "STOPPING=1");
    break;
  case STOPPED:
    break;
  }
}

status get_status() { return current_status; }

void reload() { spdlog::warn("{}: Not implemented yet", __func__); }

void stop() { spdlog::warn("{}: Not implemented yet", __func__); }

} // namespace service
