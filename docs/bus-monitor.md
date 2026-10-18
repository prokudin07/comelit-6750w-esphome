# Whole-bus event log

Enable `bus_monitor: true` to print interpreted events for **all checksum-valid received frames**, regardless of `apartment_address`. The apartment setting only filters your ring sensor; it does not filter monitoring. All original raw frame logs, invalid checksum results, debug rejects and overflow warnings remain available.

## Complete example

Build [examples/bus-monitor.yaml](../examples/bus-monitor.yaml) with your Wi-Fi secrets. It is the normal receive-only firmware with the monitor enabled. Open ESPHome **Logs**, or run:

```sh
esphome logs examples/bus-monitor.yaml
```

The additional INFO log tag is `comelit_bus`. For example, a sequence for bus address 3 may look like this (illustrative output):

```text
[I][comelit_bus]: BUS address=3 event=CALL_FINAL command=50 (0x32) checksum=5 valid=YES
[I][comelit_bus]: BUS address=3 event=ANSWER_OFF_HOOK command=17 (0x11) checksum=4 valid=YES
[I][comelit_bus]: BUS address=3 event=UNKNOWN_COMMAND command=49 (0x31) checksum=5 valid=YES
[I][comelit_bus]: BUS address=3 event=DOOR_OPEN_REQUEST command=16 (0x10) checksum=3 valid=YES
[I][comelit_bus]: BUS address=3 event=HANG_UP_ON_HOOK command=18 (0x12) checksum=4 valid=YES
```

## Add to an existing external-component project

In your existing `comelit_simplebus:` block, add:

```yaml
comelit_simplebus:
  rx_pin: GPIO3  # Keep your existing input-pin configuration.
  bus_monitor: true
```

Merge with the existing block instead of creating a second one. Keep the existing ring sensor and automations. Refresh the external component to the latest repository commit before compiling (a temporary `refresh: 0s` in the `external_components` source forces checking for updates).

Alternatively, the reusable [packages/bus-monitor.yaml](../packages/bus-monitor.yaml) supplies the source, GPIO3 input, logger settings and monitor setting:

```yaml
packages:
  comelit_bus_monitor: github://prokudin07/comelit-6750w-esphome/packages/bus-monitor.yaml@main
```

Your device YAML still supplies `esphome:`, board/framework, Wi-Fi, API and OTA. The package can merge with an existing instance of this external component. Do not add a second RX handler on GPIO3.

## Add to the historical working HA sniffer

The original HA project in `reference/ha-snapshot/` uses `tp29_sniffer.h` and cannot run a second receiver interrupt on the same GPIO. For that project, you can add just the logger without replacing the working decoder:

1. Copy [bus_monitor.h](../components/comelit_simplebus/bus_monitor.h) beside `tp29_sniffer.h` in your ESPHome directory.
2. Add the helper to your existing `esphome.includes` list so ESPHome copies it into the build directory:

```yaml
esphome:
  includes:
    - comelit_protocol.h
    - comelit_control.h
    - bus_monitor.h
    - tp29_sniffer.h
```

Also add `#include "bus_monitor.h"` near the top of `tp29_sniffer.h`. Keep the other fields under your existing `esphome:` block.
3. In `tp29_process_frames()`, immediately after `bool valid = (checksum == calculated_checksum);` (possibly split over several lines), insert:

```cpp
esphome::comelit_simplebus::log_bus_frame(command, address, checksum, valid);
```

4. Keep the existing setup/process calls and HA entities. Set logger level to INFO or DEBUG and compile/install your edited firmware normally. Do not install the external-component package at the same time as the old sniffer.

The helper only logs; it does not attach an interrupt, drive a pin, transmit or alter the ring sensor. It runs from frame processing, not the ISR. The stored HA snapshot in this repository is left unchanged.

## Interpreting the events

| Event | Meaning of the received command |
|---|---|
| `CALL_RING` (48) | Entrance call ring packet; still missing in existing physical captures |
| `CALL_FINAL` (50) | Final call-initiation packet; evidence of a call to this address |
| `ANSWER_OFF_HOOK` (17) | Answer / off-hook command observed for this address |
| `DOOR_OPEN_REQUEST` (16) | Door-open request observed for this address |
| `HANG_UP_ON_HOOK` (18) | Hang-up / on-hook command observed for this address |
| `UNKNOWN_COMMAND` | Unclassified command; numeric command and address are retained |

The `address` is a **Simplebus address**, not a verified apartment number. Map it to apartments only when you have independently established the correspondence. Commands 22, 23, 49 and 63 deliberately remain unknown.

This is a log of received packets, not a guaranteed complete account of every apartment's activity. TP28 visibility, timing rejection, missing 48 frames, noise, resets and queue overflow can cause omissions. A missing answer or open request in the log means it was not observed, not that it did not occur. Repeated packets are printed individually; 48/48/50 are not merged into a single call. The monitor does not infer unanswered calls or match whole conversations. An observed command 16 does not confirm that the physical lock actually opened; 17 does not independently confirm audio connected. No ACK, audio or video decoding is added.
