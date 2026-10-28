#include "ProbeBake.h"

#include "../Scene/Components.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>

namespace Hephaestus {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr char kMagic[4] = {'H', 'P', 'P', 'B'};
constexpr uint32_t kVersion = 1;
// DDGI (Majercik et al. 2019): a probe that sees backfaces on over 25% of its rays is inside geometry.
constexpr float kBackfaceInsideFraction = 0.25f;

struct Aabb {
    Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
             std::numeric_limits<float>::max()};
    Vec3 max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
             -std::numeric_limits<float>::max()};

    void Grow(const Vec3& p) {
        min = v3(std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z));
        max = v3(std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z));
    }
    void Grow(const Aabb& b) {
        Grow(b.min);
        Grow(b.max);
    }
};

struct BvhNode {
    Aabb bounds;
    uint32_t first = 0;  // first child index for inner nodes, first triangle for leaves
    uint32_t count = 0;  // triangle count; 0 for inner nodes
};

class Bvh {
public:
    explicit Bvh(const std::vector<ProbeBakeTriangle>& triangles) : m_tris(triangles) {
        m_order.resize(triangles.size());
        for (uint32_t i = 0; i < m_order.size(); ++i) m_order[i] = i;
        m_centroids.resize(triangles.size());
        for (size_t i = 0; i < triangles.size(); ++i) {
            const auto& t = triangles[i];
            m_centroids[i] = vscale(vadd(vadd(t.a, t.b), t.c), 1.0f / 3.0f);
        }
        if (!triangles.empty()) {
            m_nodes.reserve(triangles.size() * 2);
            m_nodes.push_back({});
            build(0, 0, static_cast<uint32_t>(triangles.size()));
        }
    }

    // Closest hit along origin + t * dir, t in (0, maxT).
    bool Intersect(const Vec3& origin, const Vec3& dir, float& outT, uint32_t& outTri) const {
        if (m_nodes.empty()) return false;
        const Vec3 inv(1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z);
        float best = std::numeric_limits<float>::max();
        bool hit = false;
        uint32_t stack[64];  // median splits bound the depth by log2(triangle count) + 1
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const BvhNode& node = m_nodes[stack[--top]];
            if (!hitsBox(node.bounds, origin, inv, best)) continue;
            if (node.count > 0) {
                for (uint32_t i = 0; i < node.count; ++i) {
                    const uint32_t tri = m_order[node.first + i];
                    float t;
                    if (intersectTriangle(m_tris[tri], origin, dir, t) && t < best) {
                        best = t;
                        outTri = tri;
                        hit = true;
                    }
                }
            } else {
                stack[top++] = node.first;
                stack[top++] = node.first + 1;
            }
        }
        outT = best;
        return hit;
    }

private:
    void build(uint32_t nodeIndex, uint32_t first, uint32_t count) {
        Aabb bounds, centroidBounds;
        for (uint32_t i = first; i < first + count; ++i) {
            const auto& t = m_tris[m_order[i]];
            bounds.Grow(t.a);
            bounds.Grow(t.b);
            bounds.Grow(t.c);
            centroidBounds.Grow(m_centroids[m_order[i]]);
        }
        m_nodes[nodeIndex].bounds = bounds;
        const Vec3 extent = vsub(centroidBounds.max, centroidBounds.min);
        const int axis = extent.x > extent.y ? (extent.x > extent.z ? 0 : 2) : (extent.y > extent.z ? 1 : 2);
        const float axisExtent = axis == 0 ? extent.x : axis == 1 ? extent.y : extent.z;
        if (count <= 4 || axisExtent <= 0.0f) {
            m_nodes[nodeIndex].first = first;
            m_nodes[nodeIndex].count = count;
            return;
        }
        auto key = [&](uint32_t tri) {
            const Vec3& c = m_centroids[tri];
            return axis == 0 ? c.x : axis == 1 ? c.y : c.z;
        };
        const uint32_t mid = first + count / 2;
        std::nth_element(m_order.begin() + first, m_order.begin() + mid, m_order.begin() + first + count,
                         [&](uint32_t l, uint32_t r) { return key(l) < key(r); });
        const uint32_t left = static_cast<uint32_t>(m_nodes.size());
        m_nodes.push_back({});
        m_nodes.push_back({});
        m_nodes[nodeIndex].first = left;
        m_nodes[nodeIndex].count = 0;
        build(left, first, mid - first);
        build(left + 1, mid, first + count - mid);
    }

    static bool hitsBox(const Aabb& b, const Vec3& o, const Vec3& inv, float maxT) {
        float t0 = 0.0f, t1 = maxT;
        const float lo[3] = {b.min.x, b.min.y, b.min.z};
        const float hi[3] = {b.max.x, b.max.y, b.max.z};
        const float org[3] = {o.x, o.y, o.z};
        const float id[3] = {inv.x, inv.y, inv.z};
        for (int a = 0; a < 3; ++a) {
            float tn = (lo[a] - org[a]) * id[a];
            float tf = (hi[a] - org[a]) * id[a];
            if (tn > tf) std::swap(tn, tf);
            t0 = std::max(t0, tn);
            t1 = std::min(t1, tf);
            if (t0 > t1) return false;
        }
        return true;
    }

    static bool intersectTriangle(const ProbeBakeTriangle& tri, const Vec3& o, const Vec3& d, float& t) {
        const Vec3 e1 = vsub(tri.b, tri.a);
        const Vec3 e2 = vsub(tri.c, tri.a);
        const Vec3 p = vcross(d, e2);
        const float det = vdot(e1, p);
        if (std::fabs(det) < 1.0e-12f) return false;
        const float invDet = 1.0f / det;
        const Vec3 s = vsub(o, tri.a);
        const float u = vdot(s, p) * invDet;
        if (u < 0.0f || u > 1.0f) return false;
        const Vec3 q = vcross(s, e1);
        const float v = vdot(d, q) * invDet;
        if (v < 0.0f || u + v > 1.0f) return false;
        t = vdot(e2, q) * invDet;
        return t > 1.0e-5f;
    }

    const std::vector<ProbeBakeTriangle>& m_tris;
    std::vector<uint32_t> m_order;
    std::vector<Vec3> m_centroids;
    std::vector<BvhNode> m_nodes;
};

struct TraceResult {
    ProbeRayHit hit;
    bool backface = false;
};

TraceResult Trace(const Bvh& bvh, const ProbeBakeGeometry& geometry, const Vec3& origin, const Vec3& dir) {
    TraceResult r;
    float t = 0.0f;
    uint32_t tri = 0;
    if (!bvh.Intersect(origin, dir, t, tri)) {
        return r;
    }
    const ProbeBakeTriangle& triangle = geometry.triangles[tri];
    Vec3 n = vnorm(vcross(vsub(triangle.b, triangle.a), vsub(triangle.c, triangle.a)));
    r.backface = vdot(n, dir) > 0.0f;
    if (r.backface) {
        n = vscale(n, -1.0f);
    }
    r.hit.distance = t;
    r.hit.normal = n;
    if (triangle.material < geometry.materials.size()) {
        r.hit.albedo = geometry.materials[triangle.material].albedo;
        r.hit.emissive = geometry.materials[triangle.material].emissive;
    }
    return r;
}

// Fills rays for one probe and returns how many hit a backface.
int TraceProbe(const Bvh& bvh, const ProbeBakeGeometry& geometry, const Vec3& position, uint32_t rays,
               ProbeRayHit* out, float* closestBackface, Vec3* closestBackfaceDir) {
    int backfaces = 0;
    *closestBackface = std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < rays; ++i) {
        const Vec3 dir = ProbeRayDirection(i, rays);
        const TraceResult r = Trace(bvh, geometry, position, dir);
        out[i] = r.hit;
        if (r.backface) {
            ++backfaces;
            if (r.hit.distance < *closestBackface) {
                *closestBackface = r.hit.distance;
                *closestBackfaceDir = dir;
            }
        }
    }
    return backfaces;
}

template <typename T>
void Put(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
bool Get(std::ifstream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

int16_t PackSnorm(float v) {
    return static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
}

uint8_t PackUnorm(float v) {
    return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

// Octahedral normal encoding.
std::array<float, 2> OctEncode(const Vec3& n) {
    const float l1 = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z);
    float x = n.x / l1, y = n.y / l1;
    if (n.z < 0.0f) {
        const float ox = (1.0f - std::fabs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        const float oy = (1.0f - std::fabs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = ox;
        y = oy;
    }
    return {x, y};
}

Vec3 OctDecode(float x, float y) {
    Vec3 n(x, y, 1.0f - std::fabs(x) - std::fabs(y));
    if (n.z < 0.0f) {
        const float ox = (1.0f - std::fabs(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f);
        const float oy = (1.0f - std::fabs(n.x)) * (n.y >= 0.0f ? 1.0f : -1.0f);
        n.x = ox;
        n.y = oy;
    }
    return vnorm(n);
}

} // namespace

Vec3 ProbeRayDirection(uint32_t i, uint32_t n) {
    const float golden = kPi * (3.0f - std::sqrt(5.0f));
    const float y = 1.0f - (static_cast<float>(i) + 0.5f) * 2.0f / static_cast<float>(n);
    const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float phi = golden * static_cast<float>(i);
    return Vec3(r * std::cos(phi), y, r * std::sin(phi));
}

ProbeBake BakeProbeVolume(const ProbeBakeGeometry& geometry, const Vec3& center,
                          const ProbeVolumeComponent& volume, uint32_t raysPerProbe) {
    ProbeBake bake;
    bake.center = center;
    bake.size = volume.size;
    bake.probeCounts[0] = std::max(1, volume.probeCountX);
    bake.probeCounts[1] = std::max(1, volume.probeCountY);
    bake.probeCounts[2] = std::max(1, volume.probeCountZ);
    bake.raysPerProbe = raysPerProbe;
    const size_t probes = static_cast<size_t>(bake.probeCounts[0]) * bake.probeCounts[1] * bake.probeCounts[2];
    bake.probeOffsets.assign(probes, Vec3());
    bake.hits.assign(probes * raysPerProbe, ProbeRayHit{});
    if (raysPerProbe == 0) {
        return bake;
    }

    float spacing = std::numeric_limits<float>::max();
    const float sizes[3] = {volume.size.x, volume.size.y, volume.size.z};
    for (int a = 0; a < 3; ++a) {
        if (bake.probeCounts[a] > 1) {
            spacing = std::min(spacing, sizes[a] / static_cast<float>(bake.probeCounts[a] - 1));
        }
    }
    // Relocated probes stay within their own cell so neighbouring probes never swap places.
    const float maxOffset = spacing == std::numeric_limits<float>::max() ? 0.0f : 0.45f * spacing;

    const Bvh bvh(geometry.triangles);
    auto bakeProbe = [&](size_t index) {
        const int x = static_cast<int>(index % bake.probeCounts[0]);
        const int y = static_cast<int>((index / bake.probeCounts[0]) % bake.probeCounts[1]);
        const int z = static_cast<int>(index / (static_cast<size_t>(bake.probeCounts[0]) * bake.probeCounts[1]));
        const Vec3 gridPos = vadd(center, volume.ProbeOffset(x, y, z));
        ProbeRayHit* out = &bake.hits[index * raysPerProbe];
        float closest = 0.0f;
        Vec3 closestDir;
        const int backfaces = TraceProbe(bvh, geometry, gridPos, raysPerProbe, out, &closest, &closestDir);
        if (static_cast<float>(backfaces) <= kBackfaceInsideFraction * static_cast<float>(raysPerProbe) ||
            maxOffset <= 0.0f) {
            return;
        }
        // Step through the nearest backface, just past the surface, if that stays inside the cell.
        const float step = closest + 0.05f * maxOffset;
        if (step > maxOffset) {
            return;
        }
        const Vec3 offset = vscale(closestDir, step);
        std::vector<ProbeRayHit> moved(raysPerProbe);
        Vec3 unusedDir;
        const int movedBackfaces =
            TraceProbe(bvh, geometry, vadd(gridPos, offset), raysPerProbe, moved.data(), &closest, &unusedDir);
        if (movedBackfaces < backfaces) {
            bake.probeOffsets[index] = offset;
            std::copy(moved.begin(), moved.end(), out);
        }
    };

    const unsigned workers = std::max(1u, std::min(std::thread::hardware_concurrency(), 32u));
    std::vector<std::thread> threads;
    for (unsigned w = 0; w < workers; ++w) {
        threads.emplace_back([&, w]() {
            for (size_t i = w; i < probes; i += workers) {
                bakeProbe(i);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    return bake;
}

bool ProbeBakeMatchesVolume(const ProbeBake& bake, const Vec3& center, const ProbeVolumeComponent& volume) {
    constexpr float kTolerance = 1.0e-4f;
    return bake.probeCounts[0] == std::max(1, volume.probeCountX) &&
           bake.probeCounts[1] == std::max(1, volume.probeCountY) &&
           bake.probeCounts[2] == std::max(1, volume.probeCountZ) &&
           vlen(vsub(bake.size, volume.size)) <= kTolerance && vlen(vsub(bake.center, center)) <= kTolerance;
}

bool WriteProbeBake(const std::filesystem::path& path, const ProbeBake& bake, std::string& outError) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        outError = "Cannot write " + path.generic_string();
        return false;
    }
    out.write(kMagic, 4);
    Put(out, kVersion);
    Put(out, bake.center);
    Put(out, bake.size);
    for (int c : bake.probeCounts) Put(out, static_cast<int32_t>(c));
    Put(out, bake.raysPerProbe);
    for (const Vec3& offset : bake.probeOffsets) Put(out, offset);
    for (const ProbeRayHit& hit : bake.hits) {
        Put(out, hit.distance);
        const auto oct = OctEncode(hit.distance >= 0.0f ? hit.normal : Vec3(0.0f, 1.0f, 0.0f));
        Put(out, PackSnorm(oct[0]));
        Put(out, PackSnorm(oct[1]));
        const uint8_t albedo[4] = {PackUnorm(hit.albedo.x), PackUnorm(hit.albedo.y), PackUnorm(hit.albedo.z), 0};
        out.write(reinterpret_cast<const char*>(albedo), 4);
        Put(out, hit.emissive);
    }
    if (!out) {
        outError = "Failed while writing " + path.generic_string();
        return false;
    }
    return true;
}

namespace {

bool ReadHeader(std::ifstream& in, const std::filesystem::path& path, ProbeBake& result, std::string& outError) {
    if (!in) {
        outError = "Cannot open " + path.generic_string();
        return false;
    }
    char magic[4];
    uint32_t version = 0;
    if (!in.read(magic, 4) || std::memcmp(magic, kMagic, 4) != 0 || !Get(in, version) || version != kVersion) {
        outError = path.generic_string() + " is not a version " + std::to_string(kVersion) + " probe bake";
        return false;
    }
    int32_t counts[3];
    if (!Get(in, result.center) || !Get(in, result.size) || !Get(in, counts[0]) || !Get(in, counts[1]) ||
        !Get(in, counts[2]) || !Get(in, result.raysPerProbe)) {
        outError = path.generic_string() + " has a truncated header";
        return false;
    }
    for (int a = 0; a < 3; ++a) {
        if (counts[a] < 1 || counts[a] > 1024) {
            outError = path.generic_string() + " has an invalid probe count";
            return false;
        }
        result.probeCounts[a] = counts[a];
    }
    if (result.raysPerProbe == 0 || result.raysPerProbe > 4096) {
        outError = path.generic_string() + " has an invalid ray count";
        return false;
    }
    return true;
}

} // namespace

bool ReadProbeBakeHeader(const std::filesystem::path& path, ProbeBake& header, std::string& outError) {
    std::ifstream in(path, std::ios::binary);
    ProbeBake result;
    if (!ReadHeader(in, path, result, outError)) {
        return false;
    }
    header = std::move(result);
    return true;
}

bool ReadProbeBake(const std::filesystem::path& path, ProbeBake& bake, std::string& outError) {
    std::ifstream in(path, std::ios::binary);
    ProbeBake result;
    if (!ReadHeader(in, path, result, outError)) {
        return false;
    }
    const size_t probes = static_cast<size_t>(result.probeCounts[0]) * result.probeCounts[1] * result.probeCounts[2];
    result.probeOffsets.resize(probes);
    for (Vec3& offset : result.probeOffsets) {
        if (!Get(in, offset)) {
            outError = path.generic_string() + " is truncated";
            return false;
        }
    }
    result.hits.resize(probes * result.raysPerProbe);
    for (ProbeRayHit& hit : result.hits) {
        int16_t ox = 0, oy = 0;
        uint8_t albedo[4];
        if (!Get(in, hit.distance) || !Get(in, ox) || !Get(in, oy) ||
            !in.read(reinterpret_cast<char*>(albedo), 4) || !Get(in, hit.emissive)) {
            outError = path.generic_string() + " is truncated";
            return false;
        }
        hit.normal = OctDecode(ox / 32767.0f, oy / 32767.0f);
        hit.albedo = Vec3(albedo[0] / 255.0f, albedo[1] / 255.0f, albedo[2] / 255.0f);
    }
    bake = std::move(result);
    return true;
}

} // namespace Hephaestus
