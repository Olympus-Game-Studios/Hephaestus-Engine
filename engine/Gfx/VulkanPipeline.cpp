#include "VulkanPipeline.h"
#include "MaterialTextureSlots.h"
#include "VulkanSwapchain.h"
#include <fstream>
#include <stdexcept>
#include <iostream>

std::vector<char> VulkanPipeline::readFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        // Try to load from project root instead of working directory
        std::string alt = "../" + filename;
        file.open(alt, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            std::cerr << "Tried both: " << filename << " and " << alt << std::endl;
            throw std::runtime_error("Failed to open file: " + filename);
        }
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}



VkShaderModule VulkanPipeline::createShaderModule(const std::vector<char>& code)
{
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shader module");
    return module;
}

void VulkanPipeline::Init(VkDevice dev, VkRenderPass rp, VkExtent2D ext, bool enableWireframe,
                          const std::vector<VkDescriptorSetLayout>& extraSetLayouts, const std::string& fragmentShader)
{
    device = dev;
    renderPass = rp;
    extent = ext;
    ownsDescriptorSetLayout = true;

    constexpr uint32_t kTextureArraySize = kMaxMaterialTextureSlots;

    // Create descriptor set layout with 6 bindings:
    // - binding 0: UBO (view/proj matrices + camera position)
    // - binding 1: Material UBO
    // - binding 2: Lighting UBO
    // - binding 3: Combined image sampler array for material textures
    // - binding 4: Directional shadow map sampler
    // - binding 5: Environment reflection texture
    VkDescriptorSetLayoutBinding bindings[7] = {};
    
    // Binding 0: Camera/View UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 1: Material UBO (dynamic)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 2: Lighting UBO
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 3: Material texture array
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = kTextureArraySize;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 4: Shadow map sampler
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 5: Environment reflection map
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 6: Point/spot light shadow atlas
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = 7;
    setInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor set layout");

    // Try a few likely shader locations
    std::vector<std::string> candidates = {
        "shaders/triangle.vert.spv",
        "../shaders/triangle.vert.spv",
        "build/Release/shaders/triangle.vert.spv",
        "../build/Release/shaders/triangle.vert.spv"
    };
    std::vector<std::string> candidatesFrag = {
        "shaders/" + fragmentShader,
        "../shaders/" + fragmentShader,
        "build/Release/shaders/" + fragmentShader,
        "../build/Release/shaders/" + fragmentShader
    };
    std::vector<char> vertCode;
    std::vector<char> fragCode;
    bool loaded = false;
    for (size_t i = 0; i < candidates.size() && i < candidatesFrag.size(); ++i) {
        try {
            vertCode = readFile(candidates[i]);
            fragCode = readFile(candidatesFrag[i]);
            loaded = true;
            break;
        } catch (...) {
            // try next
        }
    }
    if (!loaded) {
        // Final attempt, throw a more descriptive error
        vertCode = readFile("shaders/triangle.vert.spv");
        fragCode = readFile("shaders/" + fragmentShader);
    }

    m_vertexCode = vertCode;

    // Push constant for model matrix (64 bytes = 4x4 float matrix)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 64; // sizeof(Mat4) = 16 floats * 4 bytes

    std::vector<VkDescriptorSetLayout> setLayouts = {descriptorSetLayout};
    setLayouts.insert(setLayouts.end(), extraSetLayouts.begin(), extraSetLayouts.end());
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pipeline layout");

    VkShaderModule fragShader = createShaderModule(fragCode);
    SurfacePipelines surface;
    const bool built = buildSurfacePipelines(fragShader, enableWireframe, surface);
    vkDestroyShaderModule(device, fragShader, nullptr);
    if (!built) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }
    pipeline = surface.opaque;
    pipelineBackface = surface.opaqueBackface;
    pipelineWireframe = surface.wireframe;
    pipelineWireframeBackface = surface.wireframeBackface;
    pipelineAlphaBlend = surface.alphaBlend;
    pipelineAdditive = surface.additive;
    pipelineMultiply = surface.multiply;
    pipelineMultiply2x = surface.multiply2x;
}

bool VulkanPipeline::buildSurfacePipelines(VkShaderModule fragShader, bool wireframe, SurfacePipelines& out)
{
    out = {};
    VkShaderModule vertShader = createShaderModule(m_vertexCode);

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

    auto bindingDesc = Vertex::getBindingDescription();
    auto attribs = Vertex::getAttributeDescriptions();

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

    VkPipelineRasterizationStateCreateInfo rasterBase{};
    rasterBase.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterBase.polygonMode = VK_POLYGON_MODE_FILL;
    rasterBase.cullMode = VK_CULL_MODE_NONE;
    // In Vulkan, the Y-axis of the clip space is inverted compared to framebuffer space
    // when using a standard viewport (y=0, positive height). This flips the perceived
    // winding of triangles. Use CLOCKWISE here to keep front faces visible with our
    // vertex order.
    rasterBase.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterBase.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    depth.depthBoundsTestEnable = VK_FALSE;
    depth.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorAttach{};
    colorAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorAttach.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &colorAttach;

    VkGraphicsPipelineCreateInfo basePci{};
    basePci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    basePci.stageCount = 2;
    basePci.pStages = stages;
    basePci.pVertexInputState = &vertexInput;
    basePci.pInputAssemblyState = &inputAsm;
    basePci.pViewportState = &viewportState;
    basePci.pMultisampleState = &ms;
    basePci.pDepthStencilState = &depth;
    basePci.pColorBlendState = &blend;
    basePci.layout = layout;
    basePci.renderPass = renderPass;
    basePci.subpass = 0;

    // Dynamic states: viewport & scissor
    VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamics;
    basePci.pDynamicState = &dyn;

    bool ok = true;
    auto buildPipeline = [&](VkCullModeFlags cullMode, VkPolygonMode polygonMode) -> VkPipeline {
        VkPipelineRasterizationStateCreateInfo raster = rasterBase;
        raster.cullMode = cullMode;
        raster.polygonMode = polygonMode;

        VkGraphicsPipelineCreateInfo pipelineCI = basePci;
        pipelineCI.pRasterizationState = &raster;

        VkPipeline handle = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &handle) != VK_SUCCESS) {
            ok = false;
        }
        return handle;
    };

    out.opaque = buildPipeline(VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL);
    out.opaqueBackface = buildPipeline(VK_CULL_MODE_FRONT_BIT, VK_POLYGON_MODE_FILL);

    if (wireframe) {
        out.wireframe = buildPipeline(VK_CULL_MODE_NONE, VK_POLYGON_MODE_LINE);
        out.wireframeBackface = buildPipeline(VK_CULL_MODE_FRONT_BIT, VK_POLYGON_MODE_LINE);
    }

    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    colorAttach.blendEnable = VK_TRUE;
    colorAttach.colorBlendOp = VK_BLEND_OP_ADD;
    colorAttach.alphaBlendOp = VK_BLEND_OP_ADD;
    colorAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    out.alphaBlend = buildPipeline(VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL);
    colorAttach.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    out.additive = buildPipeline(VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL);
    // Multiply: destination times source. Multiply 2x: plus source times destination again.
    colorAttach.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
    colorAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    out.multiply = buildPipeline(VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL);
    colorAttach.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
    out.multiply2x = buildPipeline(VK_CULL_MODE_NONE, VK_POLYGON_MODE_FILL);

    vkDestroyShaderModule(device, vertShader, nullptr);
    if (!ok) {
        DestroySurfacePipelines(out);
    }
    return ok;
}

bool VulkanPipeline::CreateSurfacePipelines(const std::vector<std::uint32_t>& fragmentSpirv, SurfacePipelines& out)
{
    if (device == VK_NULL_HANDLE || layout == VK_NULL_HANDLE || fragmentSpirv.empty()) {
        return false;
    }
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = fragmentSpirv.size() * sizeof(std::uint32_t);
    ci.pCode = fragmentSpirv.data();
    VkShaderModule fragShader = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &fragShader) != VK_SUCCESS) {
        return false;
    }
    const bool built = buildSurfacePipelines(fragShader, false, out);
    vkDestroyShaderModule(device, fragShader, nullptr);
    return built;
}

void VulkanPipeline::DestroySurfacePipelines(SurfacePipelines& pipelines) const
{
    for (VkPipeline* handle : {&pipelines.opaque, &pipelines.opaqueBackface, &pipelines.wireframe,
                               &pipelines.wireframeBackface, &pipelines.alphaBlend, &pipelines.additive,
                               &pipelines.multiply, &pipelines.multiply2x}) {
        if (*handle != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, *handle, nullptr);
            *handle = VK_NULL_HANDLE;
        }
    }
}

void VulkanPipeline::CreateOutlinePipeline()
{
    if (device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE || descriptorSetLayout == VK_NULL_HANDLE) {
        throw std::runtime_error("VulkanPipeline must be initialized before creating outline pipeline");
    }

    // Load outline shaders
    std::vector<char> vertCode = readFile("shaders/outline.vert.spv");
    std::vector<char> fragCode = readFile("shaders/outline.frag.spv");

    VkShaderModule vertShader = createShaderModule(vertCode);
    VkShaderModule fragShader = createShaderModule(fragCode);

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

    auto bindingDesc = Vertex::getBindingDescription();
    auto attribs = Vertex::getAttributeDescriptions();

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
    raster.cullMode = VK_CULL_MODE_FRONT_BIT; // Cull front faces so only silhouette (back faces) remains
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    raster.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_FALSE;  // Disable depth test so outline renders on top of all geometry
    depth.depthWriteEnable = VK_FALSE; // Don't write to depth buffer
    depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    depth.depthBoundsTestEnable = VK_FALSE;
    depth.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorAttach{};
    colorAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorAttach.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &colorAttach;

    // Push constant for model matrix (same as main pipeline)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 64; // sizeof(Mat4)

    // Dynamic states: viewport & scissor
    VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamics;

    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = stages;
    pipelineCI.pVertexInputState = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAsm;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pRasterizationState = &raster;
    pipelineCI.pMultisampleState = &ms;
    pipelineCI.pDepthStencilState = &depth;
    pipelineCI.pColorBlendState = &blend;
    pipelineCI.pDynamicState = &dyn;
    pipelineCI.layout = layout; // Use the same layout as the main pipeline
    pipelineCI.renderPass = renderPass;
    pipelineCI.subpass = VulkanSwapchain::kOverlaySubpass;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &outlinePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create outline graphics pipeline");
    }

    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
}

void VulkanPipeline::Cleanup()
{
    if (outlinePipeline) {
        vkDestroyPipeline(device, outlinePipeline, nullptr);
        outlinePipeline = VK_NULL_HANDLE;
    }
    if (pipelineWireframeBackface) {
        vkDestroyPipeline(device, pipelineWireframeBackface, nullptr);
        pipelineWireframeBackface = VK_NULL_HANDLE;
    }
    if (pipelineAlphaBlend) {
        vkDestroyPipeline(device, pipelineAlphaBlend, nullptr);
        pipelineAlphaBlend = VK_NULL_HANDLE;
    }
    if (pipelineAdditive) {
        vkDestroyPipeline(device, pipelineAdditive, nullptr);
        pipelineAdditive = VK_NULL_HANDLE;
    }
    for (VkPipeline* multiply : {&pipelineMultiply, &pipelineMultiply2x}) {
        if (*multiply) {
            vkDestroyPipeline(device, *multiply, nullptr);
            *multiply = VK_NULL_HANDLE;
        }
    }
    if (pipelineWireframe) {
        vkDestroyPipeline(device, pipelineWireframe, nullptr);
        pipelineWireframe = VK_NULL_HANDLE;
    }
    if (pipelineBackface) {
        vkDestroyPipeline(device, pipelineBackface, nullptr);
        pipelineBackface = VK_NULL_HANDLE;
    }
    if (pipeline) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (layout) {
        vkDestroyPipelineLayout(device, layout, nullptr);
        layout = VK_NULL_HANDLE;
    }
    if (ownsDescriptorSetLayout && descriptorSetLayout) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    }
    descriptorSetLayout = VK_NULL_HANDLE;
    ownsDescriptorSetLayout = true;
}

VkPipeline VulkanPipeline::GetTranslucentPipeline(uint32_t blendMode) const
{
    switch (blendMode) {
    case 2: return pipelineAdditive;
    case 4: return pipelineMultiply;
    case 5: return pipelineMultiply2x;
    default: return pipelineAlphaBlend;
    }
}

VkPipeline VulkanPipeline::GetPipeline(bool backfaceCullingEnabled, bool wireframe) const
{
    if (wireframe) {
        if (backfaceCullingEnabled && pipelineWireframeBackface != VK_NULL_HANDLE) {
            return pipelineWireframeBackface;
        }
        if (pipelineWireframe != VK_NULL_HANDLE) {
            return pipelineWireframe;
        }
        // Fall back to filled pipeline if wireframe was not created
    }
    if (backfaceCullingEnabled && pipelineBackface != VK_NULL_HANDLE) {
        return pipelineBackface;
    }
    return pipeline;
}

void VulkanPipeline::CreateBillboardPipeline(VkDevice dev, VkRenderPass rp, VkExtent2D ext, VkDescriptorSetLayout descLayout)
{
    device = dev;
    renderPass = rp;
    extent = ext;
    descriptorSetLayout = descLayout;
    ownsDescriptorSetLayout = false;

    // Load billboard shaders
    auto vertCode = readFile("shaders/billboard.vert.spv");
    auto fragCode = readFile("shaders/billboard.frag.spv");

    VkShaderModule vertShader = createShaderModule(vertCode);
    VkShaderModule fragShader = createShaderModule(fragCode);

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

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    // No vertex input - billboards are generated in vertex shader from gl_VertexIndex
    VkPipelineVertexInputStateCreateInfo vertInput{};
    vertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertInput.vertexBindingDescriptionCount = 0;
    vertInput.vertexAttributeDescriptionCount = 0;

    // Input assembly - draw triangle strips for quads
    VkPipelineInputAssemblyStateCreateInfo inputAsm{};
    inputAsm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAsm.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)extent.width;
    viewport.height = (float)extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_NONE; // No culling for billboards
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil - test but don't write
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE; // Don't write to depth buffer
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending - ENABLE ALPHA BLENDING
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state for viewport and scissor
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constants for billboard position, size and glyph tint
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(float) * 8; // vec3 position + float size + vec4 tint

    // Pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create billboard pipeline layout");

    // Create pipeline
    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vertInput;
    pci.pInputAssemblyState = &inputAsm;
    pci.pViewportState = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &multisampling;
    pci.pDepthStencilState = &depthStencil;
    pci.pColorBlendState = &colorBlending;
    pci.pDynamicState = &dynamicState;
    pci.layout = layout;
    pci.renderPass = renderPass;
    pci.subpass = VulkanSwapchain::kOverlaySubpass;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create billboard graphics pipeline");

    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
}
