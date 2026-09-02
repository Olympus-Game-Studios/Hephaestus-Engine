#pragma once

/**
 * @file LuaScriptInspector.h
 * @brief Inspector widget for LuaScriptComponent.
 *
 * Shows:
 *   - Current script path (read-only, or drag-drop target)
 *   - Enabled checkbox
 *   - "Browse" button (file picker for .lua)
 *   - "Remove Lua Script" button
 *
 * Migration note:
 *   Old scenes only serialise `scriptPath` for LuaScriptComponent.
 *   The `enabled` field defaults to true if absent in JSON, which matches
 *   the component default. No special migration is needed here.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"
#include "AssetReferenceEditor.h"

#include <QCheckBox>
#include <QPushButton>
#include <QLabel>

namespace Hephaestus {
namespace QtEditor {

class LuaScriptInspector : public ComponentInspector
{
    Q_OBJECT

public:
    explicit LuaScriptInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    ~LuaScriptInspector() override = default;

    void Refresh() override;

private:
    void buildUI();
    void connectSignals();
    void applyToComponent();
    void blockAllSignals(bool block);

    AssetReferenceEditor* m_pathEdit = nullptr;
    QCheckBox*   m_enabledCheck = nullptr;
    QPushButton* m_removeBtn    = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
