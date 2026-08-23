#include "codex_input.hpp"

#include "buddy_layout.hpp"

namespace buddy::codex {
namespace {

constexpr int TabTop = 210;

Action noAction() noexcept { return Action{.type = ActionType::None}; }

/* Tests a point against a left/top-inclusive, right/bottom-exclusive rectangle. */
bool inRect(std::uint16_t x, std::uint16_t y, unsigned left, unsigned top,
           unsigned width, unsigned height) noexcept
{
    return x >= left && x < left + width && y >= top && y < top + height;
}

/* Maps a bottom navigation-tab coordinate to its page action. */
Action tabAt(std::uint16_t x, std::uint16_t y) noexcept
{
    if (y < TabTop || y >= ScreenHeight || x >= ScreenWidth) return noAction();
    const unsigned index = x / 80U;
    if (index > static_cast<unsigned>(Page::Navigate)) return noAction();
    Action action{.type = ActionType::Page};
    action.page = static_cast<Page>(index);
    return action;
}

/* Reports whether a coordinate hits the dedicated menu tab. */
bool inMenuTab(std::uint16_t x, std::uint16_t y) noexcept
{
    return inRect(x, y, 240, TabTop, 80, ScreenHeight - TabTop);
}

/* Maps one cell in the shared Codex button grid to the requested action type. */
Action gridAction(std::uint16_t x, std::uint16_t y, ActionType type) noexcept
{
    using layout::codex_grid::ColumnStride;
    using layout::codex_grid::Columns;
    using layout::codex_grid::OriginX;
    using layout::codex_grid::OriginY;
    using layout::codex_grid::Rows;
    using layout::codex_grid::RowStride;
    if (x < OriginX || y < OriginY) return noAction();
    const unsigned column = (x - OriginX) / ColumnStride;
    const unsigned row = (y - OriginY) / RowStride;
    if (column >= Columns || row >= Rows ||
        !layout::contains(layout::codex_grid::cell(row * Columns + column), x, y))
        return noAction();
    const unsigned index = row * Columns + column;
    Action action{.type = type};
    if (type == ActionType::Slot) {
        action.slot = static_cast<std::uint8_t>(index);
    } else {
        action.key = buddy::codex::controlKey(index);
    }
    return action;
}

/* Hit-tests the Navigate page's direction and dial controls. */
Action navigateAction(std::uint16_t x, std::uint16_t y) noexcept
{
    Action action{.type = ActionType::Direction};
    if (inRect(x, y, 48, 38, 64, 48)) {
        action.direction = buddy::codex::Direction::Up;
    } else if (inRect(x, y, 48, 150, 64, 48)) {
        action.direction = buddy::codex::Direction::Down;
    } else if (inRect(x, y, 8, 94, 64, 48)) {
        action.direction = buddy::codex::Direction::Left;
    } else if (inRect(x, y, 88, 94, 64, 48)) {
        action.direction = buddy::codex::Direction::Right;
    } else {
        action.type = ActionType::Key;
        if (inRect(x, y, 174, 42, 62, 62)) {
            action.key = buddy::codex::Key::DialCounterClockwise;
        } else if (inRect(x, y, 246, 42, 62, 62)) {
            action.key = buddy::codex::Key::DialClockwise;
        } else if (inRect(x, y, 174, 118, 134, 78)) {
            action.key = buddy::codex::Key::DialPress;
        } else {
            return noAction();
        }
    }
    return action;
}

/* Identifies actions that require a later release or cancellation phase. */
bool actionIsHoldable(const Action &action) noexcept
{
    if (action.type == ActionType::Slot || action.type == ActionType::Direction)
        return true;
    /* Rotation is sent only once, but remains visually active until release. */
    return action.type == ActionType::Key;
}

}  // namespace

/* Starts the gesture tracker with no active held action. */
void init(Input &input) noexcept
{
    input.active = false;
    input.action = noAction();
}

/* Resolves overlays, tabs, pages, and controls in visual priority order. */
Action press(Input &input, const Model &model, std::uint16_t x, std::uint16_t y) noexcept
{
    if (input.active || x >= ScreenWidth || y >= ScreenHeight) return noAction();

    const Overlay currentOverlay = overlay(model);
    if (currentOverlay == Overlay::Menu) {
        if (inMenuTab(x, y)) return Action{.type = ActionType::CloseMenu};
        if (layout::contains(layout::codex_menu::Mute, x, y))
            return Action{.type = ActionType::ToggleMute};
        if (layout::contains(layout::codex_menu::SwitchMode, x, y))
            return Action{.type = ActionType::SwitchMode};
        if (layout::contains(layout::codex_menu::Close, x, y))
            return Action{.type = ActionType::CloseMenu};
        return noAction();
    }
    if (inMenuTab(x, y)) return Action{.type = ActionType::OpenMenu};
    if (currentOverlay == Overlay::Complete || currentOverlay == Overlay::Error ||
        currentOverlay == Overlay::RequiresInput)
        return Action{.type = ActionType::DismissOverlay};
    if (currentOverlay == Overlay::Connection || currentOverlay == Overlay::Listening)
        return noAction();

    Action action = tabAt(x, y);
    if (action.type == ActionType::None) {
        switch (model.page) {
            case Page::Control:
                if (!commandsEnabled(model)) return noAction();
                action = gridAction(x, y, ActionType::Key);
                break;
            case Page::Agents:
                if (model.connection != Connection::Connected) return noAction();
                action = gridAction(x, y, ActionType::Slot);
                break;
            case Page::Navigate:
                if (!commandsEnabled(model)) return noAction();
                action = navigateAction(x, y);
                break;
        }
    }

    if (actionIsHoldable(action)) {
        input.active = true;
        input.action = action;
    }
    return action;
}

/* Clears and returns the active action so the runtime can release it safely. */
Action cancel(Input &input) noexcept
{
    if (!input.active) return noAction();
    const Action action = input.action;
    init(input);
    return action;
}

/* Treats pointer release as an unconditional cancellation of tracked input. */
Action release(Input &input) noexcept
{
    return cancel(input);
}

/* Cancels a held action when moving to a target that no longer matches it. */
Action drag(Input &input, const Model &model, std::uint16_t x, std::uint16_t y) noexcept
{
    if (!input.active) return noAction();
    Action underPointer = noAction();
    if (x < ScreenWidth && y < TabTop) {
        switch (model.page) {
            case Page::Control: underPointer = gridAction(x, y, ActionType::Key); break;
            case Page::Agents: underPointer = gridAction(x, y, ActionType::Slot); break;
            case Page::Navigate: underPointer = navigateAction(x, y); break;
        }
    }
    if (actionEqual(input.action, underPointer)) return noAction();
    return cancel(input);
}

/* Compares the payload selected by the action type. */
bool actionEqual(const Action &left, const Action &right) noexcept
{
    if (left.type != right.type) return false;
    switch (left.type) {
        case ActionType::None:
        case ActionType::DismissOverlay:
        case ActionType::OpenMenu:
        case ActionType::CloseMenu:
        case ActionType::ToggleMute:
        case ActionType::SwitchMode:
            return true;
        case ActionType::Page: return left.page == right.page;
        case ActionType::Slot: return left.slot == right.slot;
        case ActionType::Key: return left.key == right.key;
        case ActionType::Direction: return left.direction == right.direction;
    }
    return false;
}

}  // namespace buddy::codex
