#pragma once

#include <QStyledItemDelegate>

namespace Hephaestus::Launcher {

class ProjectCardDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ProjectCardDelegate(QObject* parent = nullptr);

    static constexpr int kRowHeight = 57;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace Hephaestus::Launcher
