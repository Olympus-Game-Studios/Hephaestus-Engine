#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include "../HephaestusUI/UIRenderer.h"

#include <QWidget>
#include <vector>

namespace Hephaestus {
namespace QtEditor {

class RuntimeUIOverlayWidget : public QWidget
{
public:
    explicit RuntimeUIOverlayWidget(QWidget* parent = nullptr);

    void SetDrawCommands(const std::vector<Hephaestus::HephaestusUI::UIDrawCommand>& commands);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<Hephaestus::HephaestusUI::UIDrawCommand> m_drawCommands;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
