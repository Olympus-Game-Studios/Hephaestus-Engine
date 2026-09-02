#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../../Assets/AssetTypes.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>
#include <QString>

class QMimeData;

namespace Hephaestus {
namespace QtEditor {

class AssetReferenceEditor : public QWidget
{
    Q_OBJECT
public:
    explicit AssetReferenceEditor(const QString& label, AssetType type, QWidget* parent = nullptr);

    void setPath(const QString& portablePath, bool mixed = false, bool missing = false);
    QString path() const { return m_path; }
    bool isMixed() const { return m_mixed; }
    void setAllowEmpty(bool allow) { m_allowEmpty = allow; }

signals:
    void pathEdited(const QString& portablePath);
    void openRequested(const QString& portablePath);
    void revealRequested(const QString& portablePath);
    void findReferencesRequested(const QString& portablePath);
    void copyPathRequested(const QString& portablePath);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void browse();
    void clearPath();
    void refreshDisplay();
    bool acceptDrop(const QMimeData* mime, QString* outPath) const;

    AssetType m_type = AssetType::Unknown;
    QString m_path;
    bool m_mixed = false;
    bool m_missing = false;
    bool m_allowEmpty = true;
    QLabel* m_label = nullptr;
    QLineEdit* m_name = nullptr;
    QLabel* m_warning = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPushButton* m_openBtn = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
