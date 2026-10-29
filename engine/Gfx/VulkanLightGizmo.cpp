#include "VulkanLightGizmo.h"
#include "VulkanGizmo.h"   // for GizmoVertex
#include "VulkanBuffer.h"
#include "VulkanSwapchain.h"
#include <iostream>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <algorithm>

// ── helpers (local) ──────────────────────────────────────────────────────────

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("VulkanLightGizmo: failed to open file: " + filename);
    size_t sz = static_cast<size_t>(file.tellg());
    std::vector<char> buf(sz);
    file.seekg(0);
    file.read(buf.data(), sz);
    return buf;
}

static VkShaderModule createShaderModule(VkDevice device,
                                         const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule sm;
    if (vkCreateShaderModule(device, &ci, nullptr, &sm) != VK_SUCCESS)
        throw std::runtime_error("VulkanLightGizmo: failed to create shader module");
    return sm;
}

static constexpr float PI = 3.14159265358979f;

// ── VulkanLightGizmo implementation ─────────────────────────────────────────

VulkanLightGizmo::VulkanLightGizmo()  = default;
VulkanLightGizmo::~VulkanLightGizmo() = default;

// ── Init / Recreate / Cleanup ───────────────────────────────────────────────

void VulkanLightGizmo::Init(VkDevice device, VkRenderPass renderPass,
                            VkExtent2D extent,
                            VkDescriptorSetLayout cameraDescLayout,
                            VmaAllocator allocator) {
    GenerateGeometry(allocator);
    CreatePipeline(device, renderPass, extent, cameraDescLayout);
    std::cout << "[VulkanLightGizmo] Initialised\n";
}

void VulkanLightGizmo::Recreate(VkDevice device, VkRenderPass renderPass,
                                VkExtent2D extent,
                                VkDescriptorSetLayout cameraDescLayout) {
    if (m_pipeline) {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    CreatePipeline(device, renderPass, extent, cameraDescLayout);
}

void VulkanLightGizmo::Cleanup(VkDevice device, VmaAllocator allocator) {
    if (m_pipeline)       { vkDestroyPipeline(device, m_pipeline, nullptr);             m_pipeline       = VK_NULL_HANDLE; }
    if (m_pipelineLayout) { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }

    if (m_geo.vertexBuffer) { m_geo.vertexBuffer->Destroy(allocator); delete m_geo.vertexBuffer; m_geo.vertexBuffer = nullptr; }
    if (m_geo.indexBuffer)  { m_geo.indexBuffer->Destroy(allocator);  delete m_geo.indexBuffer;  m_geo.indexBuffer  = nullptr; }
    m_geo.indexCount = 0;
}

// ── Pipeline creation (reuses gizmo.vert + gizmo.frag) ─────────────────────

void VulkanLightGizmo::CreatePipeline(VkDevice device, VkRenderPass renderPass,
                                      VkExtent2D extent,
                                      VkDescriptorSetLayout cameraDescLayout) {
    auto vertCode = readFile("shaders/gizmo.vert.spv");
    auto fragCode = readFile("shaders/gizmo.frag.spv");

    VkShaderModule vertShader = createShaderModule(device, vertCode);
    VkShaderModule fragShader = createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertShader;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragShader;
    stages[1].pName  = "main";

    auto bindDesc = GizmoVertex::getBindingDescription();
    auto attrDesc = GizmoVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &bindDesc;
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDesc.size());
    vi.pVertexAttributeDescriptions    = attrDesc.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpState.viewportCount = 1;
    vpState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // No depth test – always draw on top
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp   = VK_COMPARE_OP_ALWAYS;

    // Alpha blending for slightly translucent gizmo (optional)
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    cba.blendEnable    = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments    = &cba;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &cameraDescLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("VulkanLightGizmo: failed to create pipeline layout");

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates    = dynStates;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount          = 2;
    pci.pStages             = stages;
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vpState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = &ds;
    pci.pColorBlendState    = &blend;
    pci.pDynamicState       = &dynState;
    pci.layout              = m_pipelineLayout;
    pci.renderPass          = renderPass;
    pci.subpass             = VulkanSwapchain::kOverlaySubpass;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("VulkanLightGizmo: failed to create pipeline");

    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
}

// ── Draw ────────────────────────────────────────────────────────────────────

float VulkanLightGizmo::CalculateScale(const Vec3& gizmoPos,
                                       const Vec3& cameraPos,
                                       const Mat4& projMatrix) const {
    float dist = vlen(vsub(gizmoPos, cameraPos));
    float projScale = 1.0f / std::max(std::abs(projMatrix.m[0]), 0.001f);
    return dist * projScale * kScreenScale;
}

void VulkanLightGizmo::Draw(VkCommandBuffer cmd, VkDescriptorSet cameraDescSet,
                            const Mat4& worldMatrix, const Vec3& cameraPos,
                            const Mat4& projMatrix, bool isSelected,
                            bool isHovered) {
    if (!m_pipeline || !m_geo.vertexBuffer || !m_geo.indexBuffer) return;

    Vec3 origin(worldMatrix.m[12], worldMatrix.m[13], worldMatrix.m[14]);
    float scale = std::max(CalculateScale(origin, cameraPos, projMatrix), 0.001f);

    // Build orientation from world matrix columns (entity's local axes)
    Mat4 rotation = Mat4::Identity();
    // X axis
    rotation.m[0]  = worldMatrix.m[0];
    rotation.m[1]  = worldMatrix.m[1];
    rotation.m[2]  = worldMatrix.m[2];
    // Y axis
    rotation.m[4]  = worldMatrix.m[4];
    rotation.m[5]  = worldMatrix.m[5];
    rotation.m[6]  = worldMatrix.m[6];
    // Z axis
    rotation.m[8]  = worldMatrix.m[8];
    rotation.m[9]  = worldMatrix.m[9];
    rotation.m[10] = worldMatrix.m[10];

    Mat4 translation = Mat4::Translation(origin);
    Mat4 scaleMat    = Mat4::Scale(Vec3(scale, scale, scale));
    Mat4 model       = Mat4::Multiply(translation, Mat4::Multiply(rotation, scaleMat));

    // Encode hover/selection into gizmo state so the fragment shader highlights.
    // The gizmo.frag "hovered axis" and "active axis" use colour-channel matching:
    //   axis 0 = mostly-red, 1 = mostly-green, 2 = mostly-blue
    // Our geometry is golden-yellow (R≈1, G≈0.85) which the shader maps to axis 0
    // (red-dominant). So hover = 0 and active = 0 triggers the highlight.
    float hoveredAxis = isHovered  ? 0.0f : -1.0f;
    float activeAxis  = isSelected ? 0.0f : -1.0f;

    PushConstants pc;
    pc.model = model;
    pc.state = Vec4(hoveredAxis, activeAxis, scale, 0.0f);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    uint32_t zeroOffset = 0;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 1, &cameraDescSet, 1, &zeroOffset);

    vkCmdPushConstants(cmd, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_geo.vertexBuffer->buffer, &offset);
    vkCmdBindIndexBuffer(cmd, m_geo.indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, m_geo.indexCount, 1, 0, 0, 0);
}

// ── Geometry generation ─────────────────────────────────────────────────────
//
// The gizmo is built at the origin pointing along -Z (the conventional
// "forward" direction in Hephaestus).  It consists of:
//   1.  A central direction arrow (shaft + cone tip).
//   2.  Four peripheral parallel ray lines showing light spread.
//   3.  A small circle/disk at the base to mark the origin.
//
// All geometry uses a warm golden colour (Vec3(1.0, 0.85, 0.35)) so it's
// clearly distinguishable from the RGB transform gizmo.
// ─────────────────────────────────────────────────────────────────────────────

void VulkanLightGizmo::GenerateGeometry(VmaAllocator allocator) {
    std::vector<GizmoVertex> verts;
    std::vector<uint16_t>    indices;

    const Vec3 forward(0.0f, 0.0f, -1.0f);       // -Z forward
    const Vec3 color(1.0f, 0.85f, 0.35f);         // warm golden yellow

    // 1. Central arrow — shaft + cone tip
    AddArrow(verts, indices, Vec3(0, 0, 0), forward, kArrowLength, kArrowRadius, color);

    // 2. Four peripheral ray lines (no cone tip) offset in X/Y
    const Vec3 offsets[4] = {
        Vec3( kRayOffset, 0, 0),
        Vec3(-kRayOffset, 0, 0),
        Vec3(0,  kRayOffset, 0),
        Vec3(0, -kRayOffset, 0),
    };
    for (const auto& off : offsets) {
        AddLine(verts, indices, off, vadd(off, vscale(forward, kRayLength)),
                kRayRadius, color);
    }

    // 3. Origin circle (torus ring in the XY plane at origin)
    AddCircle(verts, indices, Vec3(0, 0, 0), Vec3(0, 0, 1),
              kCircleRadius, kCircleTube, 24, color);

    // Upload
    m_geo.indexCount = static_cast<uint32_t>(indices.size());

    m_geo.vertexBuffer = new VulkanBuffer();
    m_geo.vertexBuffer->Create(allocator,
        verts.size() * sizeof(GizmoVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        verts.data());

    m_geo.indexBuffer = new VulkanBuffer();
    m_geo.indexBuffer->Create(allocator,
        indices.size() * sizeof(uint16_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        indices.data());
}

// ── Geometry helpers ────────────────────────────────────────────────────────

void VulkanLightGizmo::AddArrow(std::vector<GizmoVertex>& verts,
                                std::vector<uint16_t>& indices,
                                const Vec3& start, const Vec3& direction,
                                float length, float radius,
                                const Vec3& color) {
    uint16_t base = static_cast<uint16_t>(verts.size());
    Vec3 dir = vnorm(direction);
    float coneLen  = length * 0.22f;
    float shaftLen = length - coneLen;
    Vec3  shaftEnd = vadd(start, vscale(dir, shaftLen));
    Vec3  tip      = vadd(start, vscale(dir, length));

    // Build perpendicular basis
    Vec3 perp = (std::abs(dir.x) < 0.9f) ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    Vec3 tangent   = vnorm(vcross(dir, perp));
    Vec3 bitangent = vnorm(vcross(dir, tangent));

    const int seg = 12;

    // Shaft cylinder
    for (int i = 0; i < seg; ++i) {
        float a = i * 2.0f * PI / seg;
        Vec3 off = vadd(vscale(tangent, std::cos(a) * radius),
                        vscale(bitangent, std::sin(a) * radius));
        verts.push_back({vadd(start, off),    dir, color});
        verts.push_back({vadd(shaftEnd, off), dir, color});
    }
    for (int i = 0; i < seg; ++i) {
        int n   = (i + 1) % seg;
        uint16_t i0 = base + i * 2;
        uint16_t i1 = base + i * 2 + 1;
        uint16_t i2 = base + n * 2;
        uint16_t i3 = base + n * 2 + 1;
        indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
        indices.push_back(i2); indices.push_back(i1); indices.push_back(i3);
    }

    // Cone tip
    uint16_t coneBase = static_cast<uint16_t>(verts.size());
    float coneR = radius * 2.5f;
    for (int i = 0; i < seg; ++i) {
        float a = i * 2.0f * PI / seg;
        Vec3 off = vadd(vscale(tangent, std::cos(a) * coneR),
                        vscale(bitangent, std::sin(a) * coneR));
        verts.push_back({vadd(shaftEnd, off), dir, color});
    }
    verts.push_back({tip, dir, color});
    uint16_t tipIdx = static_cast<uint16_t>(verts.size() - 1);
    for (int i = 0; i < seg; ++i) {
        int n = (i + 1) % seg;
        indices.push_back(coneBase + i);
        indices.push_back(tipIdx);
        indices.push_back(coneBase + n);
    }
}

void VulkanLightGizmo::AddLine(std::vector<GizmoVertex>& verts,
                               std::vector<uint16_t>& indices,
                               const Vec3& start, const Vec3& end,
                               float radius, const Vec3& color) {
    // Thin cylinder (no cone)
    uint16_t base = static_cast<uint16_t>(verts.size());
    Vec3 dir = vnorm(vsub(end, start));

    Vec3 perp = (std::abs(dir.x) < 0.9f) ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    Vec3 tangent   = vnorm(vcross(dir, perp));
    Vec3 bitangent = vnorm(vcross(dir, tangent));

    const int seg = 8;
    for (int i = 0; i < seg; ++i) {
        float a = i * 2.0f * PI / seg;
        Vec3 off = vadd(vscale(tangent, std::cos(a) * radius),
                        vscale(bitangent, std::sin(a) * radius));
        verts.push_back({vadd(start, off), dir, color});
        verts.push_back({vadd(end,   off), dir, color});
    }
    for (int i = 0; i < seg; ++i) {
        int n = (i + 1) % seg;
        uint16_t i0 = base + i * 2;
        uint16_t i1 = base + i * 2 + 1;
        uint16_t i2 = base + n * 2;
        uint16_t i3 = base + n * 2 + 1;
        indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
        indices.push_back(i2); indices.push_back(i1); indices.push_back(i3);
    }
}

void VulkanLightGizmo::AddCircle(std::vector<GizmoVertex>& verts,
                                 std::vector<uint16_t>& indices,
                                 const Vec3& center, const Vec3& axis,
                                 float radius, float tubeRadius,
                                 int segments, const Vec3& color) {
    uint16_t base = static_cast<uint16_t>(verts.size());
    Vec3 axisN = vnorm(axis);

    // Build tangent frame on the plane perpendicular to axis
    Vec3 perp = (std::abs(axisN.x) < 0.9f) ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    Vec3 u = vnorm(vcross(axisN, perp));
    Vec3 v = vnorm(vcross(axisN, u));

    for (int i = 0; i <= segments; ++i) {
        float a = i * 2.0f * PI / segments;
        Vec3 ringPos = vadd(center,
                            vadd(vscale(u, std::cos(a) * radius),
                                 vscale(v, std::sin(a) * radius)));
        Vec3 normal = vnorm(vsub(ringPos, center));

        // Two verts per ring position (offset along ring's local normal)
        Vec3 off1 = vscale(axisN,  tubeRadius);
        Vec3 off2 = vscale(axisN, -tubeRadius);

        verts.push_back({vadd(ringPos, off1), normal, color});
        verts.push_back({vadd(ringPos, off2), normal, color});
    }

    for (int i = 0; i < segments; ++i) {
        uint16_t i0 = base + i * 2;
        uint16_t i1 = base + i * 2 + 1;
        uint16_t i2 = base + (i + 1) * 2;
        uint16_t i3 = base + (i + 1) * 2 + 1;
        indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
        indices.push_back(i2); indices.push_back(i1); indices.push_back(i3);
    }
}
