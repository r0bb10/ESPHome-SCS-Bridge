# BTicino SCS Bus Guide

This guide describes the native BTicino SCS wire protocol: the electrical
signal, bytes, telegrams, acknowledgements, and shared-bus rules. It is not an
OpenWebNet guide. OpenWebNet is a separate, higher-level text protocol used by
some BTicino gateways.

SCS is undocumented by BTicino. The information here comes from independent
bus captures and compatible implementations, corroborated by the native SCS
controller firmware in an official BTicino gateway. Treat device-specific
command meanings as evidence to verify on the relevant installation.

## What The Bus Is

SCS uses two wires for both power and communication. The idle bus voltage is
about 27 V. A sender communicates by briefly loading the bus, reducing its
voltage. Multiple devices share the same wires, so every sender must first wait
for a quiet bus and stop if another device is transmitting.

Do not connect a microcontroller GPIO directly to SCS. Use an approved SCS
interface, or a properly designed isolated/conditioned interface. A bus-side
interface must safely detect the small voltage drop and load the bus with the
correct impedance without creating excessively sharp edges.

## Quick Reference

| Item | Value |
| --- | --- |
| Nominal cell duration | 104 us |
| Nominal bit rate | 9600 baud |
| Dominant pulse for a `0` | about 35 us low, then 69 us released |
| Logical `1` | released for the full 104 us |
| Byte order | LSB first |
| Standard telegram | `A8` + 4 payload bytes + XOR + `A3` |
| Extended telegram | `A8` + 8 payload bytes + XOR + `A3` |
| Acknowledgement | `A5` |

## Bits And Bytes

Each byte has ten cells:

```text
start bit | data bit 0 | data bit 1 | ... | data bit 7 | stop bit
```

The start bit is `0`, the stop bit is `1`, and the eight data bits are sent
least-significant bit first.

SCS is return-to-zero rather than conventional UART signalling. A logical `0`
is a short dominant-low pulse followed by release. A logical `1` has no pulse.
Sampling at roughly 20 percent of each cell distinguishes the two values:

```text
cell:       |<------------- 104 us ------------->|
logical 0:  low for about 35 us, then high/released
logical 1:  high/released for the whole cell
sample:          ^ approximately 20 percent
```

The physical voltage polarity can be inverted by a particular interface. In
this document, "low" means the dominant, bus-loaded state and "high" means the
released state.

## Telegram Format

A normal telegram is seven bytes:

```text
A8 P0 P1 P2 P3 XOR A3
```

An extended telegram is eleven bytes:

```text
A8 P0 P1 P2 P3 P4 P5 P6 P7 XOR A3
```

`A8` is the start marker. `A3` is the transmitted end marker. `P0` through
`P3`, or `P0` through `P7`, are the payload. Their meanings depend on the SCS
system and device family: lighting, automation, video entry, temperature, and
other systems use different payload meanings.

The checksum is the XOR of every payload byte. It does not include `A8` or
`A3`.

Example:

```text
A8 33 00 12 00 21 A3
```

The checksum is `33 XOR 00 XOR 12 XOR 00`, which is `21`.

### Receiving Frames

For interoperable traffic, transmit `A8`, the payload, the XOR, and `A3`.

Official BTicino gateway firmware always transmits `A3`, but its recovered RX
validator accepts a complete frame when `A8` and the XOR are valid without
checking the final byte. A receiver may mirror that compatibility behavior or
require `A3` for stricter noise rejection; requiring `A3` is not a guarantee
that every OEM-compatible sender/receiver pair behaves identically.

### ACK

`A5` is a one-byte acknowledgement. It is not a state report and it does not
have the normal `A8 ... XOR A3` envelope.

An addressed device can send `A5` after receiving a valid command. Whether a
specific message expects an ACK depends on its use and device family.

## Sharing The Bus

SCS is a shared medium. A sender must not begin in the middle of someone
else's frame.

A compatible gateway behaves broadly as follows:

1. Wait until the bus is quiet.
2. Wait a randomized access delay before starting.
3. Transmit while monitoring the receive signal.
4. Abort immediately if the observed bus state disagrees with the transmitted
   state, or if an unexpected receive event occurs.
5. Retry only after another randomized access delay.

The recovered BTicino implementation uses an approximately 5.2 to 31.7 ms
random access delay. This avoids repeated collisions when two devices want to
send at the same time.

For an ACK-required command, the recovered controller waits about 2.843 ms
after the end of transmission for `A5`. If no ACK arrives, it waits about
2.912 ms and retries. It allows three total attempts. For no-ACK messages, it
emits eight copies. These are observed BTicino gateway behaviors, not a public
universal SCS specification. A receiver that publishes every copy would act on
the same command up to eight times, so it commonly suppresses identical
telegrams that repeat within a short window.

## Building An SCS Device

Separate an implementation into four concerns:

1. **Electrical interface:** safely convert the 27 V bus to logic levels and
   create the controlled bus load for transmission.
2. **PHY:** generate and sample 104 us cells, including start/stop bits and
   LSB-first bytes.
3. **Link layer:** assemble frames, validate XOR, handle ACKs, quiet-bus
   access, retries, and collisions.
4. **Device semantics:** interpret or construct payload bytes for a specific
   BTicino system and product family.

Keeping these independent makes it possible to test the protocol without a
real bus, and prevents an uncertain device command from changing reliable
physical transport code.

## What Must Be Measured

Even with a correct protocol implementation, confirm these details on the
actual hardware and installation:

- Interface-side idle polarity and voltage levels.
- Acceptable pulse width and edge shape for the chosen bus interface.
- ACK turnaround time from the target device.
- TX echo latency and collision behavior with another active device.
- The payload meanings, address rules, and ACK requirements for the device
  family being controlled.

## Further Reading

- Local reverse-engineering evidence: [`.dev/SCS.md`](../.dev/SCS.md)
- GuidoPIC SCS analysis: https://guidopic.altervista.org/alter/eibscsgt.html
- Michael Stapelberg's SCS capture and decoder work:
  https://michael.stapelberg.ch/posts/2020-09-28-nuki-scs-bticino-decoding/
- Michael Stapelberg's SCS signal-processing notes:
  https://michael.stapelberg.ch/posts/2020-11-30-scs-processing-microcontroller/
