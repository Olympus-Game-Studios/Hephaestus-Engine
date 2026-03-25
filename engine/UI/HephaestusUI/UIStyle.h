#pragma once

#include <cstdint>
#include <string>

namespace Hephaestus::HephaestusUI {

struct UIVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct UIRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    [[nodiscard]] bool Contains(const UIVec2& point) const {
        return point.x >= x && point.x <= (x + width) && point.y >= y && point.y <= (y + height);
    }
};

struct UIColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    static UIColor White() { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static UIColor Black() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    static UIColor Transparent() { return {0.0f, 0.0f, 0.0f, 0.0f}; }
};

struct UISpacing {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    static UISpacing Uniform(float value) {
        return {value, value, value, value};
    }
};

enum class UIAnchorPreset {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    Stretch
};

struct UIAnchor {
    UIAnchorPreset preset = UIAnchorPreset::TopLeft;
};

enum class UITextAlignment {
    Left,
    Center,
    Right
};

enum class UIHAlignment {
    Left,
    Center,
    Right,
    Stretch
};

enum class UIVAlignment {
    Top,
    Center,
    Bottom,
    Stretch
};

enum class UIImageScaleMode {
    Stretch,
    Fit,
    Fill,
    Center
};

struct UIStyle {
    UIColor backgroundColor = UIColor::Transparent();
    std::string backgroundImageId;
    UIImageScaleMode imageScaleMode = UIImageScaleMode::Stretch;
    UIColor imageTint = UIColor::White();

    UIColor borderColor = UIColor::Transparent();
    float borderWidth = 0.0f;
    float cornerRadius = 0.0f;

    UIColor textColor = UIColor::White();
    std::string fontFamily;
    float fontSize = 0.0f;
    bool fontBold = false;
    bool fontItalic = false;
    UITextAlignment textAlignment = UITextAlignment::Left;

    UISpacing padding = {};

    float opacity = 1.0f;
    float scale = 1.0f;

    bool textShadow = false;
    UIColor textShadowColor = {0.0f, 0.0f, 0.0f, 0.5f};
    UIVec2 textShadowOffset = {1.0f, 1.0f};
};

} // namespace Hephaestus::HephaestusUI
