#include "VulkanLightGizmo.h"
#include "VulkanGizmo.h"
#include "GizmoGeometry.h"
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
    pcr.size       = sizeof(VulkanGizmo::GizmoPushConstants);

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

void VulkanLightGizmo::DrawRange(VkCommandBuffer cmd, uint32_t firstIndex, uint32_t indexCount,
                                 const Mat4& model, const Vec4& state) const {
    VulkanGizmo::GizmoPushConstants pc;
    pc.model = model;
    pc.state = state;
    vkCmdPushConstants(cmd, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cmd, indexCount, 1, firstIndex, 0, 0);
}

void VulkanLightGizmo::Draw(VkCommandBuffer cmd, VkDescriptorSet cameraDescSet,
                            const Mat4& worldMatrix, const Mat4& viewMatrix,
                            const Mat4& projMatrix, const Vec3& cameraPos,
                            bool isSelected, bool isHovered) {
    if (!m_pipeline || !m_geo.vertexBuffer || !m_geo.indexBuffer) return;

    const Vec3 origin(worldMatrix.m[12], worldMatrix.m[13], worldMatrix.m[14]);
    const float scale = GizmoScreenScale(origin, cameraPos, projMatrix, kScreenScale);
    const Mat4 translation = Mat4::Translation(origin);
    const Mat4 scaleMat = Mat4::Scale(Vec3(scale, scale, scale));

    // Entity scale must not stretch the gizmo, so only the orientation is kept.
    const Mat4 rotation = GizmoBasis(
        vnorm(Vec3(worldMatrix.m[0], worldMatrix.m[1], worldMatrix.m[2])),
        vnorm(Vec3(worldMatrix.m[4], worldMatrix.m[5], worldMatrix.m[6])),
        vnorm(Vec3(worldMatrix.m[8], worldMatrix.m[9], worldMatrix.m[10])));
    const Vec3 toCamera = GizmoToCamera(origin, cameraPos, viewMatrix, projMatrix);
    const Mat4 billboard = GizmoBillboardRotation(toCamera, viewMatrix);

    const Vec4 state(isHovered ? GizmoHandle::LightBody : -1.0f,
                     isSelected ? GizmoHandle::LightBody : -1.0f, scale, 0.0f);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    uint32_t zeroOffset = 0;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 1, &cameraDescSet, 1, &zeroOffset);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_geo.vertexBuffer->buffer, &offset);
    vkCmdBindIndexBuffer(cmd, m_geo.indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT16);

    DrawRange(cmd, 0, m_bodyIndexCount,
              Mat4::Multiply(translation, Mat4::Multiply(rotation, scaleMat)), state);
    DrawRange(cmd, m_bodyIndexCount, m_geo.indexCount - m_bodyIndexCount,
              Mat4::Multiply(translation, Mat4::Multiply(billboard, scaleMat)), state);
}

// ── Geometry ────────────────────────────────────────────────────────────────

void VulkanLightGizmo::GenerateGeometry(VmaAllocator allocator) {
    using namespace GizmoGeometry;
    Vertices verts;
    Indices  indices;

    const Vec3 forward(0.0f, 0.0f, -1.0f);
    const Vec3 color(0xE3 / 255.0f, 0xA2 / 255.0f, 0x3B / 255.0f); // theme status-warning
    const float handle = GizmoHandle::LightBody;

    // Direction arrow, starting just outside the sun glyph.
    const float coneLength = 0.28f;
    const Vec3 shaftStart = vscale(forward, kSunRadius * 1.6f);
    const Vec3 shaftEnd = vscale(forward, kArrowLength - coneLength);
    AddCylinder(verts, indices, shaftStart, shaftEnd, kArrowRadius, color, handle);
    AddCone(verts, indices, shaftEnd, vscale(forward, kArrowLength), kArrowRadius * 3.0f,
            color, handle);

    // Dashed parallel rays around the arrow.
    const Vec3 offsets[4] = { Vec3(kRayOffset, 0, 0), Vec3(-kRayOffset, 0, 0),
                              Vec3(0, kRayOffset, 0), Vec3(0, -kRayOffset, 0) };
    const float dash = 0.22f;
    const float gap = 0.14f;
    for (const Vec3& off : offsets) {
        for (float d = kRayStart; d + dash <= kRayStart + kRayLength + 1e-4f; d += dash + gap) {
            AddCylinder(verts, indices, vadd(off, vscale(forward, d)),
                        vadd(off, vscale(forward, d + dash)), kRayRadius, color, handle, 6);
        }
    }
    m_bodyIndexCount = static_cast<uint32_t>(indices.size());

    // Sun glyph in the XY plane, drawn camera-facing: a ring and eight short rays.
    AddAnnulus(verts, indices, kSunRadius * 0.78f, kSunRadius, color, handle, 48);
    const float rayInner = kSunRadius * 1.3f;
    const float rayOuter = kSunRadius * 1.85f;
    const float halfWidth = kSunRadius * 0.09f;
    for (int i = 0; i < 8; ++i) {
        const float a = i * 2.0f * kPi / 8.0f;
        const Vec3 dir(std::cos(a), std::sin(a), 0.0f);
        const Vec3 side(-dir.y * halfWidth, dir.x * halfWidth, 0.0f);
        const uint16_t base = Base(verts);
        const Vec3 n(0, 0, 1);
        verts.push_back({vsub(vscale(dir, rayInner), side), n, color, handle});
        verts.push_back({vadd(vscale(dir, rayInner), side), n, color, handle});
        verts.push_back({vadd(vscale(dir, rayOuter), side), n, color, handle});
        verts.push_back({vsub(vscale(dir, rayOuter), side), n, color, handle});
        indices.insert(indices.end(), {base, static_cast<uint16_t>(base + 1),
                                       static_cast<uint16_t>(base + 2), base,
                                       static_cast<uint16_t>(base + 2),
                                       static_cast<uint16_t>(base + 3)});
    }

    UploadGeometry(m_geo.vertexBuffer, m_geo.indexBuffer, m_geo.indexCount,
                   allocator, verts, indices);
}
