# Validation

The supported target is ESP32-C3 (`esp32-c3-devkitm-1`) with ESPHome 2026.9.0 and Arduino framework. Other targets and versions are not claimed as tested.

On 2026-10-18, the complete external-component example successfully compiled locally with ESPHome 2026.9.0, Arduino-ESP32 3.3.11 and dummy Wi-Fi credentials. The image was 844490 bytes, with 103834 bytes RAM reported by the build. There were no compiler warnings or errors in that build. The real HA files were not modified or flashed.

Host checks passed with C++17, `-Wall -Wextra -Werror` and address/undefined-behavior sanitizers, including nine raw frames extracted from the original log. The schema also rejects an RX pin configured with output or open-drain mode. GitHub Actions is configured to repeat the host tests and a full ESPHome build.

Run the host decoder regression checks from the repository root:

```sh
python3 tests/run.py
```

The checks use the actual `decoder.h`, decode raw bit strings from the supplied log, feed synthetic edge intervals through the same state machine, exercise command 48 and known commands, malformed input, checksum errors, timing boundaries and unsigned timestamp rollover. Synthetic intervals are generated test fixtures, not additional hardware captures.

Build the full example after creating a private `secrets.yaml`:

```sh
esphome config examples/tp28-receiver.yaml
esphome compile examples/tp28-receiver.yaml
```

The repository example fetches the component from GitHub. For local development use `source: {type: local, path: ../components}` instead of the GitHub source.

The existing HA receiver is reported working by its owner. The newly packaged external component has not been flashed or physically verified. Firmware compilation and host tests cannot establish electrical compatibility, complete bus coverage, detection of the missing 48 frames or correct behavior on another installation. There is no supported TX implementation in the external component.

For a local compile with dummy credentials and no changes to your configuration, run `python3 tests/build.py` with ESPHome on `PATH`. The temporary example uses this checkout’s component.

Whole-bus monitor regression checks replay the address-3 event sequence through the actual logging helper, verify other addresses are retained, suppress invalid frames and keep unknown commands unclassified. `tests/build.py` defaults to the complete bus-monitor example; pass `tp28-receiver.yaml` to build the original minimal example instead.

The whole-bus monitor example also compiled locally on 2026-10-18 with ESPHome 2026.9.0 / ESP32-C3 Arduino (844888 bytes image). The reusable package was validated merged into the original receiver example. Neither build was installed on hardware.

A temporary copy of the original HA project was also compiled successfully after adding `bus_monitor.h` to `esphome.includes` and calling its helper from the existing frame processor. Three existing empty-format logging warnings remain in the historical sniffer. The installed HA project and preserved snapshot were not changed.
