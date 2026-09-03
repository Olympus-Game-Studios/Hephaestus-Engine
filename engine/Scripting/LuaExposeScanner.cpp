#include "LuaExposeScanner.h"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

enum class TokenKind {
    Identifier,
    String,
    Number,
    Dot,
    Comma,
    LeftParen,
    RightParen,
    Minus,
    Plus,
    Other
};

struct Token {
    TokenKind kind = TokenKind::Other;
    std::string text;
    std::size_t line = 1;
};

bool IsIdentifierStart(char value)
{
    return std::isalpha(static_cast<unsigned char>(value)) != 0 || value == '_';
}

bool IsIdentifierContinue(char value)
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

std::optional<std::pair<std::size_t, std::size_t>> LongBracketOpen(
    std::string_view source,
    std::size_t offset)
{
    if (offset >= source.size() || source[offset] != '[') {
        return std::nullopt;
    }
    std::size_t cursor = offset + 1;
    while (cursor < source.size() && source[cursor] == '=') {
        ++cursor;
    }
    if (cursor >= source.size() || source[cursor] != '[') {
        return std::nullopt;
    }
    return std::pair<std::size_t, std::size_t>{cursor - offset - 1, cursor + 1};
}

std::size_t SkipLongBracket(
    std::string_view source,
    std::size_t contentOffset,
    std::size_t equalsCount,
    std::size_t& line)
{
    std::size_t cursor = contentOffset;
    while (cursor < source.size()) {
        if (source[cursor] == '\n') {
            ++line;
        }
        if (source[cursor] == ']') {
            std::size_t close = cursor + 1;
            std::size_t equalsSeen = 0;
            while (close < source.size() && source[close] == '=') {
                ++equalsSeen;
                ++close;
            }
            if (equalsSeen == equalsCount && close < source.size() && source[close] == ']') {
                return close + 1;
            }
        }
        ++cursor;
    }
    return cursor;
}

std::string DecodeQuotedString(std::string_view source, std::size_t& cursor, std::size_t& line)
{
    const char quote = source[cursor++];
    std::string value;
    while (cursor < source.size()) {
        const char current = source[cursor++];
        if (current == quote) {
            break;
        }
        if (current == '\n') {
            ++line;
        }
        if (current != '\\' || cursor >= source.size()) {
            value.push_back(current);
            continue;
        }
        const char escaped = source[cursor++];
        switch (escaped) {
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case '\\': value.push_back('\\'); break;
            case '"': value.push_back('"'); break;
            case '\'': value.push_back('\''); break;
            case '\n': ++line; break;
            default: value.push_back(escaped); break;
        }
    }
    return value;
}

std::vector<Token> Tokenize(std::string_view source)
{
    std::vector<Token> tokens;
    std::size_t cursor = 0;
    std::size_t line = 1;

    while (cursor < source.size()) {
        const char current = source[cursor];
        if (std::isspace(static_cast<unsigned char>(current)) != 0) {
            if (current == '\n') {
                ++line;
            }
            ++cursor;
            continue;
        }

        if (current == '-' && cursor + 1 < source.size() && source[cursor + 1] == '-') {
            cursor += 2;
            if (const auto bracket = LongBracketOpen(source, cursor)) {
                cursor = SkipLongBracket(source, bracket->second, bracket->first, line);
            } else {
                while (cursor < source.size() && source[cursor] != '\n') {
                    ++cursor;
                }
            }
            continue;
        }

        if (current == '\'' || current == '"') {
            const std::size_t tokenLine = line;
            tokens.push_back({TokenKind::String, DecodeQuotedString(source, cursor, line), tokenLine});
            continue;
        }

        if (const auto bracket = LongBracketOpen(source, cursor)) {
            const std::size_t tokenLine = line;
            const std::size_t contentStart = bracket->second;
            const std::size_t end = SkipLongBracket(source, contentStart, bracket->first, line);
            const std::size_t closeLength = bracket->first + 2;
            const std::size_t contentEnd = end >= closeLength ? end - closeLength : contentStart;
            tokens.push_back({
                TokenKind::String,
                std::string(source.substr(contentStart, contentEnd - contentStart)),
                tokenLine
            });
            cursor = end;
            continue;
        }

        if (IsIdentifierStart(current)) {
            const std::size_t start = cursor++;
            while (cursor < source.size() && IsIdentifierContinue(source[cursor])) {
                ++cursor;
            }
            tokens.push_back({
                TokenKind::Identifier,
                std::string(source.substr(start, cursor - start)),
                line
            });
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(current)) != 0 ||
            (current == '.' && cursor + 1 < source.size() &&
             std::isdigit(static_cast<unsigned char>(source[cursor + 1])) != 0)) {
            const std::size_t start = cursor++;
            while (cursor < source.size()) {
                const char value = source[cursor];
                if (std::isalnum(static_cast<unsigned char>(value)) == 0 &&
                    value != '.' && value != '+' && value != '-') {
                    break;
                }
                ++cursor;
            }
            tokens.push_back({
                TokenKind::Number,
                std::string(source.substr(start, cursor - start)),
                line
            });
            continue;
        }

        TokenKind kind = TokenKind::Other;
        switch (current) {
            case '.': kind = TokenKind::Dot; break;
            case ',': kind = TokenKind::Comma; break;
            case '(': kind = TokenKind::LeftParen; break;
            case ')': kind = TokenKind::RightParen; break;
            case '-': kind = TokenKind::Minus; break;
            case '+': kind = TokenKind::Plus; break;
            default: break;
        }
        tokens.push_back({kind, std::string(1, current), line});
        ++cursor;
    }
    return tokens;
}

struct Literal {
    enum class Kind { Bool, Number, String } kind = Kind::String;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
};

std::optional<Literal> ParseLiteral(const std::vector<Token>& argument)
{
    if (argument.size() == 1) {
        const Token& token = argument.front();
        if (token.kind == TokenKind::String) {
            Literal literal;
            literal.kind = Literal::Kind::String;
            literal.stringValue = token.text;
            return literal;
        }
        if (token.kind == TokenKind::Identifier && (token.text == "true" || token.text == "false")) {
            Literal literal;
            literal.kind = Literal::Kind::Bool;
            literal.boolValue = token.text == "true";
            return literal;
        }
        if (token.kind == TokenKind::Number) {
            char* end = nullptr;
            const double value = std::strtod(token.text.c_str(), &end);
            if (end == token.text.c_str() + token.text.size()) {
                Literal literal;
                literal.kind = Literal::Kind::Number;
                literal.numberValue = value;
                return literal;
            }
        }
    }

    if (argument.size() == 2 &&
        (argument[0].kind == TokenKind::Minus || argument[0].kind == TokenKind::Plus) &&
        argument[1].kind == TokenKind::Number) {
        char* end = nullptr;
        double value = std::strtod(argument[1].text.c_str(), &end);
        if (end == argument[1].text.c_str() + argument[1].text.size()) {
            if (argument[0].kind == TokenKind::Minus) {
                value = -value;
            }
            Literal literal;
            literal.kind = Literal::Kind::Number;
            literal.numberValue = value;
            return literal;
        }
    }
    return std::nullopt;
}

std::optional<LuaExposeType> ParseExposeType(std::string_view name)
{
    if (name == "bool") return LuaExposeType::Bool;
    if (name == "integer") return LuaExposeType::Integer;
    if (name == "number") return LuaExposeType::Number;
    if (name == "string") return LuaExposeType::String;
    if (name == "vec2") return LuaExposeType::Vec2;
    if (name == "vec3") return LuaExposeType::Vec3;
    if (name == "color") return LuaExposeType::Color;
    return std::nullopt;
}

std::size_t ExpectedArgumentCount(LuaExposeType type)
{
    switch (type) {
        case LuaExposeType::Bool:
        case LuaExposeType::Integer:
        case LuaExposeType::Number:
        case LuaExposeType::String:
            return 2;
        case LuaExposeType::Vec2:
            return 3;
        case LuaExposeType::Vec3:
            return 4;
        case LuaExposeType::Color:
            return 5;
    }
    return 0;
}

bool BuildField(
    LuaExposeType type,
    const std::vector<std::vector<Token>>& arguments,
    std::size_t line,
    std::size_t order,
    LuaExposedField& outField,
    std::string& outError)
{
    if (arguments.size() != ExpectedArgumentCount(type)) {
        outError = "Expose declaration has the wrong number of arguments.";
        return false;
    }

    const auto name = ParseLiteral(arguments[0]);
    if (!name || name->kind != Literal::Kind::String || name->stringValue.empty()) {
        outError = "Expose declaration name must be a non-empty string literal.";
        return false;
    }

    std::vector<Literal> defaults;
    defaults.reserve(arguments.size() - 1);
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const auto literal = ParseLiteral(arguments[index]);
        if (!literal) {
            outError = "Expose defaults must be literals.";
            return false;
        }
        defaults.push_back(*literal);
    }

    outField.name = name->stringValue;
    outField.type = type;
    outField.sourceLine = line;
    outField.sourceOrder = order;

    switch (type) {
        case LuaExposeType::Bool:
            if (defaults[0].kind != Literal::Kind::Bool) {
                outError = "Expose.bool default must be a boolean literal.";
                return false;
            }
            outField.defaultValue = PropertyValue(defaults[0].boolValue);
            return true;
        case LuaExposeType::String:
            if (defaults[0].kind != Literal::Kind::String) {
                outError = "Expose.string default must be a string literal.";
                return false;
            }
            outField.defaultValue = PropertyValue(defaults[0].stringValue);
            return true;
        case LuaExposeType::Integer:
        case LuaExposeType::Number:
            if (defaults[0].kind != Literal::Kind::Number) {
                outError = "Expose numeric default must be a number literal.";
                return false;
            }
            if (type == LuaExposeType::Integer &&
                defaults[0].numberValue != static_cast<double>(static_cast<long long>(defaults[0].numberValue))) {
                outError = "Expose.integer default must be a whole number.";
                return false;
            }
            outField.defaultValue = PropertyValue(defaults[0].numberValue);
            return true;
        case LuaExposeType::Vec2:
        case LuaExposeType::Vec3:
        case LuaExposeType::Color:
            for (const Literal& value : defaults) {
                if (value.kind != Literal::Kind::Number) {
                    outError = "Expose vector and color defaults must be number literals.";
                    return false;
                }
            }
            if (type == LuaExposeType::Vec2) {
                outField.defaultValue = PropertyValue(Vec2(
                    static_cast<float>(defaults[0].numberValue),
                    static_cast<float>(defaults[1].numberValue)));
            } else if (type == LuaExposeType::Vec3) {
                outField.defaultValue = PropertyValue(Vec3(
                    static_cast<float>(defaults[0].numberValue),
                    static_cast<float>(defaults[1].numberValue),
                    static_cast<float>(defaults[2].numberValue)));
            } else {
                outField.defaultValue = PropertyValue(Vec4(
                    static_cast<float>(defaults[0].numberValue),
                    static_cast<float>(defaults[1].numberValue),
                    static_cast<float>(defaults[2].numberValue),
                    static_cast<float>(defaults[3].numberValue)));
            }
            return true;
    }
    return false;
}

} // namespace

LuaExposeScanResult LuaExposeScanner::Scan(std::string_view source)
{
    LuaExposeScanResult result;
    result.contentHash = ComputeContentHash(source);
    const std::vector<Token> tokens = Tokenize(source);
    std::unordered_map<std::string, std::size_t> declaredLines;

    for (std::size_t index = 0; index + 3 < tokens.size(); ++index) {
        if (tokens[index].kind != TokenKind::Identifier || tokens[index].text != "Expose" ||
            tokens[index + 1].kind != TokenKind::Dot ||
            tokens[index + 2].kind != TokenKind::Identifier ||
            tokens[index + 3].kind != TokenKind::LeftParen) {
            continue;
        }

        const std::size_t declarationLine = tokens[index].line;
        const auto type = ParseExposeType(tokens[index + 2].text);
        if (!type) {
            result.diagnostics.push_back({
                declarationLine,
                "Unknown Expose declaration type '" + tokens[index + 2].text + "'.",
                false
            });
        }

        std::vector<std::vector<Token>> arguments(1);
        int depth = 1;
        std::size_t cursor = index + 4;
        bool closed = false;
        for (; cursor < tokens.size(); ++cursor) {
            const Token& token = tokens[cursor];
            if (token.kind == TokenKind::LeftParen) {
                ++depth;
                arguments.back().push_back(token);
            } else if (token.kind == TokenKind::RightParen) {
                --depth;
                if (depth == 0) {
                    closed = true;
                    break;
                }
                arguments.back().push_back(token);
            } else if (token.kind == TokenKind::Comma && depth == 1) {
                arguments.emplace_back();
            } else {
                arguments.back().push_back(token);
            }
        }

        if (!closed) {
            result.diagnostics.push_back({
                declarationLine,
                "Expose declaration is missing a closing parenthesis.",
                false
            });
            break;
        }

        if (!arguments.empty() && arguments.back().empty()) {
            arguments.pop_back();
        }
        if (arguments.size() == 1 && arguments.front().empty()) {
            arguments.clear();
        }

        if (type) {
            LuaExposedField field;
            std::string error;
            if (!BuildField(*type, arguments, declarationLine, result.fields.size(), field, error)) {
                result.diagnostics.push_back({declarationLine, std::move(error), false});
            } else {
                const auto [existing, inserted] = declaredLines.emplace(field.name, declarationLine);
                if (!inserted) {
                    result.valid = false;
                    result.diagnostics.push_back({
                        declarationLine,
                        "Conflicting Expose declaration '" + field.name + "'; first declared on line " +
                            std::to_string(existing->second) + ".",
                        true
                    });
                } else {
                    result.fields.push_back(std::move(field));
                }
            }
        }
        index = cursor;
    }

    if (!result.valid) {
        result.fields.clear();
    }
    return result;
}

std::string LuaExposeScanner::ComputeContentHash(std::string_view source)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char value : source) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}
