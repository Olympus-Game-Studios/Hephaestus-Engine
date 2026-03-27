#include "HUIAsset.h"
#include "UICanvas.h"
#include "UIPanel.h"
#include "UIText.h"
#include "UIButton.h"
#include "UIImage.h"
#include "UIVerticalBox.h"
#include "UIHorizontalBox.h"
#include "UIGridLayout.h"
#include "UIScreen.h"
#include "UISystem.h"
#include "../../Scene/Json.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace Hephaestus::HephaestusUI {

// ─────────────────────────────────────────────────────────────────────────────
// HUIParser
// ─────────────────────────────────────────────────────────────────────────────

bool HUIParser::ParseFile(const std::string& filepath, HUIDocument& outDocument)
{
    m_Errors.clear();

    std::ifstream file(filepath);
    if (!file.is_open()) {
        addError("Failed to open file: " + filepath);
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ParseString(ss.str(), outDocument);
}

bool HUIParser::ParseString(const std::string& source, HUIDocument& outDocument)
{
    m_Errors.clear();

    if (source.empty()) {
        addError("Empty .hui source");
        return false;
    }

    JsonParser jsonParser(source);
    JsonValue root;
    std::string jsonError;

    if (!jsonParser.Parse(root, jsonError)) {
        addError("JSON parse error: " + jsonError);
        return false;
    }

    if (!root.IsObject()) {
        addError("Root element must be a JSON object");
        return false;
    }

    if (!parseDocument(root, outDocument)) {
        return false;
    }

    std::unordered_set<std::string> seenIds;
    validateWidgetIds(outDocument.root, seenIds);

    return m_Errors.empty();
}

std::string HUIParser::GetErrorString() const
{
    std::string result;
    for (const auto& err : m_Errors) {
        if (!result.empty()) result += "\n";
        result += "[HUI] ";
        if (!err.context.empty()) result += err.context + ": ";
        result += err.message;
    }
    return result;
}

bool HUIParser::parseDocument(const JsonValue& root, HUIDocument& doc)
{
    auto* versionVal = root.Find("version");
    if (versionVal && versionVal->IsNumber()) {
        doc.version = static_cast<int>(versionVal->AsNumber());
    }

    auto* controllerVal = root.Find("controller");
    if (controllerVal && controllerVal->IsString()) {
        doc.controller = controllerVal->AsString();
    }

    auto* rootWidget = root.Find("root");
    if (!rootWidget || !rootWidget->IsObject()) {
        addError("Missing or invalid 'root' widget definition");
        return false;
    }

    return parseWidget(*rootWidget, doc.root);
}

bool HUIParser::parseWidget(const JsonValue& node, HUIWidgetNode& widget)
{
    auto* typeVal = node.Find("type");
    if (!typeVal || !typeVal->IsString()) {
        addError("Widget missing 'type' field");
        return false;
    }

    bool validType = false;
    widget.type = parseWidgetType(typeVal->AsString(), validType);
    if (!validType) {
        addError("Unknown widget type: '" + typeVal->AsString() + "'",
                 "Valid types: Panel, Canvas, VerticalBox, HorizontalBox, Grid, Text, Button, Image");
        return false;
    }

    auto* idVal = node.Find("id");
    if (idVal && idVal->IsString()) {
        widget.id = idVal->AsString();
    }

    auto* classVal = node.Find("class");
    if (classVal && classVal->IsString()) {
        widget.styleClass = classVal->AsString();
    }

    auto* propsVal = node.Find("properties");
    if (propsVal && propsVal->IsObject()) {
        parseProperties(*propsVal, widget);
    }

    auto* styleVal = node.Find("style");
    if (styleVal && styleVal->IsObject()) {
        widget.hasStyle = true;
        parseStyleBlock(*styleVal, widget.style);
    }

    // Button state styles
    auto* normalStyleVal = node.Find("normalStyle");
    if (normalStyleVal && normalStyleVal->IsObject()) {
        widget.hasNormalStyle = true;
        parseStyleBlock(*normalStyleVal, widget.normalStyle);
    }
    auto* hoveredStyleVal = node.Find("hoveredStyle");
    if (hoveredStyleVal && hoveredStyleVal->IsObject()) {
        widget.hasHoveredStyle = true;
        parseStyleBlock(*hoveredStyleVal, widget.hoveredStyle);
    }
    auto* pressedStyleVal = node.Find("pressedStyle");
    if (pressedStyleVal && pressedStyleVal->IsObject()) {
        widget.hasPressedStyle = true;
        parseStyleBlock(*pressedStyleVal, widget.pressedStyle);
    }
    auto* disabledStyleVal = node.Find("disabledStyle");
    if (disabledStyleVal && disabledStyleVal->IsObject()) {
        widget.hasDisabledStyle = true;
        parseStyleBlock(*disabledStyleVal, widget.disabledStyle);
    }

    auto* childrenVal = node.Find("children");
    if (childrenVal && childrenVal->IsArray()) {
        for (const auto& childJson : childrenVal->AsArray()) {
            if (!childJson.IsObject()) {
                addError("Child widget must be a JSON object");
                continue;
            }
            HUIWidgetNode child;
            if (parseWidget(childJson, child)) {
                widget.children.push_back(std::move(child));
            }
        }
    }

    return true;
}

bool HUIParser::parseProperties(const JsonValue& props, HUIWidgetNode& widget)
{
    auto* posVal = props.Find("position");
    if (posVal) parseVec2(*posVal, widget.position);

    auto* sizeVal = props.Find("size");
    if (sizeVal) parseVec2(*sizeVal, widget.size);

    auto* minSizeVal = props.Find("minSize");
    if (minSizeVal) parseVec2(*minSizeVal, widget.minSize);

    auto* maxSizeVal = props.Find("maxSize");
    if (maxSizeVal) parseVec2(*maxSizeVal, widget.maxSize);

    auto* anchorVal = props.Find("anchor");
    if (anchorVal && anchorVal->IsString()) {
        widget.anchor = parseAnchorPreset(anchorVal->AsString());
    }

    auto* hAlignVal = props.Find("hAlignment");
    if (hAlignVal && hAlignVal->IsString()) {
        widget.hAlignment = parseHAlignment(hAlignVal->AsString());
    }

    auto* vAlignVal = props.Find("vAlignment");
    if (vAlignVal && vAlignVal->IsString()) {
        widget.vAlignment = parseVAlignment(vAlignVal->AsString());
    }

    auto* paddingVal = props.Find("padding");
    if (paddingVal) parseSpacing(*paddingVal, widget.padding);

    auto* marginVal = props.Find("margin");
    if (marginVal) parseSpacing(*marginVal, widget.margin);

    auto* visibleVal = props.Find("visible");
    if (visibleVal && visibleVal->IsBool()) widget.visible = visibleVal->AsBool();

    auto* enabledVal = props.Find("enabled");
    if (enabledVal && enabledVal->IsBool()) widget.enabled = enabledVal->AsBool();

    auto* opacityVal = props.Find("opacity");
    if (opacityVal && opacityVal->IsNumber()) widget.opacity = static_cast<float>(opacityVal->AsNumber());

    auto* zOrderVal = props.Find("zOrder");
    if (zOrderVal && zOrderVal->IsNumber()) widget.zOrder = static_cast<int>(zOrderVal->AsNumber());

    auto* interactableVal = props.Find("interactable");
    if (interactableVal && interactableVal->IsBool()) widget.interactable = interactableVal->AsBool();

    auto* clippingVal = props.Find("clippingEnabled");
    if (clippingVal && clippingVal->IsBool()) widget.clippingEnabled = clippingVal->AsBool();

    // Text-specific
    auto* textVal = props.Find("text");
    if (textVal && textVal->IsString()) widget.text = textVal->AsString();

    auto* textAlignVal = props.Find("textAlignment");
    if (textAlignVal && textAlignVal->IsString()) {
        widget.textAlignment = parseTextAlignment(textAlignVal->AsString());
    }

    auto* wordWrapVal = props.Find("wordWrap");
    if (wordWrapVal && wordWrapVal->IsBool()) widget.wordWrap = wordWrapVal->AsBool();

    // Image-specific
    auto* imageIdVal = props.Find("imageId");
    if (imageIdVal && imageIdVal->IsString()) widget.imageId = imageIdVal->AsString();

    auto* scaleModeVal = props.Find("imageScaleMode");
    if (scaleModeVal && scaleModeVal->IsString()) {
        widget.imageScaleMode = parseImageScaleMode(scaleModeVal->AsString());
    }

    auto* tintVal = props.Find("imageTint");
    if (tintVal) parseColor(*tintVal, widget.imageTint);

    // Container-specific
    auto* spacingVal = props.Find("spacing");
    if (spacingVal && spacingVal->IsNumber()) widget.spacing = static_cast<float>(spacingVal->AsNumber());

    auto* columnsVal = props.Find("columns");
    if (columnsVal && columnsVal->IsNumber()) widget.gridColumns = static_cast<int>(columnsVal->AsNumber());

    auto* hSpacingVal = props.Find("hSpacing");
    if (hSpacingVal && hSpacingVal->IsNumber()) widget.gridHSpacing = static_cast<float>(hSpacingVal->AsNumber());

    auto* vSpacingVal = props.Find("vSpacing");
    if (vSpacingVal && vSpacingVal->IsNumber()) widget.gridVSpacing = static_cast<float>(vSpacingVal->AsNumber());

    auto* childHAlignVal = props.Find("childHAlignment");
    if (childHAlignVal && childHAlignVal->IsString()) {
        widget.childHAlignment = parseHAlignment(childHAlignVal->AsString());
    }

    auto* childVAlignVal = props.Find("childVAlignment");
    if (childVAlignVal && childVAlignVal->IsString()) {
        widget.childVAlignment = parseVAlignment(childVAlignVal->AsString());
    }

    return true;
}

bool HUIParser::parseStyleBlock(const JsonValue& styleVal, UIStyle& style)
{
    auto* bgColorVal = styleVal.Find("backgroundColor");
    if (bgColorVal) parseColor(*bgColorVal, style.backgroundColor);

    auto* bgImageVal = styleVal.Find("backgroundImageId");
    if (bgImageVal && bgImageVal->IsString()) style.backgroundImageId = bgImageVal->AsString();

    auto* imgScaleModeVal = styleVal.Find("imageScaleMode");
    if (imgScaleModeVal && imgScaleModeVal->IsString()) {
        style.imageScaleMode = parseImageScaleMode(imgScaleModeVal->AsString());
    }

    auto* imgTintVal = styleVal.Find("imageTint");
    if (imgTintVal) parseColor(*imgTintVal, style.imageTint);

    auto* borderColorVal = styleVal.Find("borderColor");
    if (borderColorVal) parseColor(*borderColorVal, style.borderColor);

    auto* borderWidthVal = styleVal.Find("borderWidth");
    if (borderWidthVal && borderWidthVal->IsNumber()) style.borderWidth = static_cast<float>(borderWidthVal->AsNumber());

    auto* cornerRadiusVal = styleVal.Find("cornerRadius");
    if (cornerRadiusVal && cornerRadiusVal->IsNumber()) style.cornerRadius = static_cast<float>(cornerRadiusVal->AsNumber());

    auto* textColorVal = styleVal.Find("textColor");
    if (textColorVal) parseColor(*textColorVal, style.textColor);

    auto* fontFamilyVal = styleVal.Find("fontFamily");
    if (fontFamilyVal && fontFamilyVal->IsString()) style.fontFamily = fontFamilyVal->AsString();

    auto* fontSizeVal = styleVal.Find("fontSize");
    if (fontSizeVal && fontSizeVal->IsNumber()) style.fontSize = static_cast<float>(fontSizeVal->AsNumber());

    auto* fontBoldVal = styleVal.Find("fontBold");
    if (fontBoldVal && fontBoldVal->IsBool()) style.fontBold = fontBoldVal->AsBool();

    auto* fontItalicVal = styleVal.Find("fontItalic");
    if (fontItalicVal && fontItalicVal->IsBool()) style.fontItalic = fontItalicVal->AsBool();

    auto* textAlignVal = styleVal.Find("textAlignment");
    if (textAlignVal && textAlignVal->IsString()) {
        style.textAlignment = parseTextAlignment(textAlignVal->AsString());
    }

    auto* paddingVal = styleVal.Find("padding");
    if (paddingVal) parseSpacing(*paddingVal, style.padding);

    auto* opacityVal = styleVal.Find("opacity");
    if (opacityVal && opacityVal->IsNumber()) style.opacity = static_cast<float>(opacityVal->AsNumber());

    auto* scaleVal = styleVal.Find("scale");
    if (scaleVal && scaleVal->IsNumber()) style.scale = static_cast<float>(scaleVal->AsNumber());

    auto* textShadowVal = styleVal.Find("textShadow");
    if (textShadowVal && textShadowVal->IsBool()) style.textShadow = textShadowVal->AsBool();

    auto* textShadowColorVal = styleVal.Find("textShadowColor");
    if (textShadowColorVal) parseColor(*textShadowColorVal, style.textShadowColor);

    auto* textShadowOffsetVal = styleVal.Find("textShadowOffset");
    if (textShadowOffsetVal) parseVec2(*textShadowOffsetVal, style.textShadowOffset);

    return true;
}

bool HUIParser::parseColor(const JsonValue& colorVal, UIColor& color)
{
    if (colorVal.IsArray() && colorVal.AsArray().size() >= 3) {
        const auto& arr = colorVal.AsArray();
        color.r = static_cast<float>(arr[0].AsNumber());
        color.g = static_cast<float>(arr[1].AsNumber());
        color.b = static_cast<float>(arr[2].AsNumber());
        color.a = (arr.size() >= 4) ? static_cast<float>(arr[3].AsNumber()) : 1.0f;
        return true;
    }
    if (colorVal.IsString()) {
        const std::string& str = colorVal.AsString();
        if (str == "white") color = UIColor::White();
        else if (str == "black") color = UIColor::Black();
        else if (str == "transparent") color = UIColor::Transparent();
        else {
            addError("Unknown color name: '" + str + "'");
            return false;
        }
        return true;
    }
    return false;
}

bool HUIParser::parseSpacing(const JsonValue& spacingVal, UISpacing& spacing)
{
    if (spacingVal.IsNumber()) {
        spacing = UISpacing::Uniform(static_cast<float>(spacingVal.AsNumber()));
        return true;
    }
    if (spacingVal.IsArray()) {
        const auto& arr = spacingVal.AsArray();
        if (arr.size() == 4) {
            spacing.left = static_cast<float>(arr[0].AsNumber());
            spacing.top = static_cast<float>(arr[1].AsNumber());
            spacing.right = static_cast<float>(arr[2].AsNumber());
            spacing.bottom = static_cast<float>(arr[3].AsNumber());
            return true;
        }
    }
    return false;
}

bool HUIParser::parseVec2(const JsonValue& vecVal, UIVec2& vec)
{
    if (vecVal.IsArray() && vecVal.AsArray().size() >= 2) {
        vec.x = static_cast<float>(vecVal.AsArray()[0].AsNumber());
        vec.y = static_cast<float>(vecVal.AsArray()[1].AsNumber());
        return true;
    }
    return false;
}

UIAnchorPreset HUIParser::parseAnchorPreset(const std::string& value)
{
    if (value == "TopLeft") return UIAnchorPreset::TopLeft;
    if (value == "TopCenter") return UIAnchorPreset::TopCenter;
    if (value == "TopRight") return UIAnchorPreset::TopRight;
    if (value == "CenterLeft") return UIAnchorPreset::CenterLeft;
    if (value == "Center") return UIAnchorPreset::Center;
    if (value == "CenterRight") return UIAnchorPreset::CenterRight;
    if (value == "BottomLeft") return UIAnchorPreset::BottomLeft;
    if (value == "BottomCenter") return UIAnchorPreset::BottomCenter;
    if (value == "BottomRight") return UIAnchorPreset::BottomRight;
    if (value == "Stretch") return UIAnchorPreset::Stretch;
    addError("Unknown anchor preset: '" + value + "'");
    return UIAnchorPreset::TopLeft;
}

UIHAlignment HUIParser::parseHAlignment(const std::string& value)
{
    if (value == "Left") return UIHAlignment::Left;
    if (value == "Center") return UIHAlignment::Center;
    if (value == "Right") return UIHAlignment::Right;
    if (value == "Stretch") return UIHAlignment::Stretch;
    addError("Unknown horizontal alignment: '" + value + "'");
    return UIHAlignment::Left;
}

UIVAlignment HUIParser::parseVAlignment(const std::string& value)
{
    if (value == "Top") return UIVAlignment::Top;
    if (value == "Center") return UIVAlignment::Center;
    if (value == "Bottom") return UIVAlignment::Bottom;
    if (value == "Stretch") return UIVAlignment::Stretch;
    addError("Unknown vertical alignment: '" + value + "'");
    return UIVAlignment::Top;
}

UITextAlignment HUIParser::parseTextAlignment(const std::string& value)
{
    if (value == "Left") return UITextAlignment::Left;
    if (value == "Center") return UITextAlignment::Center;
    if (value == "Right") return UITextAlignment::Right;
    addError("Unknown text alignment: '" + value + "'");
    return UITextAlignment::Left;
}

UIImageScaleMode HUIParser::parseImageScaleMode(const std::string& value)
{
    if (value == "Stretch") return UIImageScaleMode::Stretch;
    if (value == "Fit") return UIImageScaleMode::Fit;
    if (value == "Fill") return UIImageScaleMode::Fill;
    if (value == "Center") return UIImageScaleMode::Center;
    addError("Unknown image scale mode: '" + value + "'");
    return UIImageScaleMode::Stretch;
}

HUIWidgetType HUIParser::parseWidgetType(const std::string& value, bool& valid)
{
    valid = true;
    if (value == "Panel") return HUIWidgetType::Panel;
    if (value == "Canvas") return HUIWidgetType::Canvas;
    if (value == "VerticalBox") return HUIWidgetType::VerticalBox;
    if (value == "HorizontalBox") return HUIWidgetType::HorizontalBox;
    if (value == "Grid") return HUIWidgetType::Grid;
    if (value == "Text") return HUIWidgetType::Text;
    if (value == "Button") return HUIWidgetType::Button;
    if (value == "Image") return HUIWidgetType::Image;
    valid = false;
    return HUIWidgetType::Panel;
}

void HUIParser::addError(const std::string& message, const std::string& context)
{
    m_Errors.push_back({message, context});
    std::cerr << "[HUI] ";
    if (!context.empty()) std::cerr << context << ": ";
    std::cerr << message << "\n";
}

void HUIParser::validateWidgetIds(const HUIWidgetNode& node, std::unordered_set<std::string>& seenIds)
{
    if (!node.id.empty()) {
        if (seenIds.count(node.id) > 0) {
            addError("Duplicate widget id: '" + node.id + "'");
        } else {
            seenIds.insert(node.id);
        }
    }
    for (const auto& child : node.children) {
        validateWidgetIds(child, seenIds);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// HUIBuilder
// ─────────────────────────────────────────────────────────────────────────────

HUIBuilder::BuildResult HUIBuilder::Build(const HUIDocument& document)
{
    BuildResult result;
    result.controller = document.controller;

    std::string screenName = document.root.id.empty() ? "hui_screen" : document.root.id;
    result.screen = std::make_shared<UIScreen>(screenName);

    UICanvas* rootCanvas = result.screen->GetRootCanvas();
    if (!rootCanvas) {
        result.success = false;
        return result;
    }

    auto rootWidget = buildWidget(document.root, result.widgetMap);
    if (rootWidget) {
        rootCanvas->AddChild(std::move(rootWidget));
    }

    result.success = true;
    return result;
}

std::unique_ptr<UIElement> HUIBuilder::buildWidget(const HUIWidgetNode& node,
                                                    std::unordered_map<std::string, UIElement*>& widgetMap)
{
    std::unique_ptr<UIElement> element;

    switch (node.type) {
        case HUIWidgetType::Panel: {
            auto panel = std::make_unique<UIPanel>();
            if (node.hasStyle) {
                panel->SetLocalStyle(node.style);
            }
            element = std::move(panel);
            break;
        }
        case HUIWidgetType::Canvas: {
            element = std::make_unique<UICanvas>();
            break;
        }
        case HUIWidgetType::VerticalBox: {
            auto vbox = std::make_unique<UIVerticalBox>();
            vbox->SetSpacing(node.spacing);
            vbox->SetChildHAlignment(node.childHAlignment);
            element = std::move(vbox);
            break;
        }
        case HUIWidgetType::HorizontalBox: {
            auto hbox = std::make_unique<UIHorizontalBox>();
            hbox->SetSpacing(node.spacing);
            hbox->SetChildVAlignment(node.childVAlignment);
            element = std::move(hbox);
            break;
        }
        case HUIWidgetType::Grid: {
            auto grid = std::make_unique<UIGridLayout>();
            grid->SetColumns(node.gridColumns);
            grid->SetHSpacing(node.gridHSpacing);
            grid->SetVSpacing(node.gridVSpacing);
            grid->SetCellAlignment(node.childHAlignment, node.childVAlignment);
            element = std::move(grid);
            break;
        }
        case HUIWidgetType::Text: {
            auto text = std::make_unique<UIText>(node.text);
            text->SetAlignment(node.textAlignment);
            text->SetWordWrap(node.wordWrap);
            if (node.hasStyle) {
                if (node.style.fontSize > 0.0f) {
                    UIFont font;
                    font.SetPointSize(node.style.fontSize);
                    font.SetBold(node.style.fontBold);
                    font.SetItalic(node.style.fontItalic);
                    if (!node.style.fontFamily.empty()) {
                        font.SetFamilyName(node.style.fontFamily);
                    }
                    text->SetFont(font);
                }
                text->SetColor(node.style.textColor);
                text->SetShadow(node.style.textShadow);
                text->SetShadowColor(node.style.textShadowColor);
                text->SetShadowOffset(node.style.textShadowOffset);
            }
            element = std::move(text);
            break;
        }
        case HUIWidgetType::Button: {
            auto button = std::make_unique<UIButton>(node.text);
            if (node.hasNormalStyle) button->SetNormalStyle(node.normalStyle);
            if (node.hasHoveredStyle) button->SetHoveredStyle(node.hoveredStyle);
            if (node.hasPressedStyle) button->SetPressedStyle(node.pressedStyle);
            if (node.hasDisabledStyle) button->SetDisabledStyle(node.disabledStyle);
            if (node.hasStyle) {
                button->SetNormalStyle(node.style);
            }
            element = std::move(button);
            break;
        }
        case HUIWidgetType::Image: {
            auto image = std::make_unique<UIImage>(node.imageId);
            image->SetScaleMode(node.imageScaleMode);
            image->SetTint(node.imageTint);
            element = std::move(image);
            break;
        }
    }

    if (!element) return nullptr;

    applyCommonProperties(element.get(), node);

    if (!node.id.empty()) {
        widgetMap[node.id] = element.get();
    }

    for (const auto& child : node.children) {
        auto childElement = buildWidget(child, widgetMap);
        if (childElement) {
            element->AddChild(std::move(childElement));
        }
    }

    return element;
}

void HUIBuilder::applyCommonProperties(UIElement* element, const HUIWidgetNode& node)
{
    if (node.position.x != 0.0f || node.position.y != 0.0f)
        element->SetPosition(node.position);
    if (node.size.x != 0.0f || node.size.y != 0.0f)
        element->SetSize(node.size);
    if (node.minSize.x != 0.0f || node.minSize.y != 0.0f)
        element->SetMinSize(node.minSize);
    if (node.maxSize.x != 0.0f || node.maxSize.y != 0.0f)
        element->SetMaxSize(node.maxSize);

    element->SetAnchor({node.anchor});
    element->SetHAlignment(node.hAlignment);
    element->SetVAlignment(node.vAlignment);
    element->SetPadding(node.padding);
    element->SetMargin(node.margin);
    element->SetVisible(node.visible);
    element->SetEnabled(node.enabled);
    element->SetOpacity(node.opacity);
    element->SetZOrder(node.zOrder);
    element->SetInteractable(node.interactable);
    element->SetClippingEnabled(node.clippingEnabled);
}

void HUIBuilder::applyStyle(UIElement* element, const HUIWidgetNode& node)
{
    if (node.hasStyle) {
        element->SetOwnedStyle(node.style);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Default .hui content generation
// ─────────────────────────────────────────────────────────────────────────────

std::string generateDefaultHUIContent(const std::string& name)
{
    std::ostringstream out;
    out << "{\n"
        << "    \"version\": 1,\n"
        << "    \"controller\": \"\",\n"
        << "    \"root\": {\n"
        << "        \"type\": \"Panel\",\n"
        << "        \"id\": \"root\",\n"
        << "        \"properties\": {\n"
        << "            \"anchor\": \"Stretch\",\n"
        << "            \"visible\": true\n"
        << "        },\n"
        << "        \"style\": {\n"
        << "            \"backgroundColor\": [0.1, 0.1, 0.15, 0.95],\n"
        << "            \"cornerRadius\": 0\n"
        << "        },\n"
        << "        \"children\": [\n"
        << "            {\n"
        << "                \"type\": \"VerticalBox\",\n"
        << "                \"id\": \"main_layout\",\n"
        << "                \"properties\": {\n"
        << "                    \"spacing\": 16,\n"
        << "                    \"padding\": [40, 40, 40, 40],\n"
        << "                    \"childHAlignment\": \"Center\",\n"
        << "                    \"anchor\": \"Stretch\"\n"
        << "                },\n"
        << "                \"children\": [\n"
        << "                    {\n"
        << "                        \"type\": \"Text\",\n"
        << "                        \"id\": \"title_label\",\n"
        << "                        \"properties\": {\n"
        << "                            \"text\": \"" << name << "\",\n"
        << "                            \"textAlignment\": \"Center\"\n"
        << "                        },\n"
        << "                        \"style\": {\n"
        << "                            \"fontSize\": 32,\n"
        << "                            \"fontBold\": true,\n"
        << "                            \"textColor\": [1, 1, 1, 1],\n"
        << "                            \"textShadow\": true,\n"
        << "                            \"textShadowColor\": [0, 0, 0, 0.5],\n"
        << "                            \"textShadowOffset\": [2, 2]\n"
        << "                        }\n"
        << "                    },\n"
        << "                    {\n"
        << "                        \"type\": \"Button\",\n"
        << "                        \"id\": \"action_button\",\n"
        << "                        \"properties\": {\n"
        << "                            \"text\": \"Click Me\",\n"
        << "                            \"size\": [200, 50]\n"
        << "                        },\n"
        << "                        \"normalStyle\": {\n"
        << "                            \"backgroundColor\": [0.2, 0.5, 0.8, 1.0],\n"
        << "                            \"cornerRadius\": 8,\n"
        << "                            \"textColor\": [1, 1, 1, 1],\n"
        << "                            \"fontSize\": 18\n"
        << "                        },\n"
        << "                        \"hoveredStyle\": {\n"
        << "                            \"backgroundColor\": [0.3, 0.6, 0.9, 1.0],\n"
        << "                            \"cornerRadius\": 8,\n"
        << "                            \"textColor\": [1, 1, 1, 1],\n"
        << "                            \"fontSize\": 18\n"
        << "                        },\n"
        << "                        \"pressedStyle\": {\n"
        << "                            \"backgroundColor\": [0.15, 0.4, 0.65, 1.0],\n"
        << "                            \"cornerRadius\": 8,\n"
        << "                            \"textColor\": [1, 1, 1, 1],\n"
        << "                            \"fontSize\": 18\n"
        << "                        }\n"
        << "                    },\n"
        << "                    {\n"
        << "                        \"type\": \"Button\",\n"
        << "                        \"id\": \"secondary_button\",\n"
        << "                        \"properties\": {\n"
        << "                            \"text\": \"Cancel\",\n"
        << "                            \"size\": [200, 50]\n"
        << "                        },\n"
        << "                        \"normalStyle\": {\n"
        << "                            \"backgroundColor\": [0.3, 0.3, 0.35, 1.0],\n"
        << "                            \"cornerRadius\": 8,\n"
        << "                            \"textColor\": [0.9, 0.9, 0.9, 1.0],\n"
        << "                            \"fontSize\": 18\n"
        << "                        },\n"
        << "                        \"hoveredStyle\": {\n"
        << "                            \"backgroundColor\": [0.4, 0.4, 0.45, 1.0],\n"
        << "                            \"cornerRadius\": 8,\n"
        << "                            \"textColor\": [1, 1, 1, 1],\n"
        << "                            \"fontSize\": 18\n"
        << "                        },\n"
        << "                        \"pressedStyle\": {\n"
        << "                            \"backgroundColor\": [0.25, 0.25, 0.3, 1.0],\n"
        << "                            \"cornerRadius\": 8,\n"
        << "                            \"textColor\": [1, 1, 1, 1],\n"
        << "                            \"fontSize\": 18\n"
        << "                        }\n"
        << "                    }\n"
        << "                ]\n"
        << "            }\n"
        << "        ]\n"
        << "    }\n"
        << "}\n";
    return out.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Serialization helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

std::string indent(int level)
{
    return std::string(level * 4, ' ');
}

std::string escapeJson(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string colorToJson(const UIColor& c)
{
    std::ostringstream ss;
    ss << "[" << c.r << ", " << c.g << ", " << c.b << ", " << c.a << "]";
    return ss.str();
}

std::string vec2ToJson(const UIVec2& v)
{
    std::ostringstream ss;
    ss << "[" << v.x << ", " << v.y << "]";
    return ss.str();
}

std::string spacingToJson(const UISpacing& s)
{
    std::ostringstream ss;
    ss << "[" << s.left << ", " << s.top << ", " << s.right << ", " << s.bottom << "]";
    return ss.str();
}

std::string anchorToString(UIAnchorPreset a)
{
    switch (a) {
        case UIAnchorPreset::TopLeft: return "TopLeft";
        case UIAnchorPreset::TopCenter: return "TopCenter";
        case UIAnchorPreset::TopRight: return "TopRight";
        case UIAnchorPreset::CenterLeft: return "CenterLeft";
        case UIAnchorPreset::Center: return "Center";
        case UIAnchorPreset::CenterRight: return "CenterRight";
        case UIAnchorPreset::BottomLeft: return "BottomLeft";
        case UIAnchorPreset::BottomCenter: return "BottomCenter";
        case UIAnchorPreset::BottomRight: return "BottomRight";
        case UIAnchorPreset::Stretch: return "Stretch";
    }
    return "TopLeft";
}

std::string hAlignToString(UIHAlignment a)
{
    switch (a) {
        case UIHAlignment::Left: return "Left";
        case UIHAlignment::Center: return "Center";
        case UIHAlignment::Right: return "Right";
        case UIHAlignment::Stretch: return "Stretch";
    }
    return "Left";
}

std::string vAlignToString(UIVAlignment a)
{
    switch (a) {
        case UIVAlignment::Top: return "Top";
        case UIVAlignment::Center: return "Center";
        case UIVAlignment::Bottom: return "Bottom";
        case UIVAlignment::Stretch: return "Stretch";
    }
    return "Top";
}

std::string textAlignToString(UITextAlignment a)
{
    switch (a) {
        case UITextAlignment::Left: return "Left";
        case UITextAlignment::Center: return "Center";
        case UITextAlignment::Right: return "Right";
    }
    return "Left";
}

std::string imageScaleModeToString(UIImageScaleMode m)
{
    switch (m) {
        case UIImageScaleMode::Stretch: return "Stretch";
        case UIImageScaleMode::Fit: return "Fit";
        case UIImageScaleMode::Fill: return "Fill";
        case UIImageScaleMode::Center: return "Center";
    }
    return "Stretch";
}

std::string widgetTypeToString(HUIWidgetType t)
{
    switch (t) {
        case HUIWidgetType::Panel: return "Panel";
        case HUIWidgetType::Canvas: return "Canvas";
        case HUIWidgetType::VerticalBox: return "VerticalBox";
        case HUIWidgetType::HorizontalBox: return "HorizontalBox";
        case HUIWidgetType::Grid: return "Grid";
        case HUIWidgetType::Text: return "Text";
        case HUIWidgetType::Button: return "Button";
        case HUIWidgetType::Image: return "Image";
    }
    return "Panel";
}

void serializeStyle(std::ostringstream& out, const UIStyle& style, int depth)
{
    std::string ind = indent(depth);
    std::string ind1 = indent(depth + 1);
    out << ind << "{\n";

    std::vector<std::string> entries;

    auto addColor = [&](const char* name, const UIColor& c) {
        std::ostringstream e;
        e << ind1 << "\"" << name << "\": " << colorToJson(c);
        entries.push_back(e.str());
    };

    if (style.backgroundColor.a > 0.0f)
        addColor("backgroundColor", style.backgroundColor);
    if (!style.backgroundImageId.empty()) {
        entries.push_back(ind1 + "\"backgroundImageId\": \"" + escapeJson(style.backgroundImageId) + "\"");
    }
    if (style.borderWidth > 0.0f) {
        addColor("borderColor", style.borderColor);
        std::ostringstream e;
        e << ind1 << "\"borderWidth\": " << style.borderWidth;
        entries.push_back(e.str());
    }
    if (style.cornerRadius > 0.0f) {
        std::ostringstream e;
        e << ind1 << "\"cornerRadius\": " << style.cornerRadius;
        entries.push_back(e.str());
    }
    addColor("textColor", style.textColor);
    if (!style.fontFamily.empty())
        entries.push_back(ind1 + "\"fontFamily\": \"" + escapeJson(style.fontFamily) + "\"");
    if (style.fontSize > 0.0f) {
        std::ostringstream e;
        e << ind1 << "\"fontSize\": " << style.fontSize;
        entries.push_back(e.str());
    }
    if (style.fontBold)
        entries.push_back(ind1 + "\"fontBold\": true");
    if (style.fontItalic)
        entries.push_back(ind1 + "\"fontItalic\": true");
    if (style.textShadow) {
        entries.push_back(ind1 + "\"textShadow\": true");
        addColor("textShadowColor", style.textShadowColor);
        entries.push_back(ind1 + "\"textShadowOffset\": " + vec2ToJson(style.textShadowOffset));
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        out << entries[i];
        if (i + 1 < entries.size()) out << ",";
        out << "\n";
    }

    out << ind << "}";
}

void serializeWidget(std::ostringstream& out, const HUIWidgetNode& node, int depth)
{
    std::string ind = indent(depth);
    std::string ind1 = indent(depth + 1);
    std::string ind2 = indent(depth + 2);

    out << ind << "{\n";
    out << ind1 << "\"type\": \"" << widgetTypeToString(node.type) << "\",\n";

    if (!node.id.empty())
        out << ind1 << "\"id\": \"" << escapeJson(node.id) << "\",\n";
    if (!node.styleClass.empty())
        out << ind1 << "\"class\": \"" << escapeJson(node.styleClass) << "\",\n";

    // Properties
    out << ind1 << "\"properties\": {\n";
    std::vector<std::string> propEntries;

    auto addVec2 = [&](const char* name, const UIVec2& v) {
        if (v.x != 0.0f || v.y != 0.0f) {
            propEntries.push_back(ind2 + "\"" + name + "\": " + vec2ToJson(v));
        }
    };

    addVec2("position", node.position);
    addVec2("size", node.size);
    addVec2("minSize", node.minSize);
    addVec2("maxSize", node.maxSize);

    propEntries.push_back(ind2 + "\"anchor\": \"" + anchorToString(node.anchor) + "\"");

    if (node.hAlignment != UIHAlignment::Left)
        propEntries.push_back(ind2 + "\"hAlignment\": \"" + hAlignToString(node.hAlignment) + "\"");
    if (node.vAlignment != UIVAlignment::Top)
        propEntries.push_back(ind2 + "\"vAlignment\": \"" + vAlignToString(node.vAlignment) + "\"");

    if (node.padding.left != 0 || node.padding.top != 0 || node.padding.right != 0 || node.padding.bottom != 0)
        propEntries.push_back(ind2 + "\"padding\": " + spacingToJson(node.padding));
    if (node.margin.left != 0 || node.margin.top != 0 || node.margin.right != 0 || node.margin.bottom != 0)
        propEntries.push_back(ind2 + "\"margin\": " + spacingToJson(node.margin));

    if (!node.visible)
        propEntries.push_back(ind2 + "\"visible\": false");
    if (!node.enabled)
        propEntries.push_back(ind2 + "\"enabled\": false");
    if (node.opacity != 1.0f) {
        std::ostringstream e;
        e << ind2 << "\"opacity\": " << node.opacity;
        propEntries.push_back(e.str());
    }

    // Type-specific properties
    if (node.type == HUIWidgetType::Text || node.type == HUIWidgetType::Button) {
        if (!node.text.empty())
            propEntries.push_back(ind2 + "\"text\": \"" + escapeJson(node.text) + "\"");
        if (node.textAlignment != UITextAlignment::Left)
            propEntries.push_back(ind2 + "\"textAlignment\": \"" + textAlignToString(node.textAlignment) + "\"");
        if (node.wordWrap)
            propEntries.push_back(ind2 + "\"wordWrap\": true");
    }
    if (node.type == HUIWidgetType::Image) {
        if (!node.imageId.empty())
            propEntries.push_back(ind2 + "\"imageId\": \"" + escapeJson(node.imageId) + "\"");
    }
    if (node.type == HUIWidgetType::VerticalBox || node.type == HUIWidgetType::HorizontalBox) {
        std::ostringstream e;
        e << ind2 << "\"spacing\": " << node.spacing;
        propEntries.push_back(e.str());
        if (node.type == HUIWidgetType::VerticalBox && node.childHAlignment != UIHAlignment::Stretch)
            propEntries.push_back(ind2 + "\"childHAlignment\": \"" + hAlignToString(node.childHAlignment) + "\"");
        if (node.type == HUIWidgetType::HorizontalBox && node.childVAlignment != UIVAlignment::Stretch)
            propEntries.push_back(ind2 + "\"childVAlignment\": \"" + vAlignToString(node.childVAlignment) + "\"");
    }
    if (node.type == HUIWidgetType::Grid) {
        std::ostringstream e;
        e << ind2 << "\"columns\": " << node.gridColumns;
        propEntries.push_back(e.str());
        std::ostringstream e2;
        e2 << ind2 << "\"hSpacing\": " << node.gridHSpacing;
        propEntries.push_back(e2.str());
        std::ostringstream e3;
        e3 << ind2 << "\"vSpacing\": " << node.gridVSpacing;
        propEntries.push_back(e3.str());
    }

    for (size_t i = 0; i < propEntries.size(); ++i) {
        out << propEntries[i];
        if (i + 1 < propEntries.size()) out << ",";
        out << "\n";
    }
    out << ind1 << "}";

    // Style blocks
    if (node.hasStyle) {
        out << ",\n" << ind1 << "\"style\": ";
        serializeStyle(out, node.style, depth + 1);
    }
    if (node.hasNormalStyle) {
        out << ",\n" << ind1 << "\"normalStyle\": ";
        serializeStyle(out, node.normalStyle, depth + 1);
    }
    if (node.hasHoveredStyle) {
        out << ",\n" << ind1 << "\"hoveredStyle\": ";
        serializeStyle(out, node.hoveredStyle, depth + 1);
    }
    if (node.hasPressedStyle) {
        out << ",\n" << ind1 << "\"pressedStyle\": ";
        serializeStyle(out, node.pressedStyle, depth + 1);
    }
    if (node.hasDisabledStyle) {
        out << ",\n" << ind1 << "\"disabledStyle\": ";
        serializeStyle(out, node.disabledStyle, depth + 1);
    }

    // Children
    if (!node.children.empty()) {
        out << ",\n" << ind1 << "\"children\": [\n";
        for (size_t i = 0; i < node.children.size(); ++i) {
            serializeWidget(out, node.children[i], depth + 2);
            if (i + 1 < node.children.size()) out << ",";
            out << "\n";
        }
        out << ind1 << "]";
    }

    out << "\n" << ind << "}";
}

} // anonymous namespace

std::string serializeHUIDocument(const HUIDocument& document)
{
    std::ostringstream out;
    out << "{\n";
    out << "    \"version\": " << document.version << ",\n";
    out << "    \"controller\": \"" << escapeJson(document.controller) << "\",\n";
    out << "    \"root\": ";
    serializeWidget(out, document.root, 1);
    out << "\n}\n";
    return out.str();
}

} // namespace Hephaestus::HephaestusUI
