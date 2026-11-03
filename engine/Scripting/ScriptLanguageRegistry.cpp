#include "ScriptLanguageRegistry.h"

#include "LuaExposeScanner.h"
#include "../Assets/AssetTypes.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace {

std::string ToLower(std::string_view value) {
    std::string out(value);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::vector<std::string> SplitExtensions(const char* extensions) {
    std::vector<std::string> out;
    std::string_view rest = extensions ? extensions : "";
    while (!rest.empty()) {
        const std::size_t split = rest.find(';');
        const std::string_view part = rest.substr(0, split);
        if (!part.empty()) {
            out.push_back(ToLower(part));
        }
        if (split == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(split + 1);
    }
    return out;
}

bool HasExtension(const HPScriptLanguage& language, const std::string& extension) {
    const auto extensions = SplitExtensions(language.extensions);
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

LuaExposeType ExposeTypeFor(const HPValue& value) {
    switch (value.type) {
        case HP_VALUE_BOOL: return LuaExposeType::Bool;
        case HP_VALUE_STRING: return LuaExposeType::String;
        case HP_VALUE_VEC2: return LuaExposeType::Vec2;
        case HP_VALUE_VEC3: return LuaExposeType::Vec3;
        case HP_VALUE_VEC4: return LuaExposeType::Color;
        default: return LuaExposeType::Number;
    }
}

PropertyValue PropertyFor(const HPValue& value) {
    switch (value.type) {
        case HP_VALUE_BOOL: return PropertyValue(value.boolean != 0);
        case HP_VALUE_STRING: return PropertyValue(std::string(value.string ? value.string : ""));
        case HP_VALUE_VEC2: return PropertyValue(Vec2(value.vector[0], value.vector[1]));
        case HP_VALUE_VEC3: return PropertyValue(Vec3(value.vector[0], value.vector[1], value.vector[2]));
        case HP_VALUE_VEC4:
            return PropertyValue(Vec4(value.vector[0], value.vector[1], value.vector[2], value.vector[3]));
        default: return PropertyValue(value.number);
    }
}

const HPSyntaxRule kLuaRules[] = {
    { HP_SYNTAX_KEYWORD, R"(\b(?:and|break|do|else|elseif|end|false|for|function|goto|if|in|local|nil|not|or|repeat|return|then|true|until|while|self)\b)" },
    { HP_SYNTAX_FUNCTION, R"(\b[A-Za-z_]\w*(?=\s*[({"']))" },
    { HP_SYNTAX_TYPE, R"(\b[A-Z]\w*\b)" },
    { HP_SYNTAX_NUMBER, R"(\b(?:0[xX][0-9A-Fa-f]+|\d+(?:\.\d*)?(?:[eE][+-]?\d+)?)\b)" },
    { HP_SYNTAX_STRING, R"("(?:[^"\\]|\\.)*"?|'(?:[^'\\]|\\.)*'?)" },
    { HP_SYNTAX_COMMENT, R"(--(?!\[=*\[).*)" },
};

const HPSyntaxBlock kLuaBlocks[] = {
    { HP_SYNTAX_COMMENT, R"(--\[=*\[)", R"(\]=*\])" },
    { HP_SYNTAX_STRING, R"(\[=*\[)", R"(\]=*\])" },
};

const HPSyntax kLuaSyntax = {
    kLuaRules, static_cast<uint32_t>(std::size(kLuaRules)),
    kLuaBlocks, static_cast<uint32_t>(std::size(kLuaBlocks)),
    "--",
};

} // namespace

ScriptLanguageRegistry& ScriptLanguageRegistry::Instance() {
    static ScriptLanguageRegistry registry;
    return registry;
}

bool ScriptLanguageRegistry::Register(const HPScriptLanguage* language, std::string& error) {
    if (!language || language->structSize < offsetof(HPScriptLanguage, syntax) ||
        language->apiVersion != HP_SCRIPT_LANGUAGE_API_VERSION) {
        error = "script language table is missing or built for another API version";
        return false;
    }
    const std::string id = language->id ? language->id : "";
    if (id.empty() || !language->displayName || !language->createRuntime ||
        !language->destroyRuntime || !language->createInstance || !language->destroyInstance) {
        error = "script language '" + id + "' is missing a required member";
        return false;
    }
    if (id == "lua") {
        error = "Lua is built into the engine and cannot be replaced by a plugin";
        return false;
    }
    const auto extensions = SplitExtensions(language->extensions);
    if (extensions.empty()) {
        error = "script language '" + id + "' declares no file extensions";
        return false;
    }
    for (const HPScriptLanguage* existing : m_Languages) {
        if (id == existing->id) {
            error = "script language '" + id + "' is already registered";
            return false;
        }
    }
    for (const std::string& extension : extensions) {
        if (extension.size() < 2 || extension[0] != '.' || extension == ".lua") {
            error = "script language '" + id + "' has an invalid extension '" + extension + "'";
            return false;
        }
        for (const HPScriptLanguage* existing : m_Languages) {
            if (HasExtension(*existing, extension)) {
                error = "extension " + extension + " is already claimed by " + existing->id;
                return false;
            }
        }
    }
    for (const std::string& extension : extensions) {
        RegisterScriptAssetExtension(extension);
    }
    m_Languages.push_back(language);
    return true;
}

bool ScriptLanguageRegistry::LoadModule(const std::filesystem::path& path, std::string& error) {
    std::error_code absoluteError;
    // Absolute, so dlopen/LoadLibrary never search system paths for a bare file name.
    const std::filesystem::path normalized = std::filesystem::absolute(path, absoluteError).lexically_normal();
    if (std::find(m_LoadedModules.begin(), m_LoadedModules.end(), normalized) != m_LoadedModules.end()) {
        return true;
    }
    if (!std::filesystem::is_regular_file(normalized)) {
        error = "script language module is missing: " + normalized.string();
        return false;
    }
    // shortcut: modules stay mapped until exit so no runtime can outlive its code;
    // unload them here if plugins ever need hot-swapping without a restart.
#ifdef _WIN32
    // Altered search path lets the module find runtime DLLs shipped beside it.
    HMODULE module = LoadLibraryExW(normalized.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        error = "LoadLibrary failed for " + normalized.string() + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    auto getLanguages = reinterpret_cast<HPGetScriptLanguagesFn>(
        GetProcAddress(module, HP_SCRIPT_LANGUAGES_EXPORT_NAME));
#else
    void* module = dlopen(normalized.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        error = dlerror();
        return false;
    }
    auto getLanguages = reinterpret_cast<HPGetScriptLanguagesFn>(
        dlsym(module, HP_SCRIPT_LANGUAGES_EXPORT_NAME));
#endif
    m_LoadedModules.push_back(normalized);
    if (!getLanguages) {
        error = normalized.filename().string() + " does not export " HP_SCRIPT_LANGUAGES_EXPORT_NAME;
        return false;
    }
    uint32_t count = 0;
    const HPScriptLanguage* const* languages = getLanguages(HP_SCRIPT_LANGUAGE_API_VERSION, &count);
    if (!languages || count == 0) {
        error = normalized.filename().string() + " provides no script language for API version " +
                std::to_string(HP_SCRIPT_LANGUAGE_API_VERSION);
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0; i < count; ++i) {
        std::string languageError;
        if (!Register(languages[i], languageError)) {
            error += (error.empty() ? "" : "; ") + languageError;
            ok = false;
        }
    }
    return ok;
}

const HPScriptLanguage* ScriptLanguageRegistry::FindForPath(std::string_view scriptPath) const {
    const std::string extension = ToLower(std::filesystem::path(scriptPath).extension().string());
    if (extension.empty()) {
        return nullptr;
    }
    for (const HPScriptLanguage* language : m_Languages) {
        if (HasExtension(*language, extension)) {
            return language;
        }
    }
    return nullptr;
}

const HPSyntax* ScriptLanguageRegistry::FindSyntaxForPath(std::string_view path) const {
    if (ToLower(std::filesystem::path(path).extension().string()) == ".lua") {
        return &kLuaSyntax;
    }
    const HPScriptLanguage* language = FindForPath(path);
    if (!language || language->structSize < offsetof(HPScriptLanguage, syntax) + sizeof(language->syntax)) {
        return nullptr;
    }
    return language->syntax;
}

namespace {

// Members appended after the first release exist only when the plugin's table is large enough.
template <typename Member>
bool Provides(const HPScriptLanguage& language, Member HPScriptLanguage::*member, std::size_t offset) {
    return language.structSize >= offset + sizeof(language.*member) && language.*member;
}

bool IsLua(std::string_view path) {
    return ToLower(std::filesystem::path(path).extension().string()) == ".lua";
}

} // namespace

std::vector<LuaLanguageService::Completion> ScriptLanguageRegistry::Complete(std::string_view path,
                                                                             const std::string& source,
                                                                             uint32_t line, uint32_t column) const {
    if (IsLua(path)) {
        return LuaLanguageService::Complete(source, line, column);
    }
    std::vector<LuaLanguageService::Completion> completions;
    const HPScriptLanguage* language = FindForPath(path);
    if (!language || !Provides(*language, &HPScriptLanguage::complete, offsetof(HPScriptLanguage, complete))) {
        return completions;
    }
    auto collect = [](void* user, const char* label, uint32_t kind, const char* detail) {
        if (label && *label) {
            static_cast<std::vector<LuaLanguageService::Completion>*>(user)->push_back(
                { label, kind, detail ? detail : "" });
        }
    };
    const std::string file(path);
    language->complete(file.c_str(), source.c_str(), line, column, collect, &completions);
    return completions;
}

std::string ScriptLanguageRegistry::Hover(std::string_view path, const std::string& source,
                                          uint32_t line, uint32_t column) const {
    if (IsLua(path)) {
        return LuaLanguageService::Hover(source, line, column);
    }
    const HPScriptLanguage* language = FindForPath(path);
    if (!language || !Provides(*language, &HPScriptLanguage::hover, offsetof(HPScriptLanguage, hover))) {
        return {};
    }
    const std::string file(path);
    const std::size_t size = language->hover(file.c_str(), source.c_str(), line, column, nullptr, 0);
    if (size <= 1) {
        return {};
    }
    std::string text(size, '\0');
    language->hover(file.c_str(), source.c_str(), line, column, text.data(), text.size());
    text.resize(size - 1);
    return text;
}

bool ScriptLanguageRegistry::IsScriptPath(std::string_view scriptPath) const {
    return ToLower(std::filesystem::path(scriptPath).extension().string()) == ".lua" ||
           FindForPath(scriptPath) != nullptr;
}

bool ScriptLanguageRegistry::DescribeFields(const HPScriptLanguage& language,
                                            const std::filesystem::path& resolvedPath,
                                            LuaExposeScanResult& out) {
    out = {};
    if (!language.describeFields) {
        return false;
    }
    std::ifstream file(resolvedPath, std::ios::binary);
    std::ostringstream source;
    source << file.rdbuf();
    out.contentHash = LuaExposeScanner::ComputeContentHash(source.str());

    const std::string path = resolvedPath.string();
    auto collect = [](void* user, const char* name, HPValue value) {
        auto& result = *static_cast<LuaExposeScanResult*>(user);
        if (!name || !*name) {
            return;
        }
        LuaExposedField field;
        field.name = name;
        field.type = ExposeTypeFor(value);
        field.defaultValue = PropertyFor(value);
        field.sourceOrder = result.fields.size();
        result.fields.push_back(std::move(field));
    };
    out.valid = language.describeFields(path.c_str(), collect, &out) != 0;
    if (!out.valid) {
        out.diagnostics.push_back({0, std::string(language.displayName) +
            " could not read the fields of " + resolvedPath.filename().string(), false});
    }
    return true;
}

void ScriptLanguageRegistry::Clear() {
    m_Languages.clear();
}
