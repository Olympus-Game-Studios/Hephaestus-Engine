#include "Json.h"

#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <sstream>

namespace {
void AppendUtf8(std::string& out, unsigned int codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}
}

JsonParser::JsonParser(const std::string& source)
    : m_Begin(source.data()), m_Cursor(source.data()), m_End(source.data() + source.size()) {
}

bool JsonParser::Parse(JsonValue& outValue, std::string& outError) {
    outError.clear();
    SkipWhitespace();
    if (!ParseValue(outValue)) {
        outError = m_Error.empty() ? std::string("Malformed json.") : m_Error;
        return false;
    }
    SkipWhitespace();
    if (m_Cursor != m_End) {
        outError = "Unexpected trailing characters in json.";
        return false;
    }
    return true;
}

void JsonParser::SkipWhitespace() {
    while (m_Cursor < m_End && std::isspace(static_cast<unsigned char>(*m_Cursor))) {
        ++m_Cursor;
    }
}

bool JsonParser::SetError(const std::string& error) {
    if (m_Error.empty()) {
        m_Error = error;
    }
    return false;
}

bool JsonParser::ParseValue(JsonValue& value) {
    SkipWhitespace();
    if (m_Cursor == m_End) {
        return SetError("Unexpected end of input.");
    }

    char ch = *m_Cursor;
    switch (ch) {
        case '"': return ParseString(value);
        case '{': return ParseObject(value);
        case '[': return ParseArray(value);
        case 't': return ParseLiteral("true", JsonValue::Type::Bool, value, true);
        case 'f': return ParseLiteral("false", JsonValue::Type::Bool, value, false);
        case 'n': return ParseLiteral("null", JsonValue::Type::Null, value, false);
        default:
            if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
                return ParseNumber(value);
            }
            break;
    }

    return SetError("Unexpected token while parsing json.");
}

bool JsonParser::ParseLiteral(const char* literal, JsonValue::Type type, JsonValue& value, bool boolValue) {
    const char* cursor = m_Cursor;
    while (*literal) {
        if (cursor >= m_End || *cursor != *literal) {
            return SetError("Unexpected token while parsing json.");
        }
        ++cursor;
        ++literal;
    }
    m_Cursor = cursor;
    value.type = type;
    value.boolValue = boolValue;
    return true;
}

bool JsonParser::ParseString(JsonValue& value) {
    std::string result;
    if (!ParseStringInternal(result)) {
        return false;
    }
    value.type = JsonValue::Type::String;
    value.stringValue = std::move(result);
    return true;
}

bool JsonParser::ParseStringInternal(std::string& out) {
    if (*m_Cursor != '"') {
        return SetError("Expected '""' at beginning of string.");
    }
    ++m_Cursor;
    out.clear();

    while (m_Cursor != m_End) {
        char ch = *m_Cursor++;
        if (ch == '"') {
            return true;
        }
        if (ch == '\\') {
            if (m_Cursor == m_End) {
                return SetError("Unexpected end of input in string.");
            }
            char escape = *m_Cursor++;
            switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (m_End - m_Cursor < 4) {
                        return SetError("Invalid unicode escape sequence.");
                    }
                    unsigned int codepoint = 0;
                    for (int i = 0; i < 4; ++i) {
                        char hex = *m_Cursor++;
                        codepoint <<= 4;
                        if (hex >= '0' && hex <= '9') codepoint += static_cast<unsigned int>(hex - '0');
                        else if (hex >= 'a' && hex <= 'f') codepoint += static_cast<unsigned int>(10 + hex - 'a');
                        else if (hex >= 'A' && hex <= 'F') codepoint += static_cast<unsigned int>(10 + hex - 'A');
                        else return SetError("Invalid unicode escape sequence.");
                    }
                    AppendUtf8(out, codepoint);
                    break;
                }
                default:
                    return SetError("Invalid escape sequence in string.");
            }
        } else {
            out.push_back(ch);
        }
    }
    return SetError("Unexpected end of input in string.");
}

bool JsonParser::ParseNumber(JsonValue& value) {
    char* end = nullptr;
    std::string temp(m_Cursor, m_End);
    const char* start = temp.c_str();
    double number = std::strtod(start, &end);
    if (end == start) {
        return SetError("Invalid number format.");
    }
    size_t advance = static_cast<size_t>(end - start);
    m_Cursor += advance;
    value.type = JsonValue::Type::Number;
    value.numberValue = number;
    return true;
}

bool JsonParser::ParseArray(JsonValue& value) {
    if (*m_Cursor != '[') {
        return SetError("Expected '[' at beginning of array.");
    }
    ++m_Cursor;
    value.type = JsonValue::Type::Array;
    value.arrayValue.clear();

    SkipWhitespace();
    if (m_Cursor < m_End && *m_Cursor == ']') {
        ++m_Cursor;
        return true;
    }

    while (m_Cursor < m_End) {
        JsonValue element;
        if (!ParseValue(element)) {
            return false;
        }
        value.arrayValue.push_back(std::move(element));
        SkipWhitespace();
        if (m_Cursor < m_End && *m_Cursor == ',') {
            ++m_Cursor;
            SkipWhitespace();
            continue;
        }
        if (m_Cursor < m_End && *m_Cursor == ']') {
            ++m_Cursor;
            return true;
        }
        return SetError("Expected ',' or ']' in array.");
    }
    return SetError("Unexpected end of input in array.");
}

bool JsonParser::ParseObject(JsonValue& value) {
    if (*m_Cursor != '{') {
        return SetError("Expected '{' at beginning of object.");
    }
    ++m_Cursor;
    value.type = JsonValue::Type::Object;
    value.objectValue.clear();

    SkipWhitespace();
    if (m_Cursor < m_End && *m_Cursor == '}') {
        ++m_Cursor;
        return true;
    }

    while (m_Cursor < m_End) {
        SkipWhitespace();
        std::string key;
        if (!ParseStringInternal(key)) {
            return false;
        }
        SkipWhitespace();
        if (m_Cursor >= m_End || *m_Cursor != ':') {
            return SetError("Expected ':' after object key.");
        }
        ++m_Cursor;
        SkipWhitespace();
        JsonValue member;
        if (!ParseValue(member)) {
            return false;
        }
        value.objectValue.emplace(std::move(key), std::move(member));
        SkipWhitespace();
        if (m_Cursor < m_End && *m_Cursor == ',') {
            ++m_Cursor;
            SkipWhitespace();
            continue;
        }
        if (m_Cursor < m_End && *m_Cursor == '}') {
            ++m_Cursor;
            return true;
        }
        return SetError("Expected ',' or '}' in object.");
    }
    return SetError("Unexpected end of input in object.");
}
