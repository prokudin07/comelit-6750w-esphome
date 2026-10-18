#pragma once
#include <cstdint>
#include "esphome/core/log.h"

namespace esphome::comelit_simplebus {
inline const char *bus_event_name(uint8_t command) {
  switch (command) {
    case 16: return "DOOR_OPEN_REQUEST";
    case 17: return "ANSWER_OFF_HOOK";
    case 18: return "HANG_UP_ON_HOOK";
    case 48: return "CALL_RING";
    case 50: return "CALL_FINAL";
    default: return "UNKNOWN_COMMAND";
  }
}

// No address filter or session inference. Call after computing checksum validity,
// from the main loop only. A command is not proof of a physical door opening.
inline void log_bus_frame(uint8_t command, uint8_t address, uint8_t checksum, bool valid) {
  if (!valid) return;
  ESP_LOGI("comelit_bus", "BUS address=%u event=%s command=%u (0x%02X) checksum=%u valid=YES",
           address, bus_event_name(command), command, command, checksum);
}
}  // namespace esphome::comelit_simplebus
