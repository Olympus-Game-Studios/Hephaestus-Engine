#include "ConstructionTool.h"
#include "ConstructionGeometry.h"

#include "../Navigation/NavMesh.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneAuthoring.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace Hephaestus::Construction {

namespace {

constexpr int kKeyEscape = 0x01000000;
constexpr int kKeyBackspace = 0x01000003;
constexpr int kKeyReturn = 0x01000004;
constexpr int kKeyEnter = 0x01000005;
constexpr float kHandlePickPixels = 14.0f;
constexpr float kMinDimension = 0.05f;
constexpr float kDefaultDoorWidth = 1.2f;
constexpr float kDefaultDoorHeight = 2.1f;
constexpr float kDefaultWindowHeight = 1.0f;
constexpr float kPi = 3.14159265f;
constexpr float kHalfPi = 1.57079633f;

const Vec3 kPreviewColor(1.0f, 0.62f, 0.2f);
const Vec3 kHandleColor(1.0f, 0.85f, 0.3f);
const Vec3 kWalkColor(0.3f, 0.9f, 0.45f);
const Vec3 kStraightColor(0.55f, 0.55f, 0.6f);
const Vec3 kOpeningColor(0.35f, 0.75f, 1.0f);

bool IntersectPlane(const Ray& ray, const Vec3& point, const Vec3& normal, Vec3& out)
{
    const float denom = vdot(ray.direction, normal);
    if (std::fabs(denom) < 1e-6f) {
        return false;
    }
    const float t = vdot(vsub(point, ray.origin), normal) / denom;
    if (t < 0.0f) {
        return false;
    }
    out = ray.at(t);
    return true;
}

// Parameter along the line o + t*a closest to the ray.
bool ClosestOnLine(const Vec3& o, const Vec3& a, const Ray& ray, float& t)
{
    const Vec3 w0 = vsub(o, ray.origin);
    const float A = vdot(a, a);
    const float B = vdot(a, ray.direction);
    const float C = vdot(ray.direction, ray.direction);
    const float D = vdot(a, w0);
    const float E = vdot(ray.direction, w0);
    const float denom = A * C - B * B;
    if (std::fabs(denom) < 1e-6f) {
        return false;
    }
    t = (B * E - C * D) / denom;
    return true;
}

bool ToScreen(const ToolInput& input, const Vec3& world, float& x, float& y)
{
    const Mat4& m = input.viewProj;
    const float w = m.m[3] * world.x + m.m[7] * world.y + m.m[11] * world.z + m.m[15];
    if (w <= 1e-5f) {
        return false;
    }
    const Vec3 ndc = Mat4::TransformPoint(m, world);
    x = (ndc.x + 1.0f) * 0.5f * input.viewportWidth;
    y = (ndc.y + 1.0f) * 0.5f * input.viewportHeight;
    return true;
}

Vec3 Axis(int axis)
{
    return axis == 0 ? Vec3(1, 0, 0) : axis == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
}

void AddBoxLines(std::vector<EditorDebugLine>& lines, const Mat4& world, const Vec3& mn, const Vec3& mx, const Vec3& color)
{
    Vec3 c[8];
    for (int n = 0; n < 8; ++n) {
        c[n] = Mat4::TransformPoint(world, Vec3(n & 1 ? mx.x : mn.x, n & 2 ? mx.y : mn.y, n & 4 ? mx.z : mn.z));
    }
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) {
        lines.push_back({c[e[0]], c[e[1]], color});
    }
}

void AddRectLines(std::vector<EditorDebugLine>& lines, const Vec3 corners[4], const Vec3& color)
{
    for (int n = 0; n < 4; ++n) {
        lines.push_back({corners[n], corners[(n + 1) % 4], color});
    }
}

// The face of a host an opening sits on: a box side, or a face of a wall segment.
bool HostFrame(Scene& scene, entt::entity host, WallSide side, int segment, WallFrame& out)
{
    entt::registry& registry = scene.GetRegistry();
    if (!registry.valid(host) || !registry.all_of<ConstructionShapeComponent>(host)) {
        return false;
    }
    const auto& shape = registry.get<ConstructionShapeComponent>(host);
    return CanHostOpenings(shape) && GetHostWallFrame(shape, side, segment, out);
}

Vec3 FrameCoordinates(const WallFrame& wall, const Vec3& local)
{
    return Vec3(vdot(vsub(local, wall.center), wall.u), local.y, 0.0f);
}

float HostFloor(Scene& scene, entt::entity host)
{
    return HostFloorTop(scene.GetRegistry().get<ConstructionShapeComponent>(host));
}

const char* PhaseHint(int phase)
{
    switch (phase) {
    case 1: return "Drag the footprint, release, then move up for height. Type width,depth then Enter.";
    case 2: return "Move to set the height and click to finish. Type a height then Enter. Esc cancels.";
    case 3: return "Drag to resize. Shift resizes both sides. Ctrl turns snapping off. Esc cancels.";
    case 4: return "Drag the opening's outline on the wall. Click for a default door or window.";
    case 5: return "Drag to move the opening along the wall. Esc cancels.";
    case 6: return "Click to add corners. Click the first corner to close a room, or the last one again (or Enter) to "
                   "finish. Type a length then Enter. Nearly straight sides snap straight; Shift forces it, Ctrl turns snapping "
                   "off. Corners snap to other walls' corners. Backspace removes a corner.";
    default: return "";
    }
}

} // namespace

Vec3 WallCoordinates(const BoxParams& box, WallSide side, const Vec3& local)
{
    return FrameCoordinates(GetWallFrame(box, side), local);
}

void ConstructionTool::SetMode(ToolMode mode)
{
    m_phase = Phase::Idle;
    m_typed.clear();
    m_hoverValid = false;
    m_mode = mode;
    m_measure = 0;
    m_walkNav.reset();
}

bool ConstructionTool::PressMeasure(Scene& scene, ConstructionSystem& system, const ToolInput& input)
{
    Vec3 point;
    if (!PlacementPoint(scene, system, input, point)) {
        return false;
    }
    if (m_measure == 1) {
        m_measureTo = point;
        m_measure = 2;
    } else {
        m_measureFrom = point;
        m_measureTo = point;
        m_measure = 1;
        m_walkError.clear();
        const std::filesystem::path root = m_host.projectRoot ? m_host.projectRoot() : std::filesystem::path();
        m_walkNav = Analysis::LevelNavMesh(scene, root, m_walkError);
        m_walkMetrics = Analysis::MetricsForScene(scene);
    }
    m_walkRoute = m_walkNav ? Analysis::MeasureWalk(*m_walkNav, m_walkMetrics, m_measureFrom, m_measureTo)
                            : Analysis::WalkRoute{};
    return true;
}

float ConstructionTool::Snap(float value, const ToolInput& input) const
{
    if (input.ctrl || input.gridStep <= 0.0f) {
        return value;
    }
    return std::round(value / input.gridStep) * input.gridStep;
}

bool ConstructionTool::PlacementPoint(Scene& scene, ConstructionSystem& system, const ToolInput& input, Vec3& out) const
{
    ConstructionHit hit;
    if (system.Raycast(scene, input.ray, hit)) {
        out = hit.worldPoint;
    } else if (!IntersectPlane(input.ray, Vec3(0, 0, 0), Vec3(0, 1, 0), out)) {
        return false;
    }
    out.x = Snap(out.x, input);
    out.z = Snap(out.z, input);
    return true;
}

std::vector<ConstructionTool::Handle> ConstructionTool::HandlesFor(const ConstructionShapeComponent& shape) const
{
    const BoxParams& b = shape.box;
    const float hw = b.width * 0.5f;
    const float hd = b.depth * 0.5f;
    if (shape.source == SourceKind::EditableMesh) {
        std::vector<Handle> handles;
        for (size_t n = 0; n < shape.mesh.vertices.size(); ++n) {
            Handle handle{1, 1, shape.mesh.vertices[n], -1};
            handle.vertex = static_cast<int>(n);
            handles.push_back(handle);
        }
        return handles;
    }
    if (shape.recipe == Recipe::Wall) {
        std::vector<Handle> handles;
        for (size_t n = 0; n < shape.wall.points.size(); ++n) {
            handles.push_back({1, 1, Vec3(shape.wall.points[n].x, 0.0f, shape.wall.points[n].z), static_cast<int>(n)});
        }
        WallSegmentFrame first;
        if (GetWallSegmentFrame(shape, 0, first)) {
            handles.push_back({1, 1, vadd(first.mid, Vec3(0.0f, b.height, 0.0f)), -1});
        }
        return handles;
    }
    if (shape.recipe == Recipe::Plane) {
        return {{0, 1, Vec3(hw, 0.0f, 0.0f)}, {0, -1, Vec3(-hw, 0.0f, 0.0f)},
                {2, 1, Vec3(0.0f, 0.0f, hd)}, {2, -1, Vec3(0.0f, 0.0f, -hd)}};
    }
    const float mid = b.height * 0.5f;
    return {
        {0, 1, Vec3(hw, mid, 0.0f)}, {0, -1, Vec3(-hw, mid, 0.0f)},
        {2, 1, Vec3(0.0f, mid, hd)}, {2, -1, Vec3(0.0f, mid, -hd)},
        // A ramp's height handle sits on its high edge.
        {1, 1, Vec3(0.0f, b.height, shape.recipe == Recipe::Ramp ? hd : 0.0f)},
    };
}

bool ConstructionTool::PickHandle(Scene& scene, const ToolInput& input, entt::entity& outEntity, Handle& outHandle) const
{
    const entt::entity selected = m_host.selected ? m_host.selected() : entt::null;
    if (selected == entt::null || !scene.GetRegistry().valid(selected) ||
        !scene.GetRegistry().all_of<ConstructionShapeComponent>(selected) ||
        !HasOwnGeometry(scene.GetRegistry().get<ConstructionShapeComponent>(selected))) {
        return false;
    }
    const Mat4 world = scene.GetWorldTransform(Entity(selected, &scene));
    float best = kHandlePickPixels;
    bool found = false;
    for (const Handle& handle : HandlesFor(scene.GetRegistry().get<ConstructionShapeComponent>(selected))) {
        float x = 0.0f;
        float y = 0.0f;
        if (!ToScreen(input, Mat4::TransformPoint(world, handle.local), x, y)) {
            continue;
        }
        const float d = std::hypot(x - input.screenX, y - input.screenY);
        if (d < best) {
            best = d;
            outHandle = handle;
            found = true;
        }
    }
    outEntity = selected;
    return found;
}

bool ConstructionTool::OnPress(Scene& scene, ConstructionSystem& system, const ToolInput& input)
{
    m_lastInput = input;
    if (m_phase == Phase::Height) {
        CommitBox(scene);
        return true;
    }
    if (m_phase == Phase::Path) {
        UpdatePath(scene, input);
        if (PathCanClose(input)) {
            CommitWall(scene, true);
        } else if (vlen(vsub(m_corner, m_path.back())) < kMinDimension) {
            // Clicking the last corner again finishes an open wall.
            if (m_path.size() >= 2) CommitWall(scene, false);
        } else {
            m_path.push_back(m_corner);
        }
        return true;
    }
    if (m_phase != Phase::Idle) {
        return true;
    }
    if (m_mode == ToolMode::MeasureWalk) {
        return PressMeasure(scene, system, input);
    }
    if (m_mode == ToolMode::DrawBox || m_mode == ToolMode::DrawRoom || m_mode == ToolMode::DrawStairs ||
        m_mode == ToolMode::DrawRamp) {
        Vec3 point;
        if (!PlacementPoint(scene, system, input, point)) {
            return false;
        }
        m_anchor = point;
        m_corner = point;
        m_typed.clear();
        m_phase = Phase::Base;
        return true;
    }
    if (m_mode == ToolMode::DrawWall) {
        Vec3 point;
        if (!PlacementPoint(scene, system, input, point)) {
            return false;
        }
        m_extendWall = entt::null;
        m_extendKeep = 0;
        entt::entity openWall = entt::null;
        bool atStart = false;
        if (!input.ctrl && SnapToWallCorner(scene, point, std::max(0.3f, input.gridStep * 0.5f), &openWall, &atStart) &&
            openWall != entt::null) {
            // Draw on from the end of an open wall: carry on its path, so the new corner is mitred like any other.
            const auto& existing = scene.GetRegistry().get<ConstructionShapeComponent>(openWall);
            const Mat4 world = scene.GetWorldTransform(Entity(openWall, &scene));
            m_path.clear();
            for (const WallPoint& p : existing.wall.points) {
                m_path.push_back(Mat4::TransformPoint(world, Vec3(p.x, 0.0f, p.z)));
            }
            if (atStart) std::reverse(m_path.begin(), m_path.end());
            m_extendWall = openWall;
            m_extendKeep = m_path.size();
            m_extendAtStart = atStart;
            point = m_path.back();
        } else {
            m_path = {point};
        }
        m_anchor = point;
        m_corner = point;
        m_typed.clear();
        m_phase = Phase::Path;
        return true;
    }
    if (m_mode == ToolMode::DrawOpening) {
        entt::entity picked = entt::null;
        Vec3 pickedLocal;
        if (PickOpening(scene, input.ray, picked, pickedLocal)) {
            m_opening = picked;
            m_target = scene.GetRegistry().get<TransformComponent>(picked).parent;
            m_startOpening = scene.GetRegistry().get<ConstructionShapeComponent>(picked).opening;
            m_wall = m_startOpening.side;
            m_segment = m_startOpening.segment;
            m_planePoint = pickedLocal;
            WallFrame frame;
            if (!HostFrame(scene, m_target, m_wall, m_segment, frame)) {
                return false;
            }
            m_wallStart = FrameCoordinates(frame, pickedLocal);
            if (m_host.pushUndo) m_host.pushUndo("Move opening");
            m_phase = Phase::OpeningMove;
            return true;
        }
        ConstructionHit hit;
        if (!system.Raycast(scene, input.ray, hit) || hit.tag.wall < 0) {
            return false;
        }
        WallFrame frame;
        if (!HostFrame(scene, hit.entity, static_cast<WallSide>(hit.tag.wall), std::max<int>(0, hit.tag.segment), frame)) {
            return false;
        }
        m_target = hit.entity;
        m_wall = static_cast<WallSide>(hit.tag.wall);
        m_segment = std::max<int>(0, hit.tag.segment);
        m_planePoint = hit.localPoint;
        const Vec3 wc = FrameCoordinates(frame, hit.localPoint);
        m_wallStart = wc;
        m_wallCurrent = wc;
        m_opening = entt::null;
        m_wallStart = Vec3(Snap(wc.x, input), Snap(wc.y, input), 0.0f);
        m_wallCurrent = m_wallStart;
        m_phase = Phase::OpeningDraw;
        return true;
    }
    entt::entity entity = entt::null;
    Handle handle;
    if (!PickHandle(scene, input, entity, handle)) {
        return false;
    }
    m_target = entity;
    m_handle = handle;
    const auto& shape = scene.GetRegistry().get<ConstructionShapeComponent>(entity);
    m_startBox = shape.box;
    m_startWall = shape.wall;
    m_startMesh = shape.mesh;
    m_startPosition = scene.GetRegistry().get<TransformComponent>(entity).position;
    const Mat4 world = scene.GetWorldTransform(Entity(entity, &scene));
    m_handleWorldOrigin = Mat4::TransformPoint(world, handle.local);
    const Vec3 outward = vscale(Axis(handle.axis), static_cast<float>(handle.dir));
    m_handleWorldAxis = Mat4::TransformDirection(world, outward);
    if (!ClosestOnLine(m_handleWorldOrigin, m_handleWorldAxis, input.ray, m_handleStartT)) {
        return false;
    }
    if (m_host.pushUndo) m_host.pushUndo(handle.vertex >= 0 ? "Move corner" : "Resize shape");
    m_phase = Phase::Handle;
    return true;
}

void ConstructionTool::OnMove(Scene& scene, ConstructionSystem& system, const ToolInput& input)
{
    m_lastInput = input;
    switch (m_phase) {
    case Phase::Base: UpdateBase(input); return;
    case Phase::Height: UpdateHeight(input); return;
    case Phase::Handle: UpdateHandleDrag(scene, input); return;
    case Phase::OpeningDraw: UpdateOpeningDraw(scene, input); return;
    case Phase::OpeningMove: UpdateOpeningMove(scene, input); return;
    case Phase::Path: UpdatePath(scene, input); return;
    case Phase::Idle: break;
    }
    m_hoverValid = false;
    if (m_mode == ToolMode::MeasureWalk) {
        m_hoverValid = PlacementPoint(scene, system, input, m_hoverPoint);
        if (m_measure == 1 && m_hoverValid && m_walkNav) {
            m_measureTo = m_hoverPoint;
            m_walkRoute = Analysis::MeasureWalk(*m_walkNav, m_walkMetrics, m_measureFrom, m_measureTo);
        }
    } else if (m_mode == ToolMode::DrawBox || m_mode == ToolMode::DrawRoom || m_mode == ToolMode::DrawStairs ||
        m_mode == ToolMode::DrawRamp ||
        m_mode == ToolMode::DrawWall) {
        m_hoverValid = PlacementPoint(scene, system, input, m_hoverPoint);
    } else if (m_mode == ToolMode::DrawOpening) {
        ConstructionHit hit;
        WallFrame frame;
        if (system.Raycast(scene, input.ray, hit) && hit.tag.wall >= 0 &&
            HostFrame(scene, hit.entity, static_cast<WallSide>(hit.tag.wall), std::max<int>(0, hit.tag.segment), frame)) {
            m_hoverValid = true;
            m_hoverHost = hit.entity;
            m_hoverWall = static_cast<WallSide>(hit.tag.wall);
            m_hoverSegment = std::max<int>(0, hit.tag.segment);
            m_hoverLocal = hit.localPoint;
        }
    }
}

bool ConstructionTool::OnRelease(Scene& scene, ConstructionSystem&, const ToolInput& input)
{
    m_lastInput = input;
    switch (m_phase) {
    case Phase::Base: {
        const bool clicked = std::fabs(m_corner.x - m_anchor.x) < kMinDimension ||
                             std::fabs(m_corner.z - m_anchor.z) < kMinDimension;
        if (clicked) {
            const float w = m_mode == ToolMode::DrawRoom ? 6.0f : m_mode == ToolMode::DrawStairs ? 1.2f
                : m_mode == ToolMode::DrawRamp ? 2.0f : 1.0f;
            const float d = m_mode == ToolMode::DrawRoom ? 4.0f : m_mode == ToolMode::DrawStairs ? 3.0f
                : m_mode == ToolMode::DrawRamp ? 4.0f : 1.0f;
            m_corner = vadd(m_anchor, Vec3(w * 0.5f, 0.0f, d * 0.5f));
            m_anchor = vsub(m_anchor, Vec3(w * 0.5f, 0.0f, d * 0.5f));
            m_height = DefaultHeight();
            CommitBox(scene);
            return true;
        }
        m_height = DefaultHeight();
        m_typed.clear();
        m_phase = Phase::Height;
        return true;
    }
    case Phase::Handle:
    case Phase::OpeningMove:
        m_phase = Phase::Idle;
        if (m_host.changed) m_host.changed();
        return true;
    case Phase::OpeningDraw:
        CommitOpening(scene);
        return true;
    case Phase::Height:
    case Phase::Path:
        return true;
    case Phase::Idle:
        break;
    }
    return false;
}

bool ConstructionTool::SnapToWallCorner(Scene& scene, Vec3& point, float reach, entt::entity* openEnd,
                                        bool* atStart) const
{
    entt::registry& registry = scene.GetRegistry();
    float best = reach;
    bool found = false;
    for (entt::entity entity : registry.view<ConstructionShapeComponent>()) {
        const auto& shape = registry.get<ConstructionShapeComponent>(entity);
        if (shape.source != SourceKind::Recipe || shape.operation != Operation::Add) continue;
        std::vector<WallPoint> corners;
        if (shape.recipe == Recipe::Wall) {
            corners = shape.wall.points;
        } else if (shape.recipe == Recipe::Box && shape.box.hollow) {
            const float hx = (shape.box.width - shape.box.wallThickness) * 0.5f;
            const float hz = (shape.box.depth - shape.box.wallThickness) * 0.5f;
            corners = {{-hx, -hz}, {hx, -hz}, {hx, hz}, {-hx, hz}};
        } else {
            continue;
        }
        const Mat4 world = scene.GetWorldTransform(Entity(entity, &scene));
        for (size_t n = 0; n < corners.size(); ++n) {
            const Vec3 corner = Mat4::TransformPoint(world, Vec3(corners[n].x, 0.0f, corners[n].z));
            const float distance = std::hypot(corner.x - point.x, corner.z - point.z);
            if (distance >= best || std::fabs(corner.y - point.y) > 0.5f) continue;
            best = distance;
            point = corner;
            found = true;
            const bool end = shape.recipe == Recipe::Wall && !shape.wall.closed &&
                             (n == 0 || n + 1 == corners.size());
            if (openEnd) *openEnd = end ? entity : entt::null;
            if (atStart) *atStart = n == 0;
        }
    }
    return found;
}

void ConstructionTool::UpdatePath(Scene& scene, const ToolInput& input)
{
    Vec3 point;
    if (!IntersectPlane(input.ray, m_anchor, Vec3(0, 1, 0), point)) {
        return;
    }
    point = Vec3(Snap(point.x, input), m_anchor.y, Snap(point.z, input));
    if (!m_path.empty()) {
        const Vec3& last = m_path.back();
        const float dx = point.x - last.x;
        const float dz = point.z - last.z;
        // Within 6 degrees of running along X or Z (or always with Shift) the side is made exactly straight, so
        // corners clicked a grid square off do not tilt it.
        const float off = std::min(std::fabs(dx), std::fabs(dz));
        const bool nearlyStraight = off <= std::tan(0.1047f) * std::max(std::fabs(dx), std::fabs(dz));
        if (input.shift || (!input.ctrl && nearlyStraight)) {
            if (std::fabs(dx) > std::fabs(dz)) {
                point.z = last.z;
            } else {
                point.x = last.x;
            }
        }
    }
    if (!input.ctrl && !input.shift) {
        SnapToWallCorner(scene, point, std::max(0.3f, input.gridStep * 0.5f));
    }
    m_corner = point;
}

bool ConstructionTool::PathCanClose(const ToolInput& input) const
{
    if (m_path.size() < 3) {
        return false;
    }
    const float reach = std::max(0.15f, input.gridStep * 0.5f);
    return std::hypot(m_corner.x - m_path.front().x, m_corner.z - m_path.front().z) < reach;
}

void ConstructionTool::CommitWall(Scene& scene, bool closed)
{
    std::vector<WallPoint> points;
    const Vec3 origin = m_path.front();
    for (const Vec3& p : m_path) {
        const WallPoint point{p.x - origin.x, p.z - origin.z};
        if (points.empty() || std::hypot(point.x - points.back().x, point.z - points.back().z) >= kMinDimension) {
            points.push_back(point);
        }
    }
    m_phase = Phase::Idle;
    m_typed.clear();
    m_path.clear();
    if (points.size() < 2 || (closed && points.size() < 3)) {
        return;
    }
    entt::registry& registry = scene.GetRegistry();
    if (m_extendWall != entt::null) {
        const bool extendable = registry.valid(m_extendWall) && registry.all_of<ConstructionShapeComponent>(m_extendWall);
        if (!extendable || points.size() <= m_extendKeep) {
            m_extendWall = entt::null;
            m_extendKeep = 0;
            return;
        }
        // The wall drawn on from keeps its own place; its path is rewritten in its frame.
        if (m_host.pushUndo) m_host.pushUndo(closed ? "Close wall into a room" : "Extend wall");
        const Mat4 inverse = Mat4::Invert(scene.GetWorldTransform(Entity(m_extendWall, &scene)));
        auto& shape = registry.get<ConstructionShapeComponent>(m_extendWall);
        shape.wall.points.clear();
        for (const WallPoint& p : points) {
            const Vec3 local = Mat4::TransformPoint(inverse, Vec3(p.x + origin.x, origin.y, p.z + origin.z));
            shape.wall.points.push_back(WallPoint{local.x, local.z});
        }
        if (m_extendAtStart) {
            // Keep the wall's own order, with the new corners before its first, so its openings keep their sides;
            // their segments move along by the segments added in front.
            std::reverse(shape.wall.points.begin(), shape.wall.points.end());
            const int added = static_cast<int>(points.size() - m_extendKeep);
            for (entt::entity child : registry.get<TransformComponent>(m_extendWall).children) {
                if (auto* opening = registry.try_get<ConstructionShapeComponent>(child);
                    opening && opening->recipe == Recipe::Opening) {
                    opening->opening.segment += added;
                }
            }
        }
        shape.wall.closed = closed;
        if (closed) shape.role = "room";
        const entt::entity extended = m_extendWall;
        m_extendWall = entt::null;
        m_extendKeep = 0;
        if (m_host.select) m_host.select(extended);
        if (m_host.changed) m_host.changed();
        return;
    }
    m_extendWall = entt::null;
    m_extendKeep = 0;
    if (m_host.pushUndo) m_host.pushUndo(closed ? "Draw room" : "Draw wall");
    Entity entity = scene.CreateEntity(closed ? "Room" : "Wall");
    entity.GetComponent<TransformComponent>().position = origin;
    auto& shape = entity.AddComponent<ConstructionShapeComponent>();
    shape.recipe = Recipe::Wall;
    shape.role = closed ? "room" : "wall";
    shape.wall.points = std::move(points);
    shape.wall.closed = closed;
    shape.box.height = 3.0f;
    shape.box.wallThickness = 0.2f;
    shape.box.floor = true;
    Sanitize(shape);
    if (m_host.created) m_host.created(static_cast<entt::entity>(entity));
    if (m_host.select) m_host.select(static_cast<entt::entity>(entity));
    if (m_host.changed) m_host.changed();
}

void ConstructionTool::UpdateBase(const ToolInput& input)
{
    Vec3 point;
    if (IntersectPlane(input.ray, m_anchor, Vec3(0, 1, 0), point)) {
        m_corner = Vec3(Snap(point.x, input), m_anchor.y, Snap(point.z, input));
    }
}

void ConstructionTool::UpdateHeight(const ToolInput& input)
{
    const Vec3 center = vscale(vadd(m_anchor, m_corner), 0.5f);
    Vec3 normal(-input.ray.direction.x, 0.0f, -input.ray.direction.z);
    if (vlen(normal) < 1e-4f) {
        return;
    }
    normal = vnorm(normal);
    Vec3 point;
    if (IntersectPlane(input.ray, center, normal, point)) {
        m_height = std::max(Snap(point.y - m_anchor.y, input), input.gridStep > 0.0f ? input.gridStep : kMinDimension);
    }
}

float ConstructionTool::DefaultHeight() const
{
    return m_mode == ToolMode::DrawRoom ? 3.0f : m_mode == ToolMode::DrawStairs ? 1.5f : 1.0f;
}

void ConstructionTool::CommitBox(Scene& scene)
{
    const bool room = m_mode == ToolMode::DrawRoom;
    const bool stairs = m_mode == ToolMode::DrawStairs;
    const bool ramp = m_mode == ToolMode::DrawRamp;
    if (m_host.pushUndo) m_host.pushUndo(room ? "Draw room" : stairs ? "Draw stairs" : ramp ? "Draw ramp" : "Draw block");
    Entity entity = scene.CreateEntity(room ? "Room" : stairs ? "Stairs" : ramp ? "Ramp" : "Block");
    auto& transform = entity.GetComponent<TransformComponent>();
    transform.position = Vec3((m_anchor.x + m_corner.x) * 0.5f, m_anchor.y, (m_anchor.z + m_corner.z) * 0.5f);
    auto& shape = entity.AddComponent<ConstructionShapeComponent>();
    shape.recipe = stairs ? Recipe::Stairs : ramp ? Recipe::Ramp : Recipe::Box;
    shape.role = room ? "room" : stairs ? "stairs" : ramp ? "ramp" : "block";
    float dx = std::max(std::fabs(m_corner.x - m_anchor.x), kMinDimension);
    float dz = std::max(std::fabs(m_corner.z - m_anchor.z), kMinDimension);
    if (stairs || ramp) {
        // Stairs and ramps climb along the longer side of the footprint, away from where the drag started.
        if (dx > dz) {
            transform.rotation.y = m_corner.x > m_anchor.x ? kHalfPi : -kHalfPi;
            std::swap(dx, dz);
        } else if (m_corner.z < m_anchor.z) {
            transform.rotation.y = kPi;
        }
    }
    if (stairs) {
        const float stepHeight = m_host.stepHeight ? m_host.stepHeight() : 0.3f;
        shape.stairs.maxRise = std::max(0.05f, stepHeight * 0.95f);
    }
    shape.box.width = dx;
    shape.box.depth = dz;
    shape.box.height = std::max(m_height, kMinDimension);
    shape.box.hollow = room;
    shape.box.floor = true;
    shape.box.ceiling = false;
    Sanitize(shape);
    m_phase = Phase::Idle;
    m_typed.clear();
    if (m_host.created) m_host.created(static_cast<entt::entity>(entity));
    if (m_host.select) m_host.select(static_cast<entt::entity>(entity));
    if (m_host.changed) m_host.changed();
}

void ConstructionTool::UpdateHandleDrag(Scene& scene, const ToolInput& input)
{
    entt::registry& registry = scene.GetRegistry();
    if (!registry.valid(m_target) || !registry.all_of<ConstructionShapeComponent, TransformComponent>(m_target)) {
        m_phase = Phase::Idle;
        return;
    }
    float t = 0.0f;
    if (!ClosestOnLine(m_handleWorldOrigin, m_handleWorldAxis, input.ray, t)) {
        return;
    }
    if (m_handle.vertex >= 0) {
        auto& shape = registry.get<ConstructionShapeComponent>(m_target);
        if (m_handle.vertex >= static_cast<int>(shape.mesh.vertices.size()) ||
            m_handle.vertex >= static_cast<int>(m_startMesh.vertices.size())) {
            return;
        }
        const Vec3 start = m_startMesh.vertices[static_cast<size_t>(m_handle.vertex)];
        Vec3& vertex = shape.mesh.vertices[static_cast<size_t>(m_handle.vertex)];
        if (input.shift) {
            vertex = Vec3(start.x, Snap(start.y + (t - m_handleStartT), input), start.z);
            return;
        }
        Vec3 world;
        if (!IntersectPlane(input.ray, m_handleWorldOrigin, Mat4::TransformDirection(
                scene.GetWorldTransform(Entity(m_target, &scene)), Vec3(0, 1, 0)), world)) {
            return;
        }
        const Vec3 local = Mat4::TransformPoint(Mat4::Invert(scene.GetWorldTransform(Entity(m_target, &scene))), world);
        vertex = Vec3(Snap(local.x, input), start.y, Snap(local.z, input));
        return;
    }
    if (m_handle.point >= 0) {
        auto& shape = registry.get<ConstructionShapeComponent>(m_target);
        Vec3 world;
        if (m_handle.point >= static_cast<int>(shape.wall.points.size()) ||
            !IntersectPlane(input.ray, m_handleWorldOrigin, Vec3(0, 1, 0), world)) {
            return;
        }
        const Vec3 local = Mat4::TransformPoint(Mat4::Invert(scene.GetWorldTransform(Entity(m_target, &scene))), world);
        shape.wall.points[static_cast<size_t>(m_handle.point)] = WallPoint{Snap(local.x, input), Snap(local.z, input)};
        return;
    }
    // The axis is one local unit long in world space, so t is already in the shape's own units.
    const float localDelta = t - m_handleStartT;
    auto& shape = registry.get<ConstructionShapeComponent>(m_target);
    auto& transform = registry.get<TransformComponent>(m_target);
    float* size = m_handle.axis == 0 ? &shape.box.width : m_handle.axis == 1 ? &shape.box.height : &shape.box.depth;
    const float start = m_handle.axis == 0 ? m_startBox.width : m_handle.axis == 1 ? m_startBox.height : m_startBox.depth;
    const bool symmetric = input.shift && m_handle.axis != 1;
    const float minSize = input.gridStep > 0.0f && !input.ctrl ? input.gridStep : kMinDimension;
    const float wanted = start + (symmetric ? 2.0f : 1.0f) * localDelta;
    auto resize = [&](float value) {
        *size = value;
        transform.position = m_startPosition;
        if (!symmetric && m_handle.axis != 1) {
            const Vec3 outward = vscale(Axis(m_handle.axis), static_cast<float>(m_handle.dir) * (value - start) * 0.5f);
            transform.position = vadd(m_startPosition, Mat4::TransformDirection(transform.GetLocalMatrix(), outward));
        }
    };
    const float snapped = std::max(Snap(wanted, input), minSize);
    resize(snapped);
    if (!symmetric && !input.ctrl) {
        const float faceSnap = FaceSnapDistance(scene);
        if (faceSnap != 0.0f && snapped + faceSnap >= minSize) {
            resize(snapped + faceSnap);
        }
    }
    Sanitize(shape);
}

float ConstructionTool::FaceSnapDistance(Scene& scene) const
{
    constexpr float kReach = 0.2f;
    const float axisLength = vlen(m_handleWorldAxis);
    const float along[3] = {m_handleWorldAxis.x, m_handleWorldAxis.y, m_handleWorldAxis.z};
    int k = 0;
    for (int i = 1; i < 3; ++i) {
        if (std::fabs(along[i]) > std::fabs(along[k])) k = i;
    }
    // Only a face that stands square to the world can sit flush on a neighbour's bounds.
    if (axisLength < 1e-6f || std::fabs(along[k]) < axisLength * 0.999f) {
        return 0.0f;
    }
    const BoxParams& box = scene.GetRegistry().get<ConstructionShapeComponent>(m_target).box;
    Vec3 lo(-box.width * 0.5f, 0.0f, -box.depth * 0.5f);
    Vec3 hi(box.width * 0.5f, box.height, box.depth * 0.5f);
    const float face = m_handle.axis == 1 ? box.height
                                          : static_cast<float>(m_handle.dir) * (m_handle.axis == 0 ? box.width : box.depth) * 0.5f;
    (m_handle.axis == 0 ? lo.x : m_handle.axis == 1 ? lo.y : lo.z) = face;
    (m_handle.axis == 0 ? hi.x : m_handle.axis == 1 ? hi.y : hi.z) = face;
    const AABB faceBounds = SceneAuthoring::TransformBounds(AABB(lo, hi), scene.GetWorldTransform(Entity(m_target, &scene)));
    const entt::entity self = m_target;
    const std::vector<AABB> neighbours = SceneAuthoring::NeighbourBounds(
        scene, faceBounds, kReach, [self](entt::entity handle) { return handle == self; });
    const Vec3 offset = SceneAuthoring::SnapBoundsToNeighbours(faceBounds, neighbours, kReach);
    const float worldMove = k == 0 ? offset.x : k == 1 ? offset.y : offset.z;
    // The handle axis is one local unit long, so this is the change in size along it.
    return worldMove / along[k];
}

bool ConstructionTool::WallPointFromRay(Scene& scene, const ToolInput& input, Vec3& outLocal) const
{
    WallFrame wall;
    if (!HostFrame(scene, m_target, m_wall, m_segment, wall)) {
        return false;
    }
    const Mat4 inverse = Mat4::Invert(scene.GetWorldTransform(Entity(m_target, &scene)));
    const Vec3 origin = Mat4::TransformPoint(inverse, input.ray.origin);
    const Vec3 dir = vsub(Mat4::TransformPoint(inverse, vadd(input.ray.origin, input.ray.direction)), origin);
    return IntersectPlane(Ray(origin, dir), m_planePoint, wall.normal, outLocal) ||
           IntersectPlane(Ray(origin, dir), m_planePoint, vscale(wall.normal, -1.0f), outLocal);
}

bool ConstructionTool::PickOpening(Scene& scene, const Ray& ray, entt::entity& outOpening, Vec3& outLocal) const
{
    entt::registry& registry = scene.GetRegistry();
    float best = 0.0f;
    bool found = false;
    for (entt::entity entity : registry.view<ConstructionShapeComponent, TransformComponent>()) {
        const auto& shape = registry.get<ConstructionShapeComponent>(entity);
        const entt::entity host = registry.get<TransformComponent>(entity).parent;
        const OpeningParams& o = shape.opening;
        WallFrame wall;
        if (shape.recipe != Recipe::Opening || host == entt::null || !HostFrame(scene, host, o.side, o.segment, wall)) {
            continue;
        }
        const Mat4 world = scene.GetWorldTransform(Entity(host, &scene));
        const Mat4 inverse = Mat4::Invert(world);
        const Vec3 origin = Mat4::TransformPoint(inverse, ray.origin);
        const Vec3 dir = vsub(Mat4::TransformPoint(inverse, vadd(ray.origin, ray.direction)), origin);
        Vec3 local;
        if (!IntersectPlane(Ray(origin, dir), wall.center, wall.normal, local) &&
            !IntersectPlane(Ray(origin, dir), wall.center, vscale(wall.normal, -1.0f), local)) {
            continue;
        }
        const Vec3 wc = FrameCoordinates(wall, local);
        if (std::fabs(wc.x - o.offset) > o.width * 0.5f || wc.y < o.sill || wc.y > o.sill + o.height) {
            continue;
        }
        const float distance = vlen(vsub(Mat4::TransformPoint(world, local), ray.origin));
        if (!found || distance < best) {
            found = true;
            best = distance;
            outOpening = entity;
            outLocal = local;
        }
    }
    return found;
}

void ConstructionTool::UpdateOpeningDraw(Scene& scene, const ToolInput& input)
{
    Vec3 local;
    if (!WallPointFromRay(scene, input, local)) {
        return;
    }
    WallFrame wall;
    if (!HostFrame(scene, m_target, m_wall, m_segment, wall)) {
        return;
    }
    const Vec3 wc = FrameCoordinates(wall, local);
    m_wallCurrent = Vec3(Snap(wc.x, input), Snap(wc.y, input), 0.0f);
}

void ConstructionTool::UpdateOpeningMove(Scene& scene, const ToolInput& input)
{
    entt::registry& registry = scene.GetRegistry();
    if (!registry.valid(m_opening) || !registry.all_of<ConstructionShapeComponent>(m_opening)) {
        m_phase = Phase::Idle;
        return;
    }
    Vec3 local;
    if (!WallPointFromRay(scene, input, local)) {
        return;
    }
    WallFrame wall;
    if (!HostFrame(scene, m_target, m_wall, m_segment, wall)) {
        return;
    }
    const Vec3 wc = FrameCoordinates(wall, local);
    OpeningParams& o = registry.get<ConstructionShapeComponent>(m_opening).opening;
    const float limit = std::max(0.0f, wall.halfLength - o.width * 0.5f);
    // Snap the movement, not the position, so an opening standing on the floor stays on it.
    o.offset = std::clamp(m_startOpening.offset + Snap(wc.x - m_wallStart.x, input), -limit, limit);
    o.sill = std::max(HostFloor(scene, m_target), m_startOpening.sill + Snap(wc.y - m_wallStart.y, input));
}

void ConstructionTool::CommitOpening(Scene& scene)
{
    m_phase = Phase::Idle;
    entt::registry& registry = scene.GetRegistry();
    WallFrame wall;
    if (!HostFrame(scene, m_target, m_wall, m_segment, wall)) {
        return;
    }
    const float base = HostFloor(scene, m_target);
    OpeningParams o;
    o.side = m_wall;
    o.segment = m_segment;
    const float u0 = std::min(m_wallStart.x, m_wallCurrent.x);
    const float u1 = std::max(m_wallStart.x, m_wallCurrent.x);
    const float v0 = std::min(m_wallStart.y, m_wallCurrent.y);
    const float v1 = std::max(m_wallStart.y, m_wallCurrent.y);
    const bool clicked = u1 - u0 < kMinDimension || v1 - v0 < kMinDimension;
    bool door = false;
    if (clicked) {
        door = m_wallStart.y < base + 1.0f;
        o.width = kDefaultDoorWidth;
        o.height = door ? kDefaultDoorHeight : kDefaultWindowHeight;
        o.sill = door ? base : std::max(base, m_wallStart.y - kDefaultWindowHeight * 0.5f);
        o.offset = m_wallStart.x;
    } else {
        o.width = u1 - u0;
        o.offset = (u0 + u1) * 0.5f;
        door = v0 - base < 0.15f;
        o.sill = door ? base : v0;
        o.height = v1 - o.sill;
    }
    o.width = std::min(o.width, wall.halfLength * 2.0f);
    const float limit = std::max(0.0f, wall.halfLength - o.width * 0.5f);
    o.offset = std::clamp(o.offset, -limit, limit);
    o.height = std::min(o.height, std::max(kMinDimension, wall.height - o.sill));

    if (m_host.pushUndo) m_host.pushUndo(door ? "Add doorway" : "Add window");
    Entity opening = scene.CreateEntity(door ? "Doorway" : "Window");
    auto& shape = opening.AddComponent<ConstructionShapeComponent>();
    shape.recipe = Recipe::Opening;
    shape.role = door ? "doorway" : "window";
    shape.opening = o;
    Sanitize(shape);
    scene.SetParent(opening, Entity(m_target, &scene), false);
    if (m_host.select) m_host.select(static_cast<entt::entity>(opening));
    if (m_host.changed) m_host.changed();
}

bool ConstructionTool::ApplyTyped(Scene& scene, ConstructionSystem&)
{
    std::vector<float> values;
    std::stringstream stream(m_typed);
    std::string part;
    while (std::getline(stream, part, ',')) {
        char* end = nullptr;
        const float value = std::strtof(part.c_str(), &end);
        if (end == part.c_str() || !std::isfinite(value) || value <= 0.0f) {
            m_typed.clear();
            return false;
        }
        values.push_back(value);
    }
    m_typed.clear();
    if (values.empty()) {
        return false;
    }
    if (m_phase == Phase::Base) {
        const float w = values[0];
        const float d = values.size() > 1 ? values[1] : values[0];
        const float sx = m_corner.x < m_anchor.x ? -1.0f : 1.0f;
        const float sz = m_corner.z < m_anchor.z ? -1.0f : 1.0f;
        m_corner = Vec3(m_anchor.x + sx * w, m_anchor.y, m_anchor.z + sz * d);
        m_height = DefaultHeight();
        m_phase = Phase::Height;
        return true;
    }
    if (m_phase == Phase::Height) {
        m_height = values[0];
        CommitBox(scene);
        return true;
    }
    if (m_phase == Phase::Path) {
        // The next corner, that far from the last one towards the cursor (along +X when the cursor is on it).
        const Vec3 last = m_path.back();
        Vec3 dir(m_corner.x - last.x, 0.0f, m_corner.z - last.z);
        dir = vlen(dir) < 1e-4f ? Vec3(1.0f, 0.0f, 0.0f) : vnorm(dir);
        m_path.push_back(vadd(last, vscale(dir, values[0])));
        m_corner = m_path.back();
        return true;
    }
    return false;
}

bool ConstructionTool::OnKey(Scene& scene, ConstructionSystem& system, int qtKey)
{
    if (m_mode == ToolMode::MeasureWalk && m_measure != 0 && qtKey == kKeyEscape) {
        m_measure = 0;
        return true;
    }
    if (m_phase == Phase::Idle) {
        return false;
    }
    if (qtKey == kKeyEscape) {
        Cancel(scene);
        return true;
    }
    if (m_phase != Phase::Base && m_phase != Phase::Height && m_phase != Phase::Path) {
        return false;
    }
    if ((qtKey >= '0' && qtKey <= '9') || qtKey == '.' || qtKey == ',') {
        m_typed.push_back(static_cast<char>(qtKey));
        return true;
    }
    if (qtKey == kKeyBackspace) {
        if (!m_typed.empty()) {
            m_typed.pop_back();
        } else if (m_phase == Phase::Path) {
            if (m_path.size() > m_extendKeep) {
                m_path.pop_back();
            }
            if (m_path.empty() || m_path.size() == m_extendKeep) {
                m_phase = Phase::Idle;
                m_path.clear();
            }
        }
        return true;
    }
    if (qtKey == kKeyReturn || qtKey == kKeyEnter) {
        if (m_phase == Phase::Path && m_typed.empty()) {
            CommitWall(scene, false);
        } else {
            ApplyTyped(scene, system);
        }
        return true;
    }
    return false;
}

void ConstructionTool::Cancel(Scene& scene)
{
    entt::registry& registry = scene.GetRegistry();
    if (m_phase == Phase::Handle && registry.valid(m_target) && registry.all_of<ConstructionShapeComponent>(m_target)) {
        registry.get<ConstructionShapeComponent>(m_target).box = m_startBox;
        registry.get<ConstructionShapeComponent>(m_target).wall = m_startWall;
        registry.get<ConstructionShapeComponent>(m_target).mesh = m_startMesh;
        registry.get<TransformComponent>(m_target).position = m_startPosition;
        if (m_host.discardUndo) m_host.discardUndo();
    } else if (m_phase == Phase::OpeningMove && registry.valid(m_opening) &&
               registry.all_of<ConstructionShapeComponent>(m_opening)) {
        registry.get<ConstructionShapeComponent>(m_opening).opening = m_startOpening;
        if (m_host.discardUndo) m_host.discardUndo();
    }
    m_phase = Phase::Idle;
    m_typed.clear();
    m_path.clear();
    m_extendWall = entt::null;
    m_extendKeep = 0;
}

std::string ConstructionTool::MeasureSummary() const
{
    if (m_mode != ToolMode::MeasureWalk) {
        return {};
    }
    if (m_measure == 0) {
        return "Click where the walk starts";
    }
    if (!m_walkNav) {
        return "No navmesh: " + m_walkError;
    }
    if (!m_walkRoute.found) {
        return "No walkable floor at one of the points";
    }
    char text[160];
    std::snprintf(text, sizeof(text), "Walk %.1f m, %.1f s (straight line %.1f m)", m_walkRoute.length,
                  m_walkRoute.seconds, m_walkRoute.straight);
    std::string summary = text;
    if (m_walkRoute.links > 0) {
        summary += ", " + std::to_string(m_walkRoute.links) + (m_walkRoute.links == 1 ? " vault" : " vaults");
    }
    if (!m_walkRoute.complete) {
        summary += ", the end can't be walked to";
    }
    return summary;
}

std::string ConstructionTool::MeasureStatus() const
{
    if (m_measure == 0) {
        return "Measure Walk: click where the walk starts, then where it ends. The route characters would walk is "
               "drawn in green, with its length and walking time.";
    }
    std::string status = "Measure Walk: " + MeasureSummary() + ".";
    if (m_walkNav && m_walkRoute.found) {
        status += m_walkRoute.complete ? " Times are at walking speed."
                                       : " The orange route stops at the closest point it reaches.";
        status += m_measure == 1 ? " Click to place the end." : " Click to measure again, Esc to clear.";
    }
    return status;
}

std::string ConstructionTool::StatusText() const
{
    std::string text;
    switch (m_phase) {
    case Phase::Base: text = PhaseHint(1); break;
    case Phase::Height: text = PhaseHint(2); break;
    case Phase::Handle:
        text = m_handle.vertex >= 0 ? "Drag the corner across. Hold Shift to move it up and down. Ctrl turns snapping off. Esc cancels."
                                    : PhaseHint(3);
        break;
    case Phase::OpeningDraw: text = PhaseHint(4); break;
    case Phase::OpeningMove: text = PhaseHint(5); break;
    case Phase::Path: text = PhaseHint(6); break;
    case Phase::Idle:
        if (m_mode == ToolMode::DrawBox) text = "Draw Block: drag on the ground or a surface. Click for a 1 m block.";
        else if (m_mode == ToolMode::DrawRoom) text = "Draw Room: drag on the ground or a surface. Click for a 6 x 4 m room.";
        else if (m_mode == ToolMode::DrawStairs) text = "Draw Stairs: drag the footprint (they climb along the longer side, away from where you start), then move up for the height.";
        else if (m_mode == ToolMode::DrawRamp) text = "Draw Ramp: drag the footprint (it rises along the longer side, away from where you start), then move up for the height.";
        else if (m_mode == ToolMode::DrawWall) text = "Draw Wall: click on the ground or a surface to place the first corner.";
        else if (m_mode == ToolMode::DrawOpening) text = "Draw Opening: drag on a wall. Click for a door near the floor or a window higher up. Drag an opening to move it.";
        else if (m_mode == ToolMode::MeasureWalk) text = MeasureStatus();
        break;
    }
    if (!m_typed.empty()) {
        text += "  Typed: " + m_typed;
    }
    return text;
}

void ConstructionTool::AppendOverlay(Scene& scene, const ConstructionSystem&, std::vector<EditorDebugLine>& lines,
                                     std::vector<EditorDebugPoint>& points) const
{
    const Mat4 identity = Mat4::Identity();
    if (m_mode == ToolMode::MeasureWalk) {
        if (m_hoverValid && m_measure != 1) {
            points.push_back({m_hoverPoint, kPreviewColor, 0.15f});
        }
        if (m_measure == 0) {
            return;
        }
        const Vec3 lift(0.0f, 0.05f, 0.0f);
        const Vec3 routeColor = m_walkRoute.complete ? kWalkColor : kPreviewColor;
        for (size_t n = 0; n + 1 < m_walkRoute.corners.size(); ++n) {
            lines.push_back({vadd(m_walkRoute.corners[n], lift), vadd(m_walkRoute.corners[n + 1], lift), routeColor});
        }
        lines.push_back({m_measureFrom, m_measureTo, kStraightColor});
        points.push_back({m_measureFrom, routeColor, 0.18f});
        points.push_back({m_measureTo, routeColor, 0.18f});
        return;
    }
    if (m_phase == Phase::Base || m_phase == Phase::Height) {
        const Vec3 mn(std::min(m_anchor.x, m_corner.x), m_anchor.y, std::min(m_anchor.z, m_corner.z));
        const Vec3 mx(std::max(m_anchor.x, m_corner.x), m_anchor.y + (m_phase == Phase::Height ? m_height : 0.0f),
                      std::max(m_anchor.z, m_corner.z));
        AddBoxLines(lines, identity, mn, mx, kPreviewColor);
        return;
    }
    if (m_hoverValid && m_phase == Phase::Idle &&
        (m_mode == ToolMode::DrawBox || m_mode == ToolMode::DrawRoom || m_mode == ToolMode::DrawStairs ||
        m_mode == ToolMode::DrawRamp ||
         m_mode == ToolMode::DrawWall)) {
        points.push_back({m_hoverPoint, kPreviewColor, 0.15f});
    }
    if (m_phase == Phase::Path && !m_path.empty()) {
        const Vec3 lift(0.0f, 3.0f, 0.0f);
        std::vector<Vec3> path = m_path;
        path.push_back(m_corner);
        for (size_t n = 0; n < path.size(); ++n) {
            lines.push_back({path[n], vadd(path[n], lift), kPreviewColor});
            if (n + 1 < path.size()) {
                lines.push_back({path[n], path[n + 1], kPreviewColor});
                lines.push_back({vadd(path[n], lift), vadd(path[n + 1], lift), kPreviewColor});
            }
            points.push_back({path[n], kPreviewColor, 0.12f});
        }
        if (PathCanClose(m_lastInput)) {
            points.push_back({m_path.front(), kHandleColor, 0.25f});
        }
        return;
    }

    entt::registry& registry = scene.GetRegistry();
    auto openingRect = [&](entt::entity host, WallSide side, int segment, float offset, float sill, float width,
                           float height, const Vec3& color) {
        WallFrame wall;
        if (!HostFrame(scene, host, side, segment, wall)) {
            return;
        }
        const Mat4 world = scene.GetWorldTransform(Entity(host, &scene));
        const Vec3 lift = vscale(wall.normal, 0.01f);
        Vec3 corners[4];
        const float us[4] = {offset - width * 0.5f, offset + width * 0.5f, offset + width * 0.5f, offset - width * 0.5f};
        const float vs[4] = {sill, sill, sill + height, sill + height};
        for (int n = 0; n < 4; ++n) {
            corners[n] = Mat4::TransformPoint(world, vadd(vadd(wall.center, vscale(wall.u, us[n])), vadd(Vec3(0, vs[n], 0), lift)));
        }
        AddRectLines(lines, corners, color);
    };

    if (m_phase == Phase::OpeningDraw) {
        const float u0 = std::min(m_wallStart.x, m_wallCurrent.x);
        const float u1 = std::max(m_wallStart.x, m_wallCurrent.x);
        const float v0 = std::min(m_wallStart.y, m_wallCurrent.y);
        const float v1 = std::max(m_wallStart.y, m_wallCurrent.y);
        openingRect(m_target, m_wall, m_segment, (u0 + u1) * 0.5f, v0, u1 - u0, v1 - v0, kPreviewColor);
    } else if (WallFrame hoverFrame; m_mode == ToolMode::DrawOpening && m_hoverValid && m_phase == Phase::Idle &&
               HostFrame(scene, m_hoverHost, m_hoverWall, m_hoverSegment, hoverFrame)) {
        const Vec3 wc = FrameCoordinates(hoverFrame, m_hoverLocal);
        const float base = HostFloor(scene, m_hoverHost);
        const bool door = wc.y < base + 1.0f;
        openingRect(m_hoverHost, m_hoverWall, m_hoverSegment, wc.x, door ? base : wc.y - kDefaultWindowHeight * 0.5f,
                    kDefaultDoorWidth, door ? kDefaultDoorHeight : kDefaultWindowHeight, kPreviewColor);
    }

    // Outlines of the openings in the selected host, or in the opening's host when an opening is selected.
    const entt::entity selected = m_host.selected ? m_host.selected() : entt::null;
    entt::entity host = selected;
    if (registry.valid(selected) && registry.all_of<ConstructionShapeComponent>(selected) &&
        registry.get<ConstructionShapeComponent>(selected).recipe == Recipe::Opening) {
        host = registry.get<TransformComponent>(selected).parent;
    }
    if (m_mode == ToolMode::DrawOpening && m_hoverValid) {
        host = m_hoverHost;
    }
    if (host != entt::null && registry.valid(host) && registry.all_of<ConstructionShapeComponent>(host) &&
        CanHostOpenings(registry.get<ConstructionShapeComponent>(host))) {
        for (entt::entity child : registry.get<TransformComponent>(host).children) {
            const auto* shape = registry.try_get<ConstructionShapeComponent>(child);
            if (shape && shape->recipe == Recipe::Opening) {
                const OpeningParams& o = shape->opening;
                openingRect(host, o.side, o.segment, o.offset, o.sill, o.width, o.height,
                            child == selected ? kHandleColor : kOpeningColor);
            }
        }
    }

    if (m_mode == ToolMode::Select && registry.valid(selected) && registry.all_of<ConstructionShapeComponent>(selected) &&
        HasOwnGeometry(registry.get<ConstructionShapeComponent>(selected))) {
        const Mat4 world = scene.GetWorldTransform(Entity(selected, &scene));
        const auto& shape = registry.get<ConstructionShapeComponent>(selected);
        for (const Handle& handle : HandlesFor(shape)) {
            const Vec3 p = Mat4::TransformPoint(world, handle.local);
            const float distance = vlen(vsub(p, m_lastInput.ray.origin));
            const float s = std::max(0.04f, distance * 0.012f);
            const bool active = m_phase == Phase::Handle && m_handle.axis == handle.axis && m_handle.dir == handle.dir &&
                                m_handle.point == handle.point && m_handle.vertex == handle.vertex;
            const Vec3 a = vscale(Axis((handle.axis + 1) % 3), s);
            const Vec3 b = vscale(Axis((handle.axis + 2) % 3), s);
            Vec3 corners[4] = {vadd(p, vadd(a, b)), vadd(p, vsub(a, b)), vsub(p, vadd(a, b)), vsub(p, vsub(a, b))};
            AddRectLines(lines, corners, active ? kPreviewColor : kHandleColor);
            if (handle.point >= 0 || handle.vertex >= 0) continue;
            lines.push_back({p, vadd(p, vscale(Mat4::TransformDirection(world, vscale(Axis(handle.axis), float(handle.dir))), s * 3.0f)),
                             active ? kPreviewColor : kHandleColor});
        }
    }
}

} // namespace Hephaestus::Construction
