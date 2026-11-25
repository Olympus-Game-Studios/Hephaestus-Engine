#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../HephaestusUI/UIRenderer.h"

#include <QImage>
#include <QWidget>
#include <functional>
#include <string>
#include <unordered_map>
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
    // Loaded images by imageId (a null image when it could not be loaded), and tinted copies by imageId + tint.
    std::unordered_map<std::string, QImage> m_images;
    std::unordered_map<std::string, QImage> m_tintedImages;
    std::unordered_map<std::string, uint64_t> m_dynamicVersions; // UIDynamicImages versions last converted

    const QImage& TintedImage(const std::string& imageId, const Hephaestus::HephaestusUI::UIColor& tint);
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
