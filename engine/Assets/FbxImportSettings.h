/**
 * @file FbxImportSettings.h
 * @brief Persisted FBX import options shared by static/character paths (no ufbx types).
 */

#pragma once

#include <string>

namespace Hephaestus {

enum class FbxSpaceConversion {
    Auto = 0,
    AdjustTransforms = 1,
    ModifyGeometry = 2
};

inline const char* FbxSpaceConversionToString(FbxSpaceConversion mode) {
    switch (mode) {
    case FbxSpaceConversion::AdjustTransforms:
        return "adjustTransforms";
    case FbxSpaceConversion::ModifyGeometry:
        return "modifyGeometry";
    case FbxSpaceConversion::Auto:
    default:
        return "auto";
    }
}

inline FbxSpaceConversion FbxSpaceConversionFromString(const std::string& value) {
    if (value == "adjustTransforms") {
        return FbxSpaceConversion::AdjustTransforms;
    }
    if (value == "modifyGeometry") {
        return FbxSpaceConversion::ModifyGeometry;
    }
    return FbxSpaceConversion::Auto;
}

} // namespace Hephaestus
