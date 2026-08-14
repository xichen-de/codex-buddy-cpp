#include "codex_controller.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace buddy::codex;

struct Capture final : CommandTransport {
    char messages[8][160];
    size_t count;
    bool succeed;

    bool sendJson(std::string_view json) noexcept override
    {
        if (!succeed || count >= 8 || json.size() >= 160) return false;
        memcpy(messages[count], json.data(), json.size());
        messages[count][json.size()] = '\0';
        ++count;
        return true;
    }
};

static void connect(Model &model)
{
    init(model);
    assert(applyEvent(model, Event{ConnectionChanged{Connection::Connected}}));
}

static void test_selection_commands_and_mic(void)
{
    Model model;
    connect(model);
    Capture capture{};
    capture.succeed = true;
    Action action{.type = ActionType::Slot};
    action.slot = 2;
    assert(handleAction(model, action, ActionPhase::Press, &capture));
    assert(model.selectedSlot == 2);
    assert(strstr(capture.messages[0], "\"AG02\"") != nullptr);
    assert(handleAction(model, action, ActionPhase::Release, &capture));
    assert(strstr(capture.messages[1], "\"act\":0") != nullptr);

    action.type = ActionType::Key;
    action.key = buddy::codex::Key::Fast;
    assert(handleAction(model, action, ActionPhase::Press, &capture));
    assert(handleAction(model, action, ActionPhase::Release, &capture));

    action.key = buddy::codex::Key::Mic;
    assert(handleAction(model, action, ActionPhase::Press, &capture));
    assert(model.micHeld);
    assert(overlay(model) == Overlay::Listening);
    assert(handleAction(model, action, ActionPhase::Cancel, &capture));
    assert(!model.micHeld);
}

static void test_directions_rotation_and_failure(void)
{
    Model model;
    connect(model);
    Capture capture{};
    capture.succeed = true;
    Action slot{.type = ActionType::Slot};
    slot.slot = 0;
    assert(handleAction(model, slot, ActionPhase::Press, &capture));

    Action action{.type = ActionType::Direction};
    action.direction = buddy::codex::Direction::Left;
    assert(handleAction(model, action, ActionPhase::Press, &capture));
    assert(strstr(capture.messages[1], "\"a\":0.50") != nullptr);
    assert(handleAction(model, action, ActionPhase::Release, &capture));
    assert(strstr(capture.messages[2], "\"d\":0.0") != nullptr);

    action.type = ActionType::Key;
    action.key = buddy::codex::Key::DialClockwise;
    assert(handleAction(model, action, ActionPhase::Press, &capture));
    assert(strstr(capture.messages[3], "\"act\":2") != nullptr);
    assert(!handleAction(model, action, ActionPhase::Release, &capture));

    capture.succeed = false;
    slot.slot = 4;
    assert(!handleAction(model, slot, ActionPhase::Press, &capture));
    assert(model.selectedSlot == 0);
}

static void test_buddy_menu(void)
{
    Model model;
    init(model);
    const Action open{.type = ActionType::OpenMenu};
    assert(handleAction(model, open, ActionPhase::Press, nullptr));
    assert(model.menuOpen);
    assert(!handleAction(model, open, ActionPhase::Release, nullptr));
    const Action close{.type = ActionType::CloseMenu};
    assert(handleAction(model, close, ActionPhase::Press, nullptr));
    assert(!model.menuOpen);
}

int main(void)
{
    test_selection_commands_and_mic();
    test_directions_rotation_and_failure();
    test_buddy_menu();
    puts("codex_controller host tests passed");
    return 0;
}
