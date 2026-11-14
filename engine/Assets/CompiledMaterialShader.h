#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus {

/// Bumped whenever the surface contract a compiled graph is built against changes (shaders/triangle.frag,
/// material_common.glsl, MaterialUBO); a file with another version is ignored and the material draws with the
/// standard surface until the editor compiles it again.
inline constexpr std::uint32_t kMaterialShaderInterfaceVersion = 1;

/// A material graph compiled into its own copy of the surface fragment shader, saved beside the material as
/// `<name>.hemshader` so packaged games need no shader compiler.
struct CompiledMaterialShader {
    std::uint32_t interfaceVersion = kMaterialShaderInterfaceVersion;
    std::uint64_t sourceHash = 0;           ///< hash of the generated graph source, to tell when it is out of date
    struct Texture {
        std::string path;    ///< portable path
        bool linear = false; ///< data such as normals or masks, sampled without sRGB decoding
        bool operator==(const Texture&) const = default;
    };
    std::vector<Texture> textures;          ///< in MaterialUBO::graphTextureSlots order
    std::vector<std::uint32_t> spirv;       ///< triangle.frag with the graph
    std::vector<std::uint32_t> spirvRayTraced; ///< the RAY_TRACED_SHADOWS build, for GPUs with ray queries
};

/// `Foo.hematerial` -> `Foo.hemshader`.
std::filesystem::path CompiledMaterialShaderPath(const std::filesystem::path& materialPath);

bool WriteCompiledMaterialShader(const std::filesystem::path& path, const CompiledMaterialShader& shader,
                                 std::string& outError);

/// False when the file is missing, malformed or built for another interface version.
bool ReadCompiledMaterialShader(const std::filesystem::path& path, CompiledMaterialShader& outShader,
                                std::string& outError);

/// FNV-1a, stable across runs and platforms.
std::uint64_t HashMaterialShaderSource(const std::string& source);

} // namespace Hephaestus
