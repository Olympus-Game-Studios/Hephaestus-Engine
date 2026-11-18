/**
 * @file PlayModeManager.cpp
 * @brief Implementation of the central Play Mode manager.
 */

#include "PlayModeManager.h"
#include "../Scene/ActiveCameraSync.h"
#include "../Scene/Scene.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/SceneSerializer.h"
#include "../Scripting/LuaScriptEngine.h"
#include "../Scripting/PluginScriptHost.h"
#include "../Scripting/ScriptEngine.h"
#include "../Scripting/LuaScriptComponent.h"
#include "../Gfx/EngineFrameRenderer.h"
#include "EditorContext.h"

#include <iostream>
#include <cmath>

namespace Hephaestus {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

PlayModeManager::PlayModeManager() = default;

PlayModeManager::~PlayModeManager() {
    if (m_state != PlayState::Editor) {
        // Force-stop if we're still playing when the manager is destroyed
        StopPlayMode();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

void PlayModeManager::SetScene(Scene* scene) {
    m_scene = scene;
}

void PlayModeManager::SetLuaScriptEngine(LuaScriptEngine* lua) {
    m_luaEngine = lua;
}

void PlayModeManager::SetCppScriptEngine(ScriptEngine* cpp) {
    m_cppEngine = cpp;
}

void PlayModeManager::SetPluginScriptHost(PluginScriptHost* host) {
    m_pluginScripts = host;
}

void PlayModeManager::SetLogCallback(PlayModeLogCallback cb) {
    m_logCb = std::move(cb);
}

// ─────────────────────────────────────────────────────────────────────────────
// Logging helper
// ─────────────────────────────────────────────────────────────────────────────

void PlayModeManager::Log(const std::string& msg) {
    std::cout << "[PlayMode] " << msg << std::endl;
    if (m_logCb) {
        m_logCb(msg);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// StartPlayMode
// ─────────────────────────────────────────────────────────────────────────────

bool PlayModeManager::StartPlayMode(const std::function<void(Scene&)>& afterSnapshot) {
    if (m_state != PlayState::Editor) {
        Log("Warning: StartPlayMode called but state is not Editor – ignoring.");
        return false;
    }
    if (!m_scene) {
        Log("Error: Cannot start play mode without a scene.");
        return false;
    }

    Log("Play Mode started");
    m_sceneReloadRequested = false;
    m_processingSceneReload = false;
    m_pendingScenePath.clear();

    // ── 1. Snapshot scene ──────────────────────────────────────────────────
    {
        m_editSceneSourcePath = m_scene->GetSourceAssetPath();
        SceneSerializer serializer(*m_scene);
        m_sceneSnapshot = serializer.SerializeToString();
        if (m_sceneSnapshot.empty()) {
            Log("Warning: Scene snapshot is empty – restore may not work.");
        }
    }
    if (afterSnapshot) {
        afterSnapshot(*m_scene);
    }

    // ── 2. Transition state ────────────────────────────────────────────────
    m_state = PlayState::Playing;
    m_executionMode.Reset();
    m_timeSincePlay = 0.0f;
    m_deltaTime = 0.0f;

    // ── 3. Camera: find scene camera entity or keep editor camera ──────────
    m_cameraMode = CameraMode::EditorFly;
    InitializeRuntimeCameraFromScene();

    // ── 4. Notify script engines ───────────────────────────────────────────
    if (m_luaEngine) {
        m_luaEngine->OnSimulationStarted();
    }
    if (m_cppEngine) {
        m_cppEngine->AwakeScripts();
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TogglePause
// ─────────────────────────────────────────────────────────────────────────────

void PlayModeManager::TogglePause() {
    if (m_state == PlayState::Playing) {
        m_state = PlayState::Paused;
        Log("Play Mode paused");
    } else if (m_state == PlayState::Paused) {
        m_state = PlayState::Playing;
        Log("Play Mode resumed");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// StopPlayMode
// ─────────────────────────────────────────────────────────────────────────────

void PlayModeManager::StopPlayMode() {
    if (m_state == PlayState::Editor) {
        return;  // nothing to do
    }

    Log("Play Mode stopped");
    m_sceneReloadRequested = false;
    m_processingSceneReload = false;
    m_pendingScenePath.clear();

    // ── 1. Tear down scripts ───────────────────────────────────────────────
    if (m_luaEngine) {
        m_luaEngine->OnSimulationStopped();
    }
    if (m_pluginScripts) {
        m_pluginScripts->OnSimulationStopped();
    }
    if (m_cppEngine) {
        m_cppEngine->DestroyScripts();
    }

    // ── 2. Force camera back to editor ─────────────────────────────────────
    m_cameraMode = CameraMode::EditorFly;

    // ── 3. Restore scene from snapshot ─────────────────────────────────────
    if (m_scene && !m_sceneSnapshot.empty()) {
        m_scene->Clear();
        bool ok = SceneSerializer::DeserializeFromString(*m_scene, m_sceneSnapshot);
        if (!ok) {
            Log("Error: Failed to restore scene from snapshot!");
        }
        m_scene->SetSourceAssetPath(m_editSceneSourcePath);
        m_sceneSnapshot.clear();
    }

    // ── 4. Reset time ──────────────────────────────────────────────────────
    m_deltaTime = 0.0f;
    m_timeSincePlay = 0.0f;

    // ── 5. Transition state ────────────────────────────────────────────────
    m_state = PlayState::Editor;
    m_executionMode.Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick  (call once per frame from your render/update loop)
// ─────────────────────────────────────────────────────────────────────────────

void PlayModeManager::RequestStop() {
    m_stopRequested = true;
}

bool PlayModeManager::RequestSceneReload(const std::string& scenePath)
{
    if (m_processingSceneReload) {
        return false;
    }
    if (m_state == PlayState::Editor) {
        Log("Scene.reload rejected: Play Mode is not active");
        return false;
    }
    if (!m_scene || (scenePath.empty() && !m_scene->IsReloadable())) {
        Log("Scene.reload rejected: current scene has no authored source asset");
        return false;
    }
    if (!m_sceneReloadCallback) {
        Log("Scene.reload rejected: runtime scene reload is unavailable on this host");
        return false;
    }
    if (m_sceneReloadRequested) {
        return false;
    }
    m_sceneReloadRequested = true;
    m_pendingScenePath = scenePath;
    Log(scenePath.empty() ? "Scene.reload requested" : "Scene.load requested: " + scenePath);
    return true;
}

bool PlayModeManager::ProcessSceneReload()
{
    if (!m_sceneReloadRequested) {
        return false;
    }
    m_sceneReloadRequested = false;
    m_processingSceneReload = true;
    const std::string scenePath = std::move(m_pendingScenePath);
    m_pendingScenePath.clear();

    std::string error;
    bool success = false;
    try {
        success = m_sceneReloadCallback && m_sceneReloadCallback(scenePath, error);
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "unknown scene reload failure";
    }
    m_processingSceneReload = false;

    if (!success) {
        if (error.empty()) error = "host failed to reconstruct the authored scene";
        Log("Scene.reload failed: " + error);
        // The host may already have torn down the old scene. Freeze gameplay
        // rather than advancing a partial/empty world after a failed reload.
        m_state = PlayState::Paused;
        return true;
    }

    m_timeSincePlay = 0.0f;
    m_deltaTime = 0.0f;
    m_cameraMode = CameraMode::EditorFly;
    InitializeRuntimeCameraFromScene();
    Log("Scene.reload completed: " + m_scene->GetSourceAssetPath());
    return true;
}

void PlayModeManager::InitializeRuntimeCameraFromScene()
{
    Entity activeCameraEntity;
    Entity firstCameraEntity;
    if (m_scene) {
        auto cameraView = m_scene->GetEntitiesWith<CameraComponent, TransformComponent>();
        for (auto entityHandle : cameraView) {
            Entity entity(entityHandle, m_scene);
            auto& camera = entity.GetComponent<CameraComponent>();
            if (!firstCameraEntity) firstCameraEntity = entity;
            if (camera.isActive) { activeCameraEntity = entity; break; }
        }
    }

    Entity runtimeCamera = activeCameraEntity ? activeCameraEntity : firstCameraEntity;
    ActiveCameraView activeView;
    if (m_scene && runtimeCamera && TryMakeCameraView(*m_scene, runtimeCamera, activeView)) {
        m_runtimeCamera.posX = activeView.position.x;
        m_runtimeCamera.posY = activeView.position.y;
        m_runtimeCamera.posZ = activeView.position.z;
        m_runtimeCamera.pitch = activeView.pitchRadians;
        m_runtimeCamera.yaw = activeView.yawRadians;
        m_runtimeCamera.roll = activeView.rollRadians;
        m_runtimeCamera.nearPlane = activeView.nearPlane;
        m_runtimeCamera.farPlane = activeView.farPlane;
        m_runtimeCamera.fov = activeView.fovDegrees;
    } else {
        m_runtimeCamera.posX = m_savedCamPosX;
        m_runtimeCamera.posY = m_savedCamPosY;
        m_runtimeCamera.posZ = m_savedCamPosZ;
        m_runtimeCamera.pitch = RuntimeCameraState::DegToRad(m_savedCamPitch);
        m_runtimeCamera.yaw = RuntimeCameraState::DegToRad(m_savedCamYaw);
        m_runtimeCamera.roll = 0.0f;
        m_runtimeCamera.nearPlane = m_savedCamNear;
        m_runtimeCamera.farPlane = m_savedCamFar;
        m_runtimeCamera.fov = m_savedCamFov;
    }
}

void PlayModeManager::AdvanceFrameTime(float dt) {
    if (m_state == PlayState::Editor) {
        return;
    }
    m_deltaTime = dt;
    m_timeSincePlay += dt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera authority
// ─────────────────────────────────────────────────────────────────────────────

void PlayModeManager::SetScriptCameraActive(bool active) {
    if (m_state == PlayState::Editor) {
        // Scripts cannot take camera control outside of play mode.
        return;
    }

    if (active) {
        m_cameraMode = CameraMode::RuntimeScript;
        Log("Camera authority: RuntimeScript (script took control)");
    } else {
        m_cameraMode = CameraMode::EditorFly;
        Log("Camera authority: reverted to default (script released camera)");
    }
}

bool PlayModeManager::SyncRuntimeCameraFromActiveComponent() {
    if (!m_scene) {
        return false;
    }
    const std::optional<ActiveCameraView> view = TryReadActiveCameraView(*m_scene);
    if (!view) {
        return false;
    }
    m_runtimeCamera.posX = view->position.x;
    m_runtimeCamera.posY = view->position.y;
    m_runtimeCamera.posZ = view->position.z;
    m_runtimeCamera.pitch = view->pitchRadians;
    m_runtimeCamera.yaw = view->yawRadians;
    m_runtimeCamera.roll = view->rollRadians;
    m_runtimeCamera.nearPlane = view->nearPlane;
    m_runtimeCamera.farPlane = view->farPlane;
    m_runtimeCamera.fov = view->fovDegrees;
    return true;
}

void PlayModeManager::ApplyRuntimeCameraToRender(RenderCamera& out) const {
    out.positionX = m_runtimeCamera.posX;
    out.positionY = m_runtimeCamera.posY;
    out.positionZ = m_runtimeCamera.posZ;
    out.yawDeg    = RuntimeCameraState::RadToDeg(m_runtimeCamera.yaw);
    out.pitchDeg  = RuntimeCameraState::RadToDeg(m_runtimeCamera.pitch);
    out.rollDeg   = RuntimeCameraState::RadToDeg(m_runtimeCamera.roll);
    out.nearPlane = m_runtimeCamera.nearPlane;
    out.farPlane  = m_runtimeCamera.farPlane;
    out.fovDeg    = m_runtimeCamera.fov > 0.1f ? m_runtimeCamera.fov : 60.0f;
    out.projectionMode = ProjectionMode::Perspective;
}

void PlayModeManager::SaveEditorCamera(const EditorCamera& cam) {
    m_savedCamPosX = cam.positionX;
    m_savedCamPosY = cam.positionY;
    m_savedCamPosZ = cam.positionZ;
    m_savedCamYaw  = cam.yawDeg;
    m_savedCamPitch = cam.pitchDeg;
    m_savedCamNear  = cam.nearPlane;
    m_savedCamFar   = cam.farPlane;
    m_savedCamFov   = cam.fovDeg;
    m_savedCamSpeed = cam.moveSpeed;
    m_savedCamSensitivity = cam.lookSensitivity;
}

void PlayModeManager::RestoreEditorCamera(EditorCamera& cam) const {
    cam.positionX = m_savedCamPosX;
    cam.positionY = m_savedCamPosY;
    cam.positionZ = m_savedCamPosZ;
    cam.yawDeg    = m_savedCamYaw;
    cam.pitchDeg  = m_savedCamPitch;
    cam.nearPlane = m_savedCamNear;
    cam.farPlane  = m_savedCamFar;
    cam.fovDeg    = m_savedCamFov;
    cam.moveSpeed = m_savedCamSpeed;
    cam.lookSensitivity = m_savedCamSensitivity;
}

// ─────────────────────────────────────────────────────────────────────────────
// Input routing
// ─────────────────────────────────────────────────────────────────────────────

bool PlayModeManager::ShouldRouteInputToRuntime() const {
    return m_state == PlayState::Playing;
}

bool PlayModeManager::ShouldSuppressEditorInteraction() const {
    return m_state != PlayState::Editor;
}

} // namespace Hephaestus
