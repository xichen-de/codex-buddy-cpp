#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "codex_model.hpp"
#include "codex_protocol.hpp"

/* Converts complete Codex RPC requests into model events and JSON responses. */

namespace buddy::codex {

inline constexpr const char *FirmwareVersion = "1.0.0-cores3";
inline constexpr std::size_t MaxEvents = SlotCount;
inline constexpr std::size_t ResponseSize = 512;

struct RequestContext {
    const Model *model{};
    std::uint8_t batteryPercent{};
    bool batteryKnown{};
    bool charging{};
};

struct RequestResult {
    std::array<Event, MaxEvents> events{};
    std::size_t eventCount{};
    std::array<char, ResponseSize> response{};
    std::size_t responseLength{};

    [[nodiscard]] std::string_view responseView() const noexcept
    {
        return {response.data(), responseLength};
    }
};

/* Parses one complete request into model events and a protocol response. */
[[nodiscard]] Result handleRequest(std::string_view json, const RequestContext &context,
                                   RequestResult &result) noexcept;

/* Infers a semantic Agent state while retaining raw lighting in Slot. */
[[nodiscard]] SlotStatus classifySlot(const Slot &slot) noexcept;

}  // namespace buddy::codex
