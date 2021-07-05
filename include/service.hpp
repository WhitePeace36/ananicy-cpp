#pragma once

namespace service {
enum status {
  STARTING,
  STARTED,
  STOPPING,
  STOPPED,
};

void   set_status(status status);
status get_status();

void reload();
void stop();
} // namespace service
