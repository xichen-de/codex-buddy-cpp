#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "esp_err.h"

/* Codex BLE HID boundary. Callbacks hand reports to the owning runtime loop. */

namespace buddy::codex::transport {

class Listener {
public:
    virtual ~Listener() = default;
    virtual void onConnectionChanged(bool connected) noexcept = 0;
    virtual void onReport(std::span<const std::uint8_t> report) noexcept = 0;
};

/* Starts the Codex HID device and advertising with the supplied callbacks. */
[[nodiscard]] esp_err_t initialize(Listener &listener) noexcept;

/* Returns whether a BLE HID host currently has an active connection. */
[[nodiscard]] bool connected() noexcept;

/* Frames and sends one complete Codex JSON message as HID input reports. */
[[nodiscard]] esp_err_t sendJson(std::string_view json) noexcept;

/* Updates the standard HID battery characteristic with a percentage value. */
[[nodiscard]] esp_err_t setBattery(std::uint8_t percent) noexcept;

}  // namespace buddy::codex::transport
