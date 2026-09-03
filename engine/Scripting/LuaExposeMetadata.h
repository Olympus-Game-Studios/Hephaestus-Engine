#pragma once

#include "../Core/PropertyValue.h"

#include <cstddef>
#include <string>
#include <vector>

enum class LuaExposeType {
    Bool,
    Integer,
    Number,
    String,
    Vec2,
    Vec3,
    Color
};

struct LuaExposedField {
    std::string name;
    LuaExposeType type = LuaExposeType::Number;
    PropertyValue defaultValue;
    std::size_t sourceLine = 0;
    std::size_t sourceOrder = 0;
};

struct LuaExposeDiagnostic {
    std::size_t sourceLine = 0;
    std::string message;
    bool conflict = false;
};

struct LuaExposeScanResult {
    std::vector<LuaExposedField> fields;
    std::vector<LuaExposeDiagnostic> diagnostics;
    bool valid = true;
    std::string contentHash;
};
