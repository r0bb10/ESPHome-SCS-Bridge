# OpenWebNet ↔ SCS translation reference

Recovered templates for translating OpenWebNet (OWN) messages into native
BTicino SCS telegrams. Primarily F461 firmware, with separately identified
MX/300EOS static analysis and physical-bus observations. This is a translation
blueprint, not a complete specification of every SCS system.

## Contents

- [Notation and evidence](#notation-and-evidence)
- [Telegram construction](#telegram-construction)
- [Address encoding](#address-encoding)
- [WHO 0 — Scenarios](#who-0--scenarios)
- [WHO 1 — Lighting](#who-1--lighting)
- [WHO 2 — Automation](#who-2--automation)
- [WHO 4 — Thermoregulation](#who-4--thermoregulation)
- [WHO 8 — Intercom / video entry](#who-8--intercom--video-entry)
- [WHO 9 — Auxiliary](#who-9--auxiliary)
- [WHO 15 — CEN](#who-15--cen)
- [WHO 18 — Energy / supervision](#who-18--energy--supervision)
- [WHO 25 — CEN+ / interfaces](#who-25--cen--interfaces)
- [Unmapped domains](#unmapped-domains)
- [Extending the tables](#extending-the-tables)

## Notation and evidence

| Notation | Meaning |
| --- | --- |
| `WHO`, `WHAT`, `WHERE`, `DIM` | OWN domain, operation, target, dimension |
| `W` | OWN WHERE text; its encoding is domain-specific |
| `w` | Encoded one-byte address, where applicable |
| `P0…P3` / `P0…P7` | Four / eight native payload bytes |
| `ACT`, `b`, `s`, `v` | Action byte, decimal button, press subtype, value |
| `11 00 12 00` | Hexadecimal **bytes**; no envelope or checksum |
| `D100023210010401` | Hexadecimal **string**; split into pairs from the left |
| `—` | No recovered mapping, not a zero byte |

OWN operation/parameter numbers are decimal unless stated otherwise. Address
digits must be parsed structurally, not converted indiscriminately from decimal
to binary: basic WHERE `11` becomes byte `0x11`, whereas button `11` becomes
byte `0x0B`. Preserve zero-padding where the OWN parser requires it.

| Evidence | Interpretation |
| --- | --- |
| **Mined** | Produced by the running OEM stack, or decoded through live injection; does not by itself prove physical-device acceptance |
| **Bus** | Observed on a physical SCS installation |
| **RE** | Recovered from OEM code; not necessarily exercised live |
| **Open** | Incomplete, ambiguous, gated, or unverified |

Newer verified tables override older studies and disproved/blocker notes.
Public OWN syntax alone is **not** evidence of an SCS translation. “No frame”
below means none observed under the tested stack/configuration, not that the
operation can never produce bus traffic.

| OWN frame family | Grammar |
| --- | --- |
| Command / normal event | `*WHO*WHAT*WHERE##` |
| Status request | `*#WHO*WHERE##` |
| Dimension request | `*#WHO*WHERE*DIM##` |
| Dimension write | `*#WHO*WHERE*#DIM*V1[*V2…]##` |

`#` inside WHAT/WHERE introduces domain-specific subfields; it is not a native
byte delimiter. All OWN frames end in `##`.

## Telegram construction

### Native envelope

| Telegram | On-wire bytes | Size |
| --- | --- | --- |
| Standard | `A8 P0 P1 P2 P3 XOR A3` | 7 bytes |
| Extended | `A8 P0 P1 P2 P3 P4 P5 P6 P7 XOR A3` | 11 bytes |
| Bus acknowledgement | `A5` | 1 byte, no envelope |

`XOR = P0 ^ P1 ^ …` over **payload bytes only**. Do not include `A8` or `A3`.
Transmit `A3`; OEM reception tolerates an unchecked final byte, but that is not
a reason to omit it.

```text
OWN:      *1*1*11##
payload:  11 00 12 00
XOR:      11 ^ 00 ^ 12 ^ 00 = 03
telegram: A8 11 00 12 00 03 A3

OWN:      *1*1#2*53##
payload:  D1 53 01 42 0D 00 00 02
XOR:      D1 ^ 53 ^ 01 ^ 42 ^ 0D ^ 00 ^ 00 ^ 02 = CE
telegram: A8 D1 53 01 42 0D 00 00 02 CE A3
```

Construction order: parse OWN → select WHO template → encode WHERE and
parameters → produce each payload in sequence → add its own envelope/XOR.
There is no universal WHO byte or universal destination/source layout.

### Captured `$` records are not bus telegrams

The OEM gateway transports payloads to/from its coprocessor using ASCII records.
Tables here show the native bytes; the command column is retained for tracing
the capture, not added to the payload.

| Record, channel 0 | Meaning | Native payload width |
| --- | --- | --- |
| `$003<hex>` | Outbound standard transaction, command `03` | 8 hex characters / 4 bytes for the mined standard forms |
| `$004<hex>` | Outbound standard transaction, command `04` | 8 hex characters / 4 bytes |
| `$005<hex>` / `$006<hex>` | Outbound extended transaction | 16 hex characters / 8 bytes |
| `$017<hex>` | Received standard payload, command `17` | 8 hex characters / 4 bytes |
| `$018<hex>` | Received extended payload, command `18` | 16 hex characters / 8 bytes |

The first digit after `$` is a channel selector; the next two digits are the
command. For example, `$006D15301420D000002` contains
`D1 53 01 42 0D 00 00 02`, **not** a byte `D` followed by byte `53`.
Older channel-less examples such as `$06…` use a different record notation.
An opcode `06` does not imply payload class `D2`: mined `D1` and `EC` frames
also use `06`. F461 injected RX records require bare LF; CRLF leaves a stray
CR in the payload. Terminators are never native payload data.

OWN `*#*1##` / `*#*0##` responses and native `A5` are different acknowledgements.
OWN NACKs and timeouts can coexist with emitted SCS frames; neither establishes
whether a bus device executed a command. Preserve multi-frame order. Repeated
bus copies are not additional logical operations; deduplicate at the semantic
consumer if necessary, never filter `A5` as a repeated data frame.

## Address encoding

### Lighting / automation / basic scene addressing

| OWN WHERE | Standard encoding | Extended lighting encoding | Evidence / limitation |
| --- | --- | --- | --- |
| `0` | `B1 00` for WHO 1/2 general commands | `D2 00 01 …` | Mined; not a universal broadcast address for other WHOs |
| `11` | `11 00` | `D1 11 01 …` | Mined |
| `12` | `12 00` | `D1 12 01 …` | Mined, uncommissioned target |
| `21` | `21 00` | `D1 21 01 …` | WHO 2 mined |
| `53` | `53 00` | `D1 53 01 …` | WHO 1 mined |

For these simple address forms, read the two address digits as hexadecimal
digits (`53` → `0x53`, not decimal 53 → `0x35`). This is not proof that every
room, group, interface-suffixed, or four-digit address uses the same template.

Older `bt_luci` RE contains address formatter branches `B100`, `B5%02X`,
`B3%02X`, `%02d00`, and `%X%X00`. Their complete binding to OWN group/area
syntax is unresolved here; do not replace all such forms with `w 00`.
The four-digit and `#4#I4` forms verified for **CEN** below must not be assumed
verified for lighting merely because the public grammar also accepts them.

### Domain-specific addressing

| Domain | Address rule | Example |
| --- | --- | --- |
| WHO 8 unlock | Verified `20/21/22` → `A0/A1/A2` | `*8*19*21##` → `96 A1 6F A4` |
| WHO 15 basic CEN | Two-digit WHERE → one packed byte | `21` → `21` |
| WHO 15 four-digit CEN | Decimal `AA`, `PP` in `AAPP`: `w = (A << 4) OR PL` | `0215` → `2F`; `1001` → `A1` |
| WHO 15 bus suffix | `#4#I4` → `EC i 00 00` before CEN payload; `i` is decimal I4 as binary | `#4#12` → `EC 0C 00 00 …` |
| WHO 18 | Packed nibble-level address field; use the recovered lookup | See WHO 18 |
| WHO 25 CEN+ | WHERE is `2` followed by decimal object `n`, 0–2047 | `22010` → object 2010, not address 22010 |
| WHO 25 dry contact | Verified WHERE `31` → interface index `01` | `B4 01 …` |

## WHO 0 — Scenarios

**Mined.** Standard transactions, command `03`.

| OWN | Payload | Constraint |
| --- | --- | --- |
| `*0*S*W##` | `w 00 14 s` | Scene `S=1…20`; `s` is S encoded in binary |
| `*#0*11##` | `11 00 1C 80` | Exact captured status request; other WHEREs not independently shown |

| Example | Payload |
| --- | --- |
| `*0*1*11##` | `11 00 14 01` |
| `*0*20*11##` | `11 00 14 14` |

The status request is **not** `11 00 14 C8`: its capture is
`$00311001C80`, which splits as `11 00 1C 80`. No OWN events resulted from
injected `21 00 14 01/02`, `21 00 14 C8`, or `B3 00 14 01`.
Activation feedback, F420 controls `40…46`, and other address forms remain open.
The first scene matrix had delayed responses contaminating later rows; use
its ordered scene sequence, not each row's apparent request association.

## WHO 1 — Lighting

### Standard commands

**Mined**, 172-vector matrix across WHERE `0`, `11`, `12`, `53`.

| OWN | Command | Payload |
| --- | --- | --- |
| `*1*WHAT*0##` | `04` | `B1 00 12 ACT` |
| `*1*WHAT*W##`, addressed | `03` | `w 00 12 ACT` |
| `*#1*0##` | `04` | `B1 00 15 00` |
| `*#1*W##`, addressed | `03` | `w 00 15 00` |

`12` is the shared lighting/automation discriminator; its deeper meaning is
unresolved. `15 00` is a status-read marker, not a state report.

| WHAT | Meaning | ACT / formula |
| --- | --- | --- |
| `0` | OFF | `01` |
| `1` | ON | `00` |
| `2…10` | Preset 20…100% | `((WHAT - 1) << 4) OR 0D` → `1D…9D` |
| `30` | Level up | `03` |
| `31` | Level down | `04` |
| `32` | Toggle | No frame in the matrix |

**ON=`00`, OFF=`01`.** For presets, 20% → `1D`, 50% → `4D`, 100% → `9D`;
the high nibble is one less than the level in tens.

| WHAT | Timed ON duration | ACT | WHAT | Blink period | ACT |
| --- | --- | --- | --- | --- | --- |
| `11` | 1 min | `16` | `20` | 0.5 s | `0B` |
| `12` | 2 min | `26` | `21` | 1 s | `1B` |
| `13` | 3 min | `36` | `22` | 1.5 s | `2B` |
| `14` | 4 min | `46` | `23` | 2 s | `3B` |
| `15` | 5 min | `56` | `24` | 2.5 s | `4B` |
| `16` | 15 min | `66` | `25` | 3 s | `5B` |
| `17` | 30 s | `76` | `26` | 3.5 s | `6B` |
| `18` | 0.5 s | `86` | `27` | 4 s | `7B` |
| — | — | — | `28` | 4.5 s | `8B` |
| — | — | — | `29` | 5 s | `9B` |

### Extended commands and dimensions

**Mined** parameterized commands use command `06`:

| OWN | Addressed payload | General WHERE `0` |
| --- | --- | --- |
| `*1*0#S*W##` | `D1 w 01 42 0D 01 00 s` | Replace `D1 w` with `D2 00` |
| `*1*1#S*W##` | `D1 w 01 42 0D 00 00 s` | Same replacement |
| `*1*30#L#S*W##` | `D1 w 01 42 0D 03 l s` | Same replacement |
| `*1*31#L#S*W##` | `D1 w 01 42 0D 04 l s` | Same replacement |

`l` and `s` are binary parameter values; the matrix exercised `L=3`, `S=2`.
Accepted ranges are not established by those samples. Example:
`*1*1#2*53##` → `D1 53 01 42 0D 00 00 02`.

The summary additionally records the following dimension templates; unlike
the commands above, their concrete samples are not in the retained 172-vector
matrix. Keep these as **recorded / needing re-verification**:

| OWN | Recorded addressed payload / result |
| --- | --- |
| `*#1*W*1##` | `D1 w 01 43 0D 0D 00 00` — brightness read, command `06` |
| `*#1*W*4##` | `D1 w 01 43 0D 00 00 00` — dimmer-100 read, command `06` |
| `*#1*W*3##` | No frame; gateway-local in the notes |
| `*#1*W*#1*L*S##` | No frame; gateway-local in the notes |
| `*#1*W*#2*1*2*3##` | Recorded `D1 w 01 42 06 01 02 03`, command `06`; parameterization needs confirmation |

The old summary calls dimension `#2` “HSV”, but the OWN catalog identifies
dimension `2` as timer write and `12` as HSV. Preserve the captured shape
without relabelling it as a proven color command. PIR/illuminance/motion,
color, color-temperature, and full brightness reply layouts are not recovered.
`*1*1000#1*W##` produced no outbound frame in the matrix.

### Bus → OWN action decoding

**Mined**, complete 256-action sweep at address `11`: 110 action values
produce events. Payload `w 00 12 ACT` → `*1*1000#Q*W##`, except automation
actions listed in WHO 2. Let `H=ACT>>4`, `L=ACT AND 0F`.

| ACT condition | Q / OWN operation |
| --- | --- |
| `L=0`, any H | `1` — ON |
| `L=1`, any H | `0` — OFF |
| `L=3`, any H | `30` — level up |
| `L=4`, any H | `31` — level down |
| `06` | `1` — ON; omitted from the older summary table |
| `H6`, H=1…8 | `10+H` — timed ON `11…18` |
| `96` | `50#9` |
| `A6` | `50#10` |
| `HB`, H=0…15 | `20+H` — includes inbound-only values through `35` |
| `HD`, H=0…15 | `H+1` — includes inbound-only values through `16` |

Other actions, including `B6…F6`, do not emit lighting events in the sweep.
An inbound-only operation range is not proof of an outbound encoder.

Reply to gateway `0x00`: `65 00 12 00` → `*1*1000#1*65##`;
`65 00 12 01` → `*1*1000#0*65##` (**Mined**, independent bus corroboration).
Do not inject `B1 00 12 ACT` as a device reply: the stack ignores that
outbound broadcast class. `B8` is an all-devices status broadcast class; the
exact retained lighting broadcast event templates are not tabulated here.

## WHO 2 — Automation

### Standard commands and sequences

**Mined**, 24-vector matrix plus longer-window verification.
Use `w 00 12 ACT` / command `03` when addressed, or
`B1 00 12 ACT` / command `04` for general WHERE `0`.

| OWN WHAT | Operation | Payload action sequence, in order |
| --- | --- | --- |
| `0` | STOP | `0A` |
| `1` | UP | `0A`, then `08` |
| `2` | DOWN | `0A`, then `09` |

Each action is a separate standard telegram. The stop precedes movement;
short capture windows often retain only `0A`. The notes require ≥3–4 s to
observe the late second frame; they do **not** establish an exact required
inter-frame delay for an independent implementation.

| OWN request | Payload | Command |
| --- | --- | --- |
| `*#2*W##`, addressed | `w 00 15 00` | `03` |
| `*#2*0##` | `B1 00 15 00` | `04` |
| `*#2*W*10##`, addressed | `D1 w 01 43 09 00 00 00` | `06` |
| `*#2*0*10##` | `D2 00 01 33 09 00 00 00` | `06` |

Dimension-10 byte `09` is observed but semantically opaque. The general
extended read uses `33`, not the addressed `43`.

No frames for tested `*2*1000#1*W##`, `*#2*W*#11*50*20##`,
or `*#2*W*#10##`. Position/goto-level behavior with a real actuator remains
open; public six-field dimension-10 replies do not prove a native position
format or position interpolation in MX COSO.

### Bus → OWN

| Payload | OWN event | Evidence |
| --- | --- | --- |
| `w 00 12 08` | `*2*1000#1*W##` | Mined |
| `w 00 12 09` | `*2*1000#2*W##` | Mined |
| `w 00 12 0A` | `*2*1000#0*W##` | Mined |
| `B8 w 12 08` | `*2*1*W##` | Mined + bus corroboration |
| `B8 w 12 09` | `*2*2*W##` | Mined + bus corroboration |
| `B8 w 12 0A` | `*2*0*W##` | Mined + bus corroboration |
| `B3 06 12 0A` | `*2*0*6##` — room-scoped | Mined + bus corroboration |

`10` in place of marker `12` produces no movement event. Status-read
`15 00` produces no state event.

**Separate MX RE, not an F461 substitute:** older plugins construct
`$03E4000000B10008#` (UP), with `0A`/`09` variants, and a dimension-10 read
`B1 <address> 00 00`. The E4 representation conflicts with the mined F461
path and its bus expansion is unresolved. Do not envelope its apparent
seven-byte string as a standard or extended telegram.

## WHO 4 — Thermoregulation

### General-zone mode writes

**Mined**, general WHERE `#0`; command `06`, eight-byte payloads.
The byte prefix is `D1 00 03 02`, not `D1 00 30 2C`.

| OWN WHAT, in `*4*WHAT*#0##` | Operation | Payload |
| --- | --- | --- |
| `102` | Heating, protection | `D1 00 03 02 C1 17 00 00` |
| `103` | Heating, OFF | `D1 00 03 02 C1 16 00 00` |
| `202` | Cooling, protection | `D1 00 03 02 C1 27 00 00` |
| `203` | Cooling, OFF | `D1 00 03 02 C1 26 00 00` |
| `302` | Generic, protection | `D1 00 03 02 C1 07 00 00` |
| `303` | Generic, OFF | `D1 00 03 02 C1 06 00 00` |
| `111` | Heating, automatic | `D1 00 03 02 C2 00 10 00` |
| `311` | Generic, automatic | `D1 00 03 02 C2 00 00 00` |
| `13002#3103` | Heating weekly program, exact sample | `D1 00 03 02 C1 15 02 02` |
| `23002#3103` | Cooling weekly program, exact sample | `D1 00 03 02 C1 25 02 02` |
| `3100` | Scenario/off mix; meaning incomplete | `D1 00 03 02 C1 01 FF 00` |
| `3200` | Scenario/off mix; meaning incomplete | `D1 00 03 02 C1 03 FF 00` |

The original field grouping `D | 1000 | 30 | 2C | tail` is a **nibble-level**
formatter. Heating/cooling/generic values `11/12/10` and mode tails `7/6`
straddle the actual byte boundaries; they are not independent aligned bytes.
Weekly-program parameters have only the shown samples; no general program
encoder is proven.

### General-zone reads

These are **standard**, command `04`, not extended telegrams.

| OWN | Meaning | Payload |
| --- | --- | --- |
| `*#4*#0*0##` | Temperature read | `99 00 30 00` |
| `*#4*#0*14##` | Setpoint read | `99 00 30 0E` |
| `*#4*#0*20##` | Actuator status | `B9 00 30 10` |

Do not generalize DIM→last-byte from these alone: DIM `20` uses `10`, not
decimal 20 encoded as `14`. The summary's labels `B99` / `B9B` are not bytes.

### Gaps and inbound restrictions

- No general-zone frames for tested WHAT `101/112/113/201/301/40`,
  `3000#3`, or `13003`; DIM reads `11/12/13/15/19`; writes
  `#14*0200`, `#14*0215`, `#13*3`, `#11*2`.
- Zone `1` and `2` variants emitted no frames in the retained matrix.
  Zone traffic is gated by the OEM thermo plant configuration; inserting
  address/database rows did not unlock it. Do not infer zone templates by
  replacing the general address.
- Mirroring outbound frames produced no OWN events. `bt_termo` accepts
  replies correlated to its polls within a transaction, not arbitrary
  unsolicited mode/read frames. Zone claim logic also remains gated.
- Temperature/setpoint reply value encoding and complete zone addressing
  remain open. Public OWN temperatures in tenths of a degree do not prove
  the bus representation.

## WHO 8 — Intercom / video entry

### Unlock

**RE**, independently corroborated by physical bus captures. Direct branch:
WHO `8`, WHAT `19/20`, WHERE beginning with `2`, OEM runtime state byte zero.
Standard payload, command `04`.

| OWN WHERE | Open: `*8*19*W##` | Close/release: `*8*20*W##` |
| --- | --- | --- |
| `20` | `96 A0 6F A4` | `96 A0 6F A0` |
| `21` | `96 A1 6F A4` | `96 A1 6F A0` |
| `22` | `96 A2 6F A4` | `96 A2 6F A0` |

Only these public addresses are concretely recovered. With parsed
`livello==2`, RE selects extended wrapper `E3 00 00 00` before the four-byte
unlock payload, command `06`. Other runtime/address branches construct
`B6…` from session state; their meaning remains open.

### Ring

| Direction | Template | Evidence / gate |
| --- | --- | --- |
| Bus ring | `91 aa 60 08` | RE + independent bus observation |
| Corresponding OWN identity | `*8*9#1#4*<address>#2##` | RE; `bt_vct` session-address / RING_ON gate must pass |

The address is parsed from the bus byte, but the complete public address
normalization is not recovered. Injected ring frames at `11/14/15/21` gave no
events on the unconfigured F461 route. `*8*1#1#4#…` is an OWN ring-start
family with no recovered native discriminator; ring-end mapping is open.

Do not substitute `D1…6333A000000` for unlock: the adjacent OEM path is
TeleLoop programming (internal WHO 6 context; WHAT 76 dispatcher path), not
the recovered WHAT 19/20 translation.

### Call traffic — observed, not command templates

An app-originated call following an address-20 unlock produced this sequence,
generally three copies of each payload (**Bus**):

```text
98 A0 6F A4
A0 A0 6F 01
98 A0 6F A0
A0 A0 6F 01
B1 A0 6F 88
B2 A0 6F 88
D1 01 0F 33 01 00 00 00
D2 01 0F 34 01 02 04 30
B3 01 60 88
B4 A0 6F 88
B5 01 60 88
```

Later MX300EOS calls used `B1/B2` with `A2/A1/A0` for panels `22/21/20`
and triplicated D1/D2 frames. An unanswered panel-22 call ended with
`B5 A2 6F 88`; panel 21 reached `B3 01 60 88`, `B4 A1 6F 88`,
`B5 01 60 88`. Two panel-20-style transactions included:

```text
98 A0 6F A7
A0 A0 6F 01
98 A0 6F A3
A0 A0 6F 01
```

These correlate `A0 A0 6F 01` with the `98` family but do not prove its
sender, ACK/state role, or the meaning of `A7/A3` versus `A4/A0`.
Do not emit that group, synthesize a call, or assign semantics to the extended
frames without source-attributed controlled captures. `B1` here is not the
lighting broadcast: discriminate the complete family context (`6F/60`, etc.).

## WHO 9 — Auxiliary

**Mined**, command `04`. Use exact payload lookups; address/action bit semantics
are unresolved. The actual byte boundary is `B0 00 9x xx`, **not**
`B0 00 09 <three-digit value>`.

| OWN WHAT | Notes' operation label | WHERE `0` payload | WHERE `11` payload |
| --- | --- | --- | --- |
| `1` | ON | `B0 00 9A 04` | `B0 00 9A B4` |
| `2` | OFF | `B0 00 9A 08` | `B0 00 9A B8` |
| `3` | UP | `B0 00 9B 00` | `B0 00 9B B0` |
| `4` | DOWN | `B0 00 9B 04` | — |
| `5` | Unresolved | `B0 00 9B 08` | — |
| `6` | Unresolved | `B0 00 9C 01` | — |
| `7` | Unresolved | `B0 00 9C 02` | — |
| `8` | Unresolved | `B0 00 9C 04` | — |

Input shape: `*9*WHAT*W##`. These operation labels follow the mined notes;
public libraries disagree (for example, OFF=`0` rather than `2`). The byte
translations are established; the semantic names need device-level validation.
Do not derive an addressed formula from only WHERE `11` or assume the
lighting address skeleton. `*#9*11##` produced no frame. Injected
`11 00 09 04` gave no OWN event; it is not a mirror of the captured AUX payload.
Correct-payload inbound decoding remains open.

## WHO 15 — CEN

**Mined both directions.** Buttons `b=0…31`, decimal in OWN. Single-digit
buttons **must be zero-padded** (`01`, not `1`). Standard command `03`.

| OWN | Event | Payload |
| --- | --- | --- |
| `*15*BB*W##` | Start pressure | `CD w 14 b` |
| `*15*BB#1*W##` | Short release | `CD w 10 a`, `a=(1<<5) OR b` |
| `*15*BB#2*W##` | Extended release | `CD w 10 a`, `a=(2<<5) OR b` |
| `*15*BB#3*W##` | Extended press | `CD w 10 a`, `a=(3<<5) OR b` |

For marker `10`, decode `s=ACT>>5`, `b=ACT AND 1F`; proven subtypes are
`1/2/3`. Marker `14` directly carries the button. The inbound mirror emits
the same OWN forms, including padding and verified advanced WHERE forms.

| OWN example | Payload |
| --- | --- |
| `*15*11*21##` | `CD 21 14 0B` |
| `*15*01#1*21##` | `CD 21 10 21` |
| `*15*01#2*21##` | `CD 21 10 41` |
| `*15*20#3*21##` | `CD 21 10 74` |
| `*15*00*21##` | `CD 21 14 00` |
| `*15*30*21##` | `CD 21 14 1E` |
| `*15*31*21##` | `CD 21 14 1F` |
| `*15*01*0215##` | `CD 2F 14 01` |
| `*15*03#1*1001##` | `CD A1 10 23` |

### Local-bus suffix / EC wrapper

`*15*BB[#s]*W#4#I4##` → extended payload
`EC i 00 00 CD w <14-or-10> ACT`, command `06`. I4 is decimal, binary in `i`.

| OWN | Payload |
| --- | --- |
| `*15*06*36#4#01##` | `EC 01 00 00 CD 36 14 06` |
| `*15*03*0112#4#12##` | `EC 0C 00 00 CD 1C 14 03` |
| `*15*20#2*33#4#02##` | `EC 02 00 00 CD 33 10 54` |

The EC inbound mirror preserves the suffix. Four-digit WHERE parses decimal
A/PL then packs nibbles: `0112` → `1C`, `0215` → `2F`, `1001` → `A1`.
F461 rejects zone-0 example `0001` and invalid `1115`; the verified zone-10
`1001` is a special valid form, not permission to accept arbitrary A≥10.
`*#15*21##` is rejected; no CEN status-request template is recovered.

The old “commissioning required / no CEN translation” result was caused by
invalid syntax. Correct padded/structured forms emit without a physically
commissioned object.

## WHO 18 — Energy / supervision

**Mined**, command `06`, eight-byte payloads; no general/broadcast translation.
WHERE `#0` emitted nothing; tested WHERE `2` was silent.

### Packed address and formatter

The recovered formatter is the concatenation of **hexadecimal substrings**:

```text
D + aaaa + 23 + oo + ttttttt = 16 hex characters = 8 bytes
```

`aaaa` is a four-character address field, `oo` the two-character operation,
and the tail has seven characters. These fields are **not byte-aligned**.
For example `D + 1000 + 23 + 21 + 0010401` becomes
`D1 00 02 32 10 01 04 01`. Keep all leading zeros before splitting into bytes.

| OWN WHERE | `aaaa` | Coverage |
| --- | --- | --- |
| `1` | `1000` | Retained full matrix |
| `3` | `1800` | Summary's disambiguation pass |
| `51` | `1A10` | Retained full matrix |
| `52` | `1A20` | Summary's disambiguation pass |
| `53` | `1A30` | Summary's disambiguation pass |

Partial rule: `5N` → string `1A` + two-digit hex of `N×0x10` for the shown
counter addresses. Plain-N addressing is not fully derived; retain the lookup.

### Commands and parameter samples

Use the formatter above with the table's exact operation/tail. Parameterized
rows establish the shown samples; generalized parameter widths/ranges require
more captures.

| OWN | Meaning | `oo` | `ttttttt` | Actual bytes for WHERE `1` |
| --- | --- | --- | --- | --- |
| `*18*26*W##` | Automatic reset ON | `21` | `0010401` | `D1 00 02 32 10 01 04 01` |
| `*18*27*W##` | Automatic reset OFF | `21` | `0010400` | `D1 00 02 32 10 01 04 00` |
| `*18*510#1*W##` | Current totalizer, parameter 1 | `35` | `01D0100` | `D1 00 02 33 50 1D 01 00` |
| `*18*57#1#15*W##` | Day, exact parameter sample | `35` | `01A010F` | `D1 00 02 33 50 1A 01 0F` |
| `*18*58#1*W##` | Month, exact parameter sample | `35` | `01B0100` | `D1 00 02 33 50 1B 01 00` |
| `*18*59#1*W##` | Year(?), exact parameter sample | `35` | `01C0100` | `D1 00 02 33 50 1C 01 00` |
| `*18*71*W##` | Defrost/heater(?) | `27` | `0010000` | `D1 00 02 32 70 01 00 00` |
| `*18*73#100*W##` | Notification time 100 | `27` | `0026400` | `D1 00 02 32 70 02 64 00` |
| `*18*74*W##` | Meaning unresolved | `27` | `0020000` | `D1 00 02 32 70 02 00 00` |
| `*18*75#2*W##` | Totalizer count 2 | `27` | `0040200` | `D1 00 02 32 70 04 02 00` |

Visible parameter echoes: day 15 → `0F`, time 100 → `64`, count 2 → `02`.
These confirm decimal-to-binary encoding at those sample positions, not a
complete definition of all normal-command parameters.

### Dimension reads

| OWN | Meaning | `oo` | `ttttttt` | Actual bytes for WHERE `1` |
| --- | --- | --- | --- | --- |
| `*#18*W*113##` | Active power | `30` | `01D0000` | `D1 00 02 33 00 1D 00 00` |
| `*#18*W*250##` … `*#18*W*254##` | Interface statuses; all five map identically | `31` | `0100000` | `D1 00 02 33 10 10 00 00` |
| `*#18*W*53##` | Month totalizer | `35` | `0170000` | `D1 00 02 33 50 17 00 00` |
| `*#18*W*54##` | Day totalizer | `35` | `0180000` | `D1 00 02 33 50 18 00 00` |

WHERE `51`, active power example: `D1 A1 02 33 00 1D 00 00`.
Do not treat public DIM numbers as raw bus bytes: `113` maps to operation
`30` / subfield `1D`, not byte `71`. The raw matrix corrects the summary's
operation label `33`: `$006D1000233001D0000` contains formatter operation
`30`, even though its aligned P3 byte is `33`.

### Bus → OWN: correlation and continuous flow

**RE + mined injection checks:** mirrored outbound frames produce no events.
`bt_supervisione` matches the signature/WHERE of its last request within an
approximately 2 s transaction timeout before emitting dimension replies.

The recovered measurement-delivery path is **Flusso Continuo**, not a fully
decoded single-frame value response:

| Stage | Recovered behavior |
| --- | --- |
| Start | Extended frame, textual sub-operation character `5`; stores from/to addresses and suppresses its own transmissions |
| Sequence | Counter field masked with `1F` (five bits; 32-frame window) |
| Fragments | Must arrive in strictly descending sequence order |
| Duplicate | Wait for next fragment |
| Wrong order | Abort the session |
| Accumulation | Append decoded decimal triples (`%d*%d*%d`) |
| Completion | Counter reaches zero; emit assembled measurement |
| Special routes | Address cases involving `50` and `70→4` select interface-status paths |

The exact native field offsets, value widths/scaling, and full session/address
layout remain open. “Sub-operation character `5`” must not be turned into an
assumed byte `05` without tracing the nibble/ASCII decoder. A standalone
ESP gateway need not copy the OEM poll gate, but does need the actual stream
assembler once those fields are recovered.

Other RE branches forward text-message sequences with OWN WHATs
`9012/9017/9001/9034` and interface status `*<W>*401#<v>*…##`; their complete
native templates are not recovered. Static literals include extended/D2
variants `$06D1A2F231%04X%04X`, `$06D1A2F233%02X%02X0000`,
`$06D2000233%02X%02X0000`, and partial `$06D1%02X%01X23%01X…`.
These are RE leads, not interchangeable replacements for the mined requests.

## WHO 25 — CEN+ / interfaces

### CEN+ objects and buttons

**Mined both directions**, command `04`. WHERE is prefix `2` followed by
decimal object `n=0…2047`; button `b=0…31` is decimal. Encode:

```text
P0 = B2
P1 = n AND FF
P2 = WHAT + 7B              (WHAT is decimal; 7B is hex)
P3 = ((n >> 3) AND E0) OR b
```

The upper three object bits occupy P3 bits 7…5; the button occupies bits 4…0.
Decode `n=P1 OR ((P3 AND E0)<<3)`, `b=P3 AND 1F`, `WHAT=P2-7B`.
Serialize WHERE as `2` followed by decimal `n`, not hexadecimal text.

| WHAT | Meaning | P2 |
| --- | --- | --- |
| `21` | Short pressure | `90` |
| `22` | Start extended pressure | `91` |
| `23` | Extended pressure | `92` |
| `24` | Release extended pressure | `93` |
| `25` | Rotary operation | `94` |
| `26` | Rotary operation | `95` |
| `27` | Rotary operation | `96` |
| `28` | Rotary operation | `97` |

Rotary `25…28` share the recovered encoder; the retained advanced capture
explicitly exercises `25`. Their exact directional/step semantics are not
assigned from the native code alone.

| OWN | Payload | Object / button |
| --- | --- | --- |
| `*25*21#3*21##` | `B2 01 90 03` | 1 / 3 |
| `*25*22#3*22##` | `B2 02 91 03` | 2 / 3 |
| `*25*21#20*21##` | `B2 01 90 14` | 1 / 20 |
| `*25*24#10*2101##` | `B2 65 93 0A` | 101 / 10 |
| `*25*21#3*22010##` | `B2 DA 90 E3` | 2010 / 3 |
| `*25*22#2*22047##` | `B2 FF 91 E2` | 2047 / 2 |
| `*25*25#3*21##` | `B2 01 94 03` | 1 / 3 |

Inbound mirrors were verified, including large-object addressing. Earlier
rotary rejections used dry-contact WHERE `31`, not CEN+ WHERE `21`; they do
not establish missing rotary support. `*#25*21##`, `*25*21#2*121##`, and
`*25*31#1*#31##` were rejected. A valid longer CEN+ object WHERE is not the
same as an arbitrary three-digit address.

### Dry-contact / interface

**Mined**, command `04`; concrete verified interface WHERE is `31`, index `01`.
The notes describe the index as the low WHERE digit; higher address encoding
needs a second interface sample.

| OWN | Payload |
| --- | --- |
| `*25*31#0*31##` | `B4 01 91 00` |
| `*25*31#1*31##` | `B4 01 91 01` |
| `*25*32#0*31##` | `B4 01 92 00` |
| `*25*32#1*31##` | `B4 01 92 01` |
| `*#25*31##` | `B4 00 90 01` |

Outbound rule for the tested interface: WHAT `31/32` → `91/92`, value `0/1`
in P3; status request places index in P3 instead of P1. Only the first,
fourth, and status rows have explicit retained emission vectors; the other
two follow the recovered builder.

**Inbound is not a simple inverse:**

| Payload | Observed OWN event |
| --- | --- |
| `B4 01 91 00` | `*25*31#0*31##` |
| `B4 01 91 01` | `*25*32#0*31##` |
| `B4 00 90 01` | `*#25*31##` |

P3 bit 0 interacts with the on/off operation. Retain this inbound lookup;
do not assume `91/92` alone selects WHAT while P3 directly selects its value.
Complete dry-contact state decode, IR interfaces, and the static `D2…938`
extended builder's trigger/address path remain open.

## Unmapped domains

Public OWN documentation or an OEM handler route does not supply a native
telegram template. These domains are not silently covered by the layouts above.

| WHO | Domain | Translation status |
| --- | --- | --- |
| `3` | Legacy load control | No recovered template |
| `5` | Alarm | No F461 route; no recovered native mapping |
| `6` | Legacy non-video door entry / internal programming contexts | No general public mapping; do not reuse the excluded TeleLoop literal as WHO 8 unlock |
| `7` | AV/media | MX local-media handler, absent on F461; no established native SCS path; distinct from WHO 8 |
| `13` | Gateway management | Gateway-local replies; no recovered bus mapping for management operations |
| `14` | OEM actuator lock extension | Routed to `bt_luci`; no public frames/native templates recovered |
| `16` / `22` | Legacy sound / sound diffusion | Public grammar exists; MX `bt_multi` absent on F461; no recovered native mapping |
| `17` | Scene programmer | No F461 route; no recovered native mapping |
| `24` | Lighting management via programmer gateway | Not mined; no native templates recovered |
| `29` | OEM COSO extension | Plugin route present; not mined |
| `1000+` as WHO | Diagnostics / device extensions | Stub-level/native mapping unrecovered; distinct from WHAT `1000#…` under WHO 1/2 |

## Extending the tables

For each new translation, record the following rather than adding an inferred
byte meaning to an existing family:

| Field | Required record |
| --- | --- |
| Scope | Gateway/product/firmware, WHO, direction, configuration gates |
| OWN input/output | Exact text including leading zeros, suffixes, parameter units/ranges |
| Native layout | Four or eight actual bytes, indexed from P0; label nibble fields explicitly |
| Address | Parser rule, valid ranges, general/group/interface behavior |
| Operation | Constants, decimal/hex conversion, masks/shifts, parameter widths |
| Sequence | Every payload in order; distinguish logical frames from repeated copies |
| Example | OWN ↔ payload ↔ full `A8…XOR A3` telegram |
| Evidence | Raw capture or binary function, independent reproduction, physical-device result where available |
| Unknowns | Unobserved values, ignored/rejected frames, correlation/session/commissioning gates |

Use drained, sufficiently long capture windows: delayed scene/movement frames
can otherwise appear under the next request. Verify byte count and XOR after
every formatter change. Test a second address and boundary parameter values
before promoting a lookup to a formula. For inbound work, distinguish command
echoes, gateway-addressed replies, broadcasts, and transaction/stream fragments.
Keep unresolved templates labelled; absence of an OWN event alone does not
disprove an SCS payload when the OEM consumer is gated.
