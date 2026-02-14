#include "ProjectCardDelegate.h"

#include "ProjectCardModel.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace Hephaestus::Launcher {

ProjectCardDelegate::ProjectCardDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ProjectCardDelegate::SetPlaceholderImagePath(const QString& placeholderPath)
{
    m_placeholderImagePath = placeholderPath;
}

QSize ProjectCardDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return {320, 230};
}

QRect ProjectCardDelegate::CardRect(const QStyleOptionViewItem& option) const
{
    return option.rect.adjusted(8, 8, -8, -8);
}

QRect ProjectCardDelegate::StarRect(const QRect& cardRect) const
{
    return QRect(cardRect.right() - 34, cardRect.top() + 8, 24, 24);
}

void ProjectCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect card = CardRect(option);
    const QRect thumbnailRect(card.left() + 10, card.top() + 10, card.width() - 20, 120);
    const QRect textRect(card.left() + 12, thumbnailRect.bottom() + 10, card.width() - 24, 80);
    const QRect starRect = StarRect(card);

    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;

    QColor base = QColor(35, 41, 52);
    QColor border = QColor(68, 78, 98);
    if (hovered) {
        base = QColor(42, 50, 64);
        border = QColor(111, 132, 193);
    }
    if (selected) {
        base = QColor(47, 56, 74);
        border = QColor(123, 151, 235);
    }

    QLinearGradient grad(card.topLeft(), card.bottomLeft());
    grad.setColorAt(0.0, base.lighter(106));
    grad.setColorAt(1.0, base);

    painter->setPen(QPen(border, 1.0));
    painter->setBrush(grad);
    painter->drawRoundedRect(card, 8, 8);

    if (hovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(110, 133, 208, 32));
        painter->drawRoundedRect(card.adjusted(-2, -2, 2, 2), 10, 10);
    }

    QString thumbnailPath = index.data(ProjectCardModel::ThumbnailRole).toString();
    if (thumbnailPath.isEmpty()) {
        thumbnailPath = m_placeholderImagePath;
    }

    QPixmap thumb;
    if (!thumbnailPath.isEmpty()) {
        thumb.load(thumbnailPath);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(25, 30, 38));
    painter->drawRoundedRect(thumbnailRect, 6, 6);

    if (!thumb.isNull()) {
        const QPixmap scaled = thumb.scaled(thumbnailRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QPainterPath clipPath;
        clipPath.addRoundedRect(thumbnailRect, 6, 6);
        painter->setClipPath(clipPath);
        painter->drawPixmap(thumbnailRect.topLeft(), scaled, QRect((scaled.width() - thumbnailRect.width()) / 2,
                                                                  (scaled.height() - thumbnailRect.height()) / 2,
                                                                  thumbnailRect.width(),
                                                                  thumbnailRect.height()));
        painter->setClipping(false);
    } else {
        painter->setPen(QColor(130, 142, 168));
        painter->drawText(thumbnailRect, Qt::AlignCenter, QStringLiteral("Hephaestus"));
    }

    const bool favorite = index.data(ProjectCardModel::FavoriteRole).toBool();
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(18, 22, 29, 200));
    painter->drawRoundedRect(starRect.adjusted(-2, -2, 2, 2), 4, 4);
    painter->setPen(favorite ? QColor(255, 215, 102) : QColor(168, 176, 196));
    painter->drawText(starRect, Qt::AlignCenter, favorite ? QStringLiteral("★") : QStringLiteral("☆"));

    QString name = index.data(ProjectCardModel::NameRole).toString();
    QString engineVersion = index.data(ProjectCardModel::EngineVersionRole).toString();
    QStringList tags = index.data(ProjectCardModel::TagsRole).toStringList();

    painter->setPen(QColor(233, 237, 247));
    QFont nameFont = painter->font();
    nameFont.setPointSize(10);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    painter->drawText(QRect(textRect.left(), textRect.top(), textRect.width(), 24), Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, textRect.width()));

    painter->setPen(QColor(157, 168, 192));
    QFont infoFont = painter->font();
    infoFont.setPointSize(8);
    infoFont.setBold(false);
    painter->setFont(infoFont);
    painter->drawText(QRect(textRect.left(), textRect.top() + 24, textRect.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                      QStringLiteral("Engine %1").arg(engineVersion));

    int tagX = textRect.left();
    int tagY = textRect.top() + 48;
    for (int i = 0; i < tags.size() && i < 3; ++i) {
        const QString tag = tags[i];
        const int w = qMin(92, QFontMetrics(infoFont).horizontalAdvance(tag) + 14);
        QRect badge(tagX, tagY, w, 18);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(75, 88, 116, 190));
        painter->drawRoundedRect(badge, 9, 9);
        painter->setPen(QColor(222, 228, 243));
        painter->drawText(badge, Qt::AlignCenter, QFontMetrics(infoFont).elidedText(tag, Qt::ElideRight, w - 8));
        tagX += w + 6;
    }

    const bool broken = index.data(ProjectCardModel::BrokenRole).toBool();
    if (broken) {
        QRect overlay = QRect(card.left() + 10, card.top() + 10, card.width() - 20, 24);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(170, 56, 72, 210));
        painter->drawRoundedRect(overlay, 5, 5);
        painter->setPen(QColor(255, 239, 241));
        painter->drawText(overlay, Qt::AlignCenter, QStringLiteral("⚠ Broken Project Metadata"));
    }

    painter->restore();
}

bool ProjectCardDelegate::editorEvent(QEvent* event,
                                      QAbstractItemModel* model,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index)
{
    if (!event || !index.isValid()) {
        return false;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QRect starRect = StarRect(CardRect(option));
        if (starRect.contains(mouseEvent->pos())) {
            const bool favorite = index.data(ProjectCardModel::FavoriteRole).toBool();
            emit FavoriteToggled(index, !favorite);
            return true;
        }
    }

    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

} // namespace Hephaestus::Launcher
