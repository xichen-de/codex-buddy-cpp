#pragma once

#include <cstdint>

/* Pure filter from accelerometer samples to stable, debounced motion events. */

namespace buddy::motion {

struct Events {
    bool shake{};
    bool faceDown{};
    bool faceUp{};
    bool moved{};
};

struct Detector {
    bool initialized{};
    bool orientationCalibrated{};
    bool isFaceDown{};
    std::int8_t faceUpSign{};
    float previousX{};
    float previousY{};
    float previousZ{};
    std::uint32_t faceDownSinceMs{};
    std::uint32_t lastShakeMs{};
};

/* Clears calibration, orientation, previous-sample, and debounce state. */
void init(Detector &detector) noexcept;

/* Filters one g-force sample and returns edge-triggered gesture events. */
[[nodiscard]] Events update(Detector &detector, float xG, float yG, float zG,
                            std::uint32_t nowMs) noexcept;

}  // namespace buddy::motion
