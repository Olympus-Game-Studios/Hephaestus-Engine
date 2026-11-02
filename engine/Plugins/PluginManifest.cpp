#include "PluginManifest.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <regex>

namespace Hephaestus::Plugins {
namespace {
bool SafeRelative(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) return false;
    for (const auto& part : path) if (part == "..") return false;
    return true;
}

std::array<int, 3> ParseVersion(const std::string& value) {
    std::array<int, 3> out{};
    std::smatch match;
    if (std::regex_search(value, match, std::regex(R"((\d+)\.(\d+)\.(\d+))"))) {
        for (int i = 0; i < 3; ++i) out[i] = std::stoi(match[i + 1].str());
    }
    return out;
}

// Manifests name the Windows module; other platforms load CMake's lib<name>.dylib / .so beside it.
std::filesystem::path NativeModulePath(std::filesystem::path path) {
#ifndef _WIN32
    if (path.extension() == ".dll") {
#ifdef __APPLE__
        path.replace_filename("lib" + path.stem().string() + ".dylib");
#else
        path.replace_filename("lib" + path.stem().string() + ".so");
#endif
    }
#endif
    return path;
}
}

ManifestParseResult ParsePluginManifest(const std::filesystem::path& path) {
    ManifestParseResult result;
    result.manifest.manifestPath = path;
    result.manifest.root = path.parent_path();
    try {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("manifest cannot be opened");
        const auto json = nlohmann::json::parse(input);
        auto& m = result.manifest;
        m.version = json.value("version", 0u);
        m.apiVersion = json.value("apiVersion", 1u);
        m.id = json.value("id", "");
        m.name = json.value("name", "");
        m.description = json.value("description", "");
        m.author = json.value("author", "");
        m.pluginVersion = json.value("pluginVersion", "");
        m.engineVersion = json.value("engineVersion", "");
        m.entry = NativeModulePath(json.value("entry", ""));
        m.runtimeEntry = NativeModulePath(json.value("runtimeEntry", ""));
        m.content = json.value("content", "");
        m.icon = json.value("icon", "");
        m.defaultEnabled = json.value("defaultEnabled", true);
        const std::string type = json.value("type", "editor");
        if (type == "editor") m.type = PluginType::Editor;
        else if (type == "runtime") m.type = PluginType::Runtime;
        else if (type == "hybrid") m.type = PluginType::Hybrid;
        else throw std::runtime_error("type must be editor, runtime, or hybrid");

        if (m.version != 1) throw std::runtime_error("unsupported manifest version");
        if (!std::regex_match(m.id, std::regex(R"([a-z0-9]+([._-][a-z0-9]+)+)")))
            throw std::runtime_error("id must be a lowercase reverse-domain identifier");
        if (m.name.empty() || m.pluginVersion.empty() || m.engineVersion.empty())
            throw std::runtime_error("name, pluginVersion, and engineVersion are required");
        if (m.entry.empty() && m.runtimeEntry.empty())
            throw std::runtime_error("entry or runtimeEntry is required");
        if (!m.entry.empty() && !SafeRelative(m.entry))
            throw std::runtime_error("entry must be a safe relative path");
        if (!m.runtimeEntry.empty() && !SafeRelative(m.runtimeEntry))
            throw std::runtime_error("runtimeEntry must be a safe relative path");
        if (!m.content.empty() && !SafeRelative(m.content))
            throw std::runtime_error("content must be a safe relative path");
        if (!m.icon.empty() && !SafeRelative(m.icon))
            throw std::runtime_error("icon must be a safe relative path");
        result.success = true;
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
}

bool IsEngineVersionCompatible(const std::string& constraint,
                               const std::string& engineVersion) {
    if (constraint.empty() || constraint == "*") return true;
    const auto current = ParseVersion(engineVersion);
    std::string expected = constraint;
    expected.erase(std::remove_if(expected.begin(), expected.end(), ::isspace), expected.end());
    if (expected.starts_with(">=")) return current >= ParseVersion(expected.substr(2));
    if (expected.starts_with("^")) {
        const auto base = ParseVersion(expected.substr(1));
        return current >= base && current[0] == base[0];
    }
    if (expected.starts_with("=")) expected.erase(expected.begin());
    return current == ParseVersion(expected);
}

} // namespace Hephaestus::Plugins
