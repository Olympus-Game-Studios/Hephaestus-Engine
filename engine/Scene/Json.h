#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::unordered_map<std::string, JsonValue> objectValue;

    bool IsNull() const { return type == Type::Null; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    bool AsBool(bool fallback = false) const { return IsBool() ? boolValue : fallback; }
    double AsNumber(double fallback = 0.0) const { return IsNumber() ? numberValue : fallback; }
    const std::string& AsString() const { return stringValue; }
    const std::vector<JsonValue>& AsArray() const { return arrayValue; }
    const std::unordered_map<std::string, JsonValue>& AsObject() const { return objectValue; }

    const JsonValue* Find(const std::string& key) const {
        if (!IsObject()) {
            return nullptr;
        }
        auto it = objectValue.find(key);
        return it != objectValue.end() ? &it->second : nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& source);

    bool Parse(JsonValue& outValue, std::string& outError);

private:
    const char* m_Begin;
    const char* m_Cursor;
    const char* m_End;
    std::string m_Error;

    bool ParseValue(JsonValue& value);
    bool ParseString(JsonValue& value);
    bool ParseObject(JsonValue& value);
    bool ParseArray(JsonValue& value);
    bool ParseNumber(JsonValue& value);
    bool ParseLiteral(const char* literal, JsonValue::Type type, JsonValue& value, bool boolValue);
    bool ParseStringInternal(std::string& out);
    void SkipWhitespace();
    bool SetError(const std::string& error);
};
