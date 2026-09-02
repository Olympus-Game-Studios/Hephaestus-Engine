#if HEPHAESTUS_EDITOR_QT_UI

#include "AddComponentPopup.h"

#include <QEvent>
#include <QKeyEvent>
#include <QVBoxLayout>

namespace Hephaestus {
namespace QtEditor {

AddComponentPopup::AddComponentPopup(QWidget* parent)
    : QWidget(parent, Qt::Popup)
{
    setMinimumWidth(260);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search components…"));
    m_list = new QListWidget(this);
    m_list->setMinimumHeight(220);
    layout->addWidget(m_search);
    layout->addWidget(m_list);
    m_search->installEventFilter(this);
    m_list->installEventFilter(this);

    connect(m_search, &QLineEdit::textChanged, this, [this]() { rebuildList(); });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (!item || !(item->flags() & Qt::ItemIsEnabled)) return;
        emit componentChosen(item->data(Qt::UserRole).toString());
        close();
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item || !(item->flags() & Qt::ItemIsEnabled)) return;
        emit componentChosen(item->data(Qt::UserRole).toString());
        close();
    });
}

void AddComponentPopup::Present(const QPoint& globalPos, const std::vector<AddComponentItem>& items)
{
    m_items = items;
    m_search->clear();
    rebuildList();
    move(globalPos);
    show();
    m_search->setFocus();
}

void AddComponentPopup::rebuildList()
{
    m_list->clear();
    const QString query = m_search->text().trimmed().toLower();
    for (const auto& item : m_items) {
        if (!query.isEmpty() &&
            !item.displayName.toLower().contains(query) &&
            !item.id.toLower().contains(query) &&
            !item.category.toLower().contains(query)) {
            continue;
        }
        QString label = item.displayName;
        if (item.alreadyPresent) {
            label += tr(" (already present)");
        }
        auto* row = new QListWidgetItem(label, m_list);
        row->setData(Qt::UserRole, item.id);
        if (item.disabled || item.alreadyPresent) {
            row->setFlags(row->flags() & ~Qt::ItemIsEnabled);
            if (!item.disabledReason.isEmpty()) {
                row->setToolTip(item.disabledReason);
            }
        }
    }
}

bool AddComponentPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            close();
            return true;
        }
        if (watched == m_search && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
            m_list->setFocus();
            if (m_list->count() > 0 && m_list->currentRow() < 0) {
                m_list->setCurrentRow(0);
            }
            return false;
        }
        if (watched == m_list && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            if (auto* item = m_list->currentItem()) {
                if (item->flags() & Qt::ItemIsEnabled) {
                    emit componentChosen(item->data(Qt::UserRole).toString());
                    close();
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
