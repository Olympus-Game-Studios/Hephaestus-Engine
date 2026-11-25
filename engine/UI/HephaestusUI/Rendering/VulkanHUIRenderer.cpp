#include "VulkanHUIRenderer.h"

#include "../UIDynamicImages.h"

#include "../../../Gfx/VulkanDevice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace Hephaestus::HephaestusUI {

namespace fs = std::filesystem;

namespace {

VkResult CreateOneShotCommandBuffer(VkDevice device, VkCommandPool pool, VkQueue queue,
                                    std::function<void(VkCommandBuffer)> record) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &cmd);
    if (result != VK_SUCCESS) {
        return result;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(cmd, &beginInfo);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device, pool, 1, &cmd);
        return result;
    }

    record(cmd);

    result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device, pool, 1, &cmd);
        return result;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    result = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result == VK_SUCCESS) {
        vkQueueWaitIdle(queue);
    }
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    return result;
}

void TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           VkImageSubresourceRange range) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = range;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkRect2D ToVkRect2D(const UIRect& rect, const UIVec2& viewport) {
    const float x0 = std::max(0.0f, std::min(rect.x, viewport.x));
    const float y0 = std::max(0.0f, std::min(rect.y, viewport.y));
    const float x1 = std::max(0.0f, std::min(rect.x + rect.width, viewport.x));
    const float y1 = std::max(0.0f, std::min(rect.y + rect.height, viewport.y));
    VkRect2D out{};
    out.offset.x = static_cast<int32_t>(std::floor(x0));
    out.offset.y = static_cast<int32_t>(std::floor(y0));
    out.extent.width = static_cast<uint32_t>(std::ceil(x1)) - out.offset.x;
    out.extent.height = static_cast<uint32_t>(std::ceil(y1)) - out.offset.y;
    return out;
}

bool SameRect2D(const VkRect2D& a, const VkRect2D& b) {
    return a.offset.x == b.offset.x && a.offset.y == b.offset.y &&
           a.extent.width == b.extent.width && a.extent.height == b.extent.height;
}

uint32_t NextPowerOfTwo(uint32_t value) {
    uint32_t v = value;
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

VulkanHUIRenderer::~VulkanHUIRenderer() {
    Shutdown();
}

bool VulkanHUIRenderer::Init(VulkanDevice* device, VkRenderPass renderPass,
                             uint32_t subpass, VkFormat swapchainFormat,
                             const VkExtent2D& extent, uint32_t swapchainImageCount) {
    if (!device) {
        return false;
    }
    m_Device = device;
    m_Allocator = device->allocator;
    m_RenderPass = renderPass;
    m_Subpass = subpass;
    m_SwapchainImageCount = swapchainImageCount;
    m_SrgbOutput = swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                   swapchainFormat == VK_FORMAT_R8G8B8A8_SRGB ||
                   swapchainFormat == VK_FORMAT_A8B8G8R8_SRGB_PACK32 ||
                   swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB;
    m_Viewport = {static_cast<float>(extent.width), static_cast<float>(extent.height)};

    // Slot 0 = solid white texture (used for solid-color quads and placeholders).
    m_Textures.clear();
    m_Textures.push_back({});
    UITextureEntry& white = m_Textures.back();
    if (!white.texture.CreateSolidColor(m_Allocator, m_Device->device,
                                        m_Device->physicalDevice,
                                        m_Device->commandPool,
                                        m_Device->graphicsQueue, 0xFFFFFFFFu)) {
        std::cerr << "[HUI] Failed to create UI white texture\n";
        return false;
    }
    white.width = white.texture.width;
    white.height = white.texture.height;
    white.valid = true;

    if (!createDescriptorResources()) {
        std::cerr << "[HUI] Failed to create descriptor resources\n";
        return false;
    }
    WriteDescriptorSet(0);

    if (!createFrameBuffers()) {
        std::cerr << "[HUI] Failed to create frame buffers\n";
        return false;
    }
    if (!createPipeline(renderPass)) {
        std::cerr << "[HUI] Failed to create UI pipeline\n";
        return false;
    }

    m_Initialized = true;
    std::cout << "[HUI] Vulkan UI backend initialized\n";
    return true;
}

void VulkanHUIRenderer::OnSwapchainRecreated(VkRenderPass renderPass,
                                             uint32_t subpass,
                                             VkFormat swapchainFormat,
                                             const VkExtent2D& extent,
                                             uint32_t swapchainImageCount) {
    if (!m_Initialized) return;

    destroyPipelineResources();
    destroyFrameBuffers();

    m_RenderPass = renderPass;
    m_Subpass = subpass;
    m_SwapchainImageCount = swapchainImageCount;
    m_SrgbOutput = swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                   swapchainFormat == VK_FORMAT_R8G8B8A8_SRGB ||
                   swapchainFormat == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
    m_Viewport = {static_cast<float>(extent.width), static_cast<float>(extent.height)};

    if (!createFrameBuffers()) {
        std::cerr << "[HUI] Failed to recreate UI frame buffers\n";
    }
    if (!createPipeline(renderPass)) {
        std::cerr << "[HUI] Failed to recreate UI pipeline\n";
    }
}

void VulkanHUIRenderer::OnSwapchainDestroyed() {
    destroyPipelineResources();
    destroyFrameBuffers();
}

void VulkanHUIRenderer::Shutdown() {
    if (!m_Device) return;

    vkDeviceWaitIdle(m_Device->device);

    destroyPipelineResources();
    destroyFrameBuffers();
    destroyDescriptorResources();
    destroyWhiteTexture();

    for (auto& entry : m_Textures) {
        entry.texture.Destroy(m_Allocator, m_Device->device);
    }
    m_Textures.clear();
    m_TextureLookup.clear();
    m_MissingAssetsLogged.clear();

    for (auto& retired : m_RetiredTextures) {
        retired.texture.Destroy(m_Allocator, m_Device->device);
    }
    m_RetiredTextures.clear();

    m_Atlases.clear();
    m_MissingFontsLogged.clear();

    m_Device = nullptr;
    m_Allocator = VK_NULL_HANDLE;
    m_Initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// IHUIRenderBackend
// ─────────────────────────────────────────────────────────────────────────────

void VulkanHUIRenderer::SetViewportSize(const UIVec2& viewportSize) {
    m_Viewport = viewportSize;
}

void VulkanHUIRenderer::SetProjectRoot(const std::string& projectRoot) {
    m_ProjectRoot = projectRoot;
    m_FontSearchPaths = HUIFontSearchPaths(projectRoot);
}

void VulkanHUIRenderer::SetDrawCommands(const std::vector<UIDrawCommand>& commands) {
    m_Commands = &commands;
}

void VulkanHUIRenderer::ClearDrawCommands() {
    m_Commands = nullptr;
    m_GeometryReady = false;
}

void VulkanHUIRenderer::PrepareFrame() {
    m_Data = UIBatchData{};
    m_GeometryReady = false;
    if (!m_Initialized || !m_Commands || m_Commands->empty()) {
        return;
    }

    ++m_FrameCounter;
    ReapRetiredTextures();

    const UITextureResolver resolver = [this](const std::string& imageId) {
        return ResolveTexture(imageId);
    };
    m_Data = m_GeometryBuilder.Build(*m_Commands, m_Viewport, resolver, *this);
    UploadDirtyAtlases();
    m_GeometryReady = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Font atlas provider
// ─────────────────────────────────────────────────────────────────────────────

UIFontAtlas* VulkanHUIRenderer::GetOrCreateAtlas(const UIFontKey& key) {
    auto it = m_Atlases.find(key);
    if (it != m_Atlases.end()) {
        return it->second.atlas.get();
    }

    const std::string fontFile =
        UIFontAtlas::ResolveFontFile(key.family, key.bold, key.italic, m_FontSearchPaths);
    if (fontFile.empty()) {
        if (!m_MissingFontsLogged[key]) {
            std::cerr << "[HUI] No font file found for family '" << key.family
                      << "' (size " << key.size << ", bold=" << key.bold
                      << ", italic=" << key.italic << ")\n";
            m_MissingFontsLogged[key] = true;
        }
        return nullptr;
    }

    auto atlas = std::make_unique<UIFontAtlas>();
    if (!atlas->LoadFromFile(fontFile, key.size)) {
        if (!m_MissingFontsLogged[key]) {
            std::cerr << "[HUI] Failed to load font file: " << fontFile << "\n";
            m_MissingFontsLogged[key] = true;
        }
        return nullptr;
    }

    const uint32_t slot = ReserveTextureSlot(UIFontAtlas::kAtlasSize, UIFontAtlas::kAtlasSize);
    atlas->SetTextureHandle(static_cast<int64_t>(slot));
    AtlasEntry entry;
    entry.atlas = std::move(atlas);
    entry.slot = slot;

    auto result = m_Atlases.emplace(key, std::move(entry));
    std::cout << "[HUI] Font atlas created: '" << key.family << "' " << key.size
              << "px (bold=" << key.bold << ", italic=" << key.italic
              << ") from " << fontFile << "\n";
    return result.first->second.atlas.get();
}

void VulkanHUIRenderer::UploadDirtyAtlases() {
    for (auto& [key, entry] : m_Atlases) {
        if (!entry.atlas || !entry.atlas->IsDirty()) continue;
        if (entry.slot >= m_Textures.size()) continue;

        UITextureEntry& texEntry = m_Textures[entry.slot];

        // Retire the previous GPU atlas texture so it is only destroyed after
        // in-flight frames that sample it have completed.
        if (texEntry.valid && texEntry.texture.image != VK_NULL_HANDLE) {
            RetiredTexture retired;
            retired.texture = texEntry.texture;
            retired.set = texEntry.set;
            retired.retireFrame = m_FrameCounter;
            m_RetiredTextures.push_back(retired);
            texEntry.texture = VulkanTexture{};
            texEntry.set = VK_NULL_HANDLE;
            texEntry.valid = false;
        }

        if (!UploadPixelsToTexture(texEntry.texture, entry.atlas->GetPixels(),
                                   UIFontAtlas::kAtlasSize, UIFontAtlas::kAtlasSize)) {
            std::cerr << "[HUI] Failed to create font atlas texture for '" << key.family << "'\n";
            entry.atlas->ClearDirty();
            continue;
        }
        texEntry.valid = true;
        WriteDescriptorSet(entry.slot);
        entry.atlas->ClearDirty();
    }
}

void VulkanHUIRenderer::ReapRetiredTextures() {
    if (!m_Device) return;
    for (auto it = m_RetiredTextures.begin(); it != m_RetiredTextures.end();) {
        if (m_FrameCounter >= it->retireFrame + kMaxFramesInFlight) {
            it->texture.Destroy(m_Allocator, m_Device->device);
            it = m_RetiredTextures.erase(it);
        } else {
            ++it;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture cache
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VulkanHUIRenderer::ReserveTextureSlot(uint32_t width, uint32_t height) {
    const uint32_t slot = static_cast<uint32_t>(m_Textures.size());
    m_Textures.push_back({});
    m_Textures[slot].width = width;
    m_Textures[slot].height = height;
    return slot;
}

std::string VulkanHUIRenderer::ResolveAssetPath(const std::string& imageId) const {
    return ResolveHUIImagePath(m_ProjectRoot, imageId);
}

UITextureInfo VulkanHUIRenderer::ResolveTexture(const std::string& imageId) {
    if (imageId.empty()) {
        return {-1, 1, 1};
    }
    if (UIDynamicImages::Instance().Version(imageId) != 0) {
        return ResolveDynamicTexture(imageId);
    }

    std::string absPath = ResolveAssetPath(imageId);
    if (absPath.empty()) {
        absPath = imageId;
    }

    const auto lookup = m_TextureLookup.find(absPath);
    if (lookup != m_TextureLookup.end()) {
        const UITextureEntry& entry = m_Textures[lookup->second];
        return {static_cast<int32_t>(lookup->second), entry.width, entry.height};
    }

    VulkanTexture texture;
    if (!texture.LoadFromFile(m_Allocator, m_Device->device, m_Device->physicalDevice,
                              m_Device->commandPool, m_Device->graphicsQueue, absPath, false, m_SrgbOutput)) {
        if (!m_MissingAssetsLogged[absPath]) {
            std::cerr << "[HUI] Missing image asset: " << imageId
                      << " (resolved to " << absPath << ")\n";
            m_MissingAssetsLogged[absPath] = true;
        }
        return {-1, 1, 1};
    }

    const uint32_t slot = ReserveTextureSlot(texture.width, texture.height);
    m_Textures[slot].texture = std::move(texture);
    m_Textures[slot].width = m_Textures[slot].texture.width;
    m_Textures[slot].height = m_Textures[slot].texture.height;
    m_Textures[slot].valid = true;
    WriteDescriptorSet(slot);
    m_TextureLookup[absPath] = slot;

    std::cout << "[HUI] UI texture loaded: " << absPath << " (slot " << slot << ")\n";
    return {static_cast<int32_t>(slot), m_Textures[slot].width, m_Textures[slot].height};
}

UITextureInfo VulkanHUIRenderer::ResolveDynamicTexture(const std::string& imageId) {
    auto found = m_DynamicTextures.find(imageId);
    UIDynamicImages::Image image;
    const uint64_t known = found != m_DynamicTextures.end() ? found->second.version : 0;
    if (UIDynamicImages::Instance().CopyIfNewer(imageId, known, image)) {
        if (found != m_DynamicTextures.end() && m_Textures[found->second.slot].valid &&
            m_Textures[found->second.slot].width == image.width && m_Textures[found->second.slot].height == image.height) {
            // Same size: overwrite in place. Frames still in flight may sample it, so let them finish first; this
            // only runs while a dynamic image (a video) is changing.
            vkQueueWaitIdle(m_Device->graphicsQueue);
            CopyPixelsToTexture(m_Textures[found->second.slot].texture, image.rgba.data(), image.width, image.height);
            found->second.version = image.version;
        } else {
            uint32_t slot;
            if (found != m_DynamicTextures.end()) { // a new size: retire the old texture, as atlases do
                slot = found->second.slot;
                UITextureEntry& old = m_Textures[slot];
                if (old.valid && old.texture.image != VK_NULL_HANDLE) {
                    RetiredTexture retired;
                    retired.texture = old.texture;
                    retired.set = old.set;
                    retired.retireFrame = m_FrameCounter;
                    m_RetiredTextures.push_back(retired);
                }
                old.texture = VulkanTexture{};
                old.set = VK_NULL_HANDLE;
                old.valid = false;
            } else {
                slot = ReserveTextureSlot(image.width, image.height);
            }
            UITextureEntry& entry = m_Textures[slot];
            if (!UploadPixelsToTexture(entry.texture, image.rgba.data(), image.width, image.height)) {
                std::cerr << "[HUI] Failed to create dynamic image texture '" << imageId << "'\n";
                return {-1, 1, 1};
            }
            entry.width = image.width;
            entry.height = image.height;
            entry.valid = true;
            WriteDescriptorSet(slot);
            found = m_DynamicTextures.insert_or_assign(imageId, DynamicTextureEntry{slot, image.version}).first;
        }
    }
    if (found == m_DynamicTextures.end()) return {-1, 1, 1};
    const UITextureEntry& entry = m_Textures[found->second.slot];
    return {static_cast<int32_t>(found->second.slot), entry.width, entry.height};
}

VkDescriptorSet VulkanHUIRenderer::GetDescriptorSetForSlot(int32_t slot) const {
    if (slot > 0 && slot < static_cast<int32_t>(m_Textures.size())) {
        const UITextureEntry& entry = m_Textures[static_cast<std::size_t>(slot)];
        if (entry.valid && entry.set != VK_NULL_HANDLE) {
            return entry.set;
        }
    }
    // Fall back to the white texture for invalid/missing slots.
    if (!m_Textures.empty()) {
        return m_Textures[0].set;
    }
    return VK_NULL_HANDLE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor resources
// ─────────────────────────────────────────────────────────────────────────────

bool VulkanHUIRenderer::createDescriptorResources() {
    if (!m_Device) return false;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(m_Device->device, &layoutInfo, nullptr,
                                    &m_DescriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    constexpr uint32_t kMaxSets = 512;
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = kMaxSets;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kMaxSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(m_Device->device, &poolInfo, nullptr,
                               &m_DescriptorPool) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(m_Device->device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VulkanHUIRenderer::WriteDescriptorSet(uint32_t slot) {
    if (!m_Device || slot >= m_Textures.size()) return;
    UITextureEntry& entry = m_Textures[slot];
    if (!entry.valid || entry.texture.image == VK_NULL_HANDLE) return;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;
    if (vkAllocateDescriptorSets(m_Device->device, &allocInfo, &entry.set) != VK_SUCCESS) {
        std::cerr << "[HUI] Failed to allocate descriptor set for texture slot " << slot << "\n";
        return;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = entry.texture.imageView;
    imageInfo.sampler = entry.texture.sampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = entry.set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_Device->device, 1, &write, 0, nullptr);
}

void VulkanHUIRenderer::destroyDescriptorResources() {
    if (!m_Device) return;
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device->device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_Device->device, m_DescriptorSetLayout, nullptr);
        m_DescriptorSetLayout = VK_NULL_HANDLE;
    }
    for (auto& entry : m_Textures) {
        entry.set = VK_NULL_HANDLE;
    }
}

void VulkanHUIRenderer::destroyWhiteTexture() {
    if (!m_Device || m_Textures.empty()) return;
    m_Textures[0].texture.Destroy(m_Allocator, m_Device->device);
    m_Textures[0].valid = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame buffers (per swapchain image)
// ─────────────────────────────────────────────────────────────────────────────

bool VulkanHUIRenderer::createFrameBuffers() {
    m_FrameBuffers.resize(m_SwapchainImageCount);
    return !m_FrameBuffers.empty();
}

bool VulkanHUIRenderer::EnsureFrameBuffers(uint32_t imageIndex, uint32_t vertexCount,
                                           uint32_t indexCount) {
    if (!m_Device || imageIndex >= m_FrameBuffers.size()) return false;
    FrameBuffers& fb = m_FrameBuffers[imageIndex];

    if (vertexCount > fb.vertexCapacity) {
        fb.vertex.Destroy(m_Allocator);
        const uint32_t capacity = std::max(4096u, NextPowerOfTwo(vertexCount));
        try {
            fb.vertex.Create(m_Allocator, static_cast<VkDeviceSize>(capacity) * sizeof(UIVertex),
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        } catch (const std::exception&) {
            std::cerr << "[HUI] Failed to allocate UI vertex buffer\n";
            return false;
        }
        fb.vertexCapacity = capacity;
    }
    if (indexCount > fb.indexCapacity) {
        fb.index.Destroy(m_Allocator);
        const uint32_t capacity = std::max(8192u, NextPowerOfTwo(indexCount));
        try {
            fb.index.Create(m_Allocator, static_cast<VkDeviceSize>(capacity) * sizeof(uint32_t),
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        } catch (const std::exception&) {
            std::cerr << "[HUI] Failed to allocate UI index buffer\n";
            return false;
        }
        fb.indexCapacity = capacity;
    }
    return true;
}

void VulkanHUIRenderer::destroyFrameBuffers() {
    if (!m_Device) return;
    for (FrameBuffers& fb : m_FrameBuffers) {
        fb.vertex.Destroy(m_Allocator);
        fb.index.Destroy(m_Allocator);
        fb.vertexCapacity = 0;
        fb.indexCapacity = 0;
    }
    m_FrameBuffers.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline
// ─────────────────────────────────────────────────────────────────────────────

VkShaderModule VulkanHUIRenderer::LoadShaderModule(VkDevice device, const std::string& name) {
    const std::vector<std::string> candidates = {
        "shaders/" + name,
        "../shaders/" + name,
        "build/Release/shaders/" + name,
        "../build/Release/shaders/" + name,
        "build/Debug/shaders/" + name,
        "../build/Debug/shaders/" + name,
    };

    std::vector<char> code;
    for (const std::string& path : candidates) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) continue;
        const std::streamsize size = file.tellg();
        if (size <= 0) continue;
        code.resize(static_cast<std::size_t>(size));
        file.seekg(0, std::ios::beg);
        file.read(code.data(), size);
        if (file.good() || file.eof()) break;
        code.clear();
    }
    if (code.empty()) {
        std::cerr << "[HUI] Failed to load shader: " << name << "\n";
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        std::cerr << "[HUI] Failed to create shader module: " << name << "\n";
        return VK_NULL_HANDLE;
    }
    return module;
}

bool VulkanHUIRenderer::createPipeline(VkRenderPass renderPass) {
    if (!m_Device) return false;
    m_RenderPass = renderPass;

    VkShaderModule vertModule = LoadShaderModule(m_Device->device, "hui.vert.spv");
    VkShaderModule fragModule = LoadShaderModule(m_Device->device, "hui.frag.spv");
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        if (vertModule) vkDestroyShaderModule(m_Device->device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(m_Device->device, fragModule, nullptr);
        return false;
    }

    // Pipeline layout: one descriptor set + viewport push constant.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_DescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(m_Device->device, &layoutInfo, nullptr,
                               &m_PipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(m_Device->device, vertModule, nullptr);
        vkDestroyShaderModule(m_Device->device, fragModule, nullptr);
        return false;
    }

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(UIVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 5> attributes{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, posX)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, u)};
    attributes[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, r)};
    attributes[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, rectMinX)};
    attributes[4] = {4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, cornerRadius)};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkShaderModule stageModules[2] = {vertModule, fragModule};
    const VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_VERTEX_BIT, stageModules[0], "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, stageModules[1], "main", nullptr},
    };

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = m_Subpass;

    const VkResult result = vkCreateGraphicsPipelines(m_Device->device, VK_NULL_HANDLE, 1,
                                                      &pipelineInfo, nullptr, &m_Pipeline);
    vkDestroyShaderModule(m_Device->device, vertModule, nullptr);
    vkDestroyShaderModule(m_Device->device, fragModule, nullptr);
    if (result != VK_SUCCESS) {
        vkDestroyPipelineLayout(m_Device->device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
        return false;
    }

    std::cout << "[HUI] UI pipeline created\n";
    return true;
}

void VulkanHUIRenderer::destroyPipelineResources() {
    if (!m_Device) return;
    if (m_Pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device->device, m_Pipeline, nullptr);
        m_Pipeline = VK_NULL_HANDLE;
    }
    if (m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device->device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Atlas / texture upload
// ─────────────────────────────────────────────────────────────────────────────

bool VulkanHUIRenderer::UploadPixelsToTexture(VulkanTexture& texture, const uint8_t* pixels,
                                              uint32_t width, uint32_t height) {
    if (!m_Device || !pixels) return false;

    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    if (!texture.Create(m_Allocator, m_Device->device, width, height, format,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        return false;
    }
    if (!texture.CreateImageView(m_Device->device, format, VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    if (vkCreateSampler(m_Device->device, &samplerInfo, nullptr,
                        &texture.sampler) != VK_SUCCESS) {
        return false;
    }
    texture.width = width;
    texture.height = height;
    texture.mipLevels = 1;
    return CopyPixelsToTexture(texture, pixels, width, height);
}

bool VulkanHUIRenderer::CopyPixelsToTexture(VulkanTexture& texture, const uint8_t* pixels, uint32_t width,
                                            uint32_t height) {
    if (!m_Device || !pixels || texture.image == VK_NULL_HANDLE) return false;

    // Stage the full atlas/region.
    VulkanBuffer staging;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
    staging.Create(m_Allocator, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* mapped = nullptr;
    vmaMapMemory(m_Allocator, staging.allocation, &mapped);
    std::memcpy(mapped, pixels, static_cast<std::size_t>(bytes));
    vmaUnmapMemory(m_Allocator, staging.allocation);

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    VkResult result = CreateOneShotCommandBuffer(
        m_Device->device, m_Device->commandPool, m_Device->graphicsQueue, [&](VkCommandBuffer cmd) {
            TransitionImageLayout(cmd, texture.image,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {width, height, 1};
            vkCmdCopyBufferToImage(cmd, staging.buffer, texture.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            TransitionImageLayout(cmd, texture.image,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);
        });
    staging.Destroy(m_Allocator);

    return result == VK_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording
// ─────────────────────────────────────────────────────────────────────────────

void VulkanHUIRenderer::RecordDraw(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!m_Initialized || !m_GeometryReady || m_Data.batches.empty()) return;
    if (m_Pipeline == VK_NULL_HANDLE || m_PipelineLayout == VK_NULL_HANDLE) return;
    if (!m_Device) return;

    const uint32_t vertexCount = static_cast<uint32_t>(m_Data.vertices.size());
    const uint32_t indexCount = static_cast<uint32_t>(m_Data.indices.size());
    if (vertexCount == 0 || indexCount == 0) return;

    if (!EnsureFrameBuffers(imageIndex, vertexCount, indexCount)) return;
    FrameBuffers& fb = m_FrameBuffers[imageIndex];

    // Upload geometry (safe: imageIndex's previous command buffer has completed).
    if (!m_Data.vertices.empty()) {
        void* mapped = nullptr;
        vmaMapMemory(m_Allocator, fb.vertex.allocation, &mapped);
        std::memcpy(mapped, m_Data.vertices.data(),
                    static_cast<std::size_t>(vertexCount) * sizeof(UIVertex));
        vmaUnmapMemory(m_Allocator, fb.vertex.allocation);
    }
    if (!m_Data.indices.empty()) {
        void* mapped = nullptr;
        vmaMapMemory(m_Allocator, fb.index.allocation, &mapped);
        std::memcpy(mapped, m_Data.indices.data(),
                    static_cast<std::size_t>(indexCount) * sizeof(uint32_t));
        vmaUnmapMemory(m_Allocator, fb.index.allocation);
    }

    PushConstants pc{};
    pc.viewportW = m_Viewport.x;
    pc.viewportH = m_Viewport.y;
    pc.srgbOutput = m_SrgbOutput ? 1.0f : 0.0f;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    vkCmdPushConstants(cmd, m_PipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = m_Viewport.x;
    viewport.height = m_Viewport.y;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D fullScissor{};
    fullScissor.offset = {0, 0};
    fullScissor.extent = {static_cast<uint32_t>(std::max(1.0f, m_Viewport.x)),
                          static_cast<uint32_t>(std::max(1.0f, m_Viewport.y))};
    vkCmdSetScissor(cmd, 0, 1, &fullScissor);

    const VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &fb.vertex.buffer, &vertexOffset);
    vkCmdBindIndexBuffer(cmd, fb.index.buffer, 0, VK_INDEX_TYPE_UINT32);

    int32_t lastSlot = -2;
    VkRect2D lastScissor{};
    lastScissor.offset = {-1, -1};
    lastScissor.extent = {0, 0};

    uint32_t drawCalls = 0;
    for (const UIBatch& batch : m_Data.batches) {
        if (batch.textureSlot != lastSlot) {
            VkDescriptorSet set = GetDescriptorSetForSlot(batch.textureSlot);
            if (set != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                                        0, 1, &set, 0, nullptr);
            }
            lastSlot = batch.textureSlot;
        }

        const VkRect2D scissor = ToVkRect2D(batch.scissor, m_Viewport);
        if (!SameRect2D(scissor, lastScissor)) {
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            lastScissor = scissor;
        }

        if (batch.indexCount > 0) {
            vkCmdDrawIndexed(cmd, batch.indexCount, 1, batch.indexOffset, 0, 0);
            ++drawCalls;
        }
    }
}

} // namespace Hephaestus::HephaestusUI