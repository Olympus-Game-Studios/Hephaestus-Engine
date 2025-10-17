#include "VulkanGizmo.h"
#include "VulkanBuffer.h"
#include <iostream>
#include <cmath>
#include <fstream>
#include <stdexcept>

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
    AddArrow(vertices, indices, Vec3(1, 0, 0), 1.0f, 0.02f, Vec3(1, 0, 0));
    // Y axis - Green
    AddArrow(vertices, indices, Vec3(0, 1, 0), 1.0f, 0.02f, Vec3(0, 1, 0));
    // Z axis - Blue
    AddArrow(vertices, indices, Vec3(0, 0, 1), 1.0f, 0.02f, Vec3(0, 0, 1));

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
    AddTorusSegment(vertices, indices, Vec3(1, 0, 0), 1.0f, 32, Vec3(1, 0, 0));
    // Y axis - Green (rotation around Y)
    AddTorusSegment(vertices, indices, Vec3(0, 1, 0), 1.0f, 32, Vec3(0, 1, 0));
    // Z axis - Blue (rotation around Z)
    AddTorusSegment(vertices, indices, Vec3(0, 0, 1), 1.0f, 32, Vec3(0, 0, 1));

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
    // X axis - Red
    AddArrow(vertices, indices, Vec3(1, 0, 0), 1.0f, 0.02f, Vec3(1, 0, 0));
    AddCube(vertices, indices, Vec3(1.0f, 0, 0), 0.08f, Vec3(1, 0, 0));
    
    // Y axis - Green
    AddArrow(vertices, indices, Vec3(0, 1, 0), 1.0f, 0.02f, Vec3(0, 1, 0));
    AddCube(vertices, indices, Vec3(0, 1.0f, 0), 0.08f, Vec3(0, 1, 0));
    
    // Z axis - Blue
    AddArrow(vertices, indices, Vec3(0, 0, 1), 1.0f, 0.02f, Vec3(0, 0, 1));
    AddCube(vertices, indices, Vec3(0, 0, 1.0f), 0.08f, Vec3(0, 0, 1));

    m_ScaleGeo.indexCount = static_cast<uint32_t>(indices.size());

    // Upload to GPU buffers
    m_ScaleGeo.vertexBuffer = new VulkanBuffer();
    m_ScaleGeo.vertexBuffer->Create(allocator, vertices.size() * sizeof(GizmoVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, vertices.data());
    m_ScaleGeo.indexBuffer = new VulkanBuffer();
    m_ScaleGeo.indexBuffer->Create(allocator, indices.size() * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, indices.data());
}

void VulkanGizmo::AddArrow(std::vector<GizmoVertex>& vertices, std::vector<uint16_t>& indices,
                           const Vec3& direction, float length, float radius, const Vec3& color) {
    // Simple arrow: cylinder shaft + cone tip
    uint16_t baseIndex = static_cast<uint16_t>(vertices.size());
    
    // Create a simple line for now (will enhance later)
    Vec3 start(0, 0, 0);
    Vec3 end = vscale(direction, length);
    Vec3 perp = (std::abs(direction.x) < 0.9f) ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    Vec3 tangent = vnorm(vcross(direction, perp));
    
    // Add vertices for a thin cylinder (8 segments)
    for (int i = 0; i < 8; ++i) {
        float angle = i * 3.14159f * 2.0f / 8.0f;
        Vec3 offset = vadd(vscale(tangent, std::cos(angle) * radius),
                          vscale(vcross(direction, tangent), std::sin(angle) * radius));
        
        vertices.push_back({vadd(start, offset), direction, color});
        vertices.push_back({vadd(end, offset), direction, color});
    }

    // Add indices for cylinder
    for (int i = 0; i < 8; ++i) {
        int next = (i + 1) % 8;
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

    // Add cone tip
    uint16_t tipBase = static_cast<uint16_t>(vertices.size());
    Vec3 coneStart = end;
    Vec3 coneEnd = vadd(end, vscale(direction, 0.2f));
    
    for (int i = 0; i < 8; ++i) {
        float angle = i * 3.14159f * 2.0f / 8.0f;
        Vec3 offset = vadd(vscale(tangent, std::cos(angle) * radius * 2.0f),
                          vscale(vcross(direction, tangent), std::sin(angle) * radius * 2.0f));
        
        vertices.push_back({vadd(coneStart, offset), direction, color});
    }
    vertices.push_back({coneEnd, direction, color});  // Tip point

    // Add indices for cone
    uint16_t tipPoint = static_cast<uint16_t>(vertices.size() - 1);
    for (int i = 0; i < 8; ++i) {
        int next = (i + 1) % 8;
        indices.push_back(tipBase + i);
        indices.push_back(tipPoint);
        indices.push_back(tipBase + next);
    }
}

void VulkanGizmo::AddTorusSegment(std::vector<GizmoVertex>& vertices, std::vector<uint16_t>& indices,
                                  const Vec3& axis, float radius, int segments, const Vec3& color) {
    // Create a ring/torus segment for rotation visualization
    uint16_t baseIndex = static_cast<uint16_t>(vertices.size());
    float tubeRadius = 0.02f;

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

void VulkanGizmo::Render(VkCommandBuffer cmd, VkDescriptorSet cameraDescriptor,
                        const Mat4& objectTransform, const Mat4& viewMatrix,
                        const Vec3& cameraPos, Mode mode, Space space) {
    // Select geometry based on mode
    GeometryBuffers* geo = nullptr;
    switch (mode) {
        case Mode::Translate: geo = &m_TranslateGeo; break;
        case Mode::Rotate: geo = &m_RotateGeo; break;
        case Mode::Scale: geo = &m_ScaleGeo; break;
    }

    if (!geo || !geo->vertexBuffer || !m_Pipeline) return;

    // Bind pipeline and descriptors
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                           0, 1, &cameraDescriptor, 0, nullptr);

    // Calculate gizmo transform (constant screen size)
    float gizmoScale = CalculateGizmoScale(
        Vec3(objectTransform.m[12], objectTransform.m[13], objectTransform.m[14]),
        cameraPos, Mat4::Identity()  // TODO: pass actual projection
    );

    // Build final transform matrix
    Mat4 scaleMatrix = Mat4::Scale(Vec3(gizmoScale, gizmoScale, gizmoScale));
    Mat4 gizmoTransform = (space == Space::World) 
        ? Mat4::Translation(Vec3(objectTransform.m[12], objectTransform.m[13], objectTransform.m[14]))
        : objectTransform;
    gizmoTransform = Mat4::Multiply(gizmoTransform, scaleMatrix);

    // Render each axis with different colors
    Vec4 colors[] = {
        Vec4(1.0f, 0.2f, 0.2f, 1.0f),  // Red - X axis
        Vec4(0.2f, 1.0f, 0.2f, 1.0f),  // Green - Y axis
        Vec4(0.2f, 0.2f, 1.0f, 1.0f)   // Blue - Z axis
    };

    // We will draw the whole gizmo at once, the color is in the vertex data
    GizmoPushConstants pc;
    pc.model = gizmoTransform;
    pc.color = Vec4(1,1,1,1); // Not used, color is per-vertex

    vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(GizmoPushConstants), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &geo->vertexBuffer->buffer, &offset);
    vkCmdBindIndexBuffer(cmd, geo->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, geo->indexCount, 1, 0, 0, 0);
}

int VulkanGizmo::CheckHover(const Ray& ray, const Mat4& objectTransform,
                           const Vec3& cameraPos, Mode mode, Space space) {
    // TODO: Implement ray-geometry intersection for gizmo components
    // Return axis index or -1
    return -1;
}

Mat4 VulkanGizmo::HandleDrag(const Ray& currentRay, const Ray& previousRay,
                            int activeAxis, const Mat4& objectTransform,
                            const Vec3& cameraPos, Mode mode, Space space) {
    // TODO: Implement drag manipulation logic
    // Calculate delta transform based on ray movement
    return Mat4::Identity();
}

float VulkanGizmo::CalculateGizmoScale(const Vec3& gizmoPos, const Vec3& cameraPos,
                                      const Mat4& projMatrix) const {
    // Make gizmo constant screen size regardless of distance
    float distance = vlen(vsub(gizmoPos, cameraPos));
    return distance * 0.1f;  // Scale factor to keep gizmo ~10% of view
}
