#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "claude_model.hpp"

/*
 * Frames newline-delimited Claude JSON and translates complete commands into
 * model changes plus side-effect requests for the runtime to perform.
 */

namespace buddy::claude {

inline constexpr std::size_t LineSize = 6144;
inline constexpr std::size_t ResponseSize = 512;

enum class Result : std::uint8_t {
    Incomplete,
    Complete,
    Invalid,
    NoSpace,
};

struct Context {
    std::uint32_t nowMs{};
    std::uint32_t uptimeSeconds{};
    bool secure{};
    bool batteryKnown{};
    std::uint8_t batteryPercent{};
    std::uint16_t batteryMv{};
    std::int16_t batteryMa{};
    bool usbPowered{};
};

struct Action {
    bool modelChanged{};
    bool clockChanged{};
    bool persistModel{};
    bool sendResponse{};
    bool forgetBondAfterResponse{};
    std::array<char, ResponseSize> response{};
    std::size_t responseLength{};

    [[nodiscard]] std::string_view responseView() const noexcept
    {
        return {response.data(), responseLength};
    }
};

class Decoder final {
public:
    Decoder() = default;

    void reset() noexcept;

    /* Consumes bytes through newline, discarding an oversized line until
       resync. Advances `consumed` even when returning Incomplete. */
    [[nodiscard]] Result push(std::span<const std::uint8_t> bytes,
                              std::size_t &consumed) noexcept;

    /* The decoder-owned, null-terminated complete line buffer. */
    [[nodiscard]] std::string_view line() const noexcept;

private:
    std::array<char, LineSize + 1> line_{};
    std::size_t length_{};
    bool discarding_{};
};

/* Applies one complete command and describes required runtime side effects. */
[[nodiscard]] Result handleLine(std::string_view json, Model &model,
                                const Context &context, Action &action) noexcept;

/* Encodes an approve/deny response tied to the exact permission prompt ID. */
[[nodiscard]] Result encodePermission(std::string_view promptId, bool approve,
                                      std::span<char> destination,
                                      std::size_t &length) noexcept;

}  // namespace buddy::claude
