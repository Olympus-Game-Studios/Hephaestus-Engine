#pragma once

#include "LuaExposeMetadata.h"

#include <string>
#include <string_view>

class LuaExposeScanner {
public:
    static LuaExposeScanResult Scan(std::string_view source);
    static std::string ComputeContentHash(std::string_view source);
};
