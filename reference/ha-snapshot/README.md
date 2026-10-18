# Original HA source snapshot — reference only

Read from the owner's existing ESPHome configuration on 2026-10-18. Only **reception is confirmed working**; no transmission to the intercom is working. The original experimental TX code is preserved for provenance, including Russian UI/status strings. It expects a separate external transmitter on GPIO10. It must not be interpreted as a TP29 injection example or a complete transmitter hardware design.

For installation use [the receive-only external component example](../../examples/tp28-receiver.yaml). See [the comparison](../../docs/provenance.md) for differences. Do not combine both receivers on the same pin. Actual Wi-Fi secrets, API credentials and SSH credentials are excluded.
