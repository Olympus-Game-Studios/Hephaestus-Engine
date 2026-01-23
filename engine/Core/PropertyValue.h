#pragma once

#include "Math.h"

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct PropertyValue {
    using Array = std::vector<PropertyValue>;
    using Object = std::unordered_map<std::string, PropertyValue>;
    using Variant = std::variant<std::monostate, bool, double, std::string, Vec2, Vec3, Vec4, Array, Object>;

    Variant value;

    PropertyValue() = default;
    PropertyValue(bool v) : value(v) {}
    PropertyValue(double v) : value(v) {}
    PropertyValue(std::string v) : value(std::move(v)) {}
    PropertyValue(const char* v) : value(std::string(v)) {}
    PropertyValue(const Vec2& v) : value(v) {}
    PropertyValue(const Vec3& v) : value(v) {}
    PropertyValue(const Vec4& v) : value(v) {}
    PropertyValue(Array v) : value(std::move(v)) {}
    PropertyValue(Object v) : value(std::move(v)) {}

    bool IsNull() const { return std::holds_alternative<std::monostate>(value); }
    bool IsBool() const { return std::holds_alternative<bool>(value); }
    bool IsNumber() const { return std::holds_alternative<double>(value); }
    bool IsString() const { return std::holds_alternative<std::string>(value); }
    bool IsVec2() const { return std::holds_alternative<Vec2>(value); }
    bool IsVec3() const { return std::holds_alternative<Vec3>(value); }
    bool IsVec4() const { return std::holds_alternative<Vec4>(value); }
    bool IsArray() const { return std::holds_alternative<Array>(value); }
    bool IsObject() const { return std::holds_alternative<Object>(value); }

    const bool* AsBool() const { return std::get_if<bool>(&value); }
    const double* AsNumber() const { return std::get_if<double>(&value); }
    const std::string* AsString() const { return std::get_if<std::string>(&value); }
    const Vec2* AsVec2() const { return std::get_if<Vec2>(&value); }
    const Vec3* AsVec3() const { return std::get_if<Vec3>(&value); }
    const Vec4* AsVec4() const { return std::get_if<Vec4>(&value); }
    const Array* AsArray() const { return std::get_if<Array>(&value); }
    const Object* AsObject() const { return std::get_if<Object>(&value); }

    Array* AsArray() { return std::get_if<Array>(&value); }
    Object* AsObject() { return std::get_if<Object>(&value); }
};
