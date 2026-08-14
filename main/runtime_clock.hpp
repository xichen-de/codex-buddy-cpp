#pragma once

#include <cstdint>

#include "esp_timer.h"

namespace buddy::runtime {

[[nodiscard]] inline std::uint32_t uptimeMs() noexcept
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL);
}

}  // namespace buddy::runtime
