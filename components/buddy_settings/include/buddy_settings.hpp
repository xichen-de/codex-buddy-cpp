#pragma once

#include "esp_err.h"

/* Preferences shared by both buddy modes and persisted in NVS. */

namespace buddy::settings {

/* Loads the mute preference, leaving the supplied default for a missing key. */
[[nodiscard]] esp_err_t loadMuted(bool &muted) noexcept;

/* Atomically stores the mute preference. */
[[nodiscard]] esp_err_t saveMuted(bool muted) noexcept;

}  // namespace buddy::settings
