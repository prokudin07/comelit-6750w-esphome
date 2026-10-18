# Hardware observations

These are the owner's reported measurements, not measurements newly taken while preparing this repository.

| Item | Observed value or connection |
|---|---|
| Intercom | Comelit 6750W / Simplebus2 |
| PCB | DIG477E |
| Firmware sticker | 6750W SW 1.2.0 |
| MCU | ATSAMD20J16A, TQFP64 |
| Board ground | Continuity to low-voltage electrolytic negative terminals and black speaker wire |
| TP7 | Approximately 0.2 mV DC |
| TP8 | Approximately 250 mV DC |
| TP28 | Approximately 3.3 V DC at idle |
| TP29 | Approximately 0.4 mV DC at idle |
| J3 TX | Approximately 3.3 V DC |
| J3 RX | Approximately 450 mV DC |

All voltages above were measured relative to the confirmed Comelit board ground. The proven passive receive connection is TP28 → 10 kΩ → ESP32-C3 GPIO3, with common board/ESP ground and separate USB power for the ESP. The bus L/L terminals are not GPIO-level signals or a substitute for the confirmed board ground.

## TP28 timing

| Feature | Reported observation |
|---|---|
| Underlying carrier | Approximately 25 kHz |
| Carrier / significant burst interval | Approximately 3.2–3.5 ms |
| Preamble gap | Approximately 16.7 ms |
| Zero gap | Approximately 2.5–3.1 ms |
| One gap | Approximately 5.6–6.2 ms |

These are summarized timing observations. No native scope capture, waveform image or timestamped raw edge series was included in the handoff ZIP. The address-3 log contains decoded frames, not a raw electrical trace.

## TP29: candidate, not a connection recipe

The owner continuity-traced TP29 to MCU physical pin 44, identified in the handoff as PA23. Idle was approximately 0 V. During incoming-call activity, a roughly 25 kHz square wave appeared in four approximately 3 ms bursts separated by three approximately 3 ms gaps. This resembles the four-burst ACK described in [plusvic's reverse engineering](https://github.com/plusvic/simplebus2-intercom).

The interpretation is a possible MCU-side TX/ACK control signal. Its output mode, downstream circuit and safe injection method remain unknown. The current installation has **no working transmission to the intercom**.

Do not connect an ESP push-pull output to TP29: PA23 may also drive the line. Neither a series resistor nor an open-drain transistor alone proves an unknown connection safe. Before considering injection, trace both PCB sides to the driver stage, identify components, and passively compare TP29 waveforms during call, answer, door-open and hang-up. Establish voltage limits and whether the MCU drives high/low or becomes high impedance. Do not publish a TX wiring recipe until those facts are established.

## J3

The service/debug footprint has labels including VDD, SWC, SWD, RST, TX, RX, MISO, MOSI, SS and SCK. A factory programming/debug function is plausible; J3 TX/RX have not been established as Simplebus signals. Any further investigation should begin with passive measurement only.
