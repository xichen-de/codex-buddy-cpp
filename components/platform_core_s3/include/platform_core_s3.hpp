#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "esp_err.h"

/* Shared board boundary. Mode-specific policy belongs above this component. */

namespace buddy::platform {

inline constexpr int Width = 320;
inline constexpr int Height = 240;
inline constexpr std::size_t PixelCount =
    static_cast<std::size_t>(Width) * Height;

enum class TouchType : std::uint8_t { None, Pressed, Moved, Released };

struct TouchEvent {
    TouchType type{TouchType::None};
    std::uint16_t x{};
    std::uint16_t y{};
};

enum class Sound : std::uint8_t { Connect, Attention, Approve, Deny, Complete };

enum class DisplayPower : std::uint8_t { Normal, Dimmed, Off };

struct MotionSample {
    float x_g{};
    float y_g{};
    float z_g{};
};

struct BatteryStatus {
    std::uint8_t percent{};
    bool charging{};
};

/* Initializes the CoreS3 LCD, backlight, touch controller, and framebuffer. */
[[nodiscard]] esp_err_t initialize() noexcept;

/* Returns the shared 320 x 240 RGB565 framebuffer allocated in PSRAM. */
[[nodiscard]] std::span<std::uint16_t> framebuffer() noexcept;

/* Transfers the entire RGB565 framebuffer and waits until DMA is finished. */
[[nodiscard]] esp_err_t present() noexcept;

/* Coordinates the LCD panel with normal, dimmed, and off backlight levels. */
[[nodiscard]] esp_err_t setDisplayPower(DisplayPower power) noexcept;

/* Polls the capacitive touchscreen and emits edge/move events. */
[[nodiscard]] esp_err_t pollTouch(TouchEvent &event) noexcept;

/* The current official BSP does not expose trustworthy battery telemetry. */
[[nodiscard]] std::optional<BatteryStatus> battery() noexcept;

/* Initializes speaker hardware; failure is nonfatal to the Claude runtime. */
[[nodiscard]] esp_err_t initializeSpeaker() noexcept;

/* Plays the fixed tone sequence associated with a semantic UI sound. */
[[nodiscard]] esp_err_t playSound(Sound sound) noexcept;

/* BM8563 stores local wall-clock time. These values use Unix-style seconds
   only as a compact calendar representation; no timezone is applied here. */
[[nodiscard]] esp_err_t initializeRtc() noexcept;

/* Reads BM8563 calendar fields and converts them to compact local seconds. */
[[nodiscard]] esp_err_t readRtc(std::int64_t &localSeconds) noexcept;

/* Converts compact local seconds and writes the BM8563 calendar fields. */
[[nodiscard]] esp_err_t writeRtc(std::int64_t localSeconds) noexcept;

/* Initializes the CoreS3 BMI270 accelerometer with the project configuration. */
[[nodiscard]] esp_err_t initializeImu() noexcept;

/* Reads one accelerometer sample and converts each axis to g units. */
[[nodiscard]] esp_err_t readImu(MotionSample &sample) noexcept;

}  // namespace buddy::platform
