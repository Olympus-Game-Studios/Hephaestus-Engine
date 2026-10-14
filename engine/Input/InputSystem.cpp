#include "InputSystem.h"

#include <algorithm>
#include <cmath>

namespace Hephaestus {
namespace {

template <typename T, std::size_t N>
bool InRange(const std::array<T, N>& values, int index)
{
    return index >= 0 && index < static_cast<int>(values.size());
}

float ApplyDeadzone(float value, float deadzone)
{
    const float magnitude = std::abs(value);
    if (magnitude <= deadzone) {
        return 0.0f;
    }
    const float normalized = (magnitude - deadzone) / (1.0f - deadzone);
    return std::copysign(std::min(normalized, 1.0f), value);
}

} // namespace

InputSystem::InputSystem()
    : m_Map(InputMap::CreateDefault())
{
    RebuildLookup();
}

void InputSystem::BeginFrame()
{
    m_Previous = m_Current;
    m_PreviousActionOverrides = m_ActionOverrides;
    m_Current.mouseDeltaX = m_QueuedMouseDeltaX;
    m_Current.mouseDeltaY = m_QueuedMouseDeltaY;
    m_QueuedMouseDeltaX = 0.0;
    m_QueuedMouseDeltaY = 0.0;
    m_Current.scrollDeltaX = 0.0;
    m_Current.scrollDeltaY = 0.0;
}

void InputSystem::Reset()
{
    m_Current = {};
    m_Previous = {};
    m_ActionOverrides.clear();
    m_PreviousActionOverrides.clear();
    m_AxisOverrides.clear();
    m_QueuedMouseDeltaX = 0.0;
    m_QueuedMouseDeltaY = 0.0;
    m_CursorModeRequest = 0;
}

void InputSystem::SetKeyState(int key, bool down)
{
    if (InRange(m_Current.keys, key)) m_Current.keys[static_cast<std::size_t>(key)] = down;
}

void InputSystem::SetMouseButtonState(int button, bool down)
{
    if (InRange(m_Current.mouseButtons, button)) {
        m_Current.mouseButtons[static_cast<std::size_t>(button)] = down;
    }
}

void InputSystem::SetMousePosition(double x, double y)
{
    m_Current.mouseX = x;
    m_Current.mouseY = y;
}

void InputSystem::SetMouseDelta(double x, double y)
{
    m_Current.mouseDeltaX = x;
    m_Current.mouseDeltaY = y;
}

void InputSystem::AddMouseDelta(double x, double y)
{
    m_Current.mouseDeltaX += x;
    m_Current.mouseDeltaY += y;
}

void InputSystem::QueueMouseDelta(double x, double y)
{
    m_QueuedMouseDeltaX += x;
    m_QueuedMouseDeltaY += y;
}

void InputSystem::AddScrollDelta(double x, double y)
{
    m_Current.scrollDeltaX += x;
    m_Current.scrollDeltaY += y;
}

void InputSystem::SetGamepadConnected(bool connected)
{
    m_Current.gamepadConnected = connected;
    if (!connected) {
        m_Current.gamepadButtons.fill(false);
        m_Current.gamepadAxes.fill(0.0f);
    }
}

void InputSystem::SetGamepadButtonState(int button, bool down)
{
    if (InRange(m_Current.gamepadButtons, button)) {
        m_Current.gamepadButtons[static_cast<std::size_t>(button)] = down;
    }
}

void InputSystem::SetGamepadAxisState(int axis, float value)
{
    if (InRange(m_Current.gamepadAxes, axis)) {
        m_Current.gamepadAxes[static_cast<std::size_t>(axis)] =
            std::clamp(value, -1.0f, 1.0f);
    }
}

void InputSystem::SetActionOverride(const std::string& name, float value)
{
    if (!HasAction(name)) return;
    m_ActionOverrides[name] = std::clamp(value, 0.0f, 1.0f);
}

void InputSystem::SetAxisOverride(const std::string& name, float value)
{
    if (!HasAxis(name)) return;
    m_AxisOverrides[name] = std::clamp(value, -1.0f, 1.0f);
}

void InputSystem::ClearActionOverrides()
{
    m_ActionOverrides.clear();
}

void InputSystem::ClearAxisOverrides()
{
    m_AxisOverrides.clear();
}

void InputSystem::ClearOverrides()
{
    ClearActionOverrides();
    ClearAxisOverrides();
}

bool InputSystem::IsKeyDown(int key) const
{
    return InRange(m_Current.keys, key) && m_Current.keys[static_cast<std::size_t>(key)];
}

bool InputSystem::IsMouseButtonDown(int button) const
{
    return InRange(m_Current.mouseButtons, button) &&
           m_Current.mouseButtons[static_cast<std::size_t>(button)];
}

double InputSystem::GetMouseX() const { return m_Current.mouseX; }
double InputSystem::GetMouseY() const { return m_Current.mouseY; }
double InputSystem::GetMouseDeltaX() const { return m_Current.mouseDeltaX; }
double InputSystem::GetMouseDeltaY() const { return m_Current.mouseDeltaY; }
double InputSystem::GetScrollDeltaX() const { return m_Current.scrollDeltaX; }
double InputSystem::GetScrollDeltaY() const { return m_Current.scrollDeltaY; }
bool InputSystem::IsGamepadConnected() const { return m_Current.gamepadConnected; }

bool InputSystem::IsActionDown(const std::string& name) const
{
    if (const auto overrideIt = m_ActionOverrides.find(name);
        overrideIt != m_ActionOverrides.end()) {
        return overrideIt->second >= 0.5f;
    }
    const auto it = m_ActionLookup.find(name);
    return it != m_ActionLookup.end() && EvaluateAction(m_Map.actions[it->second], m_Current);
}

bool InputSystem::IsActionPressed(const std::string& name) const
{
    const auto it = m_ActionLookup.find(name);
    if (it == m_ActionLookup.end()) return false;
    const auto& action = m_Map.actions[it->second];
    const auto currentOverride = m_ActionOverrides.find(name);
    const auto previousOverride = m_PreviousActionOverrides.find(name);
    const bool current = currentOverride != m_ActionOverrides.end()
        ? currentOverride->second >= 0.5f
        : EvaluateAction(action, m_Current);
    const bool previous = previousOverride != m_PreviousActionOverrides.end()
        ? previousOverride->second >= 0.5f
        : EvaluateAction(action, m_Previous);
    return current && !previous;
}

bool InputSystem::IsActionReleased(const std::string& name) const
{
    const auto it = m_ActionLookup.find(name);
    if (it == m_ActionLookup.end()) return false;
    const auto& action = m_Map.actions[it->second];
    const auto currentOverride = m_ActionOverrides.find(name);
    const auto previousOverride = m_PreviousActionOverrides.find(name);
    const bool current = currentOverride != m_ActionOverrides.end()
        ? currentOverride->second >= 0.5f
        : EvaluateAction(action, m_Current);
    const bool previous = previousOverride != m_PreviousActionOverrides.end()
        ? previousOverride->second >= 0.5f
        : EvaluateAction(action, m_Previous);
    return !current && previous;
}

float InputSystem::GetAxis(const std::string& name) const
{
    if (const auto overrideIt = m_AxisOverrides.find(name);
        overrideIt != m_AxisOverrides.end()) {
        return overrideIt->second;
    }
    const auto it = m_AxisLookup.find(name);
    return it == m_AxisLookup.end() ? 0.0f : EvaluateAxis(m_Map.axes[it->second], m_Current);
}

bool InputSystem::HasAction(const std::string& name) const
{
    return m_ActionLookup.contains(name);
}

bool InputSystem::HasAxis(const std::string& name) const
{
    return m_AxisLookup.contains(name);
}

void InputSystem::SetMap(InputMap map)
{
    std::string error;
    if (!map.Validate(error)) return;
    m_Map = std::move(map);
    m_MapPath.clear();
    RebuildLookup();
}

bool InputSystem::LoadMap(const std::filesystem::path& path, std::string& outError)
{
    InputMap map;
    if (!InputMap::Load(path, map, outError)) return false;
    m_Map = std::move(map);
    m_MapPath = path.lexically_normal();
    RebuildLookup();
    return true;
}

bool InputSystem::EvaluateAction(const InputActionDefinition& action,
                                 const DeviceState& state) const
{
    for (const auto& binding : action.bindings) {
        switch (binding.type) {
        case InputBindingType::Keyboard:
            if (InRange(state.keys, binding.controlCode) &&
                state.keys[static_cast<std::size_t>(binding.controlCode)]) return true;
            break;
        case InputBindingType::MouseButton:
            if (InRange(state.mouseButtons, binding.controlCode) &&
                state.mouseButtons[static_cast<std::size_t>(binding.controlCode)]) return true;
            break;
        case InputBindingType::GamepadButton:
            if (state.gamepadConnected && InRange(state.gamepadButtons, binding.controlCode) &&
                state.gamepadButtons[static_cast<std::size_t>(binding.controlCode)]) return true;
            break;
        default:
            break;
        }
    }
    return false;
}

float InputSystem::EvaluateAxis(const InputAxisDefinition& axis,
                                const DeviceState& state) const
{
    float value = 0.0f;
    for (const auto& binding : axis.bindings) {
        value += EvaluateBinding(binding, state);
    }
    return axis.clamp ? std::clamp(value, -1.0f, 1.0f) : value;
}

float InputSystem::EvaluateBinding(const InputBinding& binding,
                                   const DeviceState& state) const
{
    switch (binding.type) {
    case InputBindingType::Keyboard:
        return InRange(state.keys, binding.controlCode) &&
               state.keys[static_cast<std::size_t>(binding.controlCode)]
            ? binding.scale : 0.0f;
    case InputBindingType::MouseButton:
        return InRange(state.mouseButtons, binding.controlCode) &&
               state.mouseButtons[static_cast<std::size_t>(binding.controlCode)]
            ? binding.scale : 0.0f;
    case InputBindingType::MouseDelta:
        return static_cast<float>(binding.controlCode == 0
            ? state.mouseDeltaX : state.mouseDeltaY) * binding.scale;
    case InputBindingType::MouseWheel:
        return static_cast<float>(binding.controlCode == 0
            ? state.scrollDeltaX : state.scrollDeltaY) * binding.scale;
    case InputBindingType::GamepadButton:
        return state.gamepadConnected && InRange(state.gamepadButtons, binding.controlCode) &&
               state.gamepadButtons[static_cast<std::size_t>(binding.controlCode)]
            ? binding.scale : 0.0f;
    case InputBindingType::GamepadAxis:
        if (!state.gamepadConnected || !InRange(state.gamepadAxes, binding.controlCode)) return 0.0f;
        return ApplyDeadzone(
            state.gamepadAxes[static_cast<std::size_t>(binding.controlCode)],
            binding.deadzone) * binding.scale;
    }
    return 0.0f;
}

void InputSystem::RebuildLookup()
{
    m_ActionLookup.clear();
    m_AxisLookup.clear();
    for (std::size_t i = 0; i < m_Map.actions.size(); ++i) {
        m_ActionLookup.emplace(m_Map.actions[i].name, i);
    }
    for (std::size_t i = 0; i < m_Map.axes.size(); ++i) {
        m_AxisLookup.emplace(m_Map.axes[i].name, i);
    }
}

} // namespace Hephaestus
