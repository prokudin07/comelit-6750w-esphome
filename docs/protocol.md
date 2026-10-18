# Protocol observations and open questions

The reference model is [plusvic/simplebus2-intercom](https://github.com/plusvic/simplebus2-intercom): 18 bits, LSB first within each field, comprising a 6-bit command, 8-bit address and 4-bit checksum. The checksum is the number of set bits in the first 14 bits, masked with `0x0F`.

Nominal signaling is a 3 ms burst of 25 kHz carrier, followed by 17 ms silence for the preamble; each data bit has a 3 ms burst followed by 3 ms silence (zero) or 6 ms silence (one). A following burst delimits the last silence.

## Captured example

```text
010011 10000000 0010   (LSB first)
command 50 / address 1 / checksum 4 / calculated checksum 4
```

This is an incoming-call observation. Command 50 is the final call-initiation packet in the published sequence, not the user's hang-up action.

| Command | Published interpretation / local observation |
|---|---|
| 16 | Door-open command; observed at addresses 3 and 111 |
| 17 | Off-hook / answer; observed at address 3 |
| 18 | On-hook / end conversation; observed at address 3 |
| 19 | Published: call switchboard; not demonstrated locally |
| 20 | Published: enable camera/video; not demonstrated locally |
| 21 | Published: floor-door CFP ring; not demonstrated locally |
| 48 | Published: entrance ring; two packets before 50; missing from current decoded captures |
| 50 | Published: entrance ring/final call-initiation packet; observed at addresses 1 and 3 |
| 22, 23 | Unknown; reported 22/84 → about 9 s → 23/84 and similarly at address 92 |
| 49 | Unknown; appears during the address-3 session |
| 63 | Unknown; five repeated 63/255 frames after the session |

The [address-3 log](episode_addr3_log.md) starts with statistics showing a prior 50/3, then full frames for 17/3 at 21:49:23, 49/3 at 21:49:24, 16/3 at 21:49:25 and 18/3 at 21:49:28. Five 63/255 frames follow at 21:49:38–40. Their checksum is 14, matching the fourteen set payload bits. This weak checksum and repetitive payload do not establish the command's semantics or independently prove the waveform is a normal packet.

The handoff also reports a similar session at address 57; its underlying log was not supplied. Correlation does not establish the meanings of 22, 23, 49 or 63.

## Why are the two 48 packets missing?

The published sequence is `48 → 48 → 50`. Existing local decoding catches 50 without the preceding 48 packets. **The cause is unresolved.** The supplied episode begins after 50; it contains reject counters, not the rejected intervals and partial packets needed to reconstruct the beginning of the call. It cannot prove that the bus omitted 48, that TP28 filtered it, or that the decoder rejected it.

The external component deliberately preserves the working significant-interval state machine:

| Check | Accepted interval (µs) |
|---|---|
| Carrier interval | 3000–3800 |
| Preamble gap | 14500–19000 |
| Bit zero gap | 2300–3500 |
| Bit one gap | 5000–7000 |
| Ignored short edge interval | Below 2000 |

Each edge updates the timestamp **even when its interval is ignored**. This is the historical algorithm; it does not accumulate the duration of a continuous carrier burst. A continuous 25 kHz edge train therefore cannot itself produce a 3 ms adjacent-edge interval. The exact relationship between TP28 edges and the assumed burst envelope needs a raw trace. Changing timestamp updates to accumulate short intervals would change the algorithm and requires evidence before deployment.

With ideal accepted intervals, software tests decode 48 as well as 50: there is no explicit command-48 filter. This does not show that real 48 frames satisfy the same windows. Resynchronization after malformed input and edge shape remain possible causes; timing ranges have not been widened to conceal the missing evidence.

## Next useful capture

Keep the working installation intact. Capture TP28 a few seconds before a known call until after 50, ideally simultaneously with an independent observation of bus signaling. Save native scope/analyzer data, edge timestamps and signal levels, and decoder debug output. Preserve any `REJECT state`, bit count, partial bits and rejected interval before 50; check overflow counters. Include ACK boundaries and a known answer/open/hang-up sequence.

If debug output is insufficient, a diagnostic firmware can add a bounded ring of all significant intervals around every preamble and rejection. Keep this out of the production firmware until its ISR overhead and overflow behavior are checked. Do not infer electrical waveform details from decoded logs alone.
