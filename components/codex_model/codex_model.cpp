#include "codex_model.hpp"

#include <cstring>

namespace buddy::codex {
namespace {

template <typename... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};

constexpr bool valid(Connection value) noexcept { return value <= Connection::Connected; }
constexpr bool valid(Page value) noexcept { return value <= Page::Navigate; }
constexpr bool valid(SlotStatus value) noexcept { return value <= SlotStatus::Unknown; }

/* Maps only notification-worthy status transitions to overlays. */
Overlay overlayForStatus(SlotStatus status) noexcept
{
    switch (status) {
        case SlotStatus::Complete: return Overlay::Complete;
        case SlotStatus::Error: return Overlay::Error;
        case SlotStatus::RequiresInput: return Overlay::RequiresInput;
        default: return Overlay::None;
    }
}

/* Compares every raw and classified field received for an Agent slot. */
bool slotsEqual(const Slot &left, const Slot &right) noexcept
{
    return left.status == right.status && left.color == right.color &&
           left.brightness == right.brightness &&
           left.effectSpeed == right.effectSpeed &&
           left.breathing == right.breathing &&
           left.effect == right.effect;
}

/* Compares the raw desktop-provided lighting configuration fields. */
bool lightsEqual(const Light &left, const Light &right) noexcept
{
    return left.color == right.color && left.brightness == right.brightness &&
           left.effectSpeed == right.effectSpeed &&
           left.effect == right.effect;
}

/* Clears state that is meaningful only while a desktop is connected. */
void resetConnectionState(Model &model) noexcept
{
    model.selectedSlot = NoSlot;
    model.micHeld = false;
    model.notification = Overlay::None;
    model.notificationSlot = NoSlot;
    model.slots.fill(Slot{});
}

}  // namespace

/* Establishes all model invariants from default-constructed state. */
void init(Model &model) noexcept
{
    model = Model{};
}

/* Validates and reduces one event into model state, reporting real changes. */
bool applyEvent(Model &model, const Event &event) noexcept
{
    return std::visit(Overloaded{
        [&model](const ConnectionChanged &change) {
            if (!valid(change.value) || model.connection == change.value) return false;
            model.connection = change.value;
            if (model.connection != Connection::Connected)
                resetConnectionState(model);
            return true;
        },
        [&model](const PageSelected &selection) {
            if (!valid(selection.value) || model.page == selection.value) return false;
            model.page = selection.value;
            return true;
        },
        [&model](const SlotSelected &selection) {
            if (model.connection != Connection::Connected ||
                selection.index >= SlotCount ||
                model.selectedSlot == selection.index) return false;
            model.selectedSlot = selection.index;
            return true;
        },
        [&model](const SlotStatusChanged &change) {
            const std::uint8_t slot = change.index;
            if (model.connection != Connection::Connected || slot >= SlotCount ||
                !valid(change.value.status)) return false;
            Slot &current = model.slots[slot];
            if (slotsEqual(current, change.value)) return false;
            const SlotStatus previousStatus = current.status;
            current = change.value;
            const Overlay transitionOverlay = overlayForStatus(current.status);
            if (current.status != previousStatus &&
                transitionOverlay != Overlay::None) {
                model.notification = transitionOverlay;
                model.notificationSlot = slot;
            }
            return true;
        },
        [&model](const LightingConfigured &configuration) {
            if (lightsEqual(model.ambientLight, configuration.ambient) &&
                lightsEqual(model.keyLight, configuration.keys))
                return false;
            model.ambientLight = configuration.ambient;
            model.keyLight = configuration.keys;
            return true;
        },
        [&model](MicPressed) {
            if (!commandsEnabled(model) || model.micHeld) return false;
            model.micHeld = true;
            return true;
        },
        [&model](MicReleased) {
            if (!model.micHeld) return false;
            model.micHeld = false;
            return true;
        },
        [&model](InputCancelled) {
            if (!model.micHeld) return false;
            model.micHeld = false;
            return true;
        },
        [&model](OverlayDismissed) {
            if (model.notification == Overlay::None) return false;
            model.notification = Overlay::None;
            model.notificationSlot = NoSlot;
            return true;
        },
        [&model](MenuOpened) {
            if (model.menuOpen) return false;
            model.menuOpen = true;
            return true;
        },
        [&model](MenuClosed) {
            if (!model.menuOpen) return false;
            model.menuOpen = false;
            return true;
        },
    }, event);
}

/* Resolves menu, connection, microphone, and notification overlay precedence. */
Overlay overlay(const Model &model) noexcept
{
    if (model.menuOpen) return Overlay::Menu;
    if (model.connection != Connection::Connected) return Overlay::Connection;
    if (model.micHeld) return Overlay::Listening;
    return model.notification;
}

/* Exposes a slot only for overlays that originated from Agent status. */
std::uint8_t overlaySlot(const Model &model) noexcept
{
    const Overlay active = overlay(model);
    if (active == Overlay::Menu || active == Overlay::Connection ||
        active == Overlay::Listening) return NoSlot;
    return model.notificationSlot;
}

/* Requires both a live connection and a valid selected Agent for commands. */
bool commandsEnabled(const Model &model) noexcept
{
    return model.connection == Connection::Connected &&
           model.selectedSlot < SlotCount;
}

}  // namespace buddy::codex
