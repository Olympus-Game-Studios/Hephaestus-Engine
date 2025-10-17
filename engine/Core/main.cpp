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

#include "Math.h"

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
        float aspect = extent.width / (float)extent.height;
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

// ImGui panel that lists files and folders in the asset directory.
static void DrawAssetBrowserPanel()
{
    namespace fs = std::filesystem;

    static bool initialized = false;
    static fs::path assetRoot;
    static fs::path currentPath;
    static fs::path selectedPath;

    if (!initialized) {
        initialized = true;
        std::error_code absErr;
        fs::path absoluteRoot = fs::absolute("assets", absErr);
        assetRoot = absErr ? fs::path("assets") : absoluteRoot.lexically_normal();
        currentPath = assetRoot;
        selectedPath.clear();
    }

    assetRoot = assetRoot.lexically_normal();
    currentPath = ClampToRoot(currentPath, assetRoot);

    if (!ImGui::Begin("Asset Browser")) {
        ImGui::End();
        return;
    }

    std::error_code existsErr;
    bool rootExists = fs::exists(assetRoot, existsErr);
    if (!rootExists || existsErr) {
        std::string rootDisplay = assetRoot.generic_string();
        ImGui::TextDisabled("Asset directory not found: %s", rootDisplay.c_str());
        ImGui::End();
        return;
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
        }
    }

    ImGui::Separator();

    std::error_code iterErr;
    fs::directory_iterator dirIt(currentPath, iterErr);
    if (iterErr) {
        ImGui::TextDisabled("Cannot open directory: %s", iterErr.message().c_str());
        ImGui::End();
        return;
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

    ImGui::BeginChild("AssetBrowserList", ImVec2(0, 0), false);

    if (directories.empty() && files.empty()) {
        ImGui::TextDisabled("Folder is empty");
    }

    for (const BrowserEntry& dirEntry : directories) {
        bool isSelected = !selectedPath.empty() && selectedPath == dirEntry.path;
        std::string displayLabel = "[DIR] " + dirEntry.label;
        if (ImGui::Selectable(displayLabel.c_str(), isSelected)) {
            selectedPath = ClampToRoot(dirEntry.path, assetRoot);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            currentPath = ClampToRoot(dirEntry.path, assetRoot);
            selectedPath.clear();
        }
    }

    for (const BrowserEntry& fileEntry : files) {
        bool isSelected = !selectedPath.empty() && selectedPath == fileEntry.path;
        if (ImGui::Selectable(fileEntry.label.c_str(), isSelected)) {
            selectedPath = ClampToRoot(fileEntry.path, assetRoot);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            selectedPath = ClampToRoot(fileEntry.path, assetRoot);
        }
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

    ImGui::End();
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
    const std::vector<uint16_t> cubeIndices = {
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


    VulkanBuffer vertexBuffer;
    vertexBuffer.Create(
        vkdev.allocator,
        sizeof(cubeVertices[0]) * cubeVertices.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    void* data;
    vmaMapMemory(vkdev.allocator, vertexBuffer.allocation, &data);
    memcpy(data, cubeVertices.data(), sizeof(cubeVertices[0]) * cubeVertices.size());
    vmaFlushAllocation(vkdev.allocator, vertexBuffer.allocation, 0, VK_WHOLE_SIZE);
    vmaUnmapMemory(vkdev.allocator, vertexBuffer.allocation);

    // ── Index Buffer ──────────────────────────────
    VulkanBuffer indexBuffer;
    indexBuffer.Create(
        vkdev.allocator,
        sizeof(cubeIndices[0]) * cubeIndices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    void* indexData;
    vmaMapMemory(vkdev.allocator, indexBuffer.allocation, &indexData);
    memcpy(indexData, cubeIndices.data(), sizeof(cubeIndices[0]) * cubeIndices.size());
    vmaFlushAllocation(vkdev.allocator, indexBuffer.allocation, 0, VK_WHOLE_SIZE);
    vmaUnmapMemory(vkdev.allocator, indexBuffer.allocation);

    // ── Camera + UBO ───────────────────────────
    Camera camera;
    camera.position = {0.0f, 0.0f, 3.0f}; // Position camera to view the cube
    // Start with cursor enabled (Unreal-style - hold RMB to look/move)
    g_cameraPtr = &camera; // Set global pointer for scroll callback

    // ── Scene & Entities (ECS) ───────────────────
    Scene scene("Main Scene");
    
    // Create cube entities with different materials
    Entity cube1 = scene.CreateEntity("Cube 1 (Metal)");
    cube1.AddComponent<MeshRendererComponent>();
    cube1.AddComponent<SelectionComponent>(true); // Start selected
    auto& cube1Mat = cube1.AddComponent<MaterialComponent>(Vec3(0.8f, 0.2f, 0.2f)); // Red
    cube1Mat.metallic = 0.9f;
    cube1Mat.roughness = 0.2f;
    auto& cube1Transform = cube1.GetComponent<TransformComponent>();
    cube1Transform.position = Vec3(0.0f, 0.0f, 0.0f);
    
    Entity cube2 = scene.CreateEntity("Cube 2 (Plastic)");
    cube2.AddComponent<MeshRendererComponent>();
    auto& cube2Mat = cube2.AddComponent<MaterialComponent>(Vec3(0.2f, 0.8f, 0.2f)); // Green
    cube2Mat.metallic = 0.0f;
    cube2Mat.roughness = 0.5f;
    auto& cube2Transform = cube2.GetComponent<TransformComponent>();
    cube2Transform.position = Vec3(2.0f, 0.0f, 0.0f);
    cube2Transform.scale = Vec3(0.5f, 0.5f, 0.5f);
    
    Entity cube3 = scene.CreateEntity("Cube 3 (Rough)");
    cube3.AddComponent<MeshRendererComponent>();
    auto& cube3Mat = cube3.AddComponent<MaterialComponent>(Vec3(0.2f, 0.4f, 0.8f)); // Blue
    cube3Mat.metallic = 0.0f;
    cube3Mat.roughness = 0.9f;
    auto& cube3Transform = cube3.GetComponent<TransformComponent>();
    cube3Transform.position = Vec3(-2.0f, 0.0f, 0.0f);
    cube3Transform.rotation = Vec3(0.0f, 0.785f, 0.0f); // 45 degrees
    
    // Create a fourth cube with emissive material
    Entity cube4 = scene.CreateEntity("Cube 4 (Emissive)");
    cube4.AddComponent<MeshRendererComponent>();
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

    // ── Billboard System for Light Visualization (Resources) ─────────────────────────
    VulkanTexture lightBulbTexture;
    VulkanPipeline billboardPipeline;
    VkDescriptorSetLayout billboardDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool billboardDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> billboardDescriptorSets;
    
    // Load light bulb texture
    bool textureLoaded = false;
    try {
        if (lightBulbTexture.LoadFromFile(vkdev.allocator, vkdev.device, vkdev.physicalDevice, 
                                      vkdev.commandPool, vkdev.graphicsQueue, 
                                      "assets/textures/light_bulb.png")) {
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

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

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

        vkWaitForFences(vkdev.device, 1, &inFlight[currentFrame], VK_TRUE, UINT64_MAX);
        vkResetFences(vkdev.device, 1, &inFlight[currentFrame]);

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
        
        // Setup ImGuizmo early to check if it's being used
        ImGuizmo::BeginFrame();
        ImGuizmo::SetOrthographic(false);
        
        // Handle mouse click for object selection (only when not using ImGui/ImGuizmo)
        static bool leftMouseWasPressed = false;
        bool leftMousePressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        
        if (leftMousePressed && !leftMouseWasPressed && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() &&
            !io.WantCaptureMouse && !gizmoDragging && vulkanGizmo.GetHoveredAxis() == -1) {
            // Left mouse button just pressed and not over ImGui
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            
            // Create picking ray
            UniformBufferObject ubo = camera.makeUBO(swap.extent);
            Ray pickRay = ScreenToWorldRay(mouseX, mouseY, swap.extent.width, swap.extent.height, ubo.view, ubo.proj);
            
            // Find closest hit
            float hitDist;
            Entity hitEntity = FindClosestHitEntity(pickRay, scene, hitDist);
            
            if (hitEntity) {
                scene.SetSelectedEntity(hitEntity);
            } else {
                scene.ClearSelection();
            }
        }
        leftMouseWasPressed = leftMousePressed;
        
        // Input handling (only when not using gizmo)
    if (!ImGuizmo::IsUsing() && !gizmoDragging) {
            camera.handleMouse(window);
            camera.handleKeyboard(window, dt);
        }

        // ImGui UI
        {
            ImGui::Begin("Hephaestus Engine");
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::Separator();
            ImGui::Text("Camera Controls:");
            ImGui::Text("Hold Right Mouse Button to look around");
            ImGui::Text("WASD - Move horizontally");
            ImGui::Text("Q/E - Move up/down");
            ImGui::Text("Mouse Wheel - Adjust speed");
            ImGui::Separator();
            ImGui::Text("Camera Position: (%.1f, %.1f, %.1f)", camera.position.x, camera.position.y, camera.position.z);
            ImGui::Text("Camera Speed: %.2f", camera.moveSpeed);
            ImGui::Text("Yaw: %.1f°  Pitch: %.1f°", camera.yawDeg, camera.pitchDeg);
            ImGui::Separator();
            ImGui::Checkbox("Show Gizmo Grid (debug)", &g_ShowGizmoGrid);
            ImGui::Text("Object Selection:");
            ImGui::Text("Left Click - Select object");
            ImGui::Text("Click empty space - Deselect all");
            ImGui::End();
            
            // Scene Hierarchy Window
            ImGui::Begin("Scene Hierarchy");
            ImGui::Text("Scene: %s", scene.GetName().c_str());
            ImGui::Separator();
            
            auto view = scene.GetEntitiesWith<TagComponent>();
            for (auto entityHandle : view) {
                Entity entity(entityHandle, &scene);
                auto& tag = entity.GetComponent<TagComponent>();
                
                bool isSelected = entity.HasComponent<SelectionComponent>() && 
                                entity.GetComponent<SelectionComponent>().selected;
                
                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
                }
                
                if (ImGui::Selectable(tag.tag.c_str(), isSelected)) {
                    scene.SetSelectedEntity(entity);
                }
                
                if (isSelected) {
                    ImGui::PopStyleColor();
                }
            }
            ImGui::End();
            
            // Properties Window
            ImGui::Begin("Properties");
            Entity selectedEntity = scene.GetSelectedEntity();
            
            if (selectedEntity) {
                auto& tag = selectedEntity.GetComponent<TagComponent>();
                auto& transform = selectedEntity.GetComponent<TransformComponent>();
                
                ImGui::Text("Entity: %s", tag.tag.c_str());
                ImGui::Separator();
                
                // Gizmo operation selector
                ImGui::Text("Gizmo Mode:");
                if (ImGui::RadioButton("Translate (T)", currentGizmoOperation == ImGuizmo::TRANSLATE))
                    currentGizmoOperation = ImGuizmo::TRANSLATE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Rotate (R)", currentGizmoOperation == ImGuizmo::ROTATE))
                    currentGizmoOperation = ImGuizmo::ROTATE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Scale (S)", currentGizmoOperation == ImGuizmo::SCALE))
                    currentGizmoOperation = ImGuizmo::SCALE;
                
                ImGui::Separator();
                if (ImGui::RadioButton("Local", currentGizmoMode == ImGuizmo::LOCAL))
                    currentGizmoMode = ImGuizmo::LOCAL;
                ImGui::SameLine();
                if (ImGui::RadioButton("World", currentGizmoMode == ImGuizmo::WORLD))
                    currentGizmoMode = ImGuizmo::WORLD;
                
                ImGui::Separator();
                
                // Transform editor
                ImGui::Text("Transform:");
                ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
                Vec3 rotDeg = transform.GetRotationDegrees();
                if (ImGui::DragFloat3("Rotation", &rotDeg.x, 1.0f)) {
                    transform.SetRotationDegrees(rotDeg);
                }
                ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.01f, 10.0f);
                
                ImGui::Separator();
                
                // Material editor (if entity has a material)
                if (selectedEntity.HasComponent<MaterialComponent>()) {
                    auto& material = selectedEntity.GetComponent<MaterialComponent>();
                    ImGui::Text("Material:");
                    ImGui::ColorEdit3("Albedo", &material.albedo.x);
                    ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f);
                    ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f);
                    ImGui::SliderFloat("AO", &material.ao, 0.0f, 1.0f);
                    ImGui::ColorEdit3("Emissive", &material.emissive.x);
                    ImGui::SliderFloat("Emissive Strength", &material.emissiveStrength, 0.0f, 10.0f);
                    ImGui::Separator();
                }
                
                // Light editor (if entity has a light)
                if (selectedEntity.HasComponent<LightComponent>()) {
                    auto& light = selectedEntity.GetComponent<LightComponent>();
                    ImGui::Text("Light:");
                    
                    const char* lightTypes[] = { "Directional", "Point", "Spot" };
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
                    ImGui::Separator();
                }
                
                ImGui::Text("Debug Info:");
                ImGui::Text("Entity ID: %u", static_cast<uint32_t>(selectedEntity));
                ImGui::Text("ImGuizmo IsUsing: %s", ImGuizmo::IsUsing() ? "YES" : "NO");
                ImGui::Text("ImGuizmo IsOver: %s", ImGuizmo::IsOver() ? "YES" : "NO");
            } else {
                ImGui::TextDisabled("No entity selected");
                ImGui::Text("Click on an entity in the viewport to select it");
            }
            ImGui::End();

            DrawAssetBrowserPanel();
        }
        
        // ImGuizmo keyboard shortcuts
        if (ImGui::IsKeyPressed(ImGuiKey_T)) currentGizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_S) && !ImGui::GetIO().KeyCtrl) currentGizmoOperation = ImGuizmo::SCALE;
        
        // Build view/proj for ImGuizmo and gizmo interactions
        UniformBufferObject ubo = camera.makeUBO(swap.extent);

        // Custom gizmo interaction handling
        {
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
        }
        
        // Render ImGui
        ImGui::Render();

        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(vkdev.device);
            swap.Recreate(window);
            createPipeline();
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

        // Bind vertex buffer and index buffer, then draw indexed
        VkBuffer vertexBuffers[] = { vertexBuffer.buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);
        
        // Draw all scene objects with MeshRendererComponent
        auto renderView = scene.GetEntitiesWith<TransformComponent, MeshRendererComponent>();

        // Prepare dynamic material buffer mapping
        void* matMappedBaseVoid = nullptr;
        vmaMapMemory(vkdev.allocator, materialBuffers[imageIndex].allocation, &matMappedBaseVoid);
        uint8_t* matMappedBase = reinterpret_cast<uint8_t*>(matMappedBaseVoid);
        uint32_t matIndex = 0;

        bool materialOverflowLogged = false;
        for (auto entityHandle : renderView) {
            Entity entity(entityHandle, &scene);
            auto& transform = entity.GetComponent<TransformComponent>();
            auto& renderer = entity.GetComponent<MeshRendererComponent>();
            
            if (!renderer.visible)
                continue;

            if (matIndex >= maxMaterialsPerFrame) {
                if (!materialOverflowLogged) {
                    std::cerr << "Material dynamic UBO capacity exceeded (" << maxMaterialsPerFrame
                              << " objects). Additional objects will be skipped this frame.\n";
                    materialOverflowLogged = true;
                }
                continue;
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
            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(cubeIndices.size()), 1, 0, 0, 0);
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
        if (selectedEntity) {
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
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(vkdev.device, imguiPool, nullptr);

    vulkanGizmo.Cleanup(vkdev.device, vkdev.allocator);
    vertexBuffer.Destroy(vkdev.allocator);
    indexBuffer.Destroy(vkdev.allocator);
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
