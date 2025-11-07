# Project Manager Integration Guide

## Overview
The new Project Manager provides a modern, Godot/Unreal-like interface for managing projects in Hephaestus Engine.

## Features

### Visual Design
- **Grid View**: Large thumbnail cards with project previews
- **List View**: Compact list with project details
- **Modern UI**: Dark theme with blue accents
- **Sidebar**: Quick access to Recent, Favorites, and All Projects

### Project Management
- Create new projects with templates (3D, 2D, Minimal)
- Import existing projects
- Delete projects with confirmation
- Open project folders in file explorer
- Mark projects as favorites
- Search and filter projects
- Sort by name, date, or favorites

### Metadata System
Projects store metadata in `.hephaestus/project.meta`:
- Project name and description
- Engine version
- Creation and last modified dates
- Tags and favorites
- Screenshot path (for thumbnails)

## Integration Example

### Basic Usage in main.cpp

```cpp
#include "UI/ProjectManager.h"

// Global or class member
static HephaestusEngine::ProjectManager g_ProjectManager;

// In initialization (after Vulkan setup)
void InitializeProjectManager(VulkanDevice& device) {
    g_ProjectManager.Initialize(&device);
    g_ProjectManager.SetProjectsRoot(ResolveProjectsRoot());
    g_ProjectManager.RefreshProjects();
}

// In main loop (before project is selected)
void ShowProjectBrowser() {
    std::optional<std::filesystem::path> selectedProject = g_ProjectManager.Draw();
    
    if (selectedProject.has_value()) {
        // User selected a project
        SetActiveProjectRoot(selectedProject.value());
        g_ProjectSelected = true;
        
        // Initialize project-specific resources
        InitializeProjectAssets();
    }
}

// In shutdown
void CleanupProjectManager() {
    g_ProjectManager.Shutdown();
}
```

### Full Integration Pattern

```cpp
int main() {
    // 1. Initialize GLFW and Vulkan
    // ... (existing initialization code)
    
    // 2. Initialize Dear ImGui
    // ... (existing ImGui setup)
    
    // 3. Initialize Project Manager
    g_ProjectManager.Initialize(&vkDevice);
    g_ProjectManager.SetProjectsRoot(ResolveProjectsRoot());
    g_ProjectManager.RefreshProjects();
    
    // 4. Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        // Start ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        if (!g_ProjectSelected) {
            // Show project manager
            std::optional<std::filesystem::path> selectedProject = g_ProjectManager.Draw();
            
            if (selectedProject.has_value()) {
                SetActiveProjectRoot(selectedProject.value());
                g_ProjectSelected = true;
                
                // Load project resources
                LoadProjectAssets();
                CreateDefaultScene();
            }
        } else {
            // Show normal editor UI
            DrawMainEditor();
        }
        
        // Render
        ImGui::Render();
        // ... (rendering code)
    }
    
    // 5. Cleanup
    g_ProjectManager.Shutdown();
    // ... (other cleanup)
    
    return 0;
}
```

## Customization

### Changing Colors
Edit the color members in `ProjectManager.h`:
```cpp
ImVec4 m_ColorAccent = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);      // Blue
ImVec4 m_ColorSuccess = ImVec4(0.32f, 0.80f, 0.60f, 1.0f);     // Green
ImVec4 m_ColorError = ImVec4(0.90f, 0.36f, 0.36f, 1.0f);       // Red
```

### Adding Custom Templates
In `ProjectManager::InitializeTemplates()`:
```cpp
ProjectTemplate customTemplate;
customTemplate.name = "Physics Simulation";
customTemplate.description = "Template with physics setup";
customTemplate.include3DDefaults = true;
customTemplate.folders = { "assets/physics", "assets/simulations" };
m_Templates.push_back(customTemplate);
```

### Thumbnail Support
To add project thumbnails:
1. Save a `thumbnail.png` in the project root or `.hephaestus/` folder
2. Implement `LoadProjectThumbnail()` to load the image using `VulkanTexture`
3. Set `ProjectInfo::thumbnail` to the loaded texture ID

## Project Structure

When a new project is created, the following structure is generated:

```
MyProject/
├── .hephaestus/
│   └── project.meta          # Project metadata
├── assets/
│   ├── models/               # 3D models (.fbx, .obj, etc.)
│   ├── textures/             # Textures (.png, .jpg, etc.)
│   ├── materials/            # Material files
│   ├── scenes/               # Scene files (.hpscene)
│   ├── scripts/              # Scripts
│   ├── audio/                # Audio files
│   └── prefabs/              # Prefab files
└── thumbnail.png             # Optional project thumbnail
```

## API Reference

### ProjectManager Class

#### Methods
- `Initialize(VulkanDevice*)` - Initialize with Vulkan resources
- `Shutdown()` - Clean up resources
- `Draw()` - Draw the project manager UI, returns selected project path
- `SetProjectsRoot(path)` - Set the root directory for projects
- `RefreshProjects()` - Scan and reload project list

#### View Modes
- `ProjectViewMode::Grid` - Thumbnail grid view
- `ProjectViewMode::List` - Compact list view

#### Sort Modes
- `ProjectSortMode::LastModified` - Sort by last modified date
- `ProjectSortMode::Name` - Sort alphabetically
- `ProjectSortMode::Created` - Sort by creation date
- `ProjectSortMode::Favorites` - Show favorites first

## Future Enhancements

Potential improvements:
- [ ] Project thumbnail screenshots
- [ ] Project import wizard
- [ ] Project settings editor
- [ ] Project duplication
- [ ] Project archiving/export
- [ ] Cloud sync indicators
- [ ] Version control integration
- [ ] Project statistics (file count, size, etc.)
- [ ] Custom project templates from files
- [ ] Drag-and-drop project import
