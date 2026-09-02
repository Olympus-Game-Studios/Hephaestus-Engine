#if HEPHAESTUS_EDITOR_QT_UI

#include "AudioSourceInspector.h"
#include "EditorUndoCommand.h"

#include "../../Assets/AssetManager.h"
#include "../../Scene/Components.h"
#include "../../UI/InspectorSelectionModel.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <filesystem>

namespace Hephaestus {
namespace QtEditor {

static bool PathMissing(const std::string& portable)
{
    if (portable.empty()) return false;
    auto& assets = AssetManager::GetInstance();
    if (assets.GetAssetInfo(portable)) return false;
    if (portable.rfind("assets/", 0) == 0 && assets.GetAssetInfo(portable.substr(7))) {
        return false;
    }
    const auto resolved = std::filesystem::path(assets.ToAbsolutePath(
        portable.rfind("assets/", 0) == 0 ? portable.substr(7) : portable));
    return !std::filesystem::exists(resolved);
}

AudioSourceInspector::AudioSourceInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Audio Source"), ctx, entity, parent)
{
    SetTypeName("AudioSource");
    buildUI();
    Refresh();
}

void AudioSourceInspector::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 10, 6, 6);
    m_enabled = new BoolPropertyEditor(tr("Enabled"), this);
    m_playOnStart = new BoolPropertyEditor(tr("Play On Start"), this);
    m_loop = new BoolPropertyEditor(tr("Loop"), this);
    m_volume = new NumericPropertyEditor(tr("Volume"), this);
    m_volume->setRange(0.0, 1.0);
    m_volume->setDecimals(3);
    m_volume->setSingleStep(0.05);
    m_volume->setSliderEnabled(true);
    m_clip = new AssetReferenceEditor(tr("Clip"), AssetType::Audio, this);
    layout->addWidget(m_enabled);
    layout->addWidget(m_playOnStart);
    layout->addWidget(m_loop);
    layout->addWidget(m_volume);
    layout->addWidget(m_clip);

    connect(m_enabled, &BoolPropertyEditor::valueEdited, this, [this](bool v) {
        BeginEdit("Edit AudioSource");
        ForEachTarget<AudioSourceComponent>([&](AudioSourceComponent& audio, entt::entity) {
            audio.enabled = v;
        });
        EndEdit();
    });
    connect(m_playOnStart, &BoolPropertyEditor::valueEdited, this, [this](bool v) {
        BeginEdit("Edit AudioSource");
        ForEachTarget<AudioSourceComponent>([&](AudioSourceComponent& audio, entt::entity) {
            audio.playOnStart = v;
        });
        EndEdit();
    });
    connect(m_loop, &BoolPropertyEditor::valueEdited, this, [this](bool v) {
        BeginEdit("Edit AudioSource");
        ForEachTarget<AudioSourceComponent>([&](AudioSourceComponent& audio, entt::entity) {
            audio.loop = v;
        });
        EndEdit();
    });
    connect(m_volume, &NumericPropertyEditor::valueEdited, this, [this](double v) {
        BeginEdit("Edit AudioSource");
        ForEachTarget<AudioSourceComponent>([&](AudioSourceComponent& audio, entt::entity) {
            audio.volume = static_cast<float>(v);
        });
    });
    connect(m_volume, &NumericPropertyEditor::editingFinished, this, [this]() { EndEdit(); });
    connect(m_clip, &AssetReferenceEditor::pathEdited, this, [this](const QString& path) {
        BeginEdit("Assign audio clip");
        ForEachTarget<AudioSourceComponent>([&](AudioSourceComponent& audio, entt::entity) {
            audio.clipPath = path.toStdString();
        });
        EndEdit();
    });
}

void AudioSourceInspector::Refresh()
{
    EndEdit();
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    MixedBool enabled, play, loop;
    MixedScalar volume;
    MixedString clip;
    VisitTargets<AudioSourceComponent>([&](AudioSourceComponent& audio, entt::entity) {
        AccumulateBool(enabled, audio.enabled);
        AccumulateBool(play, audio.playOnStart);
        AccumulateBool(loop, audio.loop);
        AccumulateScalar(volume, audio.volume);
        AccumulateString(clip, audio.clipPath);
    });
    m_updating = true;
    m_enabled->setFromMixed(enabled);
    m_playOnStart->setFromMixed(play);
    m_loop->setFromMixed(loop);
    m_volume->setFromMixed(volume);
    m_clip->setPath(QString::fromStdString(clip.value), clip.mixed, PathMissing(clip.value));
    m_updating = false;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
