#pragma once

#include "Math.h"

#include <cmath>
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

    bool Equals(const PropertyValue& other, float epsilon = 0.0001f) const {
        if (value.index() != other.value.index()) {
            if (IsNumber() && other.IsNumber()) {
                return std::abs(*AsNumber() - *other.AsNumber()) <= static_cast<double>(epsilon);
            }
            return false;
        }
        if (IsNull()) {
            return true;
        }
        if (IsBool()) {
            return *AsBool() == *other.AsBool();
        }
        if (IsNumber()) {
            return std::abs(*AsNumber() - *other.AsNumber()) <= static_cast<double>(epsilon);
        }
        if (IsString()) {
            return *AsString() == *other.AsString();
        }
        if (IsVec2()) {
            const Vec2& a = *AsVec2();
            const Vec2& b = *other.AsVec2();
            return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon;
        }
        if (IsVec3()) {
            const Vec3& a = *AsVec3();
            const Vec3& b = *other.AsVec3();
            return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon && std::abs(a.z - b.z) <= epsilon;
        }
        if (IsVec4()) {
            const Vec4& a = *AsVec4();
            const Vec4& b = *other.AsVec4();
            return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon &&
                   std::abs(a.z - b.z) <= epsilon && std::abs(a.w - b.w) <= epsilon;
        }
        if (IsArray()) {
            const Array& a = *AsArray();
            const Array& b = *other.AsArray();
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                if (!a[i].Equals(b[i], epsilon)) {
                    return false;
                }
            }
            return true;
        }
        if (IsObject()) {
            const Object& a = *AsObject();
            const Object& b = *other.AsObject();
            if (a.size() != b.size()) {
                return false;
            }
            for (const auto& [key, entry] : a) {
                auto it = b.find(key);
                if (it == b.end() || !entry.Equals(it->second, epsilon)) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
};
