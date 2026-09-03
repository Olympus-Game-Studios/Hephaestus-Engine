#if HEPHAESTUS_EDITOR_QT_UI

#include "CharacterTimelineWidget.h"

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
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30));
    const QRect track(12, height() / 2 - 6, width() - 24, 12);
    p.fillRect(track, QColor(50, 50, 50));
    const int x0 = xAtTime(m_trimStart);
    const int x1 = xAtTime(m_trimEnd);
    p.fillRect(QRect(QPoint(x0, track.top()), QPoint(x1, track.bottom())), QColor(70, 120, 180));
    const int playX = xAtTime(m_time);
    p.setPen(QPen(QColor(255, 200, 80), 2));
    p.drawLine(playX, 8, playX, height() - 8);
    p.setPen(QColor(180, 180, 180));
    p.drawText(12, 14, QString("%1s").arg(m_trimStart, 0, 'f', 2));
    p.drawText(width() - 80, 14, QString("%1s").arg(m_trimEnd, 0, 'f', 2));
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
