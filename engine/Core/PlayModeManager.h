/**
 * @file PlayModeManager.h
 * @brief Central Play Mode state machine and lifecycle manager.
 *
 * This class owns the Play/Stop/Pause cycle for both the legacy ImGui editor
 * and the Qt-based editor.  It orchestrates:
 *   - Engine state transitions (Editor <-> Play)
 *   - Scene snapshot / restore (non-destructive play)
 *   - Script lifecycle (onAwake → onEnable → onUpdate/onLateUpdate → onDisable → onDestroy)
 *   - Camera authority (EditorFly vs RuntimeScript)
 *   - Input routing rules
 *   - Time / deltaTime bookkeeping
 */

#pragma once

#include <string>
#include <functional>
#include <memory>
#include <utility>
#include "SimulationExecutionMode.h"

// Forward declarations
class Scene;
class LuaScriptEngine;
class PluginScriptHost;
class ScriptEngine;

namespace Hephaestus {

struct RenderCamera;           // EngineFrameRenderer.h
struct EditorCamera;           // EditorContext.h
class EditorCameraController;  // EditorCameraController.h

// ─────────────────────────────────────────────────────────────────────────────
// Engine State
// ─────────────────────────────────────────────────────────────────────────────

enum class PlayState {
    Editor,   ///< Normal editing – scripts do NOT run
    Playing,  ///< Play mode active – scripts run every frame
    Paused    ///< Play mode but frozen – scripts do NOT tick
};

// ─────────────────────────────────────────────────────────────────────────────
// Camera Authority
// ─────────────────────────────────────────────────────────────────────────────

enum class CameraMode {
    EditorFly,       ///< Standard editor camera (fly/orbit/pan)
    RuntimeScript    ///< A Lua script is driving the camera via Camera.setActive(true)
};

// ─────────────────────────────────────────────────────────────────────────────
// Runtime Camera Controller
//   Bridges Lua Camera.* API → engine render camera.
// ─────────────────────────────────────────────────────────────────────────────

struct RuntimeCameraState {
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float pitch = 0.0f, yaw = 0.0f, roll = 0.0f;   // radians
    float nearPlane = 0.1f, farPlane = 1000.0f;
    float fov = 60.0f;

    static float RadToDeg(float v) { return v * 57.2957795f; }
    static float DegToRad(float v) { return v * 0.0174532925f; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Log callback type – routes engine_log / play-mode messages to UI console
// ─────────────────────────────────────────────────────────────────────────────

using PlayModeLogCallback = std::function<void(const std::string& message)>;

// ─────────────────────────────────────────────────────────────────────────────
// PlayModeManager
// ─────────────────────────────────────────────────────────────────────────────

class PlayModeManager {
public:
    PlayModeManager();
    ~PlayModeManager();

    // Non-copyable
    PlayModeManager(const PlayModeManager&) = delete;
    PlayModeManager& operator=(const PlayModeManager&) = delete;

    // ── Configuration (call before first use) ────────────────────────────────

    /// Set the scene that will be played.
    void SetScene(Scene* scene);

    /// Provide access to the Lua script engine (already initialised elsewhere).
    void SetLuaScriptEngine(LuaScriptEngine* lua);

    /// Provide access to the C++ script engine (already initialised elsewhere).
    void SetCppScriptEngine(ScriptEngine* cpp);

    /// Provide the host that runs scripts written in plugin languages.
    void SetPluginScriptHost(PluginScriptHost* host);

    /// Set the log callback.
    void SetLogCallback(PlayModeLogCallback cb);

    // ── State machine ────────────────────────────────────────────────────────

    PlayState GetState() const { return m_state; }
    bool IsPlaying() const { return m_state == PlayState::Playing; }
    bool IsPaused()  const { return m_state == PlayState::Paused; }
    bool IsEditor()  const { return m_state == PlayState::Editor; }
    bool IsRealtime() const { return m_executionMode.IsRealtime(); }
    bool ShouldAdvanceRealtime() const { return m_executionMode.ShouldAdvanceRealtime(); }
    void SetRealtime(bool realtime) { m_executionMode.SetRealtime(realtime); }

    /// Enter play mode.  Snapshots the scene, initialises scripts, transitions camera.
    /// @return true on success; false if already playing or scene is null.
    /// @param afterSnapshot Runs on the scene after the snapshot and before scripts wake, so its changes are
    ///        seen by scripts but undone on Stop.
    bool StartPlayMode(const std::function<void(Scene&)>& afterSnapshot = {});

    /// Freeze / unfreeze the play loop.
    void TogglePause();

    /// Exit play mode.  Tears down scripts, restores scene, restores editor camera.
    void StopPlayMode();

    // ── Per-frame update  (call from your tick / game loop) ──────────────────

    /// Record one simulated frame; the host runs it with Hephaestus::RunSimulationStep.
    void AdvanceFrameTime(float dt);

    /// Gameplay-facing request; accepted requests are processed by the host at
    /// the boundary after script callbacks return and before physics advances.
    /// scenePath names another scene to load (a project path); empty reloads the current one.
    bool RequestSceneReload(const std::string& scenePath = {});
    bool HasPendingSceneReload() const { return m_sceneReloadRequested; }
    using SceneReloadCallback = std::function<bool(const std::string& scenePath, std::string& outError)>;
    void SetSceneReloadCallback(SceneReloadCallback callback) {
        m_sceneReloadCallback = std::move(callback);
    }
    /// Consume the request and invoke the host-owned teardown/load sequence.
    /// Returns true when a request was consumed, including controlled failure.
    bool ProcessSceneReload();

    // ── Camera authority ─────────────────────────────────────────────────────

    CameraMode GetCameraMode() const { return m_cameraMode; }

    /// Called from Lua binding: Camera.setActive(enabled).
    void SetScriptCameraActive(bool active);
    bool IsScriptCameraActive() const { return m_cameraMode == CameraMode::RuntimeScript; }

    RuntimeCameraState& GetRuntimeCamera() { return m_runtimeCamera; }
    const RuntimeCameraState& GetRuntimeCamera() const { return m_runtimeCamera; }

    /// Write the runtime camera state into a RenderCamera (for the renderer).
    void ApplyRuntimeCameraToRender(RenderCamera& out) const;

    /// After LateUpdate, copy the active CameraComponent world pose into the runtime camera.
    /// Returns false when no active camera exists; the detached runtime pose is left unchanged.
    bool SyncRuntimeCameraFromActiveComponent();

    /// Snapshot the current editor camera so we can restore it on stop.
    void SaveEditorCamera(const EditorCamera& cam);

    /// Restore the editor camera after stop.
    void RestoreEditorCamera(EditorCamera& cam) const;

    // ── Time ─────────────────────────────────────────────────────────────────

    float GetDeltaTime() const { return m_deltaTime; }
    float GetTimeSincePlay() const { return m_timeSincePlay; }

    // ── Input routing helpers ────────────────────────────────────────────────

    /// Returns true when runtime scripts should receive input events.
    bool ShouldRouteInputToRuntime() const;

    /// Returns true when editor gizmo / selection should be suppressed.
    bool ShouldSuppressEditorInteraction() const;

    /// Scripts can request that play mode be stopped (e.g. Application.quit()).
    void RequestStop();
    bool IsStopRequested() const { return m_stopRequested; }
    void ClearStopRequest() { m_stopRequested = false; }

private:
    void Log(const std::string& msg);
    void InitializeRuntimeCameraFromScene();

    // State
    PlayState  m_state = PlayState::Editor;
    CameraMode m_cameraMode = CameraMode::EditorFly;
    SimulationExecutionMode m_executionMode;

    // Scene
    Scene* m_scene = nullptr;
    std::string m_sceneSnapshot;             // serialised editor scene
    EditorCamera* m_savedEditorCamera = nullptr;
    float m_savedCamPosX = 0, m_savedCamPosY = 2, m_savedCamPosZ = 5;
    float m_savedCamYaw = -90, m_savedCamPitch = -15;
    float m_savedCamNear = 0.1f, m_savedCamFar = 10000.0f;
    float m_savedCamFov = 60.0f;
    float m_savedCamSpeed = 5.0f;
    float m_savedCamSensitivity = 0.1f;

    // Scripts
    LuaScriptEngine* m_luaEngine = nullptr;
    ScriptEngine*    m_cppEngine = nullptr;
    PluginScriptHost* m_pluginScripts = nullptr;

    // Runtime camera
    RuntimeCameraState m_runtimeCamera;

    // Time
    float m_deltaTime = 0.0f;
    float m_timeSincePlay = 0.0f;

    // Stop request (set by scripts via Application.quit())
    bool m_stopRequested = false;
    bool m_sceneReloadRequested = false;
    bool m_processingSceneReload = false;
    std::string m_pendingScenePath;
    // The edited scene's source path; a Scene.load during Play changes the scene's, so Stop puts it back.
    std::string m_editSceneSourcePath;
    SceneReloadCallback m_sceneReloadCallback;

    // Logging
    PlayModeLogCallback m_logCb;
};

} // namespace Hephaestus
