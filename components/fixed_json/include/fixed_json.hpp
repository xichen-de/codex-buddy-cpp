#pragma once

#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

namespace buddy::fixed_json {

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

template <std::size_t Capacity>
struct Parser {
    std::array<Token, Capacity> tokens{};
    std::size_t count{};
    int parent{-1};
};

namespace detail {

template <std::size_t Capacity>
int newToken(Parser<Capacity> &parser, TokenType type, int start) noexcept
{
    if (parser.count >= Capacity) return -1;
    const int index = static_cast<int>(parser.count++);
    parser.tokens[static_cast<std::size_t>(index)] =
        Token{.type = type, .start = start, .end = -1, .parent = parser.parent};
    return index;
}

template <std::size_t Capacity>
bool parseString(std::string_view json, std::size_t &position,
                 Parser<Capacity> &parser) noexcept
{
    const int index = newToken(parser, TokenType::String,
                               static_cast<int>(position) + 1);
    if (index < 0) return false;
    for (++position; position < json.size(); ++position) {
        const auto byte = static_cast<unsigned char>(json[position]);
        if (byte == '"') {
            parser.tokens[static_cast<std::size_t>(index)].end =
                static_cast<int>(position);
            return true;
        }
        if (byte < 0x20U) return false;
        if (byte == '\\') {
            if (++position >= json.size() ||
                std::string_view{"\"\\/bfnrtu"}.find(json[position]) ==
                    std::string_view::npos)
                return false;
            if (json[position] == 'u') {
                for (unsigned digit = 0; digit < 4; ++digit) {
                    if (++position >= json.size() ||
                        !std::isxdigit(
                            static_cast<unsigned char>(json[position])))
                        return false;
                }
            }
        }
    }
    return false;
}

template <std::size_t Capacity>
bool parsePrimitive(std::string_view json, std::size_t &position,
                    Parser<Capacity> &parser) noexcept
{
    const int index = newToken(parser, TokenType::Primitive,
                               static_cast<int>(position));
    if (index < 0) return false;
    while (position < json.size() &&
           std::string_view{" \t\r\n,]}"}.find(json[position]) ==
               std::string_view::npos) {
        const auto byte = static_cast<unsigned char>(json[position]);
        if (byte < 0x20U || byte >= 0x7fU ||
            std::string_view{":{[\""}.find(static_cast<char>(byte)) !=
                std::string_view::npos)
            return false;
        ++position;
    }
    Token &token = parser.tokens[static_cast<std::size_t>(index)];
    if (token.start == static_cast<int>(position)) return false;
    token.end = static_cast<int>(position);
    --position;
    return true;
}

}  // namespace detail

template <std::size_t Capacity>
bool tokenize(std::string_view json, Parser<Capacity> &parser) noexcept
{
    parser = Parser<Capacity>{};
    for (std::size_t position = 0; position < json.size(); ++position) {
        const char byte = json[position];
        if (byte == '{' || byte == '[') {
            const int index = detail::newToken(
                parser, byte == '{' ? TokenType::Object : TokenType::Array,
                static_cast<int>(position));
            if (index < 0) return false;
            parser.parent = index;
        } else if (byte == '}' || byte == ']') {
            const TokenType expected =
                byte == '}' ? TokenType::Object : TokenType::Array;
            const int open = parser.parent;
            if (open < 0 ||
                parser.tokens[static_cast<std::size_t>(open)].type != expected)
                return false;
            parser.tokens[static_cast<std::size_t>(open)].end =
                static_cast<int>(position) + 1;
            parser.parent =
                parser.tokens[static_cast<std::size_t>(open)].parent;
        } else if (byte == '"') {
            if (!detail::parseString(json, position, parser)) return false;
        } else if (byte == ' ' || byte == '\t' || byte == '\r' ||
                   byte == '\n' || byte == ':' || byte == ',') {
            continue;
        } else if (!detail::parsePrimitive(json, position, parser)) {
            return false;
        }
    }
    if (parser.parent != -1 || parser.count == 0 ||
        parser.tokens[0].type != TokenType::Object)
        return false;
    for (std::size_t index = 0; index < parser.count; ++index)
        if (parser.tokens[index].end < 0) return false;
    return true;
}

inline bool tokenEquals(std::string_view json, const Token &token,
                        std::string_view text) noexcept
{
    const auto length = static_cast<std::size_t>(token.end - token.start);
    return token.type == TokenType::String && text.size() == length &&
           json.substr(static_cast<std::size_t>(token.start), length) == text;
}

/* Copies a Primitive token's raw text into a bounded stack buffer, ready for
   strtoX-style parsing. Shared by the unsigned/signed/floating-point token
   parsers below. */
template <std::size_t BufferSize>
bool copyPrimitiveToken(std::string_view json, const Token &token,
                        std::array<char, BufferSize> &buffer,
                        std::size_t &length) noexcept
{
    if (token.type != TokenType::Primitive) return false;
    length = static_cast<std::size_t>(token.end - token.start);
    if (length == 0 || length >= BufferSize) return false;
    json.copy(buffer.data(), length, static_cast<std::size_t>(token.start));
    buffer[length] = '\0';
    return true;
}

/* Parses an unsigned integer token with full-token and overflow validation. */
inline bool tokenU64(std::string_view json, const Token &token,
                     std::uint64_t &value) noexcept
{
    std::array<char, 32> buffer{};
    std::size_t length = 0;
    if (!copyPrimitiveToken(json, token, buffer, length) || buffer[0] == '-')
        return false;
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(buffer.data(), &end, 10);
    if (end != buffer.data() + length) return false;
    value = static_cast<std::uint64_t>(parsed);
    return true;
}

/* Parses a signed integer token with full-token validation. */
inline bool tokenI64(std::string_view json, const Token &token,
                     std::int64_t &value) noexcept
{
    std::array<char, 32> buffer{};
    std::size_t length = 0;
    if (!copyPrimitiveToken(json, token, buffer, length)) return false;
    char *end = nullptr;
    const long long parsed = std::strtoll(buffer.data(), &end, 10);
    if (end != buffer.data() + length) return false;
    value = static_cast<std::int64_t>(parsed);
    return true;
}

/* Parses a finite floating-point token with full-token validation. */
inline bool tokenNumber(std::string_view json, const Token &token,
                        double &value) noexcept
{
    std::array<char, 40> buffer{};
    std::size_t length = 0;
    if (!copyPrimitiveToken(json, token, buffer, length)) return false;
    char *end = nullptr;
    value = std::strtod(buffer.data(), &end);
    return end == buffer.data() + length && std::isfinite(value);
}

/* Copies and minimally unescapes a JSON string token into a bounded buffer,
   replacing unsupported \u escapes with '?'. */
inline void tokenStringCopy(std::string_view json, const Token &token,
                            std::span<char> destination) noexcept
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
                if (written + 1 < destination.size()) destination[written++] = '?';
                continue;
            default: continue;
        }
        destination[written++] = byte;
    }
    destination[written] = '\0';
}

template <std::size_t Capacity>
int objectValue(std::string_view json, const Parser<Capacity> &parser,
                int object, std::string_view key) noexcept
{
    if (object < 0 ||
        parser.tokens[static_cast<std::size_t>(object)].type != TokenType::Object)
        return -1;
    int keyToken = -1;
    for (std::size_t index = static_cast<std::size_t>(object) + 1;
         index < parser.count; ++index) {
        if (parser.tokens[index].parent != object) continue;
        if (keyToken < 0) {
            if (parser.tokens[index].type != TokenType::String) return -1;
            keyToken = static_cast<int>(index);
            continue;
        }
        if (tokenEquals(json,
                        parser.tokens[static_cast<std::size_t>(keyToken)], key))
            return static_cast<int>(index);
        keyToken = -1;
    }
    return -1;
}

}  // namespace buddy::fixed_json
