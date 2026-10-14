/**
 * GameMain.cpp
 * 
 * Standalone game runtime entrypoint for Hephaestus Engine.
 * This is a minimal, editor-free runtime that only initializes what's needed
 * to run a packaged game:
 * - Window and platform
 * - Vulkan renderer
 * - Input handling
 * - Asset loading
 * - Lua scripting (if enabled)
 * - Physics
 * - Scene loading and game loop
 * 
 * This does NOT include:
 * - ImGui/Editor UI
 * - Ultralight HTML UI (editor-only by default)
 * - Asset browser, inspector, outliner
 * - Material editor
 * - Project manager
 * 
 * Build target: HephaestusGame.exe
 */

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <entt/entt.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "AppConfig.h"
#include "Core/RuntimeBootstrap.h"
#include "../Input/InputSystem.h"

#if HEPHAESTUS_RUNTIME_AUTOMATION
#include "PackagedAutomation.h"
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Engine core
#include "../Gfx/VulkanBuffer.h"
#include "../Gfx/VulkanDevice.h"
#include "../Gfx/EngineFrameRenderer.h"
#include "../Gfx/VulkanSwapchain.h"

// Physics
#include "../Physics/JoltPhysicsEngine.h"
#include "../Physics/PhysicsLayerConfig.h"
#include "../Physics/JoltPhysicsSystem.h"
#include "../Physics/CharacterMotor.h"
#include "../Physics/CharacterControllerSystem.h"
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

// Windows headers can define macros that collide with Jolt symbols.
#ifdef RayCastResult
#undef RayCastResult
#endif

// Scene
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/ActiveCameraSync.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneSerializer.h"
#include "../Scene/PrefabOverrideSystem.h"
#include "../Scene/Transform.h"

// Scripting
#include "../Audio/AudioEngine.h"
#if HEPHAESTUS_GAME_SCRIPTING
#include "../Scripting/LuaScriptEngine.h"
#include "../Scripting/ScriptEngine.h"
#endif

// Runtime UI
#include "../UI/HephaestusUI/UIContext.h"
#include "../UI/HephaestusUI/UIRenderer.h"
#include "../UI/HephaestusUI/UISystem.h"

// Assets
#include "../Assets/MeshAsset.h"
#include "../Assets/ModelImporter.h"
#include "../Assets/SkeletalModelImporter.h"
#include "../Assets/CharacterRuntime.h"
#include "../Animation/CharacterPlayback.h"
#include "../Gfx/Vertex.h"

// stb_image is already included via ModelImporter or elsewhere
// Do not re-define STB_IMAGE_IMPLEMENTATION here

namespace fs = std::filesystem;
using namespace Hephaestus::Runtime;
namespace HUI = Hephaestus::HephaestusUI;

// Camera for Game Runtime
// ──────────────────────────────────────────────────────────────────────────────

struct GameCamera {
    Vec3 position = Vec3(0.0f, 2.0f, 5.0f);
    float yawDeg = -90.0f;
    float pitchDeg = 0.0f;
    float fovDeg = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    Hephaestus::RenderCamera ToRenderCamera() const {
        Hephaestus::RenderCamera camera{};
        camera.positionX = position.x;
        camera.positionY = position.y;
        camera.positionZ = position.z;
        camera.yawDeg = yawDeg;
        camera.pitchDeg = pitchDeg;
        camera.fovDeg = fovDeg;
        camera.nearPlane = nearPlane;
        camera.farPlane = farPlane;
        camera.projectionMode = Hephaestus::ProjectionMode::Perspective;
        return camera;
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Configuration
// ──────────────────────────────────────────────────────────────────────────────

struct GameConfig {
    std::string windowTitle = "Hephaestus Game";
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;
    bool vsync = true;
    std::string scenePath;
    std::string projectName;
    std::string inputProfile = "assets/input/Default.hpinput";
#if HEPHAESTUS_RUNTIME_AUTOMATION
    bool automationEnabled = false;
    int automationPort = 0;
#endif
};

// ──────────────────────────────────────────────────────────────────────────────
// Globals
// ──────────────────────────────────────────────────────────────────────────────

static bool g_FramebufferResized = false;
static fs::path g_RuntimeRoot;
static fs::path g_ProjectRoot;   // Detected from scene file / .heproject
static fs::path g_ProjectAssets; // g_ProjectRoot / "assets"
static GameConfig g_Config;
static GameCamera* g_GameCameraPtr = nullptr; // For Lua camera binding
static Scene* g_GameScenePtr = nullptr;

static void ApplyActiveCameraView(const Hephaestus::ActiveCameraView& view, GameCamera& camera)
{
    constexpr float kRadToDeg = 180.0f / 3.14159265f;
    camera.position = view.position;
    camera.yawDeg = view.yawRadians * kRadToDeg;
    camera.pitchDeg = view.pitchRadians * kRadToDeg;
    camera.fovDeg = view.fovDegrees;
    camera.nearPlane = view.nearPlane;
    camera.farPlane = view.farPlane;
}

static bool SyncGameCameraFromActiveComponent(GameCamera& camera)
{
    if (!g_GameScenePtr) {
        return false;
    }
    const std::optional<Hephaestus::ActiveCameraView> view =
        Hephaestus::TryReadActiveCameraView(*g_GameScenePtr);
    if (!view) {
        return false;
    }
    ApplyActiveCameraView(*view, camera);
    return true;
}
static double g_ScrollDeltaX = 0.0;
static double g_ScrollDeltaY = 0.0;

// ──────────────────────────────────────────────────────────────────────────────
// Mesh Library for Runtime
// ──────────────────────────────────────────────────────────────────────────────

static size_t LoadMeshToGPU(const MeshData& meshData, const std::string& name, 
                            VmaAllocator allocator, VkDevice device,
                            VkCommandPool cmdPool, VkQueue queue,
                            Hephaestus::EngineFrameRenderer& frameRenderer) {
    if (meshData.vertices.empty() || meshData.indices.empty()) {
        return SIZE_MAX;
    }
    
    MeshGPU mesh;
    mesh.name = name;
    mesh.sourceKey = name;
    mesh.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(meshData.indices.size());
    mesh.bounds = meshData.bounds;
    mesh.submeshes = meshData.submeshes;
    
    // Create vertex buffer
    VkDeviceSize vbSize = meshData.vertices.size() * sizeof(Vertex);
    mesh.vertexBuffer.Create(allocator, vbSize, 
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    
    // Create index buffer
    VkDeviceSize ibSize = meshData.indices.size() * sizeof(uint32_t);
    mesh.indexBuffer.Create(allocator, ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    
    // Stage and copy vertex data
    VulkanBuffer stagingVB;
    stagingVB.Create(allocator, vbSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* vbData;
    vmaMapMemory(allocator, stagingVB.allocation, &vbData);
    memcpy(vbData, meshData.vertices.data(), vbSize);
    vmaUnmapMemory(allocator, stagingVB.allocation);
    
    // Stage and copy index data
    VulkanBuffer stagingIB;
    stagingIB.Create(allocator, ibSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* ibData;
    vmaMapMemory(allocator, stagingIB.allocation, &ibData);
    memcpy(ibData, meshData.indices.data(), ibSize);
    vmaUnmapMemory(allocator, stagingIB.allocation);
    
    // Copy buffers
    VkCommandBufferAllocateInfo cmdAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAlloc.commandPool = cmdPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);
    
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    VkBufferCopy copyRegion{};
    copyRegion.size = vbSize;
    vkCmdCopyBuffer(cmd, stagingVB.buffer, mesh.vertexBuffer.buffer, 1, &copyRegion);
    
    copyRegion.size = ibSize;
    vkCmdCopyBuffer(cmd, stagingIB.buffer, mesh.indexBuffer.buffer, 1, &copyRegion);
    
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
    stagingVB.Destroy(allocator);
    stagingIB.Destroy(allocator);
    
    return frameRenderer.AddMesh(std::move(mesh));
}

// ──────────────────────────────────────────────────────────────────────────────
// Callbacks
// ──────────────────────────────────────────────────────────────────────────────

static void FramebufferResizeCallback(GLFWwindow* /*window*/, int /*width*/, int /*height*/) {
    g_FramebufferResized = true;
}

static void ScrollCallback(GLFWwindow* /*window*/, double xoffset, double yoffset) {
    g_ScrollDeltaX += xoffset;
    g_ScrollDeltaY += yoffset;
}

static fs::path DetectProjectRoot(const fs::path& startPath) {
    std::error_code ec;

    // In an exported game the runtime root IS the project root.
    // Detect this by the presence of export_manifest.txt or game.ini next to
    // the executable and stop immediately — never walk outside the dist folder.
    if (fs::exists(g_RuntimeRoot / "export_manifest.txt", ec) ||
        fs::exists(g_RuntimeRoot / "game.ini", ec)) {
        return g_RuntimeRoot;
    }

    fs::path dir = startPath;
    if (fs::is_regular_file(dir, ec)) {
        dir = dir.parent_path();
    }
    for (int depth = 0; depth < 15 && !dir.empty(); ++depth) {
        for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (entry.is_regular_file() && entry.path().extension() == ".heproject") {
                return dir;
            }
        }
        fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

static void SetProjectRootFromScene(const fs::path& scenePath) {
    g_ProjectRoot = DetectProjectRoot(scenePath);
    if (!g_ProjectRoot.empty()) {
        g_ProjectAssets = (g_ProjectRoot / "assets").lexically_normal();
        HP_LOG(Info, "Detected project root: " + g_ProjectRoot.string());
        HP_LOG(Info, "Project assets dir: " + g_ProjectAssets.string());
    } else {
        HP_LOG(Warning, "Could not detect project root from scene path: " + scenePath.string());
    }
}

static std::string DiscoverDefaultScene() {
    fs::path scenesDir = g_RuntimeRoot / "assets" / "scenes";
    
    if (!fs::exists(scenesDir)) {
        HP_LOG(Warning, "No assets/scenes directory found");
        return "";
    }

    // Look for .hpscene files
    for (const auto& entry : fs::directory_iterator(scenesDir)) {
        if (entry.path().extension() == ".hpscene") {
            HP_LOG(Info, "Auto-discovered scene: " + entry.path().string());
            return entry.path().string();
        }
    }

    HP_LOG(Warning, "No .hpscene files found in assets/scenes/");
    return "";
}

// ──────────────────────────────────────────────────────────────────────────────
// Vulkan Debug Callback
// ──────────────────────────────────────────────────────────────────────────────

#ifdef _DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void* /*userData*/) {
    
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        HP_LOG(Warning, std::string("[Vulkan] ") + (pData ? pData->pMessage : ""));
    }
    return VK_FALSE;
}
#endif

// ──────────────────────────────────────────────────────────────────────────────
// Command Line Parsing
// ──────────────────────────────────────────────────────────────────────────────

static bool ParseCommandLine(int argc, char* argv[], GameConfig& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--scene" || arg == "-s") {
            if (i + 1 < argc) {
                config.scenePath = argv[++i];
            }
        } else if (arg == "--width" || arg == "-w") {
            if (i + 1 < argc) {
                config.windowWidth = std::stoi(argv[++i]);
            }
        } else if (arg == "--height" || arg == "-h") {
            if (i + 1 < argc) {
                config.windowHeight = std::stoi(argv[++i]);
            }
        } else if (arg == "--fullscreen" || arg == "-f") {
            config.fullscreen = true;
        } else if (arg == "--windowed") {
            config.fullscreen = false;
        } else if (arg == "--title" || arg == "-t") {
            if (i + 1 < argc) {
                config.windowTitle = argv[++i];
            }
        } else if (arg == "--help") {
            std::cout << "Hephaestus Game Runtime\n";
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --scene, -s <path>     Scene file to load\n";
            std::cout << "  --width, -w <pixels>   Window width (default: 1280)\n";
            std::cout << "  --height, -h <pixels>  Window height (default: 720)\n";
            std::cout << "  --fullscreen, -f       Start in fullscreen mode\n";
            std::cout << "  --windowed             Start in windowed mode\n";
            std::cout << "  --title, -t <title>    Window title\n";
            std::cout << "  --help                 Show this help message\n";
            return false;
        }
    }
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Configuration File Loading
// ──────────────────────────────────────────────────────────────────────────────

static void LoadConfigFile(GameConfig& config) {
    fs::path configPath = g_RuntimeRoot / "game.ini";
    
    if (!fs::exists(configPath)) {
        HP_LOG(Debug, "No game.ini found, using defaults");
        return;
    }

    std::ifstream file(configPath);
    if (!file.is_open()) {
        HP_LOG(Warning, "Could not open game.ini");
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim whitespace
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(key);
        trim(value);

        if (key == "window_title") {
            config.windowTitle = value;
        } else if (key == "window_width") {
            config.windowWidth = std::stoi(value);
        } else if (key == "window_height") {
            config.windowHeight = std::stoi(value);
        } else if (key == "fullscreen") {
            config.fullscreen = (value == "true" || value == "1");
        } else if (key == "vsync") {
            config.vsync = (value == "true" || value == "1");
        } else if (key == "scene") {
            config.scenePath = value;
        } else if (key == "project_name") {
            config.projectName = value;
        } else if (key == "input_profile") {
            config.inputProfile = value;
        }
#if HEPHAESTUS_RUNTIME_AUTOMATION
        else if (key == "enabled" && value == "true" || key == "enabled" && value == "1") {
            config.automationEnabled = true;
        } else if (key == "port") {
            config.automationPort = std::stoi(value);
        }
#endif
    }

    HP_LOG(Info, "Loaded configuration from game.ini");
}

static void UpdateAudioSources(Scene& scene, AudioEngine& audioEngine) {
    auto audioView = scene.GetEntitiesWith<AudioSourceComponent>();
    for (auto entityHandle : audioView) {
        auto& audio = audioView.get<AudioSourceComponent>(entityHandle);

        if (!audio.enabled || audio.clipPath.empty()) {
            if (audio.runtimeHandle != 0) {
                audioEngine.Stop(audio.runtimeHandle);
                audio.runtimeHandle = 0;
            }
            audio.runtimeStarted = false;
            audio.runtimeLastClip.clear();
            continue;
        }

        if (audio.runtimeHandle != 0) {
            if (audio.runtimeLastClip != audio.clipPath || !audioEngine.IsPlaying(audio.runtimeHandle)) {
                audioEngine.Stop(audio.runtimeHandle);
                audio.runtimeHandle = 0;
            } else {
                audioEngine.SetVolume(audio.runtimeHandle, audio.volume);
            }
        }

        if (audio.runtimeHandle == 0 && audio.playOnStart && !audio.runtimeStarted) {
            audio.runtimeHandle = audioEngine.Play(audio.clipPath, audio.loop, audio.volume);
            audio.runtimeStarted = audio.runtimeHandle != 0;
            audio.runtimeLastClip = audio.clipPath;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Main Entry Point
// ──────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // ── Bootstrap ─────────────────────────────────
    BootstrapConfig bootstrapConfig;
    bootstrapConfig.applicationName = "HephaestusGame";
    bootstrapConfig.enableCrashHandler = true;
    bootstrapConfig.enableFileLogging = true;
    bootstrapConfig.validateDependencies = true;

    if (!Bootstrap::Initialize(bootstrapConfig)) {
        Bootstrap::ShowFatalError("Startup Error", 
            "Failed to initialize runtime bootstrap.\n"
            "The game cannot start.");
        return -1;
    }

    g_RuntimeRoot = Bootstrap::GetRuntimeRoot();
    HP_LOG(Info, "Runtime root: " + g_RuntimeRoot.string());

    // Change working directory to runtime root so shader paths work correctly
    std::error_code ec;
    fs::current_path(g_RuntimeRoot, ec);
    if (ec) {
        HP_LOG(Warning, "Failed to change working directory to runtime root: " + ec.message());
    } else {
        HP_LOG(Info, "Working directory set to: " + fs::current_path().string());
    }

    // ── Validate Dependencies ─────────────────────
    Bootstrap::BeginStage(InitStage::Bootstrap);
    
#if HEPHAESTUS_GAME_SCRIPTING
    std::vector<std::string> requiredDlls = {
        fs::exists(g_RuntimeRoot / "lua54.dll") ? "lua54.dll" : "lua.dll"
    };
#else
    std::vector<std::string> requiredDlls = {};
#endif
    // Note: Vulkan loader is typically system-installed
    std::vector<std::string> optionalDlls = {"vulkan-1.dll"};

    auto depCheck = Bootstrap::ValidateDependencies(requiredDlls, optionalDlls);
    if (!depCheck.success) {
        Bootstrap::ShowFatalError("Missing Dependencies", depCheck.errorMessage);
        Bootstrap::Shutdown();
        return -1;
    }
    
    Bootstrap::EndStage(InitStage::Bootstrap, true);

    // ── Load Configuration ────────────────────────
    LoadConfigFile(g_Config);
    
    // Command line overrides config file
    if (!ParseCommandLine(argc, argv, g_Config)) {
        Bootstrap::Shutdown();
        return 0; // --help was shown
    }

    // Auto-discover scene if not specified
    if (g_Config.scenePath.empty()) {
        g_Config.scenePath = DiscoverDefaultScene();
    }

    if (g_Config.scenePath.empty()) {
        Bootstrap::ShowFatalError("No Scene", 
            "No scene file was specified and no scenes were found.\n"
            "Please ensure your game package contains a scene file in assets/scenes/");
        Bootstrap::Shutdown();
        return -1;
    }

    // ── Platform/Window ───────────────────────────
    Bootstrap::BeginStage(InitStage::Platform);
    
    if (!glfwInit()) {
        Bootstrap::ShowFatalError("Platform Error", "Failed to initialize GLFW");
        Bootstrap::Shutdown();
        return -1;
    }
    
    Bootstrap::EndStage(InitStage::Platform, true);

    Bootstrap::BeginStage(InitStage::Window);
    
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    GLFWmonitor* monitor = nullptr;
    if (g_Config.fullscreen) {
        monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        g_Config.windowWidth = mode->width;
        g_Config.windowHeight = mode->height;
    }

    GLFWwindow* window = glfwCreateWindow(
        g_Config.windowWidth, g_Config.windowHeight,
        g_Config.windowTitle.c_str(), monitor, nullptr);
    
    if (!window) {
        Bootstrap::ShowFatalError("Window Error", "Failed to create game window");
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }

    glfwSetFramebufferSizeCallback(window, FramebufferResizeCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    
    Bootstrap::EndStage(InitStage::Window, true);

    // ── Vulkan Instance ───────────────────────────
    Bootstrap::BeginStage(InitStage::Renderer);
    
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = g_Config.windowTitle.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(HEPHAESTUS_VERSION_MAJOR, HEPHAESTUS_VERSION_MINOR, HEPHAESTUS_VERSION_PATCH);
    appInfo.pEngineName = HEPHAESTUS_APP_NAME;
    appInfo.engineVersion = VK_MAKE_VERSION(HEPHAESTUS_VERSION_MAJOR, HEPHAESTUS_VERSION_MINOR, HEPHAESTUS_VERSION_PATCH);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t extCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + extCount);

#ifdef _DEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

#ifdef __APPLE__
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo instanceCI{};
    instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCI.pApplicationInfo = &appInfo;
    instanceCI.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceCI.ppEnabledExtensionNames = extensions.data();

#ifdef __APPLE__
    instanceCI.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

#ifdef _DEBUG
    const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};
    instanceCI.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    instanceCI.ppEnabledLayerNames = validationLayers.data();
#endif

    VkInstance instance{};
    VkResult result = vkCreateInstance(&instanceCI, nullptr, &instance);
    if (result != VK_SUCCESS) {
        Bootstrap::ShowFatalError("Renderer Error", 
            "Failed to create Vulkan instance.\n"
            "Please ensure your graphics drivers support Vulkan.");
        glfwDestroyWindow(window);
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }

    // Vulkan device
    VulkanDevice vkdev;
    try {
        vkdev.Init(window, instance);
    } catch (const std::exception& e) {
        Bootstrap::ShowFatalError("Renderer Error", 
            std::string("Failed to initialize Vulkan device:\n") + e.what());
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }

    // Swapchain
    VulkanSwapchain swap;
    try {
        swap.Init(vkdev.physicalDevice, vkdev.device, vkdev.surface, window, vkdev.allocator,
                  g_Config.vsync);
    } catch (const std::exception& e) {
        Bootstrap::ShowFatalError("Renderer Error",
            std::string("Failed to create swapchain:\n") + e.what());
        vkdev.Cleanup();
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }

    Hephaestus::EngineFrameRenderer frameRenderer;
    if (!frameRenderer.Init(&vkdev, &swap)) {
        Bootstrap::ShowFatalError("Renderer Error", "Failed to initialize the frame renderer.");
        frameRenderer.Shutdown();
        swap.Cleanup();
        vkdev.Cleanup();
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }
    HP_LOG(Info, "Swapchain extent: " + std::to_string(swap.extent.width) + "x" + std::to_string(swap.extent.height));
    
    Bootstrap::EndStage(InitStage::Renderer, true);

    // ── Scene Loading (early - needed by physics and scripting) ──────
    Bootstrap::BeginStage(InitStage::SceneLoad);
    
    Scene scene;
    
    // Resolve scene path (relative to runtime root if not absolute)
    fs::path scenePath = g_Config.scenePath;
    if (scenePath.is_relative()) {
        scenePath = g_RuntimeRoot / scenePath;
    }

    HP_LOG(Info, "Loading scene: " + scenePath.string());

    // Resolve portable prefab paths (assets/prefabs/...) against the project root
    // before scene deserialize, otherwise instances become Missing Prefab stubs.
    SetProjectRootFromScene(scenePath);
    PrefabOverrideSystem::SetProjectRoot(!g_ProjectRoot.empty() ? g_ProjectRoot : g_RuntimeRoot);
    frameRenderer.SetProjectRoot((!g_ProjectRoot.empty() ? g_ProjectRoot : g_RuntimeRoot).string());

    std::string loadError;
    if (!SceneSerializer::Deserialize(scene, scenePath.string(), loadError)) {
        Bootstrap::ShowFatalError("Scene Load Error",
            "Failed to load scene:\n" + scenePath.string() + "\n\nError: " + loadError);
        // Cleanup and exit
        frameRenderer.Shutdown();
        swap.Cleanup();
        vkdev.Cleanup();
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        Bootstrap::Shutdown();
        return -1;
    }

    HP_LOG(Info, "Scene loaded successfully: " + scene.GetName());
    
    // Detect project root from the scene file so mesh/asset paths resolve
    // correctly even when the game runs from a different directory.
    if (g_ProjectRoot.empty()) {
        SetProjectRootFromScene(scenePath);
        frameRenderer.SetProjectRoot((!g_ProjectRoot.empty() ? g_ProjectRoot : g_RuntimeRoot).string());
    }
    
    Bootstrap::EndStage(InitStage::SceneLoad, true);

    Hephaestus::InputSystem inputSystem;
    if (g_Config.inputProfile.empty() || g_Config.inputProfile == "Default") {
        g_Config.inputProfile = "assets/input/Default.hpinput";
    }
    fs::path inputProfilePath = g_Config.inputProfile;
    if (inputProfilePath.is_relative()) {
        inputProfilePath = (!g_ProjectRoot.empty() ? g_ProjectRoot : g_RuntimeRoot) /
                           inputProfilePath;
    }
    std::string inputError;
    if (inputSystem.LoadMap(inputProfilePath, inputError)) {
        HP_LOG(Info, "Loaded input profile: " + inputProfilePath.string());
    } else {
        inputSystem.SetMap(Hephaestus::InputMap::CreateDefault());
        HP_LOG(Warning, inputError + "; using built-in default input map");
    }

    // ── Physics ───────────────────────────────────
    Bootstrap::BeginStage(InitStage::Physics);
    
    Physics::JoltPhysicsEngine joltPhysicsEngine;
    joltPhysicsEngine.Initialize();
    {
        Physics::PhysicsLayerConfig layerConfig;
        const fs::path layerRoot = !g_ProjectRoot.empty() ? g_ProjectRoot : g_RuntimeRoot;
        Physics::PhysicsLayerConfig::LoadFromFile(
            Physics::PhysicsLayerConfig::ProjectFile(layerRoot), layerConfig);
        joltPhysicsEngine.BeginPlayWorld(layerConfig);
    }
    
    Physics::JoltPhysicsSystem physicsSystem(joltPhysicsEngine);
    Physics::CharacterControllerSystem characterControllers(joltPhysicsEngine, physicsSystem);
    physicsSystem.SetCharacterController(&characterControllers);
    physicsSystem.SetProjectRoot(!g_ProjectRoot.empty() ? g_ProjectRoot : g_RuntimeRoot);
    
    Bootstrap::EndStage(InitStage::Physics, true);

    AudioEngine audioEngine;
    audioEngine.Initialize();
    audioEngine.SetProjectRoot(!g_ProjectRoot.empty() ? g_ProjectRoot : g_RuntimeRoot);

    // ── Runtime UI ────────────────────────────────
    HUI::UISystem uiSystem;
    HUI::UIContext uiContext;
    HUI::UIRenderer uiRenderer;
    uiContext.SetViewportSize({static_cast<float>(swap.extent.width), static_cast<float>(swap.extent.height)});

    // ── Scripting ─────────────────────────────────
#if HEPHAESTUS_GAME_SCRIPTING
    Bootstrap::BeginStage(InitStage::Scripting);
    
    LuaScriptEngine luaEngine;
    try {
        luaEngine.Initialize(&scene, window);
        // Set project root so script search paths resolve correctly on any device
        luaEngine.SetProjectRoot(!g_ProjectRoot.empty() ? g_ProjectRoot : g_RuntimeRoot);
        luaEngine.SetFrameRenderer(&frameRenderer);
        luaEngine.SetInputSystem(&inputSystem);
        luaEngine.SetAudioEngine(&audioEngine);
        luaEngine.SetUIBindings(&uiSystem, &uiContext, &uiRenderer);
        HP_LOG(Info, "Lua scripting engine initialized");
    } catch (const std::exception& e) {
        HP_LOG(Error, std::string("Failed to initialize Lua: ") + e.what());
        // Continue without scripting
    }

    // Provide Jolt physics callbacks to Lua scripts (required for fp-controller)
    PhysicsBinding physicsBinding{};
    physicsBinding.hasBody = [&scene, &joltPhysicsEngine, &physicsSystem](uint32_t entityId) -> bool {
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!scene.GetRegistry().valid(handle)) {
            return false;
        }
        return !physicsSystem.GetBodyID(handle).IsInvalid();
    };

    physicsBinding.getLinearVelocity = [&scene, &joltPhysicsEngine, &physicsSystem](
                                           uint32_t entityId, Vec3& outVelocity) -> bool {
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!scene.GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = physicsSystem.GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }
        JPH::Vec3 vel = joltPhysicsEngine.GetBodyInterface().GetLinearVelocity(bodyId);
        outVelocity = Vec3(vel.GetX(), vel.GetY(), vel.GetZ());
        return true;
    };

    physicsBinding.setLinearVelocity = [&scene, &joltPhysicsEngine, &physicsSystem](
                                           uint32_t entityId, const Vec3& velocity, bool activate) -> bool {
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!scene.GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = physicsSystem.GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }
        joltPhysicsEngine.GetBodyInterface().SetLinearVelocity(
            bodyId, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        if (activate) {
            joltPhysicsEngine.GetBodyInterface().ActivateBody(bodyId);
        }
        return true;
    };

    physicsBinding.setAngularVelocity = [&scene, &joltPhysicsEngine, &physicsSystem](
                                            uint32_t entityId, const Vec3& velocity, bool activate) -> bool {
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!scene.GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = physicsSystem.GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }
        joltPhysicsEngine.GetBodyInterface().SetAngularVelocity(
            bodyId, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        if (activate) {
            joltPhysicsEngine.GetBodyInterface().ActivateBody(bodyId);
        }
        return true;
    };

    physicsBinding.setRotation = [&scene, &joltPhysicsEngine, &physicsSystem](
                                    uint32_t entityId, const Vec3& rotationEuler, bool activate) -> bool {
        entt::entity handle = static_cast<entt::entity>(entityId);
        if (!scene.GetRegistry().valid(handle)) {
            return false;
        }
        JPH::BodyID bodyId = physicsSystem.GetBodyID(handle);
        if (bodyId.IsInvalid()) {
            return false;
        }

        JPH::Quat qx = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), rotationEuler.x);
        JPH::Quat qy = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), rotationEuler.y);
        JPH::Quat qz = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), rotationEuler.z);
        JPH::Quat rot = qx * qy * qz;

        JPH::BodyInterface& bi = joltPhysicsEngine.GetBodyInterface();
        JPH::RVec3 pos = bi.GetPosition(bodyId);
        bi.SetPositionAndRotation(bodyId, pos, rot,
                                  activate ? JPH::EActivation::Activate
                                           : JPH::EActivation::DontActivate);
        return true;
    };

    physicsBinding.raycast = [&physicsSystem, &joltPhysicsEngine](
                                uint32_t entityId, const Vec3& origin, const Vec3& direction, float maxDistance,
                                PhysicsRaycastHit& outHit, const char* layerName, std::string& outError,
                                bool ignoreSensors) -> bool {
        const std::uint16_t* layer = nullptr;
        std::uint16_t resolved = 0;
        if (layerName && layerName[0] != '\0') {
            if (!joltPhysicsEngine.TryResolveLayerToken(layerName, resolved, outError)) {
                return false;
            }
            layer = &resolved;
        }
        Physics::RaycastHit hit{};
        const bool ok = physicsSystem.Raycast(
            static_cast<entt::entity>(entityId), origin, direction, maxDistance, hit, layer, ignoreSensors);
        if (!ok) {
            return false;
        }

        outHit.hit = hit.hit;
        outHit.distance = hit.distance;
        outHit.point = hit.point;
        outHit.normal = hit.normal;
        outHit.entityId = (hit.entity != entt::null)
            ? static_cast<uint32_t>(hit.entity)
            : std::numeric_limits<uint32_t>::max();
        return true;
    };

    physicsBinding.isGrounded = [&joltPhysicsEngine, &physicsSystem, &characterControllers](uint32_t entityId) -> bool {
        const entt::entity entity = static_cast<entt::entity>(entityId);
        Physics::CharacterControllerState state;
        if (characterControllers.GetState(entity, state)) {
            return state.grounded;
        }
        return Physics::CharacterMotor::IsGrounded(joltPhysicsEngine, physicsSystem, entity);
    };

    physicsBinding.move = [&joltPhysicsEngine, &physicsSystem, &characterControllers](uint32_t entityId, float wishX, float wishZ, float speed) -> bool {
        const entt::entity entity = static_cast<entt::entity>(entityId);
        if (characterControllers.HasRuntime(entity)) {
            return characterControllers.SetLegacyMove(entity, wishX, wishZ, speed);
        }
        return Physics::CharacterMotor::Move(joltPhysicsEngine, physicsSystem, entity, wishX, wishZ, speed);
    };

    physicsBinding.jump = [&joltPhysicsEngine, &physicsSystem, &characterControllers](uint32_t entityId, float impulse) -> bool {
        const entt::entity entity = static_cast<entt::entity>(entityId);
        if (characterControllers.HasRuntime(entity)) {
            characterControllers.JumpWithSpeed(entity, impulse);
            return true;
        }
        return Physics::CharacterMotor::Jump(joltPhysicsEngine, physicsSystem, entity, impulse);
    };

    luaEngine.SetPhysicsBinding(std::move(physicsBinding));

    CharacterBinding characterBinding;
    characterBinding.move = [&characterControllers](uint32_t entityId, float worldX, float worldZ) {
        const entt::entity entity = static_cast<entt::entity>(entityId);
        if (!characterControllers.HasRuntime(entity)) return false;
        characterControllers.SetMoveInput(entity, worldX, worldZ);
        return true;
    };
    characterBinding.jump = [&characterControllers](uint32_t entityId) {
        const entt::entity entity = static_cast<entt::entity>(entityId);
        if (!characterControllers.HasRuntime(entity)) return false;
        characterControllers.Jump(entity);
        return true;
    };
    characterBinding.setCrouched = [&characterControllers](uint32_t entityId, bool crouched) {
        const entt::entity entity = static_cast<entt::entity>(entityId);
        if (!characterControllers.HasRuntime(entity)) return false;
        characterControllers.SetCrouched(entity, crouched);
        return true;
    };
    characterBinding.setSpeedMultiplier = [&characterControllers](uint32_t entityId, float multiplier) {
        const entt::entity entity = static_cast<entt::entity>(entityId);
        if (!characterControllers.HasRuntime(entity)) return false;
        characterControllers.SetSpeedMultiplier(entity, multiplier);
        return true;
    };
    characterBinding.getSpeedMultiplier = [&characterControllers](uint32_t entityId, float& outMultiplier) {
        outMultiplier = characterControllers.GetSpeedMultiplier(static_cast<entt::entity>(entityId));
        return true;
    };
    characterBinding.isCrouched = [&characterControllers](uint32_t entityId) {
        Physics::CharacterControllerState state;
        return characterControllers.GetState(static_cast<entt::entity>(entityId), state) && state.crouched;
    };
    characterBinding.isStandBlocked = [&characterControllers](uint32_t entityId) {
        Physics::CharacterControllerState state;
        return characterControllers.GetState(static_cast<entt::entity>(entityId), state) && state.standBlocked;
    };
    characterBinding.isGrounded = [&characterControllers](uint32_t entityId) {
        Physics::CharacterControllerState state;
        return characterControllers.GetState(static_cast<entt::entity>(entityId), state) && state.grounded;
    };
    characterBinding.getVelocity = [&characterControllers](uint32_t entityId, Vec3& out) {
        Physics::CharacterControllerState state;
        if (!characterControllers.GetState(static_cast<entt::entity>(entityId), state)) return false;
        out = state.velocity;
        return true;
    };
    characterBinding.getGroundNormal = [&characterControllers](uint32_t entityId, Vec3& out) {
        Physics::CharacterControllerState state;
        if (!characterControllers.GetState(static_cast<entt::entity>(entityId), state)) return false;
        out = state.groundNormal;
        return true;
    };
    characterBinding.getGroundVelocity = [&characterControllers](uint32_t entityId, Vec3& out) {
        Physics::CharacterControllerState state;
        if (!characterControllers.GetState(static_cast<entt::entity>(entityId), state)) return false;
        out = state.groundVelocity;
        return true;
    };
    characterBinding.getGroundState = [&characterControllers](uint32_t entityId, std::string& out) {
        Physics::CharacterControllerState state;
        if (!characterControllers.GetState(static_cast<entt::entity>(entityId), state)) return false;
        out = Physics::CharacterGroundStateName(state.groundState);
        return true;
    };
    characterBinding.teleport = [&characterControllers](uint32_t entityId, float x, float y, float z) {
        const entt::entity entity = static_cast<entt::entity>(entityId);
        if (!characterControllers.HasRuntime(entity)) return false;
        characterControllers.Teleport(entity, Vec3(x, y, z));
        return true;
    };
    characterBinding.setFacingYaw = [&characterControllers](uint32_t entityId, float radians) {
        const entt::entity entity = static_cast<entt::entity>(entityId);
        if (!characterControllers.HasRuntime(entity)) return false;
        characterControllers.SetFacingYaw(entity, radians);
        return true;
    };
    luaEngine.SetCharacterBinding(std::move(characterBinding));
    
    Bootstrap::EndStage(InitStage::Scripting, true);
#endif

    // ── Initialize Scene Systems ──────────────────
    // GameCamera will be set up later, but we need the pointer for binding
    GameCamera gameCamera;
    g_GameCameraPtr = &gameCamera;
    g_GameScenePtr = &scene;

#if HEPHAESTUS_GAME_SCRIPTING
    // Set up camera binding so Lua scripts can control the camera
    CameraBinding cameraBinding{};
    cameraBinding.setActive = [](bool /*active*/) {
        // In game runtime, camera is always active
    };
    cameraBinding.isActive = []() {
        return true;
    };
    cameraBinding.setPosition = [](const Vec3& pos) {
        if (g_GameScenePtr &&
            Hephaestus::WriteActiveCameraLocalPosition(*g_GameScenePtr, pos) &&
            g_GameCameraPtr &&
            SyncGameCameraFromActiveComponent(*g_GameCameraPtr)) {
            return;
        }
        if (g_GameCameraPtr) {
            g_GameCameraPtr->position = pos;
        }
    };
    cameraBinding.getPosition = []() {
        if (g_GameCameraPtr) {
            return g_GameCameraPtr->position;
        }
        return Vec3(0.0f, 0.0f, 0.0f);
    };
    cameraBinding.setRotation = [](const Vec3& rot) {
        if (g_GameScenePtr &&
            Hephaestus::WriteActiveCameraLocalRotation(*g_GameScenePtr, rot) &&
            g_GameCameraPtr &&
            SyncGameCameraFromActiveComponent(*g_GameCameraPtr)) {
            return;
        }
        if (g_GameCameraPtr) {
            // rot.x = pitch (radians), rot.y = yaw (radians)
            g_GameCameraPtr->pitchDeg = rot.x * 180.0f / 3.14159265f;
            g_GameCameraPtr->yawDeg = rot.y * 180.0f / 3.14159265f;
        }
    };
    cameraBinding.getRotation = []() {
        if (g_GameCameraPtr) {
            return Vec3(
                g_GameCameraPtr->pitchDeg * 3.14159265f / 180.0f,
                g_GameCameraPtr->yawDeg * 3.14159265f / 180.0f,
                0.0f
            );
        }
        return Vec3(0.0f, 0.0f, 0.0f);
    };
    cameraBinding.translate = [](const Vec3& delta) {
        if (g_GameScenePtr &&
            Hephaestus::TranslateActiveCameraLocal(*g_GameScenePtr, delta) &&
            g_GameCameraPtr &&
            SyncGameCameraFromActiveComponent(*g_GameCameraPtr)) {
            return;
        }
        if (g_GameCameraPtr) {
            g_GameCameraPtr->position = vadd(g_GameCameraPtr->position, delta);
        }
    };
    luaEngine.SetCameraBinding(std::move(cameraBinding));
    
    // Hydrate script components
    luaEngine.OnSimulationStarted();
#endif

    // ── Generate Builtin Primitive Meshes ─────────────────────────
    HP_LOG(Info, "Generating builtin primitive meshes...");
    
    // Cube mesh data (24 vertices with proper normals per face)
    // Uses extent [-1, +1] (2x2x2) to match Blender default Cube.glb
    const float c = 1.0f;
    const std::vector<Vertex> cubeVertices = {
        // Front face (Z+)
        {{-c, -c, c}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{c, -c, c}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{c, c, c}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-c, c, c}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // Back face (Z-)
        {{c, -c, -c}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{-c, -c, -c}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{-c, c, -c}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{c, c, -c}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // Right face (X+)
        {{c, -c, c}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{c, -c, -c}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{c, c, -c}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{c, c, c}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // Left face (X-)
        {{-c, -c, -c}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{-c, -c, c}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{-c, c, c}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-c, c, -c}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // Top face (Y+)
        {{-c, c, c}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{c, c, c}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{c, c, -c}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-c, c, -c}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // Bottom face (Y-)
        {{-c, -c, -c}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{c, -c, -c}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{c, -c, c}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-c, -c, c}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
    };
    const std::vector<uint32_t> cubeIndices = {
        0, 1, 2, 2, 3, 0,       // Front
        4, 5, 6, 6, 7, 4,       // Back
        8, 9, 10, 10, 11, 8,    // Right
        12, 13, 14, 14, 15, 12, // Left
        16, 17, 18, 18, 19, 16, // Top
        20, 21, 22, 22, 23, 20  // Bottom
    };
    
    MeshData cubeMeshData;
    cubeMeshData.vertices = cubeVertices;
    cubeMeshData.indices = cubeIndices;
    cubeMeshData.bounds = AABB(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
    
    size_t builtinCubeHandle = LoadMeshToGPU(cubeMeshData, "builtin://cube", vkdev.allocator,
        vkdev.device, vkdev.commandPool, vkdev.graphicsQueue, frameRenderer);
    if (builtinCubeHandle != SIZE_MAX) {
        HP_LOG(Info, "  Loaded builtin cube mesh at handle " + std::to_string(builtinCubeHandle));
    }
    
    // Sphere mesh generation
    auto generateSphereMesh = [](float radius, uint32_t rings, uint32_t segments) {
        MeshData data;
        const float pi = 3.14159265359f;
        
        for (uint32_t ring = 0; ring <= rings; ++ring) {
            float phi = pi * float(ring) / float(rings);
            float y = radius * std::cos(phi);
            float ringRadius = radius * std::sin(phi);
            
            for (uint32_t seg = 0; seg <= segments; ++seg) {
                float theta = 2.0f * pi * float(seg) / float(segments);
                float x = ringRadius * std::cos(theta);
                float z = ringRadius * std::sin(theta);
                
                Vertex v{};
                v.pos[0] = x; v.pos[1] = y; v.pos[2] = z;
                float len = std::sqrt(x*x + y*y + z*z);
                v.normal[0] = x/len; v.normal[1] = y/len; v.normal[2] = z/len;
                v.color[0] = 1.0f; v.color[1] = 1.0f; v.color[2] = 1.0f;
                v.texCoord[0] = float(seg) / float(segments);
                v.texCoord[1] = float(ring) / float(rings);
                data.vertices.push_back(v);
            }
        }
        
        uint32_t stride = segments + 1;
        for (uint32_t ring = 0; ring < rings; ++ring) {
            for (uint32_t seg = 0; seg < segments; ++seg) {
                uint32_t i0 = ring * stride + seg;
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
    
    MeshData sphereMeshData = generateSphereMesh(1.0f, 24, 16);
    size_t builtinSphereHandle = LoadMeshToGPU(sphereMeshData, "builtin://sphere", vkdev.allocator,
        vkdev.device, vkdev.commandPool, vkdev.graphicsQueue, frameRenderer);
    if (builtinSphereHandle != SIZE_MAX) {
        HP_LOG(Info, "  Loaded builtin sphere mesh at handle " + std::to_string(builtinSphereHandle));
    }

    {
        Hephaestus::SkeletalImportResult fixture;
        std::vector<Mat4> palette;
        Hephaestus::SkeletalModelImporter::BuildRestPoseFixture(fixture, palette);
        if (!fixture.meshes.empty()) {
            const Hephaestus::SkinnedMeshPart& part = fixture.meshes.front();
            std::vector<SkinnedVertex> verts(part.vertices.size());
            for (size_t i = 0; i < part.vertices.size(); ++i) {
                const auto& src = part.vertices[i];
                std::memcpy(verts[i].pos, src.pos, sizeof(src.pos));
                std::memcpy(verts[i].normal, src.normal, sizeof(src.normal));
                std::memcpy(verts[i].color, src.color, sizeof(src.color));
                std::memcpy(verts[i].texCoord, src.texCoord, sizeof(src.texCoord));
                std::memcpy(verts[i].joints, src.joints, sizeof(src.joints));
                std::memcpy(verts[i].weights, src.weights, sizeof(src.weights));
            }
            std::vector<MeshData::Submesh> submeshes;
            for (const auto& sub : part.submeshes) {
                MeshData::Submesh gpuSub{};
                gpuSub.indexOffset = sub.indexOffset;
                gpuSub.indexCount = sub.indexCount;
                gpuSub.materialSlot = sub.materialSlot;
                gpuSub.name = sub.name;
                submeshes.push_back(std::move(gpuSub));
            }
            const size_t handle = frameRenderer.AddSkinnedMesh(
                "Skinned Rest Fixture", verts, part.indices, submeshes, fixture.bounds, palette,
                fs::path("builtin://skinned_rest_fixture"), false);
            if (handle != SIZE_MAX) {
                HP_LOG(Info, "  Loaded builtin skinned rest fixture at handle " +
                                 std::to_string(handle));
            }
        }
    }
    
    // ── Load Scene Meshes ─────────────────────────
    HP_LOG(Info, "Loading scene meshes...");
    {
        const fs::path characterRoot = !g_ProjectRoot.empty() ? g_ProjectRoot : g_RuntimeRoot;
        Hephaestus::CharacterRuntime::BindScene(scene, characterRoot, &frameRenderer);
    }
    
    try {
        auto meshView = scene.GetEntitiesWith<MeshRendererComponent>();
        int meshEntityCount = 0;
        int loadedCount = 0;
        
        for (auto entityHandle : meshView) {
            meshEntityCount++;
            
            try {
                Entity entity(entityHandle, &scene);
                
                // Get entity name for better logging
                std::string entityName = "Entity_" + std::to_string(meshEntityCount);
                if (entity.HasComponent<TagComponent>()) {
                    entityName = entity.GetComponent<TagComponent>().tag;
                }
                
                auto& renderer = entity.GetComponent<MeshRendererComponent>();
                if (IsRuntimeGeneratedVisual(renderer)) {
                    loadedCount++;
                    continue;
                }
                
                // Skip entities without mesh asset paths
                if (renderer.meshAssetPath.empty()) {
                    HP_LOG(Info, "  Skipping " + entityName + " (no mesh path)");
                    continue;
                }
                
                // Handle builtin:// meshes by assigning pre-generated handles
                if (renderer.meshAssetPath.rfind("builtin://", 0) == 0) {
                    std::string builtinKey = renderer.meshAssetPath;
                    // Normalize to lowercase for comparison
                    std::transform(builtinKey.begin(), builtinKey.end(), builtinKey.begin(), ::tolower);
                    
                    size_t builtinHandle = frameRenderer.GetMeshHandle(builtinKey);
                    if (builtinHandle != SIZE_MAX) {
                        renderer.meshHandle = builtinHandle;
                        HP_LOG(Info, "  " + entityName + " using builtin mesh: " + renderer.meshAssetPath + " (handle " + std::to_string(renderer.meshHandle) + ")");
                        loadedCount++;
                    } else {
                        HP_LOG(Warning, "  " + entityName + " references unknown builtin mesh: " + renderer.meshAssetPath);
                    }
                    continue;
                }
                
                // Skip .hpscene files (scene references, not meshes)
                if (renderer.meshAssetPath.find(".hpscene") != std::string::npos) {
                    HP_LOG(Info, "  Skipping " + entityName + " (scene reference, not a mesh)");
                    continue;
                }
                
                std::string meshKey = renderer.meshAssetPath;
                HP_LOG(Info, "  Processing " + entityName + " -> " + meshKey);
                
                // Check if already loaded
                size_t cachedIdx = frameRenderer.GetMeshHandle(meshKey);
                if (cachedIdx != SIZE_MAX) {
                    renderer.meshHandle = cachedIdx;
                    
                    // Sync submesh count from cached mesh
                    const MeshGPU& cachedMesh = *frameRenderer.GetMesh(cachedIdx);
                    int cachedSubmeshCount = cachedMesh.submeshes.empty() ? 1 : static_cast<int>(cachedMesh.submeshes.size());
                    if (renderer.submeshCount != cachedSubmeshCount) {
                        HP_LOG(Info, "    Syncing submeshCount from cached mesh: " + std::to_string(renderer.submeshCount) + " -> " + std::to_string(cachedSubmeshCount));
                        renderer.submeshCount = cachedSubmeshCount;
                    }
                    
                    HP_LOG(Info, "    Reusing cached mesh (submeshes=" + std::to_string(cachedMesh.submeshes.size()) + 
                        ", materialSlots=" + std::to_string(renderer.appliedMaterialPaths.size()) + ")");
                    loadedCount++;
                    continue;
                }
                
                // Load mesh from file – try multiple resolution strategies
                // so that scenes created on another device still work.
                fs::path meshPath;
                fs::path assetP(renderer.meshAssetPath);

                // 1. runtimeRoot / key  (standard relative path)
                if (assetP.is_relative() && !g_RuntimeRoot.empty()) {
                    fs::path candidate = g_RuntimeRoot / assetP;
                    if (fs::exists(candidate)) meshPath = candidate;
                }

                // 1b. projectRoot / key  (project detected from .heproject)
                if (meshPath.empty() && assetP.is_relative() && !g_ProjectRoot.empty()) {
                    fs::path candidate = (g_ProjectRoot / assetP).lexically_normal();
                    if (fs::exists(candidate)) meshPath = candidate;
                }

                // 2. Backward compat: "assets/meshes/X" → ".engine/meshes/X"
                if (meshPath.empty() && assetP.is_relative() && !g_RuntimeRoot.empty()) {
                    std::string rel = assetP.generic_string();
                    if (rel.rfind("assets/meshes/", 0) == 0) {
                        std::string filename = rel.substr(std::string("assets/meshes/").size());
                        fs::path candidate = g_RuntimeRoot / ".engine" / "meshes" / filename;
                        if (fs::exists(candidate)) meshPath = candidate;
                    }
                }
                // 2b. Same backward compat under project root
                if (meshPath.empty() && assetP.is_relative() && !g_ProjectRoot.empty()) {
                    std::string rel = assetP.generic_string();
                    if (rel.rfind("assets/meshes/", 0) == 0) {
                        std::string filename = rel.substr(std::string("assets/meshes/").size());
                        fs::path candidate = (g_ProjectRoot / ".engine" / "meshes" / filename).lexically_normal();
                        if (fs::exists(candidate)) meshPath = candidate;
                    }
                }

                // 3. Strip "assets/" prefix, try under runtimeRoot/assets/
                if (meshPath.empty() && assetP.is_relative() && !g_RuntimeRoot.empty()) {
                    std::string rel = assetP.generic_string();
                    fs::path assetsDir = g_RuntimeRoot / "assets";
                    if (rel.rfind("assets/", 0) == 0) {
                        std::string stripped = rel.substr(std::string("assets/").size());
                        fs::path candidate = assetsDir / stripped;
                        if (fs::exists(candidate)) meshPath = candidate;
                    } else {
                        fs::path candidate = assetsDir / assetP;
                        if (fs::exists(candidate)) meshPath = candidate;
                    }
                }
                // 3b. Strip "assets/" prefix, try under projectRoot/assets/
                if (meshPath.empty() && assetP.is_relative() && !g_ProjectAssets.empty()) {
                    std::string rel = assetP.generic_string();
                    if (rel.rfind("assets/", 0) == 0) {
                        std::string stripped = rel.substr(std::string("assets/").size());
                        fs::path candidate = (g_ProjectAssets / stripped).lexically_normal();
                        if (fs::exists(candidate)) meshPath = candidate;
                    } else {
                        fs::path candidate = (g_ProjectAssets / assetP).lexically_normal();
                        if (fs::exists(candidate)) meshPath = candidate;
                    }
                }

                // 4. Filename search in known mesh directories
                if (meshPath.empty()) {
                    fs::path filename = assetP.filename();
                    if (!filename.empty()) {
                        std::vector<fs::path> searchDirs;
                        // Project directories first (most likely to have custom meshes)
                        if (!g_ProjectAssets.empty()) {
                            searchDirs.push_back(g_ProjectAssets / "meshes");
                            searchDirs.push_back(g_ProjectAssets);
                        }
                        if (!g_ProjectRoot.empty()) {
                            searchDirs.push_back(g_ProjectRoot / ".engine" / "meshes");
                        }
                        // Runtime directories as fallback
                        if (!g_RuntimeRoot.empty()) {
                            searchDirs.push_back(g_RuntimeRoot / "assets" / "meshes");
                            searchDirs.push_back(g_RuntimeRoot / "assets");
                            searchDirs.push_back(g_RuntimeRoot / ".engine" / "meshes");
                        }
                        for (const auto& dir : searchDirs) {
                            std::error_code ec;
                            if (!fs::is_directory(dir, ec)) continue;
                            fs::path candidate = dir / filename;
                            if (fs::exists(candidate, ec)) {
                                HP_LOG(Info, "    Fallback found '" + renderer.meshAssetPath + "' at " + candidate.string());
                                meshPath = candidate;
                                break;
                            }
                        }
                    }
                }

                // 5. Try as absolute (legacy scenes from the original device)
                if (meshPath.empty() && assetP.is_absolute() && fs::exists(assetP)) {
                    meshPath = assetP;
                }

                // 6. Fallback: map engine mesh paths to builtin:// equivalents.
                //    Handles .engine/meshes/X.glb or bare filenames like Cube.glb
                //    when the .engine/ directory wasn't deployed and the
                //    procedural builtin equivalent exists.
                if (!fs::exists(meshPath)) {
                    std::string rel = assetP.generic_string();
                    std::string stem;
                    const std::string engineMeshPrefix = ".engine/meshes/";
                    if (rel.rfind(engineMeshPrefix, 0) == 0) {
                        stem = rel.substr(engineMeshPrefix.size());
                    } else {
                        // Bare filename (e.g. "Cube.glb") — try as builtin
                        stem = assetP.filename().string();
                    }
                    // Strip extension to get base name
                    auto dotPos = stem.rfind('.');
                    if (dotPos != std::string::npos)
                        stem = stem.substr(0, dotPos);
                    if (!stem.empty()) {
                        // Lowercase for builtin:// key matching
                        std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
                        std::string builtinKey = "builtin://" + stem;
                        size_t builtinHandle = frameRenderer.GetMeshHandle(builtinKey);
                        if (builtinHandle != SIZE_MAX) {
                            renderer.meshHandle = builtinHandle;
                            HP_LOG(Info, "  " + entityName + " mapped " + renderer.meshAssetPath + " -> " + builtinKey + " (handle " + std::to_string(renderer.meshHandle) + ")");
                            loadedCount++;
                            continue;
                        }
                    }
                }
                
                if (!fs::exists(meshPath)) {
                    HP_LOG(Warning, "    Mesh not found: " + renderer.meshAssetPath
                        + " (tried project root, .engine/meshes, assets/meshes, filename search)");
                    continue;
                }
                
                MeshData meshData;
                std::string importError;
                HP_LOG(Info, "    Loading from: " + meshPath.string());
                
                if (ModelImporter::LoadModel(meshPath, meshData, importError)) {
                    HP_LOG(Info, "    Model imported, uploading to GPU...");
                    size_t idx = LoadMeshToGPU(meshData, meshKey, vkdev.allocator, 
                        vkdev.device, vkdev.commandPool, vkdev.graphicsQueue, frameRenderer);
                    if (idx != SIZE_MAX) {
                        renderer.meshHandle = idx;
                        frameRenderer.AddMeshAlias(meshPath.lexically_normal().generic_string(), idx);
                        
                        // Update submesh count from the loaded mesh data
                        const MeshGPU& loadedMesh = *frameRenderer.GetMesh(idx);
                        int newSubmeshCount = loadedMesh.submeshes.empty() ? 1 : static_cast<int>(loadedMesh.submeshes.size());
                        if (renderer.submeshCount != newSubmeshCount) {
                            HP_LOG(Info, "    Updating submeshCount: " + std::to_string(renderer.submeshCount) + " -> " + std::to_string(newSubmeshCount));
                            renderer.submeshCount = newSubmeshCount;
                        }
                        
                        // Log submesh/material debug info
                        HP_LOG(Info, "    Loaded mesh: " + meshKey + " (submeshes=" + std::to_string(loadedMesh.submeshes.size()) + 
                            ", materialSlots=" + std::to_string(renderer.appliedMaterialPaths.size()) + ")");
                        
                        // Warn if submesh count doesn't match material slot count
                        if (!loadedMesh.submeshes.empty() && 
                            renderer.appliedMaterialPaths.size() != loadedMesh.submeshes.size()) {
                            HP_LOG(Warning, "    Mesh '" + meshKey + "' has " + std::to_string(loadedMesh.submeshes.size()) + 
                                " submeshes but " + std::to_string(renderer.appliedMaterialPaths.size()) + 
                                " material paths assigned. Some sections may use fallback materials.");
                        }
                        
                        loadedCount++;
                    } else {
                        HP_LOG(Warning, "    GPU upload failed for: " + meshKey);
                    }
                } else {
                    HP_LOG(Warning, "    Failed to load mesh " + meshPath.string() + ": " + importError);
                }
            } catch (const std::exception& e) {
                HP_LOG(Error, "  Exception processing entity " + std::to_string(meshEntityCount) + ": " + std::string(e.what()));
            } catch (...) {
                HP_LOG(Error, "  Unknown exception processing entity " + std::to_string(meshEntityCount));
            }
        }
        
        HP_LOG(Info, "Mesh loading complete. Processed " + std::to_string(meshEntityCount) + " entities, loaded " + std::to_string(loadedCount) + " meshes");
    } catch (const std::exception& e) {
        HP_LOG(Error, "Exception during mesh loading: " + std::string(e.what()));
    } catch (...) {
        HP_LOG(Error, "Unknown exception during mesh loading");
    }
    
    HP_LOG(Info, "Loaded " + std::to_string(frameRenderer.GetMeshCount()) + " unique meshes to GPU");
    
    // ── Find Active Camera ────────────────────────
    HP_LOG(Info, "Finding active camera...");
    // gameCamera already declared earlier for Lua binding
    try {
        if (const std::optional<Hephaestus::ActiveCameraView> view =
                Hephaestus::TryReadActiveCameraView(scene)) {
            ApplyActiveCameraView(*view, gameCamera);
            std::string entityName = "Unknown";
            Entity entity(view->entity, &scene);
            if (entity && entity.HasComponent<TagComponent>()) {
                entityName = entity.GetComponent<TagComponent>().tag;
            }
            HP_LOG(Info, "Using camera from entity: " + entityName);
        } else {
            HP_LOG(Warning, "No active camera found, using defaults");
        }
    } catch (const std::exception& e) {
        HP_LOG(Error, "Exception finding camera: " + std::string(e.what()));
    } catch (...) {
        HP_LOG(Error, "Unknown exception finding camera");
    }

    Bootstrap::BeginStage(InitStage::Ready);
    HP_LOG(Info, "Game runtime ready!");
    Bootstrap::EndStage(InitStage::Ready, true);

    // ── Debug: Multi-Material Mesh Validation ─────
#ifdef _DEBUG
    HP_LOG(Info, "=== Multi-Material Mesh Debug Info ===");
    {
        auto debugMeshView = scene.GetEntitiesWith<MeshRendererComponent>();
        for (auto entityHandle : debugMeshView) {
            Entity entity(entityHandle, &scene);
            auto& renderer = entity.GetComponent<MeshRendererComponent>();
            
            if (!renderer.HasValidMesh() || renderer.meshHandle >= frameRenderer.GetMeshCount()) {
                continue;
            }
            
            std::string entityName = "Entity";
            if (entity.HasComponent<TagComponent>()) {
                entityName = entity.GetComponent<TagComponent>().tag;
            }
            
            const MeshGPU& mesh = *frameRenderer.GetMesh(renderer.meshHandle);
            size_t submeshCount = mesh.submeshes.empty() ? 1 : mesh.submeshes.size();
            size_t materialSlotCount = renderer.appliedMaterialPaths.size();
            
            HP_LOG(Info, "Entity: " + entityName);
            HP_LOG(Info, "  Mesh: " + mesh.name + " (handle=" + std::to_string(renderer.meshHandle) + ")");
            HP_LOG(Info, "  Submeshes: " + std::to_string(submeshCount) + ", Material slots: " + std::to_string(materialSlotCount));
            
            // Log each submesh and its material binding
            if (mesh.submeshes.empty()) {
                std::string matPath = materialSlotCount > 0 ? renderer.appliedMaterialPaths[0] : "";
                HP_LOG(Info, "  [Single mesh] Material: " + (matPath.empty() ? "(default)" : matPath));
            } else {
                for (size_t i = 0; i < mesh.submeshes.size(); ++i) {
                    const auto& sub = mesh.submeshes[i];
                    std::string matPath = "";
                    std::string matStatus = "default";
                    
                    if (static_cast<size_t>(sub.materialSlot) < materialSlotCount) {
                        matPath = renderer.appliedMaterialPaths[sub.materialSlot];
                        matStatus = matPath.empty() ? "default" : "resolved by frame renderer";
                    } else {
                        matStatus = "slot missing";
                    }
                    
                    HP_LOG(Info, "  Submesh[" + std::to_string(i) + "] slot=" + std::to_string(sub.materialSlot) + 
                        ", indices=" + std::to_string(sub.indexOffset) + "-" + std::to_string(sub.indexOffset + sub.indexCount) +
                        ", material=" + (matPath.empty() ? "(default)" : matPath) + " [" + matStatus + "]");
                }
            }
            
            // Warn about mismatches
            if (submeshCount > materialSlotCount && materialSlotCount > 0) {
                HP_LOG(Warning, "  WARNING: More submeshes (" + std::to_string(submeshCount) + 
                    ") than material slots (" + std::to_string(materialSlotCount) + 
                    "). Some sections will use fallback material.");
            }
        }
    }
    HP_LOG(Info, "=== End Multi-Material Debug Info ===");
#endif

    auto lastTime = std::chrono::high_resolution_clock::now();
    std::array<bool, 3> uiMousePressed = {false, false, false};
    double lastCursorX = 0.0;
    double lastCursorY = 0.0;
    bool hasLastCursorPosition = false;
    bool recreateSwapchain = false;

    Hephaestus::FrameRenderConfig renderConfig{};
    renderConfig.isEditorMode = false;
    renderConfig.isPlaying = true;
    renderConfig.shadingMode = Hephaestus::ViewShadingMode::Lit;
    renderConfig.renderSkybox = true;
    renderConfig.renderShadows = true;
    renderConfig.renderGizmo = false;
    renderConfig.renderBillboards = false;
    renderConfig.renderOutlines = false;
    renderConfig.renderGrid = false;

#if HEPHAESTUS_RUNTIME_AUTOMATION
    std::unique_ptr<Hephaestus::Runtime::PackagedAutomation> packagedAutomation;
    if (g_Config.automationEnabled) {
        const fs::path captureRoot = !g_ProjectRoot.empty() ? g_ProjectRoot : g_RuntimeRoot;
        packagedAutomation = std::make_unique<Hephaestus::Runtime::PackagedAutomation>(
            scene, inputSystem, uiSystem, uiContext, joltPhysicsEngine, physicsSystem, characterControllers,
            frameRenderer, captureRoot.generic_string(), g_Config.projectName);
        if (!packagedAutomation->Start(g_Config.automationPort)) {
            HP_LOG(Warning, "Runtime automation was enabled but the MCP endpoint failed to start");
        }
    }
#endif

    // ── Main Loop ─────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (g_FramebufferResized || recreateSwapchain) {
            g_FramebufferResized = false;
            recreateSwapchain = false;
            try {
                vkDeviceWaitIdle(vkdev.device);
                swap.Recreate(window);
                frameRenderer.OnResize(swap.extent.width, swap.extent.height);
            } catch (const std::exception& e) {
                HP_LOG(Error, std::string("Failed to recreate swapchain: ") + e.what());
                Bootstrap::ShowFatalError("Renderer Error",
                    std::string("Failed to recreate the swapchain:\n") + e.what());
                break;
            }
        }

        // Calculate delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        inputSystem.BeginFrame();
        for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
            const int state = glfwGetKey(window, key);
            inputSystem.SetKeyState(key, state == GLFW_PRESS || state == GLFW_REPEAT);
        }
        for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
            inputSystem.SetMouseButtonState(
                button, glfwGetMouseButton(window, button) == GLFW_PRESS);
        }

        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        inputSystem.SetMousePosition(cursorX, cursorY);
        inputSystem.SetMouseDelta(
            hasLastCursorPosition ? cursorX - lastCursorX : 0.0,
            hasLastCursorPosition ? cursorY - lastCursorY : 0.0);
        lastCursorX = cursorX;
        lastCursorY = cursorY;
        hasLastCursorPosition = true;
        inputSystem.AddScrollDelta(g_ScrollDeltaX, g_ScrollDeltaY);
        g_ScrollDeltaX = 0.0;
        g_ScrollDeltaY = 0.0;

        GLFWgamepadstate gamepadState{};
        const bool gamepadConnected =
            glfwJoystickIsGamepad(GLFW_JOYSTICK_1) == GLFW_TRUE &&
            glfwGetGamepadState(GLFW_JOYSTICK_1, &gamepadState) == GLFW_TRUE;
        inputSystem.SetGamepadConnected(gamepadConnected);
        if (gamepadConnected) {
            for (int button = 0; button <= GLFW_GAMEPAD_BUTTON_LAST; ++button) {
                inputSystem.SetGamepadButtonState(
                    button, gamepadState.buttons[button] == GLFW_PRESS);
            }
            for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis) {
                float value = gamepadState.axes[axis];
                if (axis == GLFW_GAMEPAD_AXIS_LEFT_TRIGGER ||
                    axis == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                    value = (value + 1.0f) * 0.5f;
                }
                inputSystem.SetGamepadAxisState(axis, value);
            }
        }

        uiContext.SetDeltaTimeSeconds(deltaTime);
        uiContext.SetViewportSize({static_cast<float>(swap.extent.width), static_cast<float>(swap.extent.height)});

#if HEPHAESTUS_RUNTIME_AUTOMATION
        if (packagedAutomation) {
            packagedAutomation->Tick();
        }
#endif

        const HUI::UIVec2 mousePos{static_cast<float>(cursorX), static_cast<float>(cursorY)};
        uiContext.QueueMouseMove(mousePos);

        const int mouseButtons[3] = {GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT, GLFW_MOUSE_BUTTON_MIDDLE};
        for (int i = 0; i < 3; ++i) {
            const bool pressed = glfwGetMouseButton(window, mouseButtons[i]) == GLFW_PRESS;
            if (pressed && !uiMousePressed[static_cast<size_t>(i)]) {
                uiContext.QueueMousePress(mousePos, mouseButtons[i]);
            } else if (!pressed && uiMousePressed[static_cast<size_t>(i)]) {
                uiContext.QueueMouseRelease(mousePos, mouseButtons[i]);
            }
            uiMousePressed[static_cast<size_t>(i)] = pressed;
        }

        // Update declarative ECS audio sources.
        UpdateAudioSources(scene, audioEngine);

        // Bodies for entities already in the scene exist before onUpdate.
        physicsSystem.PrepareSceneBodies(scene);

        // Update scripts
#if HEPHAESTUS_GAME_SCRIPTING
        luaEngine.Update(scene, deltaTime);
#endif

        // Fixed-step physics after scripts so onUpdate can write this frame's motion.
        physicsSystem.Update(scene, deltaTime);
#if HEPHAESTUS_GAME_SCRIPTING
        luaEngine.DispatchContactEvents(scene, physicsSystem.FrameContactEvents());
#endif

        Hephaestus::CharacterAnimationSystem::Tick(scene, deltaTime);
#if HEPHAESTUS_GAME_SCRIPTING
        luaEngine.DispatchAnimationFinished(scene);
        luaEngine.LateUpdate(scene, deltaTime);
#endif

        uiSystem.Update(uiContext);
#if HEPHAESTUS_RUNTIME_AUTOMATION
        if (packagedAutomation) packagedAutomation->NoteUIUpdated();
#endif
        uiRenderer.Reset();
        uiSystem.Render(uiRenderer);

        // Follow the active CameraComponent after LateUpdate and before render.
        // Camera.setPosition / setRotation write that entity when one exists.
        SyncGameCameraFromActiveComponent(gameCamera);

        // Submit the renderer-independent HUI command list to the Vulkan overlay
        // backend; the frame renderer consumes it during RenderFrame().
        frameRenderer.SubmitHUICommands(uiRenderer.GetDrawCommands());

        if (!frameRenderer.RenderFrame(&scene, gameCamera.ToRenderCamera(), renderConfig, deltaTime)) {
            if (frameRenderer.HasDeviceLost()) {
                HP_LOG(Error, "Vulkan device lost; stopping the game loop");
                Bootstrap::ShowFatalError("Renderer Error",
                    "The Vulkan device was lost. The game must close.");
                break;
            }
            recreateSwapchain = true;
        }
    }

    // ── Cleanup ───────────────────────────────────
    HP_LOG(Info, "Shutting down...");
    
#if HEPHAESTUS_RUNTIME_AUTOMATION
    if (packagedAutomation) {
        packagedAutomation->Stop();
    }
#endif

    vkDeviceWaitIdle(vkdev.device);
    g_GameCameraPtr = nullptr;
    g_GameScenePtr = nullptr;
    frameRenderer.Shutdown();
    swap.Cleanup();
    vkdev.Cleanup();
    vkDestroyInstance(instance, nullptr);
    
    glfwDestroyWindow(window);
    glfwTerminate();

    Bootstrap::Shutdown();
    
    return 0;
}
