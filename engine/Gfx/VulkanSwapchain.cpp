#include "VulkanSwapchain.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>

VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available)
{
    if (available.empty()) {
        throw std::runtime_error("Surface reports no formats");
    }
    if (available.size() == 1 && available[0].format == VK_FORMAT_UNDEFINED) {
        return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    }
    const VkFormat preferred[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB };
    for (VkFormat want : preferred) {
        for (const auto& f : available) {
            if (f.format == want && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return f;
            }
        }
    }
    return available[0];
}

bool IsSrgbFormat(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return true;
    default:
        return false;
    }
}

static VkFormat findSupportedFormat(VkPhysicalDevice phys,
                                    const std::vector<VkFormat>& candidates,
                                    VkImageTiling tiling,
                                    VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(phys, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            return format;
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            return format;
    }
    throw std::runtime_error("Failed to find supported format");
}

static VkFormat findDepthFormat(VkPhysicalDevice phys)
{
    return findSupportedFormat(
        phys,
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

static void createImage2D(VkDevice device, VmaAllocator allocator,
                          uint32_t width, uint32_t height, VkFormat format,
                          VkImageUsageFlags usage,
                          VkImage& image, VmaAllocation& allocation)
{
    VkImageCreateInfo img{};
    img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img.imageType = VK_IMAGE_TYPE_2D;
    img.extent = { width, height, 1 };
    img.mipLevels = 1;
    img.arrayLayers = 1;
    img.format = format;
    img.tiling = VK_IMAGE_TILING_OPTIMAL;
    img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img.usage = usage;
    img.samples = VK_SAMPLE_COUNT_1_BIT;
    img.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(allocator, &img, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image");
}

static VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect)
{
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange.aspectMask = aspect;
    view.subresourceRange.baseMipLevel = 0;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.baseArrayLayer = 0;
    view.subresourceRange.layerCount = 1;
    VkImageView iv{};
    if (vkCreateImageView(device, &view, nullptr, &iv) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image view");
    return iv;
}

void VulkanSwapchain::Init(VkPhysicalDevice phys, VkDevice dev, VkSurfaceKHR surf, GLFWwindow* window,
                           VmaAllocator alloc, bool vsync)
{
    physicalDevice = phys;
    device = dev;
    surface = surf;
    allocator = alloc;
    m_vsync = vsync;

    // ── Choose extent (window size) from GLFW ────────────────────────────────────
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    
    initInternal(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

void VulkanSwapchain::InitFromNativeSurface(VkPhysicalDevice phys, VkDevice dev, VkSurfaceKHR surf, 
                                             uint32_t width, uint32_t height, VmaAllocator alloc)
{
    physicalDevice = phys;
    device = dev;
    surface = surf;
    allocator = alloc;
    
    initInternal(width, height);
}

void VulkanSwapchain::initInternal(uint32_t width, uint32_t height)
{
    // ── Query surface capabilities ──────────────────────────────────────
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);
    
    // Clamp requested extent to the surface's supported range
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width  = std::clamp(width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
        extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    
    uint32_t imageCount = std::max(2u, caps.minImageCount);
    if (caps.maxImageCount > 0) {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    // ── Choose surface format and present mode ─────────────────────────
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    format = surfaceFormat.format;

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // VSync, guaranteed available
    if (!m_vsync) {
        const auto mailbox = std::find(presentModes.begin(), presentModes.end(),
                                       VK_PRESENT_MODE_MAILBOX_KHR);
        const auto immediate = std::find(presentModes.begin(), presentModes.end(),
                                         VK_PRESENT_MODE_IMMEDIATE_KHR);
        if (mailbox != presentModes.end()) {
            presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        } else if (immediate != presentModes.end()) {
            presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }

    // ── Create swapchain ───────────────────────────────────────────────
    VkSwapchainCreateInfoKHR swapInfo{};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = surface;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = format;
    swapInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapInfo.imageExtent = extent;
    swapInfo.imageArrayLayers = 1;
    supportsCapture = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          (supportsCapture ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.preTransform = caps.currentTransform;
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        for (auto flag : { VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR }) {
            if (caps.supportedCompositeAlpha & flag) { compositeAlpha = flag; break; }
        }
    }
    swapInfo.compositeAlpha = compositeAlpha;
    swapInfo.presentMode = presentMode;
    swapInfo.clipped = VK_TRUE;
    swapInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &swapInfo, nullptr, &swapchain) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swapchain");

    // ── Get images ────────────────────────────────────────────────────
    uint32_t actualImageCount;
    vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, nullptr);
    images.resize(actualImageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, images.data());

    // ── Create image views ─────────────────────────────────────────────
    imageViews.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &imageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image view");
    }

    // ── Depth format and image ─────────────────────────────────────────
    depthFormat = findDepthFormat(physicalDevice);
    createImage2D(device, allocator, extent.width, extent.height, depthFormat,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                  depthImage, depthAllocation);
    depthImageView = createImageView(device, depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    createImage2D(device, allocator, extent.width, extent.height, kHdrFormat,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                  hdrImage, hdrAllocation);
    hdrImageView = createImageView(device, hdrImage, kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT);

    // ── Create render pass: scene → HDR, then tone map + overlays → swapchain ──
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription hdrAttachment{};
    hdrAttachment.format = kHdrFormat;
    hdrAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    hdrAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    hdrAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    hdrAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    hdrAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    hdrAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    hdrAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference hdrWriteRef{};
    hdrWriteRef.attachment = 2;
    hdrWriteRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference hdrReadRef{};
    hdrReadRef.attachment = 2;
    hdrReadRef.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkSubpassDescription subpasses[2]{};
    subpasses[kSceneSubpass].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[kSceneSubpass].colorAttachmentCount = 1;
    subpasses[kSceneSubpass].pColorAttachments = &hdrWriteRef;
    subpasses[kSceneSubpass].pDepthStencilAttachment = &depthRef;

    subpasses[kOverlaySubpass].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[kOverlaySubpass].inputAttachmentCount = 1;
    subpasses[kOverlaySubpass].pInputAttachments = &hdrReadRef;
    subpasses[kOverlaySubpass].colorAttachmentCount = 1;
    subpasses[kOverlaySubpass].pColorAttachments = &colorRef;
    subpasses[kOverlaySubpass].pDepthStencilAttachment = &depthRef;

    // HDR and depth images are shared by all frames in flight: the previous frame must finish with them first.
    VkSubpassDependency deps[3]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = kSceneSubpass;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = kSceneSubpass;
    deps[1].dstSubpass = kOverlaySubpass;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    deps[2].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[2].dstSubpass = kOverlaySubpass;
    deps[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[2].srcAccessMask = 0;
    deps[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[3] = { colorAttachment, depthAttachment, hdrAttachment };
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 3;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 2;
    renderPassInfo.pSubpasses = subpasses;
    renderPassInfo.dependencyCount = 3;
    renderPassInfo.pDependencies = deps;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create render pass");

    // ── Create framebuffers (color + depth + HDR) ─────────────────────
    framebuffers.resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); ++i) {
        VkImageView attachments[] = { imageViews[i], depthImageView, hdrImageView };
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 3;
        fbInfo.pAttachments = attachments;
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create framebuffer");
    }

    std::cout << "Swapchain created with " << framebuffers.size() << " framebuffers.\n";
}

void VulkanSwapchain::Cleanup()
{
    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers.clear();
    if (renderPass) { vkDestroyRenderPass(device, renderPass, nullptr); renderPass = VK_NULL_HANDLE; }
    for (auto view : imageViews) vkDestroyImageView(device, view, nullptr);
    imageViews.clear();
    if (swapchain) { vkDestroySwapchainKHR(device, swapchain, nullptr); swapchain = VK_NULL_HANDLE; }
    if (depthImageView) { vkDestroyImageView(device, depthImageView, nullptr); depthImageView = VK_NULL_HANDLE; }
    if (depthImage) {
        vmaDestroyImage(allocator, depthImage, depthAllocation);
        depthImage = VK_NULL_HANDLE;
        depthAllocation = nullptr;
    }
    if (hdrImageView) { vkDestroyImageView(device, hdrImageView, nullptr); hdrImageView = VK_NULL_HANDLE; }
    if (hdrImage) {
        vmaDestroyImage(allocator, hdrImage, hdrAllocation);
        hdrImage = VK_NULL_HANDLE;
        hdrAllocation = nullptr;
    }
}

void VulkanSwapchain::Recreate(GLFWwindow* window)
{
    // Wait for non-zero size (minimized window handling)
    int w = 0, h = 0;
    do {
        glfwGetFramebufferSize(window, &w, &h);
        if (w == 0 || h == 0) glfwWaitEvents();
    } while (w == 0 || h == 0);

    Cleanup();
    // Re-init with same device/surface/allocator
    Init(physicalDevice, device, surface, window, allocator, m_vsync);
}

void VulkanSwapchain::RecreateFromNativeSurface(uint32_t width, uint32_t height)
{
    // Skip recreation if zero size (minimized window)
    if (width == 0 || height == 0) {
        std::cout << "[Swapchain] Skipping recreation for zero-size surface\n";
        return;
    }

    Cleanup();
    // Re-init with same device/surface/allocator but new dimensions
    initInternal(width, height);
}
