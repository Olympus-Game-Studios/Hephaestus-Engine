#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>
#include <string>
#include <filesystem>
#include <algorithm>
#include <system_error>
#include <optional>
#include <cstdint>
#include <unordered_map>
#include <cctype>
#include <array>

#include "Math.h"

#include "../Assets/MeshAsset.h"
#include "../Assets/ModelImporter.h"
#include "../Gfx/VulkanDevice.h"
#include "../Gfx/VulkanSwapchain.h"
#include "../Gfx/VulkanPipeline.h"
#include "../Gfx/VulkanBuffer.h"
#include "../Gfx/VulkanTexture.h"
#include "../Gfx/VulkanGizmo.h"

// Dear ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"

// Scene Graph / ECS
#include "../Scene/Scene.h"
#include "../Scene/Entity.h"
#include "../Scene/Components.h"
#include "../Scene/Transform.h"

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
static bool g_DetailsLocked = false;

struct ToolbarActions {
    bool playClicked = false;
    bool pauseClicked = false;
    bool stopClicked = false;
    bool saveClicked = false;
    bool buildClicked = false;
};

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

static ToolbarActions DrawEditorToolbar(bool isPlaying, bool isPaused)
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

    if (ImGui::Begin("##ViewportOverlay", nullptr, flags)) {
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

static void DrawWorldOutliner(Scene& scene)
{
    if (!ImGui::Begin("World Outliner")) {
        ImGui::End();
        return;
    }

    static ImGuiTextFilter filter;

    float availableWidth = ImGui::GetContentRegionAvail().x;
    const ImGuiStyle& s = ImGui::GetStyle();
    float filterWidth = std::max(availableWidth - (s.ItemSpacing.x * 6.0f + 70.0f), 200.0f);
    ImGui::SetNextItemWidth(filterWidth);
    filter.Draw("##WorldOutlinerFilter", filterWidth);
    ImGui::SameLine();
    DrawPillButton("Filter", false);

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextDisabled("Scene: %s", scene.GetName().c_str());
    ImGui::Spacing();

    auto view = scene.GetEntitiesWith<TagComponent>();

    if (ImGui::BeginTable("WorldOutlinerTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
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
            if (entity.HasComponent<LightComponent>()) {
                typeLabel = "LIGHT";
            } else if (entity.HasComponent<MeshRendererComponent>()) {
                typeLabel = "MESH";
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGuiSelectableFlags selFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick;
            if (ImGui::Selectable(tag.tag.c_str(), isSelected, selFlags)) {
                if (!g_DetailsLocked) {
                    scene.SetSelectedEntity(entity);
                }
            }

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", typeLabel);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

static void DrawDetailsPanel(Scene& scene)
{
    if (!ImGui::Begin("Details")) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Lock", &g_DetailsLocked);
    ImGui::Separator();

    Entity selectedEntity = scene.GetSelectedEntity();
    if (!selectedEntity) {
        ImGui::TextDisabled("Select an entity to view its properties.");
        ImGui::End();
        return;
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

    if (selectedEntity.HasComponent<MaterialComponent>()) {
        auto& material = selectedEntity.GetComponent<MaterialComponent>();
        ImGui::SeparatorText("Material");
        ImGui::ColorEdit3("Albedo", &material.albedo.x);
        ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f);
        ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("AO", &material.ao, 0.0f, 1.0f);
        ImGui::ColorEdit3("Emissive", &material.emissive.x);
        ImGui::SliderFloat("Emissive Strength", &material.emissiveStrength, 0.0f, 10.0f);
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
            ImGui::DragFloat3("Direction", &light.direction.x, 0.01f);
            light.direction = vnorm(light.direction);
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

    ImGui::SeparatorText("Debug");
    ImGui::Text("Entity ID: %u", static_cast<uint32_t>(selectedEntity));
    ImGui::Text("Gizmo Active: %s", g_ShowEditorGizmo ? "YES" : "NO");

    ImGui::End();
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

struct MaterialUBO {
    Vec3 albedo;
    float metallic;
    Vec3 emissive;
    float roughness;
    float ao;
    float emissiveStrength;
    float padding2;
    float padding3;
};

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
    
    int pointLightCount;
    float padding5;
    float padding6;
    float padding7;
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

// Find the closest object hit by a ray (ECS version)
Entity FindClosestHitEntity(const Ray& ray, Scene& scene, float& outDistance)
{
    Entity closestEntity;
    float closestDist = 1e30f;
    
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

    fs::path candidate = fs::current_path();
    for (int depth = 0; depth < 10; ++depth) {
        const bool hasAssets = fs::exists(candidate / "assets");
        const bool hasEditor = fs::exists(candidate / "editor");
        const bool hasEngine = fs::exists(candidate / "engine");
        if (hasAssets && hasEditor && hasEngine) {
            g_EngineRoot = candidate;
            break;
        }
        if (!candidate.has_parent_path()) {
            break;
        }
        candidate = candidate.parent_path();
    }

    if (g_EngineRoot.empty()) {
        g_EngineRoot = fs::current_path();
    }
    return g_EngineRoot;
}

static std::filesystem::path ResolveEnginePath(const std::filesystem::path& relative)
{
    return (ResolveEngineRoot() / relative).lexically_normal();
}

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
    {".prefab", "file-cpp", "PREF"}
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

// ImGui panel that lists files and folders in the asset directory.
static std::optional<std::filesystem::path> DrawAssetBrowserPanel(std::string& statusMessage)
{
    namespace fs = std::filesystem;

    std::optional<fs::path> importRequest;

    static fs::path assetRoot;
    static fs::path currentPath;
    static fs::path selectedPath;

    fs::path projectRoot = ResolveActiveProjectRoot();
    if (projectRoot.empty()) {
        if (ImGui::Begin("Asset Browser")) {
            ImGui::TextDisabled("Select or create a project to browse assets.");
        }
        ImGui::End();
        return importRequest;
    }

    fs::path desiredRoot = ResolveActiveAssetsRoot();
    if (desiredRoot.empty()) {
        if (ImGui::Begin("Asset Browser")) {
            ImGui::TextDisabled("Active project has no assets directory configured.");
        }
        ImGui::End();
        return importRequest;
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
        return importRequest;
    }

    assetRoot = assetRoot.lexically_normal();

    if (currentPath.empty()) {
        currentPath = assetRoot;
    }
    currentPath = ClampToRoot(currentPath, assetRoot);

    if (!ImGui::Begin("Asset Browser")) {
        ImGui::End();
        return importRequest;
    }

    std::error_code existsErr;
    bool rootExists = fs::exists(assetRoot, existsErr);
    if (!rootExists || existsErr) {
        std::string rootDisplay = assetRoot.generic_string();
        ImGui::TextDisabled("Asset directory not found: %s", rootDisplay.c_str());
        ImGui::End();
        return importRequest;
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
        return importRequest;
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
                        selectedPath = ClampToRoot(entry.path, assetRoot);
                    }
                    statusMessage.clear();
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
            importRequest = selectedPath;
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

    ImGui::End();
    return importRequest;
}

static std::optional<std::filesystem::path> DrawProjectBrowser()
{
    namespace fs = std::filesystem;

    std::optional<fs::path> selection;

    static std::array<char, 128> s_NewProjectBuffer{};
    static std::string s_SelectedProjectName;
    static std::string s_ErrorMessage;
    static std::string s_StatusMessage;

    fs::path projectsRoot = ResolveProjectsRoot();
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
        ImGui::Text("Create New Project");

        ImGui::PushItemWidth(-140.0f);
        ImGui::InputTextWithHint("##NewProjectName", "Project name", s_NewProjectBuffer.data(), s_NewProjectBuffer.size());
        bool enterPressed = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter);
        ImGui::PopItemWidth();

        ImGui::SameLine();
        bool createClicked = ImGui::Button("Create", ImVec2(120.0f, 0.0f));

        if (createClicked || enterPressed) {
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
                    fs::path newProjectPath = (projectsRoot / sanitized).lexically_normal();
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
                                s_ErrorMessage.clear();
                                s_StatusMessage = "Created project '" + sanitized + "'.";
                                s_SelectedProjectName = sanitized;
                                std::fill(s_NewProjectBuffer.begin(), s_NewProjectBuffer.end(), 0);
                                selection = newProjectPath;
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
    }
    ImGui::End();

    ImGui::PopStyleVar(2);

    return selection;
}

int main()
{
    // ── GLFW Window ───────────────────────────────
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Hephaestus Engine", nullptr, nullptr);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetScrollCallback(window, scrollCallback);

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

    std::vector<MeshGPU> meshLibrary;
    meshLibrary.reserve(32);

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
        std::cout << "[uploadMesh] Mesh '" << name << "' uploaded to handle " << handle << std::endl;
        return handle;
    };

    // ── Vertex Data ───────────────────────────────────
    // 3D Cube vertices with normals - we need unique vertices per face for proper lighting
    const std::vector<Vertex> cubeVertices = {
        // Front face (Z+) - all normals point forward (0, 0, 1)
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},  // 0
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},  // 1
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},  // 2
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},  // 3
        
        // Back face (Z-) - normals point backward (0, 0, -1)
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},  // 4
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},  // 5
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},  // 6
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},  // 7
        
        // Right face (X+) - normals point right (1, 0, 0)
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 8
        {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 9
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 10
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 11
        
        // Left face (X-) - normals point left (-1, 0, 0)
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 12
        {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 13
        {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 14
        {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 15
        
        // Top face (Y+) - normals point up (0, 1, 0)
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 16
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 17
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 18
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 19
        
        // Bottom face (Y-) - normals point down (0, -1, 0)
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 20
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 21
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 22
        {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},  // 23
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

    size_t cubeMeshIndex = MeshRendererComponent::InvalidMesh;
    try {
    cubeMeshIndex = uploadMesh("Default Cube", cubeMeshData, std::filesystem::path("builtin://cube"), false);
    } catch (const std::exception& e) {
        std::cerr << "Failed to upload default cube mesh: " << e.what() << std::endl;
        return -1;
    }

    // ── Camera + UBO ───────────────────────────
    Camera camera;
    camera.position = {0.0f, 0.0f, 3.0f}; // Position camera to view the cube
    // Start with cursor enabled (Unreal-style - hold RMB to look/move)
    g_cameraPtr = &camera; // Set global pointer for scroll callback

    // ── Scene & Entities (ECS) ───────────────────
    Scene scene("Main Scene");
    
    // Create cube entities with different materials
    Entity cube1 = scene.CreateEntity("Cube 1 (Metal)");
    auto& cube1Renderer = cube1.AddComponent<MeshRendererComponent>();
    cube1Renderer.SetMesh(cubeMeshIndex, cubeMeshData.bounds);
    cube1.AddComponent<SelectionComponent>(true); // Start selected
    auto& cube1Mat = cube1.AddComponent<MaterialComponent>(Vec3(0.8f, 0.2f, 0.2f)); // Red
    cube1Mat.metallic = 0.9f;
    cube1Mat.roughness = 0.2f;
    auto& cube1Transform = cube1.GetComponent<TransformComponent>();
    cube1Transform.position = Vec3(0.0f, 0.0f, 0.0f);
    
    Entity cube2 = scene.CreateEntity("Cube 2 (Plastic)");
    auto& cube2Renderer = cube2.AddComponent<MeshRendererComponent>();
    cube2Renderer.SetMesh(cubeMeshIndex, cubeMeshData.bounds);
    auto& cube2Mat = cube2.AddComponent<MaterialComponent>(Vec3(0.2f, 0.8f, 0.2f)); // Green
    cube2Mat.metallic = 0.0f;
    cube2Mat.roughness = 0.5f;
    auto& cube2Transform = cube2.GetComponent<TransformComponent>();
    cube2Transform.position = Vec3(2.0f, 0.0f, 0.0f);
    cube2Transform.scale = Vec3(0.5f, 0.5f, 0.5f);
    
    Entity cube3 = scene.CreateEntity("Cube 3 (Rough)");
    auto& cube3Renderer = cube3.AddComponent<MeshRendererComponent>();
    cube3Renderer.SetMesh(cubeMeshIndex, cubeMeshData.bounds);
    auto& cube3Mat = cube3.AddComponent<MaterialComponent>(Vec3(0.2f, 0.4f, 0.8f)); // Blue
    cube3Mat.metallic = 0.0f;
    cube3Mat.roughness = 0.9f;
    auto& cube3Transform = cube3.GetComponent<TransformComponent>();
    cube3Transform.position = Vec3(-2.0f, 0.0f, 0.0f);
    cube3Transform.rotation = Vec3(0.0f, 0.785f, 0.0f); // 45 degrees
    
    // Create a fourth cube with emissive material
    Entity cube4 = scene.CreateEntity("Cube 4 (Emissive)");
    auto& cube4Renderer = cube4.AddComponent<MeshRendererComponent>();
    cube4Renderer.SetMesh(cubeMeshIndex, cubeMeshData.bounds);
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
        Vec3(0.3f, -1.0f, 0.5f), Vec3(1.0f, 0.95f, 0.9f), 0.8f));
    
    Entity pointLight1 = scene.CreateEntity("Point Light 1");
    auto& pl1Transform = pointLight1.AddComponent<TransformComponent>();
    pl1Transform.position = Vec3(3.0f, 2.0f, 2.0f);
    pointLight1.AddComponent<LightComponent>(LightComponent::CreatePoint(
        Vec3(1.0f, 0.3f, 0.3f), 5.0f, 10.0f)); // Red light
    
    Entity pointLight2 = scene.CreateEntity("Point Light 2");
    auto& pl2Transform = pointLight2.AddComponent<TransformComponent>();
    pl2Transform.position = Vec3(-3.0f, 2.0f, 2.0f);
    pointLight2.AddComponent<LightComponent>(LightComponent::CreatePoint(
        Vec3(0.3f, 0.3f, 1.0f), 5.0f, 10.0f)); // Blue light

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
        renderer.SetMesh(meshIndex, importedMesh.bounds);
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

    // ── Billboard System for Light Visualization (Resources) ─────────────────────────
    VulkanTexture lightBulbTexture;
    VulkanPipeline billboardPipeline;
    VkDescriptorSetLayout billboardDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool billboardDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> billboardDescriptorSets;
    
    // Load light bulb texture
    bool textureLoaded = false;
    try {
    std::filesystem::path lightBulbPath = ResolveEnginePath(std::filesystem::path("assets") / "textures" / "light_bulb.png");
    if (lightBulbTexture.LoadFromFile(vkdev.allocator, vkdev.device, vkdev.physicalDevice, 
                      vkdev.commandPool, vkdev.graphicsQueue, 
                      lightBulbPath.string())) {
            if (lightBulbTexture.imageView != VK_NULL_HANDLE && lightBulbTexture.sampler != VK_NULL_HANDLE) {
                std::cout << "Loaded light bulb texture" << std::endl;
                textureLoaded = true;
            } else {
                std::cerr << "Light bulb texture loaded but imageView or sampler is null" << std::endl;
            }
        } else {
            std::cerr << "Failed to load light bulb texture file" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to load light bulb texture: " << e.what() << std::endl;
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

        // Create descriptor pool: binding 0 and 2 are UNIFORM_BUFFER; binding 1 is UNIFORM_BUFFER_DYNAMIC
        VkDescriptorPoolSize poolSizes[2] = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = static_cast<uint32_t>(imageCount * 2); // camera + lighting per set
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        poolSizes[1].descriptorCount = static_cast<uint32_t>(imageCount * 1); // material per set

        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
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

        // Update descriptor sets with all three buffers
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

            VkWriteDescriptorSet writes[3] = {};
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
            
            vkUpdateDescriptorSets(vkdev.device, 3, writes, 0, nullptr);
        }
    };
    
    // ── Billboard Lambda Definitions (after uniformBuffers) ───────────────
    auto createBillboardDescriptors = [&]() {
        // Clean up existing resources
        if (!billboardDescriptorSets.empty()) {
            billboardDescriptorSets.clear();
        }
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
        
        // Create descriptor pool
        size_t imageCount = swap.framebuffers.size();
        std::vector<VkDescriptorPoolSize> poolSizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(imageCount)},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(imageCount)}
        };
        
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = static_cast<uint32_t>(imageCount);
        
        if (vkCreateDescriptorPool(vkdev.device, &poolInfo, nullptr, &billboardDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create billboard descriptor pool");
        }
        
        // Allocate descriptor sets
        std::vector<VkDescriptorSetLayout> layouts(imageCount, billboardDescriptorLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = billboardDescriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
        allocInfo.pSetLayouts = layouts.data();
        
        billboardDescriptorSets.resize(imageCount);
        if (vkAllocateDescriptorSets(vkdev.device, &allocInfo, billboardDescriptorSets.data()) != VK_SUCCESS) {
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
            imageInfo.imageView = lightBulbTexture.imageView;
            imageInfo.sampler = lightBulbTexture.sampler;
            
            std::vector<VkWriteDescriptorSet> descriptorWrites(2);
            
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = billboardDescriptorSets[i];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &bufferInfo;
            
            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = billboardDescriptorSets[i];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &imageInfo;
            
            vkUpdateDescriptorSets(vkdev.device, static_cast<uint32_t>(descriptorWrites.size()), 
                                  descriptorWrites.data(), 0, nullptr);
        }
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

    // ── ImGui Setup ──────────────────────────────
    // Create descriptor pool for ImGui
    VkDescriptorPool imguiPool;
    {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000;
        pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
        pool_info.pPoolSizes = pool_sizes;
        if (vkCreateDescriptorPool(vkdev.device, &pool_info, nullptr, &imguiPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui descriptor pool");
        }
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // Ensure ImGuizmo uses the same ImGui context
    ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

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

    // Debug toggle: show ImGuizmo grid
    static bool g_ShowGizmoGrid = false;

    std::vector<VkSemaphore> imageAvailable(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkSemaphore> renderFinished(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkFence> inFlight(MAX_FRAMES_IN_FLIGHT);

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

    vkWaitForFences(vkdev.device, 1, &inFlight[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult acq = vkAcquireNextImageKHR(vkdev.device, swap.swapchain, UINT64_MAX,
                  imageAvailable[currentFrame], VK_NULL_HANDLE, &imageIndex);
        // Timing
        float currentTime = static_cast<float>(glfwGetTime());
        float dt = currentTime - lastTime; lastTime = currentTime;

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        float newScale = ComputeUIScale(mainViewport);
        if (std::abs(newScale - g_UIScale) > 0.01f) {
            g_UIScale = newScale;
            io.FontGlobalScale = g_UIScale;
            ApplyHephaestusEditorStyle(io, g_UIScale);
        }
        
        // Setup ImGuizmo early to check if it's being used
        ImGuizmo::BeginFrame();
        ImGuizmo::SetOrthographic(false);
        
        // Handle mouse click for object selection (only when not using ImGui/ImGuizmo)
        static bool leftMouseWasPressed = false;
        bool leftMousePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        
        if (g_ProjectSelected && leftMousePressed && !leftMouseWasPressed &&
            !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() && !io.WantCaptureMouse &&
            !gizmoDragging && vulkanGizmo.GetHoveredAxis() == -1) {
            // Left mouse button just pressed and not over ImGui
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            
            // Create picking ray
            UniformBufferObject ubo = camera.makeUBO(swap.extent);
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
        
        // Input handling (only when not using gizmo)
        if (g_ProjectSelected && !ImGuizmo::IsUsing() && !gizmoDragging) {
            camera.handleMouse(window);
            camera.handleKeyboard(window, dt);
        }

        // ImGui UI
        std::optional<std::filesystem::path> pendingAssetImport;
        static bool s_Playing = false;
        static bool s_Paused = false;

        if (!g_ProjectSelected) {
            if (gizmoDragging) {
                gizmoDragging = false;
                gizmoActiveAxis = -1;
                vulkanGizmo.EndDrag();
            }
            vulkanGizmo.SetHoveredAxis(-1);

            std::optional<std::filesystem::path> projectChoice = DrawProjectBrowser();
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
            DrawWorldOutliner(scene);
            DrawDetailsPanel(scene);
            pendingAssetImport = DrawAssetBrowserPanel(assetBrowserStatus);
            DrawLogConsole(io, assetBrowserStatus);

            // Overlays should be rendered last so they appear above docked windows
            ToolbarActions toolbarActions = DrawEditorToolbar(s_Playing, s_Paused);
            if (toolbarActions.playClicked) {
                s_Playing = true;
                s_Paused = false;
                assetBrowserStatus = "Play mode toggle (placeholder).";
            }
            if (toolbarActions.pauseClicked && s_Playing) {
                s_Paused = !s_Paused;
                assetBrowserStatus = s_Paused ? "Paused simulation (placeholder)." : "Resumed simulation (placeholder).";
            }
            if (toolbarActions.stopClicked) {
                if (s_Playing || s_Paused) {
                    s_Playing = false;
                    s_Paused = false;
                    assetBrowserStatus = "Stopped play mode (placeholder).";
                }
            }
            if (toolbarActions.saveClicked) {
                assetBrowserStatus = "Save command triggered (placeholder).";
            }
            if (toolbarActions.buildClicked) {
                assetBrowserStatus = "Build command triggered (placeholder).";
            }

            DrawViewportOverlay(currentGizmoOperation, currentGizmoMode, g_ShowGizmoGrid, g_ShowEditorGizmo);
        }

        if (pendingAssetImport) {
            Entity imported = importModelFromFile(*pendingAssetImport);
            if (!imported) {
                std::cerr << "Model import failed for path: " << pendingAssetImport->string() << std::endl;
            }
        }
        
        // ImGuizmo keyboard shortcuts
        if (g_ProjectSelected) {
            if (ImGui::IsKeyPressed(ImGuiKey_T)) currentGizmoOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_Y) && !ImGui::GetIO().KeyCtrl) currentGizmoOperation = ImGuizmo::SCALE;
        }
        
        // Build view/proj for ImGuizmo and gizmo interactions
        UniformBufferObject ubo = camera.makeUBO(swap.extent);

        // Custom gizmo interaction handling
        if (g_ProjectSelected && g_ShowEditorGizmo) {
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
                    if (leftDown && hoveredAxis != -1 && !io.WantCaptureMouse) {
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
        
        // Render ImGui
        ImGui::Render();

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
            continue;
        } else if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
            std::cerr << "Failed to acquire swapchain image: " << acq << "\n";
            break;
        }

    // Reset the fence only when we are actually going to submit work for this frame.
    vkResetFences(vkdev.device, 1, &inFlight[currentFrame]);

        // Update UBO for this image
        {
            UniformBufferObject ubo = camera.makeUBO(swap.extent);
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
                    lightUBO.dirLightDir = light.direction;
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

    VkClearValue clear[2];
    clear[0].color = { {0.1f, 0.2f, 0.3f, 1.0f} };
    clear[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = swap.renderPass;
        rp.framebuffer = swap.framebuffers[imageIndex];
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = swap.extent;
    rp.clearValueCount = 2;
    rp.pClearValues = clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);

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
                matUBO.metallic = mat.metallic;
                matUBO.roughness = mat.roughness;
                matUBO.ao = mat.ao;
                matUBO.emissive = mat.emissive;
                matUBO.emissiveStrength = mat.emissiveStrength;
            } else {
                // Default white material
                matUBO.albedo = Vec3(1.0f, 1.0f, 1.0f);
                matUBO.metallic = 0.0f;
                matUBO.roughness = 0.5f;
                matUBO.ao = 1.0f;
                matUBO.emissive = Vec3(0.0f, 0.0f, 0.0f);
                matUBO.emissiveStrength = 0.0f;
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
        Entity selectedEntity = scene.GetSelectedEntity();
        if (g_ProjectSelected && g_ShowEditorGizmo && selectedEntity) {
            auto& transform = selectedEntity.GetComponent<TransformComponent>();
            Mat4 objectMatrix = transform.GetLocalMatrix();
            
            VulkanGizmo::Mode gizmoMode = VulkanGizmo::Mode::Translate;
            if (currentGizmoOperation == ImGuizmo::ROTATE) gizmoMode = VulkanGizmo::Mode::Rotate;
            else if (currentGizmoOperation == ImGuizmo::SCALE) gizmoMode = VulkanGizmo::Mode::Scale;

            VulkanGizmo::Space gizmoSpace = (currentGizmoMode == ImGuizmo::WORLD) ? VulkanGizmo::Space::World : VulkanGizmo::Space::Local;

            vulkanGizmo.Render(cmd, descriptorSets[imageIndex]);
        }

        // ── Render Light Billboards ───────────────────────────────
        if (billboardPipeline.pipeline != VK_NULL_HANDLE && !billboardDescriptorSets.empty()) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipeline.pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipeline.layout,
                                   0, 1, &billboardDescriptorSets[imageIndex], 0, nullptr);
            
            // Draw a billboard for each light entity
            auto lightView = scene.GetEntitiesWith<LightComponent>();
            for (auto entityHandle : lightView) {
                Entity entity(entityHandle, &scene);
                auto& light = entity.GetComponent<LightComponent>();
                
                // Only draw billboards for point and spot lights (not directional)
                if (light.type == LightType::Directional)
                    continue;
                
                // Get light position
                Vec3 lightPos(0.0f, 0.0f, 0.0f);
                if (entity.HasComponent<TransformComponent>()) {
                    auto& transform = entity.GetComponent<TransformComponent>();
                    lightPos = transform.position;
                }
                
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

        // Render ImGui draw data
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

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
        } else if (pres != VK_SUCCESS) {
            std::cerr << "Present failed: " << pres << "\n";
            break;
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(vkdev.device);

    // ── Cleanup ────────────────────────────────
    // Cleanup sync objects
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(vkdev.device, imageAvailable[i], nullptr);
        vkDestroySemaphore(vkdev.device, renderFinished[i], nullptr);
        vkDestroyFence(vkdev.device, inFlight[i], nullptr);
    }
    
    // Cleanup ImGui
    DestroyAssetBrowserIcons(vkdev);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(vkdev.device, imguiPool, nullptr);

    vulkanGizmo.Cleanup(vkdev.device, vkdev.allocator);
    for (auto& mesh : meshLibrary) {
        mesh.Destroy(vkdev.allocator);
    }
    destroyDescriptors();
    
    // Cleanup billboard resources
    billboardPipeline.Cleanup();
    lightBulbTexture.Destroy(vkdev.allocator, vkdev.device);
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
