#pragma once

#include <array>
#include <cstdint>
#include <variant>

/*
 * Pure Codex presentation state. All changes go through applyEvent(), making state
 * transitions host-testable and independent of BLE and drawing.
 */

namespace buddy::codex {

inline constexpr std::size_t SlotCount = 6;
inline constexpr std::uint8_t NoSlot = UINT8_MAX;

enum class Connection : std::uint8_t {
    Disconnected,
    Connecting,
    Connected,
};

enum class Page : std::uint8_t {
    Control,
    Agents,
    Navigate,
};

enum class SlotStatus : std::uint8_t {
    Unassigned,
    Idle,
    Thinking,
    Complete,
    RequiresInput,
    Error,
    Unknown,
};

enum class Overlay : std::uint8_t {
    None,
    Menu,
    Connection,
    Complete,
    Error,
    RequiresInput,
    Listening,
};

struct Slot {
    SlotStatus status{SlotStatus::Unassigned};
    std::uint32_t color{};
    float brightness{};
    float effectSpeed{};
    bool breathing{};
    std::array<char, 16> effect{};
};

struct Light {
    std::uint32_t color{};
    float brightness{};
    float effectSpeed{};
    std::array<char, 16> effect{};
};

struct Model {
    Connection connection{Connection::Disconnected};
    Page page{Page::Control};
    std::uint8_t selectedSlot{NoSlot};
    std::array<Slot, SlotCount> slots{};
    Light ambientLight{};
    Light keyLight{};
    Overlay notification{Overlay::None};
    std::uint8_t notificationSlot{NoSlot};
    bool micHeld{};
    bool menuOpen{};
};

struct ConnectionChanged {
    Connection value{Connection::Disconnected};
};

struct PageSelected {
    Page value{Page::Control};
};

struct SlotSelected {
    std::uint8_t index{NoSlot};
};

struct SlotStatusChanged {
    std::uint8_t index{NoSlot};
    Slot value{};
};

struct LightingConfigured {
    Light ambient{};
    Light keys{};
};

struct MicPressed {};
struct MicReleased {};
struct InputCancelled {};
struct OverlayDismissed {};
struct MenuOpened {};
struct MenuClosed {};

using Event = std::variant<ConnectionChanged, PageSelected, SlotSelected,
                           SlotStatusChanged, LightingConfigured, MicPressed,
                           MicReleased, InputCancelled, OverlayDismissed,
                           MenuOpened, MenuClosed>;

/* Initializes disconnected Codex state with no selected Agent. */
void init(Model &model) noexcept;

/* Applies one event; returns true only when observable model state changed. */
[[nodiscard]] bool applyEvent(Model &model, const Event &event) noexcept;

/* Resolves the highest-priority overlay implied by the current state. */
[[nodiscard]] Overlay overlay(const Model &model) noexcept;

/* Returns the Agent slot associated with the active notification overlay. */
[[nodiscard]] std::uint8_t overlaySlot(const Model &model) noexcept;

/* Reports whether the connection and Agent selection permit host commands. */
[[nodiscard]] bool commandsEnabled(const Model &model) noexcept;

}  // namespace buddy::codex
