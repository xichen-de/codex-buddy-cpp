#include "codex_input.hpp"

#include <cassert>
#include <cstdio>

using namespace buddy::codex;

static void connect_and_select(Model &model)
{
    init(model);
    assert(applyEvent(model, Event{ConnectionChanged{Connection::Connected}}));
    assert(applyEvent(model, Event{SlotSelected{0}}));
}

static void test_control_press_release(void)
{
    Model model;
    connect_and_select(model);
    Input input;
    init(input);

    Action action = press(input, model, 10, 45);
    assert(action.type == ActionType::Key);
    assert(action.key == buddy::codex::Key::Fast);
    assert(input.active);
    action = release(input);
    assert(action.type == ActionType::Key);
    assert(action.key == buddy::codex::Key::Fast);
    assert(!input.active);

    action = press(input, model, 115, 130);
    assert(action.type == ActionType::Key);
    assert(action.key == buddy::codex::Key::Mic);
}

static void test_tabs_agents_and_blocking(void)
{
    Model model;
    init(model);
    Input input;
    init(input);
    assert(press(input, model, 20, 50).type == ActionType::None);
    Action action = press(input, model, 120, 225);
    assert(action.type == ActionType::None); /* connection overlay blocks all input */

    Event event = ConnectionChanged{Connection::Connected};
    assert(applyEvent(model, event));
    action = press(input, model, 120, 225);
    assert(action.type == ActionType::Page);
    assert(action.page == Page::Agents);
    event = PageSelected{Page::Agents};
    assert(applyEvent(model, event));

    action = press(input, model, 115, 130);
    assert(action.type == ActionType::Slot);
    assert(action.slot == 4);
    assert(cancel(input).type == ActionType::Slot);
}

static void test_navigation_and_overlays(void)
{
    Model model;
    connect_and_select(model);
    Event event = PageSelected{Page::Navigate};
    assert(applyEvent(model, event));
    Input input;
    init(input);

    Action action = press(input, model, 60, 50);
    assert(action.type == ActionType::Direction);
    assert(action.direction == buddy::codex::Direction::Up);
    (void)release(input);

    action = press(input, model, 185, 50);
    assert(action.type == ActionType::Key);
    assert(action.key == buddy::codex::Key::DialCounterClockwise);
    assert(input.active);
    assert(release(input).key == buddy::codex::Key::DialCounterClockwise);

    event = SlotStatusChanged{.index = 0, .value = Slot{
        .status = SlotStatus::Error, .color = 0xff0000, .brightness = 1.0f}};
    assert(applyEvent(model, event));
    action = press(input, model, 1, 1);
    assert(action.type == ActionType::DismissOverlay);
}

static void test_move_cancels_outside_target(void)
{
    Model model;
    connect_and_select(model);
    Input input;
    init(input);
    Action action = press(input, model, 55, 50);
    assert(action.type == ActionType::Key);
    action = drag(input, model, 60, 60);
    assert(action.type == ActionType::None);
    assert(input.active);
    action = drag(input, model, 160, 60);
    assert(action.type == ActionType::Key);
    assert(action.key == buddy::codex::Key::Fast);
    assert(!input.active);
}

static void test_buddy_menu_actions(void)
{
    Model model;
    init(model);
    Input input;
    init(input);

    Action action = press(input, model, 280, 225);
    assert(action.type == ActionType::OpenMenu);
    const Event open = MenuOpened{};
    assert(applyEvent(model, open));

    action = press(input, model, 160, 95);
    assert(action.type == ActionType::ToggleMute);
    action = press(input, model, 160, 145);
    assert(action.type == ActionType::SwitchMode);
    action = press(input, model, 160, 185);
    assert(action.type == ActionType::CloseMenu);
    action = press(input, model, 280, 225);
    assert(action.type == ActionType::CloseMenu);
    assert(press(input, model, 10, 10).type == ActionType::None);
}

int main(void)
{
    test_control_press_release();
    test_tabs_agents_and_blocking();
    test_navigation_and_overlays();
    test_move_cancels_outside_target();
    test_buddy_menu_actions();
    puts("codex_input host tests passed");
    return 0;
}
