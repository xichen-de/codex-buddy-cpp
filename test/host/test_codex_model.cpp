#include "codex_model.hpp"

#include <cassert>
#include <cstdio>

using namespace buddy::codex;

static Event connection_event(Connection connection)
{
    return ConnectionChanged{connection};
}

static Event status_event(uint8_t slot, SlotStatus status, uint32_t color)
{
    return SlotStatusChanged{.index = slot, .value = Slot{
        .status = status,
        .color = color,
        .brightness = 1.0f,
        .effectSpeed = 0.5f,
        .breathing = status == SlotStatus::Thinking,
    }};
}

static void test_initial_and_connection_state(void)
{
    Model model;
    init(model);
    assert(model.connection == Connection::Disconnected);
    assert(model.page == Page::Control);
    assert(model.selectedSlot == NoSlot);
    assert(overlay(model) == Overlay::Connection);
    assert(!commandsEnabled(model));

    Event event = connection_event(Connection::Connected);
    assert(applyEvent(model, event));
    assert(!commandsEnabled(model));
    assert(overlay(model) == Overlay::None);
    assert(!applyEvent(model, event));

    Event select = SlotSelected{0};
    assert(applyEvent(model, select));
    assert(commandsEnabled(model));
}

static void test_selection_and_disconnect_reset(void)
{
    Model model;
    init(model);
    Event select = SlotSelected{2};
    assert(!applyEvent(model, select));

    Event event = connection_event(Connection::Connected);
    assert(applyEvent(model, event));
    assert(applyEvent(model, select));
    assert(model.selectedSlot == 2);

    event = status_event(2, SlotStatus::Thinking, 0x0000FF);
    assert(applyEvent(model, event));
    event = connection_event(Connection::Disconnected);
    assert(applyEvent(model, event));
    assert(model.selectedSlot == NoSlot);
    assert(model.slots[2].status == SlotStatus::Unassigned);
}

static void test_all_six_slots_are_selectable(void)
{
    Model model;
    init(model);
    Event event = connection_event(Connection::Connected);
    assert(applyEvent(model, event));

    for (uint8_t slot = 0; slot < SlotCount; ++slot) {
        event = SlotSelected{slot};
        assert(applyEvent(model, event));
        assert(model.selectedSlot == slot);
    }
    event = SlotSelected{static_cast<uint8_t>(SlotCount)};
    assert(!applyEvent(model, event));
    assert(model.selectedSlot == SlotCount - 1);
}

static void test_transition_overlays_are_deduplicated(void)
{
    Model model;
    init(model);
    Event event = connection_event(Connection::Connected);
    assert(applyEvent(model, event));

    event = status_event(1, SlotStatus::Complete, 0x00FF00);
    assert(applyEvent(model, event));
    assert(overlay(model) == Overlay::Complete);
    assert(overlaySlot(model) == 1);

    Event dismiss = OverlayDismissed{};
    assert(applyEvent(model, dismiss));
    assert(overlay(model) == Overlay::None);
    assert(!applyEvent(model, event));
    assert(overlay(model) == Overlay::None);

    std::get<SlotStatusChanged>(event).value.color = 0x00EE00;
    assert(applyEvent(model, event));
    assert(overlay(model) == Overlay::None);

    event = status_event(1, SlotStatus::Error, 0xFF0000);
    assert(applyEvent(model, event));
    assert(overlay(model) == Overlay::Error);
}

static void test_mic_overlay_and_cancellation(void)
{
    Model model;
    init(model);
    Event mic = MicPressed{};
    assert(!applyEvent(model, mic));

    Event event = connection_event(Connection::Connected);
    assert(applyEvent(model, event));
    Event select = SlotSelected{4};
    assert(applyEvent(model, select));
    event = status_event(4, SlotStatus::RequiresInput, 0xFFBF00);
    assert(applyEvent(model, event));
    assert(applyEvent(model, mic));
    assert(overlay(model) == Overlay::Listening);

    Event cancel = InputCancelled{};
    assert(applyEvent(model, cancel));
    assert(overlay(model) == Overlay::RequiresInput);
    assert(overlaySlot(model) == 4);

    event = connection_event(Connection::Disconnected);
    assert(applyEvent(model, event));
    assert(!model.micHeld);
    event = status_event(4, SlotStatus::Error, 0xFF0000);
    assert(!applyEvent(model, event));
    assert(model.slots[4].status == SlotStatus::Unassigned);
}

static void test_buddy_menu_overrides_connection_overlay(void)
{
    Model model;
    init(model);
    const Event open = MenuOpened{};
    assert(applyEvent(model, open));
    assert(model.menuOpen);
    assert(overlay(model) == Overlay::Menu);
    assert(overlaySlot(model) == NoSlot);
    assert(!applyEvent(model, open));

    const Event close = MenuClosed{};
    assert(applyEvent(model, close));
    assert(!model.menuOpen);
    assert(overlay(model) == Overlay::Connection);
}

int main(void)
{
    test_initial_and_connection_state();
    test_selection_and_disconnect_reset();
    test_all_six_slots_are_selectable();
    test_transition_overlays_are_deduplicated();
    test_mic_overlay_and_cancellation();
    test_buddy_menu_overrides_connection_overlay();
    puts("codex_model host tests passed");
    return 0;
}
