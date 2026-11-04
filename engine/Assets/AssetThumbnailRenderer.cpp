#include "AssetThumbnailRenderer.h"

#include "AssetPathPolicy.h"
#include "ModelImporter.h"
#include "../Gfx/BuiltinMeshes.h"
#include "../Scene/PrefabInstantiator.h"
#include "../Scene/PrefabSerializer.h"
#include "../Scene/Scene.h"
#include "../UI/MaterialEditor/MaterialAssetSerializer.h"

#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>

namespace Hephaestus {

namespace {

constexpr float kPi = 3.14159265f;

Vec3 vmul(Vec3 a, Vec3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
Vec3 vmix(Vec3 a, Vec3 b, float t) { return vadd(vscale(a, 1.0f - t), vscale(b, t)); }
float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

float SrgbToLinear(uint8_t value)
{
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const float c = i / 255.0f;
            t[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return table[value];
}

/** Bilinear, repeat-wrapped sample decoded from sRGB, as the engine's SRGB texture views do. */
Vec3 SampleLinear(const ThumbnailImage& image, float u, float v)
{
    const float x = (u - std::floor(u)) * image.width - 0.5f;
    const float y = (v - std::floor(v)) * image.height - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - x0;
    const float fy = y - y0;
    auto texel = [&](int tx, int ty) {
        tx = ((tx % image.width) + image.width) % image.width;
        ty = ((ty % image.height) + image.height) % image.height;
        const uint8_t* p = &image.rgba[(static_cast<size_t>(ty) * image.width + tx) * 4];
        return v3(SrgbToLinear(p[0]), SrgbToLinear(p[1]), SrgbToLinear(p[2]));
    };
    const Vec3 top = vmix(texel(x0, y0), texel(x0 + 1, y0), fx);
    const Vec3 bottom = vmix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), fx);
    return vmix(top, bottom, fy);
}

float DistributionGGX(Vec3 N, Vec3 H, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float NdotH = std::max(vdot(N, H), 0.0f);
    const float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    const float r = roughness + 1.0f;
    const float k = (r * r) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

Vec3 FresnelSchlick(float cosTheta, Vec3 F0)
{
    const float f = std::pow(Clamp01(1.0f - cosTheta), 5.0f);
    return vadd(F0, vscale(vsub(v3(1, 1, 1), F0), f));
}

struct Camera {
    Vec3 eye, right, up, forward;
    float tanHalfFov = 0.0f;
};

struct Light {
    Vec3 direction; // towards the light
    Vec3 radiance;
};

/** Studio backdrop reflected by glossy and metallic surfaces. */
Vec3 Environment(Vec3 direction, Vec3 keyDirection)
{
    const float t = Clamp01(direction.y * 0.5f + 0.5f);
    const float horizon = std::clamp((t - 0.35f) / 0.4f, 0.0f, 1.0f);
    Vec3 color = vmix(v3(0.20f, 0.19f, 0.18f), v3(0.85f, 0.90f, 1.0f), horizon * horizon * (3.0f - 2.0f * horizon));
    const float softbox = std::pow(std::max(vdot(direction, keyDirection), 0.0f), 48.0f);
    return vadd(color, vscale(v3(1, 1, 1), softbox * 4.0f));
}

/** Port of triangle.frag's lit path, with a fixed studio rig instead of scene lights and GI. */
Vec3 Shade(const ThumbnailMaterial& material, Vec3 P, Vec3 N, float u, float v, Vec3 vertexColor,
           const Camera& camera, const std::array<Light, 2>& lights)
{
    const MaterialComponent& m = material.params;
    const Vec3 V = vnorm(vsub(camera.eye, P));
    if (vdot(N, V) < 0.0f) {
        N = vscale(N, -1.0f);
    }

    Vec3 albedo = vmul(m.albedo, vertexColor);
    if (m.useAlbedoTexture && material.albedo) {
        albedo = vmul(SampleLinear(*material.albedo, u, v), albedo);
    }
    if (m.useNormalTexture && material.normal) {
        const Vec3 tex = SampleLinear(*material.normal, u, v);
        const Vec3 mapped = vnorm(v3(tex.x * 2.0f - 1.0f, tex.y * 2.0f - 1.0f, tex.z * 2.0f - 1.0f));
        N = vnorm(vmix(N, mapped, 0.7f));
    }
    float roughness = m.roughness;
    if (m.useRoughnessTexture && material.roughness) {
        roughness *= SampleLinear(*material.roughness, u, v).x;
    }
    roughness = std::max(roughness, 0.04f);
    float ao = m.ao;
    if (m.useAoTexture && material.ao) {
        ao *= SampleLinear(*material.ao, u, v).x;
    }
    float metallic = m.metallic;
    if (m.useMetallicTexture && material.metallic) {
        metallic *= SampleLinear(*material.metallic, u, v).x;
    }
    metallic = Clamp01(metallic);

    const float specular = 0.08f * Clamp01(m.specular);
    const Vec3 F0 = vmix(v3(specular, specular, specular), albedo, metallic);
    const float NdotV = std::max(vdot(N, V), 0.0f);

    Vec3 Lo;
    for (const Light& light : lights) {
        const Vec3 L = light.direction;
        const Vec3 H = vnorm(vadd(V, L));
        const float NdotL = std::max(vdot(N, L), 0.0f);
        const float NDF = DistributionGGX(N, H, roughness);
        const float G = GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
        const Vec3 F = FresnelSchlick(std::max(vdot(H, V), 0.0f), F0);
        const Vec3 spec = vscale(F, NDF * G / (4.0f * NdotV * NdotL + 0.0001f));
        const Vec3 kD = vscale(vsub(v3(1, 1, 1), F), 1.0f - metallic);
        const Vec3 diffuse = vscale(vmul(kD, albedo), 1.0f / kPi);
        Lo = vadd(Lo, vscale(vmul(vadd(diffuse, spec), light.radiance), NdotL));
    }

    const Vec3 hemisphere = vmix(v3(0.12f, 0.11f, 0.10f), v3(0.36f, 0.39f, 0.46f), N.y * 0.5f + 0.5f);
    Vec3 color = vadd(Lo, vscale(vmul(hemisphere, albedo), (1.0f - metallic) * ao));

    if (m.reflectionStrength > 0.0f) {
        const Vec3 R = vsub(vscale(N, 2.0f * vdot(N, V)), V);
        float roughnessFactor = 1.0f - Clamp01(roughness);
        roughnessFactor *= roughnessFactor;
        const Vec3 env = vmul(Environment(R, lights[0].direction), FresnelSchlick(NdotV, F0));
        color = vadd(color, vscale(env, roughnessFactor * m.reflectionStrength * ao));
    }
    color = vadd(color, vscale(m.emissive, m.emissiveStrength));

    // tonemap.frag: Reinhard, then gamma 2.2.
    auto display = [](float c) {
        c = std::max(c, 0.0f);
        return std::pow(c / (c + 1.0f), 1.0f / 2.2f);
    };
    return v3(display(color.x), display(color.y), display(color.z));
}

Vec3 TransformNormal(const Mat4& inverseWorld, const float* n)
{
    // Inverse-transpose keeps normals perpendicular under non-uniform scale.
    const float* m = inverseWorld.m;
    return vnorm(v3(m[0] * n[0] + m[1] * n[1] + m[2] * n[2],
                    m[4] * n[0] + m[5] * n[1] + m[6] * n[2],
                    m[8] * n[0] + m[9] * n[1] + m[10] * n[2]));
}

bool Discards(const MaterialComponent& m)
{
    return Clamp01(m.opacity) < Clamp01(m.opacityMask);
}

struct PreparedDraw {
    const ThumbnailDraw* draw = nullptr;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<const ThumbnailMaterial*> triangleMaterials;
};

struct Fragment {
    float invDepth = 0.0f;
    int draw = -1;
    uint32_t triangle = 0;
    float b0 = 0.0f;
    float b1 = 0.0f;
};

} // namespace

ThumbnailImage RenderThumbnail(const std::vector<ThumbnailDraw>& draws, int size, ThumbnailView view)
{
    static const ThumbnailMaterial defaultMaterial{};

    ThumbnailImage out;
    if (size <= 0) {
        return out;
    }

    std::vector<PreparedDraw> prepared;
    Vec3 lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    Vec3 hi = vscale(lo, -1.0f);
    for (const ThumbnailDraw& draw : draws) {
        if (!draw.mesh || draw.mesh->vertices.empty() || draw.mesh->indices.size() < 3) {
            continue;
        }
        PreparedDraw p;
        p.draw = &draw;
        const Mat4 inverseWorld = Mat4::Invert(draw.world);
        p.positions.reserve(draw.mesh->vertices.size());
        p.normals.reserve(draw.mesh->vertices.size());
        for (const Vertex& vertex : draw.mesh->vertices) {
            const Vec3 pos = Mat4::TransformPoint(draw.world, v3(vertex.pos[0], vertex.pos[1], vertex.pos[2]));
            p.positions.push_back(pos);
            p.normals.push_back(TransformNormal(inverseWorld, vertex.normal));
            lo = v3(std::min(lo.x, pos.x), std::min(lo.y, pos.y), std::min(lo.z, pos.z));
            hi = v3(std::max(hi.x, pos.x), std::max(hi.y, pos.y), std::max(hi.z, pos.z));
        }
        auto slotMaterial = [&draw](int slot) {
            const ThumbnailMaterial* material =
                slot >= 0 && slot < static_cast<int>(draw.materials.size()) ? draw.materials[slot] : nullptr;
            return material ? material : &defaultMaterial;
        };
        const size_t triangleCount = draw.mesh->indices.size() / 3;
        p.triangleMaterials.assign(triangleCount, slotMaterial(0));
        for (const MeshData::Submesh& submesh : draw.mesh->submeshes) {
            const size_t first = submesh.indexOffset / 3;
            const size_t last = std::min(triangleCount, (static_cast<size_t>(submesh.indexOffset) + submesh.indexCount) / 3);
            for (size_t t = first; t < last; ++t) {
                p.triangleMaterials[t] = slotMaterial(submesh.materialSlot);
            }
        }
        prepared.push_back(std::move(p));
    }
    if (prepared.empty()) {
        return out;
    }
    out.width = size;
    out.height = size;
    out.rgba.assign(static_cast<size_t>(size) * size * 4, 0);

    const Vec3 center = vscale(vadd(lo, hi), 0.5f);
    float radius = 0.0f;
    for (const PreparedDraw& p : prepared) {
        for (const Vec3& pos : p.positions) {
            radius = std::max(radius, vlen(vsub(pos, center)));
        }
    }
    radius = std::max(radius, 0.001f);

    const float halfFov = 15.0f * kPi / 180.0f;
    const Vec3 toEye = view == ThumbnailView::Front ? vnorm(v3(0.0f, 0.15f, 1.0f)) : vnorm(v3(1.0f, 0.75f, 1.35f));
    Camera camera;
    camera.tanHalfFov = std::tan(halfFov);
    camera.eye = vadd(center, vscale(toEye, radius / std::sin(halfFov) * 1.06f));
    camera.forward = vscale(toEye, -1.0f);
    camera.right = vnorm(vcross(camera.forward, v3(0, 1, 0)));
    camera.up = vcross(camera.right, camera.forward);

    const std::array<Light, 2> lights = {{
        { vnorm(vadd(vadd(vscale(camera.right, -0.55f), vscale(camera.up, 0.8f)), vscale(toEye, 0.6f))), v3(3.2f, 3.1f, 2.95f) },
        { vnorm(vadd(vadd(vscale(camera.right, 0.9f), vscale(camera.up, 0.1f)), vscale(toEye, 0.4f))), v3(0.7f, 0.8f, 0.95f) },
    }};

    // Rasterize at 2x and box-filter down for anti-aliased edges.
    const int S = size * 2;
    std::vector<Fragment> fragments(static_cast<size_t>(S) * S);
    struct ScreenVertex { float x, y, invZ; };
    auto project = [&](Vec3 p) {
        const Vec3 c = vsub(p, camera.eye);
        const float z = std::max(vdot(c, camera.forward), 1e-4f);
        const float scale = 1.0f / (z * camera.tanHalfFov);
        return ScreenVertex{ (vdot(c, camera.right) * scale * 0.5f + 0.5f) * S,
                             (0.5f - vdot(c, camera.up) * scale * 0.5f) * S,
                             1.0f / z };
    };

    for (int d = 0; d < static_cast<int>(prepared.size()); ++d) {
        const PreparedDraw& p = prepared[d];
        const std::vector<uint32_t>& indices = p.draw->mesh->indices;
        std::vector<ScreenVertex> screen;
        screen.reserve(p.positions.size());
        for (const Vec3& pos : p.positions) {
            screen.push_back(project(pos));
        }
        for (uint32_t t = 0; t < p.triangleMaterials.size(); ++t) {
            if (Discards(p.triangleMaterials[t]->params)) {
                continue;
            }
            const uint32_t i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
            if (i0 >= screen.size() || i1 >= screen.size() || i2 >= screen.size()) {
                continue;
            }
            const ScreenVertex& a = screen[i0];
            const ScreenVertex& b = screen[i1];
            const ScreenVertex& c = screen[i2];
            const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (std::abs(area) < 1e-8f) {
                continue;
            }
            const int minX = std::max(0, static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))));
            const int maxX = std::min(S - 1, static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))));
            const int minY = std::max(0, static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))));
            const int maxY = std::min(S - 1, static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))));
            const float invArea = 1.0f / area;
            for (int y = minY; y <= maxY; ++y) {
                const float py = y + 0.5f;
                for (int x = minX; x <= maxX; ++x) {
                    const float px = x + 0.5f;
                    const float w0 = ((c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x)) * invArea;
                    const float w1 = ((a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x)) * invArea;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                        continue;
                    }
                    const float invZ = w0 * a.invZ + w1 * b.invZ + w2 * c.invZ;
                    Fragment& f = fragments[static_cast<size_t>(y) * S + x];
                    if (invZ <= f.invDepth) {
                        continue;
                    }
                    f.invDepth = invZ;
                    f.draw = d;
                    f.triangle = t;
                    f.b0 = w0 * a.invZ / invZ;
                    f.b1 = w1 * b.invZ / invZ;
                }
            }
        }
    }

    std::vector<Vec3> shaded(fragments.size());
    for (size_t i = 0; i < fragments.size(); ++i) {
        const Fragment& f = fragments[i];
        if (f.draw < 0) {
            continue;
        }
        const PreparedDraw& p = prepared[f.draw];
        const std::vector<uint32_t>& indices = p.draw->mesh->indices;
        const uint32_t i0 = indices[f.triangle * 3], i1 = indices[f.triangle * 3 + 1], i2 = indices[f.triangle * 3 + 2];
        const float b0 = f.b0, b1 = f.b1, b2 = 1.0f - f.b0 - f.b1;
        auto blend = [&](Vec3 x0, Vec3 x1, Vec3 x2) { return vadd(vadd(vscale(x0, b0), vscale(x1, b1)), vscale(x2, b2)); };
        const Vertex& v0 = p.draw->mesh->vertices[i0];
        const Vertex& v1 = p.draw->mesh->vertices[i1];
        const Vertex& v2 = p.draw->mesh->vertices[i2];
        const Vec3 position = blend(p.positions[i0], p.positions[i1], p.positions[i2]);
        Vec3 normal = vnorm(blend(p.normals[i0], p.normals[i1], p.normals[i2]));
        if (vlen(normal) == 0.0f) {
            normal = vnorm(vcross(vsub(p.positions[i1], p.positions[i0]), vsub(p.positions[i2], p.positions[i0])));
        }
        const Vec3 color = blend(v3(v0.color[0], v0.color[1], v0.color[2]),
                                 v3(v1.color[0], v1.color[1], v1.color[2]),
                                 v3(v2.color[0], v2.color[1], v2.color[2]));
        const float u = b0 * v0.texCoord[0] + b1 * v1.texCoord[0] + b2 * v2.texCoord[0];
        const float v = b0 * v0.texCoord[1] + b1 * v1.texCoord[1] + b2 * v2.texCoord[1];
        shaded[i] = Shade(*p.triangleMaterials[f.triangle], position, normal, u, v, color, camera, lights);
    }

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            Vec3 sum;
            int covered = 0;
            for (int sy = 0; sy < 2; ++sy) {
                for (int sx = 0; sx < 2; ++sx) {
                    const size_t i = static_cast<size_t>(y * 2 + sy) * S + (x * 2 + sx);
                    if (fragments[i].draw >= 0) {
                        sum = vadd(sum, shaded[i]);
                        ++covered;
                    }
                }
            }
            if (covered == 0) {
                continue;
            }
            uint8_t* px = &out.rgba[(static_cast<size_t>(y) * size + x) * 4];
            px[0] = static_cast<uint8_t>(std::lround(Clamp01(sum.x / covered) * 255.0f));
            px[1] = static_cast<uint8_t>(std::lround(Clamp01(sum.y / covered) * 255.0f));
            px[2] = static_cast<uint8_t>(std::lround(Clamp01(sum.z / covered) * 255.0f));
            px[3] = static_cast<uint8_t>(covered * 255 / 4);
        }
    }
    return out;
}

bool LoadThumbnailImage(const std::filesystem::path& file, ThumbnailImage& out)
{
    // VulkanTexture toggles the global flip flag for HDRs on the render thread.
    stbi_set_flip_vertically_on_load_thread(0);
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(file.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        return false;
    }
    out.width = width;
    out.height = height;
    out.rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return true;
}

void ShrinkThumbnailImage(ThumbnailImage& image, int maxSide)
{
    while (image.width > maxSide || image.height > maxSide) {
        const int w = std::max(1, image.width / 2);
        const int h = std::max(1, image.height / 2);
        std::vector<uint8_t> half(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 4; ++c) {
                    int sum = 0;
                    for (int sy = 0; sy < 2; ++sy) {
                        for (int sx = 0; sx < 2; ++sx) {
                            const int ix = std::min(x * 2 + sx, image.width - 1);
                            const int iy = std::min(y * 2 + sy, image.height - 1);
                            sum += image.rgba[(static_cast<size_t>(iy) * image.width + ix) * 4 + c];
                        }
                    }
                    half[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
                }
            }
        }
        image.width = w;
        image.height = h;
        image.rgba = std::move(half);
    }
}

namespace {

/** Loads and keeps everything one thumbnail render needs, recording each file it reads. */
class ThumbnailSources {
public:
    ThumbnailSources(const std::filesystem::path& projectRoot, const std::filesystem::path& assetRoot,
                     std::vector<std::filesystem::path>& dependencies)
        : m_projectRoot(projectRoot), m_assetRoot(assetRoot), m_dependencies(dependencies) {}

    std::filesystem::path Resolve(const std::string& key) const
    {
        std::error_code ec;
        const std::filesystem::path resolved = AssetPathPolicy::ResolvePortableKey(m_projectRoot, key);
        if (!resolved.empty() && std::filesystem::is_regular_file(resolved, ec)) {
            return resolved;
        }
        // Material texture paths may still be relative to the asset root.
        if (!key.empty() && key.rfind("builtin://", 0) != 0 && !m_assetRoot.empty()) {
            const std::filesystem::path underAssets = (m_assetRoot / key).lexically_normal();
            if (std::filesystem::is_regular_file(underAssets, ec)) {
                return underAssets;
            }
        }
        return {};
    }

    const MeshData* Mesh(const std::string& key)
    {
        auto [it, inserted] = m_meshes.try_emplace(key);
        if (inserted) {
            auto mesh = std::make_unique<MeshData>();
            bool loaded = BuildBuiltinMesh(key, *mesh);
            if (!loaded) {
                const std::filesystem::path file = Resolve(key);
                std::string error;
                loaded = !file.empty() && ModelImporter::LoadModel(file, *mesh, error);
                if (!file.empty()) {
                    m_dependencies.push_back(file);
                }
            }
            if (loaded) {
                it->second = std::move(mesh);
            }
        }
        return it->second.get();
    }

    const ThumbnailMaterial* MaterialFile(const std::filesystem::path& file)
    {
        auto [it, inserted] = m_materialFiles.try_emplace(file.generic_string());
        if (inserted && !file.empty()) {
            m_dependencies.push_back(file);
            MaterialComponent params;
            std::string displayName, error;
            MaterialEditor::Graph graph;
            if (MaterialEditor::LoadMaterialAssetCompat(file, params, displayName, graph, error)) {
                it->second = std::make_unique<ThumbnailMaterial>(WithTextures(params));
            }
        }
        return it->second.get();
    }

    const ThumbnailMaterial* Inline(const MaterialComponent& params)
    {
        m_inline.push_back(std::make_unique<ThumbnailMaterial>(WithTextures(params)));
        return m_inline.back().get();
    }

private:
    ThumbnailMaterial WithTextures(const MaterialComponent& params)
    {
        ThumbnailMaterial material;
        material.params = params;
        auto map = [this](bool used, const std::string& key) { return used ? Texture(key) : nullptr; };
        material.albedo = map(params.useAlbedoTexture, params.albedoTexturePath);
        material.normal = map(params.useNormalTexture, params.normalTexturePath);
        material.roughness = map(params.useRoughnessTexture, params.roughnessTexturePath);
        material.metallic = map(params.useMetallicTexture, params.metallicTexturePath);
        material.ao = map(params.useAoTexture, params.aoTexturePath);
        return material;
    }

    const ThumbnailImage* Texture(const std::string& key)
    {
        const std::filesystem::path file = Resolve(key);
        if (file.empty()) {
            return nullptr;
        }
        auto [it, inserted] = m_textures.try_emplace(file.generic_string());
        if (inserted) {
            m_dependencies.push_back(file);
            auto image = std::make_unique<ThumbnailImage>();
            if (LoadThumbnailImage(file, *image)) {
                ShrinkThumbnailImage(*image, 512);
                it->second = std::move(image);
            }
        }
        return it->second.get();
    }

    std::filesystem::path m_projectRoot;
    std::filesystem::path m_assetRoot;
    std::vector<std::filesystem::path>& m_dependencies;
    std::map<std::string, std::unique_ptr<MeshData>> m_meshes;
    std::map<std::string, std::unique_ptr<ThumbnailMaterial>> m_materialFiles;
    std::map<std::string, std::unique_ptr<ThumbnailImage>> m_textures;
    std::vector<std::unique_ptr<ThumbnailMaterial>> m_inline;
};

} // namespace

bool RenderAssetThumbnail(const std::filesystem::path& projectRoot,
                          const std::filesystem::path& assetRoot,
                          const std::filesystem::path& assetFile,
                          AssetType type,
                          int size,
                          ThumbnailImage& out,
                          std::vector<std::filesystem::path>& outDependencies,
                          std::string& outError)
{
    outDependencies.clear();
    ThumbnailSources sources(projectRoot, assetRoot, outDependencies);
    std::vector<ThumbnailDraw> draws;
    ThumbnailView view = ThumbnailView::ThreeQuarter;
    MeshData ownMesh;

    switch (type) {
        case AssetType::Material: {
            const ThumbnailMaterial* material = sources.MaterialFile(assetFile);
            if (!material) {
                outError = "Could not load material";
                return false;
            }
            BuildBuiltinMesh("builtin://sphere", ownMesh);
            draws.push_back({ &ownMesh, Mat4::Identity(), { material } });
            view = ThumbnailView::Front;
            break;
        }
        case AssetType::StaticMesh: {
            outDependencies.push_back(assetFile);
            if (!ModelImporter::LoadModel(assetFile, ownMesh, outError)) {
                return false;
            }
            draws.push_back({ &ownMesh, Mat4::Identity(), {} });
            break;
        }
        case AssetType::Prefab: {
            outDependencies.push_back(assetFile);
            PrefabAsset asset;
            if (!PrefabSerializer::Deserialize(asset, assetFile, outError)) {
                return false;
            }
            Scene scene("Thumbnail");
            PrefabInstantiateOptions options;
            options.prefabPath = assetFile.generic_string();
            options.attachInstanceComponent = false;
            PrefabInstantiator::Instantiate(scene, asset, options);
            for (entt::entity handle : scene.GetEntitiesWith<MeshRendererComponent>()) {
                Entity entity(handle, &scene);
                const MeshRendererComponent& renderer = entity.GetComponent<MeshRendererComponent>();
                const MeshData* mesh = renderer.visible ? sources.Mesh(renderer.meshAssetPath) : nullptr;
                if (!mesh) {
                    continue;
                }
                const ThumbnailMaterial* fallback = entity.HasComponent<MaterialComponent>()
                    ? sources.Inline(entity.GetComponent<MaterialComponent>())
                    : nullptr;
                ThumbnailDraw draw{ mesh, scene.GetWorldTransform(entity), {} };
                const size_t slotCount = std::max<size_t>(renderer.appliedMaterialPaths.size(), 1);
                for (size_t slot = 0; slot < slotCount; ++slot) {
                    const std::string key = slot < renderer.appliedMaterialPaths.size() ? renderer.appliedMaterialPaths[slot] : std::string();
                    const ThumbnailMaterial* material = key.empty() ? nullptr : sources.MaterialFile(sources.Resolve(key));
                    draw.materials.push_back(material ? material : fallback);
                }
                draws.push_back(std::move(draw));
            }
            break;
        }
        default:
            outError = "No preview for this asset type";
            return false;
    }

    if (draws.empty()) {
        outError = "Nothing visible to preview";
        return false;
    }
    out = RenderThumbnail(draws, size, view);
    if (out.rgba.empty()) {
        outError = "Nothing visible to preview";
        return false;
    }
    return true;
}

} // namespace Hephaestus
