#include "claude_protocol.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace buddy::claude {
namespace {

constexpr std::size_t TokenCount = 256;

enum class TokenType : std::uint8_t {
    Undefined,
    Object,
    Array,
    String,
    Primitive,
};

struct Token {
    TokenType type{TokenType::Undefined};
    int start{-1};
    int end{-1};
    int parent{-1};
};

struct Parser {
    std::array<Token, TokenCount> tokens{};
    std::size_t count{};
    int parent{-1};
};

/* Claude messages are processed on the single application task. Keeping the
   fixed token table here avoids consuming half of that task's stack. */
Parser parserStorage;

/* Allocates and initializes one token in the fixed parser token array. */
int newToken(Parser &parser, TokenType type, int start) noexcept
{
    if (parser.count >= TokenCount) return -1;
    const int index = static_cast<int>(parser.count++);
    parser.tokens[static_cast<std::size_t>(index)] =
        Token{.type = type, .start = start, .end = -1, .parent = parser.parent};
    return index;
}

/* Scans a quoted JSON string and validates escape boundaries. */
bool parseString(std::string_view json, std::size_t &position, Parser &parser) noexcept
{
    const int index = newToken(parser, TokenType::String, static_cast<int>(position) + 1);
    if (index < 0) return false;
    for (++position; position < json.size(); ++position) {
        const auto byte = static_cast<unsigned char>(json[position]);
        if (byte == '"') {
            parser.tokens[static_cast<std::size_t>(index)].end = static_cast<int>(position);
            return true;
        }
        if (byte < 0x20U) return false;
        if (byte == '\\') {
            if (++position >= json.size() ||
                std::string_view{"\"\\/bfnrtu"}.find(json[position]) == std::string_view::npos)
                return false;
            if (json[position] == 'u') {
                for (unsigned digit = 0; digit < 4; ++digit) {
                    if (++position >= json.size() ||
                        !std::isxdigit(static_cast<unsigned char>(json[position])))
                        return false;
                }
            }
        }
    }
    return false;
}

/* Scans a JSON primitive until whitespace or a structural delimiter. */
bool parsePrimitive(std::string_view json, std::size_t &position, Parser &parser) noexcept
{
    const int index = newToken(parser, TokenType::Primitive, static_cast<int>(position));
    if (index < 0) return false;
    while (position < json.size() &&
           std::string_view{" \t\r\n,]}"}.find(json[position]) == std::string_view::npos) {
        const auto byte = static_cast<unsigned char>(json[position]);
        if (byte < 0x20U || byte >= 0x7fU ||
            std::string_view{":{[\""}.find(static_cast<char>(byte)) != std::string_view::npos)
            return false;
        ++position;
    }
    if (parser.tokens[static_cast<std::size_t>(index)].start == static_cast<int>(position))
        return false;
    parser.tokens[static_cast<std::size_t>(index)].end = static_cast<int>(position);
    --position;
    return true;
}

/* Tokenizes one complete line using fixed storage and parent relationships. */
bool tokenize(std::string_view json, Parser &parser) noexcept
{
    parser = Parser{};
    parser.parent = -1;
    for (std::size_t position = 0; position < json.size(); ++position) {
        const char byte = json[position];
        if (byte == '{' || byte == '[') {
            const int index = newToken(
                parser, byte == '{' ? TokenType::Object : TokenType::Array,
                static_cast<int>(position));
            if (index < 0) return false;
            parser.parent = index;
        } else if (byte == '}' || byte == ']') {
            const TokenType expected = byte == '}' ? TokenType::Object : TokenType::Array;
            const int open = parser.parent;
            if (open < 0 || parser.tokens[static_cast<std::size_t>(open)].type != expected)
                return false;
            parser.tokens[static_cast<std::size_t>(open)].end = static_cast<int>(position) + 1;
            parser.parent = parser.tokens[static_cast<std::size_t>(open)].parent;
        } else if (byte == '"') {
            if (!parseString(json, position, parser)) return false;
        } else if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' ||
                   byte == ':' || byte == ',') {
            continue;
        } else if (!parsePrimitive(json, position, parser)) {
            return false;
        }
    }
    if (parser.parent != -1 || parser.count == 0 ||
        parser.tokens[0].type != TokenType::Object) return false;
    for (std::size_t index = 0; index < parser.count; ++index)
        if (parser.tokens[index].end < 0) return false;
    return true;
}

/* Compares a string token directly against a field or command name. */
bool tokenEquals(std::string_view json, const Token &token, std::string_view text) noexcept
{
    const auto length = static_cast<std::size_t>(token.end - token.start);
    return token.type == TokenType::String && text.size() == length &&
           json.substr(static_cast<std::size_t>(token.start), length) == text;
}

/* Finds the value token belonging to a direct child key of an object. */
int objectValue(std::string_view json, const Parser &parser, int object,
                std::string_view key) noexcept
{
    if (object < 0 || parser.tokens[static_cast<std::size_t>(object)].type != TokenType::Object)
        return -1;
    for (std::size_t index = static_cast<std::size_t>(object) + 1; index + 1 < parser.count;
         ++index) {
        if (parser.tokens[index].parent == object &&
            tokenEquals(json, parser.tokens[index], key) &&
            parser.tokens[index + 1].parent == object) return static_cast<int>(index) + 1;
    }
    return -1;
}

/* Parses an unsigned integer token with full-token and overflow validation. */
bool tokenU64(std::string_view json, const Token &token, std::uint64_t &value) noexcept
{
    if (token.type != TokenType::Primitive) return false;
    const auto length = static_cast<std::size_t>(token.end - token.start);
    if (length == 0 || length >= 32 || json[static_cast<std::size_t>(token.start)] == '-')
        return false;
    std::array<char, 32> buffer{};
    json.copy(buffer.data(), length, static_cast<std::size_t>(token.start));
    buffer[length] = '\0';
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(buffer.data(), &end, 10);
    if (end != buffer.data() + length) return false;
    value = static_cast<std::uint64_t>(parsed);
    return true;
}

/* Parses a signed integer token with full-token and range validation. */
bool tokenI64(std::string_view json, const Token &token, std::int64_t &value) noexcept
{
    if (token.type != TokenType::Primitive) return false;
    const auto length = static_cast<std::size_t>(token.end - token.start);
    if (length == 0 || length >= 32) return false;
    std::array<char, 32> buffer{};
    json.copy(buffer.data(), length, static_cast<std::size_t>(token.start));
    buffer[length] = '\0';
    char *end = nullptr;
    const long long parsed = std::strtoll(buffer.data(), &end, 10);
    if (end != buffer.data() + length) return false;
    value = static_cast<std::int64_t>(parsed);
    return true;
}

/* Appends U+FFFD's placeholder when an escaped Unicode sequence cannot be represented. */
void appendUtf8Replacement(std::span<char> destination, std::size_t &written) noexcept
{
    if (written + 1 < destination.size()) destination[written++] = '?';
}

/* Copies and minimally unescapes a JSON string into a bounded model buffer. */
void copyString(std::string_view json, const Token &token, std::span<char> destination) noexcept
{
    if (destination.empty()) return;
    destination[0] = '\0';
    if (token.type != TokenType::String) return;
    std::size_t written = 0;
    for (int position = token.start; position < token.end &&
         written + 1 < destination.size(); ++position) {
        char byte = json[static_cast<std::size_t>(position)];
        if (byte != '\\') {
            destination[written++] = byte;
            continue;
        }
        if (++position >= token.end) break;
        switch (json[static_cast<std::size_t>(position)]) {
            case '"': byte = '"'; break;
            case '\\': byte = '\\'; break;
            case '/': byte = '/'; break;
            case 'b': byte = '\b'; break;
            case 'f': byte = '\f'; break;
            case 'n': byte = ' '; break;
            case 'r': byte = ' '; break;
            case 't': byte = ' '; break;
            case 'u':
                position += 4;
                appendUtf8Replacement(destination, written);
                continue;
            default: continue;
        }
        destination[written++] = byte;
    }
    destination[written] = '\0';
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
        copyString(json, parser.tokens[static_cast<std::size_t>(message)], model.message);

    model.entries.fill({});
    const int entries = objectValue(json, parser, 0, "entries");
    std::size_t entryIndex = 0;
    if (entries >= 0 && parser.tokens[static_cast<std::size_t>(entries)].type ==
                             TokenType::Array) {
        for (std::size_t index = static_cast<std::size_t>(entries) + 1;
             index < parser.count && entryIndex < ModelEntryCount; ++index) {
            if (parser.tokens[index].parent == entries &&
                parser.tokens[index].type == TokenType::String) {
                copyString(json, parser.tokens[index], model.entries[entryIndex]);
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
            copyString(json, parser.tokens[static_cast<std::size_t>(id)], model.promptId);
            model.promptActive = model.promptId[0] != '\0';
        }
        if (tool >= 0)
            copyString(json, parser.tokens[static_cast<std::size_t>(tool)], model.promptTool);
        if (hint >= 0)
            copyString(json, parser.tokens[static_cast<std::size_t>(hint)], model.promptHint);
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
            copyString(json, parser.tokens[static_cast<std::size_t>(value)],
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
    copyString(json, commandToken, commandName);
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
    if (objectValue(json, parser, 0, "evt") >= 0) return Result::Complete;
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
