/**
 * @file EditorContext.h
 * @brief Shared editor state - single source of truth for editor mode.
 *
 * This class holds editor-wide state including:
 * - Active scene pointer
 * - Current entity selection
 * - Scene file path
 * - Dirty flag
 * 
 * Used by both Qt editor and legacy ImGui editor to ensure consistent state.
 */

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_set>
#include <vector>
#include <entt/entt.hpp>

// Forward declarations
class Scene;

namespace Hephaestus {

/**
 * @brief Editor camera data - position, orientation, projection settings.
 */
struct EditorCamera {
    float positionX = 0.0f;
    float positionY = 2.0f;
    float positionZ = 5.0f;
    float yawDeg = -90.0f;
    float pitchDeg = -15.0f;
    float nearPlane = 0.1f;
    float farPlane = 10000.0f;
    float fovDeg = 60.0f;
    float moveSpeed = 5.0f;
    float lookSensitivity = 0.1f;
};

/**
 * @brief Gizmo operation modes for transform manipulation.
 */
enum class GizmoMode {
    None = 0,
    Translate,
    Rotate,
    Scale
};

/**
 * @brief Gizmo coordinate space for transform manipulation.
 */
enum class GizmoSpace {
    World = 0,
    Local
};

/**
 * @brief Snapping settings for gizmo manipulation.
 */
struct GizmoSnapSettings {
    bool enableSnap = false;
    float translateSnap = 1.0f;     // World units
    float rotateSnapDeg = 15.0f;    // Degrees
    float scaleSnap = 0.1f;         // Scale increment
};

/**
 * @brief Editor viewport display toggles.
 */
struct EditorViewportSettings {
    bool showGrid = true;
    bool showGizmo = true;
    bool showSelectionOutline = true;
    bool showShadows = true;
    bool showSkybox = true;
    bool showLightBillboards = true;
    int shadingMode = 0; // 0=Lit, 1=Unlit, 2=Wireframe (matches ViewShadingMode)
};

/**
 * @brief Callback types for editor state changes.
 */
using SceneChangedCallback = std::function<void()>;
using SelectionChangedCallback = std::function<void(entt::entity)>;
using DirtyChangedCallback = std::function<void(bool)>;
using SceneEditedCallback = std::function<void()>;

/**
 * @brief Single source of truth for editor state.
 *
 * EditorContext owns the active scene and tracks selection, file path,
 * and dirty state. All editor UI should read/write through this context.
 */
class EditorContext {
public:
    EditorContext();
    ~EditorContext();

    // Non-copyable
    EditorContext(const EditorContext&) = delete;
    EditorContext& operator=(const EditorContext&) = delete;

    // ─────────────────────────────────────────────────────────────────────────
    // Scene Management
    // ─────────────────────────────────────────────────────────────────────────
    
    /**
     * @brief Create a new empty scene.
     * @param name Name for the new scene (default: "New Scene")
     */
    void NewScene(const std::string& name = "New Scene");

    /**
     * @brief Load a scene from file.
     * @param filepath Path to the scene file (.hpscene)
     * @return true if loading succeeded
     */
    bool LoadScene(const std::string& filepath);

    /**
     * @brief Save the active scene to its current path.
     * @return true if saving succeeded (false if no path set)
     */
    bool SaveScene();

    /**
     * @brief Save the active scene to a new path.
     * @param filepath Destination path
     * @return true if saving succeeded
     */
    bool SaveSceneAs(const std::string& filepath);

    /**
     * @brief Use an externally-owned scene instead of the internally-managed one.
     *
     * The caller retains ownership. Pass nullptr to revert to internal scene.
     * Useful for sub-editors (e.g. Prefab Editor) that manage their own scene
     * but need the engine's rendering, gizmo, and selection infrastructure.
     */
    void SetExternalScene(Scene* scene);

    /**
     * @brief Get the active scene (may be null).
     */
    Scene* GetActiveScene() { return m_externalScene ? m_externalScene : m_activeScene.get(); }
    const Scene* GetActiveScene() const { return m_externalScene ? m_externalScene : m_activeScene.get(); }

    /**
     * @brief Check if there is an active scene.
     */
    bool HasActiveScene() const { return m_externalScene || m_activeScene != nullptr; }

    /**
     * @brief Get the current scene file path (empty if new/unsaved).
     */
    const std::string& GetScenePath() const { return m_scenePath; }

    // ─────────────────────────────────────────────────────────────────────────
    // Selection Management
    // ─────────────────────────────────────────────────────────────────────────
    
    /**
     * @brief Select an entity (clears previous selection).
     * @param entity Entity to select (entt::null to clear)
     */
    void SelectEntity(entt::entity entity);

    /**
     * @brief Set multiple selected entities (e.g. outliner). Primary drives gizmo and details.
     */
    void SelectEntities(const std::vector<entt::entity>& entities, entt::entity primary);

    /**
     * @brief Viewport click selection: plain click replaces selection; Ctrl toggles; Shift adds or focuses.
     * @param picked Entity under cursor (entt::null if none / miss). Invalid handles are ignored.
     * @param ctrlHeld Qt Ctrl (toggle in/out of multi-selection).
     * @param shiftHeld Qt Shift (add to selection, or set primary if already selected).
     */
    void SelectFromViewportClick(entt::entity picked, bool ctrlHeld, bool shiftHeld);

    /**
     * @brief Clear the current selection.
     */
    void ClearSelection();

    /**
     * @brief Get the currently selected entity.
     */
    entt::entity GetSelectedEntity() const { return m_selectedEntity; }

    /**
     * @brief All entities marked selected in the active scene (empty if no scene).
     */
    std::vector<entt::entity> GetSelectedEntities() const;

    /**
     * @brief Check if an entity is selected.
     */
    bool HasSelection() const { return m_selectedEntity != entt::null; }

    // ─────────────────────────────────────────────────────────────────────────
    // Dirty Flag
    // ─────────────────────────────────────────────────────────────────────────
    
    /**
     * @brief Mark the scene as modified.
     */
    void MarkDirty();

    /**
     * @brief Clear the dirty flag (typically after save).
     */
    void ClearDirty();

    /**
     * @brief Check if the scene has unsaved changes.
     */
    bool IsDirty() const { return m_isDirty; }

    // ─────────────────────────────────────────────────────────────────────────
    // Editor Camera
    // ─────────────────────────────────────────────────────────────────────────
    
    EditorCamera& GetCamera() { return m_camera; }
    const EditorCamera& GetCamera() const { return m_camera; }

    // ─────────────────────────────────────────────────────────────────────────
    // Gizmo State
    // ─────────────────────────────────────────────────────────────────────────
    
    GizmoMode GetGizmoMode() const { return m_gizmoMode; }
    void SetGizmoMode(GizmoMode mode) { m_gizmoMode = mode; }
    
    GizmoSpace GetGizmoSpace() const { return m_gizmoSpace; }
    void SetGizmoSpace(GizmoSpace space) { m_gizmoSpace = space; }
    void ToggleGizmoSpace() { 
        m_gizmoSpace = (m_gizmoSpace == GizmoSpace::World) ? GizmoSpace::Local : GizmoSpace::World; 
    }
    
    GizmoSnapSettings& GetSnapSettings() { return m_snapSettings; }
    const GizmoSnapSettings& GetSnapSettings() const { return m_snapSettings; }

    // ─────────────────────────────────────────────────────────────────────────
    // Viewport Settings
    // ─────────────────────────────────────────────────────────────────────────
    
    EditorViewportSettings& GetViewportSettings() { return m_viewportSettings; }
    const EditorViewportSettings& GetViewportSettings() const { return m_viewportSettings; }

    bool IsEditorHidden(entt::entity entity) const {
        return m_editorHidden.contains(static_cast<uint32_t>(entity));
    }
    bool IsEditorLocked(entt::entity entity) const {
        return m_editorLocked.contains(static_cast<uint32_t>(entity));
    }
    void SetEditorHidden(entt::entity entity, bool hidden);
    void SetEditorLocked(entt::entity entity, bool locked);
    void ShowAllEditorEntities() { m_editorHidden.clear(); m_isolated.clear(); }
    void IsolateEntities(const std::vector<entt::entity>& entities);
    void ExitIsolation() { m_isolated.clear(); }
    bool IsIsolationActive() const { return !m_isolated.empty(); }
    const std::unordered_set<uint32_t>& GetEditorHiddenIds() const { return m_editorHidden; }
    const std::unordered_set<uint32_t>& GetEditorLockedIds() const { return m_editorLocked; }
    const std::unordered_set<uint32_t>& GetIsolatedIds() const { return m_isolated; }
    void SetEditorStateIds(std::unordered_set<uint32_t> hidden,
                           std::unordered_set<uint32_t> locked);

    // ─────────────────────────────────────────────────────────────────────────
    // Callbacks (for UI updates)
    // ─────────────────────────────────────────────────────────────────────────
    
    void SetSceneChangedCallback(SceneChangedCallback cb) { m_onSceneChanged = std::move(cb); }
    void SetSelectionChangedCallback(SelectionChangedCallback cb) { m_onSelectionChanged = std::move(cb); }
    void SetDirtyChangedCallback(DirtyChangedCallback cb) { m_onDirtyChanged = std::move(cb); }
    void SetSceneEditedCallback(SceneEditedCallback cb) { m_onSceneEdited = std::move(cb); }

private:
    std::unique_ptr<Scene> m_activeScene;
    Scene* m_externalScene = nullptr;
    std::string m_scenePath;
    entt::entity m_selectedEntity = entt::null;
    bool m_isDirty = false;
    EditorCamera m_camera;
    
    // Gizmo state
    GizmoMode m_gizmoMode = GizmoMode::Translate;
    GizmoSpace m_gizmoSpace = GizmoSpace::World;
    GizmoSnapSettings m_snapSettings;
    
    // Viewport settings
    EditorViewportSettings m_viewportSettings;
    std::unordered_set<uint32_t> m_editorHidden;
    std::unordered_set<uint32_t> m_editorLocked;
    std::unordered_set<uint32_t> m_isolated;

    // Callbacks
    SceneChangedCallback m_onSceneChanged;
    SelectionChangedCallback m_onSelectionChanged;
    DirtyChangedCallback m_onDirtyChanged;
    SceneEditedCallback m_onSceneEdited;
};

} // namespace Hephaestus
