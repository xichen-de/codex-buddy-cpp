#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "esp_err.h"

/* Encrypted Claude BLE UART boundary, including pairing and bond lifecycle. */

namespace buddy::claude::transport {

class Listener {
public:
    virtual ~Listener() = default;
    virtual void onConnectionChanged(bool connected) noexcept = 0;
    virtual void onData(std::span<const std::uint8_t> data) noexcept = 0;
    virtual void onPasskeyChanged(bool visible,
                                  std::uint32_t passkey) noexcept = 0;
    virtual void onSecurityChanged(bool secure) noexcept = 0;
};

/* Starts the secured Claude UART service and begins advertising. */
[[nodiscard]] esp_err_t initialize(Listener &listener) noexcept;

/* Returns whether a central currently has a GATT connection. */
[[nodiscard]] bool connected() noexcept;

/* Returns whether the active connection completed encryption/authentication. */
[[nodiscard]] bool secure() noexcept;

/* Sends one JSON line to the connected Claude client as a notification. */
[[nodiscard]] esp_err_t sendLine(std::string_view json) noexcept;

/* Removes the active peer's bond and disconnects it so pairing can restart. */
[[nodiscard]] esp_err_t forgetCurrentPeer() noexcept;

}  // namespace buddy::claude::transport
