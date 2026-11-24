#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <string>

#include "Vertex.h"

/// The surface pipelines for one fragment shader: the engine's own, or a compiled material graph's.
struct SurfacePipelines {
    VkPipeline opaque = VK_NULL_HANDLE;
    VkPipeline opaqueBackface = VK_NULL_HANDLE;
    VkPipeline wireframe = VK_NULL_HANDLE;
    VkPipeline wireframeBackface = VK_NULL_HANDLE;
    VkPipeline alphaBlend = VK_NULL_HANDLE;
    VkPipeline additive = VK_NULL_HANDLE;
    VkPipeline multiply = VK_NULL_HANDLE;
    VkPipeline multiply2x = VK_NULL_HANDLE;

    /// The pipeline for a translucent MaterialBlendMode (alpha blend for any other).
    VkPipeline Translucent(uint32_t blendMode) const {
        switch (blendMode) {
        case 2: return additive;
        case 4: return multiply;
        case 5: return multiply2x;
        default: return alphaBlend;
        }
    }
};

struct VulkanPipeline {
    VkDevice device{};
    VkRenderPass renderPass{};
    VkExtent2D extent{};
    VkDescriptorSetLayout descriptorSetLayout{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    VkPipeline pipelineBackface{};
    VkPipeline pipelineWireframe{};
    VkPipeline pipelineWireframeBackface{};
    VkPipeline pipelineAlphaBlend{};
    VkPipeline pipelineAdditive{};
    VkPipeline pipelineMultiply{};
    VkPipeline pipelineMultiply2x{};
    VkPipeline outlinePipeline{};
    bool ownsDescriptorSetLayout = true;

    // extraSetLayouts follow the pipeline's own set 0 layout.
    void Init(VkDevice dev, VkRenderPass rp, VkExtent2D ext, bool enableWireframe = false,
              const std::vector<VkDescriptorSetLayout>& extraSetLayouts = {},
              const std::string& fragmentShader = "triangle.frag.spv");
    void CreateBillboardPipeline(VkDevice dev, VkRenderPass rp, VkExtent2D ext, VkDescriptorSetLayout descLayout);
    void CreateOutlinePipeline();
    void Cleanup();

    /// Pipelines sharing this layout and vertex shader, with a material's own fragment shader (no wireframe).
    bool CreateSurfacePipelines(const std::vector<std::uint32_t>& fragmentSpirv, SurfacePipelines& out);
    void DestroySurfacePipelines(SurfacePipelines& pipelines) const;

    VkPipeline GetPipeline(bool backfaceCullingEnabled, bool wireframe = false) const;
    /// Depth-tested, non-depth-writing pipeline for a translucent blend mode (1 alpha, 2 additive, 4 multiply, 5 multiply2x).
    VkPipeline GetTranslucentPipeline(uint32_t blendMode) const;
    bool HasWireframePipeline() const {
        return pipelineWireframe != VK_NULL_HANDLE;
    }

private:
    bool buildSurfacePipelines(VkShaderModule fragShader, bool wireframe, SurfacePipelines& out);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> m_vertexCode;
    static std::vector<char> readFile(const std::string& filename);
};
