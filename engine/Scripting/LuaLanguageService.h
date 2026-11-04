#pragma once

#include "../PluginAPI/ScriptLanguageAPI.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * Code editor services for built-in Lua: completion candidates and hover text,
 * from Lua keywords, the engine's registered API and the file's own names.
 */
namespace LuaLanguageService {

struct Completion {
    std::string label;
    uint32_t kind = HP_COMPLETION_VARIABLE; // HPCompletionKind
    std::string detail;
};

/// @p line and @p column are 1-based; column counts bytes of the line.
std::vector<Completion> Complete(const std::string& source, uint32_t line, uint32_t column);
std::string Hover(const std::string& source, uint32_t line, uint32_t column);

} // namespace LuaLanguageService
