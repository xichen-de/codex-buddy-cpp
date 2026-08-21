#pragma once

#include <cstdint>

namespace buddy::layout {

struct Rect {
    std::uint16_t x{};
    std::uint16_t y{};
    std::uint16_t width{};
    std::uint16_t height{};
};

[[nodiscard]] constexpr bool contains(Rect rect, std::uint16_t x,
                                      std::uint16_t y) noexcept
{
    return x >= rect.x && y >= rect.y &&
           x < static_cast<unsigned>(rect.x) + rect.width &&
           y < static_cast<unsigned>(rect.y) + rect.height;
}

namespace codex_menu {
inline constexpr Rect Panel{20, 38, 280, 166};
inline constexpr Rect Mute{40, 76, 240, 40};
inline constexpr Rect SwitchMode{40, 125, 240, 40};
inline constexpr Rect Close{100, 174, 120, 22};
}  // namespace codex_menu

namespace claude_info {
inline constexpr Rect Mute{40, 112, 240, 38};
inline constexpr Rect SwitchMode{40, 156, 240, 40};
}  // namespace claude_info

}  // namespace buddy::layout
