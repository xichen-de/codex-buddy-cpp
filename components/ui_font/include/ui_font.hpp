#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace buddy::font {

/* Measures and rasterizes LVGL Montserrat text into an RGB565 framebuffer. */
[[nodiscard]] int textWidth(std::string_view value, int scale) noexcept;

void draw(std::span<std::uint16_t> pixels, int canvasWidth, int canvasHeight,
          std::string_view value, int x, int y, int scale,
          std::uint16_t color) noexcept;

}  // namespace buddy::font
