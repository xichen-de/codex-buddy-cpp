#pragma once

#include <cstdint>

#include "codex_model.hpp"
#include "codex_protocol.hpp"

/* Converts Codex-screen coordinates and gesture phases into semantic actions. */

namespace buddy::codex {

inline constexpr int ScreenWidth = 320;
inline constexpr int ScreenHeight = 240;

enum class ActionType : std::uint8_t {
    None,
    Page,
    Slot,
    Key,
    Direction,
    DismissOverlay,
    OpenMenu,
    CloseMenu,
    SwitchMode,
};

struct Action {
    ActionType type{ActionType::None};
    Page page{Page::Control};
    std::uint8_t slot{};
    Key key{Key::Fast};
    Direction direction{Direction::Right};
};

struct Input {
    bool active{};
    Action action{};
};

/* Clears any gesture currently tracked by the input state. */
void init(Input &input) noexcept;

/* Hit-tests a press and begins its gesture; blocked commands return None. */
[[nodiscard]] Action press(Input &input, const Model &model, std::uint16_t x,
                           std::uint16_t y) noexcept;

/* Ends a held gesture and returns the action that needs a release event. */
[[nodiscard]] Action release(Input &input) noexcept;

/* Cancels a gesture when the pointer leaves its target; otherwise returns None. */
[[nodiscard]] Action drag(Input &input, const Model &model, std::uint16_t x,
                          std::uint16_t y) noexcept;

/* Unconditionally cancels a held action so callers can send a safe release. */
[[nodiscard]] Action cancel(Input &input) noexcept;

/* Compares both the type and relevant payload of two semantic actions. */
[[nodiscard]] bool actionEqual(const Action &left, const Action &right) noexcept;

}  // namespace buddy::codex
