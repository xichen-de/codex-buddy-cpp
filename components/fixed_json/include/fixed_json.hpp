#pragma once

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
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
