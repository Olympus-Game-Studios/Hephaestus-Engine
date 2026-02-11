/**
 * @file ScriptInspector.cpp
 * @brief Implementation of the C++ ScriptComponent inspector.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "ScriptInspector.h"
#include "EditorUndoCommand.h"

#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"
#include "../../Scene/Components.h"
#include "../../Scripting/ScriptComponent.h"
#include "../../Scripting/ScriptRegistry.h"
#include "../../Scripting/ScriptEngine.h"

#include <QHBoxLayout>
#include <QMessageBox>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ScriptInspector::ScriptInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Scripts"), ctx, entity, parent)
{
    buildUI();
    connectSignals();
    Refresh();
}

// ---------------------------------------------------------------------------
// Build UI
// ---------------------------------------------------------------------------

void ScriptInspector::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 10, 6, 6);
    mainLayout->setSpacing(4);

    // Attached scripts list
    auto* attachedLabel = new QLabel(tr("Attached Script(s):"));
    attachedLabel->setStyleSheet("font-weight: bold;");
    mainLayout->addWidget(attachedLabel);

    m_scriptList = new QListWidget();
    m_scriptList->setMaximumHeight(120);
    m_scriptList->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_scriptList);

    // Add Script section
    auto* addRow = new QHBoxLayout();
    m_scriptCombo = new QComboBox();
    m_scriptCombo->setEditable(true);
    m_scriptCombo->setInsertPolicy(QComboBox::NoInsert);
    m_scriptCombo->lineEdit()->setPlaceholderText(tr("Search scripts..."));
    m_attachBtn = new QPushButton(tr("Attach"));
    addRow->addWidget(m_scriptCombo, 1);
    addRow->addWidget(m_attachBtn);
    mainLayout->addLayout(addRow);

    // Status label
    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: green; font-size: 10px;");
    m_statusLabel->setVisible(false);
    mainLayout->addWidget(m_statusLabel);

    // Remove component button
    m_removeCompBtn = new QPushButton(tr("Remove Scripts Component"));
    m_removeCompBtn->setStyleSheet("color: #cc3333;");
    mainLayout->addWidget(m_removeCompBtn);
}

// ---------------------------------------------------------------------------
// Connect signals
// ---------------------------------------------------------------------------

void ScriptInspector::connectSignals()
{
    connect(m_attachBtn, &QPushButton::clicked, this, &ScriptInspector::attachScript);

    connect(m_removeCompBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Remove ScriptComponent");

        if (reg.all_of<ScriptComponent>(m_entity)) {
            reg.remove<ScriptComponent>(m_entity);
        }
        m_ctx->MarkDirty();
        emit componentRemoved(m_entity, "Scripts");
    });
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void ScriptInspector::Refresh()
{
    // Populate the combo with all registered scripts
    m_scriptCombo->clear();
    auto registered = ScriptRegistry::GetRegisteredScripts();
    for (const auto& name : registered) {
        m_scriptCombo->addItem(QString::fromStdString(name));
    }
    m_scriptCombo->setCurrentIndex(-1);

    rebuildScriptList();
}

// ---------------------------------------------------------------------------
// Rebuild attached script list
// ---------------------------------------------------------------------------

void ScriptInspector::rebuildScriptList()
{
    m_scriptList->clear();

    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<ScriptComponent>(m_entity)) return;

    auto& sc = reg.get<ScriptComponent>(m_entity);
    for (size_t i = 0; i < sc.scriptNames.size(); ++i) {
        auto* itemWidget = new QWidget();
        auto* hl = new QHBoxLayout(itemWidget);
        hl->setContentsMargins(4, 2, 4, 2);

        auto* nameLabel = new QLabel(QString::fromStdString(sc.scriptNames[i]));
        nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        auto* removeBtn = new QPushButton(tr("Remove"));
        removeBtn->setFixedSize(60, 22);
        int idx = static_cast<int>(i);
        connect(removeBtn, &QPushButton::clicked, this, [this, idx]() { removeScript(idx); });

        hl->addWidget(nameLabel);
        hl->addWidget(removeBtn);

        auto* item = new QListWidgetItem(m_scriptList);
        item->setSizeHint(itemWidget->sizeHint());
        m_scriptList->addItem(item);
        m_scriptList->setItemWidget(item, itemWidget);
    }

    if (sc.scriptNames.empty()) {
        m_scriptList->addItem(tr("(No scripts attached)"));
        m_scriptList->item(0)->setFlags(Qt::NoItemFlags);
    }
}

// ---------------------------------------------------------------------------
// Attach script
// ---------------------------------------------------------------------------

void ScriptInspector::attachScript()
{
    QString scriptName = m_scriptCombo->currentText().trimmed();
    if (scriptName.isEmpty()) return;

    if (!ScriptRegistry::IsRegistered(scriptName.toStdString())) {
        m_statusLabel->setText(tr("Script '%1' is not registered").arg(scriptName));
        m_statusLabel->setStyleSheet("color: red; font-size: 10px;");
        m_statusLabel->setVisible(true);
        return;
    }

    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, "Attach script " + scriptName.toStdString());

    // Ensure ScriptComponent exists
    if (!reg.all_of<ScriptComponent>(m_entity)) {
        reg.emplace<ScriptComponent>(m_entity);
    }

    auto& sc = reg.get<ScriptComponent>(m_entity);

    // Create script instance
    Entity entityWrapper(m_entity, m_ctx->GetActiveScene());
    auto scriptPtr = ScriptRegistry::CreateScript(
        scriptName.toStdString(), entityWrapper, m_ctx->GetActiveScene());

    if (scriptPtr) {
        sc.AddScript(scriptPtr, scriptName.toStdString());
        m_statusLabel->setText(tr("Attached '%1'").arg(scriptName));
        m_statusLabel->setStyleSheet("color: green; font-size: 10px;");
    } else {
        m_statusLabel->setText(tr("Failed to create '%1'").arg(scriptName));
        m_statusLabel->setStyleSheet("color: red; font-size: 10px;");
    }
    m_statusLabel->setVisible(true);

    m_ctx->MarkDirty();
    rebuildScriptList();
}

// ---------------------------------------------------------------------------
// Remove script by index
// ---------------------------------------------------------------------------

void ScriptInspector::removeScript(int index)
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<ScriptComponent>(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, "Remove script");

    auto& sc = reg.get<ScriptComponent>(m_entity);
    if (index >= 0 && index < static_cast<int>(sc.GetScriptCount())) {
        sc.RemoveScript(static_cast<size_t>(index));
    }

    m_ctx->MarkDirty();
    rebuildScriptList();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
