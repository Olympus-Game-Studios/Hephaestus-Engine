#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QWidget>

namespace Hephaestus {
namespace QtEditor {

class CharacterTimelineWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CharacterTimelineWidget(QWidget* parent = nullptr);

    void setRange(float sourceDuration, float trimStart, float trimEnd, float currentTime);
    float currentTime() const { return m_time; }

signals:
    void timeScrubbed(float time);
    void trimChanged(float start, float end);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    float timeAtX(int x) const;
    int xAtTime(float time) const;

    float m_duration = 1.0f;
    float m_trimStart = 0.0f;
    float m_trimEnd = 1.0f;
    float m_time = 0.0f;
    bool m_dragging = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
