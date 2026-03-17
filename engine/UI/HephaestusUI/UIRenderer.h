#pragma once

#include "UIStyle.h"

#include <string>
#include <variant>
#include <vector>

namespace Hephaestus::HephaestusUI {

struct UIDrawRectCommand {
    UIRect rect;
    UIColor fillColor;
    UIColor borderColor;
    float borderWidth = 0.0f;
    float cornerRadius = 0.0f;
    float opacity = 1.0f;
    int zOrder = 0;
};

struct UIDrawImageCommand {
    UIRect rect;
    std::string imageId;
    UIColor tint = UIColor::White();
    float opacity = 1.0f;
    int zOrder = 0;
};

struct UIDrawTextCommand {
    UIRect rect;
    std::string text;
    std::string fontFamily;
    float fontSize = 16.0f;
    UITextAlignment alignment = UITextAlignment::Left;
    bool wordWrap = false;
    UIColor textColor = UIColor::White();
    float opacity = 1.0f;
    int zOrder = 0;
};

struct UIPushClipRectCommand {
    UIRect rect;
};

struct UIPopClipRectCommand {};

using UIDrawCommand = std::variant<UIDrawRectCommand,
                                   UIDrawImageCommand,
                                   UIDrawTextCommand,
                                   UIPushClipRectCommand,
                                   UIPopClipRectCommand>;

class UIRenderer {
public:
    void Reset();

    void DrawRect(const UIDrawRectCommand& command);
    void DrawImage(const UIDrawImageCommand& command);
    void DrawText(const UIDrawTextCommand& command);
    void PushClipRect(const UIRect& rect);
    void PopClipRect();

    [[nodiscard]] const std::vector<UIDrawCommand>& GetDrawCommands() const;

private:
    std::vector<UIDrawCommand> m_Commands;
};

} // namespace Hephaestus::HephaestusUI
