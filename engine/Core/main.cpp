#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>

#include "Math.h"

#include "../Gfx/VulkanDevice.h"
#include "../Gfx/VulkanSwapchain.h"
#include "../Gfx/VulkanPipeline.h"
#include "../Gfx/VulkanBuffer.h"

// Dear ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "ImGuizmo.h"

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

// ── Camera ───────────────────────────────────────
struct UniformBufferObject {
    Mat4 view;
    Mat4 proj;
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

// Find the closest object hit by a ray
int FindClosestHitObject(const Ray& ray, const std::vector<SceneObject>& objects, float& outDistance)
{
    int closestIndex = -1;
    float closestDist = 1e30f;
    
    for (size_t i = 0; i < objects.size(); ++i) {
        AABB worldBounds = objects[i].GetWorldBounds();
        
        float tMin, tMax;
        if (worldBounds.IntersectRay(ray, tMin, tMax)) {
            // Use entry point distance for sorting
            float hitDist = (tMin > 0) ? tMin : tMax;
            if (hitDist > 0 && hitDist < closestDist) {
                closestDist = hitDist;
                closestIndex = static_cast<int>(i);
            }
        }
    }
    
    outDistance = closestDist;
    return closestIndex;
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

    // ── Vertex Data ───────────────────────────────
    // 3D Cube vertices - 8 corners of a unit cube centered at origin
    const std::vector<Vertex> cubeVertices = {
        // Front face (Z = 0.5) - Red tint
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},  // 0: bottom-left
        {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},  // 1: bottom-right
        {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.5f, 0.5f}},  // 2: top-right
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.5f, 0.5f}},  // 3: top-left
        
        // Back face (Z = -0.5) - Blue tint
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},  // 4: bottom-left
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},  // 5: bottom-right
        {{ 0.5f,  0.5f, -0.5f}, {0.5f, 0.5f, 1.0f}},  // 6: top-right
        {{-0.5f,  0.5f, -0.5f}, {0.5f, 0.5f, 1.0f}},  // 7: top-left
    };
    
    // Index buffer for cube (12 triangles, 2 per face, 6 faces)
    const std::vector<uint16_t> cubeIndices = {
        // Front face
        0, 1, 2,  2, 3, 0,
        // Right face
        1, 5, 6,  6, 2, 1,
        // Back face
        5, 4, 7,  7, 6, 5,
        // Left face
        4, 0, 3,  3, 7, 4,
        // Top face
        3, 2, 6,  6, 7, 3,
        // Bottom face
        4, 5, 1,  1, 0, 4
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

    // ── Scene Objects ───────────────────────────
    std::vector<SceneObject> sceneObjects;
    sceneObjects.push_back(SceneObject("Cube 1"));
    sceneObjects[0].transform.position = Vec3(0.0f, 0.0f, 0.0f);
    sceneObjects[0].selected = true;
    
    sceneObjects.push_back(SceneObject("Cube 2"));
    sceneObjects[1].transform.position = Vec3(2.0f, 0.0f, 0.0f);
    sceneObjects[1].transform.scale = Vec3(0.5f, 0.5f, 0.5f);
    
    sceneObjects.push_back(SceneObject("Cube 3"));
    sceneObjects[2].transform.position = Vec3(-2.0f, 0.0f, 0.0f);
    sceneObjects[2].transform.rotation = Vec3(0.0f, 0.785f, 0.0f); // 45 degrees

    // ── ImGuizmo State ───────────────────────────
    ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;
    // Whether the gizmo should follow the camera transform. When false, the gizmo will be
    // rendered in world space independent of the user's camera position/rotation.
    static bool gizmoFollowCamera = true;

    // Per-image uniform buffers and descriptors (tied to swapchain images)
    std::vector<VulkanBuffer> uniformBuffers;
    std::vector<VkDescriptorSet> descriptorSets;
    VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
    VkDeviceSize uboSize = sizeof(UniformBufferObject);

    auto destroyDescriptors = [&]() {
        for (auto& ub : uniformBuffers) ub.Destroy(vkdev.allocator);
        uniformBuffers.clear();
        descriptorSets.clear();
        if (descriptorPool) { vkDestroyDescriptorPool(vkdev.device, descriptorPool, nullptr); descriptorPool = VK_NULL_HANDLE; }
    };

    auto createDescriptors = [&]() {
        destroyDescriptors();

        uniformBuffers.resize(swap.framebuffers.size());
        for (auto& ub : uniformBuffers) {
            ub.Create(
                vkdev.allocator,
                uboSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU
            );
        }

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = static_cast<uint32_t>(uniformBuffers.size());

        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = static_cast<uint32_t>(uniformBuffers.size());
        if (vkCreateDescriptorPool(vkdev.device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create descriptor pool");

        descriptorSets.resize(uniformBuffers.size());
        std::vector<VkDescriptorSetLayout> layouts(uniformBuffers.size(), pipeline.descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(vkdev.device, &allocInfo, descriptorSets.data()) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate descriptor sets");

        for (size_t i = 0; i < uniformBuffers.size(); ++i) {
            VkDescriptorBufferInfo bufInfo{};
            bufInfo.buffer = uniformBuffers[i].buffer;
            bufInfo.offset = 0;
            bufInfo.range = uboSize;

            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = descriptorSets[i];
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufInfo;
            vkUpdateDescriptorSets(vkdev.device, 1, &write, 0, nullptr);
        }
    };
    createDescriptors();

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
        
        if (leftMousePressed && !leftMouseWasPressed && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() && !io.WantCaptureMouse) {
            // Left mouse button just pressed and not over ImGui
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            
            // Create picking ray
            UniformBufferObject ubo = camera.makeUBO(swap.extent);
            Ray pickRay = ScreenToWorldRay(mouseX, mouseY, swap.extent.width, swap.extent.height, ubo.view, ubo.proj);
            
            // Debug: print ray info
            std::cout << "Click at (" << mouseX << ", " << mouseY << ")" << std::endl;
            std::cout << "Ray origin: (" << pickRay.origin.x << ", " << pickRay.origin.y << ", " << pickRay.origin.z << ")" << std::endl;
            std::cout << "Ray direction: (" << pickRay.direction.x << ", " << pickRay.direction.y << ", " << pickRay.direction.z << ")" << std::endl;
            
            // Find closest hit
            float hitDist;
            int hitIndex = FindClosestHitObject(pickRay, sceneObjects, hitDist);
            
            std::cout << "Hit index: " << hitIndex << ", Distance: " << hitDist << std::endl;
            
            // Debug: print object bounds
            for (size_t i = 0; i < sceneObjects.size(); ++i) {
                AABB bounds = sceneObjects[i].GetWorldBounds();
                std::cout << "Object " << i << " (" << sceneObjects[i].name << "): min(" 
                          << bounds.min.x << ", " << bounds.min.y << ", " << bounds.min.z 
                          << ") max(" << bounds.max.x << ", " << bounds.max.y << ", " << bounds.max.z << ")" << std::endl;
                
                float tMin, tMax;
                bool hit = bounds.IntersectRay(pickRay, tMin, tMax);
                std::cout << "  Hit: " << (hit ? "YES" : "NO") << ", tMin: " << tMin << ", tMax: " << tMax << std::endl;
            }
            
            if (hitIndex >= 0) {
                // Deselect all and select hit object
                for (auto& obj : sceneObjects) obj.selected = false;
                sceneObjects[hitIndex].selected = true;
            } else {
                // Clicked on empty space - deselect all
                for (auto& obj : sceneObjects) obj.selected = false;
            }
        }
        leftMouseWasPressed = leftMousePressed;
        
        // Input handling (only when not using gizmo)
        if (!ImGuizmo::IsUsing()) {
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
            ImGui::Text("Object Selection:");
            ImGui::Text("Left Click - Select object");
            ImGui::Text("Click empty space - Deselect all");
            ImGui::End();
            
            // Scene Hierarchy Window
            ImGui::Begin("Scene Hierarchy");
            for (size_t i = 0; i < sceneObjects.size(); ++i) {
                // Highlight selected object
                bool wasSelected = sceneObjects[i].selected;
                if (wasSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
                }
                
                if (ImGui::Selectable(sceneObjects[i].name, sceneObjects[i].selected)) {
                    // Deselect all, select this one
                    for (auto& obj : sceneObjects) obj.selected = false;
                    sceneObjects[i].selected = true;
                }
                
                if (wasSelected) {
                    ImGui::PopStyleColor();
                }
            }
            ImGui::End();
            
            // Properties Window
            ImGui::Begin("Properties");
            bool hasSelection = false;
            for (auto& obj : sceneObjects) {
                if (obj.selected) {
                    hasSelection = true;
                    ImGui::Text("Object: %s", obj.name);
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
                    ImGui::DragFloat3("Position", &obj.transform.position.x, 0.1f);
                    Vec3 rotDeg = Vec3(
                        obj.transform.rotation.x * 57.2958f,
                        obj.transform.rotation.y * 57.2958f,
                        obj.transform.rotation.z * 57.2958f
                    );
                    if (ImGui::DragFloat3("Rotation", &rotDeg.x, 1.0f)) {
                        obj.transform.rotation.x = rotDeg.x * 0.0174533f;
                        obj.transform.rotation.y = rotDeg.y * 0.0174533f;
                        obj.transform.rotation.z = rotDeg.z * 0.0174533f;
                    }
                    ImGui::DragFloat3("Scale", &obj.transform.scale.x, 0.01f, 0.01f, 10.0f);
                    
                    ImGui::Separator();
                    ImGui::Text("Debug Info:");
                    ImGui::Text("ImGuizmo IsUsing: %s", ImGuizmo::IsUsing() ? "YES" : "NO");
                    ImGui::Text("ImGuizmo IsOver: %s", ImGuizmo::IsOver() ? "YES" : "NO");
                    ImGui::Text("Gizmo Rect: (0, 0) to (%.0f, %.0f)", (float)swap.extent.width, (float)swap.extent.height);
                            ImGui::Separator();
                            ImGui::Checkbox("Gizmo Follow Camera", &gizmoFollowCamera);
                    
                    break;
                }
            }
            
            if (!hasSelection) {
                ImGui::TextDisabled("No object selected");
                ImGui::Text("Click on an object in the viewport to select it");
            }
            ImGui::End();
        }
        
        // ImGuizmo keyboard shortcuts
        if (ImGui::IsKeyPressed(ImGuiKey_T)) currentGizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_S) && !ImGui::GetIO().KeyCtrl) currentGizmoOperation = ImGuizmo::SCALE;
        
        // Build view/proj for ImGuizmo
        UniformBufferObject ubo = camera.makeUBO(swap.extent);
        
        // Setup ImGuizmo - use only the render viewport, not the full window
        ImGuizmo::SetDrawlist();
        // Set rect to the full viewport area in ImGui screen coordinates
        ImGuizmo::SetRect(0, 0, (float)swap.extent.width, (float)swap.extent.height);
        ImGuizmo::Enable(true);
        
        // For ImGuizmo to not move with camera, we need OpenGL-style coordinates
        // Create projection matrix in OpenGL space (Y-up, Z from -1 to 1)
        float aspect = swap.extent.width / (float)swap.extent.height;
        Mat4 gizmoProj = Mat4::PerspectiveGL(60.0f * 3.1415926535f / 180.0f, aspect, 0.1f, 100.0f);
        
        // Also need to flip Y in the view matrix for OpenGL conventions
        Mat4 gizmoView;
        if (gizmoFollowCamera) {
            // Use camera view but convert to OpenGL convention (flip Y column)
            gizmoView = ubo.view;
            gizmoView.m[1] = -gizmoView.m[1];
            gizmoView.m[5] = -gizmoView.m[5];
            gizmoView.m[9] = -gizmoView.m[9];
            gizmoView.m[13] = -gizmoView.m[13];
        } else {
            // Use identity view (camera-independent) but still place the gizmo in world space.
            // Start with identity, then flip Y to match ImGuizmo's expected OpenGL convention.
            gizmoView = Mat4::Identity();
            gizmoView.m[1] = -gizmoView.m[1];
            gizmoView.m[5] = -gizmoView.m[5];
            gizmoView.m[9] = -gizmoView.m[9];
            gizmoView.m[13] = -gizmoView.m[13];
        }
        
        // Render gizmo for selected object (always call Manipulate so it draws)
        for (auto& obj : sceneObjects) {
            if (obj.selected) {
                Mat4 objectMatrix = obj.transform.GetMatrix();

                ImGuizmo::Manipulate(
                    gizmoView.m,
                    gizmoProj.m,
                    currentGizmoOperation,
                    currentGizmoMode,
                    objectMatrix.m,
                    nullptr,
                    nullptr,
                    nullptr);
                
                // If manipulated, update transform
                if (ImGuizmo::IsUsing()) {
                    obj.transform.SetMatrix(objectMatrix);
                }
                break;
            }
        }
        
        // Render ImGui
        ImGui::Render();

        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(vkdev.device);
            swap.Recreate(window);
            createPipeline();
            allocateCmdBuffers();
            createDescriptors();
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
    // Bind descriptor set with UBO
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout,
                0, 1, &descriptorSets[imageIndex], 0, nullptr);

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
        
        // Draw all scene objects
        for (const auto& obj : sceneObjects) {
            Mat4 modelMatrix = obj.transform.GetMatrix();
            // Push constants for model matrix (we'll need to add this to the pipeline)
            vkCmdPushConstants(cmd, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), modelMatrix.m);
            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(cubeIndices.size()), 1, 0, 0, 0);
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
            allocateCmdBuffers();
            createDescriptors();
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

    vertexBuffer.Destroy(vkdev.allocator);
    indexBuffer.Destroy(vkdev.allocator);
    destroyDescriptors();
    pipeline.Cleanup();
    swap.Cleanup();
    vkdev.Cleanup();
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
