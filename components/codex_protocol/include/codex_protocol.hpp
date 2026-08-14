#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace buddy::codex {

inline constexpr std::uint16_t VendorId = 0x303A;
inline constexpr std::uint16_t ProductId = 0x8360;
inline constexpr std::uint8_t ReportId = 6;
inline constexpr std::size_t ReportBodySize = 63;
inline constexpr std::size_t PayloadSize = 61;
inline constexpr std::size_t RpcBufferSize = 4096;

enum class Key : std::uint8_t {
    Agent1,
    Agent2,
    Agent3,
    Agent4,
    Agent5,
    Agent6,
    Fast,
    Approve,
    Decline,
    Fork,
    Mic,
    Send,
    DialCounterClockwise,
    DialClockwise,
    DialPress,
};

enum class KeyAction : std::uint8_t {
    Release = 0,
    Press = 1,
    Step = 2,
};

enum class Direction : std::uint8_t {
    Right,
    Down,
    Left,
    Up,
};

enum class Result : std::uint8_t {
    Ok,
    Incomplete,
    Complete,
    InvalidArgument,
    InvalidReport,
    NoSpace,
};

[[nodiscard]] constexpr Key agentKey(std::size_t index) noexcept
{
    return static_cast<Key>(static_cast<std::uint8_t>(Key::Agent1) + index);
}

[[nodiscard]] constexpr Key controlKey(std::size_t index) noexcept
{
    return static_cast<Key>(static_cast<std::uint8_t>(Key::Fast) + index);
}

using Report = std::array<std::uint8_t, ReportBodySize>;

inline constexpr std::array<std::uint8_t, 29> HidReportMap{
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x06,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F,
    0x09, 0x01, 0x81, 0x02, 0x95, 0x3F, 0x09, 0x02, 0x91,
    0x02, 0xC0,
};

[[nodiscard]] std::string_view keyId(Key key) noexcept;

[[nodiscard]] Result encodeKeyEvent(
    Key key, KeyAction action, std::span<char> destination,
    std::size_t &length) noexcept;

[[nodiscard]] Result encodeDirectionEvent(
    Direction direction, bool pressed, std::span<char> destination,
    std::size_t &length) noexcept;

[[nodiscard]] Result encodeReports(
    std::string_view json, std::span<Report> reports,
    std::size_t &reportCount) noexcept;

class Decoder final {
public:
    Decoder() = default;

    void reset() noexcept;
    [[nodiscard]] Result push(std::span<const std::uint8_t> report) noexcept;
    [[nodiscard]] std::string_view json() const noexcept;

private:
    [[nodiscard]] Result scanJson() noexcept;

    std::array<char, RpcBufferSize + 1> json_{};
    std::size_t length_{};
    std::size_t scanOffset_{};
    unsigned depth_{};
    bool started_{};
    bool inString_{};
    bool escaped_{};
};

}  // namespace buddy::codex
