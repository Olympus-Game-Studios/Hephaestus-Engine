#pragma once

#include "../Core/Math.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ProbeVolumeComponent;

namespace Hephaestus {

struct ProbeBakeMaterial {
    Vec3 albedo{1.0f, 1.0f, 1.0f};
    Vec3 emissive{0.0f, 0.0f, 0.0f};
};

struct ProbeBakeTriangle {
    Vec3 a, b, c;
    uint32_t material = 0;
};

// World-space static geometry the probes see.
struct ProbeBakeGeometry {
    std::vector<ProbeBakeTriangle> triangles;
    std::vector<ProbeBakeMaterial> materials;
};

// distance < 0 means the ray escaped to the sky; the other fields are then unused.
struct ProbeRayHit {
    float distance = -1.0f;
    Vec3 normal{0.0f, 0.0f, 0.0f};
    Vec3 albedo{0.0f, 0.0f, 0.0f};
    Vec3 emissive{0.0f, 0.0f, 0.0f};
};

struct ProbeBake {
    Vec3 center{0.0f, 0.0f, 0.0f};
    Vec3 size{0.0f, 0.0f, 0.0f};
    int probeCounts[3] = {0, 0, 0};
    uint32_t raysPerProbe = 0;
    uint64_t geometrySignature = 0;   // 0 for version 1 files, which predate it
    std::vector<Vec3> probeOffsets;   // relocation from the grid position, per probe
    std::vector<ProbeRayHit> hits;    // raysPerProbe entries per probe, probes in x-fastest order

    size_t ProbeCount() const { return probeOffsets.size(); }
};

constexpr uint32_t kProbeBakeRaysPerProbe = 128;

// Hash of the triangles and their materials; it changes whenever the baked geometry would.
uint64_t ProbeBakeGeometrySignature(const ProbeBakeGeometry& geometry);

// Fixed spherical Fibonacci direction i of n; the bake and the GPU relight must agree on it.
Vec3 ProbeRayDirection(uint32_t i, uint32_t n);

ProbeBake BakeProbeVolume(const ProbeBakeGeometry& geometry, const Vec3& center,
                          const ProbeVolumeComponent& volume, uint32_t raysPerProbe = kProbeBakeRaysPerProbe);

// True when the bake was made for this volume at this centre; compare geometrySignature for geometry changes.
bool ProbeBakeMatchesVolume(const ProbeBake& bake, const Vec3& center, const ProbeVolumeComponent& volume);

bool WriteProbeBake(const std::filesystem::path& path, const ProbeBake& bake, std::string& outError);
bool ReadProbeBake(const std::filesystem::path& path, ProbeBake& bake, std::string& outError);
// Reads only centre, size, counts and ray count; probeOffsets and hits stay empty.
bool ReadProbeBakeHeader(const std::filesystem::path& path, ProbeBake& header, std::string& outError);

} // namespace Hephaestus
