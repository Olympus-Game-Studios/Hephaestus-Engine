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
#include "../../Scripting/LuaExposeMetadata.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>

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
    void clearParameterWidgets();
    void rebuildParameterWidgets(const LuaExposeScanResult& scan);
    void applyParameterValue(const std::string& name, const PropertyValue& value);
    void resetParameterValue(const LuaExposedField& field);
    bool isParameterOverridden(const std::string& name) const;

    AssetReferenceEditor* m_pathEdit = nullptr;
    QCheckBox*   m_enabledCheck = nullptr;
    QGroupBox* m_parametersGroup = nullptr;
    QVBoxLayout* m_parametersLayout = nullptr;
    QPushButton* m_removeBtn    = nullptr;
    LuaExposeScanResult m_scanResult;
    std::string m_scannedPath;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
