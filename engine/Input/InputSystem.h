#pragma once

#include "InputMap.h"

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace Hephaestus {

class InputSystem {
public:
    static constexpr int MAX_KEYS = 512;
    static constexpr int MAX_MOUSE_BUTTONS = 8;
    static constexpr int MAX_GAMEPAD_BUTTONS = 15;
    static constexpr int MAX_GAMEPAD_AXES = 6;

    InputSystem();

    void BeginFrame();
    void Reset();
    // Drop one-shot deltas and pressed/released edges while preserving held
    // physical state and explicit automation override levels.
    void ResetTransientState();

    void SetKeyState(int key, bool down);
    void SetMouseButtonState(int button, bool down);
    void SetMousePosition(double x, double y);
    void SetMouseDelta(double x, double y);
    void AddMouseDelta(double x, double y);
    void QueueMouseDelta(double x, double y);
    void AddScrollDelta(double x, double y);
    void SetGamepadConnected(bool connected);
    void SetGamepadButtonState(int button, bool down);
    void SetGamepadAxisState(int axis, float value);

    void SetActionOverride(const std::string& name, float value);
    // Automation arrives between frames; apply after the previous-state snapshot.
    void QueueActionOverride(const std::string& name, float value);
    void SetAxisOverride(const std::string& name, float value);
    void ClearActionOverrides();
    void ClearAxisOverrides();
    void ClearOverrides();

    bool IsKeyDown(int key) const;
    bool IsMouseButtonDown(int button) const;
    double GetMouseX() const;
    double GetMouseY() const;
    double GetMouseDeltaX() const;
    double GetMouseDeltaY() const;
    double GetScrollDeltaX() const;
    double GetScrollDeltaY() const;
    bool IsGamepadConnected() const;

    bool IsActionDown(const std::string& name) const;
    bool IsActionPressed(const std::string& name) const;
    bool IsActionReleased(const std::string& name) const;
    float GetAxis(const std::string& name) const;
    bool HasAction(const std::string& name) const;
    bool HasAxis(const std::string& name) const;

    // Runtime rebinding, for a controls menu: an action's or axis's bindings, the ones the loaded map gave it,
    // and a replacement list resolved like a loaded map (false with outError for an unknown name or control).
    const std::vector<InputBinding>* GetBindings(const std::string& name) const;
    const std::vector<InputBinding>* GetDefaultBindings(const std::string& name) const;
    bool SetBindings(const std::string& name, std::vector<InputBinding> bindings, std::string& outError);
    // The first key, mouse button or gamepad button that went down this frame.
    bool GetPressedControl(InputBindingType& outType, int& outCode) const;

    void SetMap(InputMap map);
    const InputMap& GetMap() const { return m_Map; }
    bool LoadMap(const std::filesystem::path& path, std::string& outError);
    const std::filesystem::path& GetMapPath() const { return m_MapPath; }

    void SetCursorModeRequest(int mode) { m_CursorModeRequest = mode == 1 ? 1 : 0; }
    int GetCursorModeRequest() const { return m_CursorModeRequest; }

private:
    struct DeviceState {
        std::array<bool, MAX_KEYS> keys{};
        std::array<bool, MAX_MOUSE_BUTTONS> mouseButtons{};
        std::array<bool, MAX_GAMEPAD_BUTTONS> gamepadButtons{};
        std::array<float, MAX_GAMEPAD_AXES> gamepadAxes{};
        double mouseX = 0.0;
        double mouseY = 0.0;
        double mouseDeltaX = 0.0;
        double mouseDeltaY = 0.0;
        double scrollDeltaX = 0.0;
        double scrollDeltaY = 0.0;
        bool gamepadConnected = false;
    };

    bool EvaluateAction(const InputActionDefinition& action, const DeviceState& state) const;
    float EvaluateAxis(const InputAxisDefinition& axis, const DeviceState& state) const;
    float EvaluateBinding(const InputBinding& binding, const DeviceState& state) const;
    void RebuildLookup();

    InputMap m_Map;
    InputMap m_DefaultMap;
    std::filesystem::path m_MapPath;
    DeviceState m_Current;
    DeviceState m_Previous;
    std::unordered_map<std::string, std::size_t> m_ActionLookup;
    std::unordered_map<std::string, std::size_t> m_AxisLookup;
    std::unordered_map<std::string, float> m_ActionOverrides;
    std::unordered_map<std::string, float> m_PreviousActionOverrides;
    std::unordered_map<std::string, float> m_QueuedActionOverrides;
    std::unordered_map<std::string, float> m_AxisOverrides;
    double m_QueuedMouseDeltaX = 0.0;
    double m_QueuedMouseDeltaY = 0.0;
    int m_CursorModeRequest = 0;
};

} // namespace Hephaestus
