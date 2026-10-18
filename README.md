# Comelit 6750W: Simplebus2 receive via TP28

Read Simplebus2 traffic from a **Comelit 6750W / DIG477E** PCB with an ESP32-C3 and expose an incoming-call sensor in Home Assistant using an ESPHome external component.

**The discovery:** TP28 provides a usable internal digital receive signal on the tested board. A separate analog receiver connected to the L/L bus was not needed for this setup.

**Confirmed working on the owner's installation: reception only. No transmission to the intercom is working.** The external component here is a new packaging of the working receiver, with the same decoding windows; it has not yet been flashed and verified on the physical installation. The existing HA installation was read for comparison and left unchanged.

## Hardware and wiring

Tested hardware: Comelit 6750W, PCB DIG477E, sticker `6750W SW 1.2.0`, ATSAMD20J16A in TQFP64, ESP32-C3 with separate USB power.

```text
Comelit DIG477E                         ESP32-C3
TP28 ----------------[ 10 kΩ ]--------- GPIO3 (input)
Confirmed board GND ------------------- GND
                                       USB power
TP29: leave unconnected
L/L bus: no connection to ESP GPIOs
```

TP28 measured approximately **3.3 V at idle**, relative to the confirmed board ground. Ground was checked by continuity to the negative terminals of low-voltage electrolytic capacitors and the black speaker wire. Identify and measure your own board before connecting it; test-point numbers are not a compatibility guarantee across models or PCB revisions. A 10 kΩ series resistor is not galvanic isolation or a general-purpose level shifter. Do not power the ESP from the intercom bus for this setup.

See [measurements and TP29 investigation](docs/hardware.md). TP29 is a **TX/ACK candidate only**, not a proven injection point. Do not drive it from an ESP output.

## Build with ESPHome

1. Use ESPHome **2026.9.0** or a compatible later version. The build target is ESP32-C3, `esp32-c3-devkitm-1`, Arduino framework.
2. Copy [examples/tp28-receiver.yaml](examples/tp28-receiver.yaml) to your ESPHome configuration directory.
3. Create your own `secrets.yaml` from [secrets.yaml.example](examples/secrets.yaml.example). Preserve your existing API encryption and OTA settings when adapting an existing device.
4. Set `apartment_address` to the address observed for your intercom. This is a Simplebus address, which may differ from the apartment number.
5. Validate, compile, then install through ESPHome. Do not run the old sniffer and this component on the same GPIO simultaneously.

The component is fetched directly from this repository:

```yaml
external_components:
  - source: github://prokudin07/comelit-6750w-esphome@main
    components: [comelit_simplebus]

comelit_simplebus:
  rx_pin:
    number: GPIO3
    mode: INPUT
  apartment_address: 1
  ring_duration: 3s
  ring:
    name: Incoming intercom call
  on_frame:
    - lambda: |-
        ESP_LOGI("intercom", "command=%u address=%u checksum=%u",
                 command, address, checksum);
```

The full example also includes board, Wi-Fi, API, OTA and logger settings. Standard ESPHome [external component configuration](https://esphome.io/components/external_components/) is used; no custom includes or polling lambdas are required. For reproducible deployments replace `main` with a reviewed commit or release tag.

`Incoming intercom call` turns on for three seconds after a **checksum-valid command 50 addressed to your configured address**. Another matching frame extends the pulse. This is a call-detection pulse, not a conversation-state sensor. Calls arriving less than three seconds apart may merge into one ON period. `ring_duration` accepts 1 ms–60 s. `ring` and `on_frame` are optional; `on_frame` runs for every checksum-valid frame, including other addresses, with `uint8_t command`, `address`, and `checksum`. Invalid frames are logged but do not trigger automations or the ring sensor.

Example received frame:

```text
RAW LSB: 010011 10000000 0010
command=50 address=1 checksum=4 calc=4 valid=YES
```

Reception uses an interrupt on both edges. Records are queued and processed in the main loop, so logging and ESPHome automations do not run inside the ISR. Debug logs include rejected partial frames; queue overflow is reported. Keep `on_frame` actions short to avoid losing traffic.

## Monitor calls and commands across the bus

Enable `bus_monitor: true` in `comelit_simplebus:` for readable logs of calls, answer/off-hook, door-open requests, hang-up/on-hook and unknown commands at **every received bus address**. This does not change the address filter for your ring sensor.

See [the complete bus monitor example](examples/bus-monitor.yaml), [the reusable package](packages/bus-monitor.yaml) and [instructions including the historical HA sniffer](docs/bus-monitor.md). An address is not necessarily an apartment number, and an observed door-open command does not prove that the lock opened. Missing packets remain possible.

## Findings and evidence

- [Hardware measurements, wiring, TP28, TP29 and J3](docs/hardware.md)
- [Protocol, observed commands and missing 48 packets](docs/protocol.md)
- [Original address-3 episode log](docs/episode_addr3_log.md)
- [Comparison with the working HA project and archive](docs/provenance.md)
- [Validation and limitations](docs/validation.md)
- [Original HA source snapshot](reference/ha-snapshot/) and [archive diagnostic header](reference/handoff/tp29_sniffer.h)

The HA snapshot contains older **experimental TX software** and its original Russian entity/status names for fidelity. It is reference material, not the installation example. It uses GPIO10 for a separate transmitter circuit; it does not establish that TX works and does not justify connecting GPIO10 to TP29 or L/L. The supported component in `components/` is receive-only and does not configure a TX pin.

## Background and credits

TP28 was reported by [prokudin07 in Elektroarzt issue #20](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/issues/20). The [follow-up comment](https://github.com/Elektroarzt/simplebus2-mqtt-bridge/issues/20#issuecomment-6250885180) asks for public code and timing captures. This repository shares the available receiver code, measured timing notes and decoded log; native oscilloscope/logic-analyzer capture files were not present in the supplied materials.

Protocol background and command interpretations come from [plusvic/simplebus2-intercom](https://github.com/plusvic/simplebus2-intercom). They are reverse-engineered interpretations, not an official Comelit protocol specification. Also see [Elektroarzt/simplebus2-mqtt-bridge](https://github.com/Elektroarzt/simplebus2-mqtt-bridge) for its separate analog interface, [mansellrace/comelit-esphome](https://github.com/mansellrace/comelit-esphome), and [vvigilante/comelit-simplebus1](https://github.com/vvigilante/comelit-simplebus1) for older Simplebus1 work. Test-point identities from different models must not be transferred to DIG477E without measurement.

Independent research project; not affiliated with Comelit.
