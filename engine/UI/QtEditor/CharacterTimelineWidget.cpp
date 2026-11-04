#if HEPHAESTUS_EDITOR_QT_UI

#include "CharacterTimelineWidget.h"
#include "EditorTheme.h"

#include <QPainter>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace Hephaestus {
namespace QtEditor {

CharacterTimelineWidget::CharacterTimelineWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(56);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void CharacterTimelineWidget::setRange(float sourceDuration, float trimStart, float trimEnd, float currentTime) {
    m_duration = std::max(0.001f, sourceDuration);
    m_trimStart = std::max(0.0f, trimStart);
    m_trimEnd = std::max(m_trimStart + 0.001f, trimEnd);
    m_time = std::clamp(currentTime, m_trimStart, m_trimEnd);
    update();
}

float CharacterTimelineWidget::timeAtX(int x) const {
    const int left = 12;
    const int right = width() - 12;
    const float t = static_cast<float>(x - left) / std::max(1, right - left);
    return std::clamp(t, 0.0f, 1.0f) * m_duration;
}

int CharacterTimelineWidget::xAtTime(float time) const {
    const int left = 12;
    const int right = width() - 12;
    const float t = std::clamp(time / m_duration, 0.0f, 1.0f);
    return left + static_cast<int>(t * static_cast<float>(right - left));
}

void CharacterTimelineWidget::paintEvent(QPaintEvent*) {
    using Theme::Token;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), Theme::Color(Token::SurfaceBase));
    const QRectF track(12.0, height() / 2.0 - 8.0, width() - 24.0, 16.0);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::Color(Token::SurfaceControl));
    p.drawRoundedRect(track, 4.0, 4.0);
    const int x0 = xAtTime(m_trimStart);
    const int x1 = xAtTime(m_trimEnd);
    const QRectF trimmed(x0, track.top(), x1 - x0, track.height());
    p.setBrush(Theme::Color(Token::SurfaceSelectedStrong));
    p.setPen(QPen(Theme::Color(Token::AccentSelection), 1.0));
    p.drawRoundedRect(trimmed.adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
    const int playX = xAtTime(m_time);
    p.setPen(QPen(Theme::Color(Token::AccentBrand), 2));
    p.drawLine(playX, 6, playX, height() - 6);
    p.setPen(Theme::Color(Token::TextMuted));
    p.drawText(QRect(12, 0, width() - 24, 16), Qt::AlignLeft | Qt::AlignVCenter, QString("%1 s").arg(m_trimStart, 0, 'f', 2));
    p.drawText(QRect(12, 0, width() - 24, 16), Qt::AlignRight | Qt::AlignVCenter, QString("%1 s").arg(m_trimEnd, 0, 'f', 2));
    p.setPen(Theme::Color(Token::TextPrimary));
    p.drawText(QRect(12, 0, width() - 24, 16), Qt::AlignHCenter | Qt::AlignVCenter, QString("%1 s").arg(m_time, 0, 'f', 2));
}

void CharacterTimelineWidget::mousePressEvent(QMouseEvent* event) {
    m_dragging = true;
    const float t = std::clamp(timeAtX(event->pos().x()), m_trimStart, m_trimEnd);
    m_time = t;
    emit timeScrubbed(t);
    update();
}

void CharacterTimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging) {
        return;
    }
    const float t = std::clamp(timeAtX(event->pos().x()), m_trimStart, m_trimEnd);
    m_time = t;
    emit timeScrubbed(t);
    update();
}

void CharacterTimelineWidget::mouseReleaseEvent(QMouseEvent*) {
    m_dragging = false;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
