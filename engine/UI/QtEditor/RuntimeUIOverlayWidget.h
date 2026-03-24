#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../HephaestusUI/UIRenderer.h"

#include <QWidget>
#include <functional>
#include <vector>

namespace Hephaestus {
namespace QtEditor {

struct UIMouseEventData {
    enum class Type { Press, Release, Move };
    Type type;
    float x;
    float y;
    int button;
};

using UIMouseEventCallback = std::function<void(const UIMouseEventData&)>;

class RuntimeUIOverlayWidget : public QWidget
{
public:
    explicit RuntimeUIOverlayWidget(QWidget* parent = nullptr);

    void SetDrawCommands(const std::vector<Hephaestus::HephaestusUI::UIDrawCommand>& commands);
    
    void SetMouseEventCallback(UIMouseEventCallback callback) { m_mouseCallback = std::move(callback); }
    void SetMouseEventsEnabled(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    std::vector<Hephaestus::HephaestusUI::UIDrawCommand> m_drawCommands;
    UIMouseEventCallback m_mouseCallback;
    bool m_mouseEventsEnabled = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
