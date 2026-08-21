#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "codex_input.hpp"
#include "codex_model.hpp"

/* Pure full-frame RGB565 renderer for the Codex control surface. */

namespace buddy::codex::ui {

inline constexpr int Width = 320;
inline constexpr int Height = 240;
inline constexpr std::size_t PixelCount =
    static_cast<std::size_t>(Width) * Height;

/* Returns the short display label for one semantic Agent status. */
[[nodiscard]] const char *slotStatusLabel(SlotStatus status) noexcept;

/* Returns the short display label for one Codex connection state. */
[[nodiscard]] const char *connectionLabel(Connection connection) noexcept;

/* Renders a complete Codex RGB565 frame; activeAction may be nullptr. */
void render(const Model &model, const Action *activeAction,
            std::uint32_t timeMs, std::span<std::uint16_t> pixels,
            bool muted = false) noexcept;

}  // namespace buddy::codex::ui
