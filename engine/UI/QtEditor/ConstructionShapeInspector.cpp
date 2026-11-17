#if HEPHAESTUS_EDITOR_QT_UI

#include "ConstructionShapeInspector.h"
#include "PropertyEditors.h"

#include "../../Assets/AssetManager.h"
#include "../../Construction/ConstructionBuilding.h"
#include "../../Construction/ConstructionGeometry.h"
#include "../../Construction/ConstructionKit.h"
#include "../../Navigation/NavMesh.h"
#include "../../Construction/ConstructionShape.h"
#include "../../Construction/ConstructionSystem.h"
#include "../../Scene/Entity.h"
#include "../../Scene/Scene.h"

#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <cmath>
#include <QVBoxLayout>
#include <algorithm>
#include <filesystem>

namespace Hephaestus {
namespace QtEditor {
namespace {

using Construction::Operation;
using Construction::Recipe;
using Construction::WallSide;

constexpr double kMaxSize = 10000.0;

QDoubleSpinBox* makeSpin(double minValue, double maxValue, double step)
{
    auto* spin = new QDoubleSpinBox();
    ConfigureMixedSpin(spin, minValue, maxValue, step, 2);
    spin->setSuffix(QStringLiteral(" m"));
    return spin;
}

Recipe PrimaryRecipe(EditorContext* ctx, entt::entity entity)
{
    if (!ctx || !ctx->HasActiveScene()) return Recipe::Box;
    const auto* shape = ctx->GetActiveScene()->GetRegistry().try_get<ConstructionShapeComponent>(entity);
    return shape ? shape->recipe : Recipe::Box;
}

// Whether an opening sits on a wall (placed by segment) rather than a box side.
bool OnWallHost(EditorContext* ctx, entt::entity opening)
{
    if (!ctx || !ctx->HasActiveScene()) return false;
    const entt::registry& registry = ctx->GetActiveScene()->GetRegistry();
    const auto* transform = registry.try_get<TransformComponent>(opening);
    if (!transform || transform->parent == entt::null || !registry.valid(transform->parent)) return false;
    const auto* host = registry.try_get<ConstructionShapeComponent>(transform->parent);
    return host && host->recipe == Recipe::Wall;
}

// Project-relative paths of every kit file in the project.
QStringList ProjectKits()
{
    namespace fs = std::filesystem;
    QStringList kits;
    const fs::path root(AssetManager::GetInstance().GetProjectRoot());
    std::error_code ec;
    if (root.empty() || !fs::exists(root / "assets", ec)) return kits;
    for (fs::recursive_directory_iterator it(root / "assets", fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (it->is_regular_file(ec) && it->path().extension() == ".hpkit") {
            kits.push_back(QString::fromStdString(it->path().lexically_relative(root).generic_string()));
        }
    }
    kits.sort(Qt::CaseInsensitive);
    return kits;
}

} // namespace

ConstructionShapeInspector::ConstructionShapeInspector(EditorContext* ctx, entt::entity entity,
                                                       Construction::ConstructionSystem* system, QWidget* parent)
    : ComponentInspector(tr("Construction"), ctx, entity, parent)
    , m_system(system)
{
    SetTypeName("ConstructionShape");
    buildUI();
    connectSignals();
    Refresh();
}

void ConstructionShapeInspector::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    auto* general = new QFormLayout();
    layout->addLayout(general);
    m_kind = new QLabel();
    general->addRow(tr("Shape"), m_kind);
    m_role = new QLineEdit();
    m_role->setToolTip(tr("What this shape is in the level, such as wall, floor, cover or doorway."));
    general->addRow(tr("Role"), m_role);
    m_operation = new QComboBox();
    m_operation->addItem(tr("Add"), static_cast<int>(Operation::Add));
    m_operation->addItem(tr("Subtract"), static_cast<int>(Operation::Subtract));
    m_operation->setToolTip(tr("Subtract turns the block into a see-through red box that cuts a hole in every block "
                               "and room it overlaps. It has no collision and is hidden in the game."));
    general->addRow(tr("Mode"), m_operation);
    m_generalForm = general;

    m_boxGroup = new QGroupBox(tr("Size"), this);
    auto* box = new QFormLayout(m_boxGroup);
    m_boxForm = box;
    layout->addWidget(m_boxGroup);
    m_width = makeSpin(0.05, kMaxSize, 0.25);
    m_height = makeSpin(0.05, kMaxSize, 0.25);
    m_depth = makeSpin(0.05, kMaxSize, 0.25);
    box->addRow(tr("Width (X)"), m_width);
    box->addRow(tr("Height (Y)"), m_height);
    box->addRow(tr("Depth (Z)"), m_depth);
    m_segments = new QDoubleSpinBox();
    ConfigureMixedSpin(m_segments, 3, 128, 1, 0);
    m_segments->setToolTip(tr("Sides around the shape. More looks rounder and costs more triangles."));
    box->addRow(tr("Segments"), m_segments);
    m_hollow = new QCheckBox(tr("Hollow (room)"));
    m_hollow->setToolTip(tr("Walls of the given thickness around an empty inside."));
    box->addRow(QString(), m_hollow);
    m_closed = new QCheckBox(tr("Closed (room)"));
    m_closed->setToolTip(tr("Joins the last corner back to the first, with a floor inside when Floor is on."));
    box->addRow(QString(), m_closed);
    m_wallInfo = new QLabel();
    m_wallInfo->setWordWrap(true);
    box->addRow(QString(), m_wallInfo);
    m_wallThickness = makeSpin(0.01, 50.0, 0.05);
    box->addRow(tr("Wall Thickness"), m_wallThickness);
    m_floor = new QCheckBox(tr("Floor"));
    m_ceiling = new QCheckBox(tr("Ceiling"));
    box->addRow(QString(), m_floor);
    box->addRow(QString(), m_ceiling);
    m_solid = new QCheckBox(tr("Solid collision"));
    m_solid->setToolTip(tr("Players, physics and the navmesh collide with this shape."));
    box->addRow(QString(), m_solid);

    m_stairsGroup = new QGroupBox(tr("Steps"), this);
    auto* stairs = new QFormLayout(m_stairsGroup);
    m_stairsForm = stairs;
    layout->addWidget(m_stairsGroup);
    m_steps = new QDoubleSpinBox();
    ConfigureMixedSpin(m_steps, 0, 500, 1, 0);
    m_steps->setSpecialValueText(tr("Auto"));
    m_steps->setToolTip(tr("Number of steps. Auto uses the fewest steps no taller than Max Step."));
    stairs->addRow(tr("Steps"), m_steps);
    m_maxRise = makeSpin(0.05, 5.0, 0.01);
    m_maxRise->setToolTip(tr("Tallest step Auto may make. New stairs use just under the player's step height."));
    stairs->addRow(tr("Max Step"), m_maxRise);
    m_turn = new QComboBox();
    m_turn->addItem(tr("Straight"), static_cast<int>(Construction::StairTurn::Straight));
    m_turn->addItem(tr("Quarter turn left"), static_cast<int>(Construction::StairTurn::Left));
    m_turn->addItem(tr("Quarter turn right"), static_cast<int>(Construction::StairTurn::Right));
    m_turn->addItem(tr("U-turn left"), static_cast<int>(Construction::StairTurn::UTurnLeft));
    m_turn->addItem(tr("U-turn right"), static_cast<int>(Construction::StairTurn::UTurnRight));
    m_turn->setToolTip(tr("Turning stairs climb half their steps along one side, cross a landing and climb the rest "
                          "across the far side, or back along the other side for a U-turn."));
    stairs->addRow(tr("Turn"), m_turn);
    m_flightWidth = makeSpin(0.05, kMaxSize, 0.1);
    m_flightWidth->setToolTip(tr("Width of each flight and of the landing."));
    stairs->addRow(tr("Flight Width"), m_flightWidth);
    m_stairsInfo = new QLabel();
    stairs->addRow(QString(), m_stairsInfo);
    m_rampInfo = new QLabel();
    m_rampInfo->setWordWrap(true);
    box->addRow(QString(), m_rampInfo);

    m_openingGroup = new QGroupBox(tr("Opening"), this);
    auto* opening = new QFormLayout(m_openingGroup);
    m_openingForm = opening;
    layout->addWidget(m_openingGroup);
    m_side = new QComboBox();
    m_side->addItem(tr("+X wall"), static_cast<int>(WallSide::PosX));
    m_side->addItem(tr("-X wall"), static_cast<int>(WallSide::NegX));
    m_side->addItem(tr("+Z wall"), static_cast<int>(WallSide::PosZ));
    m_side->addItem(tr("-Z wall"), static_cast<int>(WallSide::NegZ));
    opening->addRow(tr("Wall"), m_side);
    m_openingSegment = new QDoubleSpinBox();
    ConfigureMixedSpin(m_openingSegment, 0, 10000, 1, 0);
    m_openingSegment->setToolTip(tr("Which side of the wall the opening is on, counting from the first corner (0)."));
    opening->addRow(tr("Wall Side"), m_openingSegment);
    m_offset = makeSpin(-kMaxSize, kMaxSize, 0.25);
    m_offset->setToolTip(tr("Distance of the opening's centre from the middle of its wall."));
    opening->addRow(tr("Offset"), m_offset);
    m_sill = makeSpin(0.0, kMaxSize, 0.25);
    m_sill->setToolTip(tr("Height of the bottom of the opening above the base of the room."));
    opening->addRow(tr("Sill"), m_sill);
    m_openingWidth = makeSpin(0.05, kMaxSize, 0.1);
    opening->addRow(tr("Width"), m_openingWidth);
    m_openingHeight = makeSpin(0.05, kMaxSize, 0.1);
    opening->addRow(tr("Height"), m_openingHeight);

    m_facesGroup = new QGroupBox(tr("Painted Faces"), this);
    m_facesGroup->setToolTip(tr("Drag a material from the Assets panel onto a face in the viewport to paint it. "
                                "Hold Shift while dropping to paint the whole shape."));
    m_facesList = new QVBoxLayout(m_facesGroup);
    layout->addWidget(m_facesGroup);

    m_kitGroup = new QGroupBox(tr("Kit"), this);
    m_kitGroup->setToolTip(tr("Dress this wall or room with a kit's prefab pieces. The blockout stays as the "
                              "collision and navmesh shape, and is hidden in the view."));
    auto* kitLayout = new QVBoxLayout(m_kitGroup);
    m_kit = new QComboBox();
    kitLayout->addWidget(m_kit);
    m_kitInfo = new QLabel();
    m_kitInfo->setWordWrap(true);
    kitLayout->addWidget(m_kitInfo);
    m_piece = new QLabel();
    m_piece->setWordWrap(true);
    kitLayout->addWidget(m_piece);
    m_pieceSwap = new QComboBox();
    m_pieceSwap->setToolTip(tr("Pieces of the kit with the same role and width that can stand in this spot."));
    kitLayout->addWidget(m_pieceSwap);
    m_clearSwaps = new QPushButton();
    m_clearSwaps->setToolTip(tr("Put the kit's own choice back in every swapped spot."));
    kitLayout->addWidget(m_clearSwaps);
    layout->addWidget(m_kitGroup);

    auto* storeys = new QHBoxLayout();
    m_addStorey = new QPushButton(tr("Add Storey Above"));
    m_addStorey->setToolTip(tr("Stack a copy of this room on top of it: same footprint, walls and look, with its floor "
                               "on this room's walls. This room's ceiling goes, since the new floor replaces it."));
    m_addStairs = new QPushButton(tr("Add Stairs Up"));
    m_addStairs->setToolTip(tr("Put a flight of stairs along this room's longer inside wall, up to the storey above, "
                               "and cut the stairwell in its floor. Steps follow the player's step height."));
    storeys->addWidget(m_addStorey);
    storeys->addWidget(m_addStairs);
    layout->addLayout(storeys);
    auto* roofs = new QHBoxLayout();
    m_flatRoof = new QPushButton(tr("Flat Roof"));
    m_flatRoof->setToolTip(tr("Put a flat roof on this room: a slab over its walls, overhanging them by 30 cm on a "
                              "room made as a block. Its ceiling goes, since the roof replaces it."));
    m_pitchedRoof = new QPushButton(tr("Pitched Roof"));
    m_pitchedRoof->setToolTip(tr("Put a roof of two 30 degree slopes on this room, the ridge along its longer side and "
                                 "overhanging the walls by 30 cm. For rooms made as blocks."));
    roofs->addWidget(m_flatRoof);
    roofs->addWidget(m_pitchedRoof);
    layout->addLayout(roofs);
    m_addBalcony = new QPushButton(tr("Add Balcony"));
    m_addBalcony->setToolTip(tr("Build a balcony outside this doorway: a 1.5 m deep slab level with the floor, a "
                                "metre wider than the door on each side, with railings on its open edges."));
    layout->addWidget(m_addBalcony);
    m_storeyInfo = new QLabel();
    m_storeyInfo->setWordWrap(true);
    layout->addWidget(m_storeyInfo);
    m_meshInfo = new QLabel();
    m_meshInfo->setWordWrap(true);
    layout->addWidget(m_meshInfo);
    m_makeEditable = new QPushButton(tr("Make Editable"));
    m_makeEditable->setToolTip(tr("Turn this block or ramp into a mesh whose corners you drag one by one in the "
                                  "viewport. Its size settings go away; Ctrl+Z turns it back."));
    layout->addWidget(m_makeEditable);
    m_remove = new QPushButton(tr("Remove Component"));
    layout->addWidget(m_remove);
}

void ConstructionShapeInspector::connectSignals()
{
    auto edited = [this]() {
        if (m_updating) return;
        BeginEdit("Edit construction shape");
        applyToComponent();
        EndEdit();
        Refresh();
    };
    for (QDoubleSpinBox* spin : {m_width, m_height, m_depth, m_wallThickness, m_offset, m_sill, m_openingWidth,
                                 m_openingHeight, m_steps, m_maxRise, m_segments, m_openingSegment, m_flightWidth}) {
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, edited);
    }
    for (QCheckBox* check : {m_hollow, m_floor, m_ceiling, m_solid, m_closed}) {
        connect(check, &QCheckBox::stateChanged, this, edited);
    }
    connect(m_side, qOverload<int>(&QComboBox::currentIndexChanged), this, edited);
    connect(m_operation, qOverload<int>(&QComboBox::currentIndexChanged), this, edited);
    connect(m_turn, qOverload<int>(&QComboBox::currentIndexChanged), this, edited);
    connect(m_role, &QLineEdit::editingFinished, this, edited);
    connect(m_kit, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        if (m_updating) return;
        const std::string kit = m_kit->itemData(index).toString().toStdString();
        BeginEdit(kit.empty() ? "Remove kit" : "Apply kit");
        ForEachTarget<ConstructionShapeComponent>([&](ConstructionShapeComponent& shape, entt::entity) {
            if (shape.recipe == Recipe::Wall || (shape.recipe == Recipe::Box && shape.box.hollow)) shape.kit = kit;
        });
        EndEdit();
        Refresh();
    });
    connect(m_pieceSwap, qOverload<int>(&QComboBox::activated), this, [this](int index) { swapPickedPiece(index); });
    connect(m_clearSwaps, &QPushButton::clicked, this, [this]() {
        BeginEdit("Clear kit swaps");
        ForEachTarget<ConstructionShapeComponent>([](ConstructionShapeComponent& shape, entt::entity) {
            shape.kitSwaps.clear();
        });
        EndEdit();
        Refresh();
    });
    connect(m_flatRoof, &QPushButton::clicked, this, [this]() {
        runBuildingAction("Add roof", [this](Scene& scene, std::string& error) {
            return Construction::AddRoof(scene, GetEntity(), Construction::RoofStyle::Flat, 0.3f, error);
        }, tr("Added a flat roof."));
    });
    connect(m_pitchedRoof, &QPushButton::clicked, this, [this]() {
        runBuildingAction("Add roof", [this](Scene& scene, std::string& error) {
            return Construction::AddRoof(scene, GetEntity(), Construction::RoofStyle::Pitched, 0.3f, error);
        }, tr("Added a pitched roof."));
    });
    connect(m_addBalcony, &QPushButton::clicked, this, [this]() {
        runBuildingAction("Add balcony", [this](Scene& scene, std::string& error) {
            return Construction::AddBalcony(scene, GetEntity(), 1.5f, error);
        }, tr("Added a balcony."));
    });
    connect(m_addStorey, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        Scene& scene = *m_ctx->GetActiveScene();
        BeginEdit("Add storey");
        std::string error;
        const entt::entity storey = Construction::AddStorey(scene, GetEntity(), error);
        EndEdit();
        m_storeyInfo->setText(storey == entt::null ? QString::fromStdString(error)
                                                   : tr("Added a storey. Select it in the Hierarchy (it sits under this "
                                                        "room) to add another on top, or add stairs from here."));
        m_ctx->MarkDirty();
        emit componentRemoved(GetEntity(), QString());
    });
    connect(m_addStairs, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        Scene& scene = *m_ctx->GetActiveScene();
        BeginEdit("Add stairs up");
        std::string error;
        const float maxRise = Navigation::SettingsForScene(scene).agentMaxClimb * 0.95f;
        const entt::entity stairs = Construction::AddStairsUp(scene, GetEntity(), maxRise, error);
        EndEdit();
        m_storeyInfo->setText(stairs == entt::null ? QString::fromStdString(error)
                                                   : tr("Added stairs and a stairwell. Move or resize them like any "
                                                        "other shape."));
        m_ctx->MarkDirty();
        emit componentRemoved(GetEntity(), QString());
    });
    connect(m_makeEditable, &QPushButton::clicked, this, [this]() {
        BeginEdit("Make editable");
        ForEachTarget<ConstructionShapeComponent>([](ConstructionShapeComponent& shape, entt::entity) {
            Construction::MakeEditable(shape);
        });
        EndEdit();
        Refresh();
    });
    connect(m_remove, &QPushButton::clicked, this, [this]() {
        BeginEdit("Remove construction shape");
        ForEachTarget<ConstructionShapeComponent>([&](ConstructionShapeComponent&, entt::entity entity) {
            if (m_ctx && m_ctx->HasActiveScene()) {
                Entity(entity, m_ctx->GetActiveScene()).RemoveComponent<ConstructionShapeComponent>();
            }
        });
        EndEdit();
        emit componentRemoved(GetEntity(), QString::fromStdString(TypeName()));
    });
}

void ConstructionShapeInspector::runBuildingAction(const char* label,
                                                   const std::function<entt::entity(Scene&, std::string&)>& action,
                                                   const QString& done)
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    BeginEdit(label);
    std::string error;
    const entt::entity made = action(*m_ctx->GetActiveScene(), error);
    EndEdit();
    m_storeyInfo->setText(made == entt::null ? QString::fromStdString(error) : done);
    m_storeyInfo->setVisible(true);
    if (made != entt::null) {
        m_ctx->MarkDirty();
        emit componentRemoved(GetEntity(), QString());
    }
}

void ConstructionShapeInspector::applyToComponent()
{
    const Recipe recipe = PrimaryRecipe(m_ctx, GetEntity());
    std::vector<entt::entity> retargeted;
    int operation = 0;
    const bool readOperation = recipe == Recipe::Box && TryReadCombo(m_operation, operation);
    ForEachTarget<ConstructionShapeComponent>([&](ConstructionShapeComponent& shape, entt::entity entity) {
        if (shape.recipe != recipe) return;
        double value = 0.0;
        bool flag = false;
        if (!m_role->text().isEmpty() || shape.role.empty()) shape.role = m_role->text().toStdString();
        if (readOperation && static_cast<int>(shape.operation) != operation) retargeted.push_back(entity);
        if (recipe != Recipe::Opening) {
            if (TryReadSpin(m_segments, value)) shape.segments = static_cast<int>(value);
            if (TryReadSpin(m_width, value)) shape.box.width = static_cast<float>(value);
            if (TryReadSpin(m_height, value)) shape.box.height = static_cast<float>(value);
            if (TryReadSpin(m_depth, value)) shape.box.depth = static_cast<float>(value);
            if (TryReadSpin(m_wallThickness, value)) shape.box.wallThickness = static_cast<float>(value);
            if (TryReadCheck(m_hollow, flag)) shape.box.hollow = flag;
            if (TryReadCheck(m_closed, flag)) shape.wall.closed = flag;
            if (TryReadCheck(m_floor, flag)) shape.box.floor = flag;
            if (TryReadCheck(m_ceiling, flag)) shape.box.ceiling = flag;
            if (TryReadCheck(m_solid, flag)) {
                shape.collision = flag ? Construction::CollisionMode::Solid : Construction::CollisionMode::None;
            }
            if (TryReadSpin(m_steps, value)) shape.stairs.steps = static_cast<int>(value);
            if (TryReadSpin(m_maxRise, value)) shape.stairs.maxRise = static_cast<float>(value);
            if (TryReadSpin(m_flightWidth, value)) shape.stairs.flightWidth = static_cast<float>(value);
            int turn = 0;
            if (TryReadCombo(m_turn, turn)) shape.stairs.turn = static_cast<Construction::StairTurn>(turn);
        } else {
            int side = 0;
            if (TryReadCombo(m_side, side)) shape.opening.side = static_cast<WallSide>(side);
            if (TryReadSpin(m_offset, value)) shape.opening.offset = static_cast<float>(value);
            if (TryReadSpin(m_sill, value)) shape.opening.sill = static_cast<float>(value);
            if (TryReadSpin(m_openingWidth, value)) shape.opening.width = static_cast<float>(value);
            if (TryReadSpin(m_openingHeight, value)) shape.opening.height = static_cast<float>(value);
            if (TryReadSpin(m_openingSegment, value)) shape.opening.segment = static_cast<int>(value);
        }
        Construction::Sanitize(shape);
    });
    if (m_ctx && m_ctx->HasActiveScene()) {
        for (entt::entity entity : retargeted) {
            Construction::ConstructionSystem::SetOperation(*m_ctx->GetActiveScene(), entity,
                                                           static_cast<Operation>(operation));
        }
    }
}

void ConstructionShapeInspector::Refresh()
{
    EndEdit();
    const Recipe recipe = PrimaryRecipe(m_ctx, GetEntity());
    MixedScalar width, height, depth, thickness, offset, sill, openingWidth, openingHeight;
    MixedBool hollow, floor, ceiling, solid, closed;
    MixedScalar openingSegment, corners, turn, flightWidth, slope;
    MixedString role;
    MixedScalar side, steps, maxRise, stepCount, rise, segments, operation;
    QString kind;
    VisitTargets<ConstructionShapeComponent>([&](const ConstructionShapeComponent& shape, entt::entity) {
        if (shape.recipe != recipe) return;
        AccumulateString(role, shape.role);
        AccumulateScalar(segments, shape.segments);
        AccumulateScalar(operation, static_cast<int>(shape.operation));
        kind = kind.isNull() || kind == QString::fromUtf8(Construction::DisplayName(shape))
            ? QString::fromUtf8(Construction::DisplayName(shape)) : QString();
        AccumulateScalar(width, shape.box.width);
        AccumulateScalar(height, shape.box.height);
        AccumulateScalar(depth, shape.box.depth);
        AccumulateScalar(thickness, shape.box.wallThickness);
        AccumulateBool(hollow, shape.box.hollow);
        AccumulateBool(closed, shape.wall.closed);
        AccumulateScalar(corners, static_cast<double>(shape.wall.points.size()));
        AccumulateScalar(openingSegment, shape.opening.segment);
        AccumulateBool(floor, shape.box.floor);
        AccumulateBool(ceiling, shape.box.ceiling);
        AccumulateBool(solid, shape.collision == Construction::CollisionMode::Solid);
        AccumulateScalar(side, static_cast<int>(shape.opening.side));
        AccumulateScalar(offset, shape.opening.offset);
        AccumulateScalar(sill, shape.opening.sill);
        AccumulateScalar(openingWidth, shape.opening.width);
        AccumulateScalar(openingHeight, shape.opening.height);
        AccumulateScalar(steps, shape.stairs.steps);
        AccumulateScalar(maxRise, shape.stairs.maxRise);
        AccumulateScalar(turn, static_cast<int>(shape.stairs.turn));
        AccumulateScalar(flightWidth, shape.stairs.flightWidth);
        AccumulateScalar(slope, std::atan2(shape.box.height, shape.box.depth) * 57.2957795);
        AccumulateScalar(stepCount, Construction::StairCount(shape));
        AccumulateScalar(rise, shape.box.height / std::max(1, Construction::StairCount(shape)));
    });
    if (!width.present) return;

    m_updating = true;
    m_kind->setText(kind.isEmpty() ? tr("Mixed") : kind);
    SetComboFromMixed(m_operation, operation.mixed, static_cast<int>(operation.value));
    m_role->setText(role.mixed ? QString() : QString::fromStdString(role.value));
    m_role->setPlaceholderText(role.mixed ? tr("Mixed") : QString());
    bool editable = false;
    bool convertible = true;
    int meshCorners = 0;
    int meshFaces = 0;
    VisitTargets<ConstructionShapeComponent>([&](const ConstructionShapeComponent& shape, entt::entity) {
        editable = editable || shape.source == Construction::SourceKind::EditableMesh;
        convertible = convertible && Construction::CanMakeEditable(shape);
        meshCorners = static_cast<int>(shape.mesh.vertices.size());
        meshFaces = static_cast<int>(shape.mesh.faces.size());
    });
    m_makeEditable->setVisible(convertible && !editable);
    const auto* primary = m_ctx && m_ctx->HasActiveScene()
        ? m_ctx->GetActiveScene()->GetRegistry().try_get<ConstructionShapeComponent>(GetEntity()) : nullptr;
    const bool room = Targets().size() == 1 && primary && primary->source == Construction::SourceKind::Recipe &&
                      primary->operation == Construction::Operation::Add &&
                      ((primary->recipe == Recipe::Box && primary->box.hollow) ||
                       (primary->recipe == Recipe::Wall && primary->wall.closed && primary->box.floor));
    const bool hasStorey = room && Construction::StoreyAbove(*m_ctx->GetActiveScene(), GetEntity()) != entt::null;
    m_addStorey->setVisible(room);
    m_addStorey->setEnabled(!hasStorey);
    m_addStorey->setToolTip(hasStorey ? tr("This room already has a storey above it.")
                                      : tr("Stack a copy of this room on top of it: same footprint, walls and look, "
                                           "with its floor on this room's walls."));
    m_addStairs->setVisible(room);
    m_addStairs->setEnabled(hasStorey);
    bool roofed = false;
    if (room) {
        for (entt::entity child : m_ctx->GetActiveScene()->GetRegistry().get<TransformComponent>(GetEntity()).children) {
            const auto* childShape = m_ctx->GetActiveScene()->GetRegistry().try_get<ConstructionShapeComponent>(child);
            const auto* childTag = m_ctx->GetActiveScene()->GetRegistry().try_get<TagComponent>(child);
            roofed = roofed || (childShape && childShape->role == "roof") ||
                     (!childShape && childTag && childTag->tag == "Roof");
        }
    }
    m_flatRoof->setVisible(room && !hasStorey && !roofed);
    m_pitchedRoof->setVisible(room && !hasStorey && !roofed && primary->recipe == Recipe::Box);
    const auto* hostTransform = primary && primary->recipe == Recipe::Opening
        ? m_ctx->GetActiveScene()->GetRegistry().try_get<TransformComponent>(GetEntity()) : nullptr;
    const auto* host = hostTransform && hostTransform->parent != entt::null
        ? m_ctx->GetActiveScene()->GetRegistry().try_get<ConstructionShapeComponent>(hostTransform->parent) : nullptr;
    m_addBalcony->setVisible(Targets().size() == 1 && host &&
                             primary->opening.sill <= Construction::HostFloorTop(*host) + 0.05f);
    m_storeyInfo->setVisible(room && !m_storeyInfo->text().isEmpty());
    m_meshInfo->setVisible(editable);
    m_meshInfo->setText(tr("%1 corners, %2 faces. Select the shape and drag a corner's handle in the viewport to move "
                           "it; hold Shift to move it up or down. It collides as the smallest convex shape around its "
                           "corners.").arg(meshCorners).arg(meshFaces));
    m_generalForm->setRowVisible(m_operation, recipe == Recipe::Box && !editable);
    m_boxGroup->setVisible(recipe != Recipe::Opening && !editable);
    const bool wall = recipe == Recipe::Wall;
    m_boxForm->setRowVisible(m_height, recipe != Recipe::Plane);
    m_boxForm->setRowVisible(m_width, !wall);
    m_boxForm->setRowVisible(m_depth, !wall);
    m_boxForm->setRowVisible(m_closed, wall);
    m_boxForm->setRowVisible(m_wallInfo, wall);
    SetCheckFromMixed(m_closed, closed);
    m_wallInfo->setText(corners.mixed ? QString()
                        : tr("%1 corners. Drag the corner handles in the viewport to move them.").arg(static_cast<int>(corners.value)));
    const bool onWall = recipe == Recipe::Opening && OnWallHost(m_ctx, GetEntity());
    m_openingForm->setRowVisible(m_side, !onWall);
    m_openingForm->setRowVisible(m_openingSegment, onWall);
    SetSpinFromMixed(m_openingSegment, openingSegment);
    m_boxForm->setRowVisible(m_segments, recipe == Recipe::Cylinder || recipe == Recipe::Sphere);
    SetSpinFromMixed(m_segments, segments);
    m_stairsGroup->setVisible(recipe == Recipe::Stairs);
    SetComboFromMixed(m_turn, turn.mixed, static_cast<int>(turn.value));
    SetSpinFromMixed(m_flightWidth, flightWidth);
    m_stairsForm->setRowVisible(m_flightWidth, turn.mixed || turn.value != static_cast<int>(Construction::StairTurn::Straight));
    m_boxForm->setRowVisible(m_rampInfo, recipe == Recipe::Ramp);
    m_rampInfo->setText(slope.mixed ? QString()
                        : slope.value > 45.0 ? tr("%1 degree slope: steeper than characters can walk up by default (45).")
                                                  .arg(slope.value, 0, 'f', 1)
                                             : tr("%1 degree slope.").arg(slope.value, 0, 'f', 1));
    const bool subtract = operation.value == static_cast<int>(Operation::Subtract) && !operation.mixed;
    for (QWidget* roomOnly : std::initializer_list<QWidget*>{m_hollow, m_wallThickness, m_floor, m_ceiling}) {
        m_boxForm->setRowVisible(roomOnly, recipe == Recipe::Box && !subtract);
    }
    m_boxForm->setRowVisible(m_wallThickness, (recipe == Recipe::Box && !subtract) || wall);
    m_boxForm->setRowVisible(m_floor, (recipe == Recipe::Box && !subtract) || wall);
    m_boxForm->setRowVisible(m_solid, !(recipe == Recipe::Box && subtract));
    SetSpinFromMixed(m_steps, steps);
    SetSpinFromMixed(m_maxRise, maxRise);
    m_stairsInfo->setText(stepCount.mixed || rise.mixed ? QString()
                          : tr("%1 steps of %2 m").arg(static_cast<int>(stepCount.value)).arg(rise.value, 0, 'f', 3));
    m_openingGroup->setVisible(recipe == Recipe::Opening);
    SetSpinFromMixed(m_width, width);
    SetSpinFromMixed(m_height, height);
    SetSpinFromMixed(m_depth, depth);
    SetSpinFromMixed(m_wallThickness, thickness);
    SetCheckFromMixed(m_hollow, hollow);
    SetCheckFromMixed(m_floor, floor);
    SetCheckFromMixed(m_ceiling, ceiling);
    SetCheckFromMixed(m_solid, solid);
    const bool roomControls = hollow.mixed || hollow.value;
    m_wallThickness->setEnabled(roomControls || wall);
    m_floor->setEnabled(wall ? (closed.mixed || closed.value) : roomControls);
    m_ceiling->setEnabled(roomControls);
    SetComboFromMixed(m_side, side.mixed, static_cast<int>(side.value));
    SetSpinFromMixed(m_offset, offset);
    SetSpinFromMixed(m_sill, sill);
    SetSpinFromMixed(m_openingWidth, openingWidth);
    SetSpinFromMixed(m_openingHeight, openingHeight);
    m_updating = false;
    rebuildFaceList();
    refreshKit(recipe, !hollow.mixed && hollow.value);
}

void ConstructionShapeInspector::refreshKit(Recipe recipe, bool hollow)
{
    const auto* shape = m_ctx && m_ctx->HasActiveScene()
        ? m_ctx->GetActiveScene()->GetRegistry().try_get<ConstructionShapeComponent>(GetEntity()) : nullptr;
    const bool dressable = shape && (recipe == Recipe::Wall || (recipe == Recipe::Box && hollow &&
                                                                shape->operation == Operation::Add));
    m_kitGroup->setVisible(dressable);
    if (!dressable) return;
    m_updating = true;
    m_kit->clear();
    m_kit->addItem(tr("None"), QString());
    const QString current = QString::fromStdString(shape->kit);
    QStringList kits = ProjectKits();
    if (!current.isEmpty() && !kits.contains(current)) kits.prepend(current);
    for (const QString& kit : kits) {
        m_kit->addItem(QFileInfo(kit).completeBaseName(), kit);
        m_kit->setItemData(m_kit->count() - 1, kit, Qt::ToolTipRole);
    }
    m_kit->setCurrentIndex(std::max(0, m_kit->findData(current)));
    m_updating = false;

    Construction::KitSummary summary;
    if (current.isEmpty()) {
        m_kitInfo->setText(kits.isEmpty() ? tr("No kits in this project yet. Kits are .hpkit files in assets.")
                                          : tr("Showing the blockout."));
    } else if (!m_system || !m_system->Kit(GetEntity(), summary)) {
        m_kitInfo->setText(QString());
    } else if (!summary.error.empty()) {
        m_kitInfo->setText(tr("Could not apply the kit: %1").arg(QString::fromStdString(summary.error)));
    } else {
        QString text = tr("%n piece(s)", nullptr, summary.pieces);
        if (summary.fillers > 0) text += tr(", %n filler(s)", nullptr, summary.fillers);
        if (summary.maxStretch > 0.0005f) text += tr(", stretched up to %1%").arg(summary.maxStretch * 100.0f, 0, 'f', 1);
        if (summary.gaps > 0) text += tr(". %n gap(s) the kit can't fill, shown in red.", nullptr, summary.gaps);
        if (summary.staleSwaps > 0) {
            text += tr(". %n swap(s) no longer match a piece in their spot.", nullptr, summary.staleSwaps);
        }
        m_kitInfo->setText(text);
    }
    refreshPickedPiece(*shape);
}

void ConstructionShapeInspector::refreshPickedPiece(const ConstructionShapeComponent& shape)
{
    m_clearSwaps->setVisible(!shape.kitSwaps.empty());
    m_clearSwaps->setText(tr("Clear %n Swap(s)", nullptr, static_cast<int>(shape.kitSwaps.size())));
    m_pickedSlot.clear();
    Construction::KitSlotInfo info;
    if (!shape.kit.empty() && m_system && Targets().size() == 1) {
        m_pickedSlot = m_system->PickedPiece(GetEntity());
    }
    if (m_pickedSlot.empty() || !m_system->KitSlot(GetEntity(), m_pickedSlot, info)) {
        m_pickedSlot.clear();
        m_piece->setText(shape.kit.empty() ? QString() : tr("Click a piece in the viewport to swap it."));
        m_pieceSwap->hide();
        return;
    }
    std::string current = info.kitChoice;
    for (const Construction::KitSwap& swap : shape.kitSwaps) {
        if (swap.slot == m_pickedSlot &&
            std::find(info.choices.begin(), info.choices.end(), swap.prefab) != info.choices.end()) {
            current = swap.prefab;
        }
    }
    m_piece->setText(tr("%1: %2").arg(QString::fromStdString(Construction::DescribeSlot(m_pickedSlot)),
                                      QFileInfo(QString::fromStdString(current)).completeBaseName()));
    m_updating = true;
    m_pieceSwap->clear();
    for (const std::string& choice : info.choices) {
        const QString name = QFileInfo(QString::fromStdString(choice)).completeBaseName();
        m_pieceSwap->addItem(choice == info.kitChoice ? tr("%1 (kit's choice)").arg(name) : name,
                             QString::fromStdString(choice));
    }
    m_pieceSwap->setCurrentIndex(std::max(0, m_pieceSwap->findData(QString::fromStdString(current))));
    m_pieceSwap->setEnabled(info.choices.size() > 1);
    m_pieceSwap->setToolTip(info.choices.size() > 1
        ? tr("Pieces of the kit with the same role and width that can stand in this spot.")
        : tr("The kit has no other piece of this role and width. Add one marked as a variant in the kit editor."));
    m_pieceSwap->show();
    m_updating = false;
}

void ConstructionShapeInspector::swapPickedPiece(int index)
{
    if (m_updating || m_pickedSlot.empty() || !m_system) return;
    Construction::KitSlotInfo info;
    if (!m_system->KitSlot(GetEntity(), m_pickedSlot, info)) return;
    const std::string chosen = m_pieceSwap->itemData(index).toString().toStdString();
    BeginEdit("Swap kit piece");
    ForEachTarget<ConstructionShapeComponent>([&](ConstructionShapeComponent& shape, entt::entity entity) {
        if (entity != GetEntity()) return;
        auto& swaps = shape.kitSwaps;
        swaps.erase(std::remove_if(swaps.begin(), swaps.end(),
                                   [&](const Construction::KitSwap& swap) { return swap.slot == m_pickedSlot; }),
                    swaps.end());
        if (chosen != info.kitChoice) swaps.push_back({m_pickedSlot, chosen});
    });
    EndEdit();
    Refresh();
}

void ConstructionShapeInspector::rebuildFaceList()
{
    const auto* shape = m_ctx && m_ctx->HasActiveScene()
        ? m_ctx->GetActiveScene()->GetRegistry().try_get<ConstructionShapeComponent>(GetEntity()) : nullptr;
    std::vector<Construction::FaceMaterial> faces;
    if (shape && Construction::HasOwnGeometry(*shape)) {
        faces = shape->faceMaterials;
    }
    const bool same = faces.size() == m_shownFaces.size() &&
        std::equal(faces.begin(), faces.end(), m_shownFaces.begin(), [](const auto& a, const auto& b) {
            return Construction::SameFace(a.face, b.face) && a.material == b.material;
        });
    m_facesGroup->setVisible(shape && Construction::HasOwnGeometry(*shape));
    if (same && m_facesList->count() > 0) {
        return;
    }
    m_shownFaces = faces;
    while (QLayoutItem* item = m_facesList->takeAt(0)) {
        if (QWidget* widget = item->widget()) widget->deleteLater();
        if (QLayout* row = item->layout()) {
            while (QLayoutItem* child = row->takeAt(0)) {
                if (QWidget* widget = child->widget()) widget->deleteLater();
                delete child;
            }
        }
        delete item;
    }
    if (faces.empty()) {
        auto* hint = new QLabel(tr("None. Drag a material onto a face in the viewport; Shift paints the whole shape."));
        hint->setWordWrap(true);
        m_facesList->addWidget(hint);
        return;
    }
    for (const Construction::FaceMaterial& face : faces) {
        auto* row = new QHBoxLayout();
        const QString material = QFileInfo(QString::fromStdString(face.material)).completeBaseName();
        auto* label = new QLabel(tr("%1: %2").arg(QString::fromStdString(Construction::DescribeFace(face.face)), material));
        label->setToolTip(QString::fromStdString(face.material));
        auto* clear = new QPushButton(tr("Clear"));
        clear->setToolTip(tr("Give this face the shape's own material again."));
        const Construction::FaceTag tag = face.face;
        connect(clear, &QPushButton::clicked, this, [this, tag]() {
            BeginEdit("Clear painted face");
            ForEachTarget<ConstructionShapeComponent>([&](ConstructionShapeComponent& target, entt::entity) {
                std::erase_if(target.faceMaterials, [&](const auto& painted) {
                    return Construction::SameFace(painted.face, tag);
                });
            });
            EndEdit();
            Refresh();
        });
        row->addWidget(label, 1);
        row->addWidget(clear);
        m_facesList->addLayout(row);
    }
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
