# Source provenance and HA comparison

Compared on 2026-10-18 using read-only SSH access to the owner's Home Assistant configuration. No firmware was installed, no TX switch was enabled, and no intercom actions were invoked.

| Material | Role |
|---|---|
| User-supplied `comelit_simplebus2_handoff.zip` | Measurements, protocol notes, diagnostics, one decoded log and an older minimal header |
| Current HA `comelit.yaml` plus three headers | Existing deployed project; the owner confirms reception works and transmission does not |
| `components/comelit_simplebus/` | New receive-only external component derived from the working RX algorithm |

## Archive versus HA

The ZIP has a 9701-byte `tp29_sniffer.h` and a short ESPHome setup/process snippet. It has no HA ring entity or control/protocol headers. Its filename is historical: the actual RX point is **TP28**, connected to GPIO3.

The HA snapshot has a 19486-byte `tp29_sniffer.h`, `comelit_protocol.h`, `comelit_control.h` and a complete YAML. The three headers exactly match the previously saved local Comelit-ESPHome project. The YAML uses the secret key `wifi_pass` instead of the local example's `wifi_password`.

HA adds `comelit::on_frame` after checksum verification, a configurable apartment address, a ring pulse and experimental control software. The control file contains a GPIO10 RMT transmitter, buttons for commands 16/17/18, a TX-enable switch and status reporting. These are software facilities for a separate analog transmitter, **not evidence of a working TX hardware path**. TP29 is not used by that code. Per the owner's explicit clarification, only RX currently works.

## New external component

The new component keeps the timing windows, ignored short intervals and state transitions. It replaces global Arduino ISR functions with an ESPHome component and configurable input GPIO, stores payload bits in a word, queues frame/reject records, checks the checksum in the main loop and offers an optional ring binary sensor and `on_frame` automation. The ring predicate remains valid command 50 at the configured address. There is no encoder, RMT, output GPIO, TX switch or door-open action in the supported component.

This is a port, not a byte-identical binary or a fix for missing command 48. It needs a physical RX comparison before replacing the deployed firmware.

## Preserved material

`reference/ha-snapshot/` preserves the four HA files, including original Russian strings. Its Wi-Fi values are `!secret` references; the actual secrets file is not included. `reference/handoff/tp29_sniffer.h` preserves the earlier ZIP header. The address-3 log is copied unchanged, including its trailing `**svg**` artifact. The conversation handoff prompt is not an installation instruction and is not needed for using this repository.

Snapshot SHA-256 hashes follow in `reference/ha-snapshot/SHA256SUMS`.
