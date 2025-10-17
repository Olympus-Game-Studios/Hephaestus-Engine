#include "VulkanGizmo.h"
#include "VulkanBuffer.h"
#include <iostream>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <limits>

// Helper functions (could be moved to a common utility file)
static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
}

VulkanGizmo::VulkanGizmo() {
}

VulkanGizmo::~VulkanGizmo() {
}

void VulkanGizmo::Init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                       VkDescriptorSetLayout cameraDescriptorLayout, VmaAllocator allocator) {
    // Generate geometry for all gizmo types
    GenerateTranslateGeometry(device, allocator);
    GenerateRotateGeometry(device, allocator);
    GenerateScaleGeometry(device, allocator);

    // Create rendering pipeline
    CreatePipeline(device, renderPass, extent, cameraDescriptorLayout);

    std::cout << "VulkanGizmo initialized" << std::endl;
}

void VulkanGizmo::Cleanup(VkDevice device, VmaAllocator allocator) {
    if (m_Pipeline) vkDestroyPipeline(device, m_Pipeline, nullptr);
    if (m_PipelineLayout) vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);

    // Cleanup geometry buffers
    if (m_TranslateGeo.vertexBuffer) {
        m_TranslateGeo.vertexBuffer->Destroy(allocator);
        delete m_TranslateGeo.vertexBuffer;
    }
    if (m_TranslateGeo.indexBuffer) {
        m_TranslateGeo.indexBuffer->Destroy(allocator);
        delete m_TranslateGeo.indexBuffer;
    }

    if (m_RotateGeo.vertexBuffer) {
        m_RotateGeo.vertexBuffer->Destroy(allocator);
        delete m_RotateGeo.vertexBuffer;
    }
    if (m_RotateGeo.indexBuffer) {
        m_RotateGeo.indexBuffer->Destroy(allocator);
        delete m_RotateGeo.indexBuffer;
    }

    if (m_ScaleGeo.vertexBuffer) {
        m_ScaleGeo.vertexBuffer->Destroy(allocator);
        delete m_ScaleGeo.vertexBuffer;
    }
    if (m_ScaleGeo.indexBuffer) {
        m_ScaleGeo.indexBuffer->Destroy(allocator);
        delete m_ScaleGeo.indexBuffer;
    }
}

void VulkanGizmo::GenerateTranslateGeometry(VkDevice device, VmaAllocator allocator) {
    std::vector<GizmoVertex> vertices;
    std::vector<uint16_t> indices;

    // Generate three arrows along X, Y, Z axes
    // X axis - Red
    AddArrow(vertices, indices, Vec3(1, 0, 0), m_TranslateLength, m_AxisThickness * 0.5f, Vec3(1, 0, 0));
    // Y axis - Green
    AddArrow(vertices, indices, Vec3(0, 1, 0), m_TranslateLength, m_AxisThickness * 0.5f, Vec3(0, 1, 0));
    // Z axis - Blue
    AddArrow(vertices, indices, Vec3(0, 0, 1), m_TranslateLength, m_AxisThickness * 0.5f, Vec3(0, 0, 1));

    m_TranslateGeo.indexCount = static_cast<uint32_t>(indices.size());

    // Upload to GPU buffers
    m_TranslateGeo.vertexBuffer = new VulkanBuffer();
    m_TranslateGeo.vertexBuffer->Create(allocator, vertices.size() * sizeof(GizmoVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, vertices.data());
    m_TranslateGeo.indexBuffer = new VulkanBuffer();
    m_TranslateGeo.indexBuffer->Create(allocator, indices.size() * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, indices.data());
}

void VulkanGizmo::GenerateRotateGeometry(VkDevice device, VmaAllocator allocator) {
    std::vector<GizmoVertex> vertices;
    std::vector<uint16_t> indices;

    // Generate three torus segments for rotation handles
    // X axis - Red (rotation around X)
    AddTorusSegment(vertices, indices, Vec3(1, 0, 0), m_RotateRadius, 64, Vec3(1, 0, 0));
    // Y axis - Green (rotation around Y)
    AddTorusSegment(vertices, indices, Vec3(0, 1, 0), m_RotateRadius, 64, Vec3(0, 1, 0));
    // Z axis - Blue (rotation around Z)
    AddTorusSegment(vertices, indices, Vec3(0, 0, 1), m_RotateRadius, 64, Vec3(0, 0, 1));

    m_RotateGeo.indexCount = static_cast<uint32_t>(indices.size());

    // Upload to GPU buffers
    m_RotateGeo.vertexBuffer = new VulkanBuffer();
    m_RotateGeo.vertexBuffer->Create(allocator, vertices.size() * sizeof(GizmoVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, vertices.data());
    m_RotateGeo.indexBuffer = new VulkanBuffer();
    m_RotateGeo.indexBuffer->Create(allocator, indices.size() * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, indices.data());
}

void VulkanGizmo::GenerateScaleGeometry(VkDevice device, VmaAllocator allocator) {
    std::vector<GizmoVertex> vertices;
    std::vector<uint16_t> indices;

    // Generate three lines with cubes at the end for scale handles
    const float cubeSize = 0.18f;

    // X axis - Red
    AddArrow(vertices, indices, Vec3(1, 0, 0), m_ScaleLength, m_AxisThickness * 0.5f, Vec3(1, 0, 0));
    AddCube(vertices, indices, Vec3(m_ScaleLength, 0, 0), cubeSize, Vec3(1, 0, 0));
    
    // Y axis - Green
    AddArrow(vertices, indices, Vec3(0, 1, 0), m_ScaleLength, m_AxisThickness * 0.5f, Vec3(0, 1, 0));
    AddCube(vertices, indices, Vec3(0, m_ScaleLength, 0), cubeSize, Vec3(0, 1, 0));
    
    // Z axis - Blue
    AddArrow(vertices, indices, Vec3(0, 0, 1), m_ScaleLength, m_AxisThickness * 0.5f, Vec3(0, 0, 1));
    AddCube(vertices, indices, Vec3(0, 0, m_ScaleLength), cubeSize, Vec3(0, 0, 1));

    m_ScaleGeo.indexCount = static_cast<uint32_t>(indices.size());

    // Upload to GPU buffers
    m_ScaleGeo.vertexBuffer = new VulkanBuffer();
    m_ScaleGeo.vertexBuffer->Create(allocator, vertices.size() * sizeof(GizmoVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, vertices.data());
    m_ScaleGeo.indexBuffer = new VulkanBuffer();
    m_ScaleGeo.indexBuffer->Create(allocator, indices.size() * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, indices.data());
}

VulkanGizmo::GeometryBuffers* VulkanGizmo::GetGeometryForMode() {
    switch (m_Mode) {
        case Mode::Translate: return &m_TranslateGeo;
        case Mode::Rotate:    return &m_RotateGeo;
        case Mode::Scale:     return &m_ScaleGeo;
        default:              return nullptr;
    }
}

const VulkanGizmo::GeometryBuffers* VulkanGizmo::GetGeometryForMode() const {
    return const_cast<VulkanGizmo*>(this)->GetGeometryForMode();
}

float VulkanGizmo::GetAxisLengthForCurrentMode() const {
    switch (m_Mode) {
        case Mode::Translate: return m_CurrentScale * m_TranslateLength;
        case Mode::Scale:     return m_CurrentScale * m_ScaleLength;
        case Mode::Rotate:    return m_CurrentScale * m_RotateRadius;
        default:              return m_CurrentScale;
    }
}

bool VulkanGizmo::ClosestPointOnAxis(const Ray& ray, const Vec3& origin, const Vec3& axisDir,
                                     float& outAxisParam, float& outDistance, bool clampToAxis) const {
    Vec3 dirNorm = vnorm(axisDir);
    if (vlen(dirNorm) < 1e-6f) return false;

    Vec3 w0 = vsub(origin, ray.origin);
    float b = vdot(dirNorm, ray.direction);
    float d = vdot(dirNorm, w0);
    float e = vdot(ray.direction, w0);
    float denom = 1.0f - b * b;

    float s = 0.0f;
    float t = 0.0f;

    if (std::abs(denom) < 1e-6f) {
        // Lines are almost parallel; fallback to projecting onto ray
        s = 0.0f;
        t = d / std::max(b, 1e-6f);
    } else {
        s = (b * e - d) / denom;
        t = (e - b * d) / denom;
    }

    if (clampToAxis) {
        float axisLength = GetAxisLengthForCurrentMode();
        s = std::clamp(s, 0.0f, axisLength);
    }

    Vec3 closestPointAxis = vadd(origin, vscale(dirNorm, s));
    Vec3 closestPointRay = vadd(ray.origin, vscale(ray.direction, t));
    outAxisParam = s;
    outDistance = vlen(vsub(closestPointAxis, closestPointRay));
    return true;
}

bool VulkanGizmo::IntersectRotationRing(const Ray& ray, const Vec3& axisDir,
                                        float& outDistanceFromRing) const {
    Vec3 dirNorm = vnorm(axisDir);
    if (vlen(dirNorm) < 1e-6f) return false;

    float denom = vdot(dirNorm, ray.direction);
    if (std::abs(denom) < 1e-6f) return false;

    float t = vdot(dirNorm, vsub(m_GizmoOrigin, ray.origin)) / denom;
    if (t < 0.0f) return false;

    Vec3 hitPoint = vadd(ray.origin, vscale(ray.direction, t));
    Vec3 diff = vsub(hitPoint, m_GizmoOrigin);
    float axial = vdot(diff, dirNorm);
    Vec3 radial = vsub(diff, vscale(dirNorm, axial));
    float radialLength = vlen(radial);
    if (radialLength < 1e-6f) return false;

    float ringRadius = m_CurrentScale * m_RotateRadius;
    outDistanceFromRing = std::abs(radialLength - ringRadius);
    return true;
}

bool VulkanGizmo::IntersectPlane(const Ray& ray, const Vec3& planeOrigin, const Vec3& planeNormal, Vec3& outPoint) const {
    Vec3 n = vnorm(planeNormal);
    float denom = vdot(n, ray.direction);
    if (std::abs(denom) < 1e-6f) {
        return false;
    }

    float t = vdot(n, vsub(planeOrigin, ray.origin)) / denom;
    if (t < 0.0f) {
        return false;
    }

    outPoint = vadd(ray.origin, vscale(ray.direction, t));
    return true;
}

void VulkanGizmo::AddArrow(std::vector<GizmoVertex>& vertices, std::vector<uint16_t>& indices,
                           const Vec3& direction, float length, float radius, const Vec3& color) {
    // Simple arrow: cylinder shaft + cone tip
    uint16_t baseIndex = static_cast<uint16_t>(vertices.size());
    Vec3 dirNorm = vnorm(direction);
    Vec3 start(0, 0, 0);
    float coneLength = length * 0.25f;
    float shaftLength = std::max(length - coneLength, 0.001f);
    Vec3 shaftEnd = vscale(dirNorm, shaftLength);
    Vec3 coneEnd = vscale(dirNorm, length);

    Vec3 perp = (std::abs(dirNorm.x) < 0.9f) ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    Vec3 tangent = vnorm(vcross(dirNorm, perp));
    Vec3 bitangent = vnorm(vcross(dirNorm, tangent));

    const int segments = 16;

    // Shaft vertices (cylinder)
    for (int i = 0; i < segments; ++i) {
        float angle = i * 3.14159265f * 2.0f / segments;
        Vec3 offset = vadd(vscale(tangent, std::cos(angle) * radius),
                           vscale(bitangent, std::sin(angle) * radius));

        vertices.push_back({vadd(start, offset), dirNorm, color});
        vertices.push_back({vadd(shaftEnd, offset), dirNorm, color});
    }

    for (int i = 0; i < segments; ++i) {
        int next = (i + 1) % segments;
        uint16_t i0 = baseIndex + i * 2;
        uint16_t i1 = baseIndex + i * 2 + 1;
        uint16_t i2 = baseIndex + next * 2;
        uint16_t i3 = baseIndex + next * 2 + 1;

        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);

        indices.push_back(i2);
        indices.push_back(i1);
        indices.push_back(i3);
    }

    // Cone vertices
    uint16_t tipBase = static_cast<uint16_t>(vertices.size());
    float coneRadius = radius * 2.0f;
    for (int i = 0; i < segments; ++i) {
        float angle = i * 3.14159265f * 2.0f / segments;
        Vec3 offset = vadd(vscale(tangent, std::cos(angle) * coneRadius),
                           vscale(bitangent, std::sin(angle) * coneRadius));
        vertices.push_back({vadd(shaftEnd, offset), dirNorm, color});
    }
    vertices.push_back({coneEnd, dirNorm, color});

    uint16_t tipPoint = static_cast<uint16_t>(vertices.size() - 1);
    for (int i = 0; i < segments; ++i) {
        int next = (i + 1) % segments;
        indices.push_back(tipBase + i);
        indices.push_back(tipPoint);
        indices.push_back(tipBase + next);
    }
}

void VulkanGizmo::AddTorusSegment(std::vector<GizmoVertex>& vertices, std::vector<uint16_t>& indices,
                                  const Vec3& axis, float radius, int segments, const Vec3& color) {
    // Create a ring/torus segment for rotation visualization
    uint16_t baseIndex = static_cast<uint16_t>(vertices.size());
    float tubeRadius = m_AxisThickness * 0.5f;

    // Generate vertices around the ring
    for (int i = 0; i <= segments; ++i) {
        float angle = i * 3.14159f * 2.0f / segments;
        
        // Calculate position on the ring
        Vec3 ringPos;
        Vec3 tangent;
        Vec3 normal;
        
        if (std::abs(axis.x) > 0.5f) {
            // Rotation around X axis
            ringPos = Vec3(0, std::cos(angle) * radius, std::sin(angle) * radius);
            tangent = Vec3(0, -std::sin(angle), std::cos(angle));
            normal = vnorm(vsub(ringPos, Vec3(0, 0, 0)));
        } else if (std::abs(axis.y) > 0.5f) {
            // Rotation around Y axis
            ringPos = Vec3(std::cos(angle) * radius, 0, std::sin(angle) * radius);
            tangent = Vec3(-std::sin(angle), 0, std::cos(angle));
            normal = vnorm(vsub(ringPos, Vec3(0, 0, 0)));
        } else {
            // Rotation around Z axis
            ringPos = Vec3(std::cos(angle) * radius, std::sin(angle) * radius, 0);
            tangent = Vec3(-std::sin(angle), std::cos(angle), 0);
            normal = vnorm(vsub(ringPos, Vec3(0, 0, 0)));
        }

        // Add two vertices for tube cross-section
        Vec3 offset1 = vscale(axis, tubeRadius);
        Vec3 offset2 = vscale(axis, -tubeRadius);
        
        vertices.push_back({vadd(ringPos, offset1), normal, color});
        vertices.push_back({vadd(ringPos, offset2), normal, color});
    }

    // Add indices for the tube
    for (int i = 0; i < segments; ++i) {
        uint16_t i0 = baseIndex + i * 2;
        uint16_t i1 = baseIndex + i * 2 + 1;
        uint16_t i2 = baseIndex + (i + 1) * 2;
        uint16_t i3 = baseIndex + (i + 1) * 2 + 1;

        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);
        
        indices.push_back(i2);
        indices.push_back(i1);
        indices.push_back(i3);
    }
}

void VulkanGizmo::AddCube(std::vector<GizmoVertex>& vertices, std::vector<uint16_t>& indices,
                         const Vec3& position, float size, const Vec3& color) {
    uint16_t baseIndex = static_cast<uint16_t>(vertices.size());
    float hs = size * 0.5f;  // half size

    // 8 cube vertices
    Vec3 corners[8] = {
        vadd(position, Vec3(-hs, -hs, -hs)),
        vadd(position, Vec3( hs, -hs, -hs)),
        vadd(position, Vec3( hs,  hs, -hs)),
        vadd(position, Vec3(-hs,  hs, -hs)),
        vadd(position, Vec3(-hs, -hs,  hs)),
        vadd(position, Vec3( hs, -hs,  hs)),
        vadd(position, Vec3( hs,  hs,  hs)),
        vadd(position, Vec3(-hs,  hs,  hs))
    };

    // Add vertices with normals for each face
    // Front face
    vertices.push_back({corners[4], Vec3(0, 0, 1), color});
    vertices.push_back({corners[5], Vec3(0, 0, 1), color});
    vertices.push_back({corners[6], Vec3(0, 0, 1), color});
    vertices.push_back({corners[7], Vec3(0, 0, 1), color});

    // Back face
    vertices.push_back({corners[1], Vec3(0, 0, -1), color});
    vertices.push_back({corners[0], Vec3(0, 0, -1), color});
    vertices.push_back({corners[3], Vec3(0, 0, -1), color});
    vertices.push_back({corners[2], Vec3(0, 0, -1), color});

    // Add indices for the cube (12 triangles)
    uint16_t cubeIndices[] = {
        0, 1, 2,  2, 3, 0,  // Front
        4, 5, 6,  6, 7, 4,  // Back
        // Add remaining faces as needed
    };

    for (uint16_t idx : cubeIndices) {
        indices.push_back(baseIndex + idx);
    }
}

void VulkanGizmo::CreatePipeline(VkDevice device, VkRenderPass renderPass,
                                 VkExtent2D extent, VkDescriptorSetLayout cameraDescriptorLayout) {
    // Load and compile shaders
    auto vertCode = readFile("shaders/gizmo.vert.spv");
    auto fragCode = readFile("shaders/gizmo.frag.spv");

    VkShaderModule vertShader = createShaderModule(device, vertCode);
    VkShaderModule fragShader = createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShader;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShader;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

    auto bindingDesc = GizmoVertex::getBindingDescription();
    auto attribs = GizmoVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = (uint32_t)attribs.size();
    vertexInput.pVertexAttributeDescriptions = attribs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAsm{};
    inputAsm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = nullptr; // dynamic
    viewportState.scissorCount = 1;
    viewportState.pScissors = nullptr; // dynamic

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_FALSE; // Disable depth test for gizmo
    depth.depthWriteEnable = VK_FALSE; // Don't write to depth buffer
    depth.depthCompareOp = VK_COMPARE_OP_ALWAYS; // Always pass depth test

    VkPipelineColorBlendAttachmentState colorAttach{};
    colorAttach.colorWriteMask = 0xF;
    colorAttach.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &colorAttach;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(GizmoPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &cameraDescriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create gizmo pipeline layout");

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &inputAsm;
    pci.pViewportState = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &depth;
    pci.pColorBlendState = &blend;
    pci.layout = m_PipelineLayout;
    pci.renderPass = renderPass;
    pci.subpass = 0;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicStateInfo.dynamicStateCount = 2;
    dynamicStateInfo.pDynamicStates = dynamicStates;
    pci.pDynamicState = &dynamicStateInfo;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_Pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create gizmo graphics pipeline");

    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);

    std::cout << "VulkanGizmo pipeline created successfully" << std::endl;
}

void VulkanGizmo::UpdateState(const Mat4& objectTransform, const Mat4& viewMatrix,
                              const Mat4& projMatrix, const Vec3& cameraPos,
                              Mode mode, Space space) {
    m_Mode = mode;
    m_Space = space;
    m_ObjectTransform = objectTransform;
    m_ViewMatrix = viewMatrix;
    m_ProjMatrix = projMatrix;
    m_CameraPos = cameraPos;
    m_GizmoOrigin = Vec3(objectTransform.m[12], objectTransform.m[13], objectTransform.m[14]);

    if (m_Space == Space::World) {
        m_AxisDirections[0] = Vec3(1, 0, 0);
        m_AxisDirections[1] = Vec3(0, 1, 0);
        m_AxisDirections[2] = Vec3(0, 0, 1);
    } else {
        m_AxisDirections[0] = vnorm(Vec3(objectTransform.m[0], objectTransform.m[1], objectTransform.m[2]));
        m_AxisDirections[1] = vnorm(Vec3(objectTransform.m[4], objectTransform.m[5], objectTransform.m[6]));
        m_AxisDirections[2] = vnorm(Vec3(objectTransform.m[8], objectTransform.m[9], objectTransform.m[10]));
    }

    m_CurrentScale = std::max(CalculateGizmoScale(m_GizmoOrigin, cameraPos, projMatrix), 0.001f);
    GeometryBuffers* geo = GetGeometryForMode();
    m_StateValid = (geo && geo->vertexBuffer && geo->indexBuffer);

    if (m_ActiveAxis >= 0 && m_ActiveAxis < 3) {
        m_DragAxisDir = m_AxisDirections[m_ActiveAxis];
    }
}

void VulkanGizmo::Render(VkCommandBuffer cmd, VkDescriptorSet cameraDescriptor) {
    if (!m_StateValid || !m_Pipeline) return;

    GeometryBuffers* geo = GetGeometryForMode();
    if (!geo || !geo->vertexBuffer || !geo->indexBuffer) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    uint32_t zeroOffset = 0;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                           0, 1, &cameraDescriptor, 1, &zeroOffset);

    // Build transform from origin, orientation, and scale
    Mat4 rotation = Mat4::Identity();
    rotation.m[0] = m_AxisDirections[0].x; rotation.m[1] = m_AxisDirections[0].y; rotation.m[2] = m_AxisDirections[0].z;
    rotation.m[4] = m_AxisDirections[1].x; rotation.m[5] = m_AxisDirections[1].y; rotation.m[6] = m_AxisDirections[1].z;
    rotation.m[8] = m_AxisDirections[2].x; rotation.m[9] = m_AxisDirections[2].y; rotation.m[10] = m_AxisDirections[2].z;

    Mat4 translation = Mat4::Translation(m_GizmoOrigin);
    Mat4 scale = Mat4::Scale(Vec3(m_CurrentScale, m_CurrentScale, m_CurrentScale));

    Mat4 transform = Mat4::Multiply(rotation, scale);
    transform = Mat4::Multiply(translation, transform);

    GizmoPushConstants pc;
    pc.model = transform;
    pc.state = Vec4(static_cast<float>(m_HoveredAxis), static_cast<float>(m_ActiveAxis), m_CurrentScale, 0.0f);

    vkCmdPushConstants(cmd, m_PipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(GizmoPushConstants), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &geo->vertexBuffer->buffer, &offset);
    vkCmdBindIndexBuffer(cmd, geo->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, geo->indexCount, 1, 0, 0, 0);
}

int VulkanGizmo::CheckHover(const Ray& ray) const {
    if (!m_StateValid) return -1;

    int bestAxis = -1;
    float bestScore = std::numeric_limits<float>::max();

    if (m_Mode == Mode::Rotate) {
        float threshold = m_CurrentScale * m_RotationPickWidth;
        for (int axis = 0; axis < 3; ++axis) {
            float distance = 0.0f;
            if (IntersectRotationRing(ray, m_AxisDirections[axis], distance)) {
                if (distance < threshold && distance < bestScore) {
                    bestScore = distance;
                    bestAxis = axis;
                }
            }
        }
    } else {
        float threshold = m_CurrentScale * m_AxisThickness * 1.5f;
        for (int axis = 0; axis < 3; ++axis) {
            float axisParam = 0.0f;
            float distance = 0.0f;
            if (ClosestPointOnAxis(ray, m_GizmoOrigin, m_AxisDirections[axis],
                                   axisParam, distance, true)) {
                if (distance < threshold && distance < bestScore) {
                    bestScore = distance;
                    bestAxis = axis;
                }
            }
        }
    }

    return bestAxis;
}

void VulkanGizmo::SetHoveredAxis(int axis) {
    m_HoveredAxis = axis;
}

void VulkanGizmo::BeginDrag(const Ray& ray, int axis) {
    if (!m_StateValid || axis < 0 || axis > 2) return;

    m_IsDragging = true;
    m_ActiveAxis = axis;
    m_DragStartTransform = m_ObjectTransform;
    m_DragStartPosition = Vec3(m_ObjectTransform.m[12], m_ObjectTransform.m[13], m_ObjectTransform.m[14]);
    m_DragAxisDir = m_AxisDirections[axis];
    m_DragStartAxisParam = 0.0f;
    m_DragStartScale = Vec3(1, 1, 1);
    m_RotationStartValid = false;

    float axisParam = 0.0f;
    float distance = 0.0f;
    if (m_Mode != Mode::Rotate) {
        if (ClosestPointOnAxis(ray, m_GizmoOrigin, m_DragAxisDir, axisParam, distance, false)) {
            m_DragStartAxisParam = axisParam;
        }
    }

    if (m_Mode == Mode::Scale) {
        // Extract initial scale from transform columns
        Vec3 basisX(m_DragStartTransform.m[0], m_DragStartTransform.m[1], m_DragStartTransform.m[2]);
        Vec3 basisY(m_DragStartTransform.m[4], m_DragStartTransform.m[5], m_DragStartTransform.m[6]);
        Vec3 basisZ(m_DragStartTransform.m[8], m_DragStartTransform.m[9], m_DragStartTransform.m[10]);
        m_DragStartScale = Vec3(std::max(vlen(basisX), 1e-4f),
                                std::max(vlen(basisY), 1e-4f),
                                std::max(vlen(basisZ), 1e-4f));

        if (std::abs(m_DragStartAxisParam) < 0.05f * m_CurrentScale) {
            m_DragStartAxisParam = (m_DragStartAxisParam >= 0.0f ? 1.0f : -1.0f) * 0.05f * m_CurrentScale;
        }
    }

    if (m_Mode == Mode::Rotate) {
        Vec3 hitPoint;
        if (IntersectPlane(ray, m_GizmoOrigin, m_DragAxisDir, hitPoint)) {
            Vec3 fromOrigin = vsub(hitPoint, m_GizmoOrigin);
            float len = vlen(fromOrigin);
            if (len > 1e-6f) {
                m_RotationStartVector = vscale(fromOrigin, 1.0f / len);
                m_RotationStartValid = true;
            }
        }
    }
}

Mat4 VulkanGizmo::HandleDrag(const Ray& ray) {
    if (!m_IsDragging) {
        return m_ObjectTransform;
    }

    Mat4 result = m_ObjectTransform;

    if (m_ActiveAxis >= 0 && m_ActiveAxis < 3 && m_Mode == Mode::Translate) {
        float axisParam = 0.0f;
        float distance = 0.0f;
        if (ClosestPointOnAxis(ray, m_DragStartPosition, m_DragAxisDir, axisParam, distance, false)) {
            float delta = axisParam - m_DragStartAxisParam;
            Vec3 newPosition = vadd(m_DragStartPosition, vscale(m_DragAxisDir, delta));

            result = m_DragStartTransform;
            result.m[12] = newPosition.x;
            result.m[13] = newPosition.y;
            result.m[14] = newPosition.z;
        }
    } else if (m_ActiveAxis >= 0 && m_ActiveAxis < 3 && m_Mode == Mode::Scale) {
        float axisParam = 0.0f;
        float distance = 0.0f;
        if (ClosestPointOnAxis(ray, m_GizmoOrigin, m_DragAxisDir, axisParam, distance, false) && std::abs(m_DragStartAxisParam) > 1e-4f) {
            float ratio = axisParam / m_DragStartAxisParam;
            if (!std::isfinite(ratio)) {
                ratio = 1.0f;
            }
            ratio = std::max(ratio, 0.01f);

            Vec3 translation(m_DragStartTransform.m[12], m_DragStartTransform.m[13], m_DragStartTransform.m[14]);
            Vec3 basisX(m_DragStartTransform.m[0], m_DragStartTransform.m[1], m_DragStartTransform.m[2]);
            Vec3 basisY(m_DragStartTransform.m[4], m_DragStartTransform.m[5], m_DragStartTransform.m[6]);
            Vec3 basisZ(m_DragStartTransform.m[8], m_DragStartTransform.m[9], m_DragStartTransform.m[10]);

            float scaleX = m_DragStartScale.x;
            float scaleY = m_DragStartScale.y;
            float scaleZ = m_DragStartScale.z;

            switch (m_ActiveAxis) {
                case 0: scaleX = std::max(0.01f, m_DragStartScale.x * ratio); break;
                case 1: scaleY = std::max(0.01f, m_DragStartScale.y * ratio); break;
                case 2: scaleZ = std::max(0.01f, m_DragStartScale.z * ratio); break;
            }

            Vec3 normX = vnorm(basisX);
            Vec3 normY = vnorm(basisY);
            Vec3 normZ = vnorm(basisZ);

            Mat4 rotation = Mat4::Identity();
            rotation.m[0] = normX.x; rotation.m[1] = normX.y; rotation.m[2] = normX.z;
            rotation.m[4] = normY.x; rotation.m[5] = normY.y; rotation.m[6] = normY.z;
            rotation.m[8] = normZ.x; rotation.m[9] = normZ.y; rotation.m[10] = normZ.z;

            Mat4 translate = Mat4::Translation(translation);
            Mat4 scale = Mat4::Scale(Vec3(scaleX, scaleY, scaleZ));

            result = Mat4::Multiply(translate, Mat4::Multiply(rotation, scale));
        }
    } else if (m_ActiveAxis >= 0 && m_ActiveAxis < 3 && m_Mode == Mode::Rotate) {
        if (m_RotationStartValid) {
            Vec3 hitPoint;
            if (IntersectPlane(ray, m_GizmoOrigin, m_DragAxisDir, hitPoint)) {
                Vec3 fromOrigin = vsub(hitPoint, m_GizmoOrigin);
                float len = vlen(fromOrigin);
                if (len > 1e-6f) {
                    Vec3 currentVec = vscale(fromOrigin, 1.0f / len);
                    float dot = std::clamp(vdot(m_RotationStartVector, currentVec), -1.0f, 1.0f);
                    Vec3 crossVec = vcross(m_RotationStartVector, currentVec);
                    float sine = vdot(crossVec, m_DragAxisDir);
                    float angle = std::atan2(sine, dot);

                    Mat4 toOrigin = Mat4::Translation(vscale(m_GizmoOrigin, -1.0f));
                    Mat4 back = Mat4::Translation(m_GizmoOrigin);
                    Mat4 rotation = Mat4::RotationAxis(m_DragAxisDir, angle);

                    result = Mat4::Multiply(back, Mat4::Multiply(rotation, Mat4::Multiply(toOrigin, m_DragStartTransform)));
                }
            }
        }
    }

    return result;
}

void VulkanGizmo::EndDrag() {
    m_IsDragging = false;
    m_ActiveAxis = -1;
}

float VulkanGizmo::CalculateGizmoScale(const Vec3& gizmoPos, const Vec3& cameraPos,
                                      const Mat4& projMatrix) const {
    float distance = vlen(vsub(gizmoPos, cameraPos));
    float f = std::abs(projMatrix.m[5]);
    if (f > 1e-5f) {
        float fovY = 2.0f * std::atan(1.0f / f);
        float scale = distance * std::tan(fovY * 0.5f) * m_ScreenScaleFactor;
        return std::max(scale, 0.001f);
    }
    return std::max(distance * m_ScreenScaleFactor, 0.001f);
}
