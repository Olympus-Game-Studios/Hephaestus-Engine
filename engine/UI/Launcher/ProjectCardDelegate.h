#pragma once

#include <QStyledItemDelegate>

namespace Hephaestus::Launcher {

class ProjectCardDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ProjectCardDelegate(QObject* parent = nullptr);

    void SetPlaceholderImagePath(const QString& placeholderPath);

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

signals:
    void FavoriteToggled(const QModelIndex& index, bool favorite) const;

private:
    QRect CardRect(const QStyleOptionViewItem& option) const;
    QRect StarRect(const QRect& cardRect) const;

private:
    QString m_placeholderImagePath;
};

} // namespace Hephaestus::Launcher
