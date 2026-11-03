#include "ProjectCardDelegate.h"

#include "ProjectCardModel.h"
#include "../QtEditor/EditorTheme.h"

#include <QDateTime>
#include <QDir>
#include <QPainter>

namespace Hephaestus::Launcher {

namespace Theme = Hephaestus::QtEditor::Theme;
using Theme::Token;

namespace {

constexpr int kPadding = 16;
constexpr int kIconSize = 16;
constexpr int kTimeWidth = 96;
constexpr int kBadgeHeight = 20;

QFont SizedFont(const QFont& base, int pixelSize, QFont::Weight weight)
{
    QFont font = base;
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    return font;
}

} // namespace

ProjectCardDelegate::ProjectCardDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QSize ProjectCardDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    return {option.rect.width(), kRowHeight};
}

void ProjectCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRect row = option.rect;
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    if (selected) {
        painter->fillRect(row, Theme::Color(Token::SurfaceSelected));
    } else if (option.state.testFlag(QStyle::State_MouseOver)) {
        painter->fillRect(row, Theme::Color(Token::SurfaceRaised));
    }
    if (index.row() + 1 < index.model()->rowCount()) {
        painter->setPen(Theme::Color(Token::BorderSubtle));
        painter->drawLine(row.bottomLeft(), row.bottomRight());
    }

    const QRect iconRect(row.left() + kPadding, row.center().y() - kIconSize / 2, kIconSize, kIconSize);
    Theme::Icon(QStringLiteral("folder"), selected ? Theme::IconTone::Action : Theme::IconTone::Secondary)
        .paint(painter, iconRect);

    const QRect timeRect(row.right() - kPadding - kTimeWidth, row.top(), kTimeWidth, row.height());
    const QFont timeFont = SizedFont(option.font, 12, QFont::Normal);
    painter->setFont(timeFont);
    painter->setPen(Theme::Color(Token::TextMuted));
    painter->drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter,
                      RelativeTimeText(index.data(ProjectCardModel::LastModifiedRole).toDateTime(),
                                       QDateTime::currentDateTime()));

    int textRight = timeRect.left() - kPadding;
    const QString mode = index.data(ProjectCardModel::EditorModeRole).toString();
    if (mode == QLatin1String("default") || mode == QLatin1String("artist") || mode == QLatin1String("programmer")) {
        QFont badgeFont = SizedFont(option.font, 11, QFont::Medium);
        badgeFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
        const QString badgeText = mode.toUpper();
        const int badgeWidth = QFontMetrics(badgeFont).horizontalAdvance(badgeText) + 12;
        const QRect badgeRect(textRight - badgeWidth, row.center().y() - kBadgeHeight / 2, badgeWidth, kBadgeHeight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(Theme::Color(Token::SurfaceControl));
        painter->drawRoundedRect(badgeRect, 3, 3);
        painter->setFont(badgeFont);
        painter->setPen(Theme::Color(Token::TextSecondary));
        painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
        textRight = badgeRect.left() - kPadding;
    }

    const int textLeft = iconRect.right() + 13;
    const int textWidth = qMax(0, textRight - textLeft);
    const QFont nameFont = SizedFont(option.font, 13, QFont::Medium);
    const QFont pathFont = SizedFont(option.font, 11, QFont::Normal);
    const int nameHeight = QFontMetrics(nameFont).height();
    const int pathHeight = QFontMetrics(pathFont).height();
    const int textTop = row.center().y() - (nameHeight + 2 + pathHeight) / 2;

    painter->setFont(nameFont);
    painter->setPen(Theme::Color(Token::TextPrimary));
    painter->drawText(QRect(textLeft, textTop, textWidth, nameHeight), Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(nameFont).elidedText(index.data(ProjectCardModel::NameRole).toString(),
                                                         Qt::ElideRight, textWidth));

    const bool broken = index.data(ProjectCardModel::BrokenRole).toBool();
    const QString detail = broken
        ? index.data(ProjectCardModel::BrokenReasonRole).toString()
        : QDir::toNativeSeparators(index.data(ProjectCardModel::ProjectPathRole).toString());
    painter->setFont(pathFont);
    painter->setPen(Theme::Color(broken ? Token::StatusError : Token::TextMuted));
    painter->drawText(QRect(textLeft, textTop + nameHeight + 2, textWidth, pathHeight), Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(pathFont).elidedText(detail, Qt::ElideMiddle, textWidth));

    painter->restore();
}

} // namespace Hephaestus::Launcher
