#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "codex_input.hpp"
#include "codex_model.hpp"

/*
 * Bridges semantic Codex actions to local model events and protocol JSON.
 * The injected transport keeps this policy independent of the BLE stack.
 */

namespace buddy::codex {

enum class ActionPhase : std::uint8_t {
    Press,
    Release,
    Cancel,
};

class CommandTransport {
public:
    virtual ~CommandTransport() = default;
    [[nodiscard]] virtual bool sendJson(std::string_view json) noexcept = 0;
};

/*
 * Handles one press/release/cancel phase, updating local state and sending any
 * required protocol JSON. Returns true when state or host interaction changed.
 * `transport` may be null when no host interaction is possible.
 */
[[nodiscard]] bool handleAction(Model &model, const Action &action, ActionPhase phase,
                                CommandTransport *transport);

}  // namespace buddy::codex
