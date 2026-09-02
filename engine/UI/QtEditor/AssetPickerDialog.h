#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Assets/AssetTypes.h"

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <optional>

namespace Hephaestus {
namespace QtEditor {

class AssetPickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AssetPickerDialog(AssetType type, QWidget* parent = nullptr);

    std::optional<QString> selectedPortablePath() const { return m_selected; }

    static std::optional<QString> Choose(AssetType type, QWidget* parent,
                                         const QString& currentPath = {});

private:
    void rebuildList();
    AssetType m_type = AssetType::Unknown;
    QLineEdit* m_search = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_preview = nullptr;
    std::optional<QString> m_selected;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
