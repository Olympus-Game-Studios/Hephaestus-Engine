#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>
#include <string>
#include <filesystem>
#include <algorithm>
#include <utility>
#include <system_error>
#include <optional>
#include <cstdint>
#include <unordered_map>
#include <cctype>
#include <array>
#include <fstream>
#include <iomanip>
#include <cstdio>
#include <cstddef>
#include <cwchar>

#include "../extern/stb_image.h"

#include "Math.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

#include "../Assets/MeshAsset.h"
#include "../Assets/ModelImporter.h"
#include "../Gfx/VulkanDevice.h"
#include "../Gfx/VulkanSwapchain.h"
#include "../Gfx/VulkanPipeline.h"
#include "../Gfx/VulkanBuffer.h"
#include "../Gfx/VulkanTexture.h"
#include "../Gfx/VulkanGizmo.h"
#include "../Gfx/VulkanSkybox.h"
#include "../Physics/MeshCollisionSystem.h"
#include "../Physics/JoltPhysicsEngine.h"
#include "../Physics/JoltPhysicsSystem.h"

// Dear ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "imnodes.h"

// Scene Graph / ECS
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Scene/Transform.h"
#include "../Scene/SceneSerializer.h"
#include "../Scene/SceneExporter.h"
#include "../Scripting/ScriptEngine.h"
#include "../Scripting/ScriptRegistry.h"
#include "../Scripting/LuaScriptEngine.h"
#include "../Game/ExampleScripts.h"
#include "../UI/MaterialEditor/MaterialEditorPanel.h"
#include "../UI/ProjectManager.h"

// Namespace alias for convenience
namespace fs = std::filesystem;

#ifdef _DEBUG
const bool enableValidationLayers = true;
#else
const bool enableValidationLayers = false;
#endif

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT /*msgSeverity*/,
    VkDebugUtilsMessageTypeFlagsEXT /*msgType*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void* /*userData*/)
{
    std::cerr << "[VULKAN] " << (pData ? pData->pMessage : "") << std::endl;
    return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pMessenger)
{
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    return func
        ? func(instance, pCreateInfo, pAllocator, pMessenger)
        : VK_ERROR_EXTENSION_NOT_PRESENT;
}

void populateDebugCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& ci)
{
    ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debugCallback;
}



static bool g_framebufferResized = false;
static bool g_DebugSkipImportedMeshes = false;
static uint64_t g_FrameCounter = 0;
static float g_UIScale = 1.0f;

static std::filesystem::path g_EngineRoot;
static std::filesystem::path g_ProjectsRoot;
static std::filesystem::path g_ActiveProjectRoot;
static std::string g_ActiveProjectName;
static bool g_ProjectSelected = false;
static bool g_AssetBrowserNeedsReset = true;
static std::string g_SelectedScriptAssetName;
static std::string g_SelectedScriptAssetPath;
static bool g_SelectedScriptAssetValid = false;
static std::string g_ScriptAttachmentMessage;
static float g_ScriptAttachmentMessageTimer = 0.0f;
static HephaestusEngine::ProjectManager g_ProjectManager;
static LuaScriptEngine* g_LuaScriptEnginePtr = nullptr;  // Pointer to Lua engine for callbacks

// Runtime mode flags
static bool g_RuntimeMode = false;  // If true, runs as standalone runtime without editor UI
static std::string g_RuntimeScenePath;  // Scene to load in runtime mode

// Forward declarations
static std::filesystem::path ResolveActiveProjectRoot();
static std::filesystem::path ResolveActiveAssetsRoot();

constexpr float kReferenceViewportWidth = 1280.0f;
constexpr float kReferenceViewportHeight = 720.0f;

static float ComputeUIScale(const ImGuiViewport* viewport)
{
    if (!viewport || viewport->Size.x <= 0.0f || viewport->Size.y <= 0.0f) {
        return 1.0f;
    }
    float scaleX = viewport->Size.x / kReferenceViewportWidth;
    float scaleY = viewport->Size.y / kReferenceViewportHeight;
    float raw = std::min(scaleX, scaleY);
    if (raw <= 1.0f) {
        // Downscale modestly for small windows
        return std::clamp(raw, 0.80f, 1.0f);
    }
    // Gentle upscale to avoid crowding; e.g., at 2.0 raw -> 1.3 with slope 0.3, clamped to 1.2
    float gentle = 1.0f + (raw - 1.0f) * 0.30f;
    return std::clamp(gentle, 1.0f, 1.20f);
}

static void ApplyHephaestusEditorStyle(ImGuiIO& io, float scale)
{
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    style.DisabledAlpha = 0.45f;
    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(11.0f, 7.0f);
    style.CellPadding = ImVec2(9.0f, 7.0f);
    style.ItemSpacing = ImVec2(11.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 5.0f);
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = ImVec2(18.0f, 6.0f);
    style.SeparatorTextBorderSize = 1.0f;
    style.ScrollbarSize = 18.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabMinSize = 12.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 6.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.ColorButtonPosition = ImGuiDir_Left;
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.LogSliderDeadzone = 0.2f;

    // Palette leans on UE cobalt and Godot teal for a modern editor aesthetic.
    // Improved contrast for better readability
    const ImVec4 textPrimary(0.92f, 0.94f, 0.97f, 1.00f);
    const ImVec4 textMuted(0.65f, 0.68f, 0.74f, 1.00f);
    const ImVec4 bgUltraDark(0.04f, 0.05f, 0.07f, 1.00f);
    const ImVec4 bgDark(0.06f, 0.07f, 0.09f, 1.00f);
    const ImVec4 bgMid(0.11f, 0.12f, 0.15f, 1.00f);
    const ImVec4 bgLight(0.17f, 0.19f, 0.23f, 1.00f);
    const ImVec4 surfaceLow(0.19f, 0.21f, 0.25f, 1.00f);
    const ImVec4 surfaceMid(0.25f, 0.28f, 0.33f, 1.00f);
    const ImVec4 borderSoft(0.16f, 0.18f, 0.21f, 1.00f);
    const ImVec4 borderStrong(0.26f, 0.29f, 0.35f, 1.00f);
    const ImVec4 accentUE(0.15f, 0.58f, 0.98f, 1.00f);
    const ImVec4 accentUEHover(0.24f, 0.66f, 1.00f, 1.00f);
    const ImVec4 accentUEActive(0.12f, 0.49f, 0.85f, 1.00f);
    const ImVec4 accentGodot(0.26f, 0.70f, 0.77f, 1.00f);
    const ImVec4 accentGodotHover(0.33f, 0.81f, 0.86f, 1.00f);
    const ImVec4 accentGodotActive(0.18f, 0.57f, 0.62f, 1.00f);
    const ImVec4 warning(0.93f, 0.67f, 0.32f, 1.00f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = textPrimary;
    colors[ImGuiCol_TextDisabled] = textMuted;
    colors[ImGuiCol_WindowBg] = bgDark;
    colors[ImGuiCol_ChildBg] = bgDark;
    colors[ImGuiCol_PopupBg] = ImVec4(bgUltraDark.x, bgUltraDark.y, bgUltraDark.z, 0.98f);
    colors[ImGuiCol_Border] = borderSoft;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = surfaceLow;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(accentGodotHover.x, accentGodotHover.y, accentGodotHover.z, 0.65f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(accentGodotActive.x, accentGodotActive.y, accentGodotActive.z, 0.90f);
    colors[ImGuiCol_TitleBg] = bgDark;
    colors[ImGuiCol_TitleBgActive] = bgMid;
    colors[ImGuiCol_TitleBgCollapsed] = bgDark;
    colors[ImGuiCol_MenuBarBg] = bgUltraDark;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(bgUltraDark.x, bgUltraDark.y, bgUltraDark.z, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = surfaceLow;
    colors[ImGuiCol_ScrollbarGrabHovered] = surfaceMid;
    colors[ImGuiCol_ScrollbarGrabActive] = accentGodotActive;
    colors[ImGuiCol_CheckMark] = accentUE;
    colors[ImGuiCol_SliderGrab] = accentGodot;
    colors[ImGuiCol_SliderGrabActive] = accentGodotActive;
    colors[ImGuiCol_Button] = surfaceLow;
    colors[ImGuiCol_ButtonHovered] = accentUEHover;
    colors[ImGuiCol_ButtonActive] = accentUEActive;
    colors[ImGuiCol_Header] = surfaceLow;
    colors[ImGuiCol_HeaderHovered] = accentGodotHover;
    colors[ImGuiCol_HeaderActive] = accentGodotActive;
    colors[ImGuiCol_Separator] = borderStrong;
    colors[ImGuiCol_SeparatorHovered] = accentUEHover;
    colors[ImGuiCol_SeparatorActive] = accentUEActive;
    colors[ImGuiCol_ResizeGrip] = ImVec4(accentGodot.x, accentGodot.y, accentGodot.z, 0.55f);
    colors[ImGuiCol_ResizeGripHovered] = accentGodotHover;
    colors[ImGuiCol_ResizeGripActive] = accentGodotActive;
    colors[ImGuiCol_InputTextCursor] = accentUE;
    colors[ImGuiCol_TabHovered] = accentUEHover;
    colors[ImGuiCol_Tab] = bgMid;
    colors[ImGuiCol_TabSelected] = accentUEActive;
    colors[ImGuiCol_TabSelectedOverline] = accentUE;
    colors[ImGuiCol_TabDimmed] = bgUltraDark;
    colors[ImGuiCol_TabDimmedSelected] = bgMid;
    colors[ImGuiCol_TabDimmedSelectedOverline] = accentGodot;
    colors[ImGuiCol_DockingPreview] = ImVec4(accentUE.x, accentUE.y, accentUE.z, 0.35f);
    colors[ImGuiCol_DockingEmptyBg] = bgUltraDark;
    colors[ImGuiCol_PlotLines] = accentGodot;
    colors[ImGuiCol_PlotLinesHovered] = accentGodotHover;
    colors[ImGuiCol_PlotHistogram] = accentUE;
    colors[ImGuiCol_PlotHistogramHovered] = accentUEHover;
    colors[ImGuiCol_TableHeaderBg] = bgMid;
    colors[ImGuiCol_TableBorderStrong] = borderStrong;
    colors[ImGuiCol_TableBorderLight] = borderSoft;
    colors[ImGuiCol_TableRowBg] = ImVec4(bgLight.x, bgLight.y, bgLight.z, 0.20f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(bgLight.x, bgLight.y, bgLight.z, 0.40f);
    colors[ImGuiCol_TextLink] = accentUE;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(accentUE.x, accentUE.y, accentUE.z, 0.40f);
    colors[ImGuiCol_TreeLines] = ImVec4(borderSoft.x, borderSoft.y, borderSoft.z, 0.50f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(accentUE.x, accentUE.y, accentUE.z, 0.90f);
    colors[ImGuiCol_UnsavedMarker] = warning;
    colors[ImGuiCol_NavCursor] = ImVec4(accentUE.x, accentUE.y, accentUE.z, 0.80f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(accentUE.x, accentUE.y, accentUE.z, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(bgUltraDark.x, bgUltraDark.y, bgUltraDark.z, 0.60f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(bgUltraDark.x, bgUltraDark.y, bgUltraDark.z, 0.75f);

    style.ScaleAllSizes(scale);
}

static bool g_ShowEditorGizmo = true;
static bool g_ShowGizmoGrid = false;
static bool g_DetailsLocked = false;
static bool g_EnableMeshCollision = true;
static bool g_SimPlaying = false;
static bool g_SimPaused = false;
static bool g_ShowGizmoBeforePlay = true;
static bool g_ViewportOverlayHovered = false;
static bool g_ViewportOverlayFocused = false;
static std::string g_SceneSnapshotBeforePlay; // Stores scene state before entering play mode
static constexpr size_t kSceneUndoLimit = 32;
static std::vector<std::string> g_SceneUndoStack;

static void PushSceneUndoSnapshot(Scene& scene)
{
    SceneSerializer serializer(scene);
    std::string snapshot = serializer.SerializeToString();
    if (snapshot.empty()) {
        return;
    }
    if (g_SceneUndoStack.size() >= kSceneUndoLimit) {
        g_SceneUndoStack.erase(g_SceneUndoStack.begin());
    }
    g_SceneUndoStack.push_back(std::move(snapshot));
}

static std::optional<std::string> PopSceneUndoSnapshot()
{
    if (g_SceneUndoStack.empty()) {
        return std::nullopt;
    }
    std::string snapshot = std::move(g_SceneUndoStack.back());
    g_SceneUndoStack.pop_back();
    return snapshot;
}

static void ClearSceneUndoStack()
{
    g_SceneUndoStack.clear();
}

enum class PreferredIDE {
    SystemDefault = 0,
    VisualStudio,
    VisualStudioCode
};

static PreferredIDE g_PreferredIDE = PreferredIDE::SystemDefault;

static MaterialEditor::Graph g_MaterialGraph;
static MaterialEditor::Panel* g_MaterialEditorPanel = nullptr;
static std::filesystem::path g_ActiveMaterialAssetPath;

struct ToolbarActions {
    bool playClicked = false;
    bool pauseClicked = false;
    bool stopClicked = false;
    bool saveClicked = false;
    bool buildClicked = false;
    bool exportClicked = false;
};

enum class WorldOutlinerSpawnPreset {
    DefaultCube,
    DefaultSphere,
    DirectionalLight,
    PointLight,
    SpotLight
};

struct WorldOutlinerActions {
    std::optional<std::string> newLevelName;
    std::optional<entt::entity> entityToDelete;
    std::optional<WorldOutlinerSpawnPreset> spawnRequest;
};

struct DetailsPanelActions {
    std::optional<entt::entity> entityToDelete;
    std::optional<entt::entity> meshToClear;
};

static std::string TrimCopy(const std::string& value);

static std::filesystem::path ResolveEngineRoot();
static std::filesystem::path ResolveEnginePath(const std::filesystem::path& relative);
static std::filesystem::path GetExecutableDirectory();
static std::filesystem::path GetLegacyEditorSettingsPath();

static void LoadEditorSettings();
static void SaveEditorSettings();

static void ClearSelectedScriptAsset();
static void UpdateSelectedScriptAsset(const std::filesystem::path& path);

static bool DetectRuntimePackage(const std::filesystem::path& probeRoot)
{
    namespace fs = std::filesystem;
    if (probeRoot.empty()) {
        return false;
    }

    auto hasMarker = [&](const char* name) {
        return fs::exists(probeRoot / name);
    };

    if (hasMarker("export_manifest.txt") || hasMarker("PACKAGE_MANIFEST.txt") || hasMarker("RunGame.bat")) {
        return true;
    }

    bool hasAssets = fs::exists(probeRoot / "assets" / "scenes");
    bool hasEditorFolder = fs::exists(probeRoot / "editor");
    if (hasAssets && !hasEditorFolder) {
        return true;
    }

    return false;
}

static bool DrawPillButton(const char* label, bool active, const ImVec2& size = ImVec2(0.0f, 0.0f))
{
    const ImGuiStyle& s = ImGui::GetStyle();
    const float padX = s.FramePadding.x * 1.2f;
    const float padY = s.FramePadding.y * 1.2f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s.FrameRounding * 2.5f);
    ImVec4 baseColor = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    ImVec4 hoverColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    if (active) {
        baseColor = activeColor;
        hoverColor = activeColor;
    }
    ImGui::PushStyleColor(ImGuiCol_Button, baseColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return clicked;
}

static ToolbarActions DrawEditorToolbar(bool isPlaying, bool isPaused, Entity& environmentEntity, 
                                         VulkanSkybox& skybox, VulkanDevice& vkdev)
{
    ToolbarActions actions{};

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& s = ImGui::GetStyle();
    const float height = std::max(50.0f, ImGui::GetFrameHeight() * 2.2f);
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;
    // Flat, non-rounded, integrated toolbar background
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, s.WindowPadding);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

    if (ImGui::Begin("##HephaestusToolbar", nullptr, flags)) {
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.13f, 0.55f, 0.96f, 1.0f), "HE");
        ImGui::SameLine();
        ImGui::TextUnformatted("Hephaestus Engine");
        if (!g_ActiveProjectName.empty()) {
            std::string projectLabel = "Project: " + g_ActiveProjectName;
            ImGui::TextDisabled("%s", projectLabel.c_str());
        }
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(s.ItemSpacing.x, 0.0f));
        ImGui::SameLine();

        bool openSettingsPopup = false;
        if (ImGui::Button("File")) {
            ImGui::OpenPopup("EditorToolbarFileMenu");
        }

        if (ImGui::BeginPopup("EditorToolbarFileMenu")) {
            if (ImGui::MenuItem("Settings...")) {
                openSettingsPopup = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (openSettingsPopup) {
            ImGui::OpenPopup("EditorSettingsPopup");
        }

        if (ImGui::BeginPopupModal("EditorSettingsPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Editor Preferences");
            ImGui::Separator();
            const char* ideOptions[] = {"System Default", "Visual Studio", "Visual Studio Code"};
            int ideIndex = static_cast<int>(g_PreferredIDE);
            if (ImGui::Combo("Preferred IDE", &ideIndex, ideOptions, IM_ARRAYSIZE(ideOptions))) {
                g_PreferredIDE = static_cast<PreferredIDE>(ideIndex);
                SaveEditorSettings();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("Used when opening scripts from the Asset Browser.");
            }
            ImGui::Spacing();
            ImGui::Checkbox("Show Editor Gizmo", &g_ShowEditorGizmo);
            ImGui::Checkbox("Show Gizmo Grid", &g_ShowGizmoGrid);
            ImGui::Checkbox("Enable Mesh Collision", &g_EnableMeshCollision);
            ImGui::Checkbox("Lock Details Panel Selection", &g_DetailsLocked);
            
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::Button("Save Settings")) {
                SaveEditorSettings();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("Writes changes beside the executable");
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Defaults")) {
                g_ShowEditorGizmo = true;
                g_ShowGizmoGrid = false;
                g_EnableMeshCollision = true;
                g_DetailsLocked = false;
                g_PreferredIDE = PreferredIDE::SystemDefault;
                SaveEditorSettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();

        auto drawTransportButton = [](
            const char* label, bool active) {
            const ImGuiStyle& st = ImGui::GetStyle();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(st.FramePadding.x * 1.4f, st.FramePadding.y * 1.2f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, st.FrameRounding * 1.6f);
            ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_Button);
            ImVec4 hover = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
            ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
            if (active) {
                base = activeColor;
                hover = activeColor;
            }
            ImGui::PushStyleColor(ImGuiCol_Button, base);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
            bool clicked = ImGui::Button(label);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            return clicked;
        };

        actions.playClicked = drawTransportButton("Play", isPlaying && !isPaused);
        ImGui::SameLine();
        actions.pauseClicked = drawTransportButton("Pause", isPaused);
        ImGui::SameLine();
        actions.stopClicked = drawTransportButton("Stop", isPlaying || isPaused);

        ImGui::SameLine(0.0f, s.ItemSpacing.x * 2.0f);
        actions.saveClicked = drawTransportButton("Save", false);
        ImGui::SameLine();
        actions.buildClicked = drawTransportButton("Build", false);
        ImGui::SameLine();
        actions.exportClicked = drawTransportButton("Export", false);

        float remaining = ImGui::GetContentRegionAvail().x;
        if (remaining > 70.0f) {
            ImGui::SameLine(0.0f, remaining - 60.0f);
        } else {
            ImGui::SameLine();
        }
        ImGui::TextColored(ImVec4(0.23f, 0.67f, 0.74f, 1.0f), "ONLINE");
        // Draw a subtle bottom border to separate from content
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 sz = ImGui::GetWindowSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(pos.x, pos.y + sz.y - 1.0f), ImVec2(pos.x + sz.x, pos.y + sz.y - 1.0f), ImGui::GetColorU32(ImGuiCol_Border), 1.0f);
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    return actions;
}

static void DrawViewportOverlay(ImGuizmo::OPERATION& operation,
                                ImGuizmo::MODE& mode,
                                bool& showGrid,
                                bool& showGizmo)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& s = ImGui::GetStyle();
    // Scale overlay width with global UI scale to fit buttons
    float baseW = 640.0f * g_UIScale;
    float minW = 480.0f * g_UIScale;
    float margin = s.ItemSpacing.x * 2.0f;
    float overlayWidth = std::clamp(baseW, minW, viewport->Size.x - margin);
    // Compute overlay height based on content: title line + spacing + pill buttons + window padding + cell padding
    const float windowPadY = s.WindowPadding.y * 1.1f; // matches window padding push below
    const float textH = ImGui::GetTextLineHeight();
    const float pillPadY = s.FramePadding.y * 1.2f;   // matches pill button padding in DrawPillButton
    const float btnH = ImGui::GetFontSize() + pillPadY * 2.0f;
    const float cellPadY = s.CellPadding.y * 2.0f;    // table adds top/bottom cell padding
    const float safety = std::max(2.0f, s.ItemSpacing.y * 0.4f);
    float computedH = windowPadY * 2.0f + textH + s.ItemSpacing.y + btnH + cellPadY + safety;
    const float overlayHeight = std::max(64.0f * g_UIScale, computedH);
    const float toolbarStrip = std::max(50.0f, ImGui::GetFrameHeight() * 2.2f) + s.WindowPadding.y * 2.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + (viewport->Size.x - overlayWidth) * 0.5f,
                                    viewport->Pos.y + toolbarStrip + s.ItemSpacing.y));
    ImGui::SetNextWindowSize(ImVec2(overlayWidth, overlayHeight));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s.WindowRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(s.WindowPadding.x * 1.1f, s.WindowPadding.y * 1.1f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.11f, 0.94f));

    g_ViewportOverlayHovered = false;
    g_ViewportOverlayFocused = false;

    if (ImGui::Begin("##ViewportOverlay", nullptr, flags)) {
        g_ViewportOverlayHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        g_ViewportOverlayFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(s.ItemSpacing.x * 1.2f, s.ItemSpacing.y));
    if (ImGui::BeginTable("ViewportOverlayLayout", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("##OpMode", ImGuiTableColumnFlags_WidthStretch, 0.72f);
        ImGui::TableSetupColumn("##Grid", ImGuiTableColumnFlags_WidthStretch, 0.28f);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("PERSPECTIVE");
            ImGui::Spacing();

            const char* opLabels[3] = {"Move", "Rotate", "Scale"};
            ImGuizmo::OPERATION opValues[3] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
            for (int i = 0; i < 3; ++i) {
                bool active = (operation == opValues[i]);
                if (i > 0) {
                    ImGui::SameLine();
                }
                if (DrawPillButton(opLabels[i], active)) {
                    operation = opValues[i];
                }
            }

            ImGui::SameLine();
            ImGui::Dummy(ImVec2(s.ItemSpacing.x * 1.5f, 0.0f));
            ImGui::SameLine();

            const char* modeLabels[2] = {"Local", "World"};
            ImGuizmo::MODE modeValues[2] = {ImGuizmo::LOCAL, ImGuizmo::WORLD};
            for (int i = 0; i < 2; ++i) {
                bool active = (mode == modeValues[i]);
                if (i > 0) {
                    ImGui::SameLine();
                }
                if (DrawPillButton(modeLabels[i], active)) {
                    mode = modeValues[i];
                }
            }

            ImGui::TableNextColumn();
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing())); // Match left column vertical offset
            ImGui::Dummy(ImVec2(s.ItemSpacing.x * 1.6f, 0.0f));
            ImGui::SameLine();
            if (DrawPillButton("Grid", showGrid)) {
                showGrid = !showGrid;
            }
            ImGui::SameLine();
            if (DrawPillButton("Gizmo", showGizmo)) {
                showGizmo = !showGizmo;
            }
            ImGui::EndTable();
        }
        // Extra bottom spacer to avoid clipping depending on fonts/platform
        ImGui::Dummy(ImVec2(0.0f, s.ItemSpacing.y * 0.6f + s.FramePadding.y * 0.4f));
        ImGui::PopStyleVar();
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

static void BeginHephaestusDockspace()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Reserve space at the top for the toolbar to avoid overlap with docked windows
    const ImGuiStyle& s = ImGui::GetStyle();
    const float toolbarHeight = std::max(50.0f, ImGui::GetFrameHeight() * 2.2f);
    const float topOffset = toolbarHeight;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + topOffset));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, std::max(0.0f, viewport->Size.y - topOffset)));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##HephaestusDockspaceHost", nullptr, hostFlags);
    ImGuiID dockspaceID = ImGui::GetID("HephaestusDockspace");
    ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;
    ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), dockFlags);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    static bool initialized = false;
    if (!initialized) {
        initialized = true;

        ImGui::DockBuilderRemoveNode(dockspaceID);
        ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace | dockFlags);
    // Initial dock size will be adjusted by host window size; set to current available size
    ImGui::DockBuilderSetNodeSize(dockspaceID, ImVec2(viewport->Size.x, std::max(0.0f, viewport->Size.y - topOffset)));

        ImGuiID dockMain = dockspaceID;
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.26f, nullptr, &dockMain);
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.49f, nullptr, &dockMain);
    // Allocate a taller strip along the bottom so the asset browser has room for grid previews.
    ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.36f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("World Outliner", dockLeft);
        ImGui::DockBuilderDockWindow("Details", dockRight);
        ImGui::DockBuilderDockWindow("Asset Browser", dockBottom);
        ImGui::DockBuilderDockWindow("Log Console", dockBottom);
        ImGui::DockBuilderFinish(dockspaceID);
    }
}

static WorldOutlinerActions DrawActorPalette()
{
    WorldOutlinerActions actions{};

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    if (ImGui::BeginChild("##ActorPaletteChild", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::TextUnformatted("Place Actors");
        ImGui::Separator();
        ImGui::Spacing();

        // Calculate button size and layout
        float availWidth = ImGui::GetContentRegionAvail().x;
        const ImGuiStyle& s = ImGui::GetStyle();
        float buttonSize = 64.0f;
        float buttonSpacing = s.ItemSpacing.x;
        int buttonsPerRow = std::max(1, static_cast<int>((availWidth + buttonSpacing) / (buttonSize + buttonSpacing)));

        const struct ActorButton {
            const char* icon;
            const char* label;
            WorldOutlinerSpawnPreset preset;
        } actors[] = {
            {"C", "Cube", WorldOutlinerSpawnPreset::DefaultCube},
            {"S", "Sphere", WorldOutlinerSpawnPreset::DefaultSphere},
            {"D", "Dir Light", WorldOutlinerSpawnPreset::DirectionalLight},
            {"P", "Point Light", WorldOutlinerSpawnPreset::PointLight},
            {"O", "Spot Light", WorldOutlinerSpawnPreset::SpotLight},
        };

        for (int i = 0; i < sizeof(actors) / sizeof(actors[0]); ++i) {
            const ActorButton& actor = actors[i];
            
            ImGui::BeginGroup();
            
            // Draw button with icon
            ImGui::PushID(i);
            ImGui::Button(actor.icon, ImVec2(buttonSize, buttonSize));
            
            // Check for double-click on the button
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                actions.spawnRequest = actor.preset;
            }
            
            // Handle drag-and-drop source
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                // Set payload to carry the actor preset type
                ImGui::SetDragDropPayload("ACTOR_PALETTE_ITEM", &actor.preset, sizeof(WorldOutlinerSpawnPreset));
                // Preview text while dragging
                ImGui::TextUnformatted(actor.label);
                ImGui::EndDragDropSource();
            }
            
            ImGui::PopID();
            
            // Draw label centered below button
            const char* label = actor.label;
            float labelWidth = ImGui::CalcTextSize(label).x;
            float offsetX = (buttonSize - labelWidth) * 0.5f;
            if (offsetX > 0.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
            }
            ImGui::TextUnformatted(label);
            
            ImGui::EndGroup();

            // Layout buttons in rows
            if ((i + 1) % buttonsPerRow != 0 && i < (sizeof(actors) / sizeof(actors[0]) - 1)) {
                ImGui::SameLine();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    return actions;
}

static WorldOutlinerActions DrawWorldOutliner(Scene& scene)
{
    WorldOutlinerActions actions{};

    if (!ImGui::Begin("World Outliner")) {
        ImGui::End();
        return actions;
    }

    static ImGuiTextFilter filter;
    static std::array<char, 128> s_NewLevelNameBuffer{};
    static bool s_FocusNewLevelInput = false;
    constexpr const char* kNewLevelPopupId = "Create New Level###WorldOutlinerNewLevel";
    constexpr const char* kAddEntityPopupId = "Add Entity###WorldOutlinerAddEntity";

    // Split the window: top half for outliner, bottom half for actor palette
    float windowHeight = ImGui::GetContentRegionAvail().y;
    float outlinerHeight = windowHeight * 0.5f;
    float paletteHeight = windowHeight - outlinerHeight - ImGui::GetStyle().ItemSpacing.y;

    // === TOP SECTION: World Outliner ===
    if (ImGui::BeginChild("##WorldOutlinerSection", ImVec2(0, outlinerHeight), false)) {
        float availableWidth = ImGui::GetContentRegionAvail().x;
        const ImGuiStyle& s = ImGui::GetStyle();
        float filterWidth = std::max(availableWidth - (s.ItemSpacing.x * 6.0f + 70.0f), 200.0f);
        ImGui::SetNextItemWidth(filterWidth);
        filter.Draw("##WorldOutlinerFilter", filterWidth);
        ImGui::SameLine();
        DrawPillButton("Filter", false);
        ImGui::SameLine();
        if (ImGui::Button("+##WorldOutlinerAddEntity")) {
            ImGui::OpenPopup(kAddEntityPopupId);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Add a default primitive or light.");
        }
        if (ImGui::BeginPopup(kAddEntityPopupId)) {
            if (ImGui::MenuItem("Default Cube")) {
                actions.spawnRequest = WorldOutlinerSpawnPreset::DefaultCube;
            }
            if (ImGui::MenuItem("Default Sphere")) {
                actions.spawnRequest = WorldOutlinerSpawnPreset::DefaultSphere;
            }
            if (ImGui::MenuItem("Directional Light")) {
                actions.spawnRequest = WorldOutlinerSpawnPreset::DirectionalLight;
            }
            if (ImGui::MenuItem("Point Light")) {
                actions.spawnRequest = WorldOutlinerSpawnPreset::PointLight;
            }
            if (ImGui::MenuItem("Spot Light")) {
                actions.spawnRequest = WorldOutlinerSpawnPreset::SpotLight;
            }
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        ImGui::Separator();

        ImGui::TextDisabled("Scene: %s", scene.GetName().c_str());
        ImGui::SameLine();
        if (DrawPillButton("New Level", false)) {
            s_NewLevelNameBuffer.fill(0);
            const std::string suggestedName = "New Level";
            std::strncpy(s_NewLevelNameBuffer.data(), suggestedName.c_str(), s_NewLevelNameBuffer.size() - 1);
            s_FocusNewLevelInput = true;
            ImGui::OpenPopup(kNewLevelPopupId);
        }
        
        // Show keyboard shortcuts help
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextDisabled("?");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("World Outliner Keyboard Shortcuts:");
            ImGui::Separator();
            ImGui::BulletText("Shift+Backspace or Delete - Delete selected entity");
            ImGui::BulletText("Ctrl+Shift+D - Duplicate selected entity");
            ImGui::BulletText("Ctrl+Z - Undo last change");
            ImGui::BulletText("Esc - Deselect");
            ImGui::BulletText("Right-click - Context menu");
            ImGui::EndTooltip();
        }

        if (ImGui::BeginPopupModal(kNewLevelPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (s_FocusNewLevelInput || ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
                s_FocusNewLevelInput = false;
            }

            ImGui::InputText("Level Name", s_NewLevelNameBuffer.data(), s_NewLevelNameBuffer.size(), ImGuiInputTextFlags_AutoSelectAll);
            bool nameFieldActive = ImGui::IsItemActive();

            std::string trimmedName = TrimCopy(s_NewLevelNameBuffer.data());
            bool validName = !trimmedName.empty();
            if (!validName) {
                ImGui::TextDisabled("Name cannot be blank.");
            }

            ImGui::Spacing();

            bool createClicked = false;
            bool submitViaEnter = validName && nameFieldActive && ImGui::IsKeyPressed(ImGuiKey_Enter);

            if (!validName) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Create")) {
                createClicked = true;
            }
            if (!validName) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }

            if (validName && (createClicked || submitViaEnter)) {
                actions.newLevelName = trimmedName;
                filter.Clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::Spacing();

        auto view = scene.GetEntitiesWith<TagComponent>();

        if (ImGui::BeginTable("WorldOutlinerTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);

            for (auto entityHandle : view) {
                Entity entity(entityHandle, &scene);
                auto& tag = entity.GetComponent<TagComponent>();
                if (!filter.PassFilter(tag.tag.c_str())) {
                    continue;
                }

                bool isSelected = entity.HasComponent<SelectionComponent>() &&
                                  entity.GetComponent<SelectionComponent>().selected;

                const char* typeLabel = "ENTITY";
                if (entity.HasComponent<EnvironmentComponent>()) {
                    typeLabel = "ENV";
                } else if (entity.HasComponent<LightComponent>()) {
                    typeLabel = "LIGHT";
                } else if (entity.HasComponent<MeshRendererComponent>()) {
                    typeLabel = "MESH";
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(entityHandle));
                ImGuiSelectableFlags selFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick;
                if (ImGui::Selectable(tag.tag.c_str(), isSelected, selFlags)) {
                    if (!g_DetailsLocked) {
                        scene.SetSelectedEntity(entity);
                    }
                }

                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Delete", "Shift+Backspace")) {
                        actions.entityToDelete = entityHandle;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", typeLabel);
            }

            ImGui::EndTable();
        }

        // Keyboard shortcuts (when window is focused and not typing in text input)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput) {

            const ImGuiIO& io = ImGui::GetIO();
            const bool ctrlDown = io.KeyCtrl;
            const bool shiftDown = io.KeyShift;
            const ImGuiInputFlags shortcutRoute = ImGuiInputFlags_RouteFocused;

            const bool deleteViaChord = ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_Backspace, shortcutRoute);
            const bool deleteViaKey = ImGui::Shortcut(ImGuiKey_Delete, shortcutRoute);

            // Shift+Backspace or Delete - Delete selected entity
            if (!actions.entityToDelete && (deleteViaChord || deleteViaKey ||
                (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && shiftDown))) {
                Entity selected = scene.GetSelectedEntity();
                if (selected) {
                    actions.entityToDelete = static_cast<entt::entity>(selected);
                }
            }

            const bool duplicateShortcut = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D, shortcutRoute);
            // Ctrl+Shift+D - Duplicate selected entity
            if (duplicateShortcut || (ImGui::IsKeyPressed(ImGuiKey_D, false) && ctrlDown && shiftDown)) {
                Entity selected = scene.GetSelectedEntity();
                if (selected) {
                    PushSceneUndoSnapshot(scene);
                    scene.CopyEntityToClipboard(selected);
                    Entity duplicated = scene.PasteClipboard();
                    if (duplicated) {
                        scene.SetSelectedEntity(duplicated);
                    }
                }
            }
            
            // Escape - Deselect current entity
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                scene.ClearSelection();
            }
        }
    }
    ImGui::EndChild();

    // === BOTTOM SECTION: Actor Palette ===
    WorldOutlinerActions paletteActions = DrawActorPalette();
    if (paletteActions.spawnRequest) {
        actions.spawnRequest = paletteActions.spawnRequest;
    }

    ImGui::End();
    return actions;
}

static DetailsPanelActions DrawDetailsPanel(Scene& scene, ScriptEngine& scriptEngine, VulkanSkybox& skybox, VulkanDevice& vkdev)
{
    DetailsPanelActions actions{};

    if (!ImGui::Begin("Details")) {
        ImGui::End();
        return actions;
    }

    ImGui::Checkbox("Lock", &g_DetailsLocked);
    ImGui::Separator();

    Entity selectedEntity = scene.GetSelectedEntity();
    if (!selectedEntity) {
        ImGui::TextDisabled("Select an entity to view its properties.");
        ImGui::End();
        return actions;
    }

    auto& tag = selectedEntity.GetComponent<TagComponent>();
    auto& transform = selectedEntity.GetComponent<TransformComponent>();

    ImGui::Text("%s", tag.tag.c_str());

    ImGui::SeparatorText("Transform");
    ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
    Vec3 rotDeg = transform.GetRotationDegrees();
    if (ImGui::DragFloat3("Rotation", &rotDeg.x, 1.0f)) {
        transform.SetRotationDegrees(rotDeg);
    }
    ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.01f, 10.0f);

    // Environment Component
    if (selectedEntity.HasComponent<EnvironmentComponent>()) {
        auto& env = selectedEntity.GetComponent<EnvironmentComponent>();
        
        ImGui::SeparatorText("Environment");
        
        const char* bgTypes[] = {"Solid Color", "HDRI"};
        int bgTypeIdx = static_cast<int>(env.type);
        if (ImGui::Combo("Background Type", &bgTypeIdx, bgTypes, IM_ARRAYSIZE(bgTypes))) {
            env.type = static_cast<EnvironmentComponent::BackgroundType>(bgTypeIdx);
        }
        
        if (env.type == EnvironmentComponent::BackgroundType::SolidColor) {
            float color[3] = {env.solidColor.x, env.solidColor.y, env.solidColor.z};
            if (ImGui::ColorEdit3("Background Color", color)) {
                env.solidColor = Vec3(color[0], color[1], color[2]);
            }
        } else {
            static char hdriPathBuffer[512] = "";
            // Initialize buffer from current path if needed
            if (hdriPathBuffer[0] == '\0' && !env.hdriPath.empty()) {
                strncpy(hdriPathBuffer, env.hdriPath.c_str(), sizeof(hdriPathBuffer) - 1);
                hdriPathBuffer[sizeof(hdriPathBuffer) - 1] = '\0';
            }
            
            if (ImGui::InputText("HDRI Path", hdriPathBuffer, sizeof(hdriPathBuffer))) {
                env.hdriPath = std::string(hdriPathBuffer);
            }
            
            if (ImGui::Button("Load HDRI")) {
                if (!env.hdriPath.empty()) {
                    if (skybox.LoadHDRI(env.hdriPath, vkdev.allocator, vkdev.physicalDevice,
                                      vkdev.commandPool, vkdev.graphicsQueue)) {
                        std::cout << "Loaded HDRI: " << env.hdriPath << std::endl;
                    } else {
                        std::cerr << "Failed to load HDRI: " << env.hdriPath << std::endl;
                    }
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("Path to .hdr or .exr file");
            }
            
            ImGui::SliderFloat("HDRI Intensity", &env.hdriIntensity, 0.1f, 5.0f);
        }
    }

    if (selectedEntity.HasComponent<MeshRendererComponent>()) {
        auto& renderer = selectedEntity.GetComponent<MeshRendererComponent>();
        ImGui::SeparatorText("Mesh");
        if (!renderer.meshAssetPath.empty()) {
            ImGui::TextDisabled("Source: %s", renderer.meshAssetPath.c_str());
        } else if (renderer.HasValidMesh()) {
            ImGui::TextDisabled("Source: Built-in mesh");
        } else {
            ImGui::TextDisabled("Source: None");
        }
        ImGui::Checkbox("Backface Culling", &renderer.backfaceCulling);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable to hide back faces for this mesh. Keep disabled for double-sided geometry.");
        }

        bool hasCollider = selectedEntity.HasComponent<MeshColliderComponent>();
        if (!hasCollider && ImGui::Button("Add Mesh Collider")) {
            selectedEntity.AddComponent<MeshColliderComponent>();
            hasCollider = true;
        }

        bool removeCollider = false;
        if (hasCollider) {
            auto& collider = selectedEntity.GetComponent<MeshColliderComponent>();
            ImGui::SeparatorText("Mesh Collider");
            ImGui::Checkbox("Collision Enabled", &collider.enabled);
            ImGui::Checkbox("Static Collider", &collider.isStatic);
            ImGui::Checkbox("Trigger", &collider.isTrigger);
            ImGui::Checkbox("Debug Draw Bounds", &collider.debugDraw);

            if (collider.contacts.empty()) {
                ImGui::TextDisabled("Contacts: none this frame.");
            } else {
                ImGui::TextDisabled("Contacts: %zu", collider.contacts.size());
                for (const MeshColliderContact& contact : collider.contacts) {
                    Entity other(contact.other, &scene);
                    std::string otherName;
                    if (other) {
                        if (other.HasComponent<TagComponent>()) {
                            otherName = other.GetComponent<TagComponent>().tag;
                        } else {
                            otherName = "Entity";
                        }
                    } else {
                        otherName = "Entity (destroyed)";
                    }
                    const char* triggerLabel = contact.trigger ? " [trigger]" : "";
                    ImGui::BulletText("%s | pen %.3f | n(%.2f, %.2f, %.2f)%s",
                        otherName.c_str(), contact.penetration,
                        contact.normal.x, contact.normal.y, contact.normal.z,
                        triggerLabel);
                }
            }

            if (ImGui::Button("Remove Mesh Collider")) {
                removeCollider = true;
            }
        }

        if (removeCollider) {
            selectedEntity.RemoveComponent<MeshColliderComponent>();
            hasCollider = false;
        }

        if (ImGui::Button("Remove Mesh Component")) {
            actions.meshToClear = static_cast<entt::entity>(selectedEntity);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Detach the mesh renderer and material from this entity.");
        }
    }

    bool hasRigidBody = selectedEntity.HasComponent<RigidBodyComponent>();
    bool hasPhysicsShape = selectedEntity.HasComponent<PhysicsShapeComponent>();

    if (!hasRigidBody) {
        if (ImGui::Button("Add Rigid Body")) {
            auto& rb = selectedEntity.AddComponent<RigidBodyComponent>();
            rb = RigidBodyComponent::CreateDynamic();
            if (!hasPhysicsShape) {
                auto& shape = selectedEntity.AddComponent<PhysicsShapeComponent>();
                shape = PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f));
                hasPhysicsShape = true;
            }
            hasRigidBody = true;
        }
    } else {
        auto& rigidBody = selectedEntity.GetComponent<RigidBodyComponent>();
        ImGui::SeparatorText("Rigid Body");

        ImGui::Checkbox("Static Body", &rigidBody.isStatic);
        ImGui::Checkbox("Trigger Volume", &rigidBody.isTrigger);
        ImGui::Checkbox("Use Gravity", &rigidBody.useGravity);
        ImGui::Checkbox("Freeze Rotation", &rigidBody.freezeRotation);

        if (!rigidBody.isStatic && !rigidBody.isTrigger) {
            if (ImGui::DragFloat("Mass", &rigidBody.mass, 0.1f, 0.01f, 1000.0f, "%.2f kg")) {
                rigidBody.mass = std::max(0.0f, rigidBody.mass);
            }
        } else {
            rigidBody.mass = 0.0f;
            rigidBody.useGravity = false;
        }

        if (!rigidBody.isStatic && !rigidBody.isTrigger && rigidBody.mass <= 0.0f) {
            rigidBody.mass = 1.0f;
        }

        ImGui::DragFloat("Linear Damping", &rigidBody.linearDamping, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Angular Damping", &rigidBody.angularDamping, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Friction", &rigidBody.friction, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Restitution", &rigidBody.restitution, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat3("Linear Velocity", &rigidBody.linearVelocity.x, 0.1f);
        ImGui::DragFloat3("Angular Velocity", &rigidBody.angularVelocity.x, 0.1f);
        ImGui::Checkbox("Debug Draw Collider", &rigidBody.debugDraw);

        bool removeRigidBody = false;
        if (ImGui::Button("Remove Rigid Body")) {
            removeRigidBody = true;
        }

        if (removeRigidBody) {
            selectedEntity.RemoveComponent<RigidBodyComponent>();
            hasRigidBody = false;
        }
    }

    if (!hasPhysicsShape) {
        if (ImGui::Button("Add Physics Shape")) {
            auto& shape = selectedEntity.AddComponent<PhysicsShapeComponent>();
            shape = PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f));
            hasPhysicsShape = true;
        }
    } else {
        auto& shape = selectedEntity.GetComponent<PhysicsShapeComponent>();
        ImGui::SeparatorText("Physics Shape");
        const char* shapeLabels[] = {"Box", "Sphere", "Capsule"};
        int shapeIndex = 0;
        switch (shape.type) {
        case PhysicsShapeType::Sphere: shapeIndex = 1; break;
        case PhysicsShapeType::Capsule: shapeIndex = 2; break;
        default: shapeIndex = 0; break;
        }
        if (ImGui::Combo("Shape Type", &shapeIndex, shapeLabels, 3)) {
            switch (shapeIndex) {
            case 1: shape.type = PhysicsShapeType::Sphere; break;
            case 2: shape.type = PhysicsShapeType::Capsule; break;
            default: shape.type = PhysicsShapeType::Box; break;
            }
        }

        if (shape.type == PhysicsShapeType::Box) {
            if (ImGui::DragFloat3("Half Extents", &shape.extents.x, 0.01f, 0.01f, 100.0f)) {
                shape.extents.x = std::max(0.01f, shape.extents.x);
                shape.extents.y = std::max(0.01f, shape.extents.y);
                shape.extents.z = std::max(0.01f, shape.extents.z);
            }
        } else if (shape.type == PhysicsShapeType::Sphere) {
            if (ImGui::DragFloat("Radius", &shape.radius, 0.01f, 0.01f, 100.0f)) {
                shape.radius = std::max(0.01f, shape.radius);
            }
        } else if (shape.type == PhysicsShapeType::Capsule) {
            if (ImGui::DragFloat("Height", &shape.height, 0.01f, 0.01f, 100.0f)) {
                shape.height = std::max(0.01f, shape.height);
            }
            if (ImGui::DragFloat("Radius##Capsule", &shape.radius, 0.01f, 0.01f, 100.0f)) {
                shape.radius = std::max(0.01f, shape.radius);
            }
        }

        ImGui::DragFloat3("Offset", &shape.offset.x, 0.01f);

        bool removeShape = false;
        if (ImGui::Button("Remove Physics Shape")) {
            removeShape = true;
        }

        if (removeShape) {
            selectedEntity.RemoveComponent<PhysicsShapeComponent>();
            hasPhysicsShape = false;
        }
    }

    if (selectedEntity.HasComponent<LightComponent>()) {
        auto& light = selectedEntity.GetComponent<LightComponent>();
        ImGui::SeparatorText("Light");

        const char* lightTypes[] = {"Directional", "Point", "Spot"};
        int currentType = static_cast<int>(light.type);
        if (ImGui::Combo("Type", &currentType, lightTypes, 3)) {
            light.type = static_cast<LightType>(currentType);
        }

        ImGui::ColorEdit3("Color", &light.color.x);
        ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 10.0f);

        if (light.type == LightType::Point || light.type == LightType::Spot) {
            ImGui::SliderFloat("Range", &light.range, 0.1f, 50.0f);
            ImGui::SliderFloat("Constant", &light.constant, 0.0f, 10.0f);
            ImGui::SliderFloat("Linear", &light.linear, 0.0f, 1.0f);
            ImGui::SliderFloat("Quadratic", &light.quadratic, 0.0f, 1.0f);
        }

        if (light.type == LightType::Directional) {
            // Compute current direction from transform rotation
            Vec3 currentDirection = Vec3(0.0f, -1.0f, 0.0f); // Default
            if (selectedEntity.HasComponent<TransformComponent>()) {
                auto& transform = selectedEntity.GetComponent<TransformComponent>();
                Mat4 rx = Mat4::RotationX(transform.rotation.x);
                Mat4 ry = Mat4::RotationY(transform.rotation.y);
                Mat4 rz = Mat4::RotationZ(transform.rotation.z);
                Mat4 rotMatrix = Mat4::Multiply(rx, Mat4::Multiply(ry, rz));
                currentDirection = Mat4::TransformDirection(rotMatrix, currentDirection);
                currentDirection = vnorm(currentDirection);
            }
            
            // Allow user to edit direction, then compute rotation from it
            Vec3 editDirection = currentDirection;
            if (ImGui::DragFloat3("Direction", &editDirection.x, 0.01f, -1.0f, 1.0f)) {
                // Normalize the direction
                editDirection = vnorm(editDirection);
                
                // Compute rotation that would produce this direction
                // We want to rotate the default direction (0, -1, 0) to match editDirection
                if (selectedEntity.HasComponent<TransformComponent>()) {
                    auto& transform = selectedEntity.GetComponent<TransformComponent>();
                    
                    // Calculate pitch (rotation around X axis)
                    // For direction (dx, dy, dz), pitch affects the y and z components
                    float pitch = std::asin(-editDirection.y);
                    
                    // Calculate yaw (rotation around Y axis) 
                    float yaw = std::atan2(editDirection.x, editDirection.z);
                    
                    // Set the rotation (we ignore roll for directional lights)
                    transform.rotation.x = pitch;
                    transform.rotation.y = yaw;
                    // Keep existing roll or set to 0
                    transform.rotation.z = 0.0f;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Light direction in world space. You can also use Transform Rotation.");
            }
        }

        if (light.type == LightType::Spot) {
            float innerDeg = light.innerConeAngle * 57.2958f;
            float outerDeg = light.outerConeAngle * 57.2958f;
            if (ImGui::SliderFloat("Inner Angle", &innerDeg, 0.0f, 90.0f)) {
                light.innerConeAngle = innerDeg * 0.0174533f;
            }
            if (ImGui::SliderFloat("Outer Angle", &outerDeg, 0.0f, 90.0f)) {
                light.outerConeAngle = outerDeg * 0.0174533f;
            }
        }
    }

    // Script Component Section
    ScriptComponent* scriptCompPtr = nullptr;
    if (selectedEntity.HasComponent<ScriptComponent>()) {
        scriptCompPtr = &selectedEntity.GetComponent<ScriptComponent>();
    }

    if (ImGui::CollapsingHeader("Scripts", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (scriptCompPtr && !scriptCompPtr->scriptNames.empty()) {
            ImGui::Text("Attached Scripts: (%zu)", scriptCompPtr->scriptNames.size());
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "[OK] Scripts are attached and active");
            ImGui::Spacing();
            for (size_t i = 0; i < scriptCompPtr->scriptNames.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::BulletText("%s", scriptCompPtr->scriptNames[i].c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    std::string scriptName = scriptCompPtr->scriptNames[i];
                    scriptEngine.DetachScript(selectedEntity, scriptName);
                    std::cout << "Removed script: " << scriptName << "\n";
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No scripts attached to this entity.");
        }

        ImGui::Spacing();
        static char scriptNameBuffer[128] = "";
        ImGui::Text("Add Script:");
        if (g_SelectedScriptAssetValid && g_SelectedScriptAssetPath.empty()) {
            ImGui::TextDisabled("Asset Browser selection: %s", g_SelectedScriptAssetName.c_str());
        }
        ImGui::InputText("##scriptName", scriptNameBuffer, sizeof(scriptNameBuffer));
        ImGui::SameLine();

        if (ImGui::Button("Attach##script")) {
            std::string scriptToAttach;
            if (scriptNameBuffer[0] != '\0') {
                scriptToAttach = scriptNameBuffer;
            } else if (g_SelectedScriptAssetValid && g_SelectedScriptAssetPath.empty()) {
                scriptToAttach = g_SelectedScriptAssetName;
            }

            std::cout << "\n======== [SCRIPT ATTACH DEBUG] ========\n";
            std::cout << "Step 1: Script name = '" << scriptToAttach << "'\n";

            if (!scriptToAttach.empty()) {
                std::cout << "Step 2: Getting registered scripts list...\n";
                auto allScripts = ScriptRegistry::GetRegisteredScripts();
                std::cout << "Step 3: Total registered scripts = " << allScripts.size() << "\n";
                for (const auto& s : allScripts) {
                    std::cout << "  - " << s << "\n";
                }
                
                std::cout << "Step 4: Checking if '" << scriptToAttach << "' is registered...\n";
                if (ScriptRegistry::IsRegistered(scriptToAttach)) {
                    bool alreadyAttached = scriptCompPtr && scriptCompPtr->GetScriptByName(scriptToAttach) != nullptr;
                    if (alreadyAttached) {
                        std::cout << "Step 5: Script already attached, skipping duplicate.\n";
                        g_ScriptAttachmentMessage = "[WARN] Script '" + scriptToAttach + "' already attached.";
                        g_ScriptAttachmentMessageTimer = 3.0f;
                    } else {
                        std::cout << "Step 5: Script is registered and not yet attached.\n";
                        if (!scriptCompPtr) {
                            std::cout << "Step 6a: No ScriptComponent yet, creating one...\n";
                            scriptCompPtr = &selectedEntity.AddComponent<ScriptComponent>();
                            std::cout << "Step 6b: ScriptComponent created\n";
                        } else {
                            std::cout << "Step 6: ScriptComponent already exists\n";
                        }

                        ScriptPtr newScript = ScriptRegistry::CreateScript(scriptToAttach, selectedEntity, &scene);
                        std::cout << "Step 7: Script instance pointer = " << (newScript ? "VALID" : "NULL") << "\n";
                        if (newScript) {
                            scriptEngine.AttachScript(selectedEntity, newScript, scriptToAttach);
                            std::cout << "Step 8: SUCCESS! Script attached via ScriptEngine.\n";
                            g_ScriptAttachmentMessage = "[OK] Script '" + scriptToAttach + "' attached!";
                            g_ScriptAttachmentMessageTimer = 3.0f;
                            scriptNameBuffer[0] = '\0';
                            ClearSelectedScriptAsset();
                            std::cout << "RESULT: ATTACH SUCCESSFUL [YES]\n";
                        } else {
                            std::cout << "RESULT: FAILED - CreateScript returned nullptr\n";
                        }
                    }
                } else {
                    std::cout << "Step 5: NO - Script NOT registered!\n";
                    std::cout << "RESULT: FAILED - Script not in registry\n";
                }
            } else {
                std::cout << "RESULT: FAILED - No script name specified\n";
            }
            std::cout << "======== [END DEBUG] ========\n\n";
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Attach a script from the registry. Leave the field empty to use the script selected in the Asset Browser.");
        }

        if (!g_ScriptAttachmentMessage.empty() && g_ScriptAttachmentMessageTimer > 0.0f) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%s", g_ScriptAttachmentMessage.c_str());
        }

        const auto registeredScripts = ScriptRegistry::GetRegisteredScripts();
        if (!registeredScripts.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Registered Scripts:");
            for (const std::string& name : registeredScripts) {
                ImGui::BulletText("%s", name.c_str());
            }
        }
    }

    // Lua Script Component Section
    LuaScriptComponent* luaScriptCompPtr = nullptr;
    if (selectedEntity.HasComponent<LuaScriptComponent>()) {
        luaScriptCompPtr = &selectedEntity.GetComponent<LuaScriptComponent>();
    }

    if (ImGui::CollapsingHeader("Lua Scripts", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (luaScriptCompPtr && !luaScriptCompPtr->scriptPath.empty()) {
            ImGui::Text("Attached Script: %s", luaScriptCompPtr->scriptPath.c_str());
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "[OK] Lua script is attached and active");
            ImGui::Checkbox("Lua Script Enabled##lua", &luaScriptCompPtr->enabled);
            ImGui::Spacing();
            if (ImGui::Button("Remove Lua Script")) {
                luaScriptCompPtr->scriptPath.clear();
                luaScriptCompPtr->enabled = true;
                std::cout << "Removed Lua script from entity\n";
            }
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No Lua scripts attached to this entity.");
        }

        ImGui::Spacing();
        static char luaScriptPathBuffer[256] = "";
        ImGui::Text("Add Lua Script:");
        if (g_SelectedScriptAssetValid) {
            const std::string& selectionLabel = g_SelectedScriptAssetPath.empty() ? g_SelectedScriptAssetName : g_SelectedScriptAssetPath;
            ImGui::TextDisabled("Asset Browser selection: %s", selectionLabel.c_str());
        }
        ImGui::InputText("##luaScriptPath", luaScriptPathBuffer, sizeof(luaScriptPathBuffer));
        ImGui::SameLine();

        if (ImGui::Button("Attach##lua")) {
            std::string scriptToAttach;
            if (luaScriptPathBuffer[0] != '\0') {
                scriptToAttach = luaScriptPathBuffer;
            } else if (g_SelectedScriptAssetValid) {
                scriptToAttach = !g_SelectedScriptAssetPath.empty() ? g_SelectedScriptAssetPath : g_SelectedScriptAssetName;
            }

            std::cout << "\n======== [LUA SCRIPT ATTACH DEBUG] ========\n";
            std::cout << "Step 1: Lua script path = '" << scriptToAttach << "'\n";

            if (!scriptToAttach.empty()) {
                std::cout << "Step 2: Checking if file exists...\n";
                
                // Try to resolve the path
                fs::path resolvedPath = scriptToAttach;
                if (!fs::is_regular_file(resolvedPath)) {
                    // Try from project root
                    resolvedPath = ResolveActiveProjectRoot() / scriptToAttach;
                    if (!fs::is_regular_file(resolvedPath)) {
                        std::cout << "Step 3: File not found at: " << resolvedPath << "\n";
                    } else {
                        std::cout << "Step 3: File found at: " << resolvedPath << "\n";
                    }
                } else {
                    std::cout << "Step 3: File found at: " << resolvedPath << "\n";
                }

                if (!luaScriptCompPtr) {
                    std::cout << "Step 4: No LuaScriptComponent yet, creating one...\n";
                    luaScriptCompPtr = &selectedEntity.AddComponent<LuaScriptComponent>();
                    std::cout << "Step 5: LuaScriptComponent created\n";
                } else {
                    std::cout << "Step 4: LuaScriptComponent already exists\n";
                }
                
                if (luaScriptCompPtr) {
                    std::cout << "Step 6: Setting script path...\n";
                    luaScriptCompPtr->scriptPath = scriptToAttach;
                    luaScriptCompPtr->enabled = true;
                    std::cout << "Step 7: SUCCESS! Lua script attached\n";
                    std::cout << "Step 8: Script path = '" << luaScriptCompPtr->scriptPath << "'\n";
                    g_ScriptAttachmentMessage = "[OK] Lua script '" + scriptToAttach + "' attached!";
                    g_ScriptAttachmentMessageTimer = 3.0f;
                    luaScriptPathBuffer[0] = '\0';
                    ClearSelectedScriptAsset();
                    std::cout << "RESULT: LUA ATTACH SUCCESSFUL [YES]\n";
                } else {
                    std::cout << "RESULT: FAILED - luaScriptCompPtr is null after creation\n";
                }
            } else {
                std::cout << "RESULT: FAILED - No script path specified\n";
            }
            std::cout << "======== [END LUA DEBUG] ========\n\n";
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Attach a Lua script file. Example: engine/Game/scripts/my_script.lua\nLeave the field empty to use the script selected in the Asset Browser.");
        }

        if (!g_ScriptAttachmentMessage.empty() && g_ScriptAttachmentMessageTimer > 0.0f) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%s", g_ScriptAttachmentMessage.c_str());
        }
    }

    ImGui::SeparatorText("Actions");
    if (ImGui::Button("Delete Entity")) {
        actions.entityToDelete = static_cast<entt::entity>(selectedEntity);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Delete this entity from the scene.");
    }

    ImGui::SeparatorText("Debug");
    ImGui::Text("Entity ID: %u", static_cast<uint32_t>(selectedEntity));
    ImGui::Text("Gizmo Active: %s", g_ShowEditorGizmo ? "YES" : "NO");

    ImGui::End();
    return actions;
}

static void DrawLogConsole(const ImGuiIO& io, const std::string& statusMessage)
{
    if (!ImGui::Begin("Log Console")) {
        ImGui::End();
        return;
    }

    const char* tabLabels[] = {"Log", "Warnings", "Errors"};
    for (int i = 0; i < 3; ++i) {
        bool active = (i == 0);
        if (i > 0) {
            ImGui::SameLine();
        }
        DrawPillButton(tabLabels[i], active, ImVec2(0.0f, 0.0f));
    }

    ImGui::Separator();
    if (io.Framerate > 0.0f) {
        ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
    } else {
        ImGui::Text("FPS: --");
    }

    ImGui::Text("Frame Counter: %llu", static_cast<unsigned long long>(g_FrameCounter));
    ImGui::TextDisabled("(info) Connected to engine Hephaestus-v0.1");

    if (!statusMessage.empty()) {
        ImGui::TextWrapped("%s", statusMessage.c_str());
    }

    ImGui::Checkbox("Skip Imported Mesh Rendering", &g_DebugSkipImportedMeshes);
    ImGui::Checkbox("Enable Mesh Collision", &g_EnableMeshCollision);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Toggle runtime mesh collision detection and simple resolution.");
    }

    ImGui::End();
}

static void framebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    (void)window; (void)width; (void)height;
    g_framebufferResized = true;
}

// ── Camera & Uniform Buffers ─────────────────────
struct UniformBufferObject {
    Mat4 view;
    Mat4 proj;
    Vec3 viewPos;
    float padding1;
};

struct alignas(16) MaterialUBO {
    Vec3 albedo;              // offset 0, size 12 (padded to 16 in std140)
    float albedoPadding;      // offset 12, explicit padding
    float metallic;           // offset 16
    float metallicPadding0;   // offset 20
    float metallicPadding1;   // offset 24
    float metallicPadding2;   // offset 28
    Vec3 emissive;            // offset 32, size 12 (padded to 16 in std140)
    float emissivePadding;    // offset 44, explicit padding
    float roughness;          // offset 48
    float ao;                 // offset 52
    float emissiveStrength;   // offset 56
    uint32_t useAlbedoTexture;     // offset 60
    uint32_t albedoTextureIndex;   // offset 64
    uint32_t useNormalTexture;     // offset 68
    uint32_t normalTextureIndex;   // offset 72
    uint32_t useRoughnessTexture;  // offset 76
    uint32_t roughnessTextureIndex;// offset 80
    uint32_t useAoTexture;         // offset 84
    uint32_t aoTextureIndex;       // offset 88
    uint32_t paddingTail[9];       // offset 92-127 (pad to 16-byte boundary = 128 bytes)
};

static_assert(offsetof(MaterialUBO, metallic) == 16, "MaterialUBO.metallic misaligned");
static_assert(offsetof(MaterialUBO, emissive) == 32, "MaterialUBO.emissive misaligned");
static_assert(offsetof(MaterialUBO, roughness) == 48, "MaterialUBO.roughness misaligned");
static_assert(offsetof(MaterialUBO, useAlbedoTexture) == 60, "MaterialUBO.useAlbedoTexture misaligned");
static_assert(offsetof(MaterialUBO, useNormalTexture) == 68, "MaterialUBO.useNormalTexture misaligned");
static_assert(offsetof(MaterialUBO, useRoughnessTexture) == 76, "MaterialUBO.useRoughnessTexture misaligned");
static_assert(offsetof(MaterialUBO, useAoTexture) == 84, "MaterialUBO.useAoTexture misaligned");
static_assert(sizeof(MaterialUBO) % 16 == 0, "MaterialUBO size must align to 16 bytes");

static constexpr uint32_t kMaxMaterialTextureSlots = 128;

struct MaterialTextureSlot {
    VulkanTexture texture;
    VkDescriptorImageInfo descriptor{};
    std::string key;
    bool valid = false;
};

static std::vector<MaterialTextureSlot> g_MaterialTextureSlots;
static std::unordered_map<std::string, uint32_t> g_MaterialTextureLookup;
static uint32_t g_DefaultMaterialTextureSlot = 0;

struct LightingUBO {
    // Directional light
    Vec3 dirLightDir;
    float dirLightIntensity;
    Vec3 dirLightColor;
    float padding4;
    
    // Ambient
    Vec3 ambientColor;
    float ambientIntensity;
    
    // Point lights (up to 4)
    struct {
        float x, y, z, range;
    } pointLightPositions[4];
    
    struct {
        float r, g, b, intensity;
    } pointLightColors[4];
    
    struct {
        float constant, linear, quadratic, unused;
    } pointLightAttenuation[4];
    
    // Spot lights (up to 4)
    struct {
        float x, y, z, range;
    } spotLightPositions[4];
    
    struct {
        float x, y, z, innerAngle;
    } spotLightDirections[4];
    
    struct {
        float r, g, b, intensity;
    } spotLightColors[4];
    
    struct {
        float constant, linear, quadratic, outerAngle;
    } spotLightAttenuation[4];
    
    int pointLightCount;
    int spotLightCount;
    float padding5;
    float padding6;
};

struct Camera {
    Vec3 position = {0.0f, 0.0f, 5.0f};
    float yawDeg   = 90.0f;
    float pitchDeg = 0.0f;
    float moveSpeed = 2.0f;
    float mouseSens = 0.1f;
    bool firstMouse = true;
    double lastX = 0.0, lastY = 0.0;
    bool rightMouseDown = false;

    void handleMouse(GLFWwindow* window) {
        // Check if right mouse button is held
        bool rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        
        // Toggle cursor mode based on right mouse button
        if (rmb && !rightMouseDown) {
            // Just pressed - disable cursor and reset first mouse
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            firstMouse = true;
            rightMouseDown = true;
        } else if (!rmb && rightMouseDown) {
            // Just released - enable cursor
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            rightMouseDown = false;
        }

        // Only process look input when right mouse is held
        if (!rightMouseDown) return;

        double xpos, ypos; glfwGetCursorPos(window, &xpos, &ypos);
        if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; return; }
        float xoffset = static_cast<float>(xpos - lastX);
        float yoffset = static_cast<float>(ypos - lastY);
        lastX = xpos; lastY = ypos;

        yawDeg   -= xoffset * mouseSens;
        pitchDeg -= yoffset * mouseSens;
        if (pitchDeg > 89.0f) pitchDeg = 89.0f;
        if (pitchDeg < -89.0f) pitchDeg = -89.0f;
    }

    void handleKeyboard(GLFWwindow* window, float dt) {
        // Only allow keyboard movement when right mouse is held (Unreal-style)
        if (!rightMouseDown) return;
        
        float velocity = moveSpeed * dt;
        // compute forward and right from yaw/pitch
        float yawR = yawDeg * 3.1415926535f / 180.0f;
        float pitchR = pitchDeg * 3.1415926535f / 180.0f;
        Vec3 front = vnorm(v3(
            std::cos(yawR) * std::cos(pitchR),
            std::sin(pitchR),
            std::sin(yawR) * std::cos(pitchR)));
        Vec3 right = vnorm(vcross(front, v3(0.0f, 1.0f, 0.0f)));
        Vec3 up    = vnorm(vcross(right, front));

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) position = vsub(position, vscale(front, velocity));
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) position = vadd(position, vscale(front, velocity));
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) position = vsub(position, vscale(right, velocity));
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) position = vadd(position, vscale(right, velocity));
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) position = vadd(position, vscale(up, velocity));
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) position = vsub(position, vscale(up, velocity));
    }

    UniformBufferObject makeUBO(VkExtent2D extent) const {
        float yawR = yawDeg * 3.1415926535f / 180.0f;
        float pitchR = pitchDeg * 3.1415926535f / 180.0f;
        Vec3 front = vnorm(v3(
            std::cos(yawR) * std::cos(pitchR),
            std::sin(pitchR),
            std::sin(yawR) * std::cos(pitchR)));
        Vec3 up = v3(0.0f, 1.0f, 0.0f);
        UniformBufferObject u{};
        u.view = Mat4::LookAt(position, vadd(position, front), up);
        uint32_t safeWidth = std::max(extent.width, 1u);
        uint32_t safeHeight = std::max(extent.height, 1u);
        float aspect = static_cast<float>(safeWidth) / static_cast<float>(safeHeight);
        u.proj = Mat4::Perspective(60.0f * 3.1415926535f / 180.0f, aspect, 0.1f, 100.0f);
        u.viewPos = position;
        u.padding1 = 0.0f;
        return u;
    }
};

// Store camera pointer for scroll callback
static Camera* g_cameraPtr = nullptr;

struct RuntimeCameraController {
    Camera* camera = nullptr;
    bool manualActive = false;
    Vec3 manualPosition = Vec3(0.0f, 0.0f, 0.0f);
    Vec3 manualRotation = Vec3(0.0f, 0.0f, 0.0f); // radians (pitch, yaw, roll)

    static float RadToDeg(float value) {
        return value * 57.2957795f;
    }

    static float DegToRad(float value) {
        return value * 0.0174532925f;
    }

    void syncFromCamera() {
        if (!camera) {
            return;
        }
        manualPosition = camera->position;
        manualRotation.x = DegToRad(camera->pitchDeg);
        manualRotation.y = DegToRad(camera->yawDeg);
        manualRotation.z = 0.0f;
    }

    void applyToCamera() {
        if (!camera) {
            return;
        }
        camera->position = manualPosition;
        float clampedPitch = std::clamp(RadToDeg(manualRotation.x), -89.0f, 89.0f);
        camera->pitchDeg = clampedPitch;
        camera->yawDeg = RadToDeg(manualRotation.y);
    }

    void setActive(bool active) {
        if (active) {
            if (!manualActive) {
                syncFromCamera();
                std::cout << "[RuntimeCamera] Activating script camera control" << std::endl;
            }
            manualActive = true;
            applyToCamera();
            if (camera) {
                camera->rightMouseDown = false;
                camera->firstMouse = true;
            }
        } else {
            if (manualActive) {
                std::cout << "[RuntimeCamera] Deactivating script camera control" << std::endl;
            }
            manualActive = false;
            syncFromCamera();
        }
    }

    bool isActive() const {
        return manualActive;
    }

    void setPosition(const Vec3& pos) {
        manualPosition = pos;
        if (manualActive) {
            applyToCamera();
        }
    }

    Vec3 getPosition() const {
        if (manualActive) {
            return manualPosition;
        }
        if (!camera) {
            return Vec3();
        }
        return camera->position;
    }

    void setRotation(const Vec3& rot) {
        manualRotation = rot;
        if (manualActive) {
            applyToCamera();
        }
    }

    Vec3 getRotation() const {
        if (manualActive) {
            return manualRotation;
        }
        if (!camera) {
            return Vec3();
        }
        return Vec3(DegToRad(camera->pitchDeg), DegToRad(camera->yawDeg), 0.0f);
    }

    void translate(const Vec3& delta) {
        manualPosition = vadd(manualPosition, delta);
        if (manualActive) {
            applyToCamera();
        }
    }
};

static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    (void)window; (void)xoffset;
    if (g_cameraPtr) {
        // Increase/decrease speed by 10% per scroll tick
        g_cameraPtr->moveSpeed *= (yoffset > 0) ? 1.1f : 0.9f;
        // Clamp to reasonable range
        if (g_cameraPtr->moveSpeed < 0.1f) g_cameraPtr->moveSpeed = 0.1f;
        if (g_cameraPtr->moveSpeed > 50.0f) g_cameraPtr->moveSpeed = 50.0f;
    }
}

// ── Ray Picking ──────────────────────────────────
// Convert screen coordinates to a ray in world space
Ray ScreenToWorldRay(double mouseX, double mouseY, int screenWidth, int screenHeight, 
                     const Mat4& viewMatrix, const Mat4& projMatrix)
{
    // Normalize mouse coordinates to [-1, 1]
    float x = (2.0f * static_cast<float>(mouseX)) / screenWidth - 1.0f;
    // For Vulkan with Y-flip in projection, screen Y maps directly
    float y = (2.0f * static_cast<float>(mouseY)) / screenHeight - 1.0f;
    
    // Create points at near and far planes in NDC space
    // For Vulkan, depth is [0, 1]
    Vec3 rayNearNDC(x, y, 0.0f);  // Near plane
    Vec3 rayFarNDC(x, y, 1.0f);   // Far plane
    
    // Convert to view space
    Mat4 invProj = Mat4::Invert(projMatrix);
    Vec3 rayNearView = Mat4::TransformPoint(invProj, rayNearNDC);
    Vec3 rayFarView = Mat4::TransformPoint(invProj, rayFarNDC);
    
    // Convert to world space
    Mat4 invView = Mat4::Invert(viewMatrix);
    Vec3 rayNearWorld = Mat4::TransformPoint(invView, rayNearView);
    Vec3 rayFarWorld = Mat4::TransformPoint(invView, rayFarView);
    
    // Ray direction is from near to far
    Vec3 rayDirection = vnorm(vsub(rayFarWorld, rayNearWorld));
    
    return Ray(rayNearWorld, rayDirection);
}

// Convert world space point to screen space (returns point in screen pixels)
static Vec3 WorldToScreenPoint(const Vec3& worldPos, const Mat4& viewMatrix, const Mat4& projMatrix, 
                               int screenWidth, int screenHeight)
{
    // Transform to clip space
    Vec3 viewPos = Mat4::TransformPoint(viewMatrix, worldPos);
    Vec3 clipPos = Mat4::TransformPoint(projMatrix, viewPos);
    
    // Perspective divide
    Vec3 ndcPos(clipPos.x / clipPos.z, clipPos.y / clipPos.z, clipPos.z);
    
    // Convert from [-1, 1] to screen coordinates
    float screenX = (ndcPos.x + 1.0f) * 0.5f * screenWidth;
    float screenY = (ndcPos.y + 1.0f) * 0.5f * screenHeight;
    
    return Vec3(screenX, screenY, ndcPos.z);
}

// Draw a wireframe cone for spotlight visualization
static void DrawSpotlightConeVisualization(Entity spotEntity, const Scene& scene,
                                          const Mat4& viewMatrix, const Mat4& projMatrix,
                                          int screenWidth, int screenHeight)
{
    if (!spotEntity || !spotEntity.HasComponent<TransformComponent>() || !spotEntity.HasComponent<LightComponent>()) {
        return;
    }
    
    auto& transform = spotEntity.GetComponent<TransformComponent>();
    auto& light = spotEntity.GetComponent<LightComponent>();
    
    if (light.type != LightType::Spot) {
        return;
    }
    
    // Draw cone visualization using ImGui's background draw list (renders behind UI)
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    
    const uint32_t coneSegments = 12;
    const ImU32 coneColor = IM_COL32(0, 200, 255, 100); // Semi-transparent cyan
    const ImU32 coneColorBright = IM_COL32(100, 255, 255, 180); // Brighter for lines
    
    // Calculate cone geometry in world space
    Vec3 coneOrigin = transform.position;
    
    // Get spotlight direction (forward along -Z by default, rotate by entity)
    Mat4 transformMatrix = transform.GetLocalMatrix();
    Vec3 coneDir = Mat4::TransformDirection(transformMatrix, Vec3(0, 0, -1));
    coneDir = vnorm(coneDir);
    
    float coneHeight = light.range;
    float outerRadius = coneHeight * std::tan(light.outerConeAngle * 0.5f);
    
    // Project cone tip to screen
    Vec3 tipScreen = WorldToScreenPoint(coneOrigin, viewMatrix, projMatrix, screenWidth, screenHeight);
    ImVec2 tipScreenPos(tipScreen.x, tipScreen.y);
    
    // Generate cone base points
    std::vector<Vec3> basePoints;
    const float pi = 3.14159265359f;
    for (uint32_t i = 0; i < coneSegments; ++i) {
        float angle = (float)i / coneSegments * 2.0f * pi;
        float x = std::cos(angle) * outerRadius;
        float y = std::sin(angle) * outerRadius;
        
        // Create vectors perpendicular to cone direction
        Vec3 rightDir = vnorm(vcross(coneDir, Vec3(0, 1, 0)));
        if (vlen(rightDir) < 0.01f) {
            rightDir = vnorm(vcross(coneDir, Vec3(1, 0, 0)));
        }
        Vec3 upDir = vnorm(vcross(rightDir, coneDir));
        
        Vec3 basePoint = vadd(coneOrigin, vadd(vscale(coneDir, coneHeight), vadd(vscale(rightDir, x), vscale(upDir, y))));
        basePoints.push_back(basePoint);
    }
    
    // Project base points to screen and draw lines from tip to base
    for (uint32_t i = 0; i < coneSegments; ++i) {
        Vec3 baseScreen = WorldToScreenPoint(basePoints[i], viewMatrix, projMatrix, screenWidth, screenHeight);
        ImVec2 baseScreenPos(baseScreen.x, baseScreen.y);
        
        // Only draw if points are in front of camera
        if (tipScreen.z > 0 && baseScreen.z > 0) {
            drawList->AddLine(tipScreenPos, baseScreenPos, coneColorBright, 1.5f);
        }
    }
    
    // Draw base circle
    for (uint32_t i = 0; i < coneSegments; ++i) {
        uint32_t next = (i + 1) % coneSegments;
        Vec3 currentScreen = WorldToScreenPoint(basePoints[i], viewMatrix, projMatrix, screenWidth, screenHeight);
        Vec3 nextScreen = WorldToScreenPoint(basePoints[next], viewMatrix, projMatrix, screenWidth, screenHeight);
        ImVec2 currentPos(currentScreen.x, currentScreen.y);
        ImVec2 nextPos(nextScreen.x, nextScreen.y);
        
        if (currentScreen.z > 0 && nextScreen.z > 0) {
            drawList->AddLine(currentPos, nextPos, coneColorBright, 1.5f);
        }
    }
}

// Draw a wireframe arrow for directional light visualization
static void DrawDirectionalLightArrowVisualization(Entity dirLightEntity, const Scene& scene,
                                                    const Mat4& viewMatrix, const Mat4& projMatrix,
                                                    int screenWidth, int screenHeight)
{
    if (!dirLightEntity || !dirLightEntity.HasComponent<TransformComponent>() || !dirLightEntity.HasComponent<LightComponent>()) {
        return;
    }
    
    auto& transform = dirLightEntity.GetComponent<TransformComponent>();
    auto& light = dirLightEntity.GetComponent<LightComponent>();
    
    if (light.type != LightType::Directional) {
        return;
    }
    
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    
    const ImU32 arrowColor = IM_COL32(255, 220, 100, 200); // Golden/yellow color for sun
    const float arrowLength = 2.0f;
    const float arrowHeadLength = 0.4f;
    const float arrowHeadWidth = 0.3f;
    
    // Calculate arrow direction from transform rotation
    Mat4 rx = Mat4::RotationX(transform.rotation.x);
    Mat4 ry = Mat4::RotationY(transform.rotation.y);
    Mat4 rz = Mat4::RotationZ(transform.rotation.z);
    Mat4 rotMatrix = Mat4::Multiply(rx, Mat4::Multiply(ry, rz));
    Vec3 direction = Mat4::TransformDirection(rotMatrix, Vec3(0.0f, -1.0f, 0.0f));
    direction = vnorm(direction);
    
    // Arrow starts at the light position
    Vec3 arrowStart = transform.position;
    Vec3 arrowEnd = vadd(arrowStart, vscale(direction, arrowLength));
    
    // Project to screen space
    Vec3 startScreen = WorldToScreenPoint(arrowStart, viewMatrix, projMatrix, screenWidth, screenHeight);
    Vec3 endScreen = WorldToScreenPoint(arrowEnd, viewMatrix, projMatrix, screenWidth, screenHeight);
    
    // Draw main arrow shaft
    if (startScreen.z > 0 && endScreen.z > 0) {
        drawList->AddLine(ImVec2(startScreen.x, startScreen.y), ImVec2(endScreen.x, endScreen.y), arrowColor, 2.5f);
        
        // Create arrowhead at the end
        // Calculate perpendicular vectors for the arrowhead
        Vec3 right = vnorm(vcross(direction, Vec3(0, 1, 0)));
        if (vlen(right) < 0.01f) {
            right = vnorm(vcross(direction, Vec3(1, 0, 0)));
        }
        Vec3 up = vnorm(vcross(right, direction));
        
        // Arrowhead base point (slightly back from the tip)
        Vec3 headBase = vadd(arrowEnd, vscale(direction, -arrowHeadLength));
        
        // Create 4 arrowhead points (forming a cone-like shape)
        Vec3 headPoint1 = vadd(headBase, vscale(right, arrowHeadWidth));
        Vec3 headPoint2 = vadd(headBase, vscale(right, -arrowHeadWidth));
        Vec3 headPoint3 = vadd(headBase, vscale(up, arrowHeadWidth));
        Vec3 headPoint4 = vadd(headBase, vscale(up, -arrowHeadWidth));
        
        // Project arrowhead points
        Vec3 hp1Screen = WorldToScreenPoint(headPoint1, viewMatrix, projMatrix, screenWidth, screenHeight);
        Vec3 hp2Screen = WorldToScreenPoint(headPoint2, viewMatrix, projMatrix, screenWidth, screenHeight);
        Vec3 hp3Screen = WorldToScreenPoint(headPoint3, viewMatrix, projMatrix, screenWidth, screenHeight);
        Vec3 hp4Screen = WorldToScreenPoint(headPoint4, viewMatrix, projMatrix, screenWidth, screenHeight);
        
        // Draw arrowhead lines from points to tip
        if (hp1Screen.z > 0) {
            drawList->AddLine(ImVec2(endScreen.x, endScreen.y), ImVec2(hp1Screen.x, hp1Screen.y), arrowColor, 2.5f);
        }
        if (hp2Screen.z > 0) {
            drawList->AddLine(ImVec2(endScreen.x, endScreen.y), ImVec2(hp2Screen.x, hp2Screen.y), arrowColor, 2.5f);
        }
        if (hp3Screen.z > 0) {
            drawList->AddLine(ImVec2(endScreen.x, endScreen.y), ImVec2(hp3Screen.x, hp3Screen.y), arrowColor, 2.5f);
        }
        if (hp4Screen.z > 0) {
            drawList->AddLine(ImVec2(endScreen.x, endScreen.y), ImVec2(hp4Screen.x, hp4Screen.y), arrowColor, 2.5f);
        }
        
        // Draw arrowhead base cross
        if (hp1Screen.z > 0 && hp2Screen.z > 0) {
            drawList->AddLine(ImVec2(hp1Screen.x, hp1Screen.y), ImVec2(hp2Screen.x, hp2Screen.y), arrowColor, 2.0f);
        }
        if (hp3Screen.z > 0 && hp4Screen.z > 0) {
            drawList->AddLine(ImVec2(hp3Screen.x, hp3Screen.y), ImVec2(hp4Screen.x, hp4Screen.y), arrowColor, 2.0f);
        }
    }
}

// Helper: Ray-Sphere intersection test for light billboards
static bool IntersectRaySphere(const Ray& ray, const Vec3& center, float radius, float& outDist)
{
    Vec3 oc = vsub(ray.origin, center);
    float a = vdot(ray.direction, ray.direction);
    float b = 2.0f * vdot(oc, ray.direction);
    float c = vdot(oc, oc) - radius * radius;
    float discriminant = b * b - 4 * a * c;
    
    if (discriminant < 0) {
        return false;
    }
    
    float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
    if (t < 0) {
        t = (-b + std::sqrt(discriminant)) / (2.0f * a);
    }
    
    if (t < 0) {
        return false;
    }
    
    outDist = t;
    return true;
}

// Find the closest object hit by a ray (ECS version)
Entity FindClosestHitEntity(const Ray& ray, Scene& scene, float& outDistance)
{
    Entity closestEntity;
    float closestDist = 1e30f;
    
    // Check mesh entities
    auto view = scene.GetEntitiesWith<TransformComponent, MeshRendererComponent>();
    for (auto entityHandle : view) {
        Entity entity(entityHandle, &scene);
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& renderer = entity.GetComponent<MeshRendererComponent>();
        
        if (!renderer.visible)
            continue;
            
        AABB worldBounds = renderer.GetWorldBounds(transform);
        
        float tMin, tMax;
        if (worldBounds.IntersectRay(ray, tMin, tMax)) {
            float hitDist = (tMin > 0) ? tMin : tMax;
            if (hitDist > 0 && hitDist < closestDist) {
                closestDist = hitDist;
                closestEntity = entity;
            }
        }
    }
    
    // Check light entities (treat as spheres matching billboard size)
    const float lightBillboardRadius = 0.3f; // Match billboard size from rendering
    auto lightView = scene.GetEntitiesWith<TransformComponent, LightComponent>();
    for (auto entityHandle : lightView) {
        Entity entity(entityHandle, &scene);
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& light = entity.GetComponent<LightComponent>();
        
        Vec3 lightPos = transform.position;
        // For directional lights, use the position directly (where the billboard is drawn)
        
        float hitDist;
        if (IntersectRaySphere(ray, lightPos, lightBillboardRadius, hitDist)) {
            if (hitDist > 0 && hitDist < closestDist) {
                closestDist = hitDist;
                closestEntity = entity;
            }
        }
    }
    
    outDistance = closestDist;
    return closestEntity;
}

// Helper to keep browser navigation within the asset root directory.
static bool PathInsideRoot(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    std::filesystem::path normalizedRoot = root.lexically_normal();
    std::filesystem::path normalizedCandidate = candidate.lexically_normal();

    std::string rootStr = normalizedRoot.generic_string();
    std::string candidateStr = normalizedCandidate.generic_string();

    if (rootStr.empty()) {
        return true;
    }

    if (candidateStr.size() < rootStr.size()) {
        return false;
    }

    if (candidateStr.compare(0, rootStr.size(), rootStr) != 0) {
        return false;
    }

    if (candidateStr.size() == rootStr.size()) {
        return true;
    }

    char separator = candidateStr[rootStr.size()];
    return separator == '/';
}

static std::filesystem::path ClampToRoot(const std::filesystem::path& candidate, const std::filesystem::path& root)
{
    if (PathInsideRoot(root, candidate)) {
        return candidate.lexically_normal();
    }
    return root.lexically_normal();
}

static std::filesystem::path ResolveEngineRoot()
{
    namespace fs = std::filesystem;
    if (!g_EngineRoot.empty()) {
        return g_EngineRoot;
    }

    auto qualifies = [&](const fs::path& candidate) {
        const bool hasAssets = fs::exists(candidate / "assets");
        const bool hasEditor = fs::exists(candidate / "editor");
        const bool hasEngine = fs::exists(candidate / "engine");
        bool accepted = false;
        if (g_RuntimeMode) {
            accepted = hasAssets;
        } else {
            accepted = hasAssets && hasEditor && hasEngine;
        }
        std::cout << "[ResolveEngineRoot] Probe " << candidate << " (assets=" << hasAssets
                  << ", editor=" << hasEditor << ", engine=" << hasEngine
                  << ") => " << (accepted ? "accepted" : "rejected") << std::endl;
        return accepted;
    };

    auto searchFrom = [&](fs::path start) -> bool {
        if (start.empty()) {
            return false;
        }
        fs::path candidate = start;
        for (int depth = 0; depth < 10; ++depth) {
            if (qualifies(candidate)) {
                g_EngineRoot = candidate;
                std::cout << "[ResolveEngineRoot] Found engine root: " << g_EngineRoot << std::endl;
                return true;
            }
            if (!candidate.has_parent_path()) {
                break;
            }
            candidate = candidate.parent_path();
        }
        return false;
    };

    const fs::path exeDir = GetExecutableDirectory();
    const fs::path currentDir = fs::current_path();

    if (g_RuntimeMode) {
        // Prefer the executable directory when running as an exported runtime
        if (searchFrom(exeDir)) {
            return g_EngineRoot;
        }
        if (searchFrom(currentDir)) {
            return g_EngineRoot;
        }
    } else {
        if (searchFrom(currentDir)) {
            return g_EngineRoot;
        }
        if (searchFrom(exeDir)) {
            return g_EngineRoot;
        }
    }

    if (g_RuntimeMode && !exeDir.empty()) {
        g_EngineRoot = exeDir;
    } else {
        g_EngineRoot = currentDir;
    }
    std::cout << "[ResolveEngineRoot] Using fallback engine root: " << g_EngineRoot << std::endl;
    return g_EngineRoot;
}

static std::filesystem::path ResolveEnginePath(const std::filesystem::path& relative)
{
    return (ResolveEngineRoot() / relative).lexically_normal();
}

static std::filesystem::path GetExecutableDirectory()
{
#ifdef _WIN32
    std::wstring buffer(512, L'\0');
    while (true) {
        DWORD result = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (result == 0) {
            break;
        }
        if (result < buffer.size() - 1) {
            buffer.resize(result);
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
    return std::filesystem::current_path();
}

#ifdef _WIN32
static int CALLBACK DirectoryPickerCallback(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
    if (uMsg == BFFM_INITIALIZED && lpData) {
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, lpData);
    }
    return 0;
}

static std::optional<std::filesystem::path> PromptDirectoryPicker(const std::filesystem::path& initialPath)
{
    BROWSEINFOW bi{};
    bi.hwndOwner = nullptr;
    bi.lpszTitle = L"Select project location";
    bi.ulFlags = BIF_USENEWUI | BIF_RETURNONLYFSDIRS;
    wchar_t displayName[MAX_PATH] = {};
    bi.pszDisplayName = displayName;
    bi.lpfn = DirectoryPickerCallback;

    std::wstring normalized = initialPath.lexically_normal().wstring();
    if (!normalized.empty()) {
        bi.lParam = reinterpret_cast<LPARAM>(normalized.c_str());
    }

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return std::nullopt;
    }

    wchar_t selected[MAX_PATH] = {};
    bool gotPath = SHGetPathFromIDListW(pidl, selected) == TRUE;
    CoTaskMemFree(pidl);

    if (!gotPath) {
        return std::nullopt;
    }

    return std::filesystem::path(selected);
}
#else
static std::optional<std::filesystem::path> PromptDirectoryPicker(const std::filesystem::path&)
{
    return std::nullopt;
}
#endif

static std::filesystem::path ResolveProjectsRoot()
{
    namespace fs = std::filesystem;
    if (!g_ProjectsRoot.empty()) {
        return g_ProjectsRoot;
    }

    fs::path engineRoot = ResolveEngineRoot();
    const std::array<fs::path, 4> candidates = {
        engineRoot / "build" / "Release" / "projects",
        engineRoot / "build" / "Debug" / "projects",
        engineRoot / "build" / "projects",
        engineRoot / "projects"
    };

    for (const auto& candidate : candidates) {
        std::error_code ec;
        fs::create_directories(candidate, ec);
        if (!ec) {
            g_ProjectsRoot = candidate.lexically_normal();
            break;
        }
    }

    if (g_ProjectsRoot.empty()) {
        g_ProjectsRoot = (engineRoot / "projects").lexically_normal();
        std::error_code fallbackErr;
        fs::create_directories(g_ProjectsRoot, fallbackErr);
        if (fallbackErr) {
            std::cerr << "Warning: unable to create projects root at "
                      << g_ProjectsRoot << ": " << fallbackErr.message() << "\n";
        }
    }

    return g_ProjectsRoot;
}

static bool CreateSolidColorTexture(MaterialTextureSlot& slot, VulkanDevice& vkdev, uint32_t rgba)
{
    if (!slot.texture.CreateSolidColor(vkdev.allocator,
                                       vkdev.device,
                                       vkdev.physicalDevice,
                                       vkdev.commandPool,
                                       vkdev.graphicsQueue,
                                       rgba)) {
        return false;
    }

    slot.descriptor.sampler = slot.texture.sampler;
    slot.descriptor.imageView = slot.texture.imageView;
    slot.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    slot.valid = true;
    return true;
}

static void SetActiveProjectRoot(const std::filesystem::path& projectPath)
{
    g_ActiveProjectRoot = projectPath.lexically_normal();
    if (!g_ActiveProjectRoot.empty()) {
        std::string name = g_ActiveProjectRoot.filename().string();
        if (name.empty()) {
            name = g_ActiveProjectRoot.stem().string();
        }
        g_ActiveProjectName = name;
        g_ProjectSelected = true;
    } else {
        g_ActiveProjectName.clear();
        g_ProjectSelected = false;
    }
    g_AssetBrowserNeedsReset = true;

    if (g_LuaScriptEnginePtr) {
        g_LuaScriptEnginePtr->SetProjectRoot(g_ActiveProjectRoot);
    }
}

static std::filesystem::path ResolveActiveProjectRoot()
{
    return g_ActiveProjectRoot;
}

static std::filesystem::path ResolveActiveAssetsRoot()
{
    if (g_ActiveProjectRoot.empty()) {
        return {};
    }
    return (g_ActiveProjectRoot / "assets").lexically_normal();
}

struct AssetBrowserIcon {
    VulkanTexture texture;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    ImTextureID textureID = ImTextureID_Invalid;
    ImTextureRef textureRef;
    ImVec2 originalSize = ImVec2(64.0f, 64.0f);

    AssetBrowserIcon() = default;
    AssetBrowserIcon(const AssetBrowserIcon&) = delete;
    AssetBrowserIcon& operator=(const AssetBrowserIcon&) = delete;
    AssetBrowserIcon(AssetBrowserIcon&&) noexcept = default;
    AssetBrowserIcon& operator=(AssetBrowserIcon&&) noexcept = default;
};

static std::unordered_map<std::string, AssetBrowserIcon> g_AssetBrowserIconRegistry;
static bool g_AssetBrowserIconsReady = false;
static constexpr const char* kDefaultFileIconKey = "file-cpp";

struct AssetBrowserResult {
    std::optional<std::filesystem::path> modelImportRequest;
    std::optional<std::filesystem::path> sceneOpenRequest;
    std::optional<std::filesystem::path> materialOpenRequest;
    std::optional<std::filesystem::path> materialCreateRequest;
};

struct AssetExtensionDescriptor {
    const char* extension;
    const char* iconKey;
    const char* badge;
};

static const AssetExtensionDescriptor kAssetExtensionDescriptors[] = {
    {".fbx", "file-fbx", "FBX"},
    {".obj", "file-obj", "OBJ"},
    {".gltf", "file-obj", "GLTF"},
    {".glb", "file-obj", "GLB"},
    {".png", "file-png", "PNG"},
    {".jpg", "file-jpg", "JPG"},
    {".jpeg", "file-jpg", "JPEG"},
    {".tga", "file-png", "TGA"},
    {".bmp", "file-png", "BMP"},
    {".svg", "file-svg", "SVG"},
    {".mp3", "file-audio", "MP3"},
    {".wav", "file-audio", "WAV"},
    {".ogg", "file-audio", "OGG"},
    {".flac", "file-audio", "FLAC"},
    {".mp4", "file-video", "MP4"},
    {".mov", "file-video", "MOV"},
    {".avi", "file-video", "AVI"},
    {".cpp", "file-cpp", "CPP"},
    {".cc", "file-cpp", "CC"},
    {".cxx", "file-cpp", "CXX"},
    {".c", "file-cpp", "C"},
    {".h", "file-cpp", "H"},
    {".hpp", "file-cpp", "HPP"},
    {".hxx", "file-cpp", "HXX"},
    {".txt", "file-cpp", "TXT"},
    {".json", "file-cpp", "JSON"},
    {".material", "file-cpp", "MAT"},
    {".hematerial", "file-cpp", "HMAT"},
    {".prefab", "file-cpp", "PREF"},
    {".hpscene", "file-cpp", "SCN"}
};

static std::string ToLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static std::string ToUpperCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

static std::string TrimCopy(const std::string& value)
{
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return std::string();
    }
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

static PreferredIDE PreferredIDEFromString(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (value == "visualstudio" || value == "visual_studio" || value == "vs") {
        return PreferredIDE::VisualStudio;
    }
    if (value == "visualstudiocode" || value == "vscode" || value == "visual_studio_code" || value == "code") {
        return PreferredIDE::VisualStudioCode;
    }
    return PreferredIDE::SystemDefault;
}

static std::string PreferredIDEToString(PreferredIDE value)
{
    switch (value) {
        case PreferredIDE::VisualStudio:
            return "VisualStudio";
        case PreferredIDE::VisualStudioCode:
            return "VisualStudioCode";
        default:
            return "SystemDefault";
    }
}

static std::filesystem::path GetLegacyEditorSettingsPath()
{
    return ResolveEnginePath(std::filesystem::path("editor") / "settings.ini");
}

static std::filesystem::path GetEditorSettingsPath()
{
    return (GetExecutableDirectory() / "settings.ini").lexically_normal();
}

static void LoadEditorSettings()
{
    namespace fs = std::filesystem;

    g_PreferredIDE = PreferredIDE::SystemDefault;

    fs::path settingsPath = GetEditorSettingsPath();
    fs::path legacyPath = GetLegacyEditorSettingsPath();

    std::error_code err;
    bool usingLegacySettings = false;
    if (!fs::exists(settingsPath, err) || err) {
        err = {};
        if (fs::exists(legacyPath, err) && !err) {
            settingsPath = legacyPath;
            usingLegacySettings = true;
        } else {
            return;
        }
    }

    std::ifstream in(settingsPath);
    if (!in) {
        std::cerr << "Failed to open editor settings file: " << settingsPath << "\n";
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        std::string key = TrimCopy(line.substr(0, pos));
        std::string value = TrimCopy(line.substr(pos + 1));

        if (key == "preferred_ide") {
            g_PreferredIDE = PreferredIDEFromString(value);
        }
    }

    if (usingLegacySettings) {
        SaveEditorSettings();
    }
}

static void SaveEditorSettings()
{
    namespace fs = std::filesystem;

    fs::path settingsPath = GetEditorSettingsPath();
    fs::path parent = settingsPath.parent_path();
    std::error_code dirErr;
    fs::create_directories(parent, dirErr);
    if (dirErr) {
        std::cerr << "Failed to create editor settings directory: " << parent << " (" << dirErr.message() << ")\n";
        return;
    }

    std::ofstream out(settingsPath, std::ios::trunc);
    if (!out) {
        std::cerr << "Failed to write editor settings: " << settingsPath << "\n";
        return;
    }

    out << "preferred_ide=" << PreferredIDEToString(g_PreferredIDE) << '\n';
}

static std::string SanitizeSceneFilename(const std::string& sceneName)
{
    std::string trimmed = TrimCopy(sceneName);
    if (trimmed.empty()) {
        trimmed = "Untitled";
    }

    std::string sanitized;
    sanitized.reserve(trimmed.size());

    char lastAppended = '\0';
    for (char ch : trimmed) {
        unsigned char uc = static_cast<unsigned char>(ch);
        if (std::isalnum(uc)) {
            sanitized.push_back(ch);
            lastAppended = ch;
        } else if (ch == '-' || ch == '_' || std::isspace(uc) || ch == '.') {
            if (lastAppended != '_') {
                sanitized.push_back('_');
                lastAppended = '_';
            }
        }
    }

    while (!sanitized.empty() && sanitized.back() == '_') {
        sanitized.pop_back();
    }

    if (sanitized.empty()) {
        sanitized = "Scene";
    }

    return sanitized;
}

static AssetBrowserIcon* FindAssetBrowserIcon(const std::string& key)
{
    auto it = g_AssetBrowserIconRegistry.find(key);
    return (it != g_AssetBrowserIconRegistry.end()) ? &it->second : nullptr;
}

static std::string DetermineAssetIconKey(const std::filesystem::path& path, bool isDirectory)
{
    if (isDirectory) {
        return "folder";
    }

    std::string ext = ToLowerCopy(path.extension().string());
    if (ext.empty()) {
        return kDefaultFileIconKey;
    }

    for (const auto& descriptor : kAssetExtensionDescriptors) {
        if (ext == descriptor.extension) {
            return descriptor.iconKey;
        }
    }

    return kDefaultFileIconKey;
}

static std::string DetermineAssetBadgeText(const std::filesystem::path& path, bool isDirectory)
{
    if (isDirectory) {
        return "DIR";
    }

    std::string ext = ToLowerCopy(path.extension().string());
    if (ext.empty()) {
        return "FILE";
    }

    for (const auto& descriptor : kAssetExtensionDescriptors) {
        if (ext == descriptor.extension) {
            if (descriptor.badge && descriptor.badge[0] != '\0') {
                return descriptor.badge;
            }
            break;
        }
    }

    if (!ext.empty() && ext[0] == '.') {
        ext.erase(ext.begin());
    }

    std::string badge = ToUpperCopy(ext);
    if (badge.empty()) {
        return "FILE";
    }
    if (badge.size() > 4) {
        badge.resize(4);
    }
    return badge;
}

static ImVec4 DetermineAssetBadgeColor(const std::string& iconKey, bool isDirectory)
{
    if (isDirectory) {
        return ImVec4(0.18f, 0.42f, 0.80f, 0.90f);
    }

    struct BadgeColorMapping {
        const char* key;
        ImVec4 color;
    };

    static const BadgeColorMapping kBadgeColors[] = {
        {"file-fbx", ImVec4(0.30f, 0.48f, 0.90f, 0.90f)},
        {"file-obj", ImVec4(0.30f, 0.48f, 0.90f, 0.90f)},
        {"file-png", ImVec4(0.20f, 0.66f, 0.62f, 0.90f)},
        {"file-jpg", ImVec4(0.32f, 0.58f, 0.36f, 0.90f)},
        {"file-svg", ImVec4(0.46f, 0.60f, 0.92f, 0.90f)},
        {"file-audio", ImVec4(0.57f, 0.38f, 0.78f, 0.90f)},
        {"file-video", ImVec4(0.86f, 0.54f, 0.28f, 0.90f)},
        {"file-cpp", ImVec4(0.46f, 0.50f, 0.64f, 0.90f)}
    };

    for (const auto& mapping : kBadgeColors) {
        if (iconKey == mapping.key) {
            return mapping.color;
        }
    }

    return ImVec4(0.32f, 0.34f, 0.40f, 0.90f);
}

static bool IsSceneFile(const std::filesystem::path& path)
{
    std::string ext = ToLowerCopy(path.extension().string());
    return ext == ".hpscene";
}

static bool IsMaterialFile(const std::filesystem::path& path)
{
    std::string ext = ToLowerCopy(path.extension().string());
    return ext == ".hematerial";
}

static bool IsScriptFile(const std::filesystem::path& path)
{
    std::string ext = ToLowerCopy(path.extension().string());
    static const std::array<const char*, 10> scriptExtensions = {
        ".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hxx", ".txt", ".json", ".lua"
    };
    for (const auto* scriptExt : scriptExtensions) {
        if (ext == scriptExt) {
            return true;
        }
    }
    return false;
}

static void ClearSelectedScriptAsset()
{
    g_SelectedScriptAssetName.clear();
    g_SelectedScriptAssetPath.clear();
    g_SelectedScriptAssetValid = false;
}

static void UpdateSelectedScriptAsset(const std::filesystem::path& path)
{
    if (path.empty()) {
        ClearSelectedScriptAsset();
        return;
    }

    std::error_code fileErr;
    if (!fs::is_regular_file(path, fileErr) || fileErr) {
        ClearSelectedScriptAsset();
        return;
    }

    const std::string ext = ToLowerCopy(path.extension().string());

    if (ext == ".lua") {
        g_SelectedScriptAssetName = path.filename().string();

        fs::path projectRoot = ResolveActiveProjectRoot();
        std::error_code relErr;
        if (!projectRoot.empty()) {
            fs::path relative = fs::relative(path, projectRoot, relErr);
            if (!relErr) {
                g_SelectedScriptAssetPath = relative.generic_string();
            }
        }
        if (g_SelectedScriptAssetPath.empty()) {
            g_SelectedScriptAssetPath = path.lexically_normal().generic_string();
        }

        g_SelectedScriptAssetValid = true;
        return;
    }

    if (IsScriptFile(path)) {
        g_SelectedScriptAssetName = path.stem().string();
        g_SelectedScriptAssetPath.clear();
        g_SelectedScriptAssetValid = !g_SelectedScriptAssetName.empty();
        return;
    }

    ClearSelectedScriptAsset();
}

static void OpenFileInPreferredIDE(const std::filesystem::path& path)
{
    namespace fs = std::filesystem;
    
    if (!fs::exists(path)) {
        std::cerr << "File does not exist: " << path << "\n";
        return;
    }

#ifdef _WIN32
    std::wstring wPath = path.wstring();
    auto shellOpen = [](const wchar_t* file, const wchar_t* parameters) -> bool {
        intptr_t result = reinterpret_cast<intptr_t>(ShellExecuteW(
            nullptr,
            L"open",
            file,
            parameters,
            nullptr,
            SW_SHOW));
        if (result <= 32) {
            std::wcerr << L"ShellExecuteW failed for '" << file << L"' (code " << result << L")" << std::endl;
            return false;
        }
        return true;
    };

    auto makeQuoted = [](const std::wstring& value) -> std::wstring {
        if (!value.empty() && value.front() == L'"' && value.back() == L'"') {
            return value;
        }
        std::wstring quoted;
        quoted.reserve(value.size() + 2);
        quoted.append(1, L'"');
        quoted.append(value);
        quoted.append(1, L'"');
        return quoted;
    };

    using LaunchSpec = std::pair<std::wstring, std::wstring>;
    auto tryPrograms = [&](const std::vector<LaunchSpec>& specs) -> bool {
        for (const auto& spec : specs) {
            const wchar_t* params = spec.second.empty() ? nullptr : spec.second.c_str();
            if (shellOpen(spec.first.c_str(), params)) {
                return true;
            }
        }
        return false;
    };

    bool launched = false;
    switch (g_PreferredIDE) {
        case PreferredIDE::SystemDefault:
            launched = shellOpen(wPath.c_str(), nullptr);
            break;
        case PreferredIDE::VisualStudio: {
            std::vector<LaunchSpec> candidates;
            std::wstring quotedPath = makeQuoted(wPath);
            std::wstring editParam = L"/Edit ";
            editParam += quotedPath;

            candidates.emplace_back(std::wstring(L"devenv.exe"), editParam);
            if (const wchar_t* vsInstallDir = _wgetenv(L"VSINSTALLDIR")) {
                std::wstring candidate = vsInstallDir;
                if (!candidate.empty() && candidate.back() != L'\\' && candidate.back() != L'/') {
                    candidate += L'\\';
                }
                candidate += L"Common7\\IDE\\devenv.exe";
                candidates.emplace_back(std::move(candidate), editParam);
            }
            candidates.emplace_back(std::wstring(L"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/devenv.exe"), editParam);
            candidates.emplace_back(std::wstring(L"C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/devenv.exe"), editParam);
            candidates.emplace_back(std::wstring(L"C:/Program Files/Microsoft Visual Studio/2022/Enterprise/Common7/IDE/devenv.exe"), editParam);
            launched = tryPrograms(candidates);
            break;
        }
        case PreferredIDE::VisualStudioCode: {
            std::vector<LaunchSpec> candidates;
            std::wstring quotedPath = makeQuoted(wPath);
            std::wstring codeParams = L"--reuse-window ";
            codeParams += quotedPath;

            candidates.emplace_back(std::wstring(L"code"), codeParams);
            candidates.emplace_back(std::wstring(L"Code.exe"), codeParams);
            if (const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA")) {
                std::wstring candidate = localAppData;
                if (!candidate.empty() && candidate.back() != L'\\' && candidate.back() != L'/') {
                    candidate += L'\\';
                }
                candidate += L"Programs\\Microsoft VS Code\\Code.exe";
                candidates.emplace_back(std::move(candidate), codeParams);
            }
            candidates.emplace_back(std::wstring(L"C:/Program Files/Microsoft VS Code/Code.exe"), codeParams);
            launched = tryPrograms(candidates);
            break;
        }
    }

    if (!launched && g_PreferredIDE != PreferredIDE::SystemDefault) {
        std::cerr << "Preferred IDE launch failed for " << path << ". Falling back to system default." << std::endl;
        launched = shellOpen(wPath.c_str(), nullptr);
    }

    if (!launched) {
        std::cerr << "Failed to open file in any IDE: " << path << "\n";
    }
#else
    // For non-Windows platforms, we could add support using:
    // - macOS: open command
    // - Linux: xdg-open command
    std::cerr << "Opening files in a specific IDE is currently only supported on Windows\n";
#endif
}

static void EnsureAssetBrowserIconsLoaded(VulkanDevice& vkdev)
{
    if (g_AssetBrowserIconsReady) {
        return;
    }
    g_AssetBrowserIconsReady = true;

    struct IconDef {
        const char* key;
        const char* file;
    };

    namespace fs = std::filesystem;

    fs::path iconRoot = ResolveEnginePath(fs::path("editor") / "Icons");
    if (!fs::exists(iconRoot)) {
        std::cerr << "Asset icon directory missing: " << iconRoot << "\n";
        return;
    }

    const IconDef iconDefs[] = {
        {"folder", "folder.png"},
        {"file-cpp", "file-cpp.png"},
        {"file-fbx", "file-fbx.png"},
        {"file-obj", "file-obj.png"},
        {"file-png", "file-png.png"},
        {"file-jpg", "file-jpg.png"},
        {"file-svg", "file-svg.png"},
        {"file-audio", "file-audio.png"},
        {"file-video", "file-video.png"}
    };

    for (const auto& def : iconDefs) {
        fs::path iconPath = iconRoot / def.file;
        if (!fs::exists(iconPath)) {
            std::cerr << "Asset icon missing: " << iconPath << "\n";
            continue;
        }

        AssetBrowserIcon icon;
        bool loaded = false;
        try {
            loaded = icon.texture.LoadFromFile(
                vkdev.allocator,
                vkdev.device,
                vkdev.physicalDevice,
                vkdev.commandPool,
                vkdev.graphicsQueue,
                iconPath.string(),
                false);
        } catch (const std::exception& e) {
            std::cerr << "Failed to load asset icon " << iconPath << ": " << e.what() << "\n";
            loaded = false;
        }

        if (!loaded) {
            continue;
        }

        icon.descriptor = ImGui_ImplVulkan_AddTexture(
            icon.texture.sampler,
            icon.texture.imageView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (icon.descriptor == VK_NULL_HANDLE) {
            std::cerr << "Failed to register ImGui descriptor for icon: " << iconPath << "\n";
            icon.texture.Destroy(vkdev.allocator, vkdev.device);
            continue;
        }

        icon.textureID = static_cast<ImTextureID>(reinterpret_cast<uint64_t>(icon.descriptor));
        icon.textureRef = ImTextureRef(icon.textureID);
        icon.originalSize = ImVec2(
            static_cast<float>(std::max<uint32_t>(icon.texture.width, 1u)),
            static_cast<float>(std::max<uint32_t>(icon.texture.height, 1u)));

        std::cout << "Loaded asset icon: " << def.key << " -> " << iconPath << " (" << icon.originalSize.x << "x" << icon.originalSize.y << ")\n";

        g_AssetBrowserIconRegistry.emplace(def.key, std::move(icon));
    }
}

static void DestroyAssetBrowserIcons(VulkanDevice& vkdev)
{
    for (auto& [key, icon] : g_AssetBrowserIconRegistry) {
        (void)key;
        if (icon.descriptor != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(icon.descriptor);
            icon.descriptor = VK_NULL_HANDLE;
        }
        icon.textureID = ImTextureID_Invalid;
        icon.textureRef = ImTextureRef();
        if (icon.texture.image != VK_NULL_HANDLE) {
            icon.texture.Destroy(vkdev.allocator, vkdev.device);
        }
    }
    g_AssetBrowserIconRegistry.clear();
    g_AssetBrowserIconsReady = false;
}

static void ResetMaterialGraph()
{
    g_MaterialGraph.Clear();
    MaterialEditor::Node& output = g_MaterialGraph.CreateNode(MaterialEditor::NodeType::MaterialOutput);
    if (g_MaterialEditorPanel) {
        g_MaterialEditorPanel->QueueNodePlacement(output.id, ImVec2(0.0f, 0.0f));
    }
}

static void ActivateMaterialAsset(const std::filesystem::path& path, std::string& statusMessage)
{
    g_ActiveMaterialAssetPath = path.lexically_normal();
    
    if (g_MaterialEditorPanel) {
        g_MaterialEditorPanel->SetActiveAsset(g_ActiveMaterialAssetPath);
        g_MaterialEditorPanel->SetOpen(true);
    }
    
    // Clear and load the graph
    g_MaterialGraph.Clear();
    
    // Load material data into the graph if the file exists and has graph data
    if (std::filesystem::exists(g_ActiveMaterialAssetPath) && g_MaterialEditorPanel) {
        g_MaterialEditorPanel->LoadMaterialIntoGraph(g_MaterialGraph);
    }
    
    // If the graph is empty (new material or failed to load), create default output node
    if (g_MaterialGraph.GetNodes().empty()) {
        MaterialEditor::Node& output = g_MaterialGraph.CreateNode(MaterialEditor::NodeType::MaterialOutput);
        if (g_MaterialEditorPanel) {
            g_MaterialEditorPanel->QueueNodePlacement(output.id, ImVec2(0.0f, 0.0f));
        }
    }

    std::string friendly = g_ActiveMaterialAssetPath.filename().replace_extension().string();
    statusMessage = "Editing material '" + friendly + "'.";
}

// ImGui panel that lists files and folders in the asset directory.
static AssetBrowserResult DrawAssetBrowserPanel(std::string& statusMessage)
{
    namespace fs = std::filesystem;

    AssetBrowserResult result;

    static fs::path assetRoot;
    static fs::path currentPath;
    static fs::path selectedPath;
    static bool openMaterialPopup = false;
    static char materialNameBuffer[128] = "NewMaterial";
    static std::string materialPopupError;

    static bool openFolderPopup = false;
    static char folderNameBuffer[128] = "NewFolder";
    static std::string folderPopupError;

    static bool openScriptPopup = false;
    static char scriptNameBuffer[128] = "NewScript";
    static std::string scriptPopupError;

    static bool openLuaScriptPopup = false;
    static char luaScriptNameBuffer[128] = "NewScript";
    static std::string luaScriptPopupError;

    static bool openDeleteConfirmPopup = false;
    static fs::path deleteConfirmPath;
    static bool deleteConfirmIsFolder = false;
    static std::string deletePopupError;

    fs::path projectRoot = ResolveActiveProjectRoot();
    if (projectRoot.empty()) {
        if (ImGui::Begin("Asset Browser")) {
            ImGui::TextDisabled("Select or create a project to browse assets.");
        }
        ImGui::End();
        selectedPath.clear();
        ClearSelectedScriptAsset();
        return result;
    }

    fs::path desiredRoot = ResolveActiveAssetsRoot();
    if (desiredRoot.empty()) {
        if (ImGui::Begin("Asset Browser")) {
            ImGui::TextDisabled("Active project has no assets directory configured.");
        }
        ImGui::End();
        selectedPath.clear();
        ClearSelectedScriptAsset();
        return result;
    }

    if (g_AssetBrowserNeedsReset || assetRoot != desiredRoot) {
        assetRoot = desiredRoot;
        currentPath = assetRoot;
        selectedPath.clear();
        g_AssetBrowserNeedsReset = false;
    }

    std::error_code ensureRootErr;
    if (!assetRoot.empty()) {
        fs::create_directories(assetRoot, ensureRootErr);
    }

    if (ensureRootErr) {
        if (ImGui::Begin("Asset Browser")) {
            ImGui::TextDisabled("Cannot open project assets: %s", ensureRootErr.message().c_str());
        }
        ImGui::End();
        selectedPath.clear();
        ClearSelectedScriptAsset();
        return result;
    }

    assetRoot = assetRoot.lexically_normal();

    if (currentPath.empty()) {
        currentPath = assetRoot;
    }
    currentPath = ClampToRoot(currentPath, assetRoot);

    if (!ImGui::Begin("Asset Browser")) {
        ImGui::End();
        return result;
    }

    std::error_code existsErr;
    bool rootExists = fs::exists(assetRoot, existsErr);
    if (!rootExists || existsErr) {
        std::string rootDisplay = assetRoot.generic_string();
    ImGui::TextDisabled("Asset directory not found: %s", rootDisplay.c_str());
    ImGui::End();
    selectedPath.clear();
    ClearSelectedScriptAsset();
    return result;
    }

    if (!fs::exists(currentPath)) {
        currentPath = assetRoot;
    }

    if (!selectedPath.empty() && !PathInsideRoot(assetRoot, selectedPath)) {
        selectedPath.clear();
    }

    std::error_code relErr;
    fs::path relativePath = fs::relative(currentPath, assetRoot, relErr);
    std::string relativeDisplay;
    if (relErr || relativePath.empty() || relativePath == ".") {
        relativeDisplay = ".";
    } else {
        relativeDisplay = relativePath.generic_string();
    }

    ImGui::Text("Path: %s", relativeDisplay.c_str());

    auto triggerMaterialCreatePopup = [&]() {
        std::snprintf(materialNameBuffer, sizeof(materialNameBuffer), "NewMaterial");
        materialPopupError.clear();
        openMaterialPopup = true;
    };

    auto triggerFolderCreatePopup = [&]() {
        std::snprintf(folderNameBuffer, sizeof(folderNameBuffer), "NewFolder");
        folderPopupError.clear();
        openFolderPopup = true;
    };

    auto triggerScriptCreatePopup = [&]() {
        std::snprintf(scriptNameBuffer, sizeof(scriptNameBuffer), "NewScript");
        scriptPopupError.clear();
        openScriptPopup = true;
    };

    auto triggerLuaScriptCreatePopup = [&]() {
        std::snprintf(luaScriptNameBuffer, sizeof(luaScriptNameBuffer), "NewScript");
        luaScriptPopupError.clear();
        openLuaScriptPopup = true;
    };

    auto triggerDeleteConfirmPopup = [&](const fs::path& path, bool isFolder) {
        deleteConfirmPath = path;
        deleteConfirmIsFolder = isFolder;
        deletePopupError.clear();
        openDeleteConfirmPopup = true;
    };

    if (ImGui::BeginPopupContextWindow("AssetBrowserContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("New Folder...")) {
            triggerFolderCreatePopup();
        }
        if (ImGui::MenuItem("New Material...")) {
            triggerMaterialCreatePopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("New C++ Script...")) {
            triggerScriptCreatePopup();
        }
        if (ImGui::MenuItem("New Lua Script...")) {
            triggerLuaScriptCreatePopup();
        }
        ImGui::EndPopup();
    }

    if (openMaterialPopup) {
        ImGui::OpenPopup("CreateMaterialPopup");
        openMaterialPopup = false;
    }

    if (ImGui::BeginPopupModal("CreateMaterialPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create material asset");
        ImGui::Separator();
        ImGui::TextDisabled("Directory: %s", relativeDisplay.c_str());
        ImGui::InputText("Name", materialNameBuffer, sizeof(materialNameBuffer));
        if (!materialPopupError.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.93f, 0.36f, 0.31f, 1.0f), "%s", materialPopupError.c_str());
        }

        ImGui::Spacing();

        if (ImGui::Button("Create")) {
            std::string trimmed = TrimCopy(materialNameBuffer);
            if (trimmed.empty()) {
                materialPopupError = "Please enter a material name.";
            } else {
                std::string sanitized = SanitizeSceneFilename(trimmed);
                if (sanitized.empty()) {
                    sanitized = "Material";
                }
                std::filesystem::path materialPath = currentPath / (sanitized + ".hematerial");
                if (std::filesystem::exists(materialPath)) {
                    materialPopupError = "A material with that name already exists.";
                } else {
                    std::error_code writeErr;
                    std::filesystem::create_directories(materialPath.parent_path(), writeErr);
                    if (writeErr) {
                        materialPopupError = "Unable to ensure material directory.";
                    } else {
                        std::ofstream out(materialPath, std::ios::trunc);
                        if (!out) {
                            materialPopupError = "Failed to create material file.";
                        } else {
                            out << "{\n";
                            out << "  \"name\": \"" << sanitized << "\"\n";
                            out << "}\n";
                            out.close();
                            selectedPath = materialPath;
                            statusMessage = "Created material '" + sanitized + "'.";
                            result.materialCreateRequest = materialPath;
                            ImGui::CloseCurrentPopup();
                            materialPopupError.clear();
                        }
                    }
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            materialPopupError.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // Folder creation popup
    if (openFolderPopup) {
        ImGui::OpenPopup("CreateFolderPopup");
        openFolderPopup = false;
    }

    if (ImGui::BeginPopupModal("CreateFolderPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create new folder");
        ImGui::Separator();
        ImGui::TextDisabled("Location: %s", relativeDisplay.c_str());
        ImGui::InputText("Folder name##folder", folderNameBuffer, sizeof(folderNameBuffer));
        if (!folderPopupError.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.93f, 0.36f, 0.31f, 1.0f), "%s", folderPopupError.c_str());
        }

        ImGui::Spacing();

        if (ImGui::Button("Create##folder")) {
            std::string trimmed = TrimCopy(folderNameBuffer);
            if (trimmed.empty()) {
                folderPopupError = "Please enter a folder name.";
            } else {
                std::string sanitized = SanitizeSceneFilename(trimmed);
                if (sanitized.empty()) {
                    sanitized = "Folder";
                }
                fs::path folderPath = currentPath / sanitized;
                if (fs::exists(folderPath)) {
                    folderPopupError = "A folder with that name already exists.";
                } else {
                    std::error_code createErr;
                    fs::create_directories(folderPath, createErr);
                    if (createErr) {
                        folderPopupError = "Failed to create folder: " + std::string(createErr.message());
                    } else {
                        selectedPath = ClampToRoot(folderPath, assetRoot);
                        statusMessage = "Created folder '" + sanitized + "'.";
                        ImGui::CloseCurrentPopup();
                        folderPopupError.clear();
                    }
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##folder")) {
            folderPopupError.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // Script creation popup
    if (openScriptPopup) {
        ImGui::OpenPopup("CreateScriptPopup");
        openScriptPopup = false;
    }

    if (ImGui::BeginPopupModal("CreateScriptPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create C++ Script");
        ImGui::Separator();
        ImGui::TextDisabled("Location: %s", relativeDisplay.c_str());
        ImGui::InputText("Script name##script", scriptNameBuffer, sizeof(scriptNameBuffer));
        if (!scriptPopupError.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.93f, 0.36f, 0.31f, 1.0f), "%s", scriptPopupError.c_str());
        }

        ImGui::Spacing();

        if (ImGui::Button("Create##script")) {
            std::string trimmed = TrimCopy(scriptNameBuffer);
            if (trimmed.empty()) {
                scriptPopupError = "Please enter a script name.";
            } else {
                std::string sanitized = SanitizeSceneFilename(trimmed);
                if (sanitized.empty()) {
                    sanitized = "Script";
                }
                
                // Header file content
                std::string headerContent = R"(#pragma once
#include "engine/Scripting/ScriptBase.h"

class )" + sanitized + R"( : public ScriptBase {
public:
    )" + sanitized + R"((Entity entity, Scene* scene) {
        m_Entity = entity;
        m_Scene = scene;
    }

    virtual ~)" + sanitized + R"(() = default;

    void OnAwake() override {
        // Initialize the script
    }

    void OnUpdate(float deltaTime) override {
        // Main game logic - called every frame
        // deltaTime is time elapsed since last frame in seconds
    }

    void OnLateUpdate(float deltaTime) override {
        // Called after OnUpdate - useful for camera or post-processing logic
    }

    void OnEnable() override {
        // Called when the script is enabled
    }

    void OnDisable() override {
        // Called when the script is disabled
    }

    void OnDestroy() override {
        // Called when the script is about to be destroyed - cleanup here
    }

private:
    // Add your script members here
};
)";
                
                // Implementation file content
                std::string implContent = R"(#include ")" + sanitized + R"(.h"

// Add any implementation details here if needed
// You can use this file for out-of-line method definitions if your header gets complex
)";
                
                fs::path headerPath = currentPath / (sanitized + ".h");
                fs::path implPath = currentPath / (sanitized + ".cpp");
                
                if (fs::exists(headerPath) || fs::exists(implPath)) {
                    scriptPopupError = "A script with that name already exists.";
                } else {
                    std::error_code createErr;
                    fs::create_directories(currentPath, createErr);
                    if (createErr) {
                        scriptPopupError = "Failed to create script directory: " + std::string(createErr.message());
                    } else {
                        // Write header file
                        std::ofstream headerFile(headerPath);
                        if (!headerFile) {
                            scriptPopupError = "Failed to create header file.";
                        } else {
                            headerFile << headerContent;
                            headerFile.close();
                            
                            // Write implementation file
                            std::ofstream implFile(implPath);
                            if (!implFile) {
                                scriptPopupError = "Failed to create implementation file.";
                                fs::remove(headerPath);
                            } else {
                                implFile << implContent;
                                implFile.close();
                                selectedPath = headerPath;
                                statusMessage = "Created script '" + sanitized + "'.";
                                ImGui::CloseCurrentPopup();
                                scriptPopupError.clear();
                            }
                        }
                    }
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##script")) {
            scriptPopupError.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // Lua Script creation popup
    if (openLuaScriptPopup) {
        ImGui::OpenPopup("CreateLuaScriptPopup");
        openLuaScriptPopup = false;
    }

    if (ImGui::BeginPopupModal("CreateLuaScriptPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create Lua Script");
        ImGui::Separator();
        ImGui::TextDisabled("Location: %s", relativeDisplay.c_str());
        ImGui::InputText("Script name##lua", luaScriptNameBuffer, sizeof(luaScriptNameBuffer));
        if (!luaScriptPopupError.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.93f, 0.36f, 0.31f, 1.0f), "%s", luaScriptPopupError.c_str());
        }

        ImGui::Spacing();

        if (ImGui::Button("Create##lua")) {
            std::string trimmed = TrimCopy(luaScriptNameBuffer);
            if (trimmed.empty()) {
                luaScriptPopupError = "Please enter a script name.";
            } else {
                std::string sanitized = SanitizeSceneFilename(trimmed);
                if (sanitized.empty()) {
                    sanitized = "script";
                }
                
                // Lua script content template
                std::string luaContent = R"(-- )" + sanitized + R"(.lua
-- Lua script for Hephaestus Engine

function onAwake()
    print("[)" + sanitized + R"(] Script awakened")
end

function onUpdate(deltaTime)
    -- Main game logic - called every frame
    -- deltaTime is time elapsed since last frame in seconds
end

function onLateUpdate(deltaTime)
    -- Called after onUpdate - useful for post-processing logic
end

function onDestroy()
    print("[)" + sanitized + R"(] Script destroyed")
end
)";

                // Write Lua file next to the user-selected location
                fs::path scriptPath = currentPath / (sanitized + ".lua");
                if (fs::exists(scriptPath)) {
                    luaScriptPopupError = "A Lua script with that name already exists here.";
                } else {
                    std::error_code createErr;
                    fs::create_directories(currentPath, createErr);
                    if (createErr) {
                        luaScriptPopupError = "Failed to create destination directory: " + std::string(createErr.message());
                    } else {
                        std::ofstream luaFile(scriptPath);
                        if (!luaFile) {
                            luaScriptPopupError = "Failed to create Lua file.";
                        } else {
                            luaFile << luaContent;
                            luaFile.close();
                            selectedPath = scriptPath;
                            statusMessage = "Created Lua script '" + sanitized + ".lua'.";
                            ImGui::CloseCurrentPopup();
                            luaScriptPopupError.clear();
                        }
                    }
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##lua")) {
            luaScriptPopupError.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // Delete confirmation popup
    if (openDeleteConfirmPopup) {
        ImGui::OpenPopup("DeleteConfirmPopup");
        openDeleteConfirmPopup = false;
    }

    if (ImGui::BeginPopupModal("DeleteConfirmPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string itemName = deleteConfirmPath.filename().string();
        std::string itemType = deleteConfirmIsFolder ? "folder" : "file";
        
        ImGui::TextUnformatted("Confirm deletion");
        ImGui::Separator();
        ImGui::TextWrapped("Are you sure you want to delete this %s?\n\n\"%s\"\n\nThis action cannot be undone.", 
            itemType.c_str(), itemName.c_str());
        
        if (!deletePopupError.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.93f, 0.36f, 0.31f, 1.0f), "%s", deletePopupError.c_str());
        }

        ImGui::Spacing();

        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            std::error_code deleteErr;
            if (deleteConfirmIsFolder) {
                size_t removed = fs::remove_all(deleteConfirmPath, deleteErr);
                if (deleteErr) {
                    deletePopupError = "Failed to delete folder: " + std::string(deleteErr.message());
                } else {
                    statusMessage = "Deleted folder '" + itemName + "' (" + std::to_string(removed) + " items removed).";
                    selectedPath.clear();
                    ImGui::CloseCurrentPopup();
                    deletePopupError.clear();
                }
            } else {
                fs::remove(deleteConfirmPath, deleteErr);
                if (deleteErr) {
                    deletePopupError = "Failed to delete file: " + std::string(deleteErr.message());
                } else {
                    statusMessage = "Deleted file '" + itemName + "'.";
                    selectedPath.clear();
                    ImGui::CloseCurrentPopup();
                    deletePopupError.clear();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##delete", ImVec2(120, 0))) {
            deletePopupError.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (currentPath != assetRoot) {
        if (ImGui::Button("Up")) {
            fs::path parent = currentPath.parent_path();
            currentPath = parent.empty() ? assetRoot : ClampToRoot(parent, assetRoot);
            if (!PathInsideRoot(assetRoot, selectedPath)) {
                selectedPath.clear();
            }
            statusMessage.clear();
        }
    }

    ImGui::Separator();

    std::error_code iterErr;
    fs::directory_iterator dirIt(currentPath, iterErr);
    if (iterErr) {
    ImGui::TextDisabled("Cannot open directory: %s", iterErr.message().c_str());
    ImGui::End();
    selectedPath.clear();
    ClearSelectedScriptAsset();
    return result;
    }

    struct BrowserEntry {
        std::string label;
        fs::path path;
        bool isDirectory;
    };

    std::vector<BrowserEntry> directories;
    std::vector<BrowserEntry> files;

    for (auto it = dirIt; it != fs::directory_iterator(); ++it) {
        const fs::directory_entry& entry = *it;
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') {
            continue;
        }

        BrowserEntry item;
        item.label = name;
        item.path = entry.path();
        item.isDirectory = entry.is_directory();

        if (item.isDirectory) {
            directories.push_back(item);
        } else {
            files.push_back(item);
        }
    }

    auto alphabetical = [](const BrowserEntry& a, const BrowserEntry& b) {
        return a.label < b.label;
    };

    std::sort(directories.begin(), directories.end(), alphabetical);
    std::sort(files.begin(), files.end(), alphabetical);

    size_t totalEntries = directories.size() + files.size();

    const ImGuiStyle& style = ImGui::GetStyle();
    float footerReserve = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    footerReserve += ImGui::GetTextLineHeightWithSpacing() * 3.0f;
    footerReserve += style.ItemSpacing.y * 4.0f;
    footerReserve = std::max(footerReserve, 120.0f * g_UIScale);

    // Leave space for footer controls so the import button stays visible.
    ImGui::BeginChild("AssetBrowserList", ImVec2(0.0f, -footerReserve), false);

    if (ImGui::BeginPopupContextWindow("AssetBrowserListContext", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("New Folder...")) {
            triggerFolderCreatePopup();
        }
        if (ImGui::MenuItem("New Material...")) {
            triggerMaterialCreatePopup();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("New C++ Script...")) {
            triggerScriptCreatePopup();
        }
        if (ImGui::MenuItem("New Lua Script...")) {
            triggerLuaScriptCreatePopup();
        }
        ImGui::EndPopup();
    }

    if (directories.empty() && files.empty()) {
        ImGui::TextDisabled("Folder is empty");
    } else {
        ImGui::TextDisabled("%zu items", totalEntries);
        ImGui::Spacing();
        const float baseIconSize = 72.0f;
        const float iconSize = baseIconSize * g_UIScale;
        const float cellPadding = 14.0f * g_UIScale;
        const float textPaddingY = 6.0f * g_UIScale;
        const float minCellWidth = iconSize + cellPadding * 2.0f;
        const float labelHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        const float cellHeight = iconSize + cellPadding * 2.0f + labelHeight + textPaddingY;

        float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        int columns = static_cast<int>(availableWidth / minCellWidth);
        if (columns < 1) {
            columns = 1;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(cellPadding * 0.5f, cellPadding * 0.5f));
        if (ImGui::BeginTable("AssetBrowserGrid", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoBordersInBody)) {
            int cellIndex = 0;

            auto drawEntry = [&](const BrowserEntry& entry) {
                bool isSelected = !selectedPath.empty() && selectedPath == entry.path;
                if (cellIndex % columns == 0) {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, cellHeight);
                }
                ImGui::TableSetColumnIndex(cellIndex % columns);

                ImGui::PushID(entry.path.generic_string().c_str());

                ImVec2 cellSize(minCellWidth, cellHeight);
                ImVec2 cellPos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##AssetCell", cellSize);
                bool hovered = ImGui::IsItemHovered();
                bool clicked = ImGui::IsItemClicked();
                bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const float rounding = std::max(6.0f, 6.0f * g_UIScale);
                ImU32 bgColor = 0;
                if (isSelected) {
                    bgColor = ImGui::GetColorU32(ImVec4(0.26f, 0.54f, 0.99f, 0.30f));
                } else if (hovered) {
                    bgColor = ImGui::GetColorU32(ImVec4(0.33f, 0.53f, 0.87f, 0.18f));
                }
                if (bgColor != 0) {
                    ImVec2 cellMax(cellPos.x + cellSize.x, cellPos.y + cellSize.y);
                    drawList->AddRectFilled(cellPos, cellMax, bgColor, rounding);
                }

                std::string iconKey = DetermineAssetIconKey(entry.path, entry.isDirectory);
                std::string badgeText = DetermineAssetBadgeText(entry.path, entry.isDirectory);
                ImVec4 badgeColor = DetermineAssetBadgeColor(iconKey, entry.isDirectory);
                const float badgeHeight = iconSize;
                ImVec2 badgeMin(
                    cellPos.x + cellPadding,
                    cellPos.y + cellPadding);
                ImVec2 badgeMax(
                    cellPos.x + cellSize.x - cellPadding,
                    badgeMin.y + badgeHeight);
                badgeMax.y = std::min(badgeMax.y, cellPos.y + cellSize.y - cellPadding - textPaddingY);
                drawList->AddRectFilled(badgeMin, badgeMax, ImGui::GetColorU32(badgeColor), rounding);
                drawList->AddRect(badgeMin, badgeMax, ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.07f, badgeColor.w)), rounding, 0, 1.0f);

                ImVec2 textSize = ImGui::CalcTextSize(badgeText.c_str());
                ImVec2 textPos(
                    badgeMin.x + std::max(0.0f, (badgeMax.x - badgeMin.x - textSize.x) * 0.5f),
                    badgeMin.y + std::max(0.0f, (badgeMax.y - badgeMin.y - textSize.y) * 0.5f));
                drawList->AddText(textPos, ImGui::GetColorU32(ImVec4(0.95f, 0.97f, 1.0f, 1.0f)), badgeText.c_str());

                float finalIconHeight = badgeMax.y - badgeMin.y;

                ImRect textRect(
                    ImVec2(cellPos.x + cellPadding, cellPos.y + cellPadding + finalIconHeight + textPaddingY),
                    ImVec2(cellPos.x + cellSize.x - cellPadding, cellPos.y + cellSize.y - cellPadding));

                ImGui::PushClipRect(textRect.Min, textRect.Max, true);
                ImGui::RenderTextClipped(
                    textRect.Min,
                    textRect.Max,
                    entry.label.c_str(),
                    nullptr,
                    nullptr,
                    ImVec2(0.5f, 0.0f));
                ImGui::PopClipRect();

                if (clicked) {
                    selectedPath = ClampToRoot(entry.path, assetRoot);
                    statusMessage.clear();
                }
                if (doubleClicked) {
                    if (entry.isDirectory) {
                        currentPath = ClampToRoot(entry.path, assetRoot);
                        selectedPath.clear();
                    } else {
                        std::filesystem::path clamped = ClampToRoot(entry.path, assetRoot);
                        selectedPath = clamped;
                        if (IsSceneFile(entry.path)) {
                            result.sceneOpenRequest = clamped;
                            statusMessage.clear();
                        } else if (IsMaterialFile(entry.path)) {
                            result.materialOpenRequest = clamped;
                            statusMessage.clear();
                        } else if (IsScriptFile(entry.path)) {
                            OpenFileInPreferredIDE(clamped);
                            statusMessage = "Opening '" + clamped.filename().string() + "' in your default IDE...";
                        } else {
                            statusMessage.clear();
                        }
                    }
                }

                // Right-click context menu for individual items
                if (ImGui::BeginPopupContextItem("AssetItemContext")) {
                    if (entry.isDirectory) {
                        if (ImGui::MenuItem("Delete Folder")) {
                            triggerDeleteConfirmPopup(entry.path, true);
                        }
                    } else {
                        if (ImGui::MenuItem("Delete File")) {
                            triggerDeleteConfirmPopup(entry.path, false);
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
                ++cellIndex;
            };

            for (const BrowserEntry& dirEntry : directories) {
                drawEntry(dirEntry);
            }
            for (const BrowserEntry& fileEntry : files) {
                drawEntry(fileEntry);
            }

            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    ImGui::EndChild();

    if (!selectedPath.empty()) {
        std::error_code selectedErr;
        fs::path selectedRelative = fs::relative(selectedPath, assetRoot, selectedErr);
        std::string selectedDisplay;
        if (selectedErr || selectedRelative.empty() || selectedRelative == ".") {
            selectedDisplay = selectedPath.filename().string();
        } else {
            selectedDisplay = selectedRelative.generic_string();
        }

        ImGui::Separator();
        ImGui::Text("Selected: %s", selectedDisplay.c_str());
    }

    std::error_code selectedFileErr;
    bool selectedIsFile = !selectedPath.empty() && fs::is_regular_file(selectedPath, selectedFileErr) && !selectedFileErr;
    bool supportedFormat = selectedIsFile && ModelImporter::IsSupportedExtension(selectedPath);

    if (ImGui::Button("Import Model")) {
        if (supportedFormat) {
            result.modelImportRequest = selectedPath;
        } else {
            if (!selectedIsFile) {
                statusMessage = "Select a model file to import";
            } else {
                statusMessage = "Supported formats: .obj, .fbx (ASCII)";
            }
        }
    }

    if (selectedIsFile && !supportedFormat) {
        ImGui::TextDisabled("Supported formats: .obj, .fbx (ASCII)");
    } else if (!selectedIsFile && !selectedPath.empty()) {
        ImGui::TextDisabled("Select a file to enable import");
    }

    if (!statusMessage.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", statusMessage.c_str());
    }

    UpdateSelectedScriptAsset(selectedPath);

    ImGui::End();
    return result;
}

static std::optional<std::filesystem::path> DrawProjectBrowser()
{
    namespace fs = std::filesystem;

    std::optional<fs::path> selection;

    static std::array<char, 128> s_NewProjectBuffer{};
    static std::string s_SelectedProjectName;
    static std::string s_ErrorMessage;
    static std::string s_StatusMessage;
    static std::array<char, 1024> s_ProjectLocationBuffer{};
    static std::filesystem::path s_LastProjectsRoot;

    fs::path projectsRoot = ResolveProjectsRoot();
    // Track when projects root changes to keep it updated locally
    if (projectsRoot != s_LastProjectsRoot) {
        s_LastProjectsRoot = projectsRoot;
    }
    std::error_code scanErr;
    std::string enumerationError;
    std::vector<fs::path> projectPaths;
    fs::directory_options options = fs::directory_options::skip_permission_denied;
    for (fs::directory_iterator it(projectsRoot, options, scanErr);
         !scanErr && it != fs::directory_iterator(); ++it) {
        if (it->is_directory()) {
            projectPaths.push_back(it->path().lexically_normal());
        }
    }
    if (scanErr) {
        enumerationError = "Failed to enumerate projects: " + scanErr.message();
    }

    std::sort(projectPaths.begin(), projectPaths.end(), [](const fs::path& a, const fs::path& b) {
        return ToLowerCopy(a.filename().string()) < ToLowerCopy(b.filename().string());
    });

    int selectedIndex = -1;
    for (size_t i = 0; i < projectPaths.size(); ++i) {
        std::string name = projectPaths[i].filename().string();
        if (!s_SelectedProjectName.empty() && s_SelectedProjectName == name) {
            selectedIndex = static_cast<int>(i);
            break;
        }
    }
    if (selectedIndex == -1 && !s_SelectedProjectName.empty()) {
        s_SelectedProjectName.clear();
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(640.0f, 420.0f), ImGuiCond_Appearing);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoMove;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    if (ImGui::Begin("Project Browser", nullptr, windowFlags)) {
        ImGui::Text("Select or create a project to continue");
        std::string rootDisplay = projectsRoot.generic_string();
        std::string locationDisplayInput = TrimCopy(s_ProjectLocationBuffer.data());
        fs::path locationDisplayRoot = locationDisplayInput.empty() ? projectsRoot : fs::path(locationDisplayInput);
        locationDisplayRoot = locationDisplayRoot.lexically_normal();
        ImGui::TextDisabled("Projects directory: %s", rootDisplay.c_str());
        if (!enumerationError.empty()) {
            ImGui::TextColored(ImVec4(0.90f, 0.36f, 0.36f, 1.0f), "%s", enumerationError.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();

        float listHeight = std::max(220.0f, ImGui::GetTextLineHeightWithSpacing() * 8.0f);
        if (ImGui::BeginChild("##ProjectList", ImVec2(0.0f, listHeight), true, ImGuiWindowFlags_HorizontalScrollbar)) {
            for (size_t i = 0; i < projectPaths.size(); ++i) {
                const fs::path& path = projectPaths[i];
                std::string name = path.filename().string();
                bool isSelected = static_cast<int>(i) == selectedIndex;
                ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
                if (ImGui::Selectable(name.c_str(), isSelected, flags)) {
                    s_SelectedProjectName = name;
                    selectedIndex = static_cast<int>(i);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        selection = path;
                        s_ErrorMessage.clear();
                        s_StatusMessage.clear();
                        break;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    std::string tooltip = path.generic_string();
                    ImGui::SetTooltip("%s", tooltip.c_str());
                }
            }
        }
        ImGui::EndChild();

        if (projectPaths.empty() && s_ErrorMessage.empty()) {
            ImGui::TextDisabled("No projects found. Create one below.");
        }

        bool canOpen = selectedIndex >= 0 && selectedIndex < static_cast<int>(projectPaths.size());
        if (!canOpen) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Open Project", ImVec2(160.0f, 0.0f)) && canOpen) {
            selection = projectPaths[static_cast<size_t>(selectedIndex)];
            s_ErrorMessage.clear();
            s_StatusMessage.clear();
        }
        if (!canOpen) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Double-click a project name to open it");

        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Project Location:");
        ImGui::PushItemWidth(-140.0f);
        ImGui::InputTextWithHint("##NewProjectLocation", "Leave blank to use the default directory", s_ProjectLocationBuffer.data(), s_ProjectLocationBuffer.size());
        ImGui::PopItemWidth();
    #ifdef _WIN32
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(120.0f, 0.0f))) {
            std::string currentLocation = TrimCopy(s_ProjectLocationBuffer.data());
            fs::path baseLocation = currentLocation.empty() ? projectsRoot : fs::path(currentLocation);
            if (auto picked = PromptDirectoryPicker(baseLocation)) {
            std::string normalized = picked->lexically_normal().generic_string();
            std::fill(s_ProjectLocationBuffer.begin(), s_ProjectLocationBuffer.end(), 0);
            std::strncpy(s_ProjectLocationBuffer.data(), normalized.c_str(), s_ProjectLocationBuffer.size() - 1);
            }
        }
    #else
        ImGui::SameLine();
        ImGui::TextDisabled("Browse (Windows only)");
    #endif
        ImGui::TextDisabled("Current target: %s", locationDisplayRoot.generic_string().c_str());
        ImGui::Separator();
        ImGui::Text("Create New Project");

        ImGui::PushItemWidth(-140.0f);
        ImGui::InputTextWithHint("##NewProjectName", "Project name", s_NewProjectBuffer.data(), s_NewProjectBuffer.size());
        bool enterPressed = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter);
        ImGui::PopItemWidth();

        ImGui::SameLine();
        bool createClicked = ImGui::Button("Create", ImVec2(120.0f, 0.0f));

        if (createClicked || enterPressed) {
            std::cout << "[ProjectCreation] Create button clicked or Enter pressed" << std::endl;
            std::string rawName(s_NewProjectBuffer.data());
            std::string trimmed = TrimCopy(rawName);
            if (trimmed.empty()) {
                s_ErrorMessage = "Project name cannot be empty.";
                s_StatusMessage.clear();
            } else {
                std::string sanitized = trimmed;
                for (char& ch : sanitized) {
                    unsigned char uc = static_cast<unsigned char>(ch);
                    if (std::isalnum(uc) || ch == ' ' || ch == '_' || ch == '-') {
                        continue;
                    }
                    ch = '_';
                }
                while (!sanitized.empty() && (sanitized.back() == ' ' || sanitized.back() == '.')) {
                    sanitized.pop_back();
                }
                if (sanitized.empty()) {
                    s_ErrorMessage = "Project name cannot end with spaces or dots.";
                    s_StatusMessage.clear();
                } else {
                    std::string locationOverride = TrimCopy(s_ProjectLocationBuffer.data());
                    fs::path baseLocation = locationOverride.empty() ? projectsRoot : fs::path(locationOverride);
                    baseLocation = baseLocation.lexically_normal();
                    fs::path newProjectPath = (baseLocation / sanitized).lexically_normal();
                    if (fs::exists(newProjectPath)) {
                        s_ErrorMessage = "A project with that name already exists.";
                        s_StatusMessage.clear();
                    } else {
                        std::error_code createErr;
                        fs::create_directories(newProjectPath, createErr);
                        if (createErr) {
                            s_ErrorMessage = "Failed to create project: " + createErr.message();
                            s_StatusMessage.clear();
                        } else {
                            const std::vector<fs::path> subdirs = {
                                newProjectPath / "assets",
                                newProjectPath / "assets" / "models",
                                newProjectPath / "assets" / "textures",
                                newProjectPath / "assets" / "scripts",
                                newProjectPath / "assets" / "audio",
                                newProjectPath / "assets" / "materials",
                                newProjectPath / "assets" / "prefabs",
                                newProjectPath / "assets" / "scenes"
                            };
                            std::error_code subErr;
                            bool subdirFailure = false;
                            for (const auto& dir : subdirs) {
                                fs::create_directories(dir, subErr);
                                if (subErr) {
                                    s_ErrorMessage = "Failed to prepare project folders: " + subErr.message();
                                    s_StatusMessage.clear();
                                    subdirFailure = true;
                                    break;
                                }
                            }
                            if (!subdirFailure) {
                                // Copy engine assets to the new project
                                fs::path engineRoot = ResolveEngineRoot();
                                fs::path engineMeshesPath = engineRoot / "assets" / "meshes";
                                fs::path engineTexturesPath = engineRoot / "assets" / "textures";
                                fs::path projectModelsPath = newProjectPath / "assets" / "models";
                                fs::path projectTexturesPath = newProjectPath / "assets" / "textures";
                                
                                std::cout << "\n[ProjectCreation] Starting asset copy..." << std::endl;
                                std::cout << "  Engine root: " << engineRoot << std::endl;
                                std::cout << "  Engine meshes path: " << engineMeshesPath << " (exists: " << fs::exists(engineMeshesPath) << ")" << std::endl;
                                std::cout << "  Engine textures path: " << engineTexturesPath << " (exists: " << fs::exists(engineTexturesPath) << ")" << std::endl;
                                std::cout << "  Project models path: " << projectModelsPath << " (exists: " << fs::exists(projectModelsPath) << ")" << std::endl;
                                std::cout << "  Project textures path: " << projectTexturesPath << " (exists: " << fs::exists(projectTexturesPath) << ")" << std::endl;
                                
                                bool copyFailure = false;
                                std::error_code copyErr;
                                int meshesCopied = 0;
                                int texturesCopied = 0;
                                
                                // Copy meshes to models folder
                                if (fs::exists(engineMeshesPath)) {
                                    std::cout << "  Copying meshes..." << std::endl;
                                    for (const auto& entry : fs::directory_iterator(engineMeshesPath)) {
                                        if (fs::is_regular_file(entry)) {
                                            fs::copy_file(entry.path(), projectModelsPath / entry.path().filename(), 
                                                        fs::copy_options::overwrite_existing, copyErr);
                                            if (copyErr) {
                                                std::cout << "    ERROR: Failed to copy " << entry.path().filename() << ": " << copyErr.message() << std::endl;
                                                s_ErrorMessage = "Failed to copy engine meshes: " + copyErr.message();
                                                s_StatusMessage.clear();
                                                copyFailure = true;
                                                break;
                                            }
                                            meshesCopied++;
                                            std::cout << "    Copied: " << entry.path().filename() << std::endl;
                                        }
                                    }
                                } else {
                                    std::cout << "  Engine meshes path does not exist - skipping mesh copy" << std::endl;
                                }
                                
                                // Copy textures to textures folder
                                if (!copyFailure && fs::exists(engineTexturesPath)) {
                                    std::cout << "  Copying textures..." << std::endl;
                                    for (const auto& entry : fs::directory_iterator(engineTexturesPath)) {
                                        if (fs::is_regular_file(entry)) {
                                            fs::copy_file(entry.path(), projectTexturesPath / entry.path().filename(), 
                                                        fs::copy_options::overwrite_existing, copyErr);
                                            if (copyErr) {
                                                std::cout << "    ERROR: Failed to copy " << entry.path().filename() << ": " << copyErr.message() << std::endl;
                                                s_ErrorMessage = "Failed to copy engine textures: " + copyErr.message();
                                                s_StatusMessage.clear();
                                                copyFailure = true;
                                                break;
                                            }
                                            texturesCopied++;
                                            std::cout << "    Copied: " << entry.path().filename() << std::endl;
                                        }
                                    }
                                } else if (!copyFailure) {
                                    std::cout << "  Engine textures path does not exist - skipping texture copy" << std::endl;
                                }
                                
                                std::cout << "  Summary: Meshes copied=" << meshesCopied << ", Textures copied=" << texturesCopied << std::endl;
                                
                                if (!copyFailure) {
                                    s_ErrorMessage.clear();
                                    s_StatusMessage = "Created project '" + sanitized + "'.";
                                    s_SelectedProjectName = sanitized;
                                    std::fill(s_NewProjectBuffer.begin(), s_NewProjectBuffer.end(), 0);
                                    selection = newProjectPath;
                                    std::cout << "[ProjectCreation] Project created successfully!\n" << std::endl;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!s_ErrorMessage.empty()) {
            ImGui::TextColored(ImVec4(0.90f, 0.36f, 0.36f, 1.0f), "%s", s_ErrorMessage.c_str());
        } else if (!s_StatusMessage.empty()) {
            ImGui::TextColored(ImVec4(0.32f, 0.80f, 0.60f, 1.0f), "%s", s_StatusMessage.c_str());
        }

        ImGui::Separator();
        ImGui::TextDisabled("Projects are stored inside: %s", rootDisplay.c_str());
        ImGui::TextDisabled("Current creation target: %s", locationDisplayRoot.generic_string().c_str());
    }
    ImGui::End();

    ImGui::PopStyleVar(2);

    return selection;
}

// ──────────────────────────────────────────────────────────────────────────────
// Load and set window icon
// ──────────────────────────────────────────────────────────────────────────────
static void SetWindowIcon(GLFWwindow* window)
{
    namespace fs = std::filesystem;

    struct IconSpec {
        const char* relativePath;
    };

    const std::array<IconSpec, 6> iconSpecs = {{
        {"editor/Icons/logo/256px.png"},
        {"editor/Icons/logo/128px.png"},
        {"editor/Icons/logo/64px.png"},
        {"editor/Icons/logo/48px.png"},
        {"editor/Icons/logo/32px.png"},
        {"editor/Icons/logo/16px.png"}
    }};

    std::vector<GLFWimage> images;
    std::vector<unsigned char*> allocatedPixels;
    images.reserve(iconSpecs.size());
    allocatedPixels.reserve(iconSpecs.size());

    for (const auto& spec : iconSpecs) {
        fs::path iconPath = ResolveEnginePath(fs::path(spec.relativePath));
        if (!fs::exists(iconPath)) {
            std::cout << "[SetWindowIcon] Missing icon file: " << iconPath << std::endl;
            continue;
        }

        std::string pathUtf8 = iconPath.string();
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(pathUtf8.c_str(), &width, &height, &channels, 4);
        if (!pixels) {
            std::cout << "[SetWindowIcon] Failed to load icon: " << iconPath << std::endl;
            continue;
        }

        GLFWimage image{};
        image.width = width;
        image.height = height;
        image.pixels = pixels;
        images.push_back(image);
        allocatedPixels.push_back(pixels);

        std::cout << "[SetWindowIcon] Loaded icon: " << iconPath << " (" << width << "x" << height << ")" << std::endl;
    }

    if (!images.empty()) {
        glfwSetWindowIcon(window, static_cast<int>(images.size()), images.data());
        std::cout << "[SetWindowIcon] Applied " << images.size() << " icon candidate(s)." << std::endl;
    } else {
        std::cout << "[SetWindowIcon] No icon images loaded. Using default window icon." << std::endl;
        std::cout << "[SetWindowIcon] Looking for PNGs in editor/Icons/logo/." << std::endl;
    }

    for (unsigned char* pixels : allocatedPixels) {
        stbi_image_free(pixels);
    }
}

int main(int argc, char* argv[])
{
    const std::filesystem::path executableDir = GetExecutableDirectory();
    bool autoRuntime = DetectRuntimePackage(executableDir);
    bool runtimeRequested = false;
    bool editorRequested = false;

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--runtime" || arg == "-r") {
            runtimeRequested = true;
        } else if (arg == "--editor") {
            editorRequested = true;
        } else if (arg == "--scene" || arg == "-s") {
            if (i + 1 < argc) {
                g_RuntimeScenePath = argv[++i];
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Hephaestus Engine\n";
            std::cout << "Usage: HephaestusEngine [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --runtime, -r          Run in runtime mode (no editor)\n";
            std::cout << "  --editor               Force editor mode even in runtime packages\n";
            std::cout << "  --scene <path>, -s     Load specific scene file\n";
            std::cout << "  --help, -h             Show this help message\n";
            return 0;
        }
    }

    if (editorRequested) {
        g_RuntimeMode = false;
    } else if (runtimeRequested) {
        g_RuntimeMode = true;
    } else {
        g_RuntimeMode = autoRuntime;
    }

    if (g_RuntimeMode) {
        SetActiveProjectRoot(executableDir);
        std::cout << "[Runtime] Project root: " << executableDir << std::endl;
    }

    // In runtime mode, auto-discover scene if not specified
    if (g_RuntimeMode && g_RuntimeScenePath.empty()) {
        namespace fs = std::filesystem;
        fs::path scenesDir = executableDir / "assets" / "scenes";
        if (fs::exists(scenesDir)) {
            for (const auto& entry : fs::directory_iterator(scenesDir)) {
                if (entry.path().extension() == ".hpscene") {
                    g_RuntimeScenePath = entry.path().string();
                    std::cout << "[Runtime] Auto-discovered scene: " << g_RuntimeScenePath << std::endl;
                    break;
                }
            }
        }
    }

    // ── GLFW Window ───────────────────────────────
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    const char* windowTitle = g_RuntimeMode ? "Hephaestus Runtime" : "Hephaestus Engine";
    GLFWwindow* window = glfwCreateWindow(1280, 720, windowTitle, nullptr, nullptr);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetScrollCallback(window, scrollCallback);

    // Set window icon
    SetWindowIcon(window);

    // ── Vulkan Instance ───────────────────────────
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hephaestus Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Hephaestus Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t extCount = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&extCount);

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &appInfo;
    ici.enabledExtensionCount = extCount;
    ici.ppEnabledExtensionNames = exts;

    VkDebugUtilsMessengerCreateInfoEXT dbgCI{};
    if (enableValidationLayers) {
        ici.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        ici.ppEnabledLayerNames = validationLayers.data();
        populateDebugCreateInfo(dbgCI);
        ici.pNext = &dbgCI;
    }

    VkInstance instance{};
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan instance!\n";
        return -1;
    }

    VkDebugUtilsMessengerEXT debugMessenger{};
    if (enableValidationLayers) {
        populateDebugCreateInfo(dbgCI);
        CreateDebugUtilsMessengerEXT(instance, &dbgCI, nullptr, &debugMessenger);
    } else {
        std::cout << "Validation layers disabled. Build a Debug configuration to enable Vulkan validation output." << std::endl;
    }

    // ── Device + Swapchain ───────────────────────
    VulkanDevice vkdev;
    vkdev.Init(window, instance);

    VulkanSwapchain swap;
    swap.Init(vkdev.physicalDevice, vkdev.device, vkdev.surface, window, vkdev.allocator);

    // ── Pipeline ─────────────────────────────────
    VulkanPipeline pipeline;
    auto createPipeline = [&]() {
        pipeline.Cleanup();
        pipeline.Init(vkdev.device, swap.renderPass, swap.extent);
        pipeline.CreateOutlinePipeline();
    };
    try {
        createPipeline();
    } catch (const std::exception& e) {
        std::cerr << "Pipeline error: " << e.what() << std::endl;
        system("pause");
        return -1;
    }

    // -- Custom Vulkan Gizmo
    VulkanGizmo vulkanGizmo;
    vulkanGizmo.Init(vkdev.device, swap.renderPass, swap.extent, pipeline.descriptorSetLayout, vkdev.allocator);
    bool gizmoDragging = false;
    int gizmoActiveAxis = -1;

    // -- Skybox for HDRI backgrounds
    VulkanSkybox skybox;
    try {
        skybox.Init(vkdev.device, swap.renderPass, swap.extent, vkdev.allocator, 
                    vkdev.physicalDevice, vkdev.commandPool, vkdev.graphicsQueue,
                    static_cast<uint32_t>(swap.images.size()),
                    ResolveEngineRoot().string());
    } catch (const std::exception& e) {
        std::cerr << "Skybox initialization error: " << e.what() << std::endl;
        system("pause");
        return -1;
    }

    bool projectManagerInitialized = false;
    if (!g_RuntimeMode) {
        g_ProjectManager.Initialize(&vkdev);
        g_ProjectManager.SetProjectsRoot(ResolveProjectsRoot());
        projectManagerInitialized = true;
    }

    LoadEditorSettings();

    std::vector<MeshGPU> meshLibrary;
    meshLibrary.reserve(32);
    std::unordered_map<std::string, size_t> meshLibraryLookup;
    meshLibraryLookup.reserve(64);

    auto makeMeshSourceKey = [&](const std::filesystem::path& sourcePath) -> std::string {
        if (sourcePath.empty()) {
            return {};
        }
        std::string raw = sourcePath.generic_string();
        if (raw.rfind("builtin://", 0) == 0) {
            return raw;
        }
        std::filesystem::path normalized = sourcePath.lexically_normal();
        std::filesystem::path projectRoot = ResolveActiveProjectRoot();
        if (!projectRoot.empty()) {
            std::filesystem::path relative = normalized.lexically_relative(projectRoot);
            std::string relStr = relative.generic_string();
            if (!relative.empty() && relStr.find("..") == std::string::npos && !relStr.empty()) {
                return relStr;
            }
        }
        return normalized.generic_string();
    };

    auto resolveMeshSourceKeyToPath = [&](const std::string& key) -> std::filesystem::path {
        if (key.empty()) {
            return {};
        }
        if (key.rfind("builtin://", 0) == 0) {
            return std::filesystem::path(key);
        }
        namespace fs = std::filesystem;
        fs::path stored = fs::path(key).lexically_normal();
        fs::path projectRoot = ResolveActiveProjectRoot();
        if (!projectRoot.empty()) {
            fs::path candidate = (projectRoot / stored).lexically_normal();
            if (fs::exists(candidate)) {
                return candidate;
            }
            fs::path assetsRoot = ResolveActiveAssetsRoot();
            if (!assetsRoot.empty()) {
                candidate = (assetsRoot / stored).lexically_normal();
                if (fs::exists(candidate)) {
                    return candidate;
                }
            }
        }
        if (fs::exists(stored)) {
            return stored;
        }
        return stored;
    };

    auto uploadMesh = [&](const std::string& name, const MeshData& data, const std::filesystem::path& sourcePath, bool imported) -> size_t {
        std::cout << "[uploadMesh] Received mesh '" << name << "' (verts=" << data.vertices.size()
                  << ", indices=" << data.indices.size() << ") from " << sourcePath.string() << std::endl;
        if (data.vertices.empty() || data.indices.empty()) {
            throw std::runtime_error("Mesh has no geometry");
        }

        if (data.indices.size() % 3 != 0) {
            std::cerr << "[uploadMesh] Warning: index count not divisible by 3 (count="
                      << data.indices.size() << ")" << std::endl;
        }

        MeshGPU mesh{};
        mesh.name = name;
        mesh.sourcePath = sourcePath;
        mesh.sourceKey = makeMeshSourceKey(sourcePath);
        mesh.vertexCount = static_cast<uint32_t>(data.vertices.size());
        mesh.indexCount = static_cast<uint32_t>(data.indices.size());
        mesh.bounds = data.bounds;
        mesh.imported = imported;

        VkDeviceSize vertexBufferSize = sizeof(Vertex) * data.vertices.size();
        std::cout << "[uploadMesh] Creating vertex buffer (" << vertexBufferSize << " bytes)" << std::endl;
        mesh.vertexBuffer.Create(
            vkdev.allocator,
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        void* mapped = nullptr;
        VkResult mapResult = vmaMapMemory(vkdev.allocator, mesh.vertexBuffer.allocation, &mapped);
        if (mapResult != VK_SUCCESS || mapped == nullptr) {
            throw std::runtime_error("vmaMapMemory failed for vertex buffer");
        }
        std::memcpy(mapped, data.vertices.data(), static_cast<size_t>(vertexBufferSize));
        vmaFlushAllocation(vkdev.allocator, mesh.vertexBuffer.allocation, 0, VK_WHOLE_SIZE);
        vmaUnmapMemory(vkdev.allocator, mesh.vertexBuffer.allocation);

        VkDeviceSize indexBufferSize = sizeof(uint32_t) * data.indices.size();
        std::cout << "[uploadMesh] Creating index buffer (" << indexBufferSize << " bytes)" << std::endl;
        mesh.indexBuffer.Create(
            vkdev.allocator,
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        mapResult = vmaMapMemory(vkdev.allocator, mesh.indexBuffer.allocation, &mapped);
        if (mapResult != VK_SUCCESS || mapped == nullptr) {
            throw std::runtime_error("vmaMapMemory failed for index buffer");
        }
        std::memcpy(mapped, data.indices.data(), static_cast<size_t>(indexBufferSize));
        vmaFlushAllocation(vkdev.allocator, mesh.indexBuffer.allocation, 0, VK_WHOLE_SIZE);
        vmaUnmapMemory(vkdev.allocator, mesh.indexBuffer.allocation);

        meshLibrary.push_back(std::move(mesh));
        size_t handle = meshLibrary.size() - 1;
        if (!meshLibrary[handle].sourceKey.empty()) {
            meshLibraryLookup[meshLibrary[handle].sourceKey] = handle;
        }
        std::cout << "[uploadMesh] Mesh '" << name << "' uploaded to handle " << handle << std::endl;
        return handle;
    };

    // ── Vertex Data ───────────────────────────────────
    // 3D Cube vertices with normals - we need unique vertices per face for proper lighting
    const std::vector<Vertex> cubeVertices = {
        // Front face (Z+) - all normals point forward (0, 0, 1)
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},  // 0
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},  // 1
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},  // 2
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},  // 3

        // Back face (Z-) - normals point backward (0, 0, -1)
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},  // 4
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},  // 5
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},  // 6
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},  // 7

        // Right face (X+) - normals point right (1, 0, 0)
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},  // 8
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},  // 9
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},  // 10
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},  // 11

        // Left face (X-) - normals point left (-1, 0, 0)
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},  // 12
        {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},  // 13
        {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},  // 14
        {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},  // 15

        // Top face (Y+) - normals point up (0, 1, 0)
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},  // 16
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},  // 17
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},  // 18
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},  // 19

        // Bottom face (Y-) - normals point down (0, -1, 0)
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},  // 20
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},  // 21
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},  // 22
        {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},  // 23
    };
    
    // Index buffer for cube (12 triangles, 2 per face, 6 faces)
    const std::vector<uint32_t> cubeIndices = {
        // Front face
        0, 1, 2,  2, 3, 0,
        // Back face
        4, 5, 6,  6, 7, 4,
        // Right face
        8, 9, 10,  10, 11, 8,
        // Left face
        12, 13, 14,  14, 15, 12,
        // Top face
        16, 17, 18,  18, 19, 16,
        // Bottom face
        20, 21, 22,  22, 23, 20
    };

    MeshData cubeMeshData;
    cubeMeshData.vertices = cubeVertices;
    cubeMeshData.indices = cubeIndices;
    cubeMeshData.bounds = AABB(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));

    // Generate cone mesh for spotlight visualization
    auto generateConeMesh = [](float height, float radius, uint32_t segments) {
        MeshData data;
        const float pi = 3.14159265359f;
        const float twoPi = pi * 2.0f;
        
        // Reserve space
        data.vertices.reserve(segments + 2); // tip + base ring + center
        data.indices.reserve(segments * 6 + segments * 3); // sides + base
        
        // Add cone tip at origin
        Vertex tipVertex{};
        tipVertex.pos[0] = 0.0f;
        tipVertex.pos[1] = 0.0f;
        tipVertex.pos[2] = 0.0f;
        tipVertex.normal[0] = 0.0f;
        tipVertex.normal[1] = 0.0f;
        tipVertex.normal[2] = 1.0f;
        tipVertex.color[0] = 0.0f;
        tipVertex.color[1] = 1.0f;
        tipVertex.color[2] = 1.0f;
        tipVertex.texCoord[0] = 0.5f;
        tipVertex.texCoord[1] = 0.5f;
        data.vertices.push_back(tipVertex);
        
        // Add base ring vertices
        for (uint32_t i = 0; i < segments; ++i) {
            float angle = (float)i / segments * twoPi;
            float x = std::cos(angle) * radius;
            float z = std::sin(angle) * radius;
            
            Vertex v{};
            v.pos[0] = x;
            v.pos[1] = height;
            v.pos[2] = z;
            v.normal[0] = x;
            v.normal[1] = 0.0f;
            v.normal[2] = z;
            // Normalize normal
            float len = std::sqrt(v.normal[0]*v.normal[0] + v.normal[2]*v.normal[2]);
            if (len > 0) {
                v.normal[0] /= len;
                v.normal[2] /= len;
            }
            v.color[0] = 0.0f;
            v.color[1] = 1.0f;
            v.color[2] = 1.0f;
            v.texCoord[0] = (float)i / segments;
            v.texCoord[1] = 1.0f;
            data.vertices.push_back(v);
        }
        
        // Add center of base for capping
        Vertex centerVertex{};
        centerVertex.pos[0] = 0.0f;
        centerVertex.pos[1] = height;
        centerVertex.pos[2] = 0.0f;
        centerVertex.normal[0] = 0.0f;
        centerVertex.normal[1] = 1.0f;
        centerVertex.normal[2] = 0.0f;
        centerVertex.color[0] = 0.0f;
        centerVertex.color[1] = 1.0f;
        centerVertex.color[2] = 1.0f;
        centerVertex.texCoord[0] = 0.5f;
        centerVertex.texCoord[1] = 0.5f;
        data.vertices.push_back(centerVertex);
        
        // Add indices for cone sides
        uint32_t tipIdx = 0;
        uint32_t baseStart = 1;
        uint32_t centerIdx = baseStart + segments;
        
        for (uint32_t i = 0; i < segments; ++i) {
            uint32_t next = (i + 1) % segments;
            uint32_t i0 = baseStart + i;
            uint32_t i1 = baseStart + next;
            
            // Two triangles for side
            data.indices.push_back(tipIdx);
            data.indices.push_back(i0);
            data.indices.push_back(i1);
        }
        
        // Add indices for base cap
        for (uint32_t i = 0; i < segments; ++i) {
            uint32_t next = (i + 1) % segments;
            uint32_t i0 = baseStart + i;
            uint32_t i1 = baseStart + next;
            
            data.indices.push_back(centerIdx);
            data.indices.push_back(i1);
            data.indices.push_back(i0);
        }
        
        data.bounds = AABB(Vec3(-radius, 0, -radius), Vec3(radius, height, radius));
        return data;
    };

    auto generateSphereMesh = [](float radius, uint32_t segments, uint32_t rings) {
        MeshData data;
        const float pi = 3.14159265359f;
        const float twoPi = pi * 2.0f;
        data.vertices.reserve((segments + 1) * (rings + 1));
        data.indices.reserve(segments * rings * 6);

        for (uint32_t y = 0; y <= rings; ++y) {
            float v = static_cast<float>(y) / static_cast<float>(rings);
            float theta = v * pi;
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            for (uint32_t x = 0; x <= segments; ++x) {
                float u = static_cast<float>(x) / static_cast<float>(segments);
                float phi = u * twoPi;
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                float nx = cosPhi * sinTheta;
                float ny = cosTheta;
                float nz = sinPhi * sinTheta;

                Vertex vertex{};
                vertex.pos[0] = nx * radius;
                vertex.pos[1] = ny * radius;
                vertex.pos[2] = nz * radius;
                vertex.normal[0] = nx;
                vertex.normal[1] = ny;
                vertex.normal[2] = nz;
                vertex.color[0] = 1.0f;
                vertex.color[1] = 1.0f;
                vertex.color[2] = 1.0f;
                vertex.texCoord[0] = u;
                vertex.texCoord[1] = 1.0f - v;
                data.vertices.push_back(vertex);
            }
        }

        uint32_t stride = segments + 1;
        for (uint32_t y = 0; y < rings; ++y) {
            for (uint32_t x = 0; x < segments; ++x) {
                uint32_t i0 = y * stride + x;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + stride;
                uint32_t i3 = i2 + 1;

                data.indices.push_back(i0);
                data.indices.push_back(i2);
                data.indices.push_back(i1);

                data.indices.push_back(i1);
                data.indices.push_back(i2);
                data.indices.push_back(i3);
            }
        }

        data.bounds = AABB(Vec3(-radius, -radius, -radius), Vec3(radius, radius, radius));
        return data;
    };

    MeshData sphereMeshData = generateSphereMesh(0.5f, 24, 16);

    size_t cubeMeshIndex = MeshRendererComponent::InvalidMesh;
    try {
        cubeMeshIndex = uploadMesh("Default Cube", cubeMeshData, std::filesystem::path("builtin://cube"), false);
    } catch (const std::exception& e) {
        std::cerr << "Failed to upload default cube mesh: " << e.what() << std::endl;
        return -1;
    }
    const MeshGPU& defaultCubeMesh = meshLibrary[cubeMeshIndex];

    try {
        uploadMesh("Default Sphere", sphereMeshData, std::filesystem::path("builtin://sphere"), false);
    } catch (const std::exception& e) {
        std::cerr << "Failed to upload default sphere mesh: " << e.what() << std::endl;
        return -1;
    }

    // ── Camera + UBO ───────────────────────────
    Camera camera;
    camera.position = {0.0f, 0.0f, 3.0f}; // Position camera to view the cube
    // Start with cursor enabled (Unreal-style - hold RMB to look/move)
    g_cameraPtr = &camera; // Set global pointer for scroll callback

    RuntimeCameraController cameraController;
    cameraController.camera = &camera;
    cameraController.syncFromCamera();

    // ── Scene & Entities (ECS) ───────────────────
    Scene scene("Main Scene");
    
    // Create environment entity for scene settings
    Entity environmentEntity = scene.CreateEntity("Environment");
    environmentEntity.AddComponent<EnvironmentComponent>();
    
    ScriptEngine scriptEngine;
    scriptEngine.Initialize(&scene);
    
    // Initialize Lua script engine
    LuaScriptEngine luaScriptEngine;
    g_LuaScriptEnginePtr = &luaScriptEngine;  // Store pointer for access in callbacks
    luaScriptEngine.Initialize(&scene, window);
    luaScriptEngine.SetProjectRoot(ResolveActiveProjectRoot());

    CameraBinding cameraBinding{};
    cameraBinding.setActive = [&cameraController](bool active) {
        cameraController.setActive(active);
    };
    cameraBinding.isActive = [&cameraController]() {
        return cameraController.isActive();
    };
    cameraBinding.setPosition = [&cameraController](const Vec3& pos) {
        cameraController.setPosition(pos);
    };
    cameraBinding.getPosition = [&cameraController]() {
        return cameraController.getPosition();
    };
    cameraBinding.setRotation = [&cameraController](const Vec3& rot) {
        cameraController.setRotation(rot);
    };
    cameraBinding.getRotation = [&cameraController]() {
        return cameraController.getRotation();
    };
    cameraBinding.translate = [&cameraController](const Vec3& delta) {
        cameraController.translate(delta);
    };
    luaScriptEngine.SetCameraBinding(std::move(cameraBinding));

    // Register example scripts
    ScriptRegistry::Register<RotatingCube>("RotatingCube", 
        [](Entity entity, Scene* scene) {
            return std::make_shared<RotatingCube>(entity, scene);
        }
    );
    ScriptRegistry::Register<SimpleMovement>("SimpleMovement",
        [](Entity entity, Scene* scene) {
            return std::make_shared<SimpleMovement>(entity, scene);
        }
    );
    // Register user scripts
    ScriptRegistry::Register<PlayerController>("PlayerController",
        [](Entity entity, Scene* scene) {
            return std::make_shared<PlayerController>(entity, scene);
        }
    );
    ScriptRegistry::Register<AndyScript>("AndyScript",
        [](Entity entity, Scene* scene) {
            return std::make_shared<AndyScript>(entity, scene);
        }
    );
    
    // Auto-register user scripts from project
    // NOTE: For now, only hardcoded user scripts from ExampleScripts.h are available
    // To add your own scripts:
    // 1. Create your script class in engine/Game/ExampleScripts.h (or similar)
    // 2. Register it here using ScriptRegistry::Register<YourScript>("YourScript", ...)
    // Example:
    // ScriptRegistry::Register<AndyScript>("AndyScript", 
    //     [](Entity entity, Scene* scene) {
    //         return std::make_shared<AndyScript>(entity, scene);
    //     }
    // );
    
    Physics::MeshCollisionSystem collisionSystem;
    
    // Initialize Jolt Physics Engine
    Physics::JoltPhysicsEngine joltPhysicsEngine;
    bool physicsInitialized = false;
    try {
        joltPhysicsEngine.Initialize();
        physicsInitialized = true;
        std::cout << "Physics engine initialized successfully\n";
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize physics engine: " << e.what() << "\n";
        // Continue anyway - physics is optional
    }
    
    // Create Jolt Physics System
    Physics::JoltPhysicsSystem joltPhysicsSystem(joltPhysicsEngine);
    
    // Create cube entities with different materials
    Entity cube1 = scene.CreateEntity("Cube 1 (Metal)");
    auto& cube1Renderer = cube1.AddComponent<MeshRendererComponent>();
    cube1Renderer.SetMesh(cubeMeshIndex, defaultCubeMesh.bounds, defaultCubeMesh.sourceKey, defaultCubeMesh.imported);
    cube1.AddComponent<SelectionComponent>(true); // Start selected
    auto& cube1Mat = cube1.AddComponent<MaterialComponent>(Vec3(0.8f, 0.2f, 0.2f)); // Red
    cube1Mat.metallic = 0.9f;
    cube1Mat.roughness = 0.2f;
    auto& cube1Transform = cube1.GetComponent<TransformComponent>();
    cube1Transform.position = Vec3(0.0f, 0.0f, 0.0f);
    
    Entity cube2 = scene.CreateEntity("Cube 2 (Plastic)");
    auto& cube2Renderer = cube2.AddComponent<MeshRendererComponent>();
    cube2Renderer.SetMesh(cubeMeshIndex, defaultCubeMesh.bounds, defaultCubeMesh.sourceKey, defaultCubeMesh.imported);
    auto& cube2Mat = cube2.AddComponent<MaterialComponent>(Vec3(0.2f, 0.8f, 0.2f)); // Green
    cube2Mat.metallic = 0.0f;
    cube2Mat.roughness = 0.5f;
    auto& cube2Transform = cube2.GetComponent<TransformComponent>();
    cube2Transform.position = Vec3(2.0f, 0.0f, 0.0f);
    cube2Transform.scale = Vec3(0.5f, 0.5f, 0.5f);
    
    Entity cube3 = scene.CreateEntity("Cube 3 (Rough)");
    auto& cube3Renderer = cube3.AddComponent<MeshRendererComponent>();
    cube3Renderer.SetMesh(cubeMeshIndex, defaultCubeMesh.bounds, defaultCubeMesh.sourceKey, defaultCubeMesh.imported);
    auto& cube3Mat = cube3.AddComponent<MaterialComponent>(Vec3(0.2f, 0.4f, 0.8f)); // Blue
    cube3Mat.metallic = 0.0f;
    cube3Mat.roughness = 0.9f;
    auto& cube3Transform = cube3.GetComponent<TransformComponent>();
    cube3Transform.position = Vec3(-2.0f, 0.0f, 0.0f);
    cube3Transform.rotation = Vec3(0.0f, 0.785f, 0.0f); // 45 degrees
    
    // Create a fourth cube with emissive material
    Entity cube4 = scene.CreateEntity("Cube 4 (Emissive)");
    auto& cube4Renderer = cube4.AddComponent<MeshRendererComponent>();
    cube4Renderer.SetMesh(cubeMeshIndex, defaultCubeMesh.bounds, defaultCubeMesh.sourceKey, defaultCubeMesh.imported);
    auto& cube4Mat = cube4.AddComponent<MaterialComponent>(Vec3(0.8f, 0.8f, 0.2f)); // Yellow
    cube4Mat.metallic = 0.0f;
    cube4Mat.roughness = 0.3f;
    cube4Mat.emissive = Vec3(1.0f, 0.8f, 0.2f);
    cube4Mat.emissiveStrength = 2.0f;
    auto& cube4Transform = cube4.GetComponent<TransformComponent>();
    cube4Transform.position = Vec3(0.0f, 2.0f, 0.0f);
    cube4Transform.scale = Vec3(0.3f, 0.3f, 0.3f);
    
    // Create lights
    Entity dirLight = scene.CreateEntity("Directional Light");
    dirLight.AddComponent<LightComponent>(LightComponent::CreateDirectional(
        Vec3(0.0f, -1.0f, 0.0f), Vec3(1.0f, 0.95f, 0.9f), 0.8f));
    auto& dirLightTransform = dirLight.GetComponent<TransformComponent>();
    dirLightTransform.position = Vec3(0.0f, 5.0f, 0.0f); // Position above the scene for visibility
    dirLightTransform.rotation = Vec3(0.524f, 0.262f, 0.0f); // ~30 deg X, ~15 deg Y for nice angled lighting
    
    Entity pointLight1 = scene.CreateEntity("Point Light 1");
    auto& pl1Transform = pointLight1.GetComponent<TransformComponent>();
    pl1Transform.position = Vec3(3.0f, 2.0f, 2.0f);
    pointLight1.AddComponent<LightComponent>(LightComponent::CreatePoint(
        Vec3(1.0f, 0.3f, 0.3f), 5.0f, 10.0f)); // Red light
    
    Entity pointLight2 = scene.CreateEntity("Point Light 2");
    auto& pl2Transform = pointLight2.GetComponent<TransformComponent>();
    pl2Transform.position = Vec3(-3.0f, 2.0f, 2.0f);
    pointLight2.AddComponent<LightComponent>(LightComponent::CreatePoint(
        Vec3(0.3f, 0.3f, 1.0f), 5.0f, 10.0f)); // Blue light

    // Physics demo: static ground + falling rigid body
    Entity ground = scene.CreateEntity("Physics Ground");
    auto& groundTransform = ground.GetComponent<TransformComponent>();
    groundTransform.position = Vec3(0.0f, -1.0f, 0.0f);
    groundTransform.scale = Vec3(10.0f, 0.5f, 10.0f);
    auto& groundRenderer = ground.AddComponent<MeshRendererComponent>();
    groundRenderer.SetMesh(cubeMeshIndex, defaultCubeMesh.bounds, defaultCubeMesh.sourceKey, defaultCubeMesh.imported);
    auto& groundMaterial = ground.AddComponent<MaterialComponent>(Vec3(0.4f, 0.4f, 0.4f));
    groundMaterial.roughness = 0.9f;
    auto& groundShape = ground.AddComponent<PhysicsShapeComponent>();
    groundShape = PhysicsShapeComponent::CreateBox(Vec3(5.0f, 0.25f, 5.0f));
    auto& groundBody = ground.AddComponent<RigidBodyComponent>();
    groundBody = RigidBodyComponent::CreateStatic();
    groundBody.friction = 0.9f;

    Entity fallingCube = scene.CreateEntity("Physics Cube");
    auto& fallingTransform = fallingCube.GetComponent<TransformComponent>();
    fallingTransform.position = Vec3(0.0f, 4.0f, 0.0f);
    auto& fallingRenderer = fallingCube.AddComponent<MeshRendererComponent>();
    fallingRenderer.SetMesh(cubeMeshIndex, defaultCubeMesh.bounds, defaultCubeMesh.sourceKey, defaultCubeMesh.imported);
    auto& fallingMaterial = fallingCube.AddComponent<MaterialComponent>(Vec3(0.8f, 0.6f, 0.3f));
    fallingMaterial.roughness = 0.4f;
    auto& fallingShape = fallingCube.AddComponent<PhysicsShapeComponent>();
    fallingShape = PhysicsShapeComponent::CreateBox(Vec3(0.5f, 0.5f, 0.5f));
    auto& fallingBody = fallingCube.AddComponent<RigidBodyComponent>();
    fallingBody = RigidBodyComponent::CreateDynamic(1.0f);
    fallingBody.friction = 0.6f;
    fallingBody.restitution = 0.2f;

    static std::string assetBrowserStatus;

    auto makeUniqueEntityName = [&](const std::string& baseName) {
        if (!scene.FindEntityByName(baseName)) {
            return baseName;
        }
        int suffix = 1;
        std::string candidate;
        do {
            candidate = baseName + " (" + std::to_string(suffix++) + ")";
        } while (scene.FindEntityByName(candidate));
        return candidate;
    };

    auto meshHandleFromKey = [&](const std::string& key) -> std::optional<size_t> {
        auto it = meshLibraryLookup.find(key);
        if (it == meshLibraryLookup.end()) {
            return std::nullopt;
        }
        size_t index = it->second;
        if (index >= meshLibrary.size()) {
            return std::nullopt;
        }
        return index;
    };

    auto spawnPrimitiveFromMesh = [&](const std::string& baseName, const std::string& meshKey) -> Entity {
        std::optional<size_t> handle = meshHandleFromKey(meshKey);
        if (!handle) {
            return {};
        }
        const MeshGPU& mesh = meshLibrary[*handle];
        Entity entity = scene.CreateEntity(makeUniqueEntityName(baseName));
        auto& renderer = entity.AddComponent<MeshRendererComponent>();
        renderer.SetMesh(*handle, mesh.bounds, mesh.sourceKey, mesh.imported);
        entity.AddComponent<MaterialComponent>(Vec3(1.0f, 1.0f, 1.0f));
        if (!g_DetailsLocked) {
            scene.SetSelectedEntity(entity);
        }
        return entity;
    };

    auto spawnDirectionalLightEntity = [&]() -> Entity {
        Entity light = scene.CreateEntity(makeUniqueEntityName("Directional Light"));
        light.AddComponent<LightComponent>(LightComponent::CreateDirectional(Vec3(0.0f, -1.0f, 0.0f), Vec3(1.0f, 0.95f, 0.9f), 1.0f));
        auto& transform = light.GetComponent<TransformComponent>();
        transform.position = Vec3(0.0f, 3.0f, 0.0f);
        // Set rotation to create a nice default direction (angled down and slightly forward)
        // Rotation of about 30 degrees on X axis and 15 degrees on Y gives a nice angled sunlight
        transform.rotation = Vec3(0.524f, 0.262f, 0.0f); // ~30 deg X, ~15 deg Y in radians
        if (!g_DetailsLocked) {
            scene.SetSelectedEntity(light);
        }
        return light;
    };

    auto spawnPointLightEntity = [&]() -> Entity {
        Entity light = scene.CreateEntity(makeUniqueEntityName("Point Light"));
        light.AddComponent<LightComponent>(LightComponent::CreatePoint(Vec3(1.0f, 1.0f, 1.0f), 5.0f, 10.0f));
        auto& transform = light.GetComponent<TransformComponent>();
        transform.position = Vec3(0.0f, 2.0f, 0.0f);
        if (!g_DetailsLocked) {
            scene.SetSelectedEntity(light);
        }
        return light;
    };

    auto spawnSpotLightEntity = [&]() -> Entity {
        Entity light = scene.CreateEntity(makeUniqueEntityName("Spot Light"));
        light.AddComponent<LightComponent>(LightComponent::CreateSpot(Vec3(1.0f, 1.0f, 1.0f), 5.0f, 10.0f, 0.785f, 0.959f));
        auto& transform = light.GetComponent<TransformComponent>();
        transform.position = Vec3(0.0f, 3.0f, 0.0f);
        // Angle the spotlight downward
        transform.rotation = Vec3(0.524f, 0.0f, 0.0f); // ~30 deg X in radians
        if (!g_DetailsLocked) {
            scene.SetSelectedEntity(light);
        }
        return light;
    };

    auto importModelFromFile = [&](const std::filesystem::path& modelPath) -> Entity {
        std::cout << "[Import] Attempting to load model from " << modelPath.string() << std::endl;
        MeshData importedMesh;
        std::string error;
        if (!ModelImporter::LoadModel(modelPath, importedMesh, error)) {
            assetBrowserStatus = "Import failed: " + error;
            std::cerr << "[Import] LoadModel failed: " << error << std::endl;
            return {};
        }

        std::cout << "[Import] Mesh data loaded (verts=" << importedMesh.vertices.size()
                  << ", indices=" << importedMesh.indices.size() << ")" << std::endl;

        std::string baseName = modelPath.stem().string();
        if (baseName.empty()) {
            baseName = "Imported Model";
        }
        std::string entityName = makeUniqueEntityName(baseName);

        size_t meshIndex = MeshRendererComponent::InvalidMesh;
        try {
            std::cout << "[Import] Uploading mesh to GPU as '" << entityName << "'" << std::endl;
            meshIndex = uploadMesh(entityName, importedMesh, modelPath, true);
            std::cout << "[Import] Upload complete. Handle=" << meshIndex << std::endl;
        } catch (const std::exception& e) {
            assetBrowserStatus = std::string("Failed to upload mesh: ") + e.what();
            std::cerr << "[Import] Upload failed: " << e.what() << std::endl;
            return {};
        }

    Entity entity = scene.CreateEntity(entityName);
    auto& renderer = entity.AddComponent<MeshRendererComponent>();
    const MeshGPU& importedMeshGPU = meshLibrary[meshIndex];
        renderer.SetMesh(meshIndex, importedMeshGPU.bounds, importedMeshGPU.sourceKey, importedMeshGPU.imported);
        entity.AddComponent<MaterialComponent>(Vec3(1.0f, 1.0f, 1.0f));
        if (!g_DetailsLocked) {
            scene.SetSelectedEntity(entity);
        }

        size_t triangleCount = importedMesh.indices.size() / 3;
        assetBrowserStatus = "Imported " + entityName + " (" + std::to_string(importedMesh.vertices.size()) +
            " verts, " + std::to_string(triangleCount) + " tris) from " + modelPath.filename().string();
        std::cout << assetBrowserStatus << std::endl;
        return entity;
    };

    auto rebuildSceneMeshesFromComponents = [&](Scene& targetScene) {
        namespace fs = std::filesystem;

        std::vector<MeshGPU> retained;
        retained.reserve(meshLibrary.size());
        for (auto& mesh : meshLibrary) {
            if (!mesh.imported) {
                retained.push_back(std::move(mesh));
            } else {
                mesh.Destroy(vkdev.allocator);
            }
        }
        meshLibrary = std::move(retained);
        meshLibraryLookup.clear();
        for (size_t i = 0; i < meshLibrary.size(); ++i) {
            if (!meshLibrary[i].sourceKey.empty()) {
                meshLibraryLookup[meshLibrary[i].sourceKey] = i;
            }
        }

        auto renderView = targetScene.GetEntitiesWith<MeshRendererComponent>();
        for (auto entityHandle : renderView) {
            Entity entity(entityHandle, &targetScene);
            auto& renderer = entity.GetComponent<MeshRendererComponent>();

            if (renderer.meshAssetPath.empty()) {
                if (renderer.HasValidMesh() && renderer.meshHandle < meshLibrary.size()) {
                    const MeshGPU& gpu = meshLibrary[renderer.meshHandle];
                    renderer.meshAssetPath = gpu.sourceKey;
                    renderer.meshImported = gpu.imported;
                    renderer.localBounds = gpu.bounds;
                } else {
                    renderer.meshHandle = MeshRendererComponent::InvalidMesh;
                    continue;
                }
            }

            auto lookupIt = meshLibraryLookup.find(renderer.meshAssetPath);
            if (lookupIt != meshLibraryLookup.end()) {
                size_t handle = lookupIt->second;
                const MeshGPU& gpu = meshLibrary[handle];
                renderer.SetMesh(handle, gpu.bounds, gpu.sourceKey, gpu.imported);
                continue;
            }

            if (renderer.meshAssetPath.rfind("builtin://", 0) == 0) {
                auto builtinIt = meshLibraryLookup.find(renderer.meshAssetPath);
                if (builtinIt != meshLibraryLookup.end()) {
                    size_t handle = builtinIt->second;
                    const MeshGPU& gpu = meshLibrary[handle];
                    renderer.SetMesh(handle, gpu.bounds, gpu.sourceKey, gpu.imported);
                } else {
                    std::string entityName = entity.HasComponent<TagComponent>()
                        ? entity.GetComponent<TagComponent>().tag
                        : std::string("Entity");
                    std::cerr << "Unknown builtin mesh '" << renderer.meshAssetPath
                              << "' referenced by entity '" << entityName << "'." << std::endl;
                    renderer.meshHandle = MeshRendererComponent::InvalidMesh;
                }
                continue;
            }

            fs::path absolute = resolveMeshSourceKeyToPath(renderer.meshAssetPath);
            if (absolute.empty() || !fs::exists(absolute)) {
                std::string entityName = entity.HasComponent<TagComponent>()
                    ? entity.GetComponent<TagComponent>().tag
                    : std::string("Entity");
                std::cerr << "Scene references missing mesh asset '" << renderer.meshAssetPath
                          << "' for entity '" << entityName << "'." << std::endl;
                renderer.meshHandle = MeshRendererComponent::InvalidMesh;
                continue;
            }

            MeshData reimported;
            std::string error;
            if (!ModelImporter::LoadModel(absolute, reimported, error)) {
                std::string entityName = entity.HasComponent<TagComponent>()
                    ? entity.GetComponent<TagComponent>().tag
                    : absolute.stem().string();
                std::cerr << "Failed to reload mesh from '" << absolute << "' for entity '"
                          << entityName << "': " << error << std::endl;
                renderer.meshHandle = MeshRendererComponent::InvalidMesh;
                continue;
            }

            std::string meshName = entity.HasComponent<TagComponent>()
                ? entity.GetComponent<TagComponent>().tag
                : absolute.stem().string();

            try {
                size_t handle = uploadMesh(meshName, reimported, absolute, true);
                const MeshGPU& gpu = meshLibrary[handle];
                renderer.SetMesh(handle, gpu.bounds, gpu.sourceKey, gpu.imported);
            } catch (const std::exception& e) {
                std::cerr << "Failed to upload mesh '" << absolute << "' during scene reload: "
                          << e.what() << std::endl;
                renderer.meshHandle = MeshRendererComponent::InvalidMesh;
            }
        }
    };

    auto hydrateScriptsFromComponents = [&](Scene& targetScene) {
        scriptEngine.DestroyScripts();

        auto view = targetScene.GetRegistry().view<ScriptComponent>();
        for (auto entityHandle : view) {
            Entity entity(entityHandle, &targetScene);
            if (!entity) {
                continue;
            }

            auto& scriptComp = entity.GetComponent<ScriptComponent>();
            if (scriptComp.scriptNames.empty()) {
                scriptComp.RemoveAllScripts();
                continue;
            }

            std::vector<std::string> serializedNames = scriptComp.scriptNames;
            scriptComp.RemoveAllScripts();

            for (const std::string& scriptName : serializedNames) {
                if (scriptName.empty()) {
                    continue;
                }

                if (!ScriptRegistry::IsRegistered(scriptName)) {
                    std::cerr << "[ScriptHydration] Script '" << scriptName
                              << "' is not registered. Skipping." << std::endl;
                    continue;
                }

                ScriptPtr recreated = ScriptRegistry::CreateScript(scriptName, entity, &targetScene);
                if (!recreated) {
                    std::cerr << "[ScriptHydration] Failed to instantiate script '"
                              << scriptName << "'." << std::endl;
                    continue;
                }

                scriptEngine.AttachScript(entity, recreated, scriptName);
            }
        }
    };

    auto performSceneUndo = [&](Scene& targetScene) -> bool {
        auto snapshot = PopSceneUndoSnapshot();
        if (!snapshot) {
            assetBrowserStatus = "Nothing to undo.";
            return false;
        }
        if (SceneSerializer::DeserializeFromString(targetScene, *snapshot)) {
            rebuildSceneMeshesFromComponents(targetScene);
            targetScene.ClearSelection();
            assetBrowserStatus = "Undo: Restored previous scene state.";
            std::cout << assetBrowserStatus << std::endl;
            return true;
        }
        assetBrowserStatus = "Undo failed: unable to restore scene.";
        std::cerr << assetBrowserStatus << std::endl;
        return false;
    };

    // ── Billboard System for Light Visualization (Resources) ─────────────────────────
    VulkanPipeline billboardPipeline;
    VkDescriptorSetLayout billboardDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool billboardDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> billboardDescriptorSets_Point;
    std::vector<VkDescriptorSet> billboardDescriptorSets_Spot;
    std::vector<VkDescriptorSet> billboardDescriptorSets_Directional;
    
    // Load light textures for different light types
    VulkanTexture pointLightTexture;
    VulkanTexture spotLightTexture;
    VulkanTexture directionalLightTexture;
    bool textureLoaded = false;
    
    try {
        // Load point light texture (use existing light_bulb.png)
        std::filesystem::path pointLightPath = ResolveEnginePath(std::filesystem::path("assets") / "textures" / "light_bulb.png");
        if (!pointLightTexture.LoadFromFile(vkdev.allocator, vkdev.device, vkdev.physicalDevice, 
                          vkdev.commandPool, vkdev.graphicsQueue, pointLightPath.string())) {
            std::cerr << "Failed to load point light texture file" << std::endl;
        } else if (pointLightTexture.imageView == VK_NULL_HANDLE || pointLightTexture.sampler == VK_NULL_HANDLE) {
            std::cerr << "Point light texture loaded but imageView or sampler is null" << std::endl;
        } else {
            std::cout << "Loaded point light texture" << std::endl;
        }
        
        // Load spot light texture (try to load, fall back to point light if not found)
        std::filesystem::path spotLightPath = ResolveEnginePath(std::filesystem::path("assets") / "textures" / "spotlight.png");
        if (!spotLightTexture.LoadFromFile(vkdev.allocator, vkdev.device, vkdev.physicalDevice, 
                          vkdev.commandPool, vkdev.graphicsQueue, spotLightPath.string())) {
            std::cout << "spotlight.png not found, using point light texture for spotlights" << std::endl;
            // Use point light texture as fallback
            spotLightTexture = pointLightTexture;
        } else if (spotLightTexture.imageView == VK_NULL_HANDLE || spotLightTexture.sampler == VK_NULL_HANDLE) {
            std::cerr << "Spot light texture loaded but imageView or sampler is null, using fallback" << std::endl;
            spotLightTexture = pointLightTexture;
        } else {
            std::cout << "Loaded spot light texture" << std::endl;
        }
        
        // Load directional light texture (try to load, fall back to point light if not found)
        std::filesystem::path directionalLightPath = ResolveEnginePath(std::filesystem::path("assets") / "textures" / "sun.png");
        if (!directionalLightTexture.LoadFromFile(vkdev.allocator, vkdev.device, vkdev.physicalDevice, 
                          vkdev.commandPool, vkdev.graphicsQueue, directionalLightPath.string())) {
            std::cout << "sun.png not found, using point light texture for directional lights" << std::endl;
            // Use point light texture as fallback
            directionalLightTexture = pointLightTexture;
        } else if (directionalLightTexture.imageView == VK_NULL_HANDLE || directionalLightTexture.sampler == VK_NULL_HANDLE) {
            std::cerr << "Directional light texture loaded but imageView or sampler is null, using fallback" << std::endl;
            directionalLightTexture = pointLightTexture;
        } else {
            std::cout << "Loaded directional light texture" << std::endl;
        }
        
        textureLoaded = true;
    } catch (const std::exception& e) {
        std::cerr << "Exception while loading light textures: " << e.what() << std::endl;
    }

    // ── ImGuizmo State ───────────────────────────
    ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;

    // Per-image uniform buffers and descriptors (tied to swapchain images)
    std::vector<VulkanBuffer> uniformBuffers;
    std::vector<VulkanBuffer> materialBuffers;
    std::vector<VulkanBuffer> lightingBuffers;
    std::vector<VkDescriptorSet> descriptorSets;
    VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
    VkDeviceSize uboSize = sizeof(UniformBufferObject);
    VkDeviceSize materialUboSize = sizeof(MaterialUBO);
    // For dynamic UBO, ensure alignment to minUniformBufferOffsetAlignment
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(vkdev.physicalDevice, &props);
    VkDeviceSize alignment = props.limits.minUniformBufferOffsetAlignment;
    VkDeviceSize materialStride = (alignment > 0)
        ? ((materialUboSize + alignment - 1) & ~(alignment - 1))
        : materialUboSize;
    uint32_t maxMaterialsPerFrame = 1024;
    VkDeviceSize lightingUboSize = sizeof(LightingUBO);

    auto destroyDescriptors = [&]() {
        for (auto& ub : uniformBuffers) ub.Destroy(vkdev.allocator);
        for (auto& mb : materialBuffers) mb.Destroy(vkdev.allocator);
        for (auto& lb : lightingBuffers) lb.Destroy(vkdev.allocator);
        uniformBuffers.clear();
        materialBuffers.clear();
        lightingBuffers.clear();
        descriptorSets.clear();
        if (descriptorPool) { vkDestroyDescriptorPool(vkdev.device, descriptorPool, nullptr); descriptorPool = VK_NULL_HANDLE; }
    };

    auto createDescriptors = [&]() {
        destroyDescriptors();

        size_t imageCount = swap.framebuffers.size();
        uniformBuffers.resize(imageCount);
        materialBuffers.resize(imageCount);
        lightingBuffers.resize(imageCount);
        
        for (size_t i = 0; i < imageCount; ++i) {
            uniformBuffers[i].Create(vkdev.allocator, uboSize, 
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
            materialBuffers[i].Create(vkdev.allocator, materialStride * maxMaterialsPerFrame, 
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
            lightingBuffers[i].Create(vkdev.allocator, lightingUboSize, 
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        }

        // Create descriptor pool: camera, material UBO, lighting UBO, and texture samplers
        std::vector<VkDescriptorPoolSize> poolSizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(imageCount * 2)}, // camera + lighting per set
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, static_cast<uint32_t>(imageCount * 1)}, // material per set
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(imageCount * kMaxMaterialTextureSlots)}
        };

        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = static_cast<uint32_t>(imageCount);
        if (vkCreateDescriptorPool(vkdev.device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create descriptor pool");

        descriptorSets.resize(imageCount);
        std::vector<VkDescriptorSetLayout> layouts(imageCount, pipeline.descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(vkdev.device, &allocInfo, descriptorSets.data()) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate descriptor sets");

        // Ensure default texture slot exists
        if (g_MaterialTextureSlots.empty()) {
            g_MaterialTextureSlots.emplace_back();
            auto& defaultSlot = g_MaterialTextureSlots.back();
            if (!CreateSolidColorTexture(defaultSlot, vkdev, 0xFFFFFFFFu)) {
                throw std::runtime_error("Failed to create default material texture");
            }
            defaultSlot.key = "__default";
            g_MaterialTextureLookup.clear();
            g_MaterialTextureLookup.emplace(defaultSlot.key, 0);
            g_DefaultMaterialTextureSlot = 0;
        }

        // Update descriptor sets with buffers and texture array
        for (size_t i = 0; i < imageCount; ++i) {
            VkDescriptorBufferInfo bufInfos[3] = {};
            
            // Binding 0: Camera UBO
            bufInfos[0].buffer = uniformBuffers[i].buffer;
            bufInfos[0].offset = 0;
            bufInfos[0].range = uboSize;
            
            // Binding 1: Material UBO (dynamic)
            bufInfos[1].buffer = materialBuffers[i].buffer;
            bufInfos[1].offset = 0;
            bufInfos[1].range = materialUboSize;
            
            // Binding 2: Lighting UBO
            bufInfos[2].buffer = lightingBuffers[i].buffer;
            bufInfos[2].offset = 0;
            bufInfos[2].range = lightingUboSize;

            std::vector<VkDescriptorImageInfo> imageInfos(kMaxMaterialTextureSlots,
                g_MaterialTextureSlots[g_DefaultMaterialTextureSlot].descriptor);
            for (size_t slot = 0; slot < std::min(g_MaterialTextureSlots.size(), static_cast<size_t>(kMaxMaterialTextureSlots)); ++slot) {
                if (g_MaterialTextureSlots[slot].valid) {
                    imageInfos[slot] = g_MaterialTextureSlots[slot].descriptor;
                }
            }

            VkWriteDescriptorSet writes[4] = {};
            // Binding 0
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = descriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &bufInfos[0];
            // Binding 1 (dynamic)
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = descriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &bufInfos[1];
            // Binding 2
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = descriptorSets[i];
            writes[2].dstBinding = 2;
            writes[2].dstArrayElement = 0;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].descriptorCount = 1;
            writes[2].pBufferInfo = &bufInfos[2];

            // Binding 3: texture sampler array
            writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet = descriptorSets[i];
            writes[3].dstBinding = 3;
            writes[3].dstArrayElement = 0;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[3].descriptorCount = kMaxMaterialTextureSlots;
            writes[3].pImageInfo = imageInfos.data();
            
            vkUpdateDescriptorSets(vkdev.device, 4, writes, 0, nullptr);
        }
    };

    auto updateTextureSlotInDescriptors = [&](uint32_t slotIndex) {
        if (slotIndex >= kMaxMaterialTextureSlots) {
            return;
        }
        if (slotIndex >= g_MaterialTextureSlots.size()) {
            return;
        }
        auto& slot = g_MaterialTextureSlots[slotIndex];
        if (!slot.valid) {
            return;
        }
        for (size_t i = 0; i < descriptorSets.size(); ++i) {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSets[i];
            write.dstBinding = 3;
            write.dstArrayElement = slotIndex;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &slot.descriptor;
            vkUpdateDescriptorSets(vkdev.device, 1, &write, 0, nullptr);
        }
    };

    auto ensureMaterialTextureSlot = [&](const std::string& rawPath) -> uint32_t {
        if (rawPath.empty()) {
            return g_DefaultMaterialTextureSlot;
        }

        namespace fs = std::filesystem;
        fs::path normalizedInput = fs::path(rawPath).lexically_normal();
        std::string key = normalizedInput.generic_string();

        auto existingIt = g_MaterialTextureLookup.find(key);
        if (existingIt != g_MaterialTextureLookup.end()) {
            return existingIt->second;
        }

        if (g_MaterialTextureSlots.size() >= kMaxMaterialTextureSlots) {
            std::cerr << "Material texture cache is full (" << kMaxMaterialTextureSlots
                      << "). Using default texture." << std::endl;
            return g_DefaultMaterialTextureSlot;
        }

        std::vector<fs::path> candidatePaths;
        auto addCandidate = [&](const fs::path& candidate) {
            if (candidate.empty()) {
                return;
            }
            fs::path normalized = candidate.lexically_normal();
            if (normalized.empty()) {
                return;
            }
            if (std::find(candidatePaths.begin(), candidatePaths.end(), normalized) == candidatePaths.end()) {
                candidatePaths.push_back(std::move(normalized));
            }
        };

        addCandidate(normalizedInput);

        fs::path projectRoot = ResolveActiveProjectRoot();
        if (!projectRoot.empty()) {
            addCandidate(projectRoot / normalizedInput);

            fs::path assetsRoot = ResolveActiveAssetsRoot();
            if (!assetsRoot.empty()) {
                std::string normalizedKey = normalizedInput.generic_string();
                bool startsWithAssets = false;
                if (!normalizedKey.empty()) {
                    if (normalizedKey.rfind("assets/", 0) == 0 || normalizedKey.rfind("assets\\", 0) == 0) {
                        startsWithAssets = true;
                    }
                }
                if (!startsWithAssets) {
                    addCandidate(assetsRoot / normalizedInput);
                }
            }
        }

        addCandidate(ResolveEngineRoot() / normalizedInput);

        fs::path resolvedPath;
        for (const fs::path& candidate : candidatePaths) {
            std::error_code ec;
            if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
                resolvedPath = candidate;
                break;
            }
        }

        if (resolvedPath.empty()) {
            std::cerr << "Material texture not found. Tried:";
            for (const auto& candidate : candidatePaths) {
                std::cerr << "\n  - " << candidate;
            }
            std::cerr << "\n(from: " << rawPath << ")" << std::endl;
            return g_DefaultMaterialTextureSlot;
        }

        std::cout << "Loading material texture: " << resolvedPath << std::endl;

        MaterialTextureSlot newSlot;
        try {
            if (!newSlot.texture.LoadFromFile(
                    vkdev.allocator,
                    vkdev.device,
                    vkdev.physicalDevice,
                    vkdev.commandPool,
                    vkdev.graphicsQueue,
                    resolvedPath.string())) {
                std::cerr << "Failed to load material texture: " << resolvedPath << std::endl;
                return g_DefaultMaterialTextureSlot;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception while loading material texture '" << resolvedPath
                      << "': " << e.what() << std::endl;
            return g_DefaultMaterialTextureSlot;
        }

        newSlot.descriptor.sampler = newSlot.texture.sampler;
        newSlot.descriptor.imageView = newSlot.texture.imageView;
        newSlot.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        newSlot.key = key;
        newSlot.valid = true;

        uint32_t slotIndex = static_cast<uint32_t>(g_MaterialTextureSlots.size());
        g_MaterialTextureSlots.push_back(std::move(newSlot));
        g_MaterialTextureLookup.emplace(key, slotIndex);
        updateTextureSlotInDescriptors(slotIndex);

        return slotIndex;
    };
    
    // ── Billboard Lambda Definitions (after uniformBuffers) ───────────────
    auto createBillboardDescriptors = [&]() {
        // Clean up existing resources
        billboardDescriptorSets_Point.clear();
        billboardDescriptorSets_Spot.clear();
        billboardDescriptorSets_Directional.clear();
        
        if (billboardDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(vkdev.device, billboardDescriptorPool, nullptr);
            billboardDescriptorPool = VK_NULL_HANDLE;
        }
        
        if (billboardDescriptorLayout == VK_NULL_HANDLE) {
            // Binding 0: Camera UBO (same as main pipeline)
            VkDescriptorSetLayoutBinding uboBinding{};
            uboBinding.binding = 0;
            uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboBinding.descriptorCount = 1;
            uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            
            // Binding 1: Texture sampler
            VkDescriptorSetLayoutBinding samplerBinding{};
            samplerBinding.binding = 1;
            samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerBinding.descriptorCount = 1;
            samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            
            std::vector<VkDescriptorSetLayoutBinding> bindings = {uboBinding, samplerBinding};
            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            
            if (vkCreateDescriptorSetLayout(vkdev.device, &layoutInfo, nullptr, &billboardDescriptorLayout) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create billboard descriptor set layout");
            }
        }
        
        // Create descriptor pool (3x for 3 light types)
        size_t imageCount = swap.framebuffers.size();
        std::vector<VkDescriptorPoolSize> poolSizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(imageCount * 3)},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(imageCount * 3)}
        };
        
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = static_cast<uint32_t>(imageCount * 3);
        
        if (vkCreateDescriptorPool(vkdev.device, &poolInfo, nullptr, &billboardDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create billboard descriptor pool");
        }
        
        // Lambda to create descriptor sets for a given texture and light type
        auto createDescriptorSetsForLightType = [&](VulkanTexture& texture, std::vector<VkDescriptorSet>& outSets) {
            // Allocate descriptor sets
            std::vector<VkDescriptorSetLayout> layouts(imageCount, billboardDescriptorLayout);
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = billboardDescriptorPool;
            allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
            allocInfo.pSetLayouts = layouts.data();
            
            outSets.resize(imageCount);
            if (vkAllocateDescriptorSets(vkdev.device, &allocInfo, outSets.data()) != VK_SUCCESS) {
                throw std::runtime_error("Failed to allocate billboard descriptor sets");
            }
            
            // Update descriptor sets
            for (size_t i = 0; i < imageCount; i++) {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = uniformBuffers[i].buffer;
                bufferInfo.offset = 0;
                bufferInfo.range = sizeof(UniformBufferObject);
                
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = texture.imageView;
                imageInfo.sampler = texture.sampler;
                
                std::vector<VkWriteDescriptorSet> descriptorWrites(2);
                
                descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[0].dstSet = outSets[i];
                descriptorWrites[0].dstBinding = 0;
                descriptorWrites[0].dstArrayElement = 0;
                descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descriptorWrites[0].descriptorCount = 1;
                descriptorWrites[0].pBufferInfo = &bufferInfo;
                
                descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[1].dstSet = outSets[i];
                descriptorWrites[1].dstBinding = 1;
                descriptorWrites[1].dstArrayElement = 0;
                descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrites[1].descriptorCount = 1;
                descriptorWrites[1].pImageInfo = &imageInfo;
                
                vkUpdateDescriptorSets(vkdev.device, static_cast<uint32_t>(descriptorWrites.size()), 
                                      descriptorWrites.data(), 0, nullptr);
            }
        };
        
        // Create descriptor sets for each light type
        createDescriptorSetsForLightType(pointLightTexture, billboardDescriptorSets_Point);
        createDescriptorSetsForLightType(spotLightTexture, billboardDescriptorSets_Spot);
        createDescriptorSetsForLightType(directionalLightTexture, billboardDescriptorSets_Directional);
    };
    
    auto createBillboardPipeline = [&]() {
        billboardPipeline.Cleanup();
        billboardPipeline.CreateBillboardPipeline(vkdev.device, swap.renderPass, swap.extent, 
                                                   billboardDescriptorLayout);
    };
    
    createDescriptors();
    
    // Create billboard resources only if texture loaded successfully
    if (textureLoaded) {
        try {
            std::cout << "Creating billboard descriptors..." << std::endl;
            createBillboardDescriptors();
            std::cout << "Creating billboard pipeline..." << std::endl;
            createBillboardPipeline();
            std::cout << "Billboard system initialized" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Billboard initialization error: " << e.what() << std::endl;
            // Continue without billboards instead of crashing
        }
    } else {
        std::cerr << "Skipping billboard system - texture not loaded" << std::endl;
    }

    // timing
    float lastTime = static_cast<float>(glfwGetTime());

    // ── Sync Objects ─────────────────────────────
    const int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkCommandBuffer> cmdBufs;
    auto allocateCmdBuffers = [&]() {
        if (!cmdBufs.empty()) {
            vkFreeCommandBuffers(vkdev.device, vkdev.commandPool, (uint32_t)cmdBufs.size(), cmdBufs.data());
            cmdBufs.clear();
        }
        cmdBufs.resize(swap.framebuffers.size());
        VkCommandBufferAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        a.commandPool = vkdev.commandPool;
        a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        a.commandBufferCount = static_cast<uint32_t>(cmdBufs.size());
        vkAllocateCommandBuffers(vkdev.device, &a, cmdBufs.data());
    };
    allocateCmdBuffers();

    // Initialize ImGui (skip in runtime mode)
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    if (!g_RuntimeMode) {
        // ImGui descriptor pool setup
        VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(pool_sizes));
        pool_info.pPoolSizes = pool_sizes;
        if (vkCreateDescriptorPool(vkdev.device, &pool_info, nullptr, &imguiPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui descriptor pool");
        }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // Ensure ImGuizmo uses the same ImGui context
    ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
    ImNodes::CreateContext();
    ImNodes::SetImGuiContext(ImGui::GetCurrentContext());
    ImNodes::StyleColorsDark();
    ImNodes::GetStyle().GridSpacing = 28.0f;
    if (!g_MaterialEditorPanel) {
        g_MaterialEditorPanel = new MaterialEditor::Panel();
    }
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &io.KeyCtrl;

    // Load custom font
    const float baseFontSize = 16.0f;
    std::filesystem::path fontPath = ResolveEnginePath(std::filesystem::path("editor") / "fonts" / "JetBrainsMono-VariableFont_wght.ttf");
    if (std::filesystem::exists(fontPath)) {
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 1;
        fontConfig.PixelSnapH = true;
        io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), baseFontSize, &fontConfig);
    } else {
        std::cerr << "Warning: Font not found at " << fontPath << ", using default font.\n";
        io.Fonts->AddFontDefault();
    }

    // Setup Dear ImGui style
    g_UIScale = ComputeUIScale(ImGui::GetMainViewport());
    ApplyHephaestusEditorStyle(io, g_UIScale);
    io.FontGlobalScale = g_UIScale;

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = vkdev.physicalDevice;
    init_info.Device = vkdev.device;
    init_info.QueueFamily = vkdev.graphicsFamily;
    init_info.Queue = vkdev.graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = imguiPool;
    init_info.MinImageCount = 2;
    init_info.ImageCount = static_cast<uint32_t>(swap.images.size());
    init_info.PipelineInfoMain.RenderPass = swap.renderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;
    ImGui_ImplVulkan_Init(&init_info);
    
    EnsureAssetBrowserIconsLoaded(vkdev);
    }  // end if (!g_RuntimeMode)
    
    // Store ImGui IO for later manipulation during play mode (editor only)
    ImGuiIO* gImGuiIOPtr = g_RuntimeMode ? nullptr : &ImGui::GetIO();

    // Runtime mode: Load scene and start simulation automatically
    if (g_RuntimeMode) {
        if (!g_RuntimeScenePath.empty()) {
            std::cout << "[Runtime] Loading scene: " << g_RuntimeScenePath << std::endl;
            std::string loadError;
            if (SceneSerializer::Deserialize(scene, g_RuntimeScenePath, loadError)) {
                std::cout << "[Runtime] Scene loaded successfully" << std::endl;
                rebuildSceneMeshesFromComponents(scene);
                hydrateScriptsFromComponents(scene);
                
                // Start simulation automatically
                g_SimPlaying = true;
                g_SimPaused = false;
                luaScriptEngine.OnSimulationStarted();
                std::cout << "[Runtime] Simulation started" << std::endl;
            } else {
                std::cerr << "[Runtime] Failed to load scene: " << loadError << std::endl;
            }
        } else {
            std::cerr << "[Runtime] No scene specified to load!" << std::endl;
            std::cerr << "[Runtime] Usage: HephaestusEngine --runtime --scene <path_to_scene.hpscene>" << std::endl;
        }
    }

    std::vector<VkSemaphore> imageAvailable(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkSemaphore> renderFinished(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkFence> inFlight(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkFence> imagesInFlight(swap.images.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkCreateSemaphore(vkdev.device, &sci, nullptr, &imageAvailable[i]);
        vkCreateSemaphore(vkdev.device, &sci, nullptr, &renderFinished[i]);
        vkCreateFence(vkdev.device, &fci, nullptr, &inFlight[i]);
    }

    uint32_t currentFrame = 0;

    // ── Main Loop ────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ++g_FrameCounter;

        VkResult waitCurrent = vkWaitForFences(vkdev.device, 1, &inFlight[currentFrame], VK_TRUE, UINT64_MAX);
        if (waitCurrent == VK_ERROR_DEVICE_LOST) {
            std::cerr << "Device lost while waiting for in-flight fence (frame " << currentFrame << ")." << std::endl;
            break;
        }
        if (waitCurrent != VK_SUCCESS) {
            std::cerr << "vkWaitForFences failed for in-flight fence (frame " << currentFrame << ") with code " << waitCurrent << "." << std::endl;
            continue;
        }

        uint32_t imageIndex;
        VkResult acq = vkAcquireNextImageKHR(vkdev.device, swap.swapchain, UINT64_MAX,
                                            imageAvailable[currentFrame], VK_NULL_HANDLE, &imageIndex);
        // Timing
        float currentTime = static_cast<float>(glfwGetTime());
        float dt = currentTime - lastTime; lastTime = currentTime;

        // Update scripts (before rendering) - only when playing and not paused
        scriptEngine.AwakeScripts();
        if (g_SimPlaying && !g_SimPaused) {
            scriptEngine.UpdateScripts(dt);
            if (g_LuaScriptEnginePtr) {
                g_LuaScriptEnginePtr->Update(scene, dt);
            }
            // Apply camera controller changes after scripts have updated
            if (cameraController.isActive()) {
                cameraController.applyToCamera();
            }
        }

        auto makeCameraUBO = [&]() {
            return camera.makeUBO(swap.extent);
        };

        // Start the Dear ImGui frame (skip in runtime mode)
        if (!g_RuntimeMode) {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            
            // During play mode, clear ImGui input capture so raw GLFW input reaches scripts
            if (g_SimPlaying && !g_SimPaused) {
                ImGuiIO& io = ImGui::GetIO();
                io.WantCaptureKeyboard = false;
                io.WantCaptureMouse = false;
            }

            const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
            float newScale = ComputeUIScale(mainViewport);
            if (std::abs(newScale - g_UIScale) > 0.01f) {
                g_UIScale = newScale;
                ImGuiIO& io = ImGui::GetIO();
                io.FontGlobalScale = g_UIScale;
                ApplyHephaestusEditorStyle(io, g_UIScale);
            }
        }
        
        // Setup ImGuizmo early to check if it's being used (editor only)
        if (!g_RuntimeMode) {
            ImGuizmo::BeginFrame();
            ImGuizmo::SetOrthographic(false);
        }
        
        // Handle mouse click for object selection (editor only)
        static bool leftMouseWasPressed = false;
        bool leftMousePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        
        if (!g_RuntimeMode && g_ProjectSelected && !g_SimPlaying && leftMousePressed && !leftMouseWasPressed &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() && !ImGui::GetIO().WantCaptureMouse &&
            !gizmoDragging && vulkanGizmo.GetHoveredAxis() == -1) {
            // Left mouse button just pressed and not over ImGui
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            
            // Create picking ray
            UniformBufferObject ubo = makeCameraUBO();
            Ray pickRay = ScreenToWorldRay(mouseX, mouseY, swap.extent.width, swap.extent.height, ubo.view, ubo.proj);
            
            // Find closest hit
            float hitDist;
            Entity hitEntity = FindClosestHitEntity(pickRay, scene, hitDist);
            
            if (!g_DetailsLocked) {
                if (hitEntity) {
                    scene.SetSelectedEntity(hitEntity);
                } else {
                    scene.ClearSelection();
                }
            }
        }
        leftMouseWasPressed = leftMousePressed;
        
        // Editor camera input (disabled when ImGuizmo is active or when scripts control camera during play)
        if (g_ProjectSelected && !ImGuizmo::IsUsing() && !gizmoDragging) {
            bool scriptHasCamera = (g_SimPlaying && !g_SimPaused && cameraController.isActive());
            if (!scriptHasCamera) {
                camera.handleMouse(window);
                camera.handleKeyboard(window, dt);
            }
        }

        // ImGui UI (skip in runtime mode)
        std::optional<std::filesystem::path> pendingAssetImport;
        if (!g_RuntimeMode) {
        ImGuiIO& io = ImGui::GetIO();
        if (!g_ProjectSelected) {
            if (gizmoDragging) {
                gizmoDragging = false;
                gizmoActiveAxis = -1;
                vulkanGizmo.EndDrag();
            }
            vulkanGizmo.SetHoveredAxis(-1);

            std::optional<std::filesystem::path> projectChoice = g_ProjectManager.Draw();
            if (projectChoice) {
                SetActiveProjectRoot(*projectChoice);
                if (!g_ActiveProjectName.empty()) {
                    assetBrowserStatus = "Opened project '" + g_ActiveProjectName + "'.";
                } else {
                    assetBrowserStatus = "Opened project.";
                }
            }
        } else {
            BeginHephaestusDockspace();

            // Draw docked panels first
            WorldOutlinerActions outlinerActions = DrawWorldOutliner(scene);
            if (outlinerActions.newLevelName) {
                if (gizmoDragging) {
                    gizmoDragging = false;
                    gizmoActiveAxis = -1;
                    vulkanGizmo.EndDrag();
                }
                vulkanGizmo.SetHoveredAxis(-1);

                scene.Clear();
                scene.ClearSelection();
                scene.SetName(*outlinerActions.newLevelName);
                
                // Recreate environment entity for new scene
                environmentEntity = scene.CreateEntity("Environment");
                environmentEntity.AddComponent<EnvironmentComponent>();
                
                rebuildSceneMeshesFromComponents(scene);
                ClearSceneUndoStack();

                assetBrowserStatus = "Created new level '" + scene.GetName() + "'.";
                std::cout << assetBrowserStatus << std::endl;
            }
            if (outlinerActions.spawnRequest) {
                auto logFailure = [&](const std::string& message) {
                    assetBrowserStatus = message;
                    std::cerr << assetBrowserStatus << std::endl;
                };
                auto logSuccess = [&](const std::string& message) {
                    assetBrowserStatus = message;
                    std::cout << assetBrowserStatus << std::endl;
                };

                switch (*outlinerActions.spawnRequest) {
                    case WorldOutlinerSpawnPreset::DefaultCube: {
                        Entity created = spawnPrimitiveFromMesh("Cube", "builtin://cube");
                        if (created) {
                            logSuccess("Added default cube to scene.");
                        } else {
                            logFailure("Failed to add cube: built-in mesh missing.");
                        }
                        break;
                    }
                    case WorldOutlinerSpawnPreset::DefaultSphere: {
                        Entity created = spawnPrimitiveFromMesh("Sphere", "builtin://sphere");
                        if (created) {
                            logSuccess("Added default sphere to scene.");
                        } else {
                            logFailure("Failed to add sphere: built-in mesh missing.");
                        }
                        break;
                    }
                    case WorldOutlinerSpawnPreset::DirectionalLight: {
                        Entity created = spawnDirectionalLightEntity();
                        if (created) {
                            logSuccess("Added directional light to scene.");
                        } else {
                            logFailure("Failed to add directional light.");
                        }
                        break;
                    }
                    case WorldOutlinerSpawnPreset::PointLight: {
                        Entity created = spawnPointLightEntity();
                        if (created) {
                            logSuccess("Added point light to scene.");
                        } else {
                            logFailure("Failed to add point light.");
                        }
                        break;
                    }
                    case WorldOutlinerSpawnPreset::SpotLight: {
                        Entity created = spawnSpotLightEntity();
                        if (created) {
                            logSuccess("Added spot light to scene.");
                        } else {
                            logFailure("Failed to add spot light.");
                        }
                        break;
                    }
                }
            }
            DetailsPanelActions detailsActions = DrawDetailsPanel(scene, scriptEngine, skybox, vkdev);

            std::optional<entt::entity> viewportDeleteRequest;
            {
                const ImGuiIO& shortcutIO = ImGui::GetIO();
                const bool viewportOverlayActive = g_ViewportOverlayHovered || g_ViewportOverlayFocused;
                const bool viewportInputContext = !shortcutIO.WantTextInput &&
                                                  (!shortcutIO.WantCaptureKeyboard || viewportOverlayActive);

                if (viewportInputContext) {
                    const ImGuiInputFlags viewportRoute = ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused;
                    const bool deleteShortcut = ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_Backspace, viewportRoute) ||
                                                 ImGui::Shortcut(ImGuiKey_Delete, viewportRoute);
                    const bool deleteFallback = (shortcutIO.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) ||
                                                ImGui::IsKeyPressed(ImGuiKey_Delete, false);
                    if (!viewportDeleteRequest && (deleteShortcut || deleteFallback)) {
                        Entity selected = scene.GetSelectedEntity();
                        if (selected) {
                            viewportDeleteRequest = static_cast<entt::entity>(selected);
                        }
                    }

                    const bool duplicateShortcut = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D, viewportRoute);
                    const bool duplicateFallback = (shortcutIO.KeyCtrl && shortcutIO.KeyShift &&
                                                    ImGui::IsKeyPressed(ImGuiKey_D, false));
                    if (duplicateShortcut || duplicateFallback) {
                        Entity selected = scene.GetSelectedEntity();
                        if (selected) {
                            PushSceneUndoSnapshot(scene);
                            scene.CopyEntityToClipboard(selected);
                            Entity duplicated = scene.PasteClipboard();
                            if (duplicated) {
                                scene.SetSelectedEntity(duplicated);
                            }
                        }
                    }
                }
            }

            // Decay attachment success message
            if (g_ScriptAttachmentMessageTimer > 0.0f) {
                g_ScriptAttachmentMessageTimer -= dt;
                if (g_ScriptAttachmentMessageTimer <= 0.0f) {
                    g_ScriptAttachmentMessage.clear();
                    g_ScriptAttachmentMessageTimer = 0.0f;
                }
            }

            if (g_MaterialEditorPanel) {
                if (g_MaterialEditorPanel->HasActiveAsset() && g_MaterialGraph.GetNodes().empty()) {
                    ResetMaterialGraph();
                }
                std::optional<MaterialEditor::PanelResult> panelResult = g_MaterialEditorPanel->Draw(g_MaterialGraph);
                if (panelResult) {
                    Entity selected = scene.GetSelectedEntity();
                    if (selected) {
                        std::string entityName = selected.HasComponent<TagComponent>()
                            ? selected.GetComponent<TagComponent>().tag
                            : std::string("Entity");
                        if (!selected.HasComponent<MaterialComponent>()) {
                            selected.AddComponent<MaterialComponent>();
                        }
                        
                        std::cout << "Applying material to entity. Texture enabled: " 
                                  << panelResult->material.useAlbedoTexture 
                                  << ", Texture path: '" 
                                  << panelResult->material.albedoTexturePath << "'" << std::endl;
                        
                        selected.GetComponent<MaterialComponent>() = panelResult->material;
                        assetBrowserStatus = "Applied material '" + panelResult->name + "' to '" + entityName + "'.";
                        std::cout << assetBrowserStatus << std::endl;
                    } else {
                        assetBrowserStatus = "Material '" + panelResult->name + "' not applied: no entity selected.";
                        std::cerr << assetBrowserStatus << std::endl;
                    }
                }
            }

            std::vector<entt::entity> entityDeletionQueue;
            auto enqueueDeletion = [&](const std::optional<entt::entity>& candidate) {
                if (!candidate || *candidate == entt::null) {
                    return;
                }
                if (std::find(entityDeletionQueue.begin(), entityDeletionQueue.end(), *candidate) == entityDeletionQueue.end()) {
                    entityDeletionQueue.push_back(*candidate);
                }
            };

            enqueueDeletion(outlinerActions.entityToDelete);
            enqueueDeletion(detailsActions.entityToDelete);
            enqueueDeletion(viewportDeleteRequest);

            if (!entityDeletionQueue.empty()) {
                PushSceneUndoSnapshot(scene);
                if (gizmoDragging) {
                    gizmoDragging = false;
                    gizmoActiveAxis = -1;
                    vulkanGizmo.EndDrag();
                }
                vulkanGizmo.SetHoveredAxis(-1);

                for (entt::entity handle : entityDeletionQueue) {
                    if (!scene.GetRegistry().valid(handle)) {
                        continue;
                    }

                    Entity toDelete(handle, &scene);
                    std::string entityName = toDelete.HasComponent<TagComponent>()
                        ? toDelete.GetComponent<TagComponent>().tag
                        : std::string("Entity");

                    bool wasSelected = false;
                    if (Entity selected = scene.GetSelectedEntity()) {
                        wasSelected = static_cast<entt::entity>(selected) == handle;
                    }

                    if (g_LuaScriptEnginePtr) {
                        g_LuaScriptEnginePtr->OnEntityDestroyed(toDelete);
                    }
                    scene.DestroyEntity(toDelete);
                    if (wasSelected) {
                        scene.ClearSelection();
                    }

                    assetBrowserStatus = "Deleted '" + entityName + "' from scene.";
                    std::cout << assetBrowserStatus << std::endl;
                }
            }

            if (detailsActions.meshToClear) {
                entt::entity handle = *detailsActions.meshToClear;
                bool pendingDeletion = std::find(entityDeletionQueue.begin(), entityDeletionQueue.end(), handle) != entityDeletionQueue.end();
                if (!pendingDeletion && scene.GetRegistry().valid(handle)) {
                    Entity target(handle, &scene);
                    if (target.HasComponent<MeshRendererComponent>()) {
                        std::string entityName = target.HasComponent<TagComponent>()
                            ? target.GetComponent<TagComponent>().tag
                            : std::string("Entity");

                        target.RemoveComponent<MeshRendererComponent>();
                        if (target.HasComponent<MaterialComponent>()) {
                            target.RemoveComponent<MaterialComponent>();
                        }
                        if (target.HasComponent<MeshColliderComponent>()) {
                            target.RemoveComponent<MeshColliderComponent>();
                        }

                        assetBrowserStatus = "Removed mesh component from '" + entityName + "'.";
                        std::cout << assetBrowserStatus << std::endl;
                    }
                }
            }

            AssetBrowserResult browserResult = DrawAssetBrowserPanel(assetBrowserStatus);
            if (browserResult.modelImportRequest) {
                pendingAssetImport = browserResult.modelImportRequest;
            }
            if (browserResult.sceneOpenRequest) {
                namespace fs = std::filesystem;
                std::string loadError;
                if (SceneSerializer::Deserialize(scene, *browserResult.sceneOpenRequest, loadError)) {
                    if (gizmoDragging) {
                        gizmoDragging = false;
                        gizmoActiveAxis = -1;
                        vulkanGizmo.EndDrag();
                    }
                    rebuildSceneMeshesFromComponents(scene);
                    hydrateScriptsFromComponents(scene);
                    ClearSceneUndoStack();
                    fs::path projectRoot = ResolveActiveProjectRoot();
                    fs::path relativePath = projectRoot.empty()
                        ? browserResult.sceneOpenRequest->filename()
                        : browserResult.sceneOpenRequest->lexically_relative(projectRoot);
                    std::string friendly = relativePath.empty()
                        ? browserResult.sceneOpenRequest->filename().generic_string()
                        : relativePath.generic_string();
                    assetBrowserStatus = "Scene loaded from " + friendly + ".";
                    std::cout << assetBrowserStatus << std::endl;
                } else {
                    assetBrowserStatus = "Scene load failed: " + loadError;
                    std::cerr << assetBrowserStatus << std::endl;
                }
            }
            if (browserResult.materialCreateRequest) {
                ActivateMaterialAsset(*browserResult.materialCreateRequest, assetBrowserStatus);
            }
            if (browserResult.materialOpenRequest) {
                ActivateMaterialAsset(*browserResult.materialOpenRequest, assetBrowserStatus);
            }
            DrawLogConsole(io, assetBrowserStatus);

            // Overlays should be rendered last so they appear above docked windows
            ToolbarActions toolbarActions = DrawEditorToolbar(g_SimPlaying, g_SimPaused, environmentEntity, skybox, vkdev);
            if (toolbarActions.playClicked) {
                // Save scene state before entering play mode
                SceneSerializer serializer(scene);
                g_SceneSnapshotBeforePlay = serializer.SerializeToString();
                
                g_SimPlaying = true;
                g_SimPaused = false;
                g_ShowGizmoBeforePlay = g_ShowEditorGizmo;
                g_ShowEditorGizmo = false;
                if (g_LuaScriptEnginePtr) {
                    g_LuaScriptEnginePtr->OnSimulationStarted();
                }
                // When entering play mode, ensure player controller is active
                assetBrowserStatus = "Entered play mode - Use WASD to move, hold Right Mouse Button to look.";
                std::cout << assetBrowserStatus << std::endl;
            }
            if (toolbarActions.pauseClicked && g_SimPlaying) {
                g_SimPaused = !g_SimPaused;
                if (g_SimPaused) {
                    assetBrowserStatus = "Simulation paused - scripts and physics frozen.";
                } else {
                    assetBrowserStatus = "Simulation resumed - play continues.";
                }
                std::cout << assetBrowserStatus << std::endl;
            }
            if (toolbarActions.stopClicked) {
                if (g_SimPlaying || g_SimPaused) {
                    if (g_LuaScriptEnginePtr) {
                        g_LuaScriptEnginePtr->OnSimulationStopped();
                    }
                    cameraController.setActive(false);
                    g_SimPlaying = false;
                    g_SimPaused = false;
                    g_ShowEditorGizmo = g_ShowGizmoBeforePlay;
                    
                    // Clear all physics bodies BEFORE restoring the scene
                    if (physicsInitialized) {
                        joltPhysicsSystem.ClearAllBodies();
                    }
                    
                    // Restore scene state from snapshot
                    if (!g_SceneSnapshotBeforePlay.empty()) {
                        scene.Clear();
                        if (SceneSerializer::DeserializeFromString(scene, g_SceneSnapshotBeforePlay)) {
                            hydrateScriptsFromComponents(scene);
                            // Reset all physics velocities to ensure clean state
                            auto rigidBodyView = scene.GetEntitiesWith<RigidBodyComponent>();
                            for (auto entityHandle : rigidBodyView) {
                                Entity entity(entityHandle, &scene);
                                auto& rigidBody = entity.GetComponent<RigidBodyComponent>();
                                rigidBody.linearVelocity = Vec3(0, 0, 0);
                                rigidBody.angularVelocity = Vec3(0, 0, 0);
                                rigidBody.InvalidateBodyHandle();
                            }
                            assetBrowserStatus = "Exited play mode - scene restored to pre-play state.";
                        } else {
                            assetBrowserStatus = "Exited play mode - WARNING: failed to restore scene state.";
                        }
                        g_SceneSnapshotBeforePlay.clear();
                    } else {
                        assetBrowserStatus = "Exited play mode - returned to editor.";
                    }
                    
                    std::cout << assetBrowserStatus << std::endl;
                }
            }
            if (toolbarActions.saveClicked) {
                namespace fs = std::filesystem;

                fs::path projectRoot = ResolveActiveProjectRoot();
                if (projectRoot.empty()) {
                    assetBrowserStatus = "Scene save failed: no active project.";
                } else {
                    fs::path scenesDir = (projectRoot / "assets" / "scenes").lexically_normal();
                    std::error_code dirErr;
                    fs::create_directories(scenesDir, dirErr);
                    if (dirErr) {
                        assetBrowserStatus = "Scene save failed: unable to create scenes directory (" + dirErr.message() + ").";
                        std::cerr << assetBrowserStatus << std::endl;
                    } else {
                        std::string fileStem = SanitizeSceneFilename(scene.GetName());
                        fs::path scenePath = scenesDir / (fileStem + ".hpscene");

                        SceneSerializer serializer(scene);
                        std::string error;
                        if (serializer.Serialize(scenePath, error)) {
                            fs::path relative = scenePath.lexically_relative(projectRoot);
                            std::string friendly = relative.empty() ? scenePath.filename().generic_string()
                                                                     : relative.generic_string();
                            assetBrowserStatus = "Scene saved to " + friendly + ".";
                            std::cout << assetBrowserStatus << std::endl;
                        } else {
                            assetBrowserStatus = "Scene save failed: " + error;
                            std::cerr << assetBrowserStatus << std::endl;
                        }
                    }
                }
            }
            if (toolbarActions.buildClicked) {
                assetBrowserStatus = "Build command triggered (placeholder).";
            }
            if (toolbarActions.exportClicked) {
                namespace fs = std::filesystem;

                fs::path projectRoot = ResolveActiveProjectRoot();
                if (projectRoot.empty()) {
                    assetBrowserStatus = "Export failed: no active project.";
                    std::cerr << assetBrowserStatus << std::endl;
                } else {
                    // Create export directory
                    std::string exportDirName = g_ActiveProjectName.empty() ? "export" : (g_ActiveProjectName + "_export");
                    fs::path exportDir = projectRoot / "dist" / exportDirName;

                    // Configure export options
                    SceneExporter::ExportOptions options;
                    options.outputDirectory = exportDir;
                    options.projectName = g_ActiveProjectName.empty() ? "HephaestusProject" : g_ActiveProjectName;
                    options.projectRootOverride = projectRoot;
                    options.engineRootOverride = ResolveEngineRoot();
                    options.includeShaders = true;
                    options.createManifest = true;

                    // Perform export
                    std::cout << "\n=== Starting Scene Export ===\n";
                    SceneExporter::ExportResult result = SceneExporter::Export(scene, options);

                    if (result.success) {
                        assetBrowserStatus = "Export completed! " + std::to_string(result.exportedAssets.size()) + 
                                           " files exported to dist/" + exportDirName + "/";
                        std::cout << assetBrowserStatus << "\n";
                        std::cout << "Manifest: " << result.manifestPath.string() << "\n";
                        std::cout << "=== Export Complete ===\n\n";
                    } else {
                        assetBrowserStatus = "Export failed: " + result.errorMessage;
                        std::cerr << assetBrowserStatus << "\n";
                        std::cerr << "=== Export Failed ===\n\n";
                    }
                }
            }

            DrawViewportOverlay(currentGizmoOperation, currentGizmoMode, g_ShowGizmoGrid, g_ShowEditorGizmo);

            // Handle drag-and-drop to viewport (drop actors into scene)
            static WorldOutlinerSpawnPreset s_ViewportDroppedPreset = WorldOutlinerSpawnPreset::DefaultCube;
            static bool s_ViewportHasDroppedItem = false;
            
            // Create an invisible window over the central dockspace area to accept drops
            ImGuiViewport* dropVp = ImGui::GetMainViewport();
            const ImGuiStyle& dropStyle = ImGui::GetStyle();
            const float dropToolbarHeight = std::max(50.0f, ImGui::GetFrameHeight() * 2.2f);
            const float dropTopOffset = dropToolbarHeight;
            
            ImGui::SetNextWindowPos(ImVec2(dropVp->Pos.x, dropVp->Pos.y + dropTopOffset));
            ImGui::SetNextWindowSize(ImVec2(dropVp->Size.x, std::max(0.0f, dropVp->Size.y - dropTopOffset)));
            ImGui::SetNextWindowBgAlpha(0.0f);
            
            ImGuiWindowFlags dropTargetFlags = ImGuiWindowFlags_NoTitleBar |
                                                ImGuiWindowFlags_NoResize |
                                                ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoScrollbar |
                                                ImGuiWindowFlags_NoScrollWithMouse |
                                                ImGuiWindowFlags_NoCollapse |
                                                ImGuiWindowFlags_NoSavedSettings |
                                                ImGuiWindowFlags_NoBringToFrontOnFocus |
                                                ImGuiWindowFlags_NoFocusOnAppearing |
                                                ImGuiWindowFlags_NoNavFocus |
                                                ImGuiWindowFlags_NoNavInputs |
                                                ImGuiWindowFlags_NoDecoration;
            
            ImGui::Begin("##ViewportDropTarget", nullptr, dropTargetFlags);
            
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ACTOR_PALETTE_ITEM")) {
                    IM_ASSERT(payload->DataSize == sizeof(WorldOutlinerSpawnPreset));
                    s_ViewportDroppedPreset = *(const WorldOutlinerSpawnPreset*)payload->Data;
                    s_ViewportHasDroppedItem = true;
                }
                ImGui::EndDragDropTarget();
            }
            
            ImGui::End();
            
            // Process the drop - add it to outlinerActions so it gets handled with other spawn requests
            if (s_ViewportHasDroppedItem) {
                outlinerActions.spawnRequest = s_ViewportDroppedPreset;
                s_ViewportHasDroppedItem = false;
            }
        }
        }  // end if (!g_RuntimeMode) - close editor UI section

        if (!g_RuntimeMode && pendingAssetImport) {
            Entity imported = importModelFromFile(*pendingAssetImport);
            if (!imported) {
                std::cerr << "Model import failed for path: " << pendingAssetImport->string() << std::endl;
            }
        }

        bool collisionsShouldRun = g_EnableMeshCollision && g_SimPlaying && !g_SimPaused;
        collisionSystem.SetEnabled(collisionsShouldRun);
        collisionSystem.Update(scene, dt);
        
        // Update Jolt Physics (only if initialized)
        if (physicsInitialized) {
            bool physicsShouldRun = g_SimPlaying && !g_SimPaused;
            joltPhysicsSystem.SetEnabled(physicsShouldRun);
            try {
                joltPhysicsSystem.Update(scene, dt);
            } catch (const std::exception& e) {
                std::cerr << "Physics system update error: " << e.what() << "\n";
                physicsInitialized = false;  // Disable physics after error
            }
        }
        
        // ImGuizmo keyboard shortcuts (editor only)
        if (!g_RuntimeMode && g_ProjectSelected) {
            if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmoOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_T) && !ImGui::GetIO().KeyCtrl) currentGizmoOperation = ImGuizmo::SCALE;
            
            // Editor copy/paste: Ctrl+C, Ctrl+V (single-entity clipboard)
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !io.WantTextInput) {
                Entity selected = scene.GetSelectedEntity();
                if (selected) {
                    scene.CopyEntityToClipboard(selected);
                    // optional: feedback can be added later
                }
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !io.WantTextInput) {
                if (scene.HasClipboardEntity()) {
                    PushSceneUndoSnapshot(scene);
                    Entity pasted = scene.PasteClipboard();
                    if (pasted) {
                        scene.SetSelectedEntity(pasted);
                    }
                }
            }
            if (!g_SimPlaying && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !io.WantTextInput) {
                performSceneUndo(scene);
            }
        }
        
        // Build view/proj for ImGuizmo and gizmo interactions
        UniformBufferObject ubo = makeCameraUBO();

        // Custom gizmo interaction handling (editor only)
        if (!g_RuntimeMode && g_ProjectSelected && g_ShowEditorGizmo && (!g_SimPlaying || g_SimPaused)) {
            Entity selectedEntity = scene.GetSelectedEntity();
            if (selectedEntity) {
                auto& transform = selectedEntity.GetComponent<TransformComponent>();
                Mat4 objectMatrix = transform.GetLocalMatrix();

                VulkanGizmo::Mode gizmoMode = VulkanGizmo::Mode::Translate;
                if (currentGizmoOperation == ImGuizmo::ROTATE) gizmoMode = VulkanGizmo::Mode::Rotate;
                else if (currentGizmoOperation == ImGuizmo::SCALE) gizmoMode = VulkanGizmo::Mode::Scale;

                VulkanGizmo::Space gizmoSpace = (currentGizmoMode == ImGuizmo::WORLD)
                    ? VulkanGizmo::Space::World
                    : VulkanGizmo::Space::Local;

                vulkanGizmo.UpdateState(objectMatrix, ubo.view, ubo.proj, camera.position, gizmoMode, gizmoSpace);

                double mouseX = 0.0;
                double mouseY = 0.0;
                glfwGetCursorPos(window, &mouseX, &mouseY);
                Ray gizmoRay = ScreenToWorldRay(mouseX, mouseY,
                                                swap.extent.width, swap.extent.height,
                                                ubo.view, ubo.proj);

                int hoveredAxis = gizmoDragging ? gizmoActiveAxis : vulkanGizmo.CheckHover(gizmoRay);
                vulkanGizmo.SetHoveredAxis(hoveredAxis);

                bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

                if (!gizmoDragging) {
                    if (leftDown && hoveredAxis != -1) {
                        gizmoDragging = true;
                        gizmoActiveAxis = hoveredAxis;
                        vulkanGizmo.BeginDrag(gizmoRay, hoveredAxis);
                    }
                } else {
                    if (leftDown) {
                        Mat4 newTransform = vulkanGizmo.HandleDrag(gizmoRay);
                        transform.SetFromMatrix(newTransform);
                        Mat4 updatedMatrix = transform.GetLocalMatrix();
                        vulkanGizmo.UpdateState(updatedMatrix, ubo.view, ubo.proj, camera.position, gizmoMode, gizmoSpace);
                        vulkanGizmo.SetHoveredAxis(gizmoActiveAxis);
                    } else {
                        gizmoDragging = false;
                        gizmoActiveAxis = -1;
                        vulkanGizmo.EndDrag();
                    }
                }
            } else {
                vulkanGizmo.SetHoveredAxis(-1);
                if (gizmoDragging) {
                    gizmoDragging = false;
                    gizmoActiveAxis = -1;
                    vulkanGizmo.EndDrag();
                }
            }
        } else {
            vulkanGizmo.SetHoveredAxis(-1);
            if (gizmoDragging) {
                gizmoDragging = false;
                gizmoActiveAxis = -1;
                vulkanGizmo.EndDrag();
            }
        }
        
        // Draw spotlight cone visualizations and directional light arrows (editor only)
        if (!g_RuntimeMode && g_ProjectSelected && g_ShowEditorGizmo && (!g_SimPlaying || g_SimPaused)) {
            Entity selectedEntity = scene.GetSelectedEntity();
            auto spotlightView = scene.GetEntitiesWith<LightComponent>();
            for (auto entityHandle : spotlightView) {
                Entity entity(entityHandle, &scene);
                auto& light = entity.GetComponent<LightComponent>();
                if (light.type == LightType::Spot) {
                    DrawSpotlightConeVisualization(entity, scene, ubo.view, ubo.proj, swap.extent.width, swap.extent.height);
                } else if (light.type == LightType::Directional) {
                    // Only draw arrow if this directional light is selected
                    if (selectedEntity && entity == selectedEntity) {
                        DrawDirectionalLightArrowVisualization(entity, scene, ubo.view, ubo.proj, swap.extent.width, swap.extent.height);
                    }
                }
            }
        }
        
        // Render ImGui (editor only)
        if (!g_RuntimeMode) {
            ImGui::Render();
        }

        // Late update scripts (after all regular updates) - only when playing and not paused
        if (g_SimPlaying && !g_SimPaused) {
            scriptEngine.LateUpdateScripts(dt);
            if (g_LuaScriptEnginePtr) {
                g_LuaScriptEnginePtr->LateUpdate(scene, dt);
            }
        }

        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(vkdev.device);
            swap.Recreate(window);
            createPipeline();
            vulkanGizmo.Recreate(vkdev.device, swap.renderPass, swap.extent, pipeline.descriptorSetLayout);
            // Only rebuild billboard pipeline/descriptors if the texture actually loaded.
            if (textureLoaded) {
                createBillboardPipeline();
            }
            allocateCmdBuffers();
            createDescriptors();
            if (textureLoaded) {
                createBillboardDescriptors();
            }
            imagesInFlight.assign(swap.images.size(), VK_NULL_HANDLE);
            continue;
        } else if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
            std::cerr << "Failed to acquire swapchain image: " << acq << "\n";
            break;
        }

        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            VkResult waitImageFence = vkWaitForFences(vkdev.device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
            if (waitImageFence == VK_ERROR_DEVICE_LOST) {
                std::cerr << "Device lost while waiting for swapchain image fence (image index " << imageIndex << ")." << std::endl;
                break;
            }
            if (waitImageFence != VK_SUCCESS) {
                std::cerr << "vkWaitForFences failed for swapchain image fence (image index " << imageIndex << ") with code " << waitImageFence << "." << std::endl;
                continue;
            }
        }

        // Reset the fence only when we are actually going to submit work for this frame.
        VkResult resetResult = vkResetFences(vkdev.device, 1, &inFlight[currentFrame]);
        if (resetResult != VK_SUCCESS) {
            std::cerr << "Failed to reset in-flight fence (frame " << currentFrame << ") with code " << resetResult << "." << std::endl;
            continue;
        }
        imagesInFlight[imageIndex] = inFlight[currentFrame];

        // Update UBO for this image
        {
            UniformBufferObject ubo = makeCameraUBO();
            void* mapped = nullptr;
            vmaMapMemory(vkdev.allocator, uniformBuffers[imageIndex].allocation, &mapped);
            std::memcpy(mapped, &ubo, sizeof(ubo));
            vmaFlushAllocation(vkdev.allocator, uniformBuffers[imageIndex].allocation, 0, VK_WHOLE_SIZE);
            vmaUnmapMemory(vkdev.allocator, uniformBuffers[imageIndex].allocation);
        }
        
        // Removed redundant per-frame Material UBO write. We update material data per-object
        // immediately before each draw to reflect that object's MaterialComponent.
        
        // Update Lighting UBO
        {
            LightingUBO lightUBO = {};
            
            // Gather directional lights
            auto dirLightView = scene.GetEntitiesWith<LightComponent>();
            for (auto entityHandle : dirLightView) {
                Entity entity(entityHandle, &scene);
                auto& light = entity.GetComponent<LightComponent>();
                
                if (light.type == LightType::Directional) {
                    // Compute direction from entity's transform rotation
                    Vec3 direction = Vec3(0.0f, -1.0f, 0.0f); // Default downward direction
                    if (entity.HasComponent<TransformComponent>()) {
                        auto& transform = entity.GetComponent<TransformComponent>();
                        // Create rotation matrix from transform
                        Mat4 rx = Mat4::RotationX(transform.rotation.x);
                        Mat4 ry = Mat4::RotationY(transform.rotation.y);
                        Mat4 rz = Mat4::RotationZ(transform.rotation.z);
                        Mat4 rotMatrix = Mat4::Multiply(rx, Mat4::Multiply(ry, rz));
                        // Transform the default direction by rotation
                        direction = Mat4::TransformDirection(rotMatrix, direction);
                        direction = vnorm(direction);
                    }
                    lightUBO.dirLightDir = direction;
                    lightUBO.dirLightIntensity = light.intensity;
                    lightUBO.dirLightColor = light.color;
                    break; // Only support one directional light for now
                }
            }
            
            // Gather point lights (up to 4)
            int pointLightIndex = 0;
            for (auto entityHandle : dirLightView) {
                if (pointLightIndex >= 4) break;
                
                Entity entity(entityHandle, &scene);
                auto& light = entity.GetComponent<LightComponent>();
                
                if (light.type == LightType::Point && entity.HasComponent<TransformComponent>()) {
                    auto& transform = entity.GetComponent<TransformComponent>();
                    
                    lightUBO.pointLightPositions[pointLightIndex].x = transform.position.x;
                    lightUBO.pointLightPositions[pointLightIndex].y = transform.position.y;
                    lightUBO.pointLightPositions[pointLightIndex].z = transform.position.z;
                    lightUBO.pointLightPositions[pointLightIndex].range = light.range;
                    
                    lightUBO.pointLightColors[pointLightIndex].r = light.color.x;
                    lightUBO.pointLightColors[pointLightIndex].g = light.color.y;
                    lightUBO.pointLightColors[pointLightIndex].b = light.color.z;
                    lightUBO.pointLightColors[pointLightIndex].intensity = light.intensity;
                    
                    lightUBO.pointLightAttenuation[pointLightIndex].constant = light.constant;
                    lightUBO.pointLightAttenuation[pointLightIndex].linear = light.linear;
                    lightUBO.pointLightAttenuation[pointLightIndex].quadratic = light.quadratic;
                    
                    pointLightIndex++;
                }
            }
            lightUBO.pointLightCount = pointLightIndex;
            
            // Gather spot lights (up to 4)
            int spotLightIndex = 0;
            for (auto entityHandle : dirLightView) {
                if (spotLightIndex >= 4) break;
                
                Entity entity(entityHandle, &scene);
                auto& light = entity.GetComponent<LightComponent>();
                
                if (light.type == LightType::Spot && entity.HasComponent<TransformComponent>()) {
                    auto& transform = entity.GetComponent<TransformComponent>();
                    
                    lightUBO.spotLightPositions[spotLightIndex].x = transform.position.x;
                    lightUBO.spotLightPositions[spotLightIndex].y = transform.position.y;
                    lightUBO.spotLightPositions[spotLightIndex].z = transform.position.z;
                    lightUBO.spotLightPositions[spotLightIndex].range = light.range;
                    
                    // Calculate spotlight direction from entity's rotation (same as cone visualization)
                    Mat4 transformMatrix = transform.GetLocalMatrix();
                    Vec3 spotDirection = Mat4::TransformDirection(transformMatrix, Vec3(0, 0, -1));
                    spotDirection = vnorm(spotDirection);
                    
                    lightUBO.spotLightDirections[spotLightIndex].x = spotDirection.x;
                    lightUBO.spotLightDirections[spotLightIndex].y = spotDirection.y;
                    lightUBO.spotLightDirections[spotLightIndex].z = spotDirection.z;
                    lightUBO.spotLightDirections[spotLightIndex].innerAngle = light.innerConeAngle;
                    
                    lightUBO.spotLightColors[spotLightIndex].r = light.color.x;
                    lightUBO.spotLightColors[spotLightIndex].g = light.color.y;
                    lightUBO.spotLightColors[spotLightIndex].b = light.color.z;
                    lightUBO.spotLightColors[spotLightIndex].intensity = light.intensity;
                    
                    lightUBO.spotLightAttenuation[spotLightIndex].constant = light.constant;
                    lightUBO.spotLightAttenuation[spotLightIndex].linear = light.linear;
                    lightUBO.spotLightAttenuation[spotLightIndex].quadratic = light.quadratic;
                    lightUBO.spotLightAttenuation[spotLightIndex].outerAngle = light.outerConeAngle;
                    
                    spotLightIndex++;
                }
            }
            lightUBO.spotLightCount = spotLightIndex;
            
            // Ambient lighting
            lightUBO.ambientColor = Vec3(0.1f, 0.1f, 0.15f);
            lightUBO.ambientIntensity = 0.3f;
            
            void* mapped = nullptr;
            vmaMapMemory(vkdev.allocator, lightingBuffers[imageIndex].allocation, &mapped);
            std::memcpy(mapped, &lightUBO, sizeof(lightUBO));
            vmaFlushAllocation(vkdev.allocator, lightingBuffers[imageIndex].allocation, 0, VK_WHOLE_SIZE);
            vmaUnmapMemory(vkdev.allocator, lightingBuffers[imageIndex].allocation);
        }

        VkCommandBuffer cmd = cmdBufs[imageIndex];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cmd, &bi);

        // Get environment settings
        Vec3 clearColor(0.1f, 0.2f, 0.3f);
        bool useHDRI = false;
        if (environmentEntity && environmentEntity.HasComponent<EnvironmentComponent>()) {
            auto& env = environmentEntity.GetComponent<EnvironmentComponent>();
            if (env.type == EnvironmentComponent::BackgroundType::SolidColor) {
                clearColor = env.solidColor;
            } else if (env.type == EnvironmentComponent::BackgroundType::HDRI) {
                useHDRI = skybox.hasHDRI;
            }
        }

        VkClearValue clear[2];
        clear[0].color = { {clearColor.x, clearColor.y, clearColor.z, 1.0f} };
        clear[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = swap.renderPass;
        rp.framebuffer = swap.framebuffers[imageIndex];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = swap.extent;
    rp.clearValueCount = 2;
    rp.pClearValues = clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkPipeline boundPipeline = pipeline.GetPipeline(false);
    if (boundPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, boundPipeline);
    }

    // Set dynamic viewport/scissor per current extent
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width  = (float)swap.extent.width;
    viewport.height = (float)swap.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{{0,0}, swap.extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Get selected entity once for both outline and gizmo rendering
        Entity selectedEntity = scene.GetSelectedEntity();

        // Draw all scene objects with MeshRendererComponent
        auto renderView = scene.GetEntitiesWith<TransformComponent, MeshRendererComponent>();

        // Prepare dynamic material buffer mapping
        void* matMappedBaseVoid = nullptr;
        vmaMapMemory(vkdev.allocator, materialBuffers[imageIndex].allocation, &matMappedBaseVoid);
        uint8_t* matMappedBase = reinterpret_cast<uint8_t*>(matMappedBaseVoid);
        uint32_t matIndex = 0;

        VkBuffer currentVertexBuffer = VK_NULL_HANDLE;
        VkBuffer currentIndexBuffer = VK_NULL_HANDLE;

        bool materialOverflowLogged = false;
        for (auto entityHandle : renderView) {
            Entity entity(entityHandle, &scene);
            auto& transform = entity.GetComponent<TransformComponent>();
            auto& renderer = entity.GetComponent<MeshRendererComponent>();
            
            if (!renderer.visible)
                continue;

            if (!renderer.HasValidMesh() || renderer.meshHandle >= meshLibrary.size())
                continue;

            MeshGPU& mesh = meshLibrary[renderer.meshHandle];
            if (mesh.indexCount == 0 || mesh.vertexBuffer.buffer == VK_NULL_HANDLE || mesh.indexBuffer.buffer == VK_NULL_HANDLE)
                continue;

            if (g_DebugSkipImportedMeshes && mesh.imported)
                continue;

            if (matIndex >= maxMaterialsPerFrame) {
                if (!materialOverflowLogged) {
                    std::cerr << "Material dynamic UBO capacity exceeded (" << maxMaterialsPerFrame
                              << " objects). Additional objects will be skipped this frame.\n";
                    materialOverflowLogged = true;
                }
                continue;
            }

            VkPipeline desiredPipeline = pipeline.GetPipeline(renderer.backfaceCulling);
            if (desiredPipeline != VK_NULL_HANDLE && desiredPipeline != boundPipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, desiredPipeline);
                boundPipeline = desiredPipeline;
            }

            if (mesh.vertexBuffer.buffer != currentVertexBuffer) {
                VkBuffer vb = mesh.vertexBuffer.buffer;
                VkDeviceSize vbOffset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
                currentVertexBuffer = vb;
            }

            if (mesh.indexBuffer.buffer != currentIndexBuffer) {
                vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                currentIndexBuffer = mesh.indexBuffer.buffer;
            }
            
            // Update material UBO for this specific object
            MaterialUBO matUBO = {};
            if (entity.HasComponent<MaterialComponent>()) {
                auto& mat = entity.GetComponent<MaterialComponent>();
                matUBO.albedo = mat.albedo;
                matUBO.albedoPadding = 0.0f;
                matUBO.metallic = mat.metallic;
                matUBO.metallicPadding0 = 0.0f;
                matUBO.metallicPadding1 = 0.0f;
                matUBO.metallicPadding2 = 0.0f;
                matUBO.emissive = mat.emissive;
                matUBO.emissivePadding = 0.0f;
                matUBO.roughness = mat.roughness;
                matUBO.ao = mat.ao;
                matUBO.emissiveStrength = mat.emissiveStrength;
                
                // Albedo texture
                uint32_t albedoSlot = g_DefaultMaterialTextureSlot;
                if (mat.useAlbedoTexture && !mat.albedoTexturePath.empty()) {
                    albedoSlot = ensureMaterialTextureSlot(mat.albedoTexturePath);
                    if (albedoSlot != g_DefaultMaterialTextureSlot) {
                        matUBO.useAlbedoTexture = 1u;
                    } else {
                        matUBO.useAlbedoTexture = 0u;
                    }
                } else {
                    matUBO.useAlbedoTexture = 0u;
                }
                matUBO.albedoTextureIndex = albedoSlot;
                
                // Normal map texture
                uint32_t normalSlot = g_DefaultMaterialTextureSlot;
                if (mat.useNormalTexture && !mat.normalTexturePath.empty()) {
                    normalSlot = ensureMaterialTextureSlot(mat.normalTexturePath);
                    if (normalSlot != g_DefaultMaterialTextureSlot) {
                        matUBO.useNormalTexture = 1u;
                    } else {
                        matUBO.useNormalTexture = 0u;
                    }
                } else {
                    matUBO.useNormalTexture = 0u;
                }
                matUBO.normalTextureIndex = normalSlot;
                
                // Roughness texture
                uint32_t roughnessSlot = g_DefaultMaterialTextureSlot;
                if (mat.useRoughnessTexture && !mat.roughnessTexturePath.empty()) {
                    roughnessSlot = ensureMaterialTextureSlot(mat.roughnessTexturePath);
                    if (roughnessSlot != g_DefaultMaterialTextureSlot) {
                        matUBO.useRoughnessTexture = 1u;
                    } else {
                        matUBO.useRoughnessTexture = 0u;
                    }
                } else {
                    matUBO.useRoughnessTexture = 0u;
                }
                matUBO.roughnessTextureIndex = roughnessSlot;
                
                // AO texture
                uint32_t aoSlot = g_DefaultMaterialTextureSlot;
                if (mat.useAoTexture && !mat.aoTexturePath.empty()) {
                    aoSlot = ensureMaterialTextureSlot(mat.aoTexturePath);
                    if (aoSlot != g_DefaultMaterialTextureSlot) {
                        matUBO.useAoTexture = 1u;
                    } else {
                        matUBO.useAoTexture = 0u;
                    }
                } else {
                    matUBO.useAoTexture = 0u;
                }
                matUBO.aoTextureIndex = aoSlot;
                
                matUBO.paddingTail[0] = 0;
                matUBO.paddingTail[1] = 0;
                matUBO.paddingTail[2] = 0;
                matUBO.paddingTail[3] = 0;
                matUBO.paddingTail[4] = 0;
                matUBO.paddingTail[5] = 0;
            } else {
                // Default white material
                matUBO.albedo = Vec3(1.0f, 1.0f, 1.0f);
                matUBO.albedoPadding = 0.0f;
                matUBO.metallic = 0.0f;
                matUBO.metallicPadding0 = 0.0f;
                matUBO.metallicPadding1 = 0.0f;
                matUBO.metallicPadding2 = 0.0f;
                matUBO.roughness = 0.5f;
                matUBO.ao = 1.0f;
                matUBO.emissive = Vec3(0.0f, 0.0f, 0.0f);
                matUBO.emissivePadding = 0.0f;
                matUBO.emissiveStrength = 0.0f;
                matUBO.useAlbedoTexture = 0u;
                matUBO.albedoTextureIndex = g_DefaultMaterialTextureSlot;
                matUBO.useNormalTexture = 0u;
                matUBO.normalTextureIndex = g_DefaultMaterialTextureSlot;
                matUBO.useRoughnessTexture = 0u;
                matUBO.roughnessTextureIndex = g_DefaultMaterialTextureSlot;
                matUBO.useAoTexture = 0u;
                matUBO.aoTextureIndex = g_DefaultMaterialTextureSlot;
                matUBO.paddingTail[0] = 0;
                matUBO.paddingTail[1] = 0;
                matUBO.paddingTail[2] = 0;
                matUBO.paddingTail[3] = 0;
                matUBO.paddingTail[4] = 0;
                matUBO.paddingTail[5] = 0;
            }
            
            // Write into this object's slot in the dynamic material buffer
            VkDeviceSize matOffsetBytes = static_cast<VkDeviceSize>(matIndex) * materialStride;
            std::memcpy(matMappedBase + matOffsetBytes, &matUBO, sizeof(matUBO));
                
            Mat4 modelMatrix = transform.GetLocalMatrix();
            vkCmdPushConstants(cmd, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), modelMatrix.m);
            // Bind descriptor set with dynamic offset for this object's material
            uint32_t dynOffset = static_cast<uint32_t>(matOffsetBytes);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout,
                                    0, 1, &descriptorSets[imageIndex], 1, &dynOffset);
            vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
            matIndex++;
        }

        // Flush the written range and unmap once per frame
        if (matMappedBaseVoid) {
            VkDeviceSize writtenSize = static_cast<VkDeviceSize>(matIndex) * materialStride;
            if (writtenSize > 0) {
                vmaFlushAllocation(vkdev.allocator, materialBuffers[imageIndex].allocation, 0, writtenSize);
            }
            vmaUnmapMemory(vkdev.allocator, materialBuffers[imageIndex].allocation);
        }

        // ── Render Custom Vulkan Gizmo ───────────────────────────
        // selectedEntity already declared above for outline rendering
    if (g_ProjectSelected && g_ShowEditorGizmo && (!g_SimPlaying || g_SimPaused) && selectedEntity) {
            auto& transform = selectedEntity.GetComponent<TransformComponent>();
            Mat4 objectMatrix = transform.GetLocalMatrix();
            
            VulkanGizmo::Mode gizmoMode = VulkanGizmo::Mode::Translate;
            if (currentGizmoOperation == ImGuizmo::ROTATE) gizmoMode = VulkanGizmo::Mode::Rotate;
            else if (currentGizmoOperation == ImGuizmo::SCALE) gizmoMode = VulkanGizmo::Mode::Scale;

            VulkanGizmo::Space gizmoSpace = (currentGizmoMode == ImGuizmo::WORLD) ? VulkanGizmo::Space::World : VulkanGizmo::Space::Local;

            vulkanGizmo.Render(cmd, descriptorSets[imageIndex]);
        }

        // ── Render Light Billboards ───────────────────────────────
        if (billboardPipeline.pipeline != VK_NULL_HANDLE && 
            !billboardDescriptorSets_Point.empty() && 
            !billboardDescriptorSets_Spot.empty() && 
            !billboardDescriptorSets_Directional.empty()) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipeline.pipeline);
            
            // Draw a billboard for each light entity
            auto lightView = scene.GetEntitiesWith<LightComponent>();
            for (auto entityHandle : lightView) {
                Entity entity(entityHandle, &scene);
                auto& light = entity.GetComponent<LightComponent>();
                
                // Get light position
                Vec3 lightPos(0.0f, 0.0f, 0.0f);
                
                // Select descriptor set based on light type
                VkDescriptorSet descriptorSet;
                if (light.type == LightType::Directional) {
                    // For directional lights, use transform position if available
                    if (entity.HasComponent<TransformComponent>()) {
                        auto& transform = entity.GetComponent<TransformComponent>();
                        lightPos = transform.position;
                    } else {
                        // Fallback: compute direction from default rotation and offset billboard
                        Vec3 direction = Vec3(0.0f, -1.0f, 0.0f);
                        lightPos = vscale(direction, 5.0f);
                    }
                    descriptorSet = billboardDescriptorSets_Directional[imageIndex];
                } else if (light.type == LightType::Spot) {
                    if (entity.HasComponent<TransformComponent>()) {
                        auto& transform = entity.GetComponent<TransformComponent>();
                        lightPos = transform.position;
                    }
                    descriptorSet = billboardDescriptorSets_Spot[imageIndex];
                } else {
                    // Point light
                    if (entity.HasComponent<TransformComponent>()) {
                        auto& transform = entity.GetComponent<TransformComponent>();
                        lightPos = transform.position;
                    }
                    descriptorSet = billboardDescriptorSets_Point[imageIndex];
                }
                
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipeline.layout,
                                       0, 1, &descriptorSet, 0, nullptr);
                
                // Push constants: position (vec3) + size (float)
                struct BillboardPushConstants {
                    Vec3 position;
                    float size;
                } billboardPC;
                
                billboardPC.position = lightPos;
                billboardPC.size = 0.3f; // Billboard size in world units
                
                vkCmdPushConstants(cmd, billboardPipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 
                                  0, sizeof(BillboardPushConstants), &billboardPC);
                
                // Draw 4 vertices (one quad) per billboard
                vkCmdDraw(cmd, 4, 1, 0, 0);
            }
        }

        // ── Render Selection Outline ─────────────────────────────
        // Render after all normal geometry so outline appears on top
        if (g_ProjectSelected && selectedEntity && selectedEntity.HasComponent<MeshRendererComponent>() && 
            selectedEntity.HasComponent<TransformComponent>() && pipeline.outlinePipeline != VK_NULL_HANDLE) {
            
            auto& transform = selectedEntity.GetComponent<TransformComponent>();
            auto& renderer = selectedEntity.GetComponent<MeshRendererComponent>();
            
            if (renderer.visible && renderer.HasValidMesh() && renderer.meshHandle < meshLibrary.size()) {
                MeshGPU& mesh = meshLibrary[renderer.meshHandle];
                
                if (mesh.indexCount > 0 && mesh.vertexBuffer.buffer != VK_NULL_HANDLE && 
                    mesh.indexBuffer.buffer != VK_NULL_HANDLE) {
                    
                    // Bind outline pipeline
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.outlinePipeline);
                    
                    // Bind vertex and index buffers
                    VkBuffer vb = mesh.vertexBuffer.buffer;
                    VkDeviceSize vbOffset = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
                    vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                    
                    // Push model matrix
                    Mat4 modelMatrix = transform.GetLocalMatrix();
                    vkCmdPushConstants(cmd, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), modelMatrix.m);
                    
                    // Bind descriptor set
                    uint32_t dynOffset = 0;
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout,
                                          0, 1, &descriptorSets[imageIndex], 1, &dynOffset);
                    
                    // Draw the outline
                    vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
                }
            }
        }

        // Render skybox if HDRI is enabled
        if (useHDRI) {
            skybox.UpdateDescriptorSet(imageIndex, uniformBuffers[imageIndex].buffer);
            skybox.Draw(cmd, imageIndex);
        }

        // Render ImGui draw data (editor only)
        if (!g_RuntimeMode) {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        }

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        // Submit
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &imageAvailable[currentFrame];
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &renderFinished[currentFrame];

    vkQueueSubmit(vkdev.graphicsQueue, 1, &submit, inFlight[currentFrame]);

        // Present
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderFinished[currentFrame];
        present.swapchainCount = 1;
        present.pSwapchains = &swap.swapchain;
        present.pImageIndices = &imageIndex;
        VkResult pres = vkQueuePresentKHR(vkdev.presentQueue, &present);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR || g_framebufferResized) {
            g_framebufferResized = false;
            vkDeviceWaitIdle(vkdev.device);
            swap.Recreate(window);
            createPipeline();
            vulkanGizmo.Recreate(vkdev.device, swap.renderPass, swap.extent, pipeline.descriptorSetLayout);
            // Only rebuild billboard pipeline/descriptors if the texture actually loaded.
            if (textureLoaded) {
                createBillboardPipeline();
            }
            allocateCmdBuffers();
            createDescriptors();
            if (textureLoaded) {
                createBillboardDescriptors();
            }
            imagesInFlight.assign(swap.images.size(), VK_NULL_HANDLE);
        } else if (pres != VK_SUCCESS) {
            std::cerr << "Present failed: " << pres << "\n";
            break;
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(vkdev.device);

    // ── Cleanup ────────────────────────────────
    if (projectManagerInitialized) {
        g_ProjectManager.Shutdown();
    }
    scriptEngine.Shutdown();
    SaveEditorSettings();

    // Cleanup sync objects
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(vkdev.device, imageAvailable[i], nullptr);
        vkDestroySemaphore(vkdev.device, renderFinished[i], nullptr);
        vkDestroyFence(vkdev.device, inFlight[i], nullptr);
    }
    
    // Cleanup ImGui
    DestroyAssetBrowserIcons(vkdev);
    if (g_MaterialEditorPanel) {
        delete g_MaterialEditorPanel;
        g_MaterialEditorPanel = nullptr;
    }
    ImNodes::DestroyContext();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(vkdev.device, imguiPool, nullptr);

    vulkanGizmo.Cleanup(vkdev.device, vkdev.allocator);
    skybox.Cleanup(vkdev.allocator);
    for (auto& mesh : meshLibrary) {
        mesh.Destroy(vkdev.allocator);
    }
    destroyDescriptors();

    // Cleanup material texture slots
    for (auto& slot : g_MaterialTextureSlots) {
        if (slot.valid) {
            slot.texture.Destroy(vkdev.allocator, vkdev.device);
        }
    }
    g_MaterialTextureSlots.clear();
    g_MaterialTextureLookup.clear();
    
    // Cleanup billboard resources
    billboardPipeline.Cleanup();
    pointLightTexture.Destroy(vkdev.allocator, vkdev.device);
    // Only destroy spot and directional if they're not copies of point light
    if (spotLightTexture.imageView != pointLightTexture.imageView) {
        spotLightTexture.Destroy(vkdev.allocator, vkdev.device);
    }
    if (directionalLightTexture.imageView != pointLightTexture.imageView) {
        directionalLightTexture.Destroy(vkdev.allocator, vkdev.device);
    }
    if (billboardDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkdev.device, billboardDescriptorPool, nullptr);
    }
    if (billboardDescriptorLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vkdev.device, billboardDescriptorLayout, nullptr);
    }
    
    pipeline.Cleanup();
    swap.Cleanup();
    vkdev.Cleanup();
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
