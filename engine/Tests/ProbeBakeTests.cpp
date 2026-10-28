#include "Gfx/ProbeBake.h"
#include "Scene/Components.h"

#include <cmath>
#include <filesystem>
#include <iostream>

using namespace Hephaestus;

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

bool Near(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

// Axis-aligned box of half-extent h at c. Inward faces make a room; outward faces make a solid block.
void AddBox(ProbeBakeGeometry& g, Vec3 c, float h, bool inward, uint32_t material) {
    const Vec3 p[8] = {
        v3(c.x - h, c.y - h, c.z - h), v3(c.x + h, c.y - h, c.z - h), v3(c.x + h, c.y + h, c.z - h),
        v3(c.x - h, c.y + h, c.z - h), v3(c.x - h, c.y - h, c.z + h), v3(c.x + h, c.y - h, c.z + h),
        v3(c.x + h, c.y + h, c.z + h), v3(c.x - h, c.y + h, c.z + h)};
    // Counter-clockwise seen from outside, so the geometric normal points out.
    const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}};
    for (const auto& f : faces) {
        ProbeBakeTriangle t1{p[f[0]], p[f[1]], p[f[2]], material};
        ProbeBakeTriangle t2{p[f[0]], p[f[2]], p[f[3]], material};
        if (inward) {
            std::swap(t1.b, t1.c);
            std::swap(t2.b, t2.c);
        }
        g.triangles.push_back(t1);
        g.triangles.push_back(t2);
    }
}

} // namespace

int main() {
    bool pass = true;

    {
        Vec3 sum;
        bool unit = true;
        for (uint32_t i = 0; i < kProbeBakeRaysPerProbe; ++i) {
            const Vec3 d = ProbeRayDirection(i, kProbeBakeRaysPerProbe);
            unit &= Near(vlen(d), 1.0f, 1.0e-4f);
            sum = vadd(sum, d);
        }
        pass &= Check(unit, "ray directions are unit length");
        pass &= Check(vlen(sum) / kProbeBakeRaysPerProbe < 0.02f, "ray directions cover the sphere evenly");
    }

    {
        ProbeBakeGeometry room;
        room.materials.push_back({v3(0.8f, 0.2f, 0.1f), v3(0.0f, 0.0f, 2.0f)});
        AddBox(room, v3(0, 0, 0), 5.0f, true, 0);
        ProbeVolumeComponent volume;
        volume.probeCountX = volume.probeCountY = volume.probeCountZ = 1;
        const ProbeBake bake = BakeProbeVolume(room, v3(0, 0, 0), volume);
        pass &= Check(bake.ProbeCount() == 1 && bake.hits.size() == kProbeBakeRaysPerProbe, "one probe, all rays");
        bool distances = true, normals = true, materials = true;
        for (uint32_t i = 0; i < bake.raysPerProbe; ++i) {
            const Vec3 d = ProbeRayDirection(i, bake.raysPerProbe);
            const ProbeRayHit& hit = bake.hits[i];
            const float major = std::max({std::fabs(d.x), std::fabs(d.y), std::fabs(d.z)});
            distances &= Near(hit.distance, 5.0f / major, 1.0e-3f);
            normals &= vdot(hit.normal, d) < -0.99f / std::sqrt(3.0f) && Near(vlen(hit.normal), 1.0f, 1.0e-4f);
            materials &= Near(hit.albedo.x, 0.8f, 1.0e-6f) && Near(hit.emissive.z, 2.0f, 1.0e-6f);
        }
        pass &= Check(distances, "every ray hits the closed room at the analytic distance");
        pass &= Check(normals, "hit normals face back toward the probe");
        pass &= Check(materials, "hits carry the wall material");
        pass &= Check(vlen(bake.probeOffsets[0]) == 0.0f, "a probe in open space is not moved");

        const auto path = std::filesystem::temp_directory_path() / "hephaestus_probe_bake_test.hpprobes";
        std::string error;
        pass &= Check(WriteProbeBake(path, bake, error), "bake file writes");
        ProbeBake loaded;
        pass &= Check(ReadProbeBake(path, loaded, error), "bake file reads back");
        bool same = loaded.ProbeCount() == 1 && loaded.raysPerProbe == bake.raysPerProbe &&
                    loaded.probeCounts[0] == 1 && loaded.hits.size() == bake.hits.size();
        for (size_t i = 0; same && i < bake.hits.size(); ++i) {
            same = Near(loaded.hits[i].distance, bake.hits[i].distance, 0.0f) &&
                   vdot(loaded.hits[i].normal, bake.hits[i].normal) > 0.9999f &&
                   Near(loaded.hits[i].albedo.y, bake.hits[i].albedo.y, 1.0f / 255.0f) &&
                   Near(loaded.hits[i].emissive.z, 2.0f, 0.0f);
        }
        pass &= Check(same, "bake file round trip keeps every ray");
        ProbeBake header;
        pass &= Check(ReadProbeBakeHeader(path, header, error) && header.hits.empty() &&
                          ProbeBakeMatchesVolume(header, v3(0, 0, 0), volume),
                      "the header alone identifies the volume it was baked for");
        ProbeVolumeComponent changed = volume;
        changed.probeCountY = 2;
        pass &= Check(!ProbeBakeMatchesVolume(header, v3(0, 0, 0), changed) &&
                          !ProbeBakeMatchesVolume(header, v3(0, 1, 0), volume),
                      "changing the counts or moving the volume makes the bake out of date");
        std::filesystem::remove(path);

        ProbeBake rejected;
        pass &= Check(!ReadProbeBake(path, rejected, error), "a missing bake file reports an error");
    }

    {
        const ProbeBake sky = BakeProbeVolume(ProbeBakeGeometry{}, v3(0, 0, 0), ProbeVolumeComponent{});
        bool allSky = sky.ProbeCount() == 8 * 4 * 8;
        for (const auto& hit : sky.hits) allSky &= hit.distance < 0.0f;
        pass &= Check(allSky, "with no geometry every ray escapes to the sky");
    }

    {
        // Three probes 5 m apart; the middle one starts inside a 1 m solid block.
        ProbeBakeGeometry block;
        block.materials.push_back({});
        AddBox(block, v3(0, 0, 0), 0.5f, false, 0);
        ProbeVolumeComponent volume;
        volume.size = v3(10.0f, 0.0f, 0.0f);
        volume.probeCountX = 3;
        volume.probeCountY = volume.probeCountZ = 1;
        const ProbeBake bake = BakeProbeVolume(block, v3(0, 0, 0), volume);
        const Vec3 moved = bake.probeOffsets[1];
        const float outside = std::max({std::fabs(moved.x), std::fabs(moved.y), std::fabs(moved.z)});
        pass &= Check(outside > 0.5f && vlen(moved) <= 0.45f * 5.0f, "the buried probe moves out of the block");
        pass &= Check(vlen(bake.probeOffsets[0]) == 0.0f && vlen(bake.probeOffsets[2]) == 0.0f,
                      "probes outside the block stay put");
    }

    if (pass) {
        std::cout << "ProbeBakeTests passed\n";
    }
    return pass ? 0 : 1;
}
