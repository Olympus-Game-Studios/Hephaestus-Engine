#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace HephaestusFPS {

struct InputMergeResult {
    bool changed = false;
    std::vector<std::string> added;
    std::string error;
};

inline bool HasNamedEntry(const nlohmann::json& list, const std::string& name)
{
    if (!list.is_array()) {
        return false;
    }
    for (const auto& item : list) {
        if (item.is_object() && item.value("name", std::string{}) == name) {
            return true;
        }
    }
    return false;
}

inline InputMergeResult MergeInputFragment(nlohmann::json& destination,
                                           const nlohmann::json& fragment)
{
    InputMergeResult result;
    if (!destination.is_object()) {
        result.error = "destination input map must be an object";
        return result;
    }
    if (!fragment.is_object()) {
        result.error = "fragment input map must be an object";
        return result;
    }
    if (!destination.contains("actions") || !destination["actions"].is_array()) {
        destination["actions"] = nlohmann::json::array();
    }
    if (!destination.contains("axes") || !destination["axes"].is_array()) {
        destination["axes"] = nlohmann::json::array();
    }

    auto appendMissing = [&](const char* key, const char* kind) {
        const nlohmann::json incoming = fragment.value(key, nlohmann::json::array());
        if (!incoming.is_array()) {
            return;
        }
        for (const auto& item : incoming) {
            if (!item.is_object()) {
                continue;
            }
            const std::string name = item.value("name", std::string{});
            if (name.empty() || HasNamedEntry(destination[key], name)) {
                continue;
            }
            destination[key].push_back(item);
            result.added.push_back(std::string(kind) + " " + name);
            result.changed = true;
        }
    };
    appendMissing("actions", "action");
    appendMissing("axes", "axis");
    return result;
}

} // namespace HephaestusFPS
