#pragma once

/**
 * @file ScriptInspector.h
 * @brief Inspector widget for ScriptComponent (C++ scripts).
 *
 * Shows attached scripts with remove buttons, a search-as-you-type
 * combo for registered scripts, and an "Attach" button.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "ComponentInspector.h"

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {

class ScriptInspector : public ComponentInspector
{
    Q_OBJECT

public:
    explicit ScriptInspector(EditorContext* ctx, entt::entity entity, QWidget* parent = nullptr);
    ~ScriptInspector() override = default;

    void Refresh() override;

private:
    void buildUI();
    void connectSignals();
    void rebuildScriptList();
    void attachScript();
    void removeScript(int index);

    // Widgets
    QListWidget*   m_scriptList    = nullptr;
    QComboBox*     m_scriptCombo   = nullptr;
    QPushButton*   m_attachBtn     = nullptr;
    QLabel*        m_statusLabel   = nullptr;
    QPushButton*   m_removeCompBtn = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
