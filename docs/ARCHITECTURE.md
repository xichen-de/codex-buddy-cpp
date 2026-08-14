# Codex Buddy architecture guide

This guide is a map for reading and changing the firmware. It describes the
current implementation rather than the historical sequence in which it was
built. For protocol details, use [PROTOCOL.md](PROTOCOL.md); for physical test
procedures, use [HARDWARE_TEST.md](HARDWARE_TEST.md).

## Start with the shape of the program

The firmware is one ESP-IDF application with two mutually exclusive runtimes.
`app_main()` initializes shared storage and CoreS3 hardware, shows the mode
selector, then enters either `CodexRuntime::run()` or `ClaudeRuntime::run()`. Each
runtime owns an infinite event loop, so control does not return to `app_main()`.
Switching modes restarts the device and shows the selector again.

```text
app_main
  |
  +-- initialize NVS and shared CoreS3 hardware
  +-- chooseMode (touchscreen selector)
        |
        +-- CodexRuntime::run  -> BLE HID + Codex control surface
        |
        +-- ClaudeRuntime::run -> encrypted BLE UART + owl companion
```

[`main/main.cpp`](../main/main.cpp) is the small composition root. Runtime state
belongs to `CodexRuntime` and `ClaudeRuntime` in their focused source files.
Components do not call one
another arbitrarily; their dependencies are declared in their
`CMakeLists.txt` files and exposed through headers under `include/`.

### Mode isolation and pairing

Only one Bluetooth stack is initialized at a time. Codex advertises as
`Codex Micro` using vendor-defined BLE HID. Claude advertises as
`Claude CoreS3-XXXX` using the Nordic UART Service and a stable random-static
address derived from the device Bluetooth MAC.

The modes also keep separate bonding data: Claude uses a dedicated Bluedroid
configuration store named `claude_bt`, while Codex uses the default store. This
is intended to let both desktop pairings survive mode switches without exposing
both services at once. The design is implemented, but its macOS pairing and
service-cache behavior remains a physical validation item in
[HARDWARE_TEST.md](HARDWARE_TEST.md).

Claude requires LE Secure Connections with MITM protection. The CoreS3 shows a
six-digit passkey, and its UART characteristics require an encrypted link.
Custom character-folder transfer is deliberately not implemented because it
needs a reviewed storage choice and may require a partition-layout change.

## Recommended reading order

Do not begin by reading all 700+ lines of `main/main.cpp` from top to bottom.
Choose one mode and follow its data path:

### Codex path

1. `components/codex_model/include/codex_model.hpp` — the state the screen can show.
2. `components/codex_input/include/codex_input.hpp` and `codex_input.cpp` — touchscreen
   coordinates become semantic actions.
3. `components/codex_controller/codex_controller.cpp` — actions update local state
   or become Codex key/direction messages.
4. `components/codex_protocol/codex_protocol.cpp` — JSON messages are framed in
   HID reports and incoming reports are reassembled.
5. `components/codex_rpc/codex_rpc.cpp` — desktop requests become model events
   plus a response.
6. `main/codex_runtime.cpp` — the pieces are
   scheduled and redrawn.
7. `components/codex_ui/codex_ui.cpp` — model state becomes a complete RGB565 frame.
8. `components/codex_ble_transport/codex_ble_transport.cpp` — the ESP-IDF BLE HID boundary.

### Claude path

1. `components/claude_model/include/claude_model.hpp` — owl, session, clock, and
   permission state.
2. `components/claude_protocol/claude_protocol.cpp` — newline-delimited JSON
   updates the model and requests side effects from the runtime.
3. `main/claude_runtime.cpp` — requested
   persistence, RTC writes, sounds, and redraws are performed here.
4. `components/buddy_ui/buddy_ui.cpp` — selector and Claude pages are rendered;
   touch coordinates become `buddy::display::ClaudeAction` values.
5. `components/claude_ble_transport/claude_ble_transport.cpp` — encrypted Nordic
   UART service, pairing, and bond handling.
6. `components/motion_detector/motion_detector.cpp` — raw acceleration becomes
   shake, face-down, face-up, and movement events.
7. `components/claude_storage/claude_storage.cpp` — the small NVS persistence
   boundary.

Read `components/platform_core_s3/platform_core_s3.cpp` last in either path. It
contains board-specific mechanics; the application logic above it is easier to
understand first.

## Runtime data flows

### Codex: touch to desktop

```text
CoreS3 touch
  -> buddy::platform::pollTouch
  -> buddy::codex::press/drag/release
  -> buddy::codex::handleAction    (action -> typed event and/or JSON)
  -> buddy::codex::encodeKeyEvent / encodeDirectionEvent
  -> buddy::codex::transport::sendJson
  -> ChatGPT Desktop
```

Press, release, and cancel are kept distinct. That matters for held controls
such as MIC and directional input: leaving a button or losing the connection
must emit a safe release instead of leaving the desktop action pressed.

### Codex: desktop to display

```text
BLE callback
  -> runtime queue               (copy bytes; do no application work here)
  -> buddy::codex::Decoder::push  (HID fragments -> complete JSON)
  -> buddy::codex::handleRequest  (JSON -> typed Event[] + response)
  -> buddy::codex::applyEvent
  -> buddy::codex::ui::render
  -> buddy::platform::present
```

BLE callbacks can run outside the main application loop. They therefore copy
events into a FreeRTOS queue. The loop is the sole owner that mutates the model,
decodes messages, and renders, which avoids cross-task state races.

### Claude: desktop to owl

```text
BLE UART callback
  -> Claude runtime queue
  -> buddy::claude::Decoder::push (arbitrary chunks -> JSON lines)
  -> buddy::claude::handleLine    (update model + describe side effects)
  -> ClaudeRuntime                (respond, persist, set RTC, play sound)
  -> buddy::display::renderClaude
```

`buddy::claude::Action` is an important boundary. Protocol code can request
a response, persistence, a clock update, or bond removal, but it does not
directly access BLE, NVS, the RTC, or the speaker. `ClaudeRuntime` performs those
hardware effects after parsing succeeds.

### Claude: local interaction

Touch is interpreted by `buddy::display::claudeHit()`. Permission decisions are
encoded and sent directly by the runtime because they combine UI state, BLE,
sound, and persistence. Motion samples take a separate path through
`buddy::motion::update()` before changing the Claude model or display power.

## Component responsibilities

| Component | Owns | Deliberately does not own |
| --- | --- | --- |
| `codex_model` | Codex UI state and state transitions | Touch coordinates, BLE, drawing |
| `codex_input` | Codex hit testing and gesture lifetime | Sending messages, rendering |
| `codex_controller` | Codex action policy and message intent | BLE implementation |
| `codex_protocol` | HID descriptor, framing, JSON reassembly | RPC meaning, UI state |
| `codex_rpc` | Supported desktop methods and model events | BLE and drawing |
| `ui` | Codex pixel rendering | Hardware transfer and touch |
| `codex_ble_transport` | Codex BLE HID lifecycle and reports | Application state |
| `claude_model` | Claude UI/session/permission state | Parsing, I/O, drawing |
| `claude_protocol` | Claude JSON framing and commands | BLE, NVS, RTC, sound |
| `buddy_ui` | Selector/Claude rendering and hit testing | Hardware transfer |
| `claude_ble_transport` | Claude BLE service, security, bonding | Protocol meaning |
| `claude_storage` | Persisted Claude counters/settings | Runtime policy |
| `motion_detector` | Motion filtering and gesture events | IMU hardware access |
| `platform_core_s3` | LCD, touch, speaker, RTC, and IMU APIs | Mode-specific behavior |
| `main` | Composition, owned runtime classes, queues, side effects | Reusable domain logic |

## State and ownership rules

- Codex state is held by `CodexRuntime`; only the
  Codex loop mutates it after initialization.
- Claude state is held by `ClaudeRuntime`; only its loop mutates it after
  initialization.
- BLE callbacks copy data into fixed-size queue events. The copied buffers must
  remain large enough for the transport callback's maximum chunk/report.
- Renderers always draw a complete 320 x 240 RGB565 frame into the shared PSRAM
  framebuffer. `buddy::platform::present()` transfers it to the LCD.
- Most pure components have no ESP-IDF dependency. This is intentional: they
  compile and run as strict C++20 host tests.

## How to trace or change a feature

Use the public type names as landmarks. For example:

- A Codex button starts as `buddy::codex::Action`, passes through
  `buddy::codex::handleAction()`, and becomes a scoped `buddy::codex::Key` JSON
  event.
- An Agent status starts in `buddy::codex::handleRequest()`, becomes a
  `SlotStatusChanged` variant, changes `buddy::codex::Model`, and is drawn by
  `buddy::codex::ui::render()`.
- A Claude permission prompt is parsed by `buddy::claude::handleLine()`, stored
  in `buddy::claude::Model`, drawn by `buddy::display::renderClaude()`, and
  answered by `ClaudeRuntime::processTouch()`.
- Face-down sleep starts with `buddy::platform::readImu()`, is filtered by
  `buddy::motion::update()`, and is applied by `ClaudeRuntime`.

When changing a feature, first update the lowest pure component that owns the
rule, add or adjust its host test, then connect hardware effects in `main/` or
the appropriate transport/platform component. This keeps policy testable and
keeps callbacks small.

## Tests as executable documentation

The files in `test/host/` show expected behavior without needing a CoreS3:

- `test_codex_model.cpp`, `test_codex_input.cpp`, and `test_codex_controller.cpp` explain
  the Codex state/input/action split.
- `test_codex_protocol.cpp` and `test_codex_rpc.cpp` contain concrete wire examples.
- `test_claude_protocol.cpp` and `test_claude_model.cpp` show supported Claude
  messages and owl state transitions.
- `test_codex_ui.cpp` and `test_buddy_ui.cpp` verify renderer behavior at selected pixels.
- `test_motion_detector.cpp` documents gesture thresholds through examples.

Run all host tests with `bash test/host/run.sh`. Build the actual ESP-IDF
application with `pio run -e m5stack-cores3`; neither command flashes hardware.
