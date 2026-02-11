/**
 * @file LuaScriptInspector.cpp
 * @brief Implementation of the LuaScriptComponent inspector.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "LuaScriptInspector.h"
#include "EditorUndoCommand.h"

#include "../../Core/EditorContext.h"
#include "../../Scene/Scene.h"
#include "../../Scripting/LuaScriptComponent.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

LuaScriptInspector::LuaScriptInspector(EditorContext* ctx, entt::entity entity, QWidget* parent)
    : ComponentInspector(tr("Lua Scripts"), ctx, entity, parent)
{
    buildUI();
    connectSignals();
    Refresh();
}

// ---------------------------------------------------------------------------
// Build UI
// ---------------------------------------------------------------------------

void LuaScriptInspector::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 10, 6, 6);
    mainLayout->setSpacing(4);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Enabled checkbox
    m_enabledCheck = new QCheckBox(tr("Lua Script Enabled"));
    form->addRow(QString(), m_enabledCheck);

    // Script path + browse
    {
        auto* row = new QHBoxLayout();
        m_pathEdit = new QLineEdit();
        m_pathEdit->setReadOnly(true);
        m_pathEdit->setPlaceholderText(tr("(No script assigned)"));
        m_pathEdit->setToolTip(tr("Drag a .lua file from the Asset Browser to assign"));
        m_browseBtn = new QPushButton(tr("Browse..."));
        row->addWidget(m_pathEdit, 1);
        row->addWidget(m_browseBtn);
        form->addRow(tr("Script:"), static_cast<QLayout*>(row));
    }

    mainLayout->addLayout(form);

    // Remove button
    m_removeBtn = new QPushButton(tr("Remove Lua Script"));
    m_removeBtn->setStyleSheet("color: #cc3333;");
    mainLayout->addWidget(m_removeBtn);
}

// ---------------------------------------------------------------------------
// Connect signals
// ---------------------------------------------------------------------------

void LuaScriptInspector::connectSignals()
{
    connect(m_enabledCheck, &QCheckBox::toggled, this, [this](bool) { applyToComponent(); });
    connect(m_browseBtn,    &QPushButton::clicked, this, &LuaScriptInspector::browseScript);

    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_ctx || !m_ctx->HasActiveScene()) return;
        auto& reg = m_ctx->GetActiveScene()->GetRegistry();
        if (!reg.valid(m_entity)) return;

        EditorUndoManager::PushSnapshot(m_ctx, "Remove LuaScriptComponent");

        if (reg.all_of<LuaScriptComponent>(m_entity)) {
            reg.remove<LuaScriptComponent>(m_entity);
        }
        m_ctx->MarkDirty();
        emit componentRemoved(m_entity, "LuaScript");
    });
}

// ---------------------------------------------------------------------------
// Refresh (component → UI)
// ---------------------------------------------------------------------------

void LuaScriptInspector::Refresh()
{
    if (!m_ctx || !m_ctx->HasActiveScene()) return;

    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<LuaScriptComponent>(m_entity)) return;

    const auto& lsc = reg.get<LuaScriptComponent>(m_entity);

    m_updating = true;
    blockAllSignals(true);

    m_enabledCheck->setChecked(lsc.enabled);

    if (!lsc.scriptPath.empty()) {
        m_pathEdit->setText(QString::fromStdString(lsc.scriptPath));
    } else {
        m_pathEdit->clear();
    }

    blockAllSignals(false);
    m_updating = false;
}

// ---------------------------------------------------------------------------
// Apply (UI → component)
// ---------------------------------------------------------------------------

void LuaScriptInspector::applyToComponent()
{
    if (m_updating || !m_ctx || !m_ctx->HasActiveScene()) return;

    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity) || !reg.all_of<LuaScriptComponent>(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, "Edit LuaScript");

    auto& lsc = reg.get<LuaScriptComponent>(m_entity);
    lsc.enabled = m_enabledCheck->isChecked();

    m_ctx->MarkDirty();
}

// ---------------------------------------------------------------------------
// Browse for .lua file
// ---------------------------------------------------------------------------

void LuaScriptInspector::browseScript()
{
    QString filepath = QFileDialog::getOpenFileName(
        this, tr("Select Lua Script"),
        QString("scripts"),
        tr("Lua Scripts (*.lua);;All Files (*)"));

    if (filepath.isEmpty()) return;

    if (!m_ctx || !m_ctx->HasActiveScene()) return;
    auto& reg = m_ctx->GetActiveScene()->GetRegistry();
    if (!reg.valid(m_entity)) return;

    EditorUndoManager::PushSnapshot(m_ctx, "Attach Lua script");

    // Ensure component exists
    if (!reg.all_of<LuaScriptComponent>(m_entity)) {
        reg.emplace<LuaScriptComponent>(m_entity, filepath.toStdString());
    } else {
        reg.get<LuaScriptComponent>(m_entity).scriptPath = filepath.toStdString();
    }

    m_ctx->MarkDirty();
    Refresh();
}

// ---------------------------------------------------------------------------
// Signal blocking
// ---------------------------------------------------------------------------

void LuaScriptInspector::blockAllSignals(bool block)
{
    m_enabledCheck->blockSignals(block);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
