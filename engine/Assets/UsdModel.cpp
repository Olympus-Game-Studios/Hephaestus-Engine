/**
 * @file UsdModel.cpp
 * @brief USD scenes read through tinyusdz (lightusd) and its Tydra render-scene converter.
 */

#include "UsdModel.h"

#include "asset-resolution.hh"
#include "composition.hh"
#include "lightusd.hh"
#include "tydra/render-data.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <fstream>
#include <map>

namespace fs = std::filesystem;
namespace tydra = lightusd::tydra;

namespace Hephaestus::Usd {
namespace {

constexpr int kMaxCompositionPasses = 32;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

/// The value of a float attribute (Vec2/Vec3) for one face vertex, whatever its interpolation.
bool ReadAttribute(const tydra::VertexAttribute& attribute, size_t point, size_t faceVertex, size_t components,
                   float* out) {
    if (attribute.empty() || attribute.element_size() != 1 ||
        attribute.stride_bytes() < components * sizeof(float)) {
        return false;
    }
    size_t element = 0;
    switch (attribute.variability) {
    case tydra::VertexVariability::Constant: element = 0; break;
    case tydra::VertexVariability::FaceVarying: element = faceVertex; break;
    case tydra::VertexVariability::Indexed:
        if (faceVertex >= attribute.indices.size()) {
            return false;
        }
        element = attribute.indices[faceVertex];
        break;
    default: element = point; break;
    }
    if (element >= attribute.vertex_count()) {
        return false;
    }
    std::memcpy(out, attribute.get_data().data() + element * attribute.stride_bytes(), components * sizeof(float));
    return true;
}

int ChannelOf(tydra::UVTexture::Channel channel) {
    switch (channel) {
    case tydra::UVTexture::Channel::R: return 0;
    case tydra::UVTexture::Channel::G: return 1;
    case tydra::UVTexture::Channel::B: return 2;
    case tydra::UVTexture::Channel::A: return 3;
    default: return -1;
    }
}

class MaterialReader {
public:
    MaterialReader(const tydra::RenderScene& scene, fs::path baseDir) : m_scene(scene), m_baseDir(std::move(baseDir)) {}

    SourceTexture Texture(int textureId) const {
        SourceTexture out;
        if (textureId < 0 || static_cast<size_t>(textureId) >= m_scene.textures.size()) {
            return out;
        }
        const tydra::UVTexture& texture = m_scene.textures[static_cast<size_t>(textureId)];
        if (texture.texture_image_id < 0 || static_cast<size_t>(texture.texture_image_id) >= m_scene.images.size()) {
            return out;
        }
        const std::string& asset = m_scene.images[static_cast<size_t>(texture.texture_image_id)].asset_identifier;
        fs::path file = fs::path(std::u8string(asset.begin(), asset.end()));
        if (file.is_relative()) {
            file = m_baseDir / file;
        }
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) {
            return out;
        }
        out.file = file.lexically_normal();
        out.channel = ChannelOf(texture.connectedOutputChannel);
        return out;
    }

    SourceMaterial Read(const tydra::RenderMaterial& in) const {
        SourceMaterial out;
        out.name = in.name;
        auto color = [&](const tydra::ShaderParam<tydra::vec3>& param, Vec3& value, SourceTexture* texture) {
            value = Vec3(param.value[0], param.value[1], param.value[2]);
            if (texture && param.is_texture()) {
                *texture = Texture(param.texture_id);
                if (texture->Exists()) {
                    value = Vec3(1.0f, 1.0f, 1.0f);
                }
            }
        };
        auto scalar = [&](const tydra::ShaderParam<float>& param, float& value, SourceTexture* texture) {
            value = param.value;
            if (texture && param.is_texture()) {
                *texture = Texture(param.texture_id);
                if (texture->Exists()) {
                    value = 1.0f;
                }
            }
        };
        auto normal = [&](const tydra::ShaderParam<tydra::vec3>& param) {
            if (param.is_texture()) {
                out.normalTexture = Texture(param.texture_id);
                out.normalTexture.channel = -1;
            }
        };
        if (in.surfaceShader.has_value()) {
            const tydra::PreviewSurfaceShader& s = in.surfaceShader.value();
            color(s.diffuseColor, out.baseColor, &out.baseColorTexture);
            scalar(s.roughness, out.roughness, &out.roughnessTexture);
            scalar(s.metallic, out.metallic, &out.metallicTexture);
            float ao = 1.0f;
            scalar(s.occlusion, ao, &out.aoTexture);
            scalar(s.opacity, out.opacity, nullptr);
            color(s.emissiveColor, out.emissive, nullptr);
            normal(s.normal);
        } else if (in.openPBRShader.has_value()) {
            const tydra::OpenPBRSurfaceShader& s = in.openPBRShader.value();
            color(s.base_color, out.baseColor, &out.baseColorTexture);
            scalar(s.specular_roughness, out.roughness, &out.roughnessTexture);
            scalar(s.base_metalness, out.metallic, &out.metallicTexture);
            scalar(s.opacity, out.opacity, nullptr);
            normal(s.normal);
            if (!s.emission_luminance.is_texture() && s.emission_luminance.value > 0.0f) {
                color(s.emission_color, out.emissive, nullptr);
            }
        }
        out.blend = in.materialTag == tydra::MaterialTag::Translucent ? SourceBlend::Blend : SourceBlend::Opaque;
        return out;
    }

private:
    const tydra::RenderScene& m_scene;
    fs::path m_baseDir;
};

fs::path Utf8Path(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::string Utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

/// Folders of the layers read so far, newest last: the root file's folder, then every layer resolved from it.
struct LayerFolders {
    std::vector<fs::path> folders;
};

// tinyusdz's io::FindFile refuses "../" paths and composition can pass a sibling's folder; see docs/code-notes.
int ResolveFile(const char* name, const std::vector<std::string>& searchPaths, std::string* resolved, std::string*,
                void* userdata) {
    auto* layers = static_cast<LayerFolders*>(userdata);
    const fs::path asset = Utf8Path(name ? name : "");
    std::error_code ec;
    auto found = [&](const fs::path& candidate) {
        if (!fs::is_regular_file(candidate, ec)) {
            return false;
        }
        *resolved = Utf8(candidate.lexically_normal());
        if (layers && std::find(layers->folders.begin(), layers->folders.end(), candidate.parent_path()) ==
                          layers->folders.end()) {
            layers->folders.push_back(candidate.lexically_normal().parent_path());
        }
        return true;
    };
    if (asset.is_absolute()) {
        return found(asset) ? 0 : -1;
    }
    for (const std::string& dir : searchPaths) {
        if (found(Utf8Path(dir) / asset)) {
            return 0;
        }
    }
    if (layers) {
        for (auto folder = layers->folders.rbegin(); folder != layers->folders.rend(); ++folder) {
            if (found(*folder / asset)) {
                return 0;
            }
        }
    }
    return -1;
}

int FileSize(const char* name, uint64_t* bytes, std::string* err, void*) {
    std::error_code ec;
    *bytes = fs::file_size(Utf8Path(name ? name : ""), ec);
    if (ec) {
        *err = ec.message();
        return -1;
    }
    return 0;
}

int ReadFile(const char* name, uint64_t wanted, uint8_t* out, uint64_t* read, std::string* err, void*) {
    std::ifstream in(Utf8Path(name ? name : ""), std::ios::binary);
    in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(wanted));
    *read = static_cast<uint64_t>(in.gcount());
    if (!in && !in.eof()) {
        *err = "Could not read " + std::string(name ? name : "");
        return -1;
    }
    return 0;
}

/// Prim paths the converted scene must treat specially; Tydra converts them like any other prim.
struct PrimRoles {
    std::vector<std::string> abstractPaths;  ///< `class` prims (prototypes): only drawn through their instances
    std::vector<std::string> collisionPaths; ///< UsdPhysics PhysicsCollisionAPI: colliders, not drawn
    std::vector<std::string> hiddenPaths;    ///< guide/proxy purpose or invisible: not drawn
};

std::string TokenAttribute(const lightusd::PrimSpec& prim, const char* name) {
    const auto found = prim.props().find(name);
    lightusd::value::token token;
    if (found == prim.props().end() || !found->second.is_attribute() ||
        !found->second.get_attribute().get_value(&token)) {
        return {};
    }
    return token.str();
}

void CollectRoles(const lightusd::PrimSpec& prim, const std::string& parent, PrimRoles& out) {
    const std::string path = parent + "/" + prim.name();
    if (prim.specifier() == lightusd::Specifier::Class) {
        out.abstractPaths.push_back(path);
        return;
    }
    const lightusd::APISchemas* schemas = prim.metas().get_apiSchemas_ptr();
    const bool collider = schemas && std::any_of(schemas->names.begin(), schemas->names.end(), [](const auto& name) {
        return name.first == lightusd::APISchemas::APIName::PhysicsCollisionAPI;
    });
    const std::string purpose = TokenAttribute(prim, "purpose");
    if (collider) {
        out.collisionPaths.push_back(path);
    } else if (purpose == "guide" || purpose == "proxy" || TokenAttribute(prim, "visibility") == "invisible") {
        out.hiddenPaths.push_back(path);
    }
    for (const lightusd::PrimSpec& child : prim.children()) {
        CollectRoles(child, path, out);
    }
}

using Selections = std::map<std::string, std::string>;

bool LoadStage(const fs::path& path, const Selections& selections, lightusd::Stage& stage,
               lightusd::USDZAsset& usdz, lightusd::AssetResolutionResolver& resolver, LayerFolders& layers,
               std::map<std::string, std::vector<std::string>>* outVariantSets,
               PrimRoles& outRoles, std::string& outError) {
    const std::string file = path.string();
    const std::string base = path.parent_path().string();
    std::string warn;
    std::string err;
    if (lightusd::IsUSDZ(file)) {
        if (!lightusd::LoadUSDFromFile(file, &stage, &warn, &err) ||
            !lightusd::ReadUSDZAssetInfoFromFile(file, &usdz, &warn, &err) ||
            !lightusd::SetupUSDZAssetResolution(resolver, &usdz)) {
            outError = "Could not read " + path.filename().string() + ": " + err;
            return false;
        }
        return true;
    }
    lightusd::Layer layer;
    if (!lightusd::LoadLayerFromFile(file, &layer, &warn, &err)) {
        outError = "Could not read " + path.filename().string() + ": " + err;
        return false;
    }
    for (auto& [name, prim] : layer.primspecs()) {
        for (const auto& [setName, set] : prim.variantSets()) {
            if (outVariantSets) {
                for (const auto& [choice, spec] : set.variantSet) {
                    (*outVariantSets)[setName].push_back(choice);
                }
            }
            if (const auto chosen = selections.find(setName); chosen != selections.end()) {
                if (!prim.metas().variants.has_value()) {
                    prim.metas().variants = lightusd::VariantSelectionMap();
                }
                prim.metas().variants.value()[setName] = chosen->second;
            }
        }
    }
    lightusd::AssetResolutionHandler files;
    files.resolve_fun = &ResolveFile;
    files.size_fun = &FileSize;
    files.read_fun = &ReadFile;
    files.userdata = &layers;
    layers.folders = {path.parent_path().lexically_normal()};
    resolver.register_wildcard_asset_resolution_handler(files);
    resolver.set_current_working_path(base);
    resolver.set_search_paths({base});
    lightusd::SublayersCompositionOptions sublayers;
    sublayers.allow_parent_relative_paths = true;
    lightusd::AllArcsCompositionOptions arcs;
    arcs.references.allow_parent_relative_paths = true;
    arcs.payload.allow_parent_relative_paths = true;
    std::map<std::string, lightusd::Layer> cache;
    arcs.references.layer_cache = &cache;
    arcs.payload.layer_cache = &cache;
    lightusd::Layer composed;
    if (!lightusd::CompositeSublayers(resolver, layer, &composed, &warn, &err, sublayers)) {
        outError = "Could not compose " + path.filename().string() + "'s sublayers: " + err;
        return false;
    }
    layer = std::move(composed);
    for (int pass = 0; pass < kMaxCompositionPasses; ++pass) {
        if (!layer.check_unresolved_references() && !layer.check_unresolved_payload() &&
            !layer.check_unresolved_inherits() && !layer.check_unresolved_variant() &&
            !layer.check_unresolved_specializes()) {
            break;
        }
        lightusd::Layer next;
        if (!lightusd::CompositeAllArcs(resolver, layer, &next, &warn, &err, arcs)) {
            outError = "Could not compose " + path.filename().string() + ": " + err;
            return false;
        }
        layer = std::move(next);
    }
    for (const auto& [name, prim] : layer.primspecs()) {
        CollectRoles(prim, "", outRoles);
    }
    if (!lightusd::LayerToStage(std::move(layer), &stage, &warn, &err)) {
        outError = "Could not build the stage of " + path.filename().string() + ": " + err;
        return false;
    }
    return true;
}

/// The composed scene plus the paths of its `class` (abstract) prims, which Tydra converts but are never drawn.
struct Converted {
    tydra::RenderScene scene;
    PrimRoles roles;
};


bool Convert(const fs::path& path, const Selections& selections, Converted& out,
             std::map<std::string, std::vector<std::string>>* outVariantSets, std::string& outError) {
    lightusd::Stage stage;
    lightusd::USDZAsset usdz;
    lightusd::AssetResolutionResolver resolver;
    LayerFolders layers;
    if (!LoadStage(path, selections, stage, usdz, resolver, layers, outVariantSets, out.roles, outError)) {
        return false;
    }
    tydra::RenderSceneConverter converter;
    tydra::RenderSceneConverterEnv env(stage);
    env.asset_resolver = resolver;
    env.mesh_config.triangulate = true;
    env.mesh_config.build_vertex_indices = true;
    env.mesh_config.compute_tangents_and_binormals = false;
    env.scene_config.load_texture_assets = false;
    if (!converter.ConvertToRenderScene(env, &out.scene)) {
        outError = "Could not convert " + path.filename().string() + ": " + converter.GetError();
        return false;
    }
    return true;
}

struct TextureCount {
    size_t found = 0;
    size_t missing = 0;
};

TextureCount CountTextures(const tydra::RenderScene& scene, const fs::path& baseDir) {
    MaterialReader reader(scene, baseDir);
    TextureCount count;
    for (size_t i = 0; i < scene.textures.size(); ++i) {
        ++(reader.Texture(static_cast<int>(i)).Exists() ? count.found : count.missing);
    }
    return count;
}

bool UnderAny(const std::string& path, const std::vector<std::string>& roots) {
    return std::any_of(roots.begin(), roots.end(), [&](const std::string& root) {
        return path == root || (path.size() > root.size() && path.compare(0, root.size(), root) == 0 &&
                                path[root.size()] == '/');
    });
}

std::vector<std::string> Segments(const std::string& path) {
    std::vector<std::string> out;
    size_t start = 1;
    while (start <= path.size()) {
        const size_t end = std::min(path.find('/', start), path.size());
        if (end > start) {
            out.push_back(path.substr(start, end - start));
        }
        start = end + 1;
    }
    return out;
}

} // namespace

bool IsUsd(const fs::path& path) {
    const std::string ext = Lower(path.extension().string());
    return ext == ".usd" || ext == ".usda" || ext == ".usdc" || ext == ".usdz";
}

bool LoadParts(const fs::path& path, std::vector<Part>& outParts, std::vector<SourceMaterial>& outMaterials,
               std::string& outError) {
    outParts.clear();
    outMaterials.clear();
    Converted converted;
    std::map<std::string, std::vector<std::string>> variantSets;
    if (!Convert(path, {}, converted, &variantSets, outError)) {
        return false;
    }
    // Libraries such as Cargo only download some texture variants: use the one with the most files on disk.
    TextureCount best = CountTextures(converted.scene, path.parent_path());
    for (const auto& [setName, choices] : variantSets) {
        for (const std::string& choice : choices) {
            if (best.found > 0 && best.missing == 0) {
                break;
            }
            Converted candidate;
            std::string error;
            if (Convert(path, {{setName, choice}}, candidate, nullptr, error)) {
                const TextureCount count = CountTextures(candidate.scene, path.parent_path());
                if (count.found > best.found) {
                    best = count;
                    converted = std::move(candidate);
                }
            }
        }
    }
    const tydra::RenderScene& scene = converted.scene;

    const double unit = scene.meta.metersPerUnit > 0.0 ? scene.meta.metersPerUnit : 1.0;
    const bool zUp = scene.meta.upAxis == "Z";
    auto toEngine = [&](const double p[3], float* out, bool direction) {
        const double s = direction ? 1.0 : unit;
        out[0] = static_cast<float>(p[0] * s);
        out[1] = static_cast<float>((zUp ? p[2] : p[1]) * s);
        out[2] = static_cast<float>((zUp ? -p[1] : p[2]) * s);
    };

    MaterialReader reader(scene, path.parent_path());
    std::map<int, int> materialIndex;
    auto materialFor = [&](int material) {
        if (material < 0 || static_cast<size_t>(material) >= scene.materials.size()) {
            return -1;
        }
        auto [it, inserted] = materialIndex.try_emplace(material, static_cast<int>(outMaterials.size()));
        if (inserted) {
            outMaterials.push_back(reader.Read(scene.materials[static_cast<size_t>(material)]));
        }
        return it->second;
    };

    auto buildPart = [&](const tydra::RenderMesh& mesh, const lightusd::value::matrix4d& world, int materialOverride,
                         Part& part) {
        const std::vector<uint32_t>& faceVertices = mesh.faceVertexIndices();
        const std::vector<uint32_t>& counts = mesh.faceVertexCounts();
        const double det = world.m[0][0] * (world.m[1][1] * world.m[2][2] - world.m[1][2] * world.m[2][1]) -
                           world.m[0][1] * (world.m[1][0] * world.m[2][2] - world.m[1][2] * world.m[2][0]) +
                           world.m[0][2] * (world.m[1][0] * world.m[2][1] - world.m[1][1] * world.m[2][0]);
        const bool flip = (det < 0.0) != !mesh.is_rightHanded;
        std::vector<int> faceMaterial(counts.size(), materialOverride >= 0 ? materialOverride : mesh.material_id);
        if (materialOverride < 0) {
            for (const auto& [name, subset] : mesh.material_subsetMap) {
                for (int face : subset.indices()) {
                    if (face >= 0 && static_cast<size_t>(face) < faceMaterial.size()) {
                        faceMaterial[static_cast<size_t>(face)] = subset.material_id;
                    }
                }
            }
        }
        const double translation[3] = {world.m[3][0], world.m[3][1], world.m[3][2]};
        float origin[3];
        toEngine(translation, origin, false);
        part.origin = Vec3(origin[0], origin[1], origin[2]);

        std::vector<std::vector<uint32_t>> slotIndices;
        std::map<int, size_t> slotOfMaterial;
        std::vector<int64_t> shared(mesh.is_single_indexable ? mesh.points.size() : 0, -1);
        MeshData& out = part.mesh;
        size_t offset = 0;
        for (size_t face = 0; face < counts.size(); ++face) {
            const uint32_t count = counts[face];
            if (count != 3 || offset + 3 > faceVertices.size()) {
                offset += count;
                continue;
            }
            auto [slotIt, added] = slotOfMaterial.try_emplace(faceMaterial[face], slotIndices.size());
            if (added) {
                slotIndices.emplace_back();
            }
            std::vector<uint32_t>& target = slotIndices[slotIt->second];
            for (uint32_t corner = 0; corner < 3; ++corner) {
                const size_t fv = offset + (flip && corner > 0 ? 3 - corner : corner);
                const uint32_t point = faceVertices[fv];
                if (point >= mesh.points.size()) {
                    continue;
                }
                if (!shared.empty() && shared[point] >= 0) {
                    target.push_back(static_cast<uint32_t>(shared[point]));
                    continue;
                }
                Vertex vertex{};
                const auto& p = mesh.points[point];
                double relative[3];
                for (int axis = 0; axis < 3; ++axis) {
                    relative[axis] = p[0] * world.m[0][axis] + p[1] * world.m[1][axis] + p[2] * world.m[2][axis];
                }
                toEngine(relative, vertex.pos, false);
                float n[3] = {0.0f, 1.0f, 0.0f};
                if (ReadAttribute(mesh.normals, point, fv, 3, n)) {
                    double dir[3];
                    for (int axis = 0; axis < 3; ++axis) {
                        dir[axis] = n[0] * world.m[0][axis] + n[1] * world.m[1][axis] + n[2] * world.m[2][axis];
                    }
                    const double length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                    for (double& d : dir) {
                        d = length > 0.0 ? d / length : 0.0;
                    }
                    toEngine(dir, vertex.normal, true);
                } else {
                    vertex.normal[1] = 1.0f;
                }
                vertex.color[0] = vertex.color[1] = vertex.color[2] = 1.0f;
                const auto uv = mesh.texcoords.find(0);
                float st[2] = {0.0f, 0.0f};
                if (uv != mesh.texcoords.end() && ReadAttribute(uv->second, point, fv, 2, st)) {
                    vertex.texCoord[0] = st[0];
                    vertex.texCoord[1] = 1.0f - st[1]; // USD's st origin is bottom-left
                }
                if (!shared.empty()) {
                    shared[point] = static_cast<int64_t>(out.vertices.size());
                }
                target.push_back(static_cast<uint32_t>(out.vertices.size()));
                out.vertices.push_back(vertex);
            }
            offset += count;
        }
        std::vector<int> slotSource(slotIndices.size(), -1);
        for (const auto& [material, slot] : slotOfMaterial) {
            slotSource[slot] = material;
        }
        for (size_t slot = 0; slot < slotIndices.size(); ++slot) {
            MeshData::Submesh submesh;
            submesh.indexOffset = static_cast<uint32_t>(out.indices.size());
            submesh.indexCount = static_cast<uint32_t>(slotIndices[slot].size());
            submesh.materialSlot = static_cast<int>(slot);
            const int material = materialFor(slotSource[slot]);
            submesh.name = material >= 0 ? outMaterials[static_cast<size_t>(material)].name : std::string("Default");
            part.slotMaterials.push_back(material);
            out.indices.insert(out.indices.end(), slotIndices[slot].begin(), slotIndices[slot].end());
            out.submeshes.push_back(std::move(submesh));
        }
        const float big = std::numeric_limits<float>::max();
        auto boundsOf = [&](uint32_t first, uint32_t count) {
            Vec3 lo(big, big, big);
            Vec3 hi(-big, -big, -big);
            for (uint32_t i = first; i < first + count; ++i) {
                const float* q = out.vertices[out.indices[i]].pos;
                lo = Vec3(std::min(lo.x, q[0]), std::min(lo.y, q[1]), std::min(lo.z, q[2]));
                hi = Vec3(std::max(hi.x, q[0]), std::max(hi.y, q[1]), std::max(hi.z, q[2]));
            }
            return AABB(lo, hi);
        };
        for (MeshData::Submesh& submesh : out.submeshes) {
            submesh.bounds = boundsOf(submesh.indexOffset, submesh.indexCount);
        }
        out.bounds = boundsOf(0, static_cast<uint32_t>(out.indices.size()));
        return !out.indices.empty();
    };

    std::map<std::string, Vec3> nodeOrigins; // prim path -> world position, for group pivots
    std::vector<std::vector<std::string>> partPaths;
    auto addPart = [&](const tydra::RenderMesh& mesh, const lightusd::value::matrix4d& world, int materialOverride,
                       const std::string& absPath) {
        Part part;
        part.collision = UnderAny(absPath, converted.roles.collisionPaths);
        if (buildPart(mesh, world, materialOverride, part)) {
            outParts.push_back(std::move(part));
            partPaths.push_back(Segments(absPath));
        }
    };

    std::function<void(const tydra::Node&, int)> visit = [&](const tydra::Node& node, int depth) {
        if (depth > 256 || UnderAny(node.abs_path, converted.roles.abstractPaths) ||
            (UnderAny(node.abs_path, converted.roles.hiddenPaths) &&
             !UnderAny(node.abs_path, converted.roles.collisionPaths))) {
            return;
        }
        const double at[3] = {node.global_matrix.m[3][0], node.global_matrix.m[3][1], node.global_matrix.m[3][2]};
        float origin[3];
        toEngine(at, origin, false);
        nodeOrigins[node.abs_path] = Vec3(origin[0], origin[1], origin[2]);
        if (node.is_instance && node.instance_id >= 0 && static_cast<size_t>(node.instance_id) < scene.instances.size()) {
            const tydra::RenderInstance& instance = scene.instances[static_cast<size_t>(node.instance_id)];
            if (instance.visible && instance.mesh_id >= 0 && static_cast<size_t>(instance.mesh_id) < scene.meshes.size()) {
                addPart(scene.meshes[static_cast<size_t>(instance.mesh_id)], instance.global_matrix,
                        instance.material_id, node.abs_path);
            }
        } else if (node.nodeType == tydra::NodeType::Mesh && node.id >= 0 &&
                   static_cast<size_t>(node.id) < scene.meshes.size()) {
            addPart(scene.meshes[static_cast<size_t>(node.id)], node.global_matrix, -1, node.abs_path);
        }
        for (const tydra::Node& child : node.children) {
            visit(child, depth + 1);
        }
    };
    for (const tydra::Node& node : scene.nodes) {
        visit(node, 0);
    }
    if (outParts.empty()) {
        outError = path.filename().string() + " has no polygon meshes.";
        return false;
    }

    // Names are prim paths below what every part shares (the asset prim and a "geo"-style scope), with a Mesh named
    // like its Xform said once; a part deeper than its component is grouped under it, so a wheel stays together.
    size_t common = partPaths.front().size() - 1;
    for (const std::vector<std::string>& segments : partPaths) {
        size_t same = 0;
        while (same < common && same + 1 < segments.size() && segments[same] == partPaths.front()[same]) {
            ++same;
        }
        common = same;
    }
    std::map<std::string, int> nameUses;
    for (size_t i = 0; i < outParts.size(); ++i) {
        std::vector<std::string> segments;
        for (size_t s = common; s < partPaths[i].size(); ++s) {
            if (segments.empty() || segments.back() != partPaths[i][s]) {
                segments.push_back(partPaths[i][s]);
            }
        }
        Part& part = outParts[i];
        if (segments.size() > 1) {
            part.group = segments.front();
            std::string groupPath;
            for (size_t s = 0; s <= common; ++s) {
                groupPath += "/" + partPaths[i][s];
            }
            if (const auto origin = nodeOrigins.find(groupPath); origin != nodeOrigins.end()) {
                part.groupOrigin = origin->second;
            }
            segments.erase(segments.begin());
        }
        for (const std::string& segment : segments) {
            part.name += (part.name.empty() ? "" : "_") + segment;
        }
        if (part.name.empty()) {
            part.name = "Mesh";
        }
        if (const int uses = nameUses[part.group + "/" + part.name]++; uses > 0) {
            part.name += "_" + std::to_string(uses + 1);
        }
    }
    return true;
}

} // namespace Hephaestus::Usd
