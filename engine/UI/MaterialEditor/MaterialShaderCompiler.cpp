#include "MaterialShaderCompiler.h"

#include "MaterialShaderGenerator.h"
#include "../../Core/AppPaths.h"

#include <fstream>
#include <iterator>

#if HEPHAESTUS_MATERIAL_SHADER_COMPILER
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#endif

namespace MaterialEditor {
namespace {

constexpr const char* kSurfaceShader = "triangle.frag";
constexpr const char* kGraphInclude = "material_graph.glsl";

bool ReadText(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

#if HEPHAESTUS_MATERIAL_SHADER_COMPILER

class Process {
public:
    Process() { glslang::InitializeProcess(); }
    ~Process() { glslang::FinalizeProcess(); }
};

class Includer final : public glslang::TShader::Includer {
public:
    Includer(std::filesystem::path directory, std::string graphSource)
        : m_directory(std::move(directory)), m_graphSource(std::move(graphSource)) {}

    IncludeResult* includeLocal(const char* headerName, const char*, size_t) override {
        auto* text = new std::string();
        const std::string name = headerName ? headerName : "";
        if (name == kGraphInclude) {
            *text = m_graphSource;
        } else if (name.find("..") != std::string::npos || !ReadText(m_directory / name, *text)) {
            delete text;
            return nullptr;
        }
        return new IncludeResult(name, text->data(), text->size(), text);
    }

    IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t depth) override {
        return includeLocal(headerName, includerName, depth);
    }

    void releaseInclude(IncludeResult* result) override {
        if (result) {
            delete static_cast<std::string*>(result->userData);
            delete result;
        }
    }

private:
    std::filesystem::path m_directory;
    std::string m_graphSource;
};

bool CompileVariant(const std::string& surfaceSource, const std::filesystem::path& directory,
                    const std::string& graphSource, bool rayTraced, std::vector<std::uint32_t>& outSpirv,
                    std::vector<std::string>& diagnostics) {
    static Process process;
    glslang::TShader shader(EShLangFragment);
    const char* text = surfaceSource.c_str();
    const char* name = kSurfaceShader;
    shader.setStringsWithLengthsAndNames(&text, nullptr, &name, 1);
    shader.setPreamble(rayTraced ? "#define MATERIAL_GRAPH\n#define RAY_TRACED_SHADOWS\n" : "#define MATERIAL_GRAPH\n");
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangFragment, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, rayTraced ? glslang::EShTargetVulkan_1_2 : glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, rayTraced ? glslang::EShTargetSpv_1_4 : glslang::EShTargetSpv_1_0);
    Includer includer(directory, graphSource);
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 460, false, messages, includer)) {
        diagnostics.push_back(shader.getInfoLog());
        return false;
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        diagnostics.push_back(program.getInfoLog());
        return false;
    }
    outSpirv.clear();
    glslang::GlslangToSpv(*program.getIntermediate(EShLangFragment), outSpirv);
    return !outSpirv.empty();
}

#endif

} // namespace

bool MaterialShaderCompilerAvailable() {
#if HEPHAESTUS_MATERIAL_SHADER_COMPILER
    return true;
#else
    return false;
#endif
}

std::filesystem::path FindMaterialShaderSourceDirectory() {
    const std::filesystem::path executable = Hephaestus::ExecutableDirectory();
    std::vector<std::filesystem::path> candidates;
    if (!executable.empty()) {
        candidates.push_back(executable / "shaders/source");
        candidates.push_back(executable.parent_path() / "shaders/source");
    }
    for (const char* relative : {"shaders/source", "build/Release/shaders/source"}) {
        candidates.emplace_back(relative);
    }
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate / kSurfaceShader, ec)) {
            return std::filesystem::absolute(candidate, ec);
        }
    }
    return {};
}

MaterialShaderCompileResult CompileMaterialGraph(const Graph& graph, const std::filesystem::path& shaderSourceDirectory) {
    MaterialShaderCompileResult result;
    const MaterialShaderGenerationResult generated = GenerateMaterialShader(graph);
    if (!generated.ok) {
        result.diagnostics = generated.diagnostics;
        return result;
    }
    const std::string graphSource = BuildMaterialGraphSource(generated);
    result.shader.sourceHash = Hephaestus::HashMaterialShaderSource(graphSource);
    for (const GeneratedSamplerBinding& sampler : generated.samplers) {
        result.shader.textures.push_back({sampler.resourcePath, sampler.linear});
    }

#if HEPHAESTUS_MATERIAL_SHADER_COMPILER
    std::string surfaceSource;
    if (shaderSourceDirectory.empty() || !ReadText(shaderSourceDirectory / kSurfaceShader, surfaceSource)) {
        result.diagnostics.push_back("The engine's surface shader source (shaders/source/triangle.frag) is missing, "
                                     "so the material cannot be compiled.");
        return result;
    }
    result.ok = CompileVariant(surfaceSource, shaderSourceDirectory, graphSource, false, result.shader.spirv,
                               result.diagnostics) &&
                CompileVariant(surfaceSource, shaderSourceDirectory, graphSource, true, result.shader.spirvRayTraced,
                               result.diagnostics);
#else
    (void)shaderSourceDirectory;
    result.diagnostics.push_back("This editor was built without a shader compiler, so per-pixel material graphs "
                                 "cannot be compiled.");
#endif
    return result;
}

} // namespace MaterialEditor
