#pragma once

#include "UIStyle.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct JsonValue;

namespace Hephaestus::HephaestusUI {

class UIElement;
class UIScreen;
class UISystem;

// Supported widget types in .hui files
enum class HUIWidgetType {
    Panel,
    Canvas,
    VerticalBox,
    HorizontalBox,
    Grid,
    Text,
    Button,
    Image
};

struct HUIWidgetNode {
    HUIWidgetType type = HUIWidgetType::Panel;
    std::string id;
    std::string styleClass;

    // Common properties
    UIVec2 position = {0.0f, 0.0f};
    UIVec2 size = {0.0f, 0.0f};
    UIVec2 minSize = {0.0f, 0.0f};
    UIVec2 maxSize = {0.0f, 0.0f};
    UIAnchorPreset anchor = UIAnchorPreset::TopLeft;
    UIHAlignment hAlignment = UIHAlignment::Left;
    UIVAlignment vAlignment = UIVAlignment::Top;
    UISpacing padding = {};
    UISpacing margin = {};
    bool visible = true;
    bool enabled = true;
    float opacity = 1.0f;
    int zOrder = 0;
    bool interactable = true;
    bool clippingEnabled = false;

    // Text-specific
    std::string text;
    UITextAlignment textAlignment = UITextAlignment::Left;
    bool wordWrap = false;

    // Image-specific
    std::string imageId;
    UIImageScaleMode imageScaleMode = UIImageScaleMode::Stretch;
    UIColor imageTint = UIColor::White();

    // Container-specific
    float spacing = 8.0f;
    int gridColumns = 2;
    float gridHSpacing = 8.0f;
    float gridVSpacing = 8.0f;
    UIHAlignment childHAlignment = UIHAlignment::Stretch;
    UIVAlignment childVAlignment = UIVAlignment::Stretch;

    // Inline style
    UIStyle style;
    bool hasStyle = false;

    // Button state styles
    UIStyle normalStyle;
    UIStyle hoveredStyle;
    UIStyle pressedStyle;
    UIStyle disabledStyle;
    bool hasNormalStyle = false;
    bool hasHoveredStyle = false;
    bool hasPressedStyle = false;
    bool hasDisabledStyle = false;

    std::vector<HUIWidgetNode> children;
};

struct HUIDocument {
    int version = 1;
    std::string controller;
    HUIWidgetNode root;

    [[nodiscard]] bool isValid() const { return !root.id.empty(); }
};

struct HUIError {
    std::string message;
    std::string context;
};

/**
 * Parses .hui JSON files into HUIDocument structures and instantiates
 * runtime UI widget trees.
 */
class HUIParser {
public:
    /**
     * Parse a .hui file from disk into an HUIDocument.
     * Returns true on success. On failure, populates errors vector.
     */
    bool ParseFile(const std::string& filepath, HUIDocument& outDocument);

    /**
     * Parse a .hui JSON string into an HUIDocument.
     */
    bool ParseString(const std::string& source, HUIDocument& outDocument);

    [[nodiscard]] const std::vector<HUIError>& GetErrors() const { return m_Errors; }
    [[nodiscard]] std::string GetErrorString() const;

private:
    bool parseDocument(const JsonValue& root, HUIDocument& doc);
    bool parseWidget(const JsonValue& node, HUIWidgetNode& widget);
    bool parseProperties(const JsonValue& props, HUIWidgetNode& widget);
    bool parseStyleBlock(const JsonValue& styleVal, UIStyle& style);
    bool parseColor(const JsonValue& colorVal, UIColor& color);
    bool parseSpacing(const JsonValue& spacingVal, UISpacing& spacing);
    bool parseVec2(const JsonValue& vecVal, UIVec2& vec);

    UIAnchorPreset parseAnchorPreset(const std::string& value);
    UIHAlignment parseHAlignment(const std::string& value);
    UIVAlignment parseVAlignment(const std::string& value);
    UITextAlignment parseTextAlignment(const std::string& value);
    UIImageScaleMode parseImageScaleMode(const std::string& value);
    HUIWidgetType parseWidgetType(const std::string& value, bool& valid);

    void addError(const std::string& message, const std::string& context = "");
    void validateWidgetIds(const HUIWidgetNode& node, std::unordered_set<std::string>& seenIds);

    std::vector<HUIError> m_Errors;
};

/**
 * Builds runtime UIElement widget trees from parsed HUIDocument data.
 * Exposes an id-to-element lookup map for Lua/script binding.
 */
class HUIBuilder {
public:
    struct BuildResult {
        std::shared_ptr<UIScreen> screen;
        std::unordered_map<std::string, UIElement*> widgetMap;
        std::string controller;
        bool success = false;
    };

    /**
     * Build a UIScreen from a parsed HUIDocument.
     */
    BuildResult Build(const HUIDocument& document);

private:
    std::unique_ptr<UIElement> buildWidget(const HUIWidgetNode& node,
                                           std::unordered_map<std::string, UIElement*>& widgetMap);
    void applyCommonProperties(UIElement* element, const HUIWidgetNode& node);
    void applyStyle(UIElement* element, const HUIWidgetNode& node);
};

/**
 * Generates valid .hui JSON content for use as starter templates.
 */
std::string generateDefaultHUIContent(const std::string& name);

/**
 * Serializes an HUIDocument back to JSON string.
 * Supports deterministic output ordering for round-trip stability.
 */
std::string serializeHUIDocument(const HUIDocument& document);

} // namespace Hephaestus::HephaestusUI
