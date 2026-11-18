#if HEPHAESTUS_EDITOR_QT_UI

#include "ParticleEmitterInspector.h"
#include "EditorUndoCommand.h"

#include "../../Scene/Components.h"
#include "../../UI/InspectorSelectionModel.h"

#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {

ParticleEmitterInspector::ParticleEmitterInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Particle Emitter"), ctx, entity, parent)
{
    SetTypeName("ParticleEmitter");
    buildUI();
    Refresh();
}

void ParticleEmitterInspector::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 10, 6, 6);

    m_material = new AssetReferenceEditor(tr("Material"), AssetType::Material, this);
    m_playOnStart = new BoolPropertyEditor(tr("Play On Start"), this);
    m_loop = new BoolPropertyEditor(tr("Loop"), this);
    m_destroyWhenDone = new BoolPropertyEditor(tr("Destroy When Done"), this);
    layout->addWidget(m_material);
    layout->addWidget(m_playOnStart);
    layout->addWidget(m_loop);
    layout->addWidget(m_destroyWhenDone);

    struct Spec {
        const char* label;
        float ParticleEmitterComponent::* member;
        int ParticleEmitterComponent::* intMember;
        double min, max, step;
        int decimals;
    };
    const Spec specs[] = {
        {"Duration (s)", &ParticleEmitterComponent::duration, nullptr, 0.01, 600.0, 0.1, 2},
        {"Rate (per s)", &ParticleEmitterComponent::rate, nullptr, 0.0, 10000.0, 1.0, 1},
        {"Burst", nullptr, &ParticleEmitterComponent::burst, 0.0, 10000.0, 1.0, 0},
        {"Max Particles", nullptr, &ParticleEmitterComponent::maxParticles, 0.0, 10000.0, 10.0, 0},
        {"Lifetime Min (s)", &ParticleEmitterComponent::lifetimeMin, nullptr, 0.0, 600.0, 0.1, 2},
        {"Lifetime Max (s)", &ParticleEmitterComponent::lifetimeMax, nullptr, 0.0, 600.0, 0.1, 2},
        {"Speed Min (m/s)", &ParticleEmitterComponent::speedMin, nullptr, 0.0, 1000.0, 0.1, 2},
        {"Speed Max (m/s)", &ParticleEmitterComponent::speedMax, nullptr, 0.0, 1000.0, 0.1, 2},
        {"Spread (deg)", &ParticleEmitterComponent::spreadDegrees, nullptr, 0.0, 180.0, 1.0, 1},
        {"Gravity", &ParticleEmitterComponent::gravity, nullptr, -10.0, 10.0, 0.1, 2},
        {"Drag", &ParticleEmitterComponent::drag, nullptr, 0.0, 100.0, 0.1, 2},
        {"Size Start (m)", &ParticleEmitterComponent::sizeStart, nullptr, 0.0, 100.0, 0.01, 3},
        {"Size End (m)", &ParticleEmitterComponent::sizeEnd, nullptr, 0.0, 100.0, 0.01, 3},
        {"Size Jitter", &ParticleEmitterComponent::sizeJitter, nullptr, 0.0, 1.0, 0.05, 2},
        {"Spin Min (deg/s)", &ParticleEmitterComponent::spinMin, nullptr, -3600.0, 3600.0, 10.0, 1},
        {"Spin Max (deg/s)", &ParticleEmitterComponent::spinMax, nullptr, -3600.0, 3600.0, 10.0, 1},
    };
    for (const Spec& spec : specs) {
        NumberField field;
        field.editor = new NumericPropertyEditor(tr(spec.label), this);
        field.editor->setRange(spec.min, spec.max);
        field.editor->setSingleStep(spec.step);
        field.editor->setDecimals(spec.decimals);
        field.member = spec.member;
        field.intMember = spec.intMember;
        layout->addWidget(field.editor);
        connect(field.editor, &NumericPropertyEditor::valueEdited, this, [this, field](double v) {
            BeginEdit("Edit ParticleEmitter");
            ForEachTarget<ParticleEmitterComponent>([&](ParticleEmitterComponent& emitter, entt::entity) {
                if (field.member) emitter.*field.member = static_cast<float>(v);
                else emitter.*field.intMember = static_cast<int>(v);
            });
        });
        connect(field.editor, &NumericPropertyEditor::editingFinished, this, [this]() { EndEdit(); });
        m_numbers.push_back(field);
    }

    m_colorStart = new ColorRgbaPropertyEditor(tr("Color Start"), this);
    m_colorEnd = new ColorRgbaPropertyEditor(tr("Color End"), this);
    layout->addWidget(m_colorStart);
    layout->addWidget(m_colorEnd);

    auto boolEdit = [this](BoolPropertyEditor* editor, bool ParticleEmitterComponent::* member) {
        connect(editor, &BoolPropertyEditor::valueEdited, this, [this, member](bool v) {
            BeginEdit("Edit ParticleEmitter");
            ForEachTarget<ParticleEmitterComponent>([&](ParticleEmitterComponent& emitter, entt::entity) {
                emitter.*member = v;
            });
            EndEdit();
        });
    };
    boolEdit(m_playOnStart, &ParticleEmitterComponent::playOnStart);
    boolEdit(m_loop, &ParticleEmitterComponent::loop);
    boolEdit(m_destroyWhenDone, &ParticleEmitterComponent::destroyWhenDone);

    auto colorEdit = [this](ColorRgbaPropertyEditor* editor, Vec4 ParticleEmitterComponent::* member) {
        connect(editor, &ColorRgbaPropertyEditor::valueEdited, this, [this, member](const Vec4& v) {
            BeginEdit("Edit ParticleEmitter");
            ForEachTarget<ParticleEmitterComponent>([&](ParticleEmitterComponent& emitter, entt::entity) {
                emitter.*member = v;
            });
        });
        connect(editor, &ColorRgbaPropertyEditor::editingFinished, this, [this]() { EndEdit(); });
    };
    colorEdit(m_colorStart, &ParticleEmitterComponent::colorStart);
    colorEdit(m_colorEnd, &ParticleEmitterComponent::colorEnd);

    connect(m_material, &AssetReferenceEditor::pathEdited, this, [this](const QString& path) {
        BeginEdit("Assign particle material");
        ForEachTarget<ParticleEmitterComponent>([&](ParticleEmitterComponent& emitter, entt::entity) {
            emitter.materialPath = path.toStdString();
        });
        EndEdit();
    });
}

void ParticleEmitterInspector::Refresh()
{
    EndEdit();
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    MixedBool play, loop, destroy;
    MixedString material;
    std::vector<MixedScalar> numbers(m_numbers.size());
    bool colorsMixed = false;
    bool first = true;
    Vec4 colorStart, colorEnd;
    VisitTargets<ParticleEmitterComponent>([&](ParticleEmitterComponent& emitter, entt::entity) {
        AccumulateBool(play, emitter.playOnStart);
        AccumulateBool(loop, emitter.loop);
        AccumulateBool(destroy, emitter.destroyWhenDone);
        AccumulateString(material, emitter.materialPath);
        for (size_t i = 0; i < m_numbers.size(); ++i) {
            const NumberField& field = m_numbers[i];
            AccumulateScalar(numbers[i], field.member ? emitter.*field.member
                                                      : static_cast<float>(emitter.*field.intMember));
        }
        if (first) {
            colorStart = emitter.colorStart;
            colorEnd = emitter.colorEnd;
            first = false;
        } else {
            auto differs = [](const Vec4& a, const Vec4& b) {
                return a.x != b.x || a.y != b.y || a.z != b.z || a.w != b.w;
            };
            colorsMixed = colorsMixed || differs(colorStart, emitter.colorStart) || differs(colorEnd, emitter.colorEnd);
        }
    });
    m_updating = true;
    m_playOnStart->setFromMixed(play);
    m_loop->setFromMixed(loop);
    m_destroyWhenDone->setFromMixed(destroy);
    m_material->setPath(QString::fromStdString(material.present ? material.value : std::string()), material.mixed,
                        false);
    for (size_t i = 0; i < m_numbers.size(); ++i) {
        m_numbers[i].editor->setFromMixed(numbers[i]);
    }
    m_colorStart->setValue(colorStart, colorsMixed);
    m_colorEnd->setValue(colorEnd, colorsMixed);
    m_updating = false;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
