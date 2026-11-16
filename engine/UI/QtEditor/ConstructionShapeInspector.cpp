#if HEPHAESTUS_EDITOR_QT_UI

#include "ConstructionShapeInspector.h"
#include "PropertyEditors.h"

#include "../../Construction/ConstructionShape.h"
#include "../../Construction/ConstructionSystem.h"
#include "../../Scene/Entity.h"
#include "../../Scene/Scene.h"

#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <cmath>
#include <QVBoxLayout>

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

} // namespace

ConstructionShapeInspector::ConstructionShapeInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Construction"), ctx, entity, parent)
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
    m_generalForm->setRowVisible(m_operation, recipe == Recipe::Box);
    SetComboFromMixed(m_operation, operation.mixed, static_cast<int>(operation.value));
    m_role->setText(role.mixed ? QString() : QString::fromStdString(role.value));
    m_role->setPlaceholderText(role.mixed ? tr("Mixed") : QString());
    m_boxGroup->setVisible(recipe != Recipe::Opening);
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
