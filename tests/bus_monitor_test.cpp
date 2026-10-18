#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
static std::vector<std::string> messages;
static void capture_log(const char *tag, const char *format, ...) {
  assert(std::string(tag) == "comelit_bus");
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  messages.emplace_back(message);
}
#include "bus_monitor.h"
using namespace esphome::comelit_simplebus;
int main() {
  // The helper emits no event for an invalid checksum.
  log_bus_frame(16, 3, 0, false);
  assert(messages.empty());
  // Replay the interpreted address-3 session, including its unknown command.
  log_bus_frame(50, 3, 5, true);
  log_bus_frame(17, 3, 4, true);
  log_bus_frame(49, 3, 5, true);
  log_bus_frame(16, 3, 3, true);
  log_bus_frame(18, 3, 4, true);
  assert(messages.size() == 5);
  assert(messages[0] == "BUS address=3 event=CALL_FINAL command=50 (0x32) checksum=5 valid=YES");
  assert(messages[1].find("ANSWER_OFF_HOOK") != std::string::npos);
  assert(messages[2].find("UNKNOWN_COMMAND command=49") != std::string::npos);
  assert(messages[3].find("DOOR_OPEN_REQUEST") != std::string::npos);
  assert(messages[4].find("HANG_UP_ON_HOOK") != std::string::npos);
  // All addresses are logged, including other subscribers and broadcast-like traffic.
  log_bus_frame(50, 1, 4, true);
  log_bus_frame(16, 111, 7, true);
  log_bus_frame(63, 255, 14, true);
  assert(messages.size() == 8);
  assert(messages[6].find("address=111") != std::string::npos);
  assert(messages[7].find("address=255 event=UNKNOWN_COMMAND command=63") != std::string::npos);
  assert(std::string(bus_event_name(48)) == "CALL_RING");
  for (unsigned command : {0, 19, 20, 21, 22, 23, 49, 63})
    assert(std::string(bus_event_name(command)) == "UNKNOWN_COMMAND");
  puts("PASS: whole-bus event output, all addresses, unknown commands and invalid-frame suppression");
}
