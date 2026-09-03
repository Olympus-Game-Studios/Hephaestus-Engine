#if HEPHAESTUS_EDITOR_QT_UI

#include "CharacterInspector.h"

#include "EditorUndoCommand.h"
#include "PropertyEditors.h"

#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetPathPolicy.h"
#include "../../Assets/CharacterRuntime.h"
#include "../../Core/EditorContext.h"
#include "../../Scene/CharacterComponent.h"
#include "../../Scene/Entity.h"
#include "../../Scene/Scene.h"
#include "../../Scripting/LuaExposeScanner.h"
#include "../../Scripting/LuaScriptComponent.h"
#include "../../Scripting/LuaScriptVariableReconciler.h"

#include <QFormLayout>
#include <QFont>
#include <QLabel>
#include <QMessageBox>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Hephaestus {
namespace QtEditor {
namespace {

std::filesystem::path ResolveScriptPath(const std::string& portable)
{
    auto& assets = AssetManager::GetInstance();
    std::filesystem::path path(portable);
    if (path.is_absolute()) {
        return path;
    }
    if (!assets.GetProjectRoot().empty()) {
        const auto resolved =
            AssetPathPolicy::ResolvePortableKey(assets.GetProjectRoot(), portable);
        if (!resolved.empty()) {
            return resolved;
        }
    }
    return assets.ToAbsolutePath(portable.rfind("assets/", 0) == 0 ? portable.substr(7) : portable);
}

LuaExposeScanResult ScanController(const std::string& portable)
{
    if (portable.empty()) {
        return LuaExposeScanner::Scan({});
    }
    std::ifstream file(ResolveScriptPath(portable), std::ios::binary);
    if (!file) {
        LuaExposeScanResult result;
        result.valid = false;
        result.diagnostics.push_back({0, "Controller script could not be opened.", false});
        return result;
    }
    std::ostringstream source;
    source << file.rdbuf();
    return LuaExposeScanner::Scan(source.str());
}

bool ClipExists(const CharacterAsset& asset, const std::string& id)
{
    if (id.empty()) {
        return false;
    }
    for (const CharacterClipEntry& clip : asset.clips) {
        if (clip.id == id || clip.sourceClip == id) {
            return true;
        }
    }
    return false;
}

} // namespace

CharacterInspector::CharacterInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Character"), ctx, entity, parent)
{
    SetTypeName("Character");
    buildUI();
    Refresh();
}

void CharacterInspector::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 10, 6, 6);

    m_assetEdit = new AssetReferenceEditor(tr("Asset"), AssetType::Character, this);
    m_assetEdit->setAllowEmpty(true);
    layout->addWidget(m_assetEdit);

    auto* clipRow = new QWidget(this);
    auto* clipForm = new QFormLayout(clipRow);
    clipForm->setContentsMargins(0, 0, 0, 0);
    m_clipCombo = new QComboBox(clipRow);
    m_clipHint = new QLabel(clipRow);
    m_clipHint->setWordWrap(true);
    m_clipHint->setStyleSheet(QStringLiteral("color: #c9a227;"));
    clipForm->addRow(tr("Default Animation:"), m_clipCombo);
    clipForm->addRow(QString(), m_clipHint);
    layout->addWidget(clipRow);

    m_speed = new NumericPropertyEditor(tr("Playback Speed"), this);
    m_speed->setRange(0.01, 20.0);
    m_speed->setDecimals(3);
    m_speed->setSingleStep(0.05);
    layout->addWidget(m_speed);

    m_runController = new BoolPropertyEditor(tr("Run Controller"), this);
    layout->addWidget(m_runController);
    m_controllerEdit = new AssetReferenceEditor(tr("Controller Script"), AssetType::Script, this);
    m_controllerEdit->setAllowEmpty(true);
    layout->addWidget(m_controllerEdit);
    m_controllerNote = new QLabel(
        tr("Character controller scripts run in Play Mode and HephaestusGame when Run Controller is enabled."),
        this);
    m_controllerNote->setWordWrap(true);
    m_controllerNote->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
    layout->addWidget(m_controllerNote);

    m_parametersGroup = new QGroupBox(tr("Controller Parameters"), this);
    m_parametersLayout = new QVBoxLayout(m_parametersGroup);
    layout->addWidget(m_parametersGroup);

    m_materialsGroup = new QGroupBox(tr("Materials"), this);
    m_materialsLayout = new QVBoxLayout(m_materialsGroup);
    layout->addWidget(m_materialsGroup);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    m_advanced = new QLabel(this);
    m_advanced->setWordWrap(true);
    m_advanced->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
    layout->addWidget(m_advanced);

    connect(m_assetEdit, &AssetReferenceEditor::pathEdited, this, [this](const QString& path) {
        if (m_updating) {
            return;
        }
        applyCharacterPath(path);
    });
    connect(m_clipCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updating) {
            return;
        }
        const std::string id = m_clipCombo->currentData().toString().toStdString();
        BeginEdit("Edit Character animation");
        ForEachTarget<CharacterComponent>([&](CharacterComponent& character, entt::entity) {
            character.defaultAnimation = id;
        });
        EndEdit();
        if (m_ctx) {
            m_ctx->MarkDirty();
        }
    });
    connect(m_speed, &NumericPropertyEditor::valueEdited, this, [this](double value) {
        BeginEdit("Edit Character speed");
        ForEachTarget<CharacterComponent>([&](CharacterComponent& character, entt::entity) {
            character.playbackSpeed = std::max(0.01f, static_cast<float>(value));
        });
    });
    connect(m_speed, &NumericPropertyEditor::editingFinished, this, [this]() {
        EndEdit();
        if (m_ctx) {
            m_ctx->MarkDirty();
        }
        rebindTargets(false);
    });
    connect(m_runController, &BoolPropertyEditor::valueEdited, this, [this](bool value) {
        BeginEdit("Edit Character controller");
        ForEachTarget<CharacterComponent>([&](CharacterComponent& character, entt::entity) {
            character.runController = value;
        });
        EndEdit();
        if (m_ctx) {
            m_ctx->MarkDirty();
        }
    });
    connect(m_controllerEdit, &AssetReferenceEditor::pathEdited, this, [this](const QString& path) {
        if (m_updating) {
            return;
        }
        BeginEdit("Assign character controller");
        const std::string newPath = path.toStdString();
        const LuaExposeScanResult scan = ScanController(newPath);
        ForEachTarget<CharacterComponent>([&](CharacterComponent& character, entt::entity) {
            if (character.controllerScriptPath == newPath) {
                return;
            }
            character.controllerScriptPath = newPath;
            LuaScriptComponent temp(newPath);
            temp.variables = character.controllerVariables;
            temp.authoredVariables = character.authoredControllerVariables;
            temp.authoredVariablesInitialized = character.authoredControllerVariablesInitialized;
            LuaScriptVariableReconciler::OnScriptReplaced(temp, scan);
            character.controllerVariables = temp.variables;
            character.authoredControllerVariables = temp.authoredVariables;
            character.authoredControllerVariablesInitialized = temp.authoredVariablesInitialized;
        });
        EndEdit();
        if (m_ctx) {
            m_ctx->MarkDirty();
        }
        Refresh();
    });
}

std::filesystem::path CharacterInspector::projectRoot() const
{
    return AssetManager::GetInstance().GetProjectRoot();
}

CharacterAsset CharacterInspector::loadAsset(const std::string& portable, std::string& error) const
{
    CharacterAsset asset;
    if (portable.empty()) {
        return asset;
    }
    CharacterRuntimeData data;
    if (CharacterRuntime::Load(projectRoot(), portable, data, error)) {
        return data.asset;
    }
    return asset;
}

void CharacterInspector::rebindTargets(bool applyAssetDefaults)
{
    if (!m_ctx || !m_ctx->HasActiveScene()) {
        return;
    }
    ForEachTarget<CharacterComponent>([&](CharacterComponent&, entt::entity handle) {
        Entity entity(handle, m_ctx->GetActiveScene());
        std::string error;
        CharacterRuntime::BindEntity(entity, projectRoot(), applyAssetDefaults, error);
    });
    m_ctx->RequestRebuildSceneVisuals();
}

void CharacterInspector::applyCharacterPath(const QString& path)
{
    const std::string newPath = path.toStdString();
    std::string error;
    CharacterAsset incoming;
    if (!newPath.empty()) {
        CharacterRuntimeData data;
        if (!CharacterRuntime::Load(projectRoot(), newPath, data, error)) {
            QMessageBox::critical(this, tr("Character"),
                                  tr("Could not assign character:\n%1").arg(QString::fromStdString(error)));
            Refresh();
            return;
        }
        incoming = data.asset;
    }

    BeginEdit("Assign character asset");
    ForEachTarget<CharacterComponent>([&](CharacterComponent& character, entt::entity handle) {
        const std::string previous = character.characterPath;
        if (previous == newPath) {
            return;
        }
        const bool firstAssign = previous.empty();
        CharacterAsset previousAsset;
        std::string previousError;
        if (!previous.empty()) {
            previousAsset = loadAsset(previous, previousError);
        }
        const bool usedAssetController =
            previous.empty() || character.controllerScriptPath == previousAsset.controller.scriptPath;

        std::unordered_map<std::string, std::string> materialsByIdentity;
        if (!firstAssign) {
            for (size_t i = 0; i < previousAsset.mesh.submeshes.size(); ++i) {
                const std::string value = i < character.materialOverrides.size()
                                              ? character.materialOverrides[i]
                                              : std::string();
                materialsByIdentity[CharacterSubmeshIdentity(previousAsset.mesh.submeshes[i])] = value;
            }
        }

        const std::string keepClip = character.defaultAnimation;
        const std::string keepController = character.controllerScriptPath;
        character.characterPath = newPath;
        Entity entity(handle, m_ctx->GetActiveScene());
        std::string bindError;
        if (!CharacterRuntime::BindEntity(entity, projectRoot(), firstAssign, bindError) &&
            !newPath.empty()) {
            character.characterPath = previous;
            CharacterRuntime::BindEntity(entity, projectRoot(), false, bindError);
            QMessageBox::critical(this, tr("Character"),
                                  tr("Could not bind character:\n%1")
                                      .arg(QString::fromStdString(bindError)));
            return;
        }
        if (!firstAssign && !newPath.empty()) {
            if (!ClipExists(incoming, keepClip)) {
                character.defaultAnimation.clear();
            } else {
                character.defaultAnimation = keepClip;
            }
            character.materialOverrides.assign(incoming.mesh.submeshes.size(), {});
            for (size_t i = 0; i < incoming.mesh.submeshes.size(); ++i) {
                const auto it = materialsByIdentity.find(CharacterSubmeshIdentity(incoming.mesh.submeshes[i]));
                if (it != materialsByIdentity.end()) {
                    character.materialOverrides[i] = it->second;
                }
            }
            if (!usedAssetController) {
                character.controllerScriptPath = keepController;
            } else {
                character.runController = incoming.controller.enabled;
                character.controllerScriptPath = incoming.controller.scriptPath;
            }
            CharacterRuntime::BindEntity(entity, projectRoot(), false, bindError);
        }
    });
    EndEdit();
    if (m_ctx) {
        m_ctx->MarkDirty();
        m_ctx->RequestRebuildSceneVisuals();
    }
    Refresh();
}

void CharacterInspector::rebuildClipSelector(const CharacterAsset* asset, const std::string& currentId)
{
    m_clipCombo->clear();
    m_clipCombo->addItem(tr("(None)"), QString());
    if (!asset || asset->clips.empty()) {
        m_clipCombo->setEnabled(false);
        m_clipHint->setText(tr("No clips on this character."));
        return;
    }
    m_clipCombo->setEnabled(true);
    int select = 0;
    bool found = currentId.empty();
    for (const CharacterClipEntry& clip : asset->clips) {
        QString label = QString::fromStdString(clip.displayName.empty() ? clip.id : clip.displayName);
        if (!clip.folder.empty()) {
            label = QString::fromStdString(clip.folder) + "/" + label;
        }
        m_clipCombo->addItem(label, QString::fromStdString(clip.id));
        if (clip.id == currentId || clip.sourceClip == currentId) {
            select = m_clipCombo->count() - 1;
            found = true;
        }
    }
    if (!found && !currentId.empty()) {
        m_clipCombo->addItem(QString::fromStdString(currentId), QString::fromStdString(currentId));
        select = m_clipCombo->count() - 1;
        m_clipHint->setText(tr("Clip not found on this character."));
    } else if (currentId.empty()) {
        m_clipHint->setText(tr("No default animation."));
    } else {
        m_clipHint->clear();
    }
    m_clipCombo->setCurrentIndex(select);
}

void CharacterInspector::rebuildMaterialSlots(const CharacterAsset* asset,
                                              const CharacterComponent& character)
{
    while (QLayoutItem* item = m_materialsLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    m_slotNames.clear();
    if (!asset || asset->mesh.submeshes.empty()) {
        m_materialsLayout->addWidget(new QLabel(tr("No material slots."), m_materialsGroup));
        return;
    }
    std::string currentPart;
    for (size_t i = 0; i < asset->mesh.submeshes.size(); ++i) {
        const CharacterSubmeshSlot& slot = asset->mesh.submeshes[i];
        if (!slot.meshPart.empty() && slot.meshPart != currentPart) {
            currentPart = slot.meshPart;
            auto* header = new QLabel(QString::fromStdString(currentPart), m_materialsGroup);
            QFont font = header->font();
            font.setBold(true);
            header->setFont(font);
            m_materialsLayout->addWidget(header);
        }
        m_slotNames.push_back(slot.name);
        const QString label = QString::fromStdString(slot.name.empty() ? ("Slot " + std::to_string(i)) : slot.name);
        auto* editor = new AssetReferenceEditor(label, AssetType::Material, m_materialsGroup);
        editor->setAllowEmpty(true);
        const std::string overridePath =
            i < character.materialOverrides.size() ? character.materialOverrides[i] : std::string();
        editor->setPath(QString::fromStdString(overridePath));
        connect(editor, &AssetReferenceEditor::pathEdited, this, [this, i](const QString& path) {
            if (m_updating) {
                return;
            }
            BeginEdit("Edit Character material");
            ForEachTarget<CharacterComponent>([&](CharacterComponent& target, entt::entity) {
                if (target.materialOverrides.size() <= i) {
                    target.materialOverrides.resize(i + 1);
                }
                target.materialOverrides[i] = path.toStdString();
            });
            EndEdit();
            if (m_ctx) {
                m_ctx->MarkDirty();
            }
            rebindTargets(false);
        });
        m_materialsLayout->addWidget(editor);
    }
}

void CharacterInspector::rebuildControllerVariables(const std::string& scriptPath)
{
    while (QLayoutItem* item = m_parametersLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    const LuaExposeScanResult scan = ScanController(scriptPath);
    if (!scan.diagnostics.empty()) {
        auto* label = new QLabel(QString::fromStdString(scan.diagnostics.front().message),
                                 m_parametersGroup);
        label->setWordWrap(true);
        m_parametersLayout->addWidget(label);
    }
    if (scan.fields.empty()) {
        m_parametersLayout->addWidget(
            new QLabel(tr("No exposed controller parameters."), m_parametersGroup));
        return;
    }
    for (const LuaExposedField& field : scan.fields) {
        if (field.type == LuaExposeType::Bool) {
            auto* editor = new BoolPropertyEditor(QString::fromStdString(field.name), m_parametersGroup);
            MixedBool mixed;
            VisitTargets<CharacterComponent>([&](const CharacterComponent& character, entt::entity) {
                const auto it = character.controllerVariables.find(field.name);
                if (it != character.controllerVariables.end() && it->second.IsBool()) {
                    AccumulateBool(mixed, *it->second.AsBool());
                }
            });
            editor->setFromMixed(mixed);
            connect(editor, &BoolPropertyEditor::valueEdited, this, [this, field](bool value) {
                BeginEdit("Edit controller parameter");
                ForEachTarget<CharacterComponent>([&](CharacterComponent& character, entt::entity) {
                    character.controllerVariables[field.name] = PropertyValue(value);
                    character.authoredControllerVariables.insert(field.name);
                    character.authoredControllerVariablesInitialized = true;
                });
                EndEdit();
            });
            m_parametersLayout->addWidget(editor);
        } else {
            auto* editor = new NumericPropertyEditor(QString::fromStdString(field.name), m_parametersGroup);
            MixedScalar mixed;
            VisitTargets<CharacterComponent>([&](const CharacterComponent& character, entt::entity) {
                const auto it = character.controllerVariables.find(field.name);
                if (it != character.controllerVariables.end() && it->second.IsNumber()) {
                    AccumulateScalar(mixed, static_cast<float>(*it->second.AsNumber()));
                }
            });
            editor->setFromMixed(mixed);
            connect(editor, &NumericPropertyEditor::valueEdited, this, [this, field](double value) {
                BeginEdit("Edit controller parameter");
                ForEachTarget<CharacterComponent>([&](CharacterComponent& character, entt::entity) {
                    character.controllerVariables[field.name] = PropertyValue(value);
                    character.authoredControllerVariables.insert(field.name);
                    character.authoredControllerVariablesInitialized = true;
                });
            });
            connect(editor, &NumericPropertyEditor::editingFinished, this, [this]() { EndEdit(); });
            m_parametersLayout->addWidget(editor);
        }
    }
}

void CharacterInspector::Refresh()
{
    EndEdit();
    if (!m_ctx || !m_ctx->HasActiveScene()) {
        return;
    }
    MixedString path;
    MixedString clip;
    MixedScalar speed;
    MixedBool run;
    MixedString controller;
    MixedString status;
    CharacterAsset asset;
    std::string loadError;
    VisitTargets<CharacterComponent>([&](CharacterComponent& character, entt::entity) {
        AccumulateString(path, character.characterPath);
        AccumulateString(clip, character.defaultAnimation);
        AccumulateScalar(speed, character.playbackSpeed);
        AccumulateBool(run, character.runController);
        AccumulateString(controller, character.controllerScriptPath);
        AccumulateString(status, character.lastError);
        if (!character.characterPath.empty() && asset.name.empty()) {
            asset = loadAsset(character.characterPath, loadError);
        }
    });

    m_updating = true;
    m_assetEdit->setPath(QString::fromStdString(path.present ? path.value : std::string()), path.mixed);
    m_speed->setFromMixed(speed);
    m_runController->setFromMixed(run);
    m_controllerEdit->setPath(
        QString::fromStdString(controller.present ? controller.value : std::string()), controller.mixed);
    rebuildClipSelector(asset.name.empty() ? nullptr : &asset, clip.present ? clip.value : std::string());
    CharacterComponent primary;
    VisitTargets<CharacterComponent>([&](CharacterComponent& character, entt::entity) {
        if (primary.characterPath.empty()) {
            primary = character;
        }
    });
    rebuildMaterialSlots(asset.name.empty() ? nullptr : &asset, primary);
    rebuildControllerVariables(controller.present ? controller.value : std::string());
    if (!status.value.empty()) {
        m_status->setText(QString::fromStdString(status.value));
        m_status->setStyleSheet(QStringLiteral("color: #c44;"));
    } else if (!loadError.empty()) {
        m_status->setText(QString::fromStdString(loadError));
        m_status->setStyleSheet(QStringLiteral("color: #c44;"));
    } else {
        m_status->clear();
    }
    if (!asset.name.empty()) {
        m_advanced->setText(tr("Bones %1  •  Clips %2  •  Skin %3  •  %4")
                                .arg(0)
                                .arg(static_cast<int>(asset.clips.size()))
                                .arg(QString::fromStdString(asset.skeleton.skin))
                                .arg(QString::fromStdString(asset.skeleton.signature.substr(
                                    0, std::min<size_t>(8, asset.skeleton.signature.size())))));
        CharacterRuntimeData data;
        std::string runtimeError;
        if (path.present && CharacterRuntime::Load(projectRoot(), path.value, data, runtimeError)) {
            m_advanced->setText(tr("Bones %1  •  Clips %2  •  Skin %3  •  %4")
                                    .arg(data.imported.skeleton.BoneCount())
                                    .arg(static_cast<int>(asset.clips.size()))
                                    .arg(QString::fromStdString(asset.skeleton.skin))
                                    .arg(QString::fromStdString(asset.skeleton.signature.substr(
                                        0, std::min<size_t>(8, asset.skeleton.signature.size())))));
        }
    } else {
        m_advanced->clear();
    }
    m_updating = false;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
