#include "codex_rpc.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>

#include "fixed_json.hpp"

namespace buddy::codex {
namespace {

constexpr std::size_t TokenCount = 160;
using Parser = fixed_json::Parser<TokenCount>;
using fixed_json::Token;
using fixed_json::TokenType;
using fixed_json::objectValue;
using fixed_json::tokenEquals;
using fixed_json::tokenize;
using fixed_json::tokenNumber;
using fixed_json::tokenStringCopy;

/* Merges the shared color/brightness/effect/effectSpeed fields present on both
   Slot and Light from a partial desktop object into an existing light value. */
template <typename LightLike>
void applyLightFields(std::string_view json, const Parser &parser, int object,
                      LightLike &light) noexcept
{
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
    applyLightFields(json, parser, object, slot);
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
    applyLightFields(json, parser, object, light);
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
