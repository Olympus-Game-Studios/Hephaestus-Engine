#pragma once

#include "ConstructionSystem.h"

#include "../Analysis/LevelChecks.h"
#include "../Core/EditorDebugDraw.h"
#include "../Core/Math.h"

#include <entt/entt.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Scene;

namespace Hephaestus::Construction {

enum class ToolMode { Select, DrawBox, DrawRoom, DrawStairs, DrawOpening, DrawWall, DrawRamp, MeasureWalk };

struct ToolInput {
    Ray ray;
    float screenX = 0.0f;  // swapchain pixels
    float screenY = 0.0f;
    Mat4 viewProj = Mat4::Identity();
    float viewportWidth = 1.0f;
    float viewportHeight = 1.0f;
    bool shift = false;
    bool ctrl = false;   // disables snapping
    bool alt = false;
    float gridStep = 0.25f;
};

// Hooks into the editor that owns the scene.
struct ToolHost {
    std::function<void(const std::string& label)> pushUndo;  // called once before each change
    std::function<void()> discardUndo;                       // when a change is cancelled before it was made
    std::function<void(entt::entity)> select;
    std::function<void(entt::entity)> created;               // a new box was drawn, before it is selected
    std::function<float()> stepHeight;                       // the player's step height, for new stairs
    std::function<void()> changed;                           // scene structure or data changed
    std::function<entt::entity()> selected;
    std::function<std::filesystem::path()> projectRoot;      // where model files are found, for the navmesh
};

// Viewport interaction for construction shapes: drawing boxes, rooms, walls and openings, and dragging face and point
// handles of the selected shape. Mouse handlers return true when they used the event, so the caller skips its own handling.
class ConstructionTool {
public:
    void SetHost(ToolHost host) { m_host = std::move(host); }
    void SetMode(ToolMode mode);
    ToolMode Mode() const { return m_mode; }
    bool IsBusy() const { return m_phase != Phase::Idle; }

    bool OnPress(Scene& scene, ConstructionSystem& system, const ToolInput& input);
    void OnMove(Scene& scene, ConstructionSystem& system, const ToolInput& input);
    bool OnRelease(Scene& scene, ConstructionSystem& system, const ToolInput& input);
    // Qt key codes: digits, '.', ',', '-', Backspace, Enter and Escape while a shape is being drawn.
    bool OnKey(Scene& scene, ConstructionSystem& system, int qtKey);
    void Cancel(Scene& scene);

    void AppendOverlay(Scene& scene, const ConstructionSystem& system, std::vector<EditorDebugLine>& lines,
                       std::vector<EditorDebugPoint>& points) const;

    // What the tool is doing right now, for the viewport hint strip.
    std::string StatusText() const;

private:
    enum class Phase { Idle, Base, Height, Handle, OpeningDraw, OpeningMove, Path };

    struct Handle {
        int axis = 0;   // 0 x, 1 y (top), 2 z
        int dir = 1;
        Vec3 local;
        int point = -1;  // a wall path point, dragged on the ground plane
    };

    std::vector<Handle> HandlesFor(const ConstructionShapeComponent& shape) const;
    bool PickHandle(Scene& scene, const ToolInput& input, entt::entity& outEntity, Handle& outHandle) const;
    bool PlacementPoint(Scene& scene, ConstructionSystem& system, const ToolInput& input, Vec3& out) const;
    void UpdateBase(const ToolInput& input);
    void UpdateHeight(const ToolInput& input);
    void CommitBox(Scene& scene);
    void UpdatePath(const ToolInput& input);
    bool PathCanClose(const ToolInput& input) const;
    void CommitWall(Scene& scene, bool closed);
    float DefaultHeight() const;
    void UpdateHandleDrag(Scene& scene, const ToolInput& input);
    bool WallPointFromRay(Scene& scene, const ToolInput& input, Vec3& outLocal) const;
    // The opening whose outline on its wall's outer face is under the ray (the ray passes through the hole itself).
    bool PickOpening(Scene& scene, const Ray& ray, entt::entity& outOpening, Vec3& outLocal) const;
    void UpdateOpeningDraw(Scene& scene, const ToolInput& input);
    void UpdateOpeningMove(Scene& scene, const ToolInput& input);
    void CommitOpening(Scene& scene);
    float Snap(float value, const ToolInput& input) const;
    bool ApplyTyped(Scene& scene, ConstructionSystem& system);
    bool PressMeasure(Scene& scene, ConstructionSystem& system, const ToolInput& input);
    std::string MeasureStatus() const;

    ToolHost m_host;
    ToolMode m_mode = ToolMode::Select;
    Phase m_phase = Phase::Idle;
    std::string m_typed;

    // Hover feedback.
    bool m_hoverValid = false;
    Vec3 m_hoverPoint;
    entt::entity m_hoverHost = entt::null;
    WallSide m_hoverWall = WallSide::PosZ;
    int m_hoverSegment = 0;
    Vec3 m_hoverLocal;

    // Box drawing, in world space on a horizontal plane at m_anchor.y.
    Vec3 m_anchor;
    Vec3 m_corner;
    float m_height = 0.0f;
    ToolInput m_lastInput;

    // Wall drawing: the corners placed so far, in world space; m_corner follows the cursor.
    std::vector<Vec3> m_path;

    // Handle drag.
    entt::entity m_target = entt::null;
    Handle m_handle;
    BoxParams m_startBox;
    WallParams m_startWall;
    Vec3 m_startPosition;
    Vec3 m_handleWorldOrigin;
    Vec3 m_handleWorldAxis;
    float m_handleStartT = 0.0f;

    // Opening drawing and moving, in the host's local space.
    WallSide m_wall = WallSide::PosZ;
    int m_segment = 0;
    Vec3 m_planePoint;    // a point on the wall face that was pressed
    Vec3 m_wallStart;     // (u, v) at press, in x and y
    Vec3 m_wallCurrent;
    entt::entity m_opening = entt::null;
    OpeningParams m_startOpening;

    // Measure Walk: 0 nothing placed, 1 the start placed and the end following the cursor, 2 both placed.
    int m_measure = 0;
    Vec3 m_measureFrom;
    Vec3 m_measureTo;
    std::shared_ptr<Navigation::NavMesh> m_walkNav;
    Analysis::TraversalMetrics m_walkMetrics;
    Analysis::WalkRoute m_walkRoute;
    std::string m_walkError;
};

// Coordinates of a host-local point on a wall: x is distance along the wall from its centre, y is height.
Vec3 WallCoordinates(const BoxParams& box, WallSide side, const Vec3& local);

} // namespace Hephaestus::Construction
