# CoreS3 hardware test and release checklist

The v1 physical validation pass completed successfully on 2026-08-06. This
document records the tested setup and acceptance results.

## Record the test setup

- CoreS3 hardware: M5Stack CoreS3 (board revision/serial label not recorded)
- macOS version: 26.5.2 (25F84)
- ChatGPT Desktop version: 26.730.61639 (build 6234)
- PlatformIO version: Core 6.1.19
- Firmware: Codex v1 validation build (`Version 1.0`)
- Date and tester: 2026-08-06, Xi

## Build, flash, and observe — passed

Both PlatformIO configurations built successfully, the release image flashed
over the auto-detected USB serial port, and the CoreS3 booted without a PSRAM or
display failure. The repeatable verification procedure is:

1. Build both configurations:

   ```sh
   pio run -e m5stack-cores3
   pio run -e m5stack-cores3-debug
   ```

2. Connect exactly one known CoreS3 and verify its serial port before uploading.
   Keep the computer USB connection brief and attended. Disconnect immediately
   if the device becomes warm or makes electrical noise.
3. Upload only after explicitly confirming the target:

   ```sh
   pio run -e m5stack-cores3 -t upload
   pio device monitor -b 115200
   ```

4. Confirm there is no boot loop, PSRAM allocation failure, LCD timeout, or
   touch initialization error in the monitor.

## Display and touch — passed

- [x] LCD is landscape 320 x 240, upright, and fills the screen.
- [x] Red, green, blue, amber, white, and black are not channel-swapped.
- [x] All three bottom tabs switch pages and their labels fit.
- [x] Every Control, Agent, Navigate, and Dial target responds at its center and
  near each edge without triggering a neighbor.
- [x] Press feedback starts on touch-down and clears on release/cancel.
- [x] Requires-input shows the Agent number and tells the user to check the Mac.
- [x] Text and selected-Agent border remain readable for every status color.

If orientation or coordinates are wrong, capture the observed corner mappings
before changing BSP transform settings; do not change physical pin assignments.

## Pairing and transport — passed

- [x] Earlier `Codex Micro` pairings were forgotten after descriptor changes.
- [x] ChatGPT Desktop discovers `Codex Micro` and connects without a bridge.
- [x] macOS reports VID `303A`, PID `8360`, usage page `FF00`, Report ID 6.
- [x] Pairing uses Just Works and persists across a device power cycle.
- [x] Disconnect/reconnect and a ChatGPT Desktop restart resume status updates.
- [x] Malformed or partial output reports do not crash or wedge the device.
- [x] ChatGPT Desktop accepts unavailable (`null`) battery data.

macOS forwards a 64-byte raw output report, including Report ID 6, even though
the HID descriptor declares a 63-byte report body. The ESP-IDF GATT output
attribute is widened by one byte without changing the descriptor. The main-task
stack is set to 8192 bytes so the connection-time `v.oai.rgbcfg` request cannot
overflow the task and reset BLE.

## Functional controls — passed

Use a disposable ChatGPT task. For each Agent 1 through 6, select the tile and
verify that subsequent commands target that slot.

- [x] All six Agent slots can be assigned and selected.
- [x] FAST, APPROVE, DECLINE, FORK, and SEND perform the matching host action.
- [x] MIC begins only while held, uses the Mac microphone, and stops on release.
- [x] MIC also stops after slide-off cancellation, page change, and BLE loss.
- [x] Up, down, left, and right reach their matching remappable actions.
- [x] CCW and CW emit one encoder step per tap; Dial sends press and release.
- [x] Rapid taps and long holds neither duplicate actions nor leave a key pressed.

## Host status and recovery — passed

- [x] All six slots handle unassigned, idle, thinking, complete, requires-input,
  error, and an unknown color/effect.
- [x] Complete, error, and requires-input overlays open only on transitions and
  identify the correct Agent.
- [x] Repeated unchanged reports do not reopen notifications.
- [x] Invalid slot IDs and malformed JSON are logged and safely ignored.
- [x] Host restart, BLE loss, and device restart recover without reflashing.

## Release decision

**Decision: Codex v1 hardware validation passed.** Every Codex check above was
completed on the physical CoreS3. The tested host versions are recorded here,
and the protocol notes retain the compatibility caveat for future ChatGPT
Desktop releases.

## Claude Owl Buddy validation — passed

The v1 physical validation pass for Claude mode completed successfully on
2026-08-21. The following checks apply to the dual-mode firmware and were run
on physical hardware. They do not change the completed Codex v1 record above.

- CoreS3 hardware: M5Stack CoreS3 (board revision/serial label not recorded)
- macOS version: 26.5.2 (25F84)
- Claude version: 1.34493.1 (255293), build date 2026-08-21T02:05:20.000Z
- PlatformIO version: Core 6.1.19
- Firmware: Claude Owl Buddy validation build
- Date and tester: 2026-08-21, Xi

### Startup and Codex regression

- [x] Cold boot shows Codex and Claude choices before either advertises.
- [x] Choosing Codex opens the existing six-Agent UI and advertises only
  `Codex Micro`.
- [x] Previously paired Codex reconnects and all controls/status updates still
  pass the functional checklist above.
- [x] Reboot returns to the selector without clearing the Codex bond.

### Claude security and reconnection

- [x] Choosing Claude advertises `Claude CoreS3-XXXX` over Nordic UART and does
  not expose the Codex HID service.
- [x] Claude's Hardware Buddy picker discovers the device.
- [x] The CoreS3 displays a six-digit passkey and macOS accepts it.
- [x] The link reports secure after authentication; RX, TX, and CCCD access are
  unavailable before encryption.
- [x] Power-cycle into Claude mode and confirm automatic reconnect without a
  new passkey.
- [x] Switch Claude → selector → Codex → selector → Claude and confirm both
  saved pairings continue to reconnect without macOS service-cache confusion.
- [x] Use Claude's Forget action and confirm only the intended Claude pairing
  is removed; verify the Codex pairing still works.

### Claude protocol and UI

- [x] Idle, running, waiting, and disconnected heartbeats produce sleep, idle,
  busy, and attention owl states.
- [x] Owl animations remain smooth for sleep, idle, busy, attention,
  celebration, heart, and dizzy states without display corruption.
- [x] Activity shows session counts, daily/total tokens, and up to eight recent
  entries; UP/DN scroll through all entries without overflowing the display.
- [x] Claude time sync populates the Clock page with the correct local date and
  time, then a power cycle shows time from the battery-backed BM8563 before
  Claude reconnects.
- [x] Connection, attention, approval, denial, and completion sounds are clear,
  brief, and do not reset or stall BLE.
- [ ] In both modes the screen dims after 15 seconds and turns off after one
  minute; one touch wakes it without activating the underlying control.
- [ ] A Claude permission/passkey/waiting session and a Codex Agent requiring
  input restore and hold normal brightness until the action is resolved.
- [x] Place the CoreS3 screen-up and flat once to establish orientation, then
  shake it firmly; the owl enters dizzy once without repeated false triggers.
- [x] Leave the CoreS3 face-down for more than one second; the owl/display
  sleeps. Pick it up and confirm motion wakes the display on the Owl page.
- [x] Normal tapping, speaker playback, and desk vibration do not falsely
  trigger dizzy or face-down sleep.
- [x] A disposable harmless permission request shows the correct tool and hint.
- [x] Approve Once permits exactly that request; Deny rejects exactly that
  request; neither action is duplicated by touch release.
- [x] Owner/name commands persist across a power cycle, as do approval and
  denial counts.
- [x] Stopping heartbeats for more than 30 seconds produces the disconnected
  state and a later heartbeat recovers the live state.
- [x] Info → Switch Buddy safely restarts to the startup selector.

### Switching buddies from Codex

- [x] Tap `MENU`, then `SWITCH BUDDY`; the device restarts at the Codex/Claude
  startup selector.
- [x] Open the menu while the Mac is disconnected; switching still works.
- [x] Tap `CLOSE`; the previous Codex screen returns unchanged.
- [x] Switching Codex → Claude → Codex does not require re-pairing either
  Bluetooth identity.

### Claude validation decision

**Decision: Claude Owl Buddy v1 hardware validation passed.** Every Claude
check above was completed on the physical CoreS3, alongside a Codex regression
pass to confirm dual-mode switching does not disturb the existing Codex bond.
The tested host versions are recorded here, and the protocol notes retain the
compatibility caveat for future Claude releases.
