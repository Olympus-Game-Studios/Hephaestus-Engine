#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Construction/ConstructionKit.h"

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE

namespace Hephaestus {
namespace QtEditor {

// Edits a .hpkit: its prefab pieces, their roles, widths and stretch, with a trial fill of a wall length.
class KitEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KitEditorDialog(const QString& absolutePath, QWidget* parent = nullptr);

    static void Open(QWidget* parent, const QString& absolutePath);

private:
    void setupUi();
    void rebuildTable();
    void refreshChecks();
    void addPieces();
    void measureSelected();
    void removeSelected();
    bool save();
    void setDirty(bool dirty);
    void done(int result) override;

    QString m_absolutePath;
    Construction::KitDefinition m_kit;
    bool m_dirty = false;
    bool m_updating = false;

    QLineEdit* m_name = nullptr;
    QTableWidget* m_table = nullptr;
    QDoubleSpinBox* m_trialLength = nullptr;
    QLabel* m_trial = nullptr;
    QLabel* m_problems = nullptr;
    QPushButton* m_save = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
