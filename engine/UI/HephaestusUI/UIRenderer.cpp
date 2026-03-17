#include "UIRenderer.h"

namespace Hephaestus::HephaestusUI {

void UIRenderer::Reset() {
    m_Commands.clear();
}

void UIRenderer::DrawRect(const UIDrawRectCommand& command) {
    m_Commands.emplace_back(command);
}

void UIRenderer::DrawImage(const UIDrawImageCommand& command) {
    m_Commands.emplace_back(command);
}

void UIRenderer::DrawText(const UIDrawTextCommand& command) {
    m_Commands.emplace_back(command);
}

void UIRenderer::PushClipRect(const UIRect& rect) {
    m_Commands.emplace_back(UIPushClipRectCommand{rect});
}

void UIRenderer::PopClipRect() {
    m_Commands.emplace_back(UIPopClipRectCommand{});
}

const std::vector<UIDrawCommand>& UIRenderer::GetDrawCommands() const {
    return m_Commands;
}

} // namespace Hephaestus::HephaestusUI
