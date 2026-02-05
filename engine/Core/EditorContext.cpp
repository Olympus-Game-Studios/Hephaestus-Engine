/**
 * @file EditorContext.cpp
 * @brief Implementation of shared editor state.
 */

#include "EditorContext.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneSerializer.h"
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
    if (!m_activeScene) {
        std::cerr << "[EditorContext] No active scene to save\n";
        return false;
    }
    
    // Serialize
    SceneSerializer serializer(*m_activeScene);
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
    if (m_selectedEntity == entity) {
        return; // No change
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
    if (!m_isDirty) {
        m_isDirty = true;
        if (m_onDirtyChanged) {
            m_onDirtyChanged(true);
        }
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
