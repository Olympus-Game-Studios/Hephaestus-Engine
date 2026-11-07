#include "VulkanGizmo.h"
#include "VulkanBuffer.h"
#include "VulkanSwapchain.h"
#include "GizmoGeometry.h"
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

namespace {

using namespace GizmoGeometry;

const Vec3 kCenterColor(0.80f, 0.82f, 0.86f);
const Vec3 kAxes[3] = { Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1) };

} // namespace

float GizmoScreenScale(const Vec3& gizmoPos, const Vec3& cameraPos, const Mat4& projMatrix,
                       float screenFactor) {
    if (std::abs(projMatrix.m[15] - 1.0f) < 0.001f &&
        std::abs(projMatrix.m[5]) > 1e-5f) {
        const float halfHeight = 1.0f / std::abs(projMatrix.m[5]);
        return std::max(halfHeight * 2.0f * screenFactor, 0.001f);
    }
    float distance = vlen(vsub(gizmoPos, cameraPos));
    float f = std::abs(projMatrix.m[5]);
    if (f > 1e-5f) {
        float fovY = 2.0f * std::atan(1.0f / f);
        float scale = distance * std::tan(fovY * 0.5f) * screenFactor;
        return std::max(scale, 0.001f);
    }
    return std::max(distance * screenFactor, 0.001f);
}

Vec3 GizmoToCamera(const Vec3& gizmoPos, const Vec3& cameraPos, const Mat4& viewMatrix,
                   const Mat4& projMatrix) {
    const Vec3 viewZ = vnorm(Vec3(viewMatrix.m[2], viewMatrix.m[6], viewMatrix.m[10]));
    const bool ortho = std::abs(projMatrix.m[15] - 1.0f) < 0.001f;
    const Vec3 toEye = vsub(cameraPos, gizmoPos);
    if (ortho || vdot(toEye, toEye) < 1e-8f) {
        return viewZ;
    }
    return vnorm(toEye);
}

Mat4 GizmoBasis(const Vec3& x, const Vec3& y, const Vec3& z) {
    Mat4 m = Mat4::Identity();
    m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
    m.m[4] = y.x; m.m[5] = y.y; m.m[6] = y.z;
    m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
    return m;
}

Mat4 GizmoBillboardRotation(const Vec3& toCamera, const Mat4& viewMatrix) {
    Vec3 up = vnorm(Vec3(viewMatrix.m[1], viewMatrix.m[5], viewMatrix.m[9]));
    if (std::abs(vdot(up, toCamera)) > 0.999f) {
        up = vnorm(Vec3(viewMatrix.m[0], viewMatrix.m[4], viewMatrix.m[8]));
    }
    const Vec3 right = vnorm(vcross(up, toCamera));
    const Vec3 trueUp = vcross(toCamera, right);
    return GizmoBasis(right, trueUp, toCamera);
}

VulkanGizmo::VulkanGizmo() {
}

VulkanGizmo::~VulkanGizmo() {
}

void VulkanGizmo::Init(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                       VkDescriptorSetLayout cameraDescriptorLayout, VmaAllocator allocator) {
    GenerateTranslateGeometry(allocator);
    GenerateRotateGeometry(allocator);
    GenerateScaleGeometry(allocator);
    GenerateBillboardGeometry(allocator);

    CreatePipeline(device, renderPass, extent, cameraDescriptorLayout);

    std::cout << "VulkanGizmo initialized" << std::endl;
}

void VulkanGizmo::Recreate(VkDevice device, VkRenderPass renderPass, VkExtent2D extent,
                           VkDescriptorSetLayout cameraDescriptorLayout) {
    if (m_Pipeline) {
        vkDestroyPipeline(device, m_Pipeline, nullptr);
        m_Pipeline = VK_NULL_HANDLE;
    }
    if (m_PipelineLayout) {
        vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }

    CreatePipeline(device, renderPass, extent, cameraDescriptorLayout);
}

void VulkanGizmo::Cleanup(VkDevice device, VmaAllocator allocator) {
    if (m_Pipeline) vkDestroyPipeline(device, m_Pipeline, nullptr);
    if (m_PipelineLayout) vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
    m_Pipeline = VK_NULL_HANDLE;
    m_PipelineLayout = VK_NULL_HANDLE;

    for (GeometryBuffers* geo : { &m_TranslateGeo, &m_RotateGeo, &m_ScaleGeo, &m_BillboardGeo }) {
        if (geo->vertexBuffer) {
            geo->vertexBuffer->Destroy(allocator);
            delete geo->vertexBuffer;
            geo->vertexBuffer = nullptr;
        }
        if (geo->indexBuffer) {
            geo->indexBuffer->Destroy(allocator);
            delete geo->indexBuffer;
            geo->indexBuffer = nullptr;
        }
        geo->indexCount = 0;
    }
}

void VulkanGizmo::GenerateTranslateGeometry(VmaAllocator allocator) {
    Vertices vertices;
    Indices indices;

    const float coneLength = 0.3f;
    for (int axis = 0; axis < 3; ++axis) {
        const Vec3 color = GizmoAxisColor(axis);
        const float handle = GizmoHandle::AxisX + axis;
        const Vec3 shaftEnd = vscale(kAxes[axis], kTranslateLength - coneLength);
        AddCylinder(vertices, indices, vscale(kAxes[axis], kCenterDotRadius * 1.5f), shaftEnd,
                    kShaftRadius, color, handle);
        AddCone(vertices, indices, shaftEnd, vscale(kAxes[axis], kTranslateLength),
                0.075f, color, handle);
    }

    // Each plane square takes the colour of the axis it locks.
    AddPlaneSquare(vertices, indices, kAxes[0], kAxes[1], kPlaneMin, kPlaneMax,
                   GizmoAxisColor(2), GizmoHandle::PlaneXY);
    AddPlaneSquare(vertices, indices, kAxes[0], kAxes[2], kPlaneMin, kPlaneMax,
                   GizmoAxisColor(1), GizmoHandle::PlaneXZ);
    AddPlaneSquare(vertices, indices, kAxes[1], kAxes[2], kPlaneMin, kPlaneMax,
                   GizmoAxisColor(0), GizmoHandle::PlaneYZ);

    UploadGeometry(m_TranslateGeo.vertexBuffer, m_TranslateGeo.indexBuffer,
                   m_TranslateGeo.indexCount, allocator, vertices, indices);
}

void VulkanGizmo::GenerateRotateGeometry(VmaAllocator allocator) {
    Vertices vertices;
    Indices indices;

    for (int axis = 0; axis < 3; ++axis) {
        AddTorus(vertices, indices, kAxes[axis], kRotateRadius, kRingTubeRadius,
                 GizmoAxisColor(axis), GizmoHandle::RingX + axis);
    }

    UploadGeometry(m_RotateGeo.vertexBuffer, m_RotateGeo.indexBuffer,
                   m_RotateGeo.indexCount, allocator, vertices, indices);
}

void VulkanGizmo::GenerateScaleGeometry(VmaAllocator allocator) {
    Vertices vertices;
    Indices indices;

    for (int axis = 0; axis < 3; ++axis) {
        const Vec3 color = GizmoAxisColor(axis);
        const float handle = GizmoHandle::AxisX + axis;
        const Vec3 tip = vscale(kAxes[axis], kScaleLength);
        AddCylinder(vertices, indices, vscale(kAxes[axis], kUniformCubeSize * 0.6f),
                    vscale(kAxes[axis], kScaleLength - kScaleCubeSize * 0.5f),
                    kShaftRadius, color, handle);
        AddBox(vertices, indices, tip, kScaleCubeSize, color, handle);
    }
    AddBox(vertices, indices, Vec3(0, 0, 0), kUniformCubeSize, kCenterColor,
           GizmoHandle::Center);

    UploadGeometry(m_ScaleGeo.vertexBuffer, m_ScaleGeo.indexBuffer,
                   m_ScaleGeo.indexCount, allocator, vertices, indices);
}

void VulkanGizmo::GenerateBillboardGeometry(VmaAllocator allocator) {
    Vertices vertices;
    Indices indices;

    m_CenterDotRange.first = static_cast<uint32_t>(indices.size());
    AddDisc(vertices, indices, kCenterDotRadius, kCenterColor, GizmoHandle::Center, 32);
    m_CenterDotRange.count = static_cast<uint32_t>(indices.size()) - m_CenterDotRange.first;

    m_ViewRingRange.first = static_cast<uint32_t>(indices.size());
    AddAnnulus(vertices, indices, kViewRingRadius - kRingTubeRadius * 0.8f,
               kViewRingRadius + kRingTubeRadius * 0.8f, Vec3(0.62f, 0.66f, 0.72f),
               GizmoHandle::ViewRing);
    m_ViewRingRange.count = static_cast<uint32_t>(indices.size()) - m_ViewRingRange.first;

    m_ArcDiscRange.first = static_cast<uint32_t>(indices.size());
    AddDisc(vertices, indices, 1.0f, Vec3(1, 1, 1), GizmoHandle::RotateArc, 128);
    m_ArcDiscRange.count = static_cast<uint32_t>(indices.size()) - m_ArcDiscRange.first;

    UploadGeometry(m_BillboardGeo.vertexBuffer, m_BillboardGeo.indexBuffer,
                   m_BillboardGeo.indexCount, allocator, vertices, indices);
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
        case Mode::Translate: return m_CurrentScale * kTranslateLength;
        case Mode::Scale:     return m_CurrentScale * (kScaleLength + kScaleCubeSize * 0.5f);
        case Mode::Rotate:    return m_CurrentScale * kRotateRadius;
        default:              return m_CurrentScale;
    }
}

bool VulkanGizmo::IsAxisPickable(int axis) const {
    return std::abs(vdot(m_AxisDirections[axis], m_ToCamera)) < 0.97f;
}

bool VulkanGizmo::IsPlanePickable(int plane) const {
    const int normalAxes[3] = {2, 1, 0};
    return std::abs(vdot(m_AxisDirections[normalAxes[plane]], m_ToCamera)) > 0.15f;
}

float VulkanGizmo::AxisSignTowardCamera(int axis) const {
    return vdot(m_AxisDirections[axis], m_ToCamera) >= 0.0f ? 1.0f : -1.0f;
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

float VulkanGizmo::RayDistanceToRing(const Ray& ray, const Vec3& axisDir, float radius,
                                     bool frontHalfOnly) const {
    // Sampling the ring keeps rings pickable when they are seen edge on.
    Vec3 u, v;
    OrthonormalBasis(vnorm(axisDir), u, v);
    const int samples = 96;
    float best = std::numeric_limits<float>::max();
    for (int i = 0; i < samples; ++i) {
        const float a = i * 2.0f * kPi / samples;
        const Vec3 offset = vscale(vadd(vscale(u, std::cos(a)), vscale(v, std::sin(a))), radius);
        if (frontHalfOnly && vdot(offset, m_ToCamera) < -0.02f * radius) {
            continue;
        }
        const Vec3 toPoint = vsub(vadd(m_GizmoOrigin, offset), ray.origin);
        const float t = vdot(toPoint, ray.direction);
        if (t < 0.0f) {
            continue;
        }
        best = std::min(best, vlen(vsub(toPoint, vscale(ray.direction, t))));
    }
    return best;
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
    colorAttach.blendEnable = VK_TRUE;
    colorAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttach.colorBlendOp = VK_BLEND_OP_ADD;
    colorAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttach.alphaBlendOp = VK_BLEND_OP_ADD;

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
    pci.subpass = VulkanSwapchain::kOverlaySubpass;

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
    m_ToCamera = GizmoToCamera(m_GizmoOrigin, cameraPos, viewMatrix, projMatrix);

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

void VulkanGizmo::DrawRange(VkCommandBuffer cmd, const GeometryBuffers& geo, IndexRange range,
                            const Mat4& model) const {
    if (!geo.vertexBuffer || !geo.indexBuffer || range.count == 0) return;

    GizmoPushConstants pc;
    pc.model = model;
    pc.state = Vec4(static_cast<float>(m_HoveredAxis), static_cast<float>(m_ActiveAxis),
                    m_CurrentScale, m_RotationAngle);
    vkCmdPushConstants(cmd, m_PipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(GizmoPushConstants), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &geo.vertexBuffer->buffer, &offset);
    vkCmdBindIndexBuffer(cmd, geo.indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, range.count, 1, range.first, 0, 0);
}

void VulkanGizmo::Render(VkCommandBuffer cmd, VkDescriptorSet cameraDescriptor) {
    if (!m_StateValid || !m_Pipeline) return;

    GeometryBuffers* geo = GetGeometryForMode();
    if (!geo || !geo->vertexBuffer || !geo->indexBuffer) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    uint32_t zeroOffset = 0;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                           0, 1, &cameraDescriptor, 1, &zeroOffset);

    const Mat4 translation = Mat4::Translation(m_GizmoOrigin);
    const Mat4 scale = Mat4::Scale(Vec3(m_CurrentScale, m_CurrentScale, m_CurrentScale));
    const Mat4 rotation = GizmoBasis(m_AxisDirections[0], m_AxisDirections[1], m_AxisDirections[2]);
    const Mat4 billboard = Mat4::Multiply(translation,
        Mat4::Multiply(GizmoBillboardRotation(m_ToCamera, m_ViewMatrix), scale));

    if (m_Mode == Mode::Rotate && m_IsDragging && m_RotationStartValid &&
        m_ActiveAxis >= 0 && m_ActiveAxis <= 3) {
        const float radius = m_ActiveAxis == 3 ? kViewRingRadius : kRotateRadius;
        const Vec3 u = m_RotationStartVector;
        const Vec3 w = vnorm(m_DragAxisDir);
        const Vec3 v = vcross(w, u);
        const Mat4 arcScale = Mat4::Scale(Vec3(m_CurrentScale * radius, m_CurrentScale * radius,
                                               m_CurrentScale * radius));
        DrawRange(cmd, m_BillboardGeo, m_ArcDiscRange,
                  Mat4::Multiply(translation, Mat4::Multiply(GizmoBasis(u, v, w), arcScale)));
    }

    DrawRange(cmd, *geo, {0, geo->indexCount},
              Mat4::Multiply(translation, Mat4::Multiply(rotation, scale)));

    if (m_Mode == Mode::Translate) {
        DrawRange(cmd, m_BillboardGeo, m_CenterDotRange, billboard);
    } else if (m_Mode == Mode::Rotate) {
        DrawRange(cmd, m_BillboardGeo, m_ViewRingRange, billboard);
    }
}

int VulkanGizmo::CheckHover(const Ray& ray) const {
    if (!m_StateValid) return -1;

    int bestAxis = -1;
    float bestScore = std::numeric_limits<float>::max();

    if (m_Mode == Mode::Rotate) {
        const float threshold = m_CurrentScale * kRingPickRadius;
        for (int axis = 0; axis < 3; ++axis) {
            const float distance = RayDistanceToRing(ray, m_AxisDirections[axis],
                                                     m_CurrentScale * kRotateRadius, true);
            if (distance < threshold && distance < bestScore) {
                bestScore = distance;
                bestAxis = axis;
            }
        }
        Vec3 hit;
        if (IntersectPlane(ray, m_GizmoOrigin, m_ToCamera, hit)) {
            const float distance = std::abs(vlen(vsub(hit, m_GizmoOrigin)) -
                                            m_CurrentScale * kViewRingRadius);
            if (distance < threshold && distance < bestScore) {
                bestAxis = 3;
            }
        }
        return bestAxis;
    }

    const float threshold = m_CurrentScale * kAxisPickRadius;
    for (int axis = 0; axis < 3; ++axis) {
        if (!IsAxisPickable(axis)) continue;
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

    // Centre handle: free translation or uniform scale.
    Vec3 toOrigin = vsub(m_GizmoOrigin, ray.origin);
    float t = vdot(toOrigin, ray.direction);
    if (t > 0.0f) {
        Vec3 closestPoint = vadd(ray.origin, vscale(ray.direction, t));
        float dist = vlen(vsub(closestPoint, m_GizmoOrigin));
        if (dist < m_CurrentScale * kCenterPickRadius && dist < bestScore) {
            bestScore = dist;
            bestAxis = 3;
        }
    }

    if (m_Mode == Mode::Translate && bestAxis < 0) {
        const int axisPairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
        const int normalAxes[3] = {2, 1, 0};
        for (int plane = 0; plane < 3; ++plane) {
            if (!IsPlanePickable(plane)) continue;
            Vec3 hit;
            if (!IntersectPlane(ray, m_GizmoOrigin,
                                m_AxisDirections[normalAxes[plane]], hit)) {
                continue;
            }
            const int a = axisPairs[plane][0];
            const int b = axisPairs[plane][1];
            const Vec3 offset = vsub(hit, m_GizmoOrigin);
            const float u = AxisSignTowardCamera(a) * vdot(offset, m_AxisDirections[a]) / m_CurrentScale;
            const float v = AxisSignTowardCamera(b) * vdot(offset, m_AxisDirections[b]) / m_CurrentScale;
            if (u >= kPlaneMin && u <= kPlaneMax && v >= kPlaneMin && v <= kPlaneMax) {
                bestAxis = 4 + plane; // 4=XY, 5=XZ, 6=YZ
                break;
            }
        }
    }

    return bestAxis;
}

void VulkanGizmo::SetHoveredAxis(int axis) {
    m_HoveredAxis = axis;
}

void VulkanGizmo::BeginDrag(const Ray& ray, int axis) {
    if (!m_StateValid || axis < 0 || axis > 6) return;

    m_IsDragging = true;
    m_ActiveAxis = axis;
    m_DragStartTransform = m_ObjectTransform;
    m_DragStartPosition = Vec3(m_ObjectTransform.m[12], m_ObjectTransform.m[13], m_ObjectTransform.m[14]);
    if (axis < 3)
        m_DragAxisDir = m_AxisDirections[axis];
    else if (axis == 3 && m_Mode == Mode::Rotate)
        m_DragAxisDir = m_ToCamera;
    m_DragStartAxisParam = 0.0f;
    m_DragStartScale = Vec3(1, 1, 1);
    m_RotationStartValid = false;
    m_RotationAngle = 0.0f;
    m_UniformDragStartDistance = 1.0f;

    float axisParam = 0.0f;
    float distance = 0.0f;
    if (m_Mode != Mode::Rotate && axis < 3) {
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

        if (axis == 3) {
            // Uniform scale: compute initial distance on camera-facing plane
            Vec3 hitPoint;
            if (IntersectPlane(ray, m_GizmoOrigin, m_ToCamera, hitPoint)) {
                m_UniformDragStartDistance = std::max(vlen(vsub(hitPoint, m_GizmoOrigin)), 0.01f);
            }
        } else {
            if (std::abs(m_DragStartAxisParam) < 0.05f * m_CurrentScale) {
                m_DragStartAxisParam = (m_DragStartAxisParam >= 0.0f ? 1.0f : -1.0f) * 0.05f * m_CurrentScale;
            }
        }
    }

    if (m_Mode == Mode::Translate && axis == 3) {
        IntersectPlane(ray, m_GizmoOrigin, m_ToCamera, m_FreeMoveStartPoint);
    } else if (m_Mode == Mode::Translate && axis >= 4) {
        const int normalAxis = axis == 4 ? 2 : (axis == 5 ? 1 : 0);
        m_DragPlaneNormal = m_AxisDirections[normalAxis];
        IntersectPlane(ray, m_GizmoOrigin, m_DragPlaneNormal,
                       m_FreeMoveStartPoint);
    }

    if (m_Mode == Mode::Rotate && axis <= 3) {
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

Mat4 VulkanGizmo::HandleDrag(const Ray& ray, bool bypassSnap) {
    if (!m_IsDragging) {
        return m_ObjectTransform;
    }

    Mat4 result = m_ObjectTransform;

    if (m_ActiveAxis >= 0 && m_ActiveAxis < 3 && m_Mode == Mode::Translate) {
        float axisParam = 0.0f;
        float distance = 0.0f;
        if (ClosestPointOnAxis(ray, m_DragStartPosition, m_DragAxisDir, axisParam, distance, false)) {
            float delta = axisParam - m_DragStartAxisParam;
            if (m_SnapEnabled && !bypassSnap && m_SnapIncrement > 0.0f) {
                delta = std::round(delta / m_SnapIncrement) * m_SnapIncrement;
            }
            Vec3 newPosition = vadd(m_DragStartPosition, vscale(m_DragAxisDir, delta));

            result = m_DragStartTransform;
            result.m[12] = newPosition.x;
            result.m[13] = newPosition.y;
            result.m[14] = newPosition.z;
        }
    } else if (m_ActiveAxis == 3 && m_Mode == Mode::Translate) {
        Vec3 point;
        if (IntersectPlane(ray, m_GizmoOrigin, m_ToCamera, point)) {
            Vec3 newPosition = vadd(m_DragStartPosition,
                                    vsub(point, m_FreeMoveStartPoint));
            if (m_SnapEnabled && !bypassSnap && m_SnapIncrement > 0.0f) {
                newPosition.x = std::round(newPosition.x / m_SnapIncrement) * m_SnapIncrement;
                newPosition.y = std::round(newPosition.y / m_SnapIncrement) * m_SnapIncrement;
                newPosition.z = std::round(newPosition.z / m_SnapIncrement) * m_SnapIncrement;
            }
            result = m_DragStartTransform;
            result.m[12] = newPosition.x;
            result.m[13] = newPosition.y;
            result.m[14] = newPosition.z;
        }
    } else if (m_ActiveAxis >= 4 && m_ActiveAxis <= 6 &&
               m_Mode == Mode::Translate) {
        Vec3 point;
        if (IntersectPlane(ray, m_GizmoOrigin, m_DragPlaneNormal, point)) {
            Vec3 delta = vsub(point, m_FreeMoveStartPoint);
            if (m_SnapEnabled && !bypassSnap && m_SnapIncrement > 0.0f) {
                const int firstAxis = m_ActiveAxis == 6 ? 1 : 0;
                const int secondAxis = m_ActiveAxis == 4 ? 1 : 2;
                float first = vdot(delta, m_AxisDirections[firstAxis]);
                float second = vdot(delta, m_AxisDirections[secondAxis]);
                first = std::round(first / m_SnapIncrement) * m_SnapIncrement;
                second = std::round(second / m_SnapIncrement) * m_SnapIncrement;
                delta = vadd(vscale(m_AxisDirections[firstAxis], first),
                             vscale(m_AxisDirections[secondAxis], second));
            }
            const Vec3 newPosition = vadd(m_DragStartPosition, delta);
            result = m_DragStartTransform;
            result.m[12] = newPosition.x;
            result.m[13] = newPosition.y;
            result.m[14] = newPosition.z;
        }
    } else if (m_ActiveAxis >= 0 && m_ActiveAxis <= 3 && m_Mode == Mode::Scale) {
        Vec3 translation(m_DragStartTransform.m[12], m_DragStartTransform.m[13], m_DragStartTransform.m[14]);
        Vec3 basisX(m_DragStartTransform.m[0], m_DragStartTransform.m[1], m_DragStartTransform.m[2]);
        Vec3 basisY(m_DragStartTransform.m[4], m_DragStartTransform.m[5], m_DragStartTransform.m[6]);
        Vec3 basisZ(m_DragStartTransform.m[8], m_DragStartTransform.m[9], m_DragStartTransform.m[10]);

        float scaleX = m_DragStartScale.x;
        float scaleY = m_DragStartScale.y;
        float scaleZ = m_DragStartScale.z;

        if (m_ActiveAxis == 3) {
            // Uniform scale: use camera-facing plane distance ratio
            Vec3 hitPoint;
            if (IntersectPlane(ray, m_GizmoOrigin, m_ToCamera, hitPoint)) {
                float currentDist = vlen(vsub(hitPoint, m_GizmoOrigin));
                float ratio = currentDist / m_UniformDragStartDistance;
                if (!std::isfinite(ratio)) ratio = 1.0f;
                ratio = std::max(ratio, 0.01f);
                scaleX = std::max(0.01f, m_DragStartScale.x * ratio);
                scaleY = std::max(0.01f, m_DragStartScale.y * ratio);
                scaleZ = std::max(0.01f, m_DragStartScale.z * ratio);
                if (m_SnapEnabled && !bypassSnap && m_SnapIncrement > 0.0f) {
                    scaleX = std::max(0.01f, std::round(scaleX / m_SnapIncrement) * m_SnapIncrement);
                    scaleY = std::max(0.01f, std::round(scaleY / m_SnapIncrement) * m_SnapIncrement);
                    scaleZ = std::max(0.01f, std::round(scaleZ / m_SnapIncrement) * m_SnapIncrement);
                }
            }
        } else {
            // Per-axis scale
            float axisParam = 0.0f;
            float distance = 0.0f;
            if (ClosestPointOnAxis(ray, m_GizmoOrigin, m_DragAxisDir, axisParam, distance, false) && std::abs(m_DragStartAxisParam) > 1e-4f) {
                float ratio = axisParam / m_DragStartAxisParam;
                if (!std::isfinite(ratio)) ratio = 1.0f;
                ratio = std::max(ratio, 0.01f);
                switch (m_ActiveAxis) {
                    case 0: scaleX = std::max(0.01f, m_DragStartScale.x * ratio); break;
                    case 1: scaleY = std::max(0.01f, m_DragStartScale.y * ratio); break;
                    case 2: scaleZ = std::max(0.01f, m_DragStartScale.z * ratio); break;
                }
                if (m_SnapEnabled && !bypassSnap && m_SnapIncrement > 0.0f) {
                    switch (m_ActiveAxis) {
                        case 0: scaleX = std::max(0.01f, std::round(scaleX / m_SnapIncrement) * m_SnapIncrement); break;
                        case 1: scaleY = std::max(0.01f, std::round(scaleY / m_SnapIncrement) * m_SnapIncrement); break;
                        case 2: scaleZ = std::max(0.01f, std::round(scaleZ / m_SnapIncrement) * m_SnapIncrement); break;
                    }
                }
            }
        }

        Vec3 normX = vnorm(basisX);
        Vec3 normY = vnorm(basisY);
        Vec3 normZ = vnorm(basisZ);

        Mat4 rotation = GizmoBasis(normX, normY, normZ);
        Mat4 translate = Mat4::Translation(translation);
        Mat4 scale = Mat4::Scale(Vec3(scaleX, scaleY, scaleZ));

        result = Mat4::Multiply(translate, Mat4::Multiply(rotation, scale));
    } else if (m_ActiveAxis >= 0 && m_ActiveAxis <= 3 && m_Mode == Mode::Rotate) {
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
                    if (m_SnapEnabled && !bypassSnap && m_SnapIncrement > 0.0f) {
                        const float incrementRad = m_SnapIncrement * 3.1415926535f / 180.0f;
                        angle = std::round(angle / incrementRad) * incrementRad;
                    }
                    m_RotationAngle = angle;

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
    m_RotationAngle = 0.0f;
}

Mat4 VulkanGizmo::CancelDrag() {
    const Mat4 start = m_DragStartTransform;
    EndDrag();
    return start;
}

float VulkanGizmo::CalculateGizmoScale(const Vec3& gizmoPos, const Vec3& cameraPos,
                                      const Mat4& projMatrix) const {
    return GizmoScreenScale(gizmoPos, cameraPos, projMatrix,
                            kScreenScaleFactor * m_SizeMultiplier);
}
