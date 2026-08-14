#include "codex_controller.hpp"

#include <array>

#include "codex_protocol.hpp"

namespace buddy::codex {
namespace {

/* Encodes and sends one Codex key phase through the injected transport. */
bool sendKey(CommandTransport *transport, buddy::codex::Key key,
            buddy::codex::KeyAction action)
{
    if (transport == nullptr) return false;
    std::array<char, 128> json{};
    std::size_t length{};
    if (buddy::codex::encodeKeyEvent(key, action, json, length) !=
        buddy::codex::Result::Ok) return false;
    return transport->sendJson({json.data(), length});
}

/* Encodes and sends one directional edge through the injected transport. */
bool sendDirection(CommandTransport *transport, buddy::codex::Direction direction,
                   bool pressed)
{
    if (transport == nullptr) return false;
    std::array<char, 128> json{};
    std::size_t length{};
    if (buddy::codex::encodeDirectionEvent(direction, pressed, json, length) !=
        buddy::codex::Result::Ok) return false;
    return transport->sendJson({json.data(), length});
}

}  // namespace

/* Implements action policy, including safe release semantics and model updates. */
bool handleAction(Model &model, const Action &action, ActionPhase phase,
                  CommandTransport *transport)
{
    switch (action.type) {
        case ActionType::None:
            return false;
        case ActionType::Page: {
            if (phase != ActionPhase::Press) return false;
            return applyEvent(model, Event{PageSelected{action.page}});
        }
        case ActionType::DismissOverlay: {
            if (phase != ActionPhase::Press) return false;
            return applyEvent(model, Event{OverlayDismissed{}});
        }
        case ActionType::OpenMenu:
        case ActionType::CloseMenu: {
            if (phase != ActionPhase::Press) return false;
            return action.type == ActionType::OpenMenu
                ? applyEvent(model, Event{MenuOpened{}})
                : applyEvent(model, Event{MenuClosed{}});
        }
        case ActionType::SwitchMode:
            return false;
        case ActionType::Slot: {
            if (phase == ActionPhase::Press) {
                if (model.connection != Connection::Connected ||
                    action.slot >= SlotCount ||
                    !sendKey(transport, buddy::codex::agentKey(action.slot),
                             buddy::codex::KeyAction::Press)) return false;
                (void)applyEvent(model, Event{SlotSelected{action.slot}});
                return true;
            }
            return sendKey(transport, buddy::codex::agentKey(action.slot),
                          buddy::codex::KeyAction::Release);
        }
        case ActionType::Direction:
            if (!commandsEnabled(model)) return false;
            return sendDirection(transport, action.direction,
                                 phase == ActionPhase::Press);
        case ActionType::Key: {
            if (!commandsEnabled(model)) return false;
            const buddy::codex::Key key = action.key;
            if (key == buddy::codex::Key::DialCounterClockwise ||
                key == buddy::codex::Key::DialClockwise) {
                return phase == ActionPhase::Press &&
                       sendKey(transport, key, buddy::codex::KeyAction::Step);
            }
            const bool pressed = phase == ActionPhase::Press;
            if (!sendKey(transport, key, pressed ? buddy::codex::KeyAction::Press
                                                 : buddy::codex::KeyAction::Release))
                return false;
            if (key == buddy::codex::Key::Mic) {
                if (pressed) (void)applyEvent(model, Event{MicPressed{}});
                else if (phase == ActionPhase::Cancel)
                    (void)applyEvent(model, Event{InputCancelled{}});
                else (void)applyEvent(model, Event{MicReleased{}});
            }
            return true;
        }
    }
    return false;
}

}  // namespace buddy::codex
