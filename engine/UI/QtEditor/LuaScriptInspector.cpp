/**
 * @file LuaScriptInspector.cpp
 * @brief Implementation of the LuaScriptComponent inspector.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "LuaScriptInspector.h"
#include "EditorUndoCommand.h"
#include "PropertyEditors.h"

#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetPathPolicy.h"
#include "../../Core/EditorContext.h"
#include "../../Scene/Entity.h"
#include "../../Scene/PrefabOverrideSystem.h"
#include "../../Scene/Scene.h"
#include "../../Scripting/LuaExposeScanner.h"
#include "../../Scripting/LuaScriptComponent.h"
#include "../../Scripting/LuaScriptVariableReconciler.h"

#include <QFormLayout>
#include <QMenu>
#include <QVBoxLayout>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

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
    if (portable.rfind("scripts/", 0) == 0 && assets.GetAssetInfo(portable)) {
        return false;
    }

    const std::filesystem::path projectRoot(assets.GetProjectRoot());
    if (!projectRoot.empty()) {
        const auto resolved = AssetPathPolicy::ResolvePortableKey(projectRoot, portable);
        if (!resolved.empty() && std::filesystem::exists(resolved)) {
            return false;
        }
    }

    const auto assetResolved = std::filesystem::path(assets.ToAbsolutePath(portable));
    return assetResolved.empty() || !std::filesystem::exists(assetResolved);
}

struct MixedPropertyValue {
    bool present = false;
    bool mixed = false;
    PropertyValue value;
};

static void AccumulatePropertyValue(MixedPropertyValue& mixed, const PropertyValue& value)
{
    if (!mixed.present) {
        mixed.present = true;
        mixed.value = value;
    } else if (!mixed.value.Equals(value)) {
        mixed.mixed = true;
    }
}

static QString PrettifyVariableName(const std::string& name)
{
    std::string display;
    display.reserve(name.size() + 4);
    for (std::size_t index = 0; index < name.size(); ++index) {
        const unsigned char current = static_cast<unsigned char>(name[index]);
        if (current == '_') {
            if (!display.empty() && display.back() != ' ') display.push_back(' ');
            continue;
        }
        if (index != 0 && std::isupper(current) &&
            (std::islower(static_cast<unsigned char>(name[index - 1])) ||
             std::isdigit(static_cast<unsigned char>(name[index - 1])))) {
            display.push_back(' ');
        }
        display.push_back(static_cast<char>(current));
    }
    if (!display.empty()) {
        display[0] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(display[0])));
    }
    return QString::fromStdString(display);
}

static LuaExposeScanResult ScanScriptSource(const LuaScriptComponent& component)
{
    if (!component.scriptSource.empty()) {
        return LuaExposeScanner::Scan(component.scriptSource);
    }
    if (component.scriptPath.empty()) {
        return LuaExposeScanner::Scan({});
    }

    auto& assets = AssetManager::GetInstance();
    std::filesystem::path path(component.scriptPath);
    if (!path.is_absolute()) {
        path = assets.ToAbsolutePath(component.scriptPath);
        if (!std::filesystem::exists(path) &&
            component.scriptPath.rfind("assets/", 0) == 0) {
            path = assets.ToAbsolutePath(component.scriptPath.substr(7));
        }
        if (!std::filesystem::exists(path) && !assets.GetProjectRoot().empty()) {
            path = std::filesystem::path(assets.GetProjectRoot()) / component.scriptPath;
        }
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LuaExposeScanResult result;
        result.valid = false;
        result.contentHash = LuaExposeScanner::ComputeContentHash({});
        result.diagnostics.push_back({
            0, "Script could not be opened: " + component.scriptPath, false
        });
        return result;
    }
    std::ostringstream source;
    source << file.rdbuf();
    return LuaExposeScanner::Scan(source.str());
}

static bool IsPrefabInstance(Scene& scene, entt::entity handle)
{
    return PrefabOverrideSystem::IsPrefabInstanceEntity(Entity(handle, &scene));
}

static std::unordered_map<std::string, PropertyValue>* FindScriptOverrides(
    Scene& scene,
    entt::entity handle)
{
    Entity entity(handle, &scene);
    if (!PrefabOverrideSystem::IsPrefabInstanceEntity(entity)) {
        return nullptr;
    }
    auto* instance = PrefabOverrideSystem::TryGetInstance(scene, entity);
    const auto* node = PrefabOverrideSystem::TryGetNode(entity);
    if (!instance || !node) {
        return nullptr;
    }
    auto overrideNode = instance->overrides.find(node->nodeId);
    return overrideNode != instance->overrides.end()
        ? &overrideNode->second.scriptVars : nullptr;
}

LuaScriptInspector::LuaScriptInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Lua Scripts"), ctx, entity, parent)
{
    SetTypeName("LuaScript");
    buildUI();
    connectSignals();
    Refresh();
}

void LuaScriptInspector::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 10, 6, 6);
    mainLayout->setSpacing(4);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_enabledCheck = new QCheckBox(tr("Lua Script Enabled"));
    form->addRow(QString(), m_enabledCheck);

    m_pathEdit = new AssetReferenceEditor(QString(), AssetType::Script);
    m_pathEdit->setAllowEmpty(true);
    form->addRow(tr("Script:"), m_pathEdit);

    mainLayout->addLayout(form);

    m_parametersGroup = new QGroupBox(tr("Parameters"), this);
    m_parametersLayout = new QVBoxLayout(m_parametersGroup);
    m_parametersLayout->setContentsMargins(6, 8, 6, 6);
    m_parametersLayout->setSpacing(4);
    mainLayout->addWidget(m_parametersGroup);

    m_removeBtn = new QPushButton(tr("Remove Lua Script"));
    m_removeBtn->setStyleSheet("color: #cc3333;");
    mainLayout->addWidget(m_removeBtn);
}

void LuaScriptInspector::connectSignals()
{
    connect(m_enabledCheck, &QCheckBox::toggled, this, [this](bool) {
        applyToComponent();
        EndEdit();
    });
    connect(m_pathEdit, &AssetReferenceEditor::pathEdited, this, [this](const QString& path) {
        if (m_updating) return;
        BeginEdit("Attach Lua script");
        const std::string newPath = path.toStdString();
        LuaScriptComponent scanComponent(newPath);
        const LuaExposeScanResult scan = ScanScriptSource(scanComponent);
        ForEachTarget<LuaScriptComponent>([&](LuaScriptComponent& lsc, entt::entity) {
            if (lsc.scriptPath == newPath) return;
            lsc.scriptPath = newPath;
            lsc.scriptSource.clear();
            LuaScriptVariableReconciler::OnScriptReplaced(lsc, scan);
        });
        m_scannedPath = newPath;
        m_scanResult = scan;
        EndEdit();
        Refresh();
    });

    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        EditorUndoManager::PushSnapshot(m_ctx, "Remove LuaScriptComponent");
        ForEachTarget<LuaScriptComponent>([&](LuaScriptComponent&, entt::entity entity) {
            auto& reg = m_ctx->GetActiveScene()->GetRegistry();
            if (reg.all_of<LuaScriptComponent>(entity)) {
                reg.remove<LuaScriptComponent>(entity);
            }
        });
        emit componentRemoved(m_entity, "LuaScript");
    });
}

void LuaScriptInspector::Refresh()
{
    EndEdit();
    MixedBool enabled;
    MixedString path;
    VisitTargets<LuaScriptComponent>([&](const LuaScriptComponent& lsc, entt::entity) {
        AccumulateBool(enabled, lsc.enabled);
        AccumulateString(path, lsc.scriptPath);
    });
    if (!enabled.present) return;

    m_updating = true;
    blockAllSignals(true);
    SetCheckFromMixed(m_enabledCheck, enabled);
    m_pathEdit->setPath(QString::fromStdString(path.value), path.mixed, PathMissing(path.value));
    blockAllSignals(false);

    clearParameterWidgets();
    if (path.mixed) {
        auto* status = new QLabel(tr("Multiple scripts selected"), m_parametersGroup);
        status->setStyleSheet(QStringLiteral("color: #c9a227;"));
        m_parametersLayout->addWidget(status);
        m_parametersGroup->show();
        m_updating = false;
        return;
    }
    if (path.value.empty()) {
        m_parametersGroup->hide();
        m_scannedPath.clear();
        m_scanResult = {};
        m_updating = false;
        return;
    }

    LuaScriptComponent* primary = nullptr;
    VisitTargets<LuaScriptComponent>([&](LuaScriptComponent& lsc, entt::entity) {
        if (!primary) primary = &lsc;
    });
    if (!primary) {
        m_updating = false;
        return;
    }

    LuaExposeScanResult scan = ScanScriptSource(*primary);
    const bool sourceChanged =
        m_scannedPath != path.value || m_scanResult.contentHash != scan.contentHash;
    bool reconciled = false;
    if (sourceChanged) {
        const LuaExposeScanResult* previous =
            m_scannedPath == path.value && !m_scannedPath.empty() ? &m_scanResult : nullptr;
        if (m_ctx && m_ctx->HasActiveScene()) {
            auto& registry = m_ctx->GetActiveScene()->GetRegistry();
            auto scripts = registry.view<LuaScriptComponent>();
            for (entt::entity handle : scripts) {
                auto& lsc = scripts.get<LuaScriptComponent>(handle);
                if (lsc.scriptPath != path.value) continue;
                auto* overrides = FindScriptOverrides(*m_ctx->GetActiveScene(), handle);
                const LuaScriptReconcileResult result =
                    LuaScriptVariableReconciler::OnScriptSourceChanged(
                    lsc, previous, scan, overrides);
                reconciled = reconciled || result.changed;
            }
        }
    }
    m_scannedPath = path.value;
    m_scanResult = scan;
    rebuildParameterWidgets(scan);
    m_updating = false;
    if (reconciled && m_ctx) {
        m_ctx->MarkDirty();
    }
}

void LuaScriptInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;
    BeginEdit("Edit LuaScript");
    bool enabled = false;
    if (TryReadCheck(m_enabledCheck, enabled)) {
        ForEachTarget<LuaScriptComponent>([&](LuaScriptComponent& lsc, entt::entity) {
            lsc.enabled = enabled;
        });
    }
}

void LuaScriptInspector::blockAllSignals(bool block)
{
    m_enabledCheck->blockSignals(block);
}

void LuaScriptInspector::clearParameterWidgets()
{
    while (QLayoutItem* item = m_parametersLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void LuaScriptInspector::rebuildParameterWidgets(const LuaExposeScanResult& scan)
{
    m_parametersGroup->show();
    if (!scan.diagnostics.empty()) {
        QString warning;
        const std::size_t count = std::min<std::size_t>(scan.diagnostics.size(), 3);
        for (std::size_t index = 0; index < count; ++index) {
            if (!warning.isEmpty()) warning += QLatin1Char('\n');
            const auto& diagnostic = scan.diagnostics[index];
            if (diagnostic.sourceLine != 0) {
                warning += tr("Line %1: ").arg(diagnostic.sourceLine);
            }
            warning += QString::fromStdString(diagnostic.message);
        }
        auto* status = new QLabel(warning, m_parametersGroup);
        status->setWordWrap(true);
        status->setStyleSheet(QStringLiteral("color: #c9a227;"));
        m_parametersLayout->addWidget(status);
    }
    if (!scan.valid) {
        if (scan.diagnostics.empty()) {
            m_parametersLayout->addWidget(new QLabel(
                tr("Script could not be scanned"), m_parametersGroup));
        }
        return;
    }
    if (scan.fields.empty()) {
        m_parametersLayout->addWidget(new QLabel(
            tr("No exposed parameters"), m_parametersGroup));
        return;
    }

    for (const LuaExposedField& field : scan.fields) {
        MixedPropertyValue mixed;
        std::size_t valueCount = 0;
        VisitTargets<LuaScriptComponent>([&](const LuaScriptComponent& lsc, entt::entity) {
            const auto value = lsc.variables.find(field.name);
            if (value != lsc.variables.end()) {
                AccumulatePropertyValue(mixed, value->second);
                ++valueCount;
            }
        });
        if (valueCount != m_targets.size()) {
            mixed.mixed = true;
        }

        QWidget* editor = nullptr;
        const QString label = PrettifyVariableName(field.name);
        if (field.type == LuaExposeType::Bool) {
            auto* valueEditor = new BoolPropertyEditor(label, m_parametersGroup);
            MixedBool boolValue;
            if (mixed.present && !mixed.mixed && mixed.value.IsBool()) {
                boolValue = {true, false, *mixed.value.AsBool()};
            } else {
                boolValue = {mixed.present, true, false};
            }
            valueEditor->setFromMixed(boolValue);
            connect(valueEditor, &BoolPropertyEditor::valueEdited, this,
                    [this, field](bool value) {
                if (m_updating) return;
                BeginEdit("Edit Lua parameter");
                applyParameterValue(field.name, PropertyValue(value));
                EndEdit();
            });
            editor = valueEditor;
        } else if (field.type == LuaExposeType::Integer ||
                   field.type == LuaExposeType::Number) {
            auto* valueEditor = new NumericPropertyEditor(label, m_parametersGroup);
            valueEditor->setRange(-1.0e9, 1.0e9);
            valueEditor->setDecimals(field.type == LuaExposeType::Integer ? 0 : 4);
            valueEditor->setSingleStep(field.type == LuaExposeType::Integer ? 1.0 : 0.1);
            MixedScalar number;
            if (mixed.present && !mixed.mixed && mixed.value.IsNumber()) {
                number = {true, false, *mixed.value.AsNumber()};
            } else {
                number = {mixed.present, true, 0.0};
            }
            valueEditor->setFromMixed(number);
            connect(valueEditor, &NumericPropertyEditor::valueEdited, this,
                    [this, field](double value) {
                if (m_updating) return;
                BeginEdit("Edit Lua parameter");
                applyParameterValue(field.name, PropertyValue(value));
            });
            connect(valueEditor, &NumericPropertyEditor::editingFinished,
                    this, &LuaScriptInspector::EndEdit);
            connect(valueEditor, &NumericPropertyEditor::resetRequested, this,
                    [this, field]() { resetParameterValue(field); });
            editor = valueEditor;
        } else if (field.type == LuaExposeType::String) {
            auto* valueEditor = new StringPropertyEditor(label, m_parametersGroup);
            const QString value = mixed.present && mixed.value.IsString()
                ? QString::fromStdString(*mixed.value.AsString()) : QString();
            valueEditor->setValue(value, mixed.mixed);
            connect(valueEditor, &StringPropertyEditor::valueEdited, this,
                    [this, field](const QString& value) {
                if (m_updating) return;
                BeginEdit("Edit Lua parameter");
                applyParameterValue(field.name, PropertyValue(value.toStdString()));
            });
            connect(valueEditor, &StringPropertyEditor::editingFinished,
                    this, &LuaScriptInspector::EndEdit);
            editor = valueEditor;
        } else if (field.type == LuaExposeType::Vec2) {
            auto* valueEditor = new Vec2PropertyEditor(label, m_parametersGroup);
            std::vector<Vec2> values;
            VisitTargets<LuaScriptComponent>([&](const LuaScriptComponent& lsc, entt::entity) {
                const auto value = lsc.variables.find(field.name);
                if (value != lsc.variables.end() && value->second.IsVec2()) {
                    values.push_back(*value->second.AsVec2());
                }
            });
            MixedScalar x, y;
            for (const Vec2& value : values) {
                AccumulateScalar(x, value.x);
                AccumulateScalar(y, value.y);
            }
            valueEditor->setRange(-1.0e9, 1.0e9);
            valueEditor->setDecimals(4);
            valueEditor->setFromMixed(x, y);
            connect(valueEditor, &Vec2PropertyEditor::axisEdited, this,
                    [this, field](int axis, double value) {
                if (m_updating) return;
                BeginEdit("Edit Lua parameter");
                ForEachTarget<LuaScriptComponent>(
                    [this, field, axis, value](LuaScriptComponent& lsc, entt::entity handle) {
                    Vec2 current = *field.defaultValue.AsVec2();
                    if (const auto existing = lsc.variables.find(field.name);
                        existing != lsc.variables.end() && existing->second.IsVec2()) {
                        current = *existing->second.AsVec2();
                    }
                    if (axis == 0) current.x = static_cast<float>(value);
                    else current.y = static_cast<float>(value);
                    lsc.variables[field.name] = PropertyValue(current);
                    if (m_ctx && !IsPrefabInstance(*m_ctx->GetActiveScene(), handle)) {
                        lsc.authoredVariables.insert(field.name);
                    }
                });
            });
            connect(valueEditor, &Vec2PropertyEditor::editingFinished,
                    this, &LuaScriptInspector::EndEdit);
            editor = valueEditor;
        } else if (field.type == LuaExposeType::Vec3) {
            auto* valueEditor = new Vec3PropertyEditor(label, m_parametersGroup);
            std::vector<Vec3> values;
            VisitTargets<LuaScriptComponent>([&](const LuaScriptComponent& lsc, entt::entity) {
                const auto value = lsc.variables.find(field.name);
                if (value != lsc.variables.end() && value->second.IsVec3()) {
                    values.push_back(*value->second.AsVec3());
                }
            });
            valueEditor->setRange(-1.0e9, 1.0e9);
            valueEditor->setDecimals(4);
            valueEditor->setFromMixed(MixVec3(values));
            connect(valueEditor, &Vec3PropertyEditor::axisEdited, this,
                    [this, field](int axis, double value) {
                if (m_updating) return;
                BeginEdit("Edit Lua parameter");
                ForEachTarget<LuaScriptComponent>(
                    [this, field, axis, value](LuaScriptComponent& lsc, entt::entity handle) {
                    Vec3 current = *field.defaultValue.AsVec3();
                    if (const auto existing = lsc.variables.find(field.name);
                        existing != lsc.variables.end() && existing->second.IsVec3()) {
                        current = *existing->second.AsVec3();
                    }
                    WriteVec3Axis(current, axis, static_cast<float>(value));
                    lsc.variables[field.name] = PropertyValue(current);
                    if (m_ctx && !IsPrefabInstance(*m_ctx->GetActiveScene(), handle)) {
                        lsc.authoredVariables.insert(field.name);
                    }
                });
            });
            connect(valueEditor, &Vec3PropertyEditor::editingFinished,
                    this, &LuaScriptInspector::EndEdit);
            connect(valueEditor, &Vec3PropertyEditor::resetRequested, this,
                    [this, field]() { resetParameterValue(field); });
            editor = valueEditor;
        } else if (field.type == LuaExposeType::Color) {
            auto* valueEditor = new ColorRgbaPropertyEditor(label, m_parametersGroup);
            const Vec4 value = mixed.present && mixed.value.IsVec4()
                ? *mixed.value.AsVec4() : *field.defaultValue.AsVec4();
            valueEditor->setValue(value, mixed.mixed);
            connect(valueEditor, &ColorRgbaPropertyEditor::valueEdited, this,
                    [this, field](const Vec4& value) {
                if (m_updating) return;
                BeginEdit("Edit Lua parameter");
                applyParameterValue(field.name, PropertyValue(value));
            });
            connect(valueEditor, &ColorRgbaPropertyEditor::editingFinished,
                    this, &LuaScriptInspector::EndEdit);
            editor = valueEditor;
        }

        if (!editor) continue;
        if (isParameterOverridden(field.name)) {
            editor->setStyleSheet(QStringLiteral("border-left: 2px solid #c9a227;"));
        }
        if (field.type == LuaExposeType::Bool ||
            field.type == LuaExposeType::String ||
            field.type == LuaExposeType::Vec2 ||
            field.type == LuaExposeType::Color) {
            editor->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(editor, &QWidget::customContextMenuRequested, editor,
                    [this, editor, field](const QPoint& position) {
                QMenu menu(editor);
                menu.addAction(tr("Reset to Script Default"), this,
                               [this, field]() { resetParameterValue(field); });
                menu.exec(editor->mapToGlobal(position));
            });
        }
        m_parametersLayout->addWidget(editor);
    }
}

void LuaScriptInspector::applyParameterValue(
    const std::string& name,
    const PropertyValue& value)
{
    ForEachTarget<LuaScriptComponent>(
        [this, &name, &value](LuaScriptComponent& lsc, entt::entity handle) {
        lsc.variables[name] = value;
        if (m_ctx && !IsPrefabInstance(*m_ctx->GetActiveScene(), handle)) {
            lsc.authoredVariables.insert(name);
        }
    });
}

void LuaScriptInspector::resetParameterValue(const LuaExposedField& field)
{
    if (m_updating) return;
    BeginEdit("Reset Lua parameter");
    ForEachTarget<LuaScriptComponent>(
        [this, &field](LuaScriptComponent& lsc, entt::entity handle) {
        lsc.variables[field.name] = field.defaultValue;
        if (m_ctx && !IsPrefabInstance(*m_ctx->GetActiveScene(), handle)) {
            lsc.authoredVariables.erase(field.name);
        }
    });
    EndEdit();
    Refresh();
}

bool LuaScriptInspector::isParameterOverridden(const std::string& name) const
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return false;
    Scene& scene = *m_ctx->GetActiveScene();
    for (entt::entity handle : m_targets) {
        Entity entity(handle, &scene);
        if (!PrefabOverrideSystem::IsPrefabInstanceEntity(entity)) continue;
        auto* instance = PrefabOverrideSystem::TryGetInstance(scene, entity);
        const auto* node = PrefabOverrideSystem::TryGetNode(entity);
        if (instance && node &&
            PrefabOverrideSystem::IsPropertyOverridden(
                *instance, node->nodeId, "LuaScript", name)) {
            return true;
        }
    }
    return false;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
