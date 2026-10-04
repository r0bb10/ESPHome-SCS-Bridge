# ESPHome SCS Bridge

An ESPHome external component for sending and receiving BTicino SCS telegrams
with an ESP32. It handles bus timing, framing, checksums, transmit queues and
collision retries. Received telegrams appear in the logs and in an automatically
created **Telegram** diagnostic text sensor.

This is a raw bus component, not an OpenWebNet server. Commands are defined as
payload bytes in YAML; there are no built-in light, cover or intercom entities.
The recovered OWN ↔ SCS mappings are in [re/README.md](re/README.md).

## Hardware

Use an ESP32 with the **ESP-IDF** framework. Arduino is not supported.

You need an SCS interface circuit between the bus and the GPIOs. **Do not connect
the SCS wires directly to the ESP32**: the bus carries roughly 27 V. The component
expects RX idle high, going low during a bus pulse, and TX high to drive a bus
pulse. Inverted GPIO configuration is not supported.

Another alternative is to use an interface like this from [@Mat931](https://github.com/Mat931/esp32-doorbell-bus-interface)
that directly interfaces with the scs bus, but it's intended to work on an ABB bus
so it has to be verified if the resistor values match or if it needs patching.

## Usage

Set `framework.type: esp-idf` in your existing `esp32:` configuration, then add
the following to your ESPHome YAML. Keep your normal board, Wi-Fi, API and OTA
settings. Change the pins to match your interface.

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/r0bb10/ESPHome-SCS-Bridge.git
      ref: main
    refresh: always
    components: [scs_bticino]

scs_bticino:
  id: scs
  rx_pin: GPIO3
  tx_pin: GPIO4

button:
  - platform: template
    id: devaddr_20
    name: entry_door
    on_press:
      - scs_bticino.send:
          id: scs
          type: short
          payload: [0x96, 0xA0, 0x6F, 0xA4]
      - delay: 250ms
      - scs_bticino.send:
          id: scs
          type: short
          payload: [0x96, 0xA0, 0x6F, 0xA0]
```

This example sends an unlock press and release for intercom address `20`.
The 250 ms delay is the interval between enqueueing the two commands, not a
guaranteed bus transmission time. Check the address and payloads for your plant
before using them.

Supply **only the payload**: the component adds `A8`, the XOR checksum and `A3`.

| Send type | Payload length | Behavior |
| --- | --- | --- |
| `short` | 4 bytes | Standard telegram, sent three times |
| `response` | 4 bytes | Waits for bus `A5`; up to eight attempts |
| `extended` | 8 bytes | Extended telegram, sent three times |
| `extended_alt` | 8 bytes | Alternate OEM type; currently the same transmit behavior as `extended` |

The type controls transmission behavior, not the payload's operation or address.
It does not select a `D1`/`D2` prefix for you. A queued command is not proof that
a device acted on it; use the logs and device feedback to check the result.

## Extending it

For another command, add a template button or automation using `scs_bticino.send`
and the appropriate payload from [the translation reference](re/README.md).
No C++ changes are needed for fixed payloads. New mappings should include the
exact telegram, address encoding and a tested example; keep unverified fields
marked as such.

For component changes:

- `components/scs_bticino/__init__.py` defines YAML options and actions.
- `scsbticino_codec.*` handles telegram construction and validation.
- `scsbticino_rx.*` and `scsbticino_tx.*` handle reception and transmission.
- `scsbticino.*` connects them to ESPHome and the ESP-IDF peripherals.
