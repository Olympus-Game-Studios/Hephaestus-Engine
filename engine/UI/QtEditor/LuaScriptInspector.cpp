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
#include "../../Scene/Scene.h"
#include "../../Scripting/LuaScriptComponent.h"

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
    if (portable.rfind("scripts/", 0) == 0 && assets.GetAssetInfo(portable)) {
        return false;
    }
    const auto resolved = std::filesystem::path(assets.ToAbsolutePath(portable));
    return !std::filesystem::exists(resolved);
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
        ForEachTarget<LuaScriptComponent>([&](LuaScriptComponent& lsc, entt::entity) {
            lsc.scriptPath = path.toStdString();
        });
        EndEdit();
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
    m_updating = false;
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

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
