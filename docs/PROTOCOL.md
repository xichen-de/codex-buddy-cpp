# Codex Micro compatibility protocol notes

These notes pin the evidence used for Codex Buddy's unofficial compatibility
implementation. The protocol is undocumented by its vendors and can change
with any ChatGPT Desktop release.

## Reference and license

| Field | Value |
| --- | --- |
| Repository | `https://github.com/imliubo/codex-micro-4-core2.git` |
| Revision | `2ee23a4ab696f94bb78d250f28cc4a9b879ba079` |
| Commit date | 2026-07-16 23:12:12 +08:00 |
| License | MIT, copyright 2026 imliubo |
| Reference hardware validation | Core2 and macOS, reported by the reference author on 2026-07-16 |

The reference repository permits use, modification, and redistribution under
the MIT License. Its notice is preserved in
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md). Codex Buddy uses a new
ESP-IDF component structure and does not import the reference's Arduino,
M5Unified, or ArduinoJson layers.

For auditability, the inspected files had these SHA-256 hashes:

| File | SHA-256 |
| --- | --- |
| `LICENSE` | `280708cd0c3d297bf1a3032cdcd7740172282d636ce5d089a73792c799bb07bd` |
| `docs/TECHNICAL.md` | `5b5992267b5fccc698b7c7529542139289dc3b8cdd909728707599f6fcbcf584` |
| `src/CodexMicroBle.cpp` | `ffc43f197acdde3192f6b2cf2213429f347c855c0be33e237a505e0631181bb1` |

## Evidence labels

- **Reference-verified:** implemented and reported as tested on physical Core2
  hardware by the pinned reference repository.
- **Source-observed:** directly present in the pinned source, but not claimed as
  an independently captured wire value by Codex Buddy.
- **CoreS3-verified:** independently exercised on the physical CoreS3 with the
  tested macOS and ChatGPT Desktop versions.
- **Inferred:** a working interpretation not directly confirmed by source or
  the completed physical test.

CoreS3 validation completed on 2026-08-06 with macOS 26.5.2 and ChatGPT Desktop
26.730.61639. Compatibility remains version-specific because the protocol is
undocumented.

## BLE identity and discovery

The following values are source-observed and reported reference-verified:

| Field | Value |
| --- | --- |
| Device name | `Codex Micro` |
| Manufacturer | `Work Louder` |
| PnP source | `0x02` |
| Vendor ID | `0x303A` |
| Product ID | `0x8360` |
| Release | `0x0101` |
| Appearance | Generic HID |
| Advertised service | HID service created by the BLE HID implementation |
| Scan response | Enabled |
| Preferred connection interval | Minimum `0x06`, maximum `0x12` |
| Pairing | Bonding, no input/output capability (Just Works) |

The reference pre-swaps the 16-bit PnP values because its Arduino-ESP32 2.x HID
helper serializes them differently from the BLE characteristic. That swap is a
framework workaround, not a protocol byte-order rule. The ESP-IDF transport
must verify what macOS actually enumerates before applying any equivalent.

## HID report descriptor

The source-observed descriptor is one vendor-defined application collection on
usage page `0xFF00`, with Report ID 6 and one 63-byte input plus one 63-byte
output report:

```text
06 00 FF 09 01 A1 01 85 06 15 00 26 FF 00 75 08
95 3F 09 01 81 02 95 3F 09 02 91 02 C0
```

| Direction | HID item | Report body |
| --- | --- | --- |
| Device to host | Input, data/variable/absolute | 63 bytes |
| Host to device | Output, data/variable/absolute | 63 bytes |

Current macOS sends a 64-byte value, including Report ID 6, to the BLE output
characteristic. ESP-IDF normally sizes that characteristic directly from the
63-byte descriptor and rejects the write with ATT error 13. The BLE transport
therefore widens only the underlying GATT attribute by one byte while retaining
the reference descriptor. The decoder accepts both the normal 63-byte body and
the 64-byte raw-HID variant whose first byte is `06`.

## Report framing

Each 63-byte body has this source-observed layout:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 1 | Message type, currently `02` |
| 1 | 1 | UTF-8 fragment length, 0 through 61 |
| 2 | 0–61 | JSON fragment |
| Remainder | variable | Zero padding |

Device-to-host JSON is followed by a newline, split into chunks of at most 61
bytes, and emitted as fixed 63-byte notifications. The reference delays 4 ms
between fragments. Codex Buddy retains that delay, and multi-fragment exchanges
passed physical CoreS3 testing.

Example fixture for FAST press:

```json
{"method":"v.oai.hid","params":{"k":"ACT06","act":1}}
```

The JSON is 53 bytes. Its single report begins `02 36`, contains those 53 UTF-8
bytes followed by `0A`, and is zero-padded to 63 bytes.

The Codex Buddy decoder bounds accumulated JSON at 4096 bytes, validates report
type and declared fragment length, accepts the optional leading Report ID, and
resynchronizes when a new `{"method"...}` request replaces an incomplete one.

## Device-to-host events

### Key events

```json
{"method":"v.oai.hid","params":{"k":"ACT10","act":1}}
```

| Action | Value |
| --- | ---: |
| Release | 0 |
| Press | 1 |
| Encoder step | 2 |

| Control | Key ID |
| --- | --- |
| Agent 1–6 | `AG00`–`AG05` |
| FAST | `ACT06` |
| APPROVE | `ACT07` |
| DECLINE | `ACT08` |
| FORK | `ACT09` |
| MIC | `ACT10` |
| SEND | `ACT12` |
| Dial counter-clockwise | `ENC_CC` |
| Dial clockwise | `ENC_CW` |
| Dial press | `ENC` |

Agent events include `"ag":0` through `"ag":5`. The reference sends command
events without `ag`, so the host appears to retain the selected Agent after its
Agent key event. This selection behavior is inferred until verified against the
current ChatGPT Desktop release.

Agent, command, direction, and dial-press controls use press/release pairs.
Dial rotation uses action 2 once per step. Gesture duration and click sequences
are interpreted by the host in the reference; local double-tap behavior is not
yet proven necessary.

### Direction events

```json
{"method":"v.oai.rad","params":{"a":0.75,"d":1.0}}
```

| Direction | Normalized angle | Press distance | Release distance |
| --- | ---: | ---: | ---: |
| Right | 0.00 | 1.0 | 0.0 |
| Down | 0.25 | 1.0 | 0.0 |
| Left | 0.50 | 1.0 | 0.0 |
| Up | 0.75 | 1.0 | 0.0 |

## Host-to-device RPC

Requests use a JSON-RPC-like object containing `method`, usually `params`, and
an `id`. Responses echo `id` with `result` or `error`.

| Method | Source-observed response or effect |
| --- | --- |
| `sys.version` | Return firmware version |
| `device.status` | Return version, profile/layer, battery, and charging state |
| `v.oai.thstatus` | Update one or more of six Agent light states; reply success |
| `v.oai.rgbcfg` | Store ambient/key lighting configuration; reply success |
| `lights.preview` | Reply success; reference has no equivalent lighting preview |
| `host.focused_app` | Reply success; no local behavior |
| Unknown method | Error `-32601`, `Method not found` |

Each `v.oai.thstatus` item can contain:

| Key | Meaning |
| --- | --- |
| `id` | Agent index 0 through 5; other values must be ignored |
| `c` | 24-bit RGB color |
| `b` | Brightness multiplier |
| `e` | Effect string such as `off` or `breath` |
| `s` | Effect speed |

The semantic mapping from received colors to idle, thinking, complete,
requires-input, and error is CoreS3-verified for the tested ChatGPT Desktop
version. Raw color, brightness, effect, and speed remain retained when a future
host sends an unknown combination.

## CoreS3 validation results

### Implemented ESP-IDF transport choice

The firmware uses ESP-IDF's built-in `esp_hid` device API over Bluedroid. This
path exposes the vendor-defined input/output report map, PnP identity, bonding,
battery characteristic, and output-report callback needed by this protocol.
The build defaults explicitly enable Bluedroid BLE and leave NimBLE disabled,
so the selected implementation and configuration cannot silently diverge.

Physical testing confirmed that Bluedroid/`esp_hid` reproduces the identity,
security, report exchange, and reconnect behavior expected by the tested host.
macOS enumerates VID `303A`, PID `8360`, usage page `FF00`, and Report ID 6.
Agent selection, commands, navigation, dial actions, status transitions, and
`battery: null` all work without a custom bridge.

Two ESP-IDF-specific compatibility measures were required:

1. The writable HID GATT report attribute accepts 64 bytes because macOS
   forwards the leading Report ID, while the enumerated descriptor remains the
   reference-compatible 63-byte body.
2. `CONFIG_ESP_MAIN_TASK_STACK_SIZE` is 8192 so the fixed-token JSON parser can
   process the full connection-time lighting configuration without resetting
   the BLE link.

Descriptor changes require forgetting and re-pairing the device because macOS
may cache HID metadata.
