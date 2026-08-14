#include "codex_rpc.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

namespace buddy::codex {
namespace {

constexpr std::size_t TokenCount = 160;

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

/* Allocates and initializes one token in the fixed parser token array. */
int newToken(Parser &parser, TokenType type, int start) noexcept
{
    if (parser.count >= TokenCount) return -1;
    const int index = static_cast<int>(parser.count++);
    parser.tokens[static_cast<std::size_t>(index)] =
        Token{.type = type, .start = start, .end = -1, .parent = parser.parent};
    return index;
}

/* Scans a quoted JSON string, accepting escapes without decoding them yet. */
bool parseString(std::string_view json, std::size_t &position, Parser &parser) noexcept
{
    const int token = newToken(parser, TokenType::String, static_cast<int>(position) + 1);
    if (token < 0) return false;
    for (++position; position < json.size(); ++position) {
        const auto byte = static_cast<unsigned char>(json[position]);
        if (byte == '"') {
            parser.tokens[static_cast<std::size_t>(token)].end = static_cast<int>(position);
            return true;
        }
        if (byte < 0x20U) return false;
        if (byte == '\\') {
            if (++position >= json.size() ||
                std::string_view{"\"\\/bfnrtu"}.find(json[position]) == std::string_view::npos)
                return false;
            if (json[position] == 'u') {
                for (unsigned i = 0; i < 4; ++i) {
                    if (++position >= json.size() ||
                        !std::isxdigit(static_cast<unsigned char>(json[position])))
                        return false;
                }
            }
        }
    }
    return false;
}

/* Scans a number, boolean, or null primitive up to a structural delimiter. */
bool parsePrimitive(std::string_view json, std::size_t &position, Parser &parser) noexcept
{
    const int token = newToken(parser, TokenType::Primitive, static_cast<int>(position));
    if (token < 0) return false;
    while (position < json.size() &&
           std::string_view{" \t\r\n,]}"}.find(json[position]) == std::string_view::npos) {
        const auto byte = static_cast<unsigned char>(json[position]);
        if (byte < 0x20U || byte >= 0x7fU ||
            std::string_view{":{[\""}.find(static_cast<char>(byte)) != std::string_view::npos)
            return false;
        ++position;
    }
    if (parser.tokens[static_cast<std::size_t>(token)].start == static_cast<int>(position))
        return false;
    parser.tokens[static_cast<std::size_t>(token)].end = static_cast<int>(position);
    --position;
    return true;
}

/* Tokenizes the small supported JSON subset without dynamic allocation. */
bool tokenize(std::string_view json, Parser &parser) noexcept
{
    parser = Parser{};
    parser.parent = -1;
    for (std::size_t position = 0; position < json.size(); ++position) {
        const char byte = json[position];
        if (byte == '{' || byte == '[') {
            const int token = newToken(
                parser, byte == '{' ? TokenType::Object : TokenType::Array,
                static_cast<int>(position));
            if (token < 0) return false;
            parser.parent = token;
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
    for (std::size_t i = 0; i < parser.count; ++i)
        if (parser.tokens[i].end < 0) return false;
    return true;
}

/* Compares one string token directly with a field name. */
bool tokenEquals(std::string_view json, const Token &token, std::string_view text) noexcept
{
    const auto length = static_cast<std::size_t>(token.end - token.start);
    return token.type == TokenType::String && text.size() == length &&
           json.substr(static_cast<std::size_t>(token.start), length) == text;
}

/* Finds the value token for a direct child key in an object token. */
int objectValue(std::string_view json, const Parser &parser, int object,
                std::string_view key) noexcept
{
    if (object < 0 || parser.tokens[static_cast<std::size_t>(object)].type != TokenType::Object)
        return -1;
    for (std::size_t i = static_cast<std::size_t>(object) + 1; i + 1 < parser.count; ++i) {
        if (parser.tokens[i].parent == object && tokenEquals(json, parser.tokens[i], key))
            return static_cast<int>(i) + 1;
    }
    return -1;
}

/* Copies and parses a numeric token as a bounded double value. */
bool tokenNumber(std::string_view json, const Token &token, double &value) noexcept
{
    if (token.type != TokenType::Primitive) return false;
    const auto length = static_cast<std::size_t>(token.end - token.start);
    if (length == 0 || length >= 40) return false;
    std::array<char, 40> buffer{};
    json.copy(buffer.data(), length, static_cast<std::size_t>(token.start));
    buffer[length] = '\0';
    char *end = nullptr;
    value = std::strtod(buffer.data(), &end);
    return end == buffer.data() + length && std::isfinite(value);
}

/* Copies a string token into a bounded C buffer for model storage. */
void tokenStringCopy(std::string_view json, const Token &token,
                     std::span<char> destination) noexcept
{
    if (token.type != TokenType::String || destination.empty()) return;
    std::size_t length = static_cast<std::size_t>(token.end - token.start);
    if (length >= destination.size()) length = destination.size() - 1;
    json.copy(destination.data(), length, static_cast<std::size_t>(token.start));
    destination[length] = '\0';
}

/* Creates a neutral raw slot value before applying partial desktop fields. */
Slot defaultSlot() noexcept
{
    Slot slot{.status = SlotStatus::Unassigned};
    std::strcpy(slot.effect.data(), "off");
    return slot;
}

/* Checks whether the root RPC method field matches an expected method name. */
bool methodIs(std::string_view json, const Parser &parser, int method,
             std::string_view expected) noexcept
{
    return method >= 0 && tokenEquals(json, parser.tokens[static_cast<std::size_t>(method)],
                                      expected);
}

/* Copies the request ID verbatim so the response correlates with the request. */
std::size_t copyId(std::string_view json, const Parser &parser,
                   std::span<char> destination) noexcept
{
    const int id = objectValue(json, parser, 0, "id");
    if (id < 0)
        return static_cast<std::size_t>(
            std::snprintf(destination.data(), destination.size(), "null"));
    const Token &token = parser.tokens[static_cast<std::size_t>(id)];
    const bool quoted = token.type == TokenType::String;
    const auto rawLength = static_cast<std::size_t>(token.end - token.start);
    if (rawLength + (quoted ? 2U : 0U) + 1U > destination.size()) return 0;
    std::size_t offset = 0;
    if (quoted) destination[offset++] = '"';
    json.copy(destination.data() + offset, rawLength, static_cast<std::size_t>(token.start));
    offset += rawLength;
    if (quoted) destination[offset++] = '"';
    destination[offset] = '\0';
    return offset;
}

/* Merges one partial raw Agent-light object into an app slot value. */
void updateSlot(std::string_view json, const Parser &parser, int object,
                const RequestContext &context, RequestResult &result) noexcept
{
    const int idToken = objectValue(json, parser, object, "id");
    double id = -1;
    if (idToken < 0 ||
        !tokenNumber(json, parser.tokens[static_cast<std::size_t>(idToken)], id) || id < 0 ||
        id >= SlotCount || std::floor(id) != id ||
        result.eventCount >= MaxEvents) return;
    const auto index = static_cast<std::uint8_t>(id);
    Slot slot = context.model != nullptr ? context.model->slots[index] : defaultSlot();
    double number;
    int token = objectValue(json, parser, object, "c");
    if (token >= 0 && tokenNumber(json, parser.tokens[static_cast<std::size_t>(token)], number) &&
        number >= 0 && number <= 16777215 && std::floor(number) == number)
        slot.color = static_cast<std::uint32_t>(number);
    token = objectValue(json, parser, object, "b");
    if (token >= 0 && tokenNumber(json, parser.tokens[static_cast<std::size_t>(token)], number)) {
        slot.brightness = static_cast<float>(number);
        if (slot.brightness < 0) slot.brightness = 0;
        if (slot.brightness > 1) slot.brightness = 1;
    }
    token = objectValue(json, parser, object, "e");
    if (token >= 0)
        tokenStringCopy(json, parser.tokens[static_cast<std::size_t>(token)], slot.effect);
    token = objectValue(json, parser, object, "s");
    if (token >= 0 && tokenNumber(json, parser.tokens[static_cast<std::size_t>(token)], number))
        slot.effectSpeed = static_cast<float>(number);
    slot.breathing = std::strcmp(slot.effect.data(), "breath") == 0;
    slot.status = classifySlot(slot);
    result.events[result.eventCount++] =
        SlotStatusChanged{.index = index, .value = slot};
}

/* Merges one partial ambient/key light object into a light value. */
void updateLight(std::string_view json, const Parser &parser, int object,
                 Light &light) noexcept
{
    if (object < 0 || parser.tokens[static_cast<std::size_t>(object)].type != TokenType::Object)
        return;
    double number;
    int token = objectValue(json, parser, object, "c");
    if (token >= 0 && tokenNumber(json, parser.tokens[static_cast<std::size_t>(token)], number) &&
        number >= 0 && number <= 16777215 && std::floor(number) == number)
        light.color = static_cast<std::uint32_t>(number);
    token = objectValue(json, parser, object, "b");
    if (token >= 0 && tokenNumber(json, parser.tokens[static_cast<std::size_t>(token)], number)) {
        light.brightness = static_cast<float>(number);
        if (light.brightness < 0) light.brightness = 0;
        if (light.brightness > 1) light.brightness = 1;
    }
    token = objectValue(json, parser, object, "e");
    if (token >= 0)
        tokenStringCopy(json, parser.tokens[static_cast<std::size_t>(token)], light.effect);
    token = objectValue(json, parser, object, "s");
    if (token >= 0 && tokenNumber(json, parser.tokens[static_cast<std::size_t>(token)], number))
        light.effectSpeed = static_cast<float>(number);
}

/* Converts a full lighting configuration RPC payload into model events. */
void updateLightingConfig(std::string_view json, const Parser &parser, int params,
                          const RequestContext &context, RequestResult &result) noexcept
{
    if (result.eventCount >= MaxEvents) return;
    LightingConfigured configuration{};
    if (context.model != nullptr) {
        configuration.ambient = context.model->ambientLight;
        configuration.keys = context.model->keyLight;
    } else {
        std::strcpy(configuration.ambient.effect.data(), "off");
        std::strcpy(configuration.keys.effect.data(), "off");
    }
    updateLight(json, parser, objectValue(json, parser, params, "ambient"),
               configuration.ambient);
    updateLight(json, parser, objectValue(json, parser, params, "keys"),
               configuration.keys);
    result.events[result.eventCount++] = configuration;
}

}  // namespace

/* Infers a stable UI status from the desktop's raw lighting/effect fields. */
SlotStatus classifySlot(const Slot &slot) noexcept
{
    if (slot.brightness <= 0.01f || std::strcmp(slot.effect.data(), "off") == 0)
        return SlotStatus::Unassigned;
    const unsigned red = (slot.color >> 16) & 0xffU;
    const unsigned green = (slot.color >> 8) & 0xffU;
    const unsigned blue = slot.color & 0xffU;
    if (slot.breathing || std::strcmp(slot.effect.data(), "breath") == 0)
        return SlotStatus::Thinking;
    if (red >= 160U && green >= 70U && green < red && blue < green / 2U)
        return SlotStatus::RequiresInput;
    if (red >= 160U && red > green * 3U / 2U && red > blue * 3U / 2U)
        return SlotStatus::Error;
    if (green >= 120U && green > red * 5U / 4U && green > blue * 5U / 4U)
        return SlotStatus::Complete;
    if (red >= 170U && green >= 170U && blue >= 170U) return SlotStatus::Idle;
    return SlotStatus::Unknown;
}

/* Dispatches supported RPC methods and always forms a correlated JSON reply. */
Result handleRequest(std::string_view json, const RequestContext &context,
                     RequestResult &result) noexcept
{
    result = RequestResult{};
    if (json.empty()) return Result::InvalidArgument;
    Parser parser;
    if (!tokenize(json, parser)) return Result::InvalidReport;
    const int method = objectValue(json, parser, 0, "method");
    const int params = objectValue(json, parser, 0, "params");
    std::array<char, 80> id{};
    if (copyId(json, parser, id) == 0) return Result::NoSpace;

    int written;
    if (methodIs(json, parser, method, "sys.version")) {
        written = std::snprintf(result.response.data(), result.response.size(),
                                "{\"id\":%s,\"result\":{\"version\":\"%s\"}}",
                                id.data(), FirmwareVersion);
    } else if (methodIs(json, parser, method, "device.status")) {
        if (context.batteryKnown) {
            written = std::snprintf(result.response.data(), result.response.size(),
                "{\"id\":%s,\"result\":{\"version\":\"%s\","
                "\"profile_index\":0,\"layer_index\":1,\"battery\":%u,"
                "\"is_charging\":%s}}", id.data(), FirmwareVersion,
                static_cast<unsigned>(context.batteryPercent),
                context.charging ? "true" : "false");
        } else {
            written = std::snprintf(result.response.data(), result.response.size(),
                "{\"id\":%s,\"result\":{\"version\":\"%s\","
                "\"profile_index\":0,\"layer_index\":1,\"battery\":null,"
                "\"is_charging\":false}}", id.data(), FirmwareVersion);
        }
    } else if (methodIs(json, parser, method, "v.oai.thstatus") && params >= 0 &&
               parser.tokens[static_cast<std::size_t>(params)].type == TokenType::Array) {
        for (std::size_t i = static_cast<std::size_t>(params) + 1; i < parser.count; ++i) {
            if (parser.tokens[i].parent == params &&
                parser.tokens[i].type == TokenType::Object)
                updateSlot(json, parser, static_cast<int>(i), context, result);
        }
        written = std::snprintf(result.response.data(), result.response.size(),
                                "{\"id\":%s,\"result\":{\"ok\":true}}", id.data());
    } else if (methodIs(json, parser, method, "v.oai.rgbcfg") && params >= 0 &&
               parser.tokens[static_cast<std::size_t>(params)].type == TokenType::Object) {
        updateLightingConfig(json, parser, params, context, result);
        written = std::snprintf(result.response.data(), result.response.size(),
                                "{\"id\":%s,\"result\":{\"ok\":true}}", id.data());
    } else if (methodIs(json, parser, method, "lights.preview") ||
               methodIs(json, parser, method, "host.focused_app")) {
        written = std::snprintf(result.response.data(), result.response.size(),
                                "{\"id\":%s,\"result\":{\"ok\":true}}", id.data());
    } else {
        written = std::snprintf(result.response.data(), result.response.size(),
                                "{\"id\":%s,\"error\":{\"code\":-32601,"
                                "\"message\":\"Method not found\"}}", id.data());
    }
    if (written < 0 || static_cast<std::size_t>(written) >= result.response.size()) {
        result = RequestResult{};
        return Result::NoSpace;
    }
    result.responseLength = static_cast<std::size_t>(written);
    return Result::Ok;
}

}  // namespace buddy::codex
