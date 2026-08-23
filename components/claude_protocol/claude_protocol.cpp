#include "claude_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "fixed_json.hpp"

namespace buddy::claude {
namespace {

constexpr std::size_t TokenCount = 256;
using Parser = fixed_json::Parser<TokenCount>;
using fixed_json::Token;
using fixed_json::TokenType;
using fixed_json::objectValue;
using fixed_json::tokenEquals;
using fixed_json::tokenI64;
using fixed_json::tokenize;
using fixed_json::tokenStringCopy;
using fixed_json::tokenU64;

/* Claude messages are processed on the single application task. Keeping the
   fixed token table here avoids consuming half of that task's stack. */
Parser parserStorage;

/* Finds an error flag at any depth in a turn event's raw SDK content. */
bool hasErrorFlag(std::string_view json, const Parser &parser) noexcept
{
    for (std::size_t index = 0; index + 1 < parser.count; ++index) {
        const Token &key = parser.tokens[index];
        const Token &value = parser.tokens[index + 1];
        if (key.parent >= 0 && value.parent == key.parent &&
            parser.tokens[static_cast<std::size_t>(key.parent)].type ==
                TokenType::Object &&
            tokenEquals(json, key, "is_error") && value.type == TokenType::Primitive &&
            json.substr(static_cast<std::size_t>(value.start),
                        static_cast<std::size_t>(value.end - value.start)) == "true")
            return true;
    }
    return false;
}

/* Reads an optional named unsigned field and narrows it safely to uint32_t. */
bool copyU32Field(std::string_view json, const Parser &parser, std::string_view key,
                  std::uint32_t &destination) noexcept
{
    const int index = objectValue(json, parser, 0, key);
    std::uint64_t value = 0;
    if (index < 0 || !tokenU64(json, parser.tokens[static_cast<std::size_t>(index)], value) ||
        value > UINT32_MAX) return false;
    destination = static_cast<std::uint32_t>(value);
    return true;
}

/* Reads an optional named unsigned field into a uint64_t destination. */
bool copyU64Field(std::string_view json, const Parser &parser, std::string_view key,
                  std::uint64_t &destination) noexcept
{
    const int index = objectValue(json, parser, 0, key);
    return index >= 0 && tokenU64(json, parser.tokens[static_cast<std::size_t>(index)],
                                  destination);
}

/* Appends one string with JSON escaping while preserving buffer termination. */
bool appendEscaped(std::span<char> destination, std::size_t &offset,
                   std::string_view source) noexcept
{
    for (const char ch : source) {
        const auto byte = static_cast<unsigned char>(ch);
        const char *escape = nullptr;
        if (byte == '"') escape = "\\\"";
        else if (byte == '\\') escape = "\\\\";
        else if (byte == '\n') escape = "\\n";
        else if (byte == '\r') escape = "\\r";
        else if (byte == '\t') escape = "\\t";
        if (escape != nullptr) {
            if (offset + 2 >= destination.size()) return false;
            destination[offset++] = escape[0];
            destination[offset++] = escape[1];
        } else if (byte >= 0x20U) {
            if (offset + 1 >= destination.size()) return false;
            destination[offset++] = static_cast<char>(byte);
        }
    }
    destination[offset] = '\0';
    return true;
}

/* Applies a desktop snapshot to session totals, activity, prompt, and identity. */
bool handleSnapshot(std::string_view json, const Parser &parser, Model &model,
                    std::uint32_t nowMs) noexcept
{
    const std::uint32_t previousRunning = model.runningSessions;
    std::uint32_t total = 0;
    std::uint32_t running = 0;
    std::uint32_t waiting = 0;
    if (!copyU32Field(json, parser, "total", total) ||
        !copyU32Field(json, parser, "running", running) ||
        !copyU32Field(json, parser, "waiting", waiting)) return false;

    model.connection = Connection::Connected;
    model.totalSessions = total;
    model.runningSessions = running;
    model.waitingSessions = waiting;
    copyU64Field(json, parser, "tokens", model.tokens);
    copyU64Field(json, parser, "tokens_today", model.tokensToday);

    const int message = objectValue(json, parser, 0, "msg");
    if (message >= 0)
        tokenStringCopy(json, parser.tokens[static_cast<std::size_t>(message)], model.message);

    model.entries.fill({});
    const int entries = objectValue(json, parser, 0, "entries");
    std::size_t entryIndex = 0;
    if (entries >= 0 && parser.tokens[static_cast<std::size_t>(entries)].type ==
                             TokenType::Array) {
        for (std::size_t index = static_cast<std::size_t>(entries) + 1;
             index < parser.count && entryIndex < ModelEntryCount; ++index) {
            if (parser.tokens[index].parent == entries &&
                parser.tokens[index].type == TokenType::String) {
                tokenStringCopy(json, parser.tokens[index], model.entries[entryIndex]);
                ++entryIndex;
            }
        }
    }
    const std::size_t entryCount = activityCount(model);
    const std::size_t maximumOffset =
        entryCount > ModelActivityVisibleCount
        ? entryCount - ModelActivityVisibleCount : 0;
    if (model.activityOffset > maximumOffset)
        model.activityOffset = static_cast<std::uint8_t>(maximumOffset);

    model.promptActive = false;
    model.promptId[0] = '\0';
    model.promptTool[0] = '\0';
    model.promptHint[0] = '\0';
    const int prompt = objectValue(json, parser, 0, "prompt");
    if (prompt >= 0 && parser.tokens[static_cast<std::size_t>(prompt)].type ==
                            TokenType::Object) {
        const int id = objectValue(json, parser, prompt, "id");
        const int tool = objectValue(json, parser, prompt, "tool");
        const int hint = objectValue(json, parser, prompt, "hint");
        if (id >= 0 && parser.tokens[static_cast<std::size_t>(id)].type == TokenType::String) {
            tokenStringCopy(json, parser.tokens[static_cast<std::size_t>(id)], model.promptId);
            model.promptActive = model.promptId[0] != '\0';
        }
        if (tool >= 0)
            tokenStringCopy(json, parser.tokens[static_cast<std::size_t>(tool)], model.promptTool);
        if (hint >= 0)
            tokenStringCopy(json, parser.tokens[static_cast<std::size_t>(hint)], model.promptHint);
    }
    if (previousRunning > 0 && running == 0 && waiting == 0 && !model.promptActive) {
        model.transientState = PetState::Celebrate;
        model.transientUntilMs = nowMs + 1800U;
    }
    model.lastSnapshotMs = nowMs;
    return true;
}

/* Validates formatted response length and marks the action ready to send. */
bool setResponse(Action &action, int written) noexcept
{
    if (written < 0 || static_cast<std::size_t>(written) >= action.response.size())
        return false;
    action.responseLength = static_cast<std::size_t>(written);
    action.sendResponse = true;
    return true;
}

/* Builds the device-status response from runtime and model context. */
bool statusResponse(Action &action, const Model &model,
                    const Context &context) noexcept
{
    std::array<char, ModelNameSize * 2> name{};
    std::size_t nameLength = 0;
    if (!appendEscaped(name, nameLength,
                       std::string_view{model.deviceName.data()})) return false;
    int written;
    if (context.batteryKnown) {
        written = std::snprintf(action.response.data(), action.response.size(),
            "{\"ack\":\"status\",\"ok\":true,\"data\":{\"name\":\"%s\","
            "\"sec\":%s,\"bat\":{\"pct\":%u,\"mV\":%u,\"mA\":%d,"
            "\"usb\":%s},\"sys\":{\"up\":%u},\"stats\":{\"appr\":%u,"
            "\"deny\":%u}}}", name.data(), context.secure ? "true" : "false",
            static_cast<unsigned>(context.batteryPercent),
            static_cast<unsigned>(context.batteryMv),
            static_cast<int>(context.batteryMa), context.usbPowered ? "true" : "false",
            static_cast<unsigned>(context.uptimeSeconds),
            static_cast<unsigned>(model.approvals), static_cast<unsigned>(model.denials));
    } else {
        written = std::snprintf(action.response.data(), action.response.size(),
            "{\"ack\":\"status\",\"ok\":true,\"data\":{\"name\":\"%s\","
            "\"sec\":%s,\"sys\":{\"up\":%u},\"stats\":{\"appr\":%u,"
            "\"deny\":%u}}}", name.data(), context.secure ? "true" : "false",
            static_cast<unsigned>(context.uptimeSeconds),
            static_cast<unsigned>(model.approvals), static_cast<unsigned>(model.denials));
    }
    return setResponse(action, written);
}

/* Handles state-changing desktop commands and builds their acknowledgement. */
bool commandResponse(std::string_view json, const Parser &parser, int command,
                     Model &model, const Context &context, Action &action) noexcept
{
    const Token &commandToken = parser.tokens[static_cast<std::size_t>(command)];
    if (tokenEquals(json, commandToken, "status"))
        return statusResponse(action, model, context);

    if (tokenEquals(json, commandToken, "owner") || tokenEquals(json, commandToken, "name")) {
        const bool owner = tokenEquals(json, commandToken, "owner");
        const int value = objectValue(json, parser, 0, "name");
        if (value >= 0) {
            tokenStringCopy(json, parser.tokens[static_cast<std::size_t>(value)],
                      owner ? std::span<char>{model.owner} : std::span<char>{model.deviceName});
            action.modelChanged = true;
            action.persistModel = true;
        }
        const int written = std::snprintf(action.response.data(), action.response.size(),
            "{\"ack\":\"%s\",\"ok\":true}", owner ? "owner" : "name");
        return setResponse(action, written);
    }

    if (tokenEquals(json, commandToken, "unpair")) {
        action.forgetBondAfterResponse = true;
        return setResponse(action, std::snprintf(action.response.data(),
            action.response.size(), "{\"ack\":\"unpair\",\"ok\":true}"));
    }

    std::array<char, 32> commandName{};
    tokenStringCopy(json, commandToken, commandName);
    return setResponse(action, std::snprintf(action.response.data(), action.response.size(),
        "{\"ack\":\"%s\",\"ok\":false,\"error\":\"unsupported\"}", commandName.data()));
}

}  // namespace

void Decoder::reset() noexcept
{
    line_.fill('\0');
    length_ = 0;
    discarding_ = false;
}

Result Decoder::push(std::span<const std::uint8_t> bytes, std::size_t &consumed) noexcept
{
    consumed = 0;
    while (consumed < bytes.size()) {
        const std::uint8_t byte = bytes[consumed++];
        if (byte == '\n') {
            if (discarding_) {
                reset();
                return Result::NoSpace;
            }
            if (length_ > 0 && line_[length_ - 1] == '\r') --length_;
            line_[length_] = '\0';
            return length_ == 0 ? Result::Invalid : Result::Complete;
        }
        if (discarding_) continue;
        if (length_ >= LineSize) {
            discarding_ = true;
            continue;
        }
        line_[length_++] = static_cast<char>(byte);
    }
    return Result::Incomplete;
}

std::string_view Decoder::line() const noexcept
{
    return {line_.data(), length_};
}

Result handleLine(std::string_view json, Model &model, const Context &context,
                  Action &action) noexcept
{
    action = Action{};
    Parser &parser = parserStorage;
    if (!tokenize(json, parser)) return Result::Invalid;

    const int total = objectValue(json, parser, 0, "total");
    if (total >= 0) {
        if (!handleSnapshot(json, parser, model, context.nowMs)) return Result::Invalid;
        action.modelChanged = true;
        return Result::Complete;
    }

    const int command = objectValue(json, parser, 0, "cmd");
    if (command >= 0 && parser.tokens[static_cast<std::size_t>(command)].type ==
                             TokenType::String) {
        if (!commandResponse(json, parser, command, model, context, action))
            return Result::NoSpace;
        return Result::Complete;
    }

    const int time = objectValue(json, parser, 0, "time");
    if (time >= 0) {
        if (parser.tokens[static_cast<std::size_t>(time)].type != TokenType::Array)
            return Result::Invalid;
        int epochToken = -1;
        int offsetToken = -1;
        for (std::size_t index = static_cast<std::size_t>(time) + 1; index < parser.count;
             ++index) {
            if (parser.tokens[index].parent != time) continue;
            if (epochToken < 0) epochToken = static_cast<int>(index);
            else if (offsetToken < 0) offsetToken = static_cast<int>(index);
            else return Result::Invalid;
        }
        std::uint64_t epoch = 0;
        std::int64_t offset = 0;
        if (epochToken < 0 || offsetToken < 0 ||
            !tokenU64(json, parser.tokens[static_cast<std::size_t>(epochToken)], epoch) ||
            !tokenI64(json, parser.tokens[static_cast<std::size_t>(offsetToken)], offset) ||
            offset < INT32_MIN || offset > INT32_MAX)
            return Result::Invalid;
        setClock(model, epoch, static_cast<std::int32_t>(offset), context.nowMs);
        action.modelChanged = true;
        action.clockChanged = true;
        return Result::Complete;
    }
    const int event = objectValue(json, parser, 0, "evt");
    if (event >= 0) {
        if (tokenEquals(json, parser.tokens[static_cast<std::size_t>(event)], "turn"))
            action.errorOccurred = hasErrorFlag(json, parser);
        return Result::Complete;
    }
    return Result::Invalid;
}

Result encodePermission(std::string_view promptId, bool approve, std::span<char> destination,
                        std::size_t &length) noexcept
{
    if (promptId.empty() || destination.empty()) return Result::Invalid;
    constexpr std::string_view Prefix = "{\"cmd\":\"permission\",\"id\":\"";
    const std::string_view decision = approve ? "\",\"decision\":\"once\"}"
                                              : "\",\"decision\":\"deny\"}";
    if (Prefix.size() >= destination.size()) return Result::NoSpace;
    std::ranges::copy(Prefix, destination.begin());
    std::size_t offset = Prefix.size();
    destination[offset] = '\0';
    if (!appendEscaped(destination, offset, promptId)) return Result::NoSpace;
    if (offset + decision.size() >= destination.size()) return Result::NoSpace;
    std::ranges::copy(decision, destination.begin() + static_cast<std::ptrdiff_t>(offset));
    destination[offset + decision.size()] = '\0';
    length = offset + decision.size();
    return Result::Complete;
}

}  // namespace buddy::claude
