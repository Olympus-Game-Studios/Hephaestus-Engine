#pragma once

#include <cstdint>

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

struct UIStyle {
    UIColor backgroundColor = UIColor::Transparent();
    UIColor borderColor = UIColor::Transparent();
    float borderWidth = 0.0f;
    float cornerRadius = 0.0f;
    UIColor textColor = UIColor::White();
    UISpacing padding = {};
};

} // namespace Hephaestus::HephaestusUI
