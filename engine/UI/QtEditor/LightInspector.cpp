#if HEPHAESTUS_EDITOR_QT_UI

#include "LightInspector.h"
#include "EditorUndoCommand.h"

#include "../../Scene/Components.h"
#include "../../UI/InspectorSelectionModel.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QGroupBox>

namespace Hephaestus {
namespace QtEditor {

LightInspector::LightInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Light"), ctx, entity, parent)
{
    SetTypeName("Light");
    buildUI();
    connectSignals();
    Refresh();
}

void LightInspector::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 10, 6, 6);
    mainLayout->setSpacing(6);

    auto* general = new QGroupBox(tr("General"), this);
    auto* generalLayout = new QVBoxLayout(general);
    m_enabled = new BoolPropertyEditor(tr("Enabled"), general);
    m_type = new EnumPropertyEditor(tr("Type"), general);
    m_type->addItem(tr("Directional"), static_cast<int>(LightType::Directional));
    m_type->addItem(tr("Point"), static_cast<int>(LightType::Point));
    m_type->addItem(tr("Spot"), static_cast<int>(LightType::Spot));
    m_color = new ColorPropertyEditor(tr("Color"), general);
    m_intensity = new NumericPropertyEditor(tr("Intensity"), general);
    m_intensity->setRange(0.0, 100000.0);
    m_intensity->setDecimals(3);
    m_intensity->setSingleStep(0.1);
    m_range = new NumericPropertyEditor(tr("Range"), general);
    m_range->setRange(0.01, 100000.0);
    m_range->setDecimals(3);
    m_range->setSingleStep(0.5);
    m_innerCone = new NumericPropertyEditor(tr("Inner Cone"), general);
    m_innerCone->setRange(0.0, 180.0);
    m_innerCone->setDecimals(1);
    m_innerCone->setSingleStep(1.0);
    m_innerCone->setSuffix(QString::fromUtf8("\xC2\xB0"));
    m_outerCone = new NumericPropertyEditor(tr("Outer Cone"), general);
    m_outerCone->setRange(0.0, 180.0);
    m_outerCone->setDecimals(1);
    m_outerCone->setSingleStep(1.0);
    m_outerCone->setSuffix(QString::fromUtf8("\xC2\xB0"));
    generalLayout->addWidget(m_enabled);
    generalLayout->addWidget(m_type);
    generalLayout->addWidget(m_color);
    generalLayout->addWidget(m_intensity);
    generalLayout->addWidget(m_range);
    generalLayout->addWidget(m_innerCone);
    generalLayout->addWidget(m_outerCone);
    mainLayout->addWidget(general);

    m_shadowGroup = new QGroupBox(tr("Shadows"), this);
    auto* shadowLayout = new QVBoxLayout(m_shadowGroup);
    m_castShadows = new BoolPropertyEditor(tr("Cast Shadows"), m_shadowGroup);
    m_shadowAutoFit = new BoolPropertyEditor(tr("Auto-Fit"), m_shadowGroup);
    m_shadowBias = new NumericPropertyEditor(tr("Bias"), m_shadowGroup);
    m_shadowBias->setRange(0.0, 0.1);
    m_shadowBias->setDecimals(4);
    m_shadowBias->setSingleStep(0.0001);
    m_shadowNormalBias = new NumericPropertyEditor(tr("Normal Bias"), m_shadowGroup);
    m_shadowNormalBias->setRange(0.0, 1.0);
    m_shadowNormalBias->setDecimals(4);
    m_shadowNormalBias->setSingleStep(0.005);
    m_shadowStrength = new NumericPropertyEditor(tr("Strength"), m_shadowGroup);
    m_shadowStrength->setRange(0.0, 1.0);
    m_shadowStrength->setDecimals(3);
    m_shadowStrength->setSingleStep(0.05);
    m_shadowStrength->setSliderEnabled(true);
    m_shadowMaxDistance = new NumericPropertyEditor(tr("Max Distance"), m_shadowGroup);
    m_shadowMaxDistance->setRange(50.0, 100000.0);
    m_shadowMaxDistance->setDecimals(0);
    m_shadowMaxDistance->setSingleStep(50.0);
    m_shadowDistance = new NumericPropertyEditor(tr("Distance"), m_shadowGroup);
    m_shadowDistance->setRange(10.0, 100000.0);
    m_shadowDistance->setDecimals(0);
    m_shadowDistance->setSingleStep(50.0);
    m_shadowExtent = new NumericPropertyEditor(tr("Extent"), m_shadowGroup);
    m_shadowExtent->setRange(5.0, 100000.0);
    m_shadowExtent->setDecimals(0);
    m_shadowExtent->setSingleStep(50.0);
    shadowLayout->addWidget(m_castShadows);
    shadowLayout->addWidget(m_shadowAutoFit);
    shadowLayout->addWidget(m_shadowBias);
    shadowLayout->addWidget(m_shadowNormalBias);
    shadowLayout->addWidget(m_shadowStrength);
    shadowLayout->addWidget(m_shadowMaxDistance);
    shadowLayout->addWidget(m_shadowDistance);
    shadowLayout->addWidget(m_shadowExtent);
    mainLayout->addWidget(m_shadowGroup);
}

void LightInspector::connectSignals()
{
    auto finish = [this]() { EndEdit(); };
    connect(m_enabled, &BoolPropertyEditor::valueEdited, this, [this](bool v) {
        BeginEdit("Edit light");
        ForEachTarget<LightComponent>([&](LightComponent& lc, entt::entity) { lc.enabled = v; });
        EndEdit();
    });
    connect(m_type, &EnumPropertyEditor::valueEdited, this, [this](int data) {
        BeginEdit("Edit light");
        const auto type = static_cast<LightType>(data);
        ForEachTarget<LightComponent>([&](LightComponent& lc, entt::entity) { lc.type = type; });
        EndEdit();
        updateTypeVisibility();
    });
    connect(m_color, &ColorPropertyEditor::colorEdited, this, [this](const Vec3& rgb) {
        BeginEdit("Edit light");
        ForEachTarget<LightComponent>([&](LightComponent& lc, entt::entity) { lc.color = rgb; });
        EndEdit();
    });
    connect(m_intensity, &NumericPropertyEditor::valueEdited, this, [this](double v) {
        BeginEdit("Edit light");
        ForEachTarget<LightComponent>([&](LightComponent& lc, entt::entity) {
            lc.intensity = static_cast<float>(v);
        });
    });
    connect(m_intensity, &NumericPropertyEditor::editingFinished, this, finish);
    connect(m_range, &NumericPropertyEditor::valueEdited, this, [this](double v) {
        BeginEdit("Edit light");
        ForEachTarget<LightComponent>([&](LightComponent& lc, entt::entity) {
            lc.range = static_cast<float>(v);
        });
    });
    connect(m_range, &NumericPropertyEditor::editingFinished, this, finish);
    connect(m_innerCone, &NumericPropertyEditor::valueEdited, this, [this](double v) {
        BeginEdit("Edit light");
        ForEachTarget<LightComponent>([&](LightComponent& lc, entt::entity) {
            lc.innerConeAngle = static_cast<float>(v) * 0.01745329252f;
        });
    });
    connect(m_innerCone, &NumericPropertyEditor::editingFinished, this, finish);
    connect(m_outerCone, &NumericPropertyEditor::valueEdited, this, [this](double v) {
        BeginEdit("Edit light");
        ForEachTarget<LightComponent>([&](LightComponent& lc, entt::entity) {
            lc.outerConeAngle = static_cast<float>(v) * 0.01745329252f;
        });
    });
    connect(m_outerCone, &NumericPropertyEditor::editingFinished, this, finish);
    connect(m_castShadows, &BoolPropertyEditor::valueEdited, this, [this](bool v) {
        BeginEdit("Edit light");
        ForEachTarget<LightComponent>([&](LightComponent& lc, entt::entity) { lc.castShadows = v; });
        EndEdit();
        m_shadowBias->setEnabled(v);
    });
    connect(m_shadowAutoFit, &BoolPropertyEditor::valueEdited, this, [this](bool v) {
        BeginEdit("Edit light");
        ForEachTarget<LightComponent>([&](LightComponent& lc, entt::entity) { lc.shadowAutoFit = v; });
        EndEdit();
        m_shadowMaxDistance->setVisible(v);
        m_shadowDistance->setVisible(!v);
        m_shadowExtent->setVisible(!v);
    });
    auto writeFloat = [this](NumericPropertyEditor* editor, float LightComponent::* field) {
        connect(editor, &NumericPropertyEditor::valueEdited, this, [this, field](double v) {
            BeginEdit("Edit light");
            ForEachTarget<LightComponent>([&](LightComponent& lc, entt::entity) {
                lc.*field = static_cast<float>(v);
            });
        });
        connect(editor, &NumericPropertyEditor::editingFinished, this, [this]() { EndEdit(); });
    };
    writeFloat(m_shadowBias, &LightComponent::shadowBias);
    writeFloat(m_shadowNormalBias, &LightComponent::shadowNormalBias);
    writeFloat(m_shadowStrength, &LightComponent::shadowStrength);
    writeFloat(m_shadowMaxDistance, &LightComponent::shadowMaxDistance);
    writeFloat(m_shadowDistance, &LightComponent::shadowDistance);
    writeFloat(m_shadowExtent, &LightComponent::shadowExtent);

    connect(m_intensity, &NumericPropertyEditor::resetRequested, this, [this]() {
        m_intensity->setValue(1.0);
    });
    connect(m_range, &NumericPropertyEditor::resetRequested, this, [this]() {
        m_range->setValue(10.0);
    });
}

void LightInspector::Refresh()
{
    EndEdit();
    MixedBool enabled, shadows, autoFit;
    MixedScalar intensity, range, inner, outer, bias, nBias, strength, dist, extent, maxDist;
    MixedScalar typeMix;
    MixedVec3Axes colorMix;
    VisitTargets<LightComponent>([&](LightComponent& lc, entt::entity) {
        AccumulateBool(enabled, lc.enabled);
        AccumulateBool(shadows, lc.castShadows);
        AccumulateBool(autoFit, lc.shadowAutoFit);
        AccumulateScalar(intensity, lc.intensity);
        AccumulateScalar(range, lc.range);
        AccumulateScalar(inner, lc.innerConeAngle * 57.2957795);
        AccumulateScalar(outer, lc.outerConeAngle * 57.2957795);
        AccumulateScalar(bias, lc.shadowBias);
        AccumulateScalar(nBias, lc.shadowNormalBias);
        AccumulateScalar(strength, lc.shadowStrength);
        AccumulateScalar(dist, lc.shadowDistance);
        AccumulateScalar(extent, lc.shadowExtent);
        AccumulateScalar(maxDist, lc.shadowMaxDistance);
        AccumulateScalar(typeMix, static_cast<double>(static_cast<int>(lc.type)));
        AccumulateScalar(colorMix.x, lc.color.x);
        AccumulateScalar(colorMix.y, lc.color.y);
        AccumulateScalar(colorMix.z, lc.color.z);
    });
    m_updating = true;
    m_enabled->setFromMixed(enabled);
    m_type->setFromMixed(typeMix.mixed, static_cast<int>(typeMix.value));
    const bool colorMixed = colorMix.x.mixed || colorMix.y.mixed || colorMix.z.mixed;
    m_color->setLinearRgb(Vec3(static_cast<float>(colorMix.x.value),
                               static_cast<float>(colorMix.y.value),
                               static_cast<float>(colorMix.z.value)), colorMixed);
    m_intensity->setFromMixed(intensity);
    m_range->setFromMixed(range);
    m_innerCone->setFromMixed(inner);
    m_outerCone->setFromMixed(outer);
    m_castShadows->setFromMixed(shadows);
    m_shadowAutoFit->setFromMixed(autoFit);
    m_shadowBias->setFromMixed(bias);
    m_shadowNormalBias->setFromMixed(nBias);
    m_shadowStrength->setFromMixed(strength);
    m_shadowDistance->setFromMixed(dist);
    m_shadowExtent->setFromMixed(extent);
    m_shadowMaxDistance->setFromMixed(maxDist);
    m_updating = false;
    updateTypeVisibility();
    m_shadowMaxDistance->setVisible(!autoFit.mixed && autoFit.value);
    m_shadowDistance->setVisible(!autoFit.mixed && !autoFit.value);
    m_shadowExtent->setVisible(!autoFit.mixed && !autoFit.value);
}

void LightInspector::updateTypeVisibility()
{
    int data = 0;
    const bool hasType = TryReadCombo(m_type->combo(), data);
    const LightType type = hasType ? static_cast<LightType>(data) : LightType::Point;
    const bool pointOrSpot = !hasType || type == LightType::Point || type == LightType::Spot;
    const bool spot = !hasType || type == LightType::Spot;
    m_range->setVisible(pointOrSpot);
    m_innerCone->setVisible(spot);
    m_outerCone->setVisible(spot);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
