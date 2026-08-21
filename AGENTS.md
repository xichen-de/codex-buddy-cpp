# Contributor guide

## Project scope

- This is a PlatformIO project for the M5Stack CoreS3 (ESP32-S3).
- Use PlatformIO for project configuration, dependencies, building, uploading, and monitoring.
- Use ESP-IDF as the application framework with its standard CMake component structure.
- Do not add the Arduino framework without maintainer approval.

## Working together

- Preserve existing changes and keep edits focused on the requested task.
- Explain meaningful design choices in plain language so maintainers can review
  and modify the code.
- Prefer small, readable changes over broad rewrites or generated abstractions.
- Ask before changing hardware pin assignments, partition layouts, storage formats,
  or non-UI dependencies. Focused UI libraries are allowed when they materially
  improve the on-device experience; document why they are needed.
- Never commit secrets, Wi-Fi credentials, private keys, or device-specific provisioning data.

## Code conventions

- Keep application code under `main/` and reusable features in focused ESP-IDF components.
- Compile project-owned code as C++20. Prefer scoped enums, value types,
  standard containers/views, classes, RAII, and explicit ownership over C-style
  emulation.
- Keep ESP-IDF C APIs behind C++ component boundaries. Do not add new
  project-owned `.c` or `.h` files.
- Use ESP-IDF APIs, error types, and logging (`ESP_LOGx`) consistently.
- Check recoverable errors and make hardware assumptions explicit near the relevant code.
- Use clear names and comments for hardware behavior; avoid comments that merely restate code.

## Verification

- Build with `pio run` after code or configuration changes when PlatformIO is available.
- Use `pio run -t menuconfig` for ESP-IDF configuration and keep intentional defaults reproducible.
- Report warnings or skipped checks; do not claim a build passed unless it was run successfully.
- Do not flash, erase, or change a connected device without its owner's explicit
  approval.
- Do not hand-edit or commit generated output under `.pio/` or `build/`.
