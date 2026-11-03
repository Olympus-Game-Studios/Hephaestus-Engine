#pragma once

#include "../PluginAPI/ScriptLanguageAPI.h"
#include "LuaExposeMetadata.h"
#include "LuaLanguageService.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/**
 * Scripting languages provided by plugins. Lua is built in and never listed
 * here; any script path no plugin language claims stays Lua.
 */
class ScriptLanguageRegistry {
public:
    static ScriptLanguageRegistry& Instance();

    /// Validates and adds a language. The table must outlive the registry.
    bool Register(const HPScriptLanguage* language, std::string& error);

    /// Loads a language module (a plugin's runtimeEntry) and registers every
    /// language it exports. Loading the same module twice is a no-op.
    bool LoadModule(const std::filesystem::path& path, std::string& error);

    /// The plugin language whose extension matches, or nullptr for Lua.
    const HPScriptLanguage* FindForPath(std::string_view scriptPath) const;

    /// Highlighting for the code editor: Lua's built-in syntax for .lua files,
    /// the claiming language's syntax otherwise, or nullptr for plain text.
    const HPSyntax* FindSyntaxForPath(std::string_view path) const;

    /// Completion candidates for the code editor at 1-based @p line and byte
    /// @p column: built-in Lua's for .lua files, the claiming language's otherwise.
    std::vector<LuaLanguageService::Completion> Complete(std::string_view path, const std::string& source,
                                                         uint32_t line, uint32_t column) const;

    /// Hover text for the symbol at the position, or empty.
    std::string Hover(std::string_view path, const std::string& source, uint32_t line, uint32_t column) const;

    /// True for .lua files and for files a plugin language claims.
    bool IsScriptPath(std::string_view scriptPath) const;

    const std::vector<const HPScriptLanguage*>& Languages() const { return m_Languages; }

    /// Asks the language for the script's inspector fields. Returns false when
    /// the language cannot describe them.
    static bool DescribeFields(const HPScriptLanguage& language,
                               const std::filesystem::path& resolvedPath,
                               LuaExposeScanResult& out);

    /// Forgets registered languages. Loaded modules stay mapped.
    void Clear();

private:
    std::vector<const HPScriptLanguage*> m_Languages;
    std::vector<std::filesystem::path> m_LoadedModules;
};
