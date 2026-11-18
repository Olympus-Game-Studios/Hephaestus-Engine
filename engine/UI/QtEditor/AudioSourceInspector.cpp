#if HEPHAESTUS_EDITOR_QT_UI

#include "AudioSourceInspector.h"
#include "EditorUndoCommand.h"

#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetPathPolicy.h"
#include "../../Audio/AudioEngine.h"
#include "../../Scene/Components.h"
#include "../../UI/InspectorSelectionModel.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <algorithm>
#include <filesystem>

namespace Hephaestus {
namespace QtEditor {

static bool PathMissing(const std::string& portable)
{
    if (portable.empty()) return false;
    if (portable.rfind("builtin://", 0) == 0) return false;

    auto& assets = AssetManager::GetInstance();
    if (assets.GetAssetInfo(portable)) return false;
    if (portable.rfind("assets/", 0) == 0 && assets.GetAssetInfo(portable.substr(7))) {
        return false;
    }

    const std::filesystem::path projectRoot(assets.GetProjectRoot());
    if (!projectRoot.empty()) {
        const auto resolved = AssetPathPolicy::ResolvePortableKey(projectRoot, portable);
        if (!resolved.empty() && std::filesystem::exists(resolved)) {
            return false;
        }
    }

    const auto assetResolved = std::filesystem::path(assets.ToAbsolutePath(
        portable.rfind("assets/", 0) == 0 ? portable.substr(7) : portable));
    return assetResolved.empty() || !std::filesystem::exists(assetResolved);
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
    m_bus = new EnumPropertyEditor(tr("Bus"), this);
    const auto& buses = AudioEngine::BusNames();
    for (int i = 1; i < static_cast<int>(buses.size()); ++i) {
        m_bus->addItem(QString::fromStdString(buses[static_cast<size_t>(i)]), i);
    }
    m_spatial = new BoolPropertyEditor(tr("3D"), this);
    m_minDistance = new NumericPropertyEditor(tr("Min Distance"), this);
    m_minDistance->setRange(0.01, 1000.0);
    m_minDistance->setDecimals(2);
    m_minDistance->setSingleStep(0.5);
    m_maxDistance = new NumericPropertyEditor(tr("Max Distance"), this);
    m_maxDistance->setRange(0.1, 5000.0);
    m_maxDistance->setDecimals(1);
    m_maxDistance->setSingleStep(5.0);
    layout->addWidget(m_enabled);
    layout->addWidget(m_playOnStart);
    layout->addWidget(m_loop);
    layout->addWidget(m_volume);
    layout->addWidget(m_clip);
    layout->addWidget(m_bus);
    layout->addWidget(m_spatial);
    layout->addWidget(m_minDistance);
    layout->addWidget(m_maxDistance);

    connect(m_bus, &EnumPropertyEditor::valueEdited, this, [this](int index) {
        const auto& names = AudioEngine::BusNames();
        if (index < 1 || index >= static_cast<int>(names.size())) return;
        BeginEdit("Edit AudioSource");
        ForEachTarget<AudioSourceComponent>([&](AudioSourceComponent& audio, entt::entity) {
            audio.bus = names[static_cast<size_t>(index)];
        });
        EndEdit();
    });
    connect(m_spatial, &BoolPropertyEditor::valueEdited, this, [this](bool v) {
        BeginEdit("Edit AudioSource");
        ForEachTarget<AudioSourceComponent>([&](AudioSourceComponent& audio, entt::entity) {
            audio.spatial = v;
        });
        EndEdit();
    });
    connect(m_minDistance, &NumericPropertyEditor::valueEdited, this, [this](double v) {
        BeginEdit("Edit AudioSource");
        ForEachTarget<AudioSourceComponent>([&](AudioSourceComponent& audio, entt::entity) {
            audio.minDistance = static_cast<float>(v);
        });
    });
    connect(m_minDistance, &NumericPropertyEditor::editingFinished, this, [this]() { EndEdit(); });
    connect(m_maxDistance, &NumericPropertyEditor::valueEdited, this, [this](double v) {
        BeginEdit("Edit AudioSource");
        ForEachTarget<AudioSourceComponent>([&](AudioSourceComponent& audio, entt::entity) {
            audio.maxDistance = static_cast<float>(v);
        });
    });
    connect(m_maxDistance, &NumericPropertyEditor::editingFinished, this, [this]() { EndEdit(); });

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
    MixedBool enabled, play, loop, spatial;
    MixedScalar volume, minDistance, maxDistance;
    MixedString clip, bus;
    VisitTargets<AudioSourceComponent>([&](AudioSourceComponent& audio, entt::entity) {
        AccumulateBool(spatial, audio.spatial);
        AccumulateScalar(minDistance, audio.minDistance);
        AccumulateScalar(maxDistance, audio.maxDistance);
        AccumulateString(bus, audio.bus);
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
    m_spatial->setFromMixed(spatial);
    m_minDistance->setFromMixed(minDistance);
    m_maxDistance->setFromMixed(maxDistance);
    const auto& busNames = AudioEngine::BusNames();
    const auto busIt = std::find(busNames.begin(), busNames.end(), bus.present ? bus.value : std::string("sfx"));
    m_bus->setFromMixed(bus.mixed, busIt == busNames.end() ? 1 : static_cast<int>(busIt - busNames.begin()));
    const std::string clipPath = clip.present ? clip.value : std::string();
    m_clip->setPath(QString::fromStdString(clipPath), clip.mixed, PathMissing(clipPath));
    m_updating = false;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
