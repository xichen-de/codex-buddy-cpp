#include "codex_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <type_traits>

namespace buddy::codex {
namespace {

template <typename Enum>
constexpr auto toValue(Enum value) noexcept
{
    return static_cast<std::underlying_type_t<Enum>>(value);
}

constexpr std::array<std::string_view, 15> KeyIds{
    "AG00", "AG01", "AG02", "AG03", "AG04", "AG05",
    "ACT06", "ACT07", "ACT08", "ACT09", "ACT10", "ACT12",
    "ENC_CC", "ENC_CW", "ENC",
};

constexpr bool valid(Key key) noexcept
{
    return toValue(key) <= toValue(Key::DialPress);
}

constexpr bool valid(Direction direction) noexcept
{
    return toValue(direction) <= toValue(Direction::Up);
}

Result finishJson(int written, std::span<char> destination,
                  std::size_t &length) noexcept
{
    if (written < 0 || static_cast<std::size_t>(written) >= destination.size()) {
        length = 0;
        return Result::NoSpace;
    }
    length = static_cast<std::size_t>(written);
    return Result::Ok;
}

bool payloadStartsRequest(std::span<const std::uint8_t> payload) noexcept
{
    constexpr std::string_view Prefix{"{\"method\""};
    auto first = std::ranges::find_if_not(payload, [](std::uint8_t byte) {
        return byte == ' ' || byte == '\r' || byte == '\n' || byte == '\t';
    });
    const auto remaining = std::span{first, payload.end()};
    return remaining.size() >= Prefix.size() &&
           std::equal(Prefix.begin(), Prefix.end(), remaining.begin());
}

}  // namespace

std::string_view keyId(Key key) noexcept
{
    return valid(key) ? KeyIds[toValue(key)] : std::string_view{};
}

Result encodeKeyEvent(Key key, KeyAction action,
                      std::span<char> destination,
                      std::size_t &length) noexcept
{
    const auto actionValue = toValue(action);
    if (!valid(key) || actionValue > toValue(KeyAction::Step) ||
        destination.empty()) {
        length = 0;
        return Result::InvalidArgument;
    }
    const bool rotation = key == Key::DialCounterClockwise ||
                          key == Key::DialClockwise;
    if ((rotation && action != KeyAction::Step) ||
        (!rotation && action == KeyAction::Step)) {
        length = 0;
        return Result::InvalidArgument;
    }

    const auto id = keyId(key);
    const auto keyValue = toValue(key);
    const int written = keyValue <= toValue(Key::Agent6)
        ? std::snprintf(destination.data(), destination.size(),
              "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%.*s\",\"act\":%u,\"ag\":%u}}",
              static_cast<int>(id.size()), id.data(), actionValue, keyValue)
        : std::snprintf(destination.data(), destination.size(),
              "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%.*s\",\"act\":%u}}",
              static_cast<int>(id.size()), id.data(), actionValue);
    return finishJson(written, destination, length);
}

Result encodeDirectionEvent(Direction direction, bool pressed,
                            std::span<char> destination,
                            std::size_t &length) noexcept
{
    constexpr std::array<std::string_view, 4> Angles{
        "0.00", "0.25", "0.50", "0.75"};
    if (!valid(direction) || destination.empty()) {
        length = 0;
        return Result::InvalidArgument;
    }
    const auto angle = Angles[toValue(direction)];
    const int written = std::snprintf(destination.data(), destination.size(),
        "{\"method\":\"v.oai.rad\",\"params\":{\"a\":%.*s,\"d\":%s}}",
        static_cast<int>(angle.size()), angle.data(), pressed ? "1.0" : "0.0");
    return finishJson(written, destination, length);
}

Result encodeReports(std::string_view json, std::span<Report> reports,
                     std::size_t &reportCount) noexcept
{
    reportCount = 0;
    if (json.empty()) return Result::InvalidArgument;
    const std::size_t wireLength = json.size() + 1;
    const std::size_t needed = (wireLength + PayloadSize - 1) / PayloadSize;
    if (needed > reports.size()) return Result::NoSpace;

    std::size_t sourceOffset = 0;
    for (std::size_t index = 0; index < needed; ++index) {
        auto &report = reports[index];
        report.fill(0);
        report[0] = 2;
        const std::size_t payloadLength =
            std::min(wireLength - sourceOffset, PayloadSize);
        report[1] = static_cast<std::uint8_t>(payloadLength);
        for (std::size_t i = 0; i < payloadLength; ++i) {
            const std::size_t position = sourceOffset + i;
            report[2 + i] = position < json.size()
                ? static_cast<std::uint8_t>(json[position])
                : static_cast<std::uint8_t>('\n');
        }
        sourceOffset += payloadLength;
    }
    reportCount = needed;
    return Result::Ok;
}

void Decoder::reset() noexcept
{
    json_.fill('\0');
    length_ = 0;
    scanOffset_ = 0;
    depth_ = 0;
    started_ = false;
    inString_ = false;
    escaped_ = false;
}

Result Decoder::scanJson() noexcept
{
    for (; scanOffset_ < length_; ++scanOffset_) {
        const char byte = json_[scanOffset_];
        if (!started_) {
            if (byte == ' ' || byte == '\r' || byte == '\n' || byte == '\t')
                continue;
            if (byte != '{') return Result::InvalidReport;
            started_ = true;
            depth_ = 1;
            continue;
        }
        if (inString_) {
            if (escaped_) escaped_ = false;
            else if (byte == '\\') escaped_ = true;
            else if (byte == '"') inString_ = false;
            continue;
        }
        if (byte == '"') inString_ = true;
        else if (byte == '{' || byte == '[') ++depth_;
        else if (byte == '}' || byte == ']') {
            if (depth_ == 0) return Result::InvalidReport;
            if (--depth_ == 0) {
                json_[scanOffset_ + 1] = '\0';
                length_ = scanOffset_ + 1;
                return Result::Complete;
            }
        }
    }
    return Result::Incomplete;
}

Result Decoder::push(std::span<const std::uint8_t> report) noexcept
{
    std::size_t offset = 0;
    if (report.size() == ReportBodySize + 1 && report.front() == ReportId)
        offset = 1;
    if (report.size() < offset + 2 || report[offset] != 2)
        return Result::InvalidReport;

    std::size_t payloadLength = report[offset + 1];
    if (payloadLength > PayloadSize || report.size() < offset + 2 + payloadLength)
        return Result::InvalidReport;
    auto payload = report.subspan(offset + 2, payloadLength);
    if (length_ > 0 && payloadStartsRequest(payload)) reset();
    if (length_ + payload.size() > RpcBufferSize) {
        reset();
        return Result::NoSpace;
    }
    if (length_ == 0) {
        const auto start = std::ranges::find(payload, static_cast<std::uint8_t>('{'));
        if (start == payload.end()) return Result::InvalidReport;
        payload = std::span{start, payload.end()};
    }
    std::ranges::copy(payload, json_.begin() + static_cast<std::ptrdiff_t>(length_));
    length_ += payload.size();
    json_[length_] = '\0';
    return scanJson();
}

std::string_view Decoder::json() const noexcept
{
    return {json_.data(), length_};
}

}  // namespace buddy::codex
