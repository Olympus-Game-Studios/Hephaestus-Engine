#if HEPHAESTUS_EDITOR_QT_UI

#include "ProbeVolumeInspector.h"
#include "PropertyEditors.h"

#include "../../Assets/AssetManager.h"
#include "../../Gfx/ProbeBake.h"
#include "../../Scene/Components.h"
#include "../../Scene/Entity.h"
#include "../../Scene/Scene.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {
namespace {

constexpr int kMaxProbesPerAxis = 64;

QDoubleSpinBox* makeSpin(double minValue, double maxValue, double step, int decimals)
{
    auto* spin = new QDoubleSpinBox();
    spin->setRange(minValue, maxValue);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    return spin;
}

} // namespace

ProbeVolumeInspector::ProbeVolumeInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Probe Volume"), ctx, entity, parent)
{
    SetTypeName("ProbeVolume");
    buildUI();
    connectSignals();
    Refresh();
}

void ProbeVolumeInspector::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    auto* box = new QGroupBox(tr("Volume"), this);
    auto* form = new QFormLayout(box);
    layout->addWidget(box);

    m_enabled = new QCheckBox(tr("Enabled"));
    form->addRow(QString(), m_enabled);
    const QString axes[3] = {tr("X"), tr("Y"), tr("Z")};
    for (int i = 0; i < 3; ++i) {
        m_size[i] = makeSpin(0.1, 10000.0, 0.5, 2);
        form->addRow(tr("Size %1").arg(axes[i]), m_size[i]);
    }
    for (int i = 0; i < 3; ++i) {
        m_counts[i] = makeSpin(1, kMaxProbesPerAxis, 1, 0);
        form->addRow(tr("Probes %1").arg(axes[i]), m_counts[i]);
    }
    m_intensity = makeSpin(0.0, 10.0, 0.05, 2);
    form->addRow(tr("Intensity"), m_intensity);
    m_total = new QLabel();
    form->addRow(tr("Total Probes"), m_total);

    auto* bakeBox = new QGroupBox(tr("Bake"), this);
    auto* bakeForm = new QFormLayout(bakeBox);
    layout->addWidget(bakeBox);
    m_bakeStatus = new QLabel();
    m_bakeStatus->setWordWrap(true);
    bakeForm->addRow(tr("Status"), m_bakeStatus);
    m_bake = new QPushButton(tr("Bake Probes"));
    m_bake->setToolTip(tr("Trace what each probe sees in the static meshes and save it next to the scene."));
    bakeForm->addRow(QString(), m_bake);

    m_remove = new QPushButton(tr("Remove Component"));
    layout->addWidget(m_remove);
}

void ProbeVolumeInspector::connectSignals()
{
    auto edited = [this]() {
        if (m_updating) return;
        BeginEdit("Edit Probe Volume");
        applyToComponent();
        EndEdit();
        Refresh();
    };
    connect(m_enabled, &QCheckBox::toggled, this, edited);
    for (int i = 0; i < 3; ++i) {
        connect(m_size[i], qOverload<double>(&QDoubleSpinBox::valueChanged), this, edited);
        connect(m_counts[i], qOverload<double>(&QDoubleSpinBox::valueChanged), this, edited);
    }
    connect(m_intensity, qOverload<double>(&QDoubleSpinBox::valueChanged), this, edited);
    connect(m_bake, &QPushButton::clicked, this, &ProbeVolumeInspector::bakeRequested);
    connect(m_remove, &QPushButton::clicked, this, [this]() {
        ForEachTarget<ProbeVolumeComponent>([&](ProbeVolumeComponent&, entt::entity entity) {
            if (m_ctx && m_ctx->HasActiveScene()) {
                Entity sceneEntity(entity, m_ctx->GetActiveScene());
                sceneEntity.RemoveComponent<ProbeVolumeComponent>();
            }
        });
        emit componentRemoved(GetEntity(), QString::fromStdString(TypeName()));
    });
}

void ProbeVolumeInspector::applyToComponent()
{
    if (m_updating) return;
    ForEachTarget<ProbeVolumeComponent>([&](ProbeVolumeComponent& component, entt::entity) {
        if (m_enabled->checkState() != Qt::PartiallyChecked) component.enabled = m_enabled->isChecked();
        double value = 0.0;
        float* sizes[3] = {&component.size.x, &component.size.y, &component.size.z};
        int* counts[3] = {&component.probeCountX, &component.probeCountY, &component.probeCountZ};
        for (int i = 0; i < 3; ++i) {
            if (TryReadSpin(m_size[i], value)) *sizes[i] = static_cast<float>(value);
            if (TryReadSpin(m_counts[i], value)) *counts[i] = static_cast<int>(value);
        }
        if (TryReadSpin(m_intensity, value)) component.intensity = static_cast<float>(value);
    });
    if (m_ctx) m_ctx->MarkDirty();
}

void ProbeVolumeInspector::Refresh()
{
    EndEdit();
    MixedBool enabled;
    MixedScalar size[3], counts[3], intensity, total;
    VisitTargets<ProbeVolumeComponent>([&](const ProbeVolumeComponent& component, entt::entity) {
        AccumulateBool(enabled, component.enabled);
        AccumulateScalar(size[0], component.size.x);
        AccumulateScalar(size[1], component.size.y);
        AccumulateScalar(size[2], component.size.z);
        AccumulateScalar(counts[0], component.probeCountX);
        AccumulateScalar(counts[1], component.probeCountY);
        AccumulateScalar(counts[2], component.probeCountZ);
        AccumulateScalar(intensity, component.intensity);
        AccumulateScalar(total, component.probeCountX * component.probeCountY * component.probeCountZ);
    });
    if (!intensity.present) return;

    m_updating = true;
    SetCheckFromMixed(m_enabled, enabled);
    for (int i = 0; i < 3; ++i) {
        SetSpinFromMixed(m_size[i], size[i]);
        SetSpinFromMixed(m_counts[i], counts[i]);
    }
    SetSpinFromMixed(m_intensity, intensity);
    m_total->setText(total.mixed ? tr("—") : QString::number(static_cast<int>(total.value)));

    QString status;
    if (Targets().size() > 1) {
        status = tr("Select one volume to see its bake status.");
    } else if (m_ctx && m_ctx->HasActiveScene()) {
        Scene* scene = m_ctx->GetActiveScene();
        const auto* volumePtr = scene->GetRegistry().try_get<ProbeVolumeComponent>(GetEntity());
        if (!volumePtr) {
            status.clear();
        } else if (volumePtr->bakePath.empty()) {
            status = tr("Not baked.");
        } else {
            const ProbeVolumeComponent& volume = *volumePtr;
            const std::filesystem::path file =
                std::filesystem::path(AssetManager::GetInstance().GetProjectRoot()) / volume.bakePath;
            const Mat4 world = scene->GetWorldTransform(Entity(GetEntity(), scene));
            ProbeBake header;
            std::string error;
            if (!ReadProbeBakeHeader(file, header, error)) {
                status = tr("Bake file unreadable: %1").arg(QString::fromStdString(error));
            } else if (!ProbeBakeMatchesVolume(header, Vec3(world.m[12], world.m[13], world.m[14]), volume)) {
                status = tr("Out of date: the volume moved or changed since the last bake.");
            } else {
                status = tr("Baked: %1").arg(QString::fromStdString(volume.bakePath));
            }
        }
    }
    m_bakeStatus->setText(status);
    m_updating = false;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
