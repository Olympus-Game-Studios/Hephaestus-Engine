/**
 * @file EditorContext.cpp
 * @brief Implementation of shared editor state.
 */

#include "EditorContext.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneSerializer.h"
#include "../Scene/PrefabOverrideSystem.h"
#include <algorithm>
#include <iostream>
#include <filesystem>

namespace Hephaestus {

EditorContext::EditorContext()
{
    std::cout << "[EditorContext] Created\n";
}

EditorContext::~EditorContext()
{
    std::cout << "[EditorContext] Destroyed\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene Management
// ─────────────────────────────────────────────────────────────────────────────

void EditorContext::SetExternalScene(Scene* scene)
{
    ClearSelection();
    m_externalScene = scene;

    if (m_onSceneChanged) {
        m_onSceneChanged();
    }
}

void EditorContext::NewScene(const std::string& name)
{
    // Clear selection first
    ClearSelection();
    
    // Create new scene
    m_activeScene = std::make_unique<Scene>(name);
    m_scenePath.clear();
    m_isDirty = false;
    
    std::cout << "[EditorContext] New scene created: " << name << "\n";
    
    // Notify listeners
    if (m_onSceneChanged) {
        m_onSceneChanged();
    }
    if (m_onDirtyChanged) {
        m_onDirtyChanged(m_isDirty);
    }
}

bool EditorContext::LoadScene(const std::string& filepath)
{
    // Check file exists
    if (!std::filesystem::exists(filepath)) {
        std::cerr << "[EditorContext] Scene file not found: " << filepath << "\n";
        return false;
    }
    
    // Clear selection
    ClearSelection();
    
    // Create new scene object
    auto newScene = std::make_unique<Scene>();
    
    // Deserialize
    std::string error;
    if (!SceneSerializer::Deserialize(*newScene, filepath, error)) {
        std::cerr << "[EditorContext] Failed to load scene: " << error << "\n";
        return false;
    }
    
    // Success - swap in the new scene
    m_activeScene = std::move(newScene);
    m_scenePath = filepath;
    m_isDirty = false;
    
    std::cout << "[EditorContext] Scene loaded: " << filepath << "\n";
    
    // Notify listeners
    if (m_onSceneChanged) {
        m_onSceneChanged();
    }
    if (m_onDirtyChanged) {
        m_onDirtyChanged(m_isDirty);
    }
    
    return true;
}

bool EditorContext::SaveScene()
{
    if (m_scenePath.empty()) {
        std::cerr << "[EditorContext] No scene path set, use SaveSceneAs\n";
        return false;
    }
    
    return SaveSceneAs(m_scenePath);
}

bool EditorContext::SaveSceneAs(const std::string& filepath)
{
    Scene* scene = GetActiveScene();
    if (!scene) {
        std::cerr << "[EditorContext] No active scene to save\n";
        return false;
    }

    PrefabOverrideSystem::SyncAllInstances(*scene);

    SceneSerializer serializer(*scene);
    std::string error;
    if (!serializer.Serialize(filepath, error)) {
        std::cerr << "[EditorContext] Failed to save scene: " << error << "\n";
        return false;
    }
    
    // Update path and clear dirty
    m_scenePath = filepath;
    m_isDirty = false;
    
    std::cout << "[EditorContext] Scene saved: " << filepath << "\n";
    
    if (m_onDirtyChanged) {
        m_onDirtyChanged(m_isDirty);
    }
    
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection Management
// ─────────────────────────────────────────────────────────────────────────────

void EditorContext::SelectEntity(entt::entity entity)
{
    if (m_activeScene) {
        if (entity != entt::null) {
            const auto sel = m_activeScene->GetSelectedEntities();
            if (m_selectedEntity == entity && sel.size() == 1 && sel[0]
                && static_cast<entt::entity>(sel[0]) == entity) {
                return;
            }
        } else if (m_selectedEntity == entt::null) {
            const auto sel = m_activeScene->GetSelectedEntities();
            if (sel.empty()) {
                return;
            }
        }
    } else if (m_selectedEntity == entity) {
        return;
    }

    m_selectedEntity = entity;

    // Also update scene's internal selection if scene exists
    if (m_activeScene && entity != entt::null) {
        Entity e(entity, m_activeScene.get());
        m_activeScene->SetSelectedEntity(e);
    } else if (m_activeScene) {
        m_activeScene->ClearSelection();
    }

    if (m_onSelectionChanged) {
        m_onSelectionChanged(entity);
    }
}

void EditorContext::SelectEntities(const std::vector<entt::entity>& entities, entt::entity primary)
{
    if (!m_activeScene) {
        if (m_selectedEntity != entt::null || !entities.empty()) {
            m_selectedEntity = entt::null;
            if (m_onSelectionChanged) {
                m_onSelectionChanged(entt::null);
            }
        }
        return;
    }

    if (entities.empty()) {
        ClearSelection();
        return;
    }

    entt::entity resolvedPrimary = primary;
    if (resolvedPrimary == entt::null || !m_activeScene->GetRegistry().valid(resolvedPrimary)) {
        resolvedPrimary = entt::null;
        for (entt::entity e : entities) {
            if (e != entt::null && m_activeScene->GetRegistry().valid(e)) {
                resolvedPrimary = e;
                break;
            }
        }
    }

    m_activeScene->SetSelectedEntities(entities, resolvedPrimary);
    m_selectedEntity = resolvedPrimary;

    if (m_onSelectionChanged) {
        m_onSelectionChanged(resolvedPrimary);
    }
}

void EditorContext::SelectFromViewportClick(entt::entity picked, bool ctrlHeld, bool shiftHeld)
{
    if (!m_activeScene) {
        return;
    }

    const bool hit = (picked != entt::null) && m_activeScene->GetRegistry().valid(picked);

    if (!ctrlHeld && !shiftHeld) {
        if (hit) {
            SelectEntity(picked);
        } else {
            ClearSelection();
        }
        return;
    }

    if (!hit) {
        return;
    }

    std::vector<entt::entity> cur = GetSelectedEntities();
    const auto it = std::find(cur.begin(), cur.end(), picked);

    if (ctrlHeld) {
        if (it != cur.end()) {
            cur.erase(it);
            if (cur.empty()) {
                ClearSelection();
            } else {
                entt::entity newPrimary = cur.front();
                if (m_selectedEntity != entt::null && m_activeScene->GetRegistry().valid(m_selectedEntity)) {
                    const auto pit = std::find(cur.begin(), cur.end(), m_selectedEntity);
                    if (pit != cur.end()) {
                        newPrimary = m_selectedEntity;
                    }
                }
                SelectEntities(cur, newPrimary);
            }
        } else {
            cur.push_back(picked);
            SelectEntities(cur, picked);
        }
        return;
    }

    // Shift: add to selection, or focus as primary if already selected
    if (it == cur.end()) {
        cur.push_back(picked);
    }
    SelectEntities(cur, picked);
}

std::vector<entt::entity> EditorContext::GetSelectedEntities() const
{
    std::vector<entt::entity> out;
    if (!m_activeScene) {
        return out;
    }
    for (const auto& e : m_activeScene->GetSelectedEntities()) {
        out.push_back(static_cast<entt::entity>(e));
    }
    return out;
}

void EditorContext::ClearSelection()
{
    if (m_selectedEntity == entt::null) {
        return; // Already clear
    }
    
    m_selectedEntity = entt::null;
    
    if (m_activeScene) {
        m_activeScene->ClearSelection();
    }
    
    if (m_onSelectionChanged) {
        m_onSelectionChanged(entt::null);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Dirty Flag
// ─────────────────────────────────────────────────────────────────────────────

void EditorContext::MarkDirty()
{
    const bool wasDirty = m_isDirty;
    m_isDirty = true;
    if (!wasDirty && m_onDirtyChanged) {
        m_onDirtyChanged(true);
    }
    if (m_onSceneEdited) {
        m_onSceneEdited();
    }
}

void EditorContext::ClearDirty()
{
    if (m_isDirty) {
        m_isDirty = false;
        if (m_onDirtyChanged) {
            m_onDirtyChanged(false);
        }
    }
}

} // namespace Hephaestus
