#pragma once

#include <cstdint>
#include <span>

#include "claude_model.hpp"

/* Pure renderer and hit testing for the mode selector and Claude screens. */

namespace buddy::display {

inline constexpr int Width = 320;
inline constexpr int Height = 240;
inline constexpr std::size_t PixelCount =
    static_cast<std::size_t>(Width) * Height;

enum class Mode : std::uint8_t { None, Codex, Claude };

enum class ClaudeAction : std::uint8_t {
    None,
    PagePet,
    PageActivity,
    PageClock,
    PageInfo,
    ActivityUp,
    ActivityDown,
    Approve,
    Deny,
    ToggleMute,
    SwitchMode,
};

/* Draws the startup mode selector into a complete RGB565 framebuffer. */
void renderSelector(std::span<std::uint16_t> pixels) noexcept;

/* Maps selector coordinates to Codex, Claude, or NONE outside both buttons. */
[[nodiscard]] Mode selectorHit(std::uint16_t x, std::uint16_t y) noexcept;

/* Draws the current Claude page plus passkey or permission overlays. */
void renderClaude(const claude::Model &model, std::uint32_t nowMs,
                  bool passkeyVisible, std::uint32_t passkey,
                  bool batteryKnown, std::uint8_t batteryPercent,
                  bool charging, std::span<std::uint16_t> pixels,
                  bool muted = false) noexcept;

/* Maps a Claude-screen press to a page, scroll, decision, or mode action. */
[[nodiscard]] ClaudeAction claudeHit(const claude::Model &model,
                                     std::uint16_t x,
                                     std::uint16_t y) noexcept;

}  // namespace buddy::display
