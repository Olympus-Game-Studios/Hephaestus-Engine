#include "UIRenderer.h"

namespace Hephaestus::HephaestusUI {

namespace {

UIRect Scaled(const UIRect& rect, float scale) {
    return {rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale};
}

} // namespace

void UIRenderer::Reset() {
    m_Commands.clear();
}

void UIRenderer::DrawRect(const UIDrawRectCommand& command) {
    UIDrawRectCommand scaled = command;
    scaled.rect = Scaled(command.rect, m_Scale);
    scaled.borderWidth = command.borderWidth * m_Scale;
    scaled.cornerRadius = command.cornerRadius * m_Scale;
    m_Commands.emplace_back(std::move(scaled));
}

void UIRenderer::DrawImage(const UIDrawImageCommand& command) {
    UIDrawImageCommand scaled = command;
    scaled.rect = Scaled(command.rect, m_Scale);
    m_Commands.emplace_back(std::move(scaled));
}

void UIRenderer::DrawText(const UIDrawTextCommand& command) {
    UIDrawTextCommand scaled = command;
    scaled.rect = Scaled(command.rect, m_Scale);
    scaled.fontSize = command.fontSize * m_Scale;
    scaled.shadowOffset = {command.shadowOffset.x * m_Scale, command.shadowOffset.y * m_Scale};
    m_Commands.emplace_back(std::move(scaled));
}

void UIRenderer::PushClipRect(const UIRect& rect) {
    m_Commands.emplace_back(UIPushClipRectCommand{Scaled(rect, m_Scale)});
}

void UIRenderer::PopClipRect() {
    m_Commands.emplace_back(UIPopClipRectCommand{});
}

const std::vector<UIDrawCommand>& UIRenderer::GetDrawCommands() const {
    return m_Commands;
}

} // namespace Hephaestus::HephaestusUI
