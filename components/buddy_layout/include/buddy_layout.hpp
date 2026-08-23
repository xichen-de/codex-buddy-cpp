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

namespace claude_approval {
inline constexpr Rect Approve{12, 143, 143, 55};
inline constexpr Rect Deny{165, 143, 143, 55};
}  // namespace claude_approval

/* Shared geometry for the Codex 3x2 command/agent button grid: both the
   Control and Agents pages, and their hit-testers, index into this grid. */
namespace codex_grid {
inline constexpr std::uint16_t OriginX = 5;
inline constexpr std::uint16_t OriginY = 38;
inline constexpr std::uint16_t CellWidth = 100;
inline constexpr std::uint16_t CellHeight = 76;
inline constexpr std::uint16_t ColumnStride = 105;
inline constexpr std::uint16_t RowStride = 84;
inline constexpr unsigned Columns = 3;
inline constexpr unsigned Rows = 2;

[[nodiscard]] constexpr Rect cell(unsigned index) noexcept
{
    const unsigned column = index % Columns;
    const unsigned row = index / Columns;
    return Rect{
        static_cast<std::uint16_t>(OriginX + column * ColumnStride),
        static_cast<std::uint16_t>(OriginY + row * RowStride), CellWidth,
        CellHeight};
}
}  // namespace codex_grid

}  // namespace buddy::layout
