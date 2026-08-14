#pragma once

#include "claude_model.hpp"
#include "esp_err.h"

/* NVS persistence boundary for the durable subset of buddy::claude::Model. */

namespace buddy::claude::storage {

/* Loads durable Claude identity/statistics from NVS; missing keys keep defaults. */
[[nodiscard]] esp_err_t load(Model &model) noexcept;

/* Writes the durable subset of Claude model state to NVS atomically. */
[[nodiscard]] esp_err_t save(const Model &model) noexcept;

}  // namespace buddy::claude::storage
