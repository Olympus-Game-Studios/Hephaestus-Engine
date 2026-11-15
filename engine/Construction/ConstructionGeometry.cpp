#include "ConstructionGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <tuple>

namespace Hephaestus::Construction {

namespace {

constexpr float kSplitEpsilon = 1e-4f;

struct Prism {
    float min[3];
    float max[3];
};

void AddSplit(std::vector<float>& splits, float value, float lo, float hi)
{
    splits.push_back(std::clamp(value, lo, hi));
}

void Unique(std::vector<float>& splits)
{
    std::sort(splits.begin(), splits.end());
    std::vector<float> out;
    for (float value : splits) {
        if (out.empty() || value - out.back() > kSplitEpsilon) {
            out.push_back(value);
        }
    }
    splits = std::move(out);
}

Prism OpeningPrism(const BoxParams& box, const OpeningParams& opening)
{
    const WallFrame wall = GetWallFrame(box, opening.side);
    const float u0 = opening.offset - opening.width * 0.5f;
    const float u1 = opening.offset + opening.width * 0.5f;
    const float hw = box.width * 0.5f;
    const float hd = box.depth * 0.5f;
    Prism prism{};
    prism.min[1] = opening.sill;
    prism.max[1] = opening.sill + opening.height;
    switch (opening.side) {
    case WallSide::PosZ:
    case WallSide::NegZ:
        prism.min[0] = u0;
        prism.max[0] = u1;
        prism.min[2] = opening.side == WallSide::PosZ ? hd - wall.thickness : -hd;
        prism.max[2] = opening.side == WallSide::PosZ ? hd : -hd + wall.thickness;
        break;
    case WallSide::PosX:
    case WallSide::NegX:
        prism.min[2] = u0;
        prism.max[2] = u1;
        prism.min[0] = opening.side == WallSide::PosX ? hw - wall.thickness : -hw;
        prism.max[0] = opening.side == WallSide::PosX ? hw : -hw + wall.thickness;
        break;
    }
    return prism;
}

bool Inside(const Prism& prism, const float c[3])
{
    for (int a = 0; a < 3; ++a) {
        if (c[a] <= prism.min[a] || c[a] >= prism.max[a]) {
            return false;
        }
    }
    return true;
}

class CellGrid {
public:
    // A grid whose solid cells are exactly the union of the given boxes (stairs and other stacked shapes).
    explicit CellGrid(const std::vector<Prism>& solids)
    {
        m_solidPrisms = solids;
        for (const Prism& prism : solids) {
            for (int a = 0; a < 3; ++a) {
                m_splits[a].push_back(prism.min[a]);
                m_splits[a].push_back(prism.max[a]);
            }
        }
        Finish();
    }

    CellGrid(const BoxParams& box, const std::vector<OpeningParams>& openings, const std::vector<LocalBox>& cuts)
    {
        const float hw = box.width * 0.5f;
        const float hd = box.depth * 0.5f;
        const float t = box.wallThickness;
        const float lo[3] = {-hw, 0.0f, -hd};
        const float hi[3] = {hw, box.height, hd};
        for (int a = 0; a < 3; ++a) {
            m_splits[a] = {lo[a], hi[a]};
        }
        m_hollow = box.hollow;
        if (m_hollow) {
            m_interior.min[0] = -hw + t;
            m_interior.max[0] = hw - t;
            m_interior.min[2] = -hd + t;
            m_interior.max[2] = hd - t;
            m_interior.min[1] = box.floor ? t : -1.0f;
            m_interior.max[1] = box.ceiling ? box.height - t : box.height + 1.0f;
            for (int a = 0; a < 3; ++a) {
                AddSplit(m_splits[a], m_interior.min[a], lo[a], hi[a]);
                AddSplit(m_splits[a], m_interior.max[a], lo[a], hi[a]);
            }
        }
        std::vector<Prism> holes;
        for (const OpeningParams& opening : openings) {
            holes.push_back(OpeningPrism(box, opening));
        }
        for (const LocalBox& cut : cuts) {
            holes.push_back(Prism{{cut.min.x, cut.min.y, cut.min.z}, {cut.max.x, cut.max.y, cut.max.z}});
        }
        for (Prism prism : holes) {
            bool empty = false;
            for (int a = 0; a < 3; ++a) {
                prism.min[a] = std::clamp(prism.min[a], lo[a], hi[a]);
                prism.max[a] = std::clamp(prism.max[a], lo[a], hi[a]);
                empty = empty || prism.max[a] - prism.min[a] <= kSplitEpsilon;
            }
            if (empty) {
                continue;
            }
            m_prisms.push_back(prism);
            for (int a = 0; a < 3; ++a) {
                AddSplit(m_splits[a], prism.min[a], lo[a], hi[a]);
                AddSplit(m_splits[a], prism.max[a], lo[a], hi[a]);
            }
        }
        Finish();
    }

    int Count(int axis) const { return m_count[axis]; }
    float Split(int axis, int index) const { return m_splits[axis][index]; }

    bool Solid(int i, int j, int k) const
    {
        if (i < 0 || j < 0 || k < 0 || i >= m_count[0] || j >= m_count[1] || k >= m_count[2]) {
            return false;
        }
        return m_solid[Index(i, j, k)] != 0;
    }

    bool InInterior(int i, int j, int k) const
    {
        if (!m_hollow || i < 0 || j < 0 || k < 0 || i >= m_count[0] || j >= m_count[1] || k >= m_count[2]) {
            return false;
        }
        float c[3];
        Center(i, j, k, c);
        return Inside(m_interior, c) && !InAnyPrism(c);
    }

    void Center(int i, int j, int k, float c[3]) const
    {
        const int idx[3] = {i, j, k};
        for (int a = 0; a < 3; ++a) {
            c[a] = 0.5f * (m_splits[a][idx[a]] + m_splits[a][idx[a] + 1]);
        }
    }

private:
    void Finish()
    {
        for (int a = 0; a < 3; ++a) {
            Unique(m_splits[a]);
            m_count[a] = std::max(0, static_cast<int>(m_splits[a].size()) - 1);
        }
        m_solid.resize(static_cast<size_t>(m_count[0]) * m_count[1] * m_count[2]);
        for (int k = 0; k < m_count[2]; ++k) {
            for (int j = 0; j < m_count[1]; ++j) {
                for (int i = 0; i < m_count[0]; ++i) {
                    m_solid[Index(i, j, k)] = ComputeSolid(i, j, k) ? 1 : 0;
                }
            }
        }
    }

    size_t Index(int i, int j, int k) const
    {
        return (static_cast<size_t>(k) * m_count[1] + j) * m_count[0] + i;
    }

    bool InAnyPrism(const float c[3]) const
    {
        for (const Prism& prism : m_prisms) {
            if (Inside(prism, c)) {
                return true;
            }
        }
        return false;
    }

    bool ComputeSolid(int i, int j, int k) const
    {
        float c[3];
        Center(i, j, k, c);
        if (!m_solidPrisms.empty()) {
            for (const Prism& prism : m_solidPrisms) {
                if (Inside(prism, c)) {
                    return true;
                }
            }
            return false;
        }
        if (m_hollow && Inside(m_interior, c)) {
            return false;
        }
        return !InAnyPrism(c);
    }

    std::vector<Prism> m_solidPrisms;
    bool m_hollow = false;
    Prism m_interior{};
    std::vector<Prism> m_prisms;
    std::array<std::vector<float>, 3> m_splits;
    int m_count[3] = {0, 0, 0};
    std::vector<std::uint8_t> m_solid;
};

FaceGroup OuterGroup(int axis, int dir)
{
    if (axis == 1) {
        return dir > 0 ? FaceGroup::Top : FaceGroup::Bottom;
    }
    if (axis == 0) {
        return dir > 0 ? FaceGroup::PosX : FaceGroup::NegX;
    }
    return dir > 0 ? FaceGroup::PosZ : FaceGroup::NegZ;
}

std::int8_t WallFor(FaceGroup group, int axis, int dir)
{
    switch (group) {
    case FaceGroup::PosX: return static_cast<std::int8_t>(WallSide::PosX);
    case FaceGroup::NegX: return static_cast<std::int8_t>(WallSide::NegX);
    case FaceGroup::PosZ: return static_cast<std::int8_t>(WallSide::PosZ);
    case FaceGroup::NegZ: return static_cast<std::int8_t>(WallSide::NegZ);
    case FaceGroup::Inner:
        // An inner face looks into the room, so it belongs to the wall on the opposite side of its normal.
        if (axis == 0) return static_cast<std::int8_t>(dir > 0 ? WallSide::NegX : WallSide::PosX);
        if (axis == 2) return static_cast<std::int8_t>(dir > 0 ? WallSide::NegZ : WallSide::PosZ);
        return -1;
    default: return -1;
    }
}

void EmitQuad(EvaluatedGeometry& out, int axis, int dir, float plane, float b0, float b1, float c0, float c1,
              FaceTag tag)
{
    const int ab = (axis + 1) % 3;
    const int ac = (axis + 2) % 3;
    float corners[4][3];
    const float bs[4] = {b0, b1, b1, b0};
    const float cs[4] = {c0, c0, c1, c1};
    for (int n = 0; n < 4; ++n) {
        corners[n][axis] = plane;
        corners[n][ab] = bs[n];
        corners[n][ac] = cs[n];
    }
    float normal[3] = {0.0f, 0.0f, 0.0f};
    normal[axis] = static_cast<float>(dir);

    const Vec3 p0(corners[0][0], corners[0][1], corners[0][2]);
    const Vec3 p1(corners[1][0], corners[1][1], corners[1][2]);
    const Vec3 p2(corners[2][0], corners[2][1], corners[2][2]);
    const bool flip = vdot(vcross(vsub(p1, p0), vsub(p2, p0)), Vec3(normal[0], normal[1], normal[2])) < 0.0f;

    const uint32_t base = static_cast<uint32_t>(out.mesh.vertices.size());
    for (int n = 0; n < 4; ++n) {
        Vertex v{};
        std::memcpy(v.pos, corners[n], sizeof(v.pos));
        std::memcpy(v.normal, normal, sizeof(v.normal));
        v.color[0] = v.color[1] = v.color[2] = 1.0f;
        // Metres in the face plane, so textures keep their size however the shape is resized.
        if (axis == 1) {
            v.texCoord[0] = corners[n][0];
            v.texCoord[1] = corners[n][2];
        } else if (axis == 0) {
            v.texCoord[0] = corners[n][2];
            v.texCoord[1] = corners[n][1];
        } else {
            v.texCoord[0] = corners[n][0];
            v.texCoord[1] = corners[n][1];
        }
        out.mesh.vertices.push_back(v);
    }
    if (flip) {
        out.mesh.indices.insert(out.mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    } else {
        out.mesh.indices.insert(out.mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    out.triangleTags.push_back(tag);
    out.triangleTags.push_back(tag);
}

void EmitFaces(const CellGrid& grid, EvaluatedGeometry& out)
{
    for (int axis = 0; axis < 3; ++axis) {
        const int ab = (axis + 1) % 3;
        const int ac = (axis + 2) % 3;
        const int nb = grid.Count(ab);
        const int nc = grid.Count(ac);
        std::vector<int> mask(static_cast<size_t>(nb) * nc);
        for (int dir : {1, -1}) {
            for (int layer = 0; layer < grid.Count(axis); ++layer) {
                for (int p = 0; p < nb; ++p) {
                    for (int q = 0; q < nc; ++q) {
                        int cell[3];
                        cell[axis] = layer;
                        cell[ab] = p;
                        cell[ac] = q;
                        int next[3] = {cell[0], cell[1], cell[2]};
                        next[axis] += dir;
                        int value = -1;
                        if (grid.Solid(cell[0], cell[1], cell[2]) && !grid.Solid(next[0], next[1], next[2])) {
                            const bool outside = next[axis] < 0 || next[axis] >= grid.Count(axis);
                            const FaceGroup group = outside ? OuterGroup(axis, dir)
                                : grid.InInterior(next[0], next[1], next[2]) ? FaceGroup::Inner
                                : FaceGroup::Reveal;
                            value = static_cast<int>(group);
                        }
                        mask[static_cast<size_t>(p) * nc + q] = value;
                    }
                }
                const float plane = grid.Split(axis, dir > 0 ? layer + 1 : layer);
                for (int p = 0; p < nb; ++p) {
                    for (int q = 0; q < nc;) {
                        const int value = mask[static_cast<size_t>(p) * nc + q];
                        if (value < 0) {
                            ++q;
                            continue;
                        }
                        int q1 = q + 1;
                        while (q1 < nc && mask[static_cast<size_t>(p) * nc + q1] == value) {
                            ++q1;
                        }
                        int p1 = p + 1;
                        for (; p1 < nb; ++p1) {
                            bool rowMatches = true;
                            for (int s = q; s < q1; ++s) {
                                if (mask[static_cast<size_t>(p1) * nc + s] != value) {
                                    rowMatches = false;
                                    break;
                                }
                            }
                            if (!rowMatches) {
                                break;
                            }
                        }
                        for (int r = p; r < p1; ++r) {
                            for (int s = q; s < q1; ++s) {
                                mask[static_cast<size_t>(r) * nc + s] = -1;
                            }
                        }
                        const FaceGroup group = static_cast<FaceGroup>(value);
                        EmitQuad(out, axis, dir, plane, grid.Split(ab, p), grid.Split(ab, p1), grid.Split(ac, q),
                                 grid.Split(ac, q1), FaceTag{group, WallFor(group, axis, dir)});
                        q = q1;
                    }
                }
            }
        }
    }
}

void EmitSolids(const CellGrid& grid, EvaluatedGeometry& out)
{
    const int nx = grid.Count(0);
    const int ny = grid.Count(1);
    const int nz = grid.Count(2);
    std::vector<std::uint8_t> used(static_cast<size_t>(nx) * ny * nz, 0);
    auto index = [&](int i, int j, int k) { return (static_cast<size_t>(k) * ny + j) * nx + i; };
    auto free = [&](int i, int j, int k) { return grid.Solid(i, j, k) && !used[index(i, j, k)]; };
    for (int j = 0; j < ny; ++j) {
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i < nx; ++i) {
                if (!free(i, j, k)) {
                    continue;
                }
                int i1 = i + 1;
                while (i1 < nx && free(i1, j, k)) {
                    ++i1;
                }
                int k1 = k + 1;
                for (; k1 < nz; ++k1) {
                    bool ok = true;
                    for (int x = i; x < i1 && ok; ++x) ok = free(x, j, k1);
                    if (!ok) break;
                }
                int j1 = j + 1;
                for (; j1 < ny; ++j1) {
                    bool ok = true;
                    for (int z = k; z < k1 && ok; ++z) {
                        for (int x = i; x < i1 && ok; ++x) ok = free(x, j1, z);
                    }
                    if (!ok) break;
                }
                for (int y = j; y < j1; ++y) {
                    for (int z = k; z < k1; ++z) {
                        for (int x = i; x < i1; ++x) used[index(x, y, z)] = 1;
                    }
                }
                out.solids.push_back(LocalBox{Vec3(grid.Split(0, i), grid.Split(1, j), grid.Split(2, k)),
                                              Vec3(grid.Split(0, i1), grid.Split(1, j1), grid.Split(2, k1))});
            }
        }
    }
}

struct Hasher {
    std::uint64_t value = 1469598103934665603ull;
    void Bytes(const void* data, size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t n = 0; n < size; ++n) {
            value ^= bytes[n];
            value *= 1099511628211ull;
        }
    }
    void Float(float f)
    {
        if (f == 0.0f) f = 0.0f;  // +0 and -0 hash the same
        Bytes(&f, sizeof(f));
    }
    void Int(std::int64_t i) { Bytes(&i, sizeof(i)); }
};

} // namespace

WallFrame GetWallFrame(const BoxParams& box, WallSide side)
{
    WallFrame wall;
    const float hw = box.width * 0.5f;
    const float hd = box.depth * 0.5f;
    wall.height = box.height;
    switch (side) {
    case WallSide::PosZ:
    case WallSide::NegZ:
        wall.center = Vec3(0.0f, 0.0f, side == WallSide::PosZ ? hd : -hd);
        wall.u = Vec3(1.0f, 0.0f, 0.0f);
        wall.normal = Vec3(0.0f, 0.0f, side == WallSide::PosZ ? 1.0f : -1.0f);
        wall.halfLength = hw;
        wall.thickness = box.hollow ? box.wallThickness : box.depth;
        break;
    case WallSide::PosX:
    case WallSide::NegX:
        wall.center = Vec3(side == WallSide::PosX ? hw : -hw, 0.0f, 0.0f);
        wall.u = Vec3(0.0f, 0.0f, 1.0f);
        wall.normal = Vec3(side == WallSide::PosX ? 1.0f : -1.0f, 0.0f, 0.0f);
        wall.halfLength = hd;
        wall.thickness = box.hollow ? box.wallThickness : box.width;
        break;
    }
    return wall;
}

void SortOpenings(std::vector<OpeningParams>& openings)
{
    std::sort(openings.begin(), openings.end(), [](const OpeningParams& a, const OpeningParams& b) {
        return std::make_tuple(static_cast<int>(a.side), a.offset, a.sill, a.width, a.height) <
               std::make_tuple(static_cast<int>(b.side), b.offset, b.sill, b.width, b.height);
    });
}

bool EvaluateBox(const BoxParams& box, std::vector<OpeningParams> openings, EvaluatedGeometry& out,
                 const std::vector<LocalBox>& cuts)
{
    out = EvaluatedGeometry{};
    if (!(box.width > 0.0f) || !(box.height > 0.0f) || !(box.depth > 0.0f)) {
        return false;
    }
    SortOpenings(openings);
    const CellGrid grid(box, openings, cuts);
    EmitFaces(grid, out);
    EmitSolids(grid, out);
    out.mesh.bounds = AABB(Vec3(-box.width * 0.5f, 0.0f, -box.depth * 0.5f),
                           Vec3(box.width * 0.5f, box.height, box.depth * 0.5f));
    MeshData::Submesh all;
    all.indexOffset = 0;
    all.indexCount = static_cast<uint32_t>(out.mesh.indices.size());
    all.materialSlot = 0;
    all.name = "Construction";
    all.bounds = out.mesh.bounds;
    out.mesh.submeshes.push_back(all);
    return !out.mesh.indices.empty();
}

bool EvaluateStairs(const BoxParams& size, int steps, EvaluatedGeometry& out)
{
    out = EvaluatedGeometry{};
    if (!(size.width > 0.0f) || !(size.height > 0.0f) || !(size.depth > 0.0f) || steps < 1) {
        return false;
    }
    const float hw = size.width * 0.5f;
    const float hd = size.depth * 0.5f;
    const float run = size.depth / static_cast<float>(steps);
    const float rise = size.height / static_cast<float>(steps);
    std::vector<Prism> columns;
    for (int step = 0; step < steps; ++step) {
        Prism column{};
        column.min[0] = -hw;
        column.max[0] = hw;
        column.min[1] = 0.0f;
        column.max[1] = rise * static_cast<float>(step + 1);
        column.min[2] = -hd + run * static_cast<float>(step);
        column.max[2] = step + 1 == steps ? hd : -hd + run * static_cast<float>(step + 1);
        columns.push_back(column);
    }
    const CellGrid grid(columns);
    EmitFaces(grid, out);
    EmitSolids(grid, out);
    out.mesh.bounds = AABB(Vec3(-hw, 0.0f, -hd), Vec3(hw, size.height, hd));
    MeshData::Submesh all;
    all.indexCount = static_cast<uint32_t>(out.mesh.indices.size());
    all.name = "Construction";
    all.bounds = out.mesh.bounds;
    out.mesh.submeshes.push_back(all);
    return !out.mesh.indices.empty();
}

namespace {

void PushVertex(MeshData& mesh, const Vec3& p, const Vec3& n, float u, float v)
{
    Vertex vertex{};
    vertex.pos[0] = p.x;
    vertex.pos[1] = p.y;
    vertex.pos[2] = p.z;
    vertex.normal[0] = n.x;
    vertex.normal[1] = n.y;
    vertex.normal[2] = n.z;
    vertex.color[0] = vertex.color[1] = vertex.color[2] = 1.0f;
    vertex.texCoord[0] = u;
    vertex.texCoord[1] = v;
    mesh.vertices.push_back(vertex);
}

void Finish(EvaluatedGeometry& out, const AABB& bounds)
{
    out.mesh.bounds = bounds;
    MeshData::Submesh all;
    all.indexCount = static_cast<uint32_t>(out.mesh.indices.size());
    all.name = "Construction";
    all.bounds = bounds;
    out.mesh.submeshes.push_back(all);
    out.triangleTags.assign(out.mesh.indices.size() / 3, FaceTag{FaceGroup::Reveal, -1});
}

// Counter-clockwise seen from outside, matching the box faces.
void PushTriangle(MeshData& mesh, uint32_t a, uint32_t b, uint32_t c)
{
    mesh.indices.insert(mesh.indices.end(), {a, b, c});
}

} // namespace

bool EvaluatePlane(const BoxParams& size, EvaluatedGeometry& out)
{
    out = EvaluatedGeometry{};
    const float hw = size.width * 0.5f;
    const float hd = size.depth * 0.5f;
    const Vec3 up(0.0f, 1.0f, 0.0f);
    PushVertex(out.mesh, Vec3(-hw, 0.0f, hd), up, -hw, hd);
    PushVertex(out.mesh, Vec3(hw, 0.0f, hd), up, hw, hd);
    PushVertex(out.mesh, Vec3(hw, 0.0f, -hd), up, hw, -hd);
    PushVertex(out.mesh, Vec3(-hw, 0.0f, -hd), up, -hw, -hd);
    PushTriangle(out.mesh, 0, 1, 2);
    PushTriangle(out.mesh, 0, 2, 3);
    // A thin slab under the surface, so the plane can be walked on.
    out.solids.push_back(LocalBox{Vec3(-hw, -0.02f, -hd), Vec3(hw, 0.0f, hd)});
    Finish(out, AABB(Vec3(-hw, 0.0f, -hd), Vec3(hw, 0.0f, hd)));
    return true;
}

bool EvaluateCylinder(const BoxParams& size, int segments, EvaluatedGeometry& out)
{
    out = EvaluatedGeometry{};
    const float rx = size.width * 0.5f;
    const float rz = size.depth * 0.5f;
    const float h = size.height;
    const float pi = 3.14159265f;
    MeshData& mesh = out.mesh;
    for (int i = 0; i <= segments; ++i) {
        const float a = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
        const Vec3 n = vnorm(Vec3(std::cos(a) / std::max(rx, 1e-4f), 0.0f, std::sin(a) / std::max(rz, 1e-4f)));
        const float u = a * 0.5f * (rx + rz);
        PushVertex(mesh, Vec3(rx * std::cos(a), 0.0f, rz * std::sin(a)), n, u, 0.0f);
        PushVertex(mesh, Vec3(rx * std::cos(a), h, rz * std::sin(a)), n, u, h);
    }
    for (int i = 0; i < segments; ++i) {
        const uint32_t b0 = static_cast<uint32_t>(i * 2);
        PushTriangle(mesh, b0, b0 + 1, b0 + 3);
        PushTriangle(mesh, b0, b0 + 3, b0 + 2);
    }
    for (int cap = 0; cap < 2; ++cap) {
        const float y = cap == 0 ? 0.0f : h;
        const Vec3 n(0.0f, cap == 0 ? -1.0f : 1.0f, 0.0f);
        const uint32_t center = static_cast<uint32_t>(mesh.vertices.size());
        PushVertex(mesh, Vec3(0.0f, y, 0.0f), n, 0.0f, 0.0f);
        for (int i = 0; i <= segments; ++i) {
            const float a = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            PushVertex(mesh, Vec3(rx * std::cos(a), y, rz * std::sin(a)), n, rx * std::cos(a), rz * std::sin(a));
        }
        for (int i = 0; i < segments; ++i) {
            const uint32_t a = center + 1 + static_cast<uint32_t>(i);
            if (cap == 0) PushTriangle(mesh, center, a, a + 1);
            else PushTriangle(mesh, center, a + 1, a);
        }
    }
    RoundCollider collider;
    collider.kind = RoundCollider::Kind::Cylinder;
    collider.center = Vec3(0.0f, h * 0.5f, 0.0f);
    collider.radius = std::max(rx, rz);
    collider.height = h;
    out.rounds.push_back(collider);
    Finish(out, AABB(Vec3(-rx, 0.0f, -rz), Vec3(rx, h, rz)));
    return true;
}

bool EvaluateSphere(const BoxParams& size, int segments, EvaluatedGeometry& out)
{
    out = EvaluatedGeometry{};
    const float rx = size.width * 0.5f;
    const float ry = size.height * 0.5f;
    const float rz = size.depth * 0.5f;
    const float pi = 3.14159265f;
    const int rings = std::max(2, segments / 2);
    MeshData& mesh = out.mesh;
    for (int r = 0; r <= rings; ++r) {
        const float phi = pi * static_cast<float>(r) / static_cast<float>(rings);
        for (int s = 0; s <= segments; ++s) {
            const float theta = 2.0f * pi * static_cast<float>(s) / static_cast<float>(segments);
            const Vec3 unit(std::sin(phi) * std::cos(theta), -std::cos(phi), std::sin(phi) * std::sin(theta));
            const Vec3 n = vnorm(Vec3(unit.x / std::max(rx, 1e-4f), unit.y / std::max(ry, 1e-4f), unit.z / std::max(rz, 1e-4f)));
            PushVertex(mesh, Vec3(unit.x * rx, ry + unit.y * ry, unit.z * rz), n,
                       theta * 0.5f * (rx + rz), phi * ry);
        }
    }
    const uint32_t row = static_cast<uint32_t>(segments + 1);
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const uint32_t a = static_cast<uint32_t>(r) * row + static_cast<uint32_t>(s);
            const uint32_t b = a + row;
            if (r < rings - 1) PushTriangle(mesh, a, b, b + 1);  // the top ring meets at the pole
            if (r > 0) PushTriangle(mesh, a, b + 1, a + 1);
        }
    }
    RoundCollider collider;
    collider.kind = RoundCollider::Kind::Sphere;
    collider.center = Vec3(0.0f, ry, 0.0f);
    collider.radius = std::max({rx, ry, rz});
    out.rounds.push_back(collider);
    Finish(out, AABB(Vec3(-rx, 0.0f, -rz), Vec3(rx, 2.0f * ry, rz)));
    return true;
}

bool EvaluateShape(const ConstructionShapeComponent& shape, std::vector<OpeningParams> openings,
                   EvaluatedGeometry& out, const std::vector<LocalBox>& cuts)
{
    if (shape.recipe == Recipe::Plane) {
        return EvaluatePlane(shape.box, out);
    }
    if (shape.recipe == Recipe::Cylinder) {
        return EvaluateCylinder(shape.box, shape.segments, out);
    }
    if (shape.recipe == Recipe::Sphere) {
        return EvaluateSphere(shape.box, shape.segments, out);
    }
    if (shape.recipe == Recipe::Stairs) {
        return EvaluateStairs(shape.box, StairCount(shape), out);
    }
    if (IsCutter(shape)) {
        BoxParams solid = shape.box;
        solid.hollow = false;
        return EvaluateBox(solid, {}, out);
    }
    return EvaluateBox(shape.box, std::move(openings), out, cuts);
}

std::uint64_t HashInputs(const ConstructionShapeComponent& shape, std::vector<OpeningParams> openings,
                         const std::vector<LocalBox>& cuts)
{
    Hasher h;
    h.Int(shape.evaluatorVersion);
    h.Int(static_cast<int>(shape.source));
    h.Int(static_cast<int>(shape.recipe));
    h.Int(static_cast<int>(shape.collision));
    h.Int(static_cast<int>(shape.operation));
    const BoxParams& b = shape.box;
    h.Float(b.width);
    h.Float(b.height);
    h.Float(b.depth);
    h.Int(b.hollow);
    h.Float(b.wallThickness);
    h.Int(b.floor);
    h.Int(b.ceiling);
    h.Int(shape.stairs.steps);
    h.Int(shape.segments);
    h.Float(shape.stairs.maxRise);
    SortOpenings(openings);
    h.Int(static_cast<std::int64_t>(openings.size()));
    for (const OpeningParams& o : openings) {
        h.Int(static_cast<int>(o.side));
        h.Float(o.offset);
        h.Float(o.sill);
        h.Float(o.width);
        h.Float(o.height);
    }
    h.Int(static_cast<std::int64_t>(cuts.size()));
    for (const LocalBox& cut : cuts) {
        h.Float(cut.min.x); h.Float(cut.min.y); h.Float(cut.min.z);
        h.Float(cut.max.x); h.Float(cut.max.y); h.Float(cut.max.z);
    }
    return h.value;
}

std::uint64_t HashGeometry(const EvaluatedGeometry& geometry)
{
    Hasher h;
    for (const Vertex& v : geometry.mesh.vertices) {
        for (float f : v.pos) h.Float(f);
        for (float f : v.normal) h.Float(f);
        for (float f : v.texCoord) h.Float(f);
    }
    h.Bytes(geometry.mesh.indices.data(), geometry.mesh.indices.size() * sizeof(uint32_t));
    for (const LocalBox& box : geometry.solids) {
        h.Float(box.min.x); h.Float(box.min.y); h.Float(box.min.z);
        h.Float(box.max.x); h.Float(box.max.y); h.Float(box.max.z);
    }
    return h.value;
}

} // namespace Hephaestus::Construction
