/**
 * @file PlayModeManager.cpp
 * @brief Implementation of the central Play Mode manager.
 */

#include "PlayModeManager.h"
#include "../Scene/Scene.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/SceneSerializer.h"
#include "../Scripting/LuaScriptEngine.h"
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

bool PlayModeManager::StartPlayMode() {
    if (m_state != PlayState::Editor) {
        Log("Warning: StartPlayMode called but state is not Editor – ignoring.");
        return false;
    }
    if (!m_scene) {
        Log("Error: Cannot start play mode without a scene.");
        return false;
    }

    Log("Play Mode started");

    // ── 1. Snapshot scene ──────────────────────────────────────────────────
    {
        SceneSerializer serializer(*m_scene);
        m_sceneSnapshot = serializer.SerializeToString();
        if (m_sceneSnapshot.empty()) {
            Log("Warning: Scene snapshot is empty – restore may not work.");
        }
    }

    // ── 2. Transition state ────────────────────────────────────────────────
    m_state = PlayState::Playing;
    m_timeSincePlay = 0.0f;
    m_deltaTime = 0.0f;

    // ── 3. Camera: find scene camera entity or keep editor camera ──────────
    m_cameraMode = CameraMode::EditorFly;   // default until script takes over

    // Look for a CameraComponent entity to use as runtime starting camera
    Entity activeCameraEntity;
    Entity firstCameraEntity;
    auto cameraView = m_scene->GetEntitiesWith<CameraComponent, TransformComponent>();
    for (auto entityHandle : cameraView) {
        Entity entity(entityHandle, m_scene);
        auto& cam = entity.GetComponent<CameraComponent>();
        if (!firstCameraEntity) firstCameraEntity = entity;
        if (cam.isActive) { activeCameraEntity = entity; break; }
    }

    Entity runtimeCamera = activeCameraEntity ? activeCameraEntity : firstCameraEntity;
    if (runtimeCamera) {
        auto& camComp = runtimeCamera.GetComponent<CameraComponent>();
        auto& camTransform = runtimeCamera.GetComponent<TransformComponent>();
        m_runtimeCamera.posX = camTransform.position.x;
        m_runtimeCamera.posY = camTransform.position.y;
        m_runtimeCamera.posZ = camTransform.position.z;
        // TransformComponent stores Euler rotation in radians; runtime camera also uses radians.
        m_runtimeCamera.pitch = camTransform.rotation.x;
        m_runtimeCamera.yaw   = camTransform.rotation.y;
        m_runtimeCamera.roll  = camTransform.rotation.z;
        m_runtimeCamera.nearPlane = camComp.nearPlane;
        m_runtimeCamera.farPlane  = camComp.farPlane;
        m_runtimeCamera.fov       = camComp.fov;
    } else {
        // Fallback: copy editor camera position into runtime camera
        m_runtimeCamera.posX  = m_savedCamPosX;
        m_runtimeCamera.posY  = m_savedCamPosY;
        m_runtimeCamera.posZ  = m_savedCamPosZ;
        m_runtimeCamera.pitch = RuntimeCameraState::DegToRad(m_savedCamPitch);
        m_runtimeCamera.yaw   = RuntimeCameraState::DegToRad(m_savedCamYaw);
        m_runtimeCamera.roll  = 0.0f;
        m_runtimeCamera.nearPlane = m_savedCamNear;
        m_runtimeCamera.farPlane  = m_savedCamFar;
        m_runtimeCamera.fov       = m_savedCamFov;
    }

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

    // ── 1. Tear down scripts ───────────────────────────────────────────────
    if (m_luaEngine) {
        m_luaEngine->OnSimulationStopped();
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
        m_sceneSnapshot.clear();
    }

    // ── 4. Reset time ──────────────────────────────────────────────────────
    m_deltaTime = 0.0f;
    m_timeSincePlay = 0.0f;

    // ── 5. Transition state ────────────────────────────────────────────────
    m_state = PlayState::Editor;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick  (call once per frame from your render/update loop)
// ─────────────────────────────────────────────────────────────────────────────

void PlayModeManager::RequestStop() {
    m_stopRequested = true;
}

void PlayModeManager::Tick(float dt) {
    m_deltaTime = dt;

    if (m_state != PlayState::Playing) {
        return;  // nothing to tick in editor or paused
    }

    m_timeSincePlay += dt;

    // ── Update C++ scripts ─────────────────────────────────────────────────
    if (m_cppEngine) {
        m_cppEngine->UpdateScripts(dt);
        m_cppEngine->LateUpdateScripts(dt);
    }

    // ── Update Lua scripts ─────────────────────────────────────────────────
    if (m_luaEngine && m_scene) {
        m_luaEngine->Update(*m_scene, dt);
        m_luaEngine->LateUpdate(*m_scene, dt);
    }
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

void PlayModeManager::ApplyRuntimeCameraToRender(RenderCamera& out) const {
    out.positionX = m_runtimeCamera.posX;
    out.positionY = m_runtimeCamera.posY;
    out.positionZ = m_runtimeCamera.posZ;
    out.yawDeg    = RuntimeCameraState::RadToDeg(m_runtimeCamera.yaw);
    out.pitchDeg  = RuntimeCameraState::RadToDeg(m_runtimeCamera.pitch);
    out.nearPlane = m_runtimeCamera.nearPlane;
    out.farPlane  = m_runtimeCamera.farPlane;
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
