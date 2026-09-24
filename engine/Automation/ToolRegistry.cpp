#include "ToolRegistry.h"

#include "Core/PlayModeManager.h"

#include <cctype>

namespace Hephaestus::Automation {
namespace {

bool IsValidToolName(std::string_view name)
{
    if (name.empty() || name.size() > 128) return false;
    for (const unsigned char character : name) {
        if (!std::isalnum(character) && character != '_' && character != '.' && character != '-') {
            return false;
        }
    }
    return true;
}

bool MeetsPlayRequirement(PlayRequirement requirement, PlayState state)
{
    switch (requirement) {
    case PlayRequirement::Any: return true;
    case PlayRequirement::EditorOnly: return state == PlayState::Editor;
    case PlayRequirement::PlayModeActive: return state != PlayState::Editor;
    case PlayRequirement::PlayingOnly: return state == PlayState::Playing;
    }
    return false;
}

} // namespace

bool ToolRegistry::Register(ToolDefinition definition, std::string& outError)
{
    if (!IsValidToolName(definition.name)) {
        outError = "Tool names must contain 1-128 characters from [A-Za-z0-9_.-]";
        return false;
    }
    if (definition.hostMask == HostMask::None) {
        outError = "Tool must declare at least one host";
        return false;
    }
    if (!definition.inputSchema.is_object()) {
        outError = "Tool input schema must be a JSON object";
        return false;
    }
    if (!definition.handler) {
        outError = "Tool handler is required";
        return false;
    }
    if (m_Tools.contains(definition.name)) {
        outError = "Tool is already registered: " + definition.name;
        return false;
    }
    const std::string name = definition.name;
    m_Tools.emplace(name, std::move(definition));
    outError.clear();
    return true;
}

const ToolDefinition* ToolRegistry::Find(std::string_view name) const noexcept
{
    const auto it = m_Tools.find(name);
    return it == m_Tools.end() ? nullptr : &it->second;
}

std::vector<const ToolDefinition*> ToolRegistry::Available(HostMask host, PlayState state) const
{
    std::vector<const ToolDefinition*> available;
    for (const auto& [name, definition] : m_Tools) {
        (void)name;
        if (HasHost(definition.hostMask, host) &&
            MeetsPlayRequirement(definition.playRequirement, state)) {
            available.push_back(&definition);
        }
    }
    return available;
}

} // namespace Hephaestus::Automation
