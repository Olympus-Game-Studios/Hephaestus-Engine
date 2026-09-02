#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QString>
#include <functional>
#include <string>
#include <vector>

namespace Hephaestus {
namespace QtEditor {

struct AddComponentItem {
    QString id;
    QString displayName;
    QString category;
    bool alreadyPresent = false;
    bool disabled = false;
    QString disabledReason;
};

class AddComponentPopup : public QWidget
{
    Q_OBJECT
public:
    explicit AddComponentPopup(QWidget* parent = nullptr);

    void Present(const QPoint& globalPos, const std::vector<AddComponentItem>& items);

signals:
    void componentChosen(const QString& typeId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuildList();
    QLineEdit* m_search = nullptr;
    QListWidget* m_list = nullptr;
    std::vector<AddComponentItem> m_items;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
