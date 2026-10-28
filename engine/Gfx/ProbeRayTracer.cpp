#include "ProbeRayTracer.h"

#include "VulkanDevice.h"
#include "../Assets/MeshAsset.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace Hephaestus {

namespace {

struct GpuTraceInstance {
    Vec4 albedo;
    Vec4 emissive;
    VkDeviceAddress vertices;
    VkDeviceAddress indices;
};

enum Binding : uint32_t {
    kParams = 0,
    kLighting = 1,
    kOffsets = 3,
    kScene = 11,
    kInstances = 12,
    kHits = 13,
};

VkShaderModule LoadShader(VkDevice device, const std::string& name) {
    const std::string candidates[] = {"shaders/" + name, "../shaders/" + name, "build/Release/shaders/" + name,
                                      "../build/Release/shaders/" + name};
    for (const auto& path : candidates) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) continue;
        const size_t size = static_cast<size_t>(file.tellg());
        std::vector<char> code(size);
        file.seekg(0);
        file.read(code.data(), static_cast<std::streamsize>(size));
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        return vkCreateShaderModule(device, &info, nullptr, &module) == VK_SUCCESS ? module : VK_NULL_HANDLE;
    }
    std::cerr << "[ProbeRayTracer] Missing shader " << name << "\n";
    return VK_NULL_HANDLE;
}

VkDeviceAddress AlignUp(VkDeviceAddress value, VkDeviceSize alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

} // namespace

VkDeviceAddress ProbeRayTracer::bufferAddress(VkBuffer buffer) const {
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = buffer;
    return vkGetBufferDeviceAddress(m_device->device, &info);
}

bool ProbeRayTracer::Init(VulkanDevice* device) {
    m_device = device;
    if (!device->SupportsRayQuery()) {
        return false;
    }
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext = &asProps;
    vkGetPhysicalDeviceProperties2(device->physicalDevice, &props);
    m_scratchAlignment = std::max<VkDeviceSize>(1, asProps.minAccelerationStructureScratchOffsetAlignment);

    const VkShaderStageFlags compute = VK_SHADER_STAGE_COMPUTE_BIT;
    const VkDescriptorSetLayoutBinding bindings[] = {
        {kParams, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, compute, nullptr},
        {kLighting, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, compute, nullptr},
        {kOffsets, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, compute, nullptr},
        {kScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, compute, nullptr},
        {kInstances, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, compute, nullptr},
        {kHits, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, compute, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(std::size(bindings));
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device->device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS) return false;
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    if (vkCreatePipelineLayout(device->device, &pl, nullptr, &m_pipelineLayout) != VK_SUCCESS) return false;

    VkShaderModule module = LoadShader(device->device, "probe_trace.comp.spv");
    if (!module) return false;
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT,
                  module, "main", nullptr};
    info.layout = m_pipelineLayout;
    const VkResult result = vkCreateComputePipelines(device->device, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline);
    vkDestroyShaderModule(device->device, module, nullptr);
    if (result != VK_SUCCESS) {
        m_pipeline = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool ProbeRayTracer::CreateSwapchainResources(uint32_t imageCount) {
    if (!Ready()) return true;
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * imageCount},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * imageCount},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, imageCount},
    };
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = imageCount;
    pool.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
    pool.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(m_device->device, &pool, nullptr, &m_pool) != VK_SUCCESS) return false;
    m_frames.assign(imageCount, FrameResources{});
    std::vector<VkDescriptorSetLayout> layouts(imageCount, m_setLayout);
    std::vector<VkDescriptorSet> sets(imageCount);
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = m_pool;
    alloc.descriptorSetCount = imageCount;
    alloc.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(m_device->device, &alloc, sets.data()) != VK_SUCCESS) return false;
    for (uint32_t i = 0; i < imageCount; ++i) {
        m_frames[i].set = sets[i];
        if (!ensureCapacity(m_frames[i], 1)) return false;
    }
    return true;
}

void ProbeRayTracer::destroyStructure(AccelerationStructure& as) {
    if (as.handle) m_device->RayTracing().destroyAccelerationStructure(m_device->device, as.handle, nullptr);
    as.buffer.Destroy(m_device->allocator);
    as = AccelerationStructure{};
}

void ProbeRayTracer::DestroySwapchainResources() {
    if (!m_device) return;
    for (FrameResources& frame : m_frames) {
        destroyStructure(frame.tlas);
        frame.instanceBuffer.Destroy(m_device->allocator);
        frame.instanceData.Destroy(m_device->allocator);
        frame.scratch.Destroy(m_device->allocator);
    }
    m_frames.clear();
    if (m_pool) vkDestroyDescriptorPool(m_device->device, m_pool, nullptr);
    m_pool = VK_NULL_HANDLE;
}

void ProbeRayTracer::Shutdown() {
    if (!m_device) return;
    DestroySwapchainResources();
    for (auto& entry : m_meshStructures) destroyStructure(entry.second);
    m_meshStructures.clear();
    if (m_pipeline) vkDestroyPipeline(m_device->device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device->device, m_pipelineLayout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_device->device, m_setLayout, nullptr);
    m_pipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
    m_device = nullptr;
}

bool ProbeRayTracer::buildMeshStructure(const MeshGPU& mesh) {
    const auto& rt = m_device->RayTracing();
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto& triangles = geometry.geometry.triangles;
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = bufferAddress(mesh.vertexBuffer.buffer);
    triangles.vertexStride = sizeof(Vertex);
    triangles.maxVertex = mesh.vertexCount > 0 ? mesh.vertexCount - 1 : 0;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = bufferAddress(mesh.indexBuffer.buffer);

    VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    const uint32_t primitives = mesh.indexCount / 3;
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    rt.getBuildSizes(m_device->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &primitives, &sizes);

    AccelerationStructure as;
    VulkanBuffer scratch;
    try {
        as.buffer.Create(m_device->allocator, sizes.accelerationStructureSize,
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                         VMA_MEMORY_USAGE_GPU_ONLY);
        scratch.Create(m_device->allocator, sizes.buildScratchSize + m_scratchAlignment,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       VMA_MEMORY_USAGE_GPU_ONLY);
    } catch (const std::exception& e) {
        std::cerr << "[ProbeRayTracer] " << e.what() << "\n";
        as.buffer.Destroy(m_device->allocator);
        return false;
    }
    VkAccelerationStructureCreateInfoKHR create{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    create.buffer = as.buffer.buffer;
    create.size = sizes.accelerationStructureSize;
    create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (rt.createAccelerationStructure(m_device->device, &create, nullptr, &as.handle) != VK_SUCCESS) {
        as.buffer.Destroy(m_device->allocator);
        scratch.Destroy(m_device->allocator);
        return false;
    }
    build.dstAccelerationStructure = as.handle;
    build.scratchData.deviceAddress = AlignUp(bufferAddress(scratch.buffer), m_scratchAlignment);
    VkAccelerationStructureBuildRangeInfoKHR range{primitives, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;

    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = m_device->commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device->device, &alloc, &cmd);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    rt.cmdBuildAccelerationStructures(cmd, 1, &build, &ranges);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(m_device->graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_device->graphicsQueue);
    vkFreeCommandBuffers(m_device->device, m_device->commandPool, 1, &cmd);
    scratch.Destroy(m_device->allocator);

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addressInfo.accelerationStructure = as.handle;
    as.address = rt.getAccelerationStructureAddress(m_device->device, &addressInfo);
    m_meshStructures[mesh.vertexBuffer.buffer] = std::move(as);
    return true;
}

bool ProbeRayTracer::ensureCapacity(FrameResources& frame, uint32_t instanceCount) {
    const uint32_t needed = std::max(1u, instanceCount);
    if (frame.capacity >= needed) return true;
    const uint32_t capacity = std::max(needed, frame.capacity * 2);
    vkDeviceWaitIdle(m_device->device);
    destroyStructure(frame.tlas);
    frame.instanceBuffer.Destroy(m_device->allocator);
    frame.instanceData.Destroy(m_device->allocator);
    frame.scratch.Destroy(m_device->allocator);
    frame.capacity = 0;

    const auto& rt = m_device->RayTracing();
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    rt.getBuildSizes(m_device->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &capacity, &sizes);
    try {
        frame.tlas.buffer.Create(m_device->allocator, sizes.accelerationStructureSize,
                                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                 VMA_MEMORY_USAGE_GPU_ONLY);
        frame.scratch.Create(m_device->allocator, sizes.buildScratchSize + m_scratchAlignment,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                             VMA_MEMORY_USAGE_GPU_ONLY);
        frame.instanceBuffer.Create(m_device->allocator, capacity * sizeof(VkAccelerationStructureInstanceKHR),
                                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                    VMA_MEMORY_USAGE_CPU_TO_GPU);
        frame.instanceData.Create(m_device->allocator, capacity * sizeof(GpuTraceInstance),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    } catch (const std::exception& e) {
        std::cerr << "[ProbeRayTracer] " << e.what() << "\n";
        return false;
    }
    VkAccelerationStructureCreateInfoKHR create{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    create.buffer = frame.tlas.buffer.buffer;
    create.size = sizes.accelerationStructureSize;
    create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (rt.createAccelerationStructure(m_device->device, &create, nullptr, &frame.tlas.handle) != VK_SUCCESS) {
        return false;
    }
    frame.capacity = capacity;
    return true;
}

bool ProbeRayTracer::PrepareScene(uint32_t imageIndex, const std::vector<ProbeTraceInstance>& instances) {
    if (!Ready() || imageIndex >= m_frames.size()) return false;
    std::vector<VkAccelerationStructureInstanceKHR> asInstances;
    std::vector<GpuTraceInstance> data;
    asInstances.reserve(instances.size());
    data.reserve(instances.size());
    for (const ProbeTraceInstance& instance : instances) {
        const MeshGPU* mesh = instance.mesh;
        if (!mesh || mesh->skinned || mesh->indexCount < 3 || !mesh->vertexBuffer.buffer || !mesh->indexBuffer.buffer) {
            continue;
        }
        auto found = m_meshStructures.find(mesh->vertexBuffer.buffer);
        if (found == m_meshStructures.end()) {
            if (!buildMeshStructure(*mesh)) continue;
            found = m_meshStructures.find(mesh->vertexBuffer.buffer);
        }
        VkAccelerationStructureInstanceKHR as{};
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                as.transform.matrix[row][col] = instance.world.m[col * 4 + row];
            }
        }
        as.instanceCustomIndex = static_cast<uint32_t>(data.size());
        as.mask = 0xFF;
        as.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        as.accelerationStructureReference = found->second.address;
        asInstances.push_back(as);
        data.push_back({Vec4(instance.albedo.x, instance.albedo.y, instance.albedo.z, 1.0f),
                        Vec4(instance.emissive.x, instance.emissive.y, instance.emissive.z, 0.0f),
                        bufferAddress(mesh->vertexBuffer.buffer), bufferAddress(mesh->indexBuffer.buffer)});
    }

    FrameResources& frame = m_frames[imageIndex];
    if (!ensureCapacity(frame, static_cast<uint32_t>(asInstances.size()))) return false;
    frame.count = static_cast<uint32_t>(asInstances.size());
    if (frame.count > 0) {
        void* mapped = nullptr;
        vmaMapMemory(m_device->allocator, frame.instanceBuffer.allocation, &mapped);
        std::memcpy(mapped, asInstances.data(), asInstances.size() * sizeof(asInstances[0]));
        vmaUnmapMemory(m_device->allocator, frame.instanceBuffer.allocation);
        vmaMapMemory(m_device->allocator, frame.instanceData.allocation, &mapped);
        std::memcpy(mapped, data.data(), data.size() * sizeof(data[0]));
        vmaUnmapMemory(m_device->allocator, frame.instanceData.allocation);
    }
    return true;
}

void ProbeRayTracer::WriteTraceSet(uint32_t imageIndex, VkBuffer params, VkBuffer lighting, VkBuffer offsets,
                                   VkBuffer hits) {
    FrameResources& frame = m_frames[imageIndex];
    VkWriteDescriptorSetAccelerationStructureKHR asWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures = &frame.tlas.handle;
    const VkDescriptorBufferInfo paramsInfo{params, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo lightingInfo{lighting, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo offsetsInfo{offsets, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo instancesInfo{frame.instanceData.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo hitsInfo{hits, 0, VK_WHOLE_SIZE};
    auto write = [&](uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* buffer) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = frame.set;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = type;
        w.pBufferInfo = buffer;
        return w;
    };
    VkWriteDescriptorSet sceneWrite = write(kScene, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr);
    sceneWrite.pNext = &asWrite;
    const VkWriteDescriptorSet writes[] = {
        write(kParams, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &paramsInfo),
        write(kLighting, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &lightingInfo),
        write(kOffsets, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &offsetsInfo),
        sceneWrite,
        write(kInstances, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &instancesInfo),
        write(kHits, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &hitsInfo),
    };
    vkUpdateDescriptorSets(m_device->device, static_cast<uint32_t>(std::size(writes)), writes, 0, nullptr);
}

void ProbeRayTracer::RecordScene(VkCommandBuffer cmd, uint32_t imageIndex) {
    FrameResources& frame = m_frames[imageIndex];
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = bufferAddress(frame.instanceBuffer.buffer);
    VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    build.dstAccelerationStructure = frame.tlas.handle;
    build.scratchData.deviceAddress = AlignUp(bufferAddress(frame.scratch.buffer), m_scratchAlignment);
    VkAccelerationStructureBuildRangeInfoKHR range{frame.count, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    m_device->RayTracing().cmdBuildAccelerationStructures(cmd, 1, &build, &ranges);

    VkMemoryBarrier built{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    built.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    built.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &built, 0,
                         nullptr, 0, nullptr);
}

void ProbeRayTracer::Record(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t probeCount) {
    FrameResources& frame = m_frames[imageIndex];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &frame.set, 0, nullptr);
    vkCmdDispatch(cmd, probeCount, 1, 1);

    VkMemoryBarrier traced{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    traced.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    traced.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &traced, 0, nullptr, 0, nullptr);
}

} // namespace Hephaestus
