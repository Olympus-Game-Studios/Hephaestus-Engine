#if HEPHAESTUS_EDITOR_QT_UI

#include "HUIVisualEditorDialog.h"
#include "EditorDockTitleBar.h"
#include "EditorTheme.h"

#include "../../Assets/AssetManager.h"
#include "../HephaestusUI/HUIAsset.h"
#include "../HephaestusUI/UICanvas.h"
#include "../HephaestusUI/UIContext.h"
#include "../HephaestusUI/UIElement.h"
#include "../HephaestusUI/UIRenderer.h"
#include "../HephaestusUI/UIScreen.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLayout>
#include <QLayoutItem>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QPixmap>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QSignalBlocker>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLineF>
#include <QList>
#include <QGuiApplication>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <map>

namespace Hephaestus {
namespace QtEditor {
namespace {

using namespace HephaestusUI;

QString ToQString(const std::string& value) { return QString::fromStdString(value); }
std::string ToStdString(const QString& value) { return value.toStdString(); }

QString WidgetTypeLabel(HUIWidgetType type)
{
    switch (type) {
        case HUIWidgetType::Panel: return QObject::tr("Panel");
        case HUIWidgetType::Canvas: return QObject::tr("Canvas");
        case HUIWidgetType::VerticalBox: return QObject::tr("Vertical Stack");
        case HUIWidgetType::HorizontalBox: return QObject::tr("Horizontal Stack");
        case HUIWidgetType::Grid: return QObject::tr("Grid");
        case HUIWidgetType::Text: return QObject::tr("Label");
        case HUIWidgetType::Button: return QObject::tr("Button");
        case HUIWidgetType::Image: return QObject::tr("Image");
    }
    return QObject::tr("Widget");
}

QIcon WidgetTypeIcon(HUIWidgetType type, const QSize& size)
{
    const qreal dpr = 2.0;
    QPixmap pixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = Theme::Color(Theme::Token::TextSecondary);
    const qreal w = size.width();
    const qreal h = size.height();
    const qreal u = std::min(w / 28.0, h / 20.0);
    const QPointF o((w - 28.0 * u) * 0.5, (h - 20.0 * u) * 0.5);
    auto box = [&](qreal x, qreal y, qreal bw, qreal bh) { return QRectF(o.x() + x * u, o.y() + y * u, bw * u, bh * u); };
    auto outline = [&](const QRectF& r, qreal radius) {
        painter.setPen(QPen(ink, 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(r.adjusted(0.7, 0.7, -0.7, -0.7), radius * u, radius * u);
    };
    auto fill = [&](const QRectF& r) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawRoundedRect(r, u, u);
    };
    switch (type) {
        case HUIWidgetType::Panel: outline(box(2, 2, 24, 16), 3); break;
        case HUIWidgetType::Canvas: outline(box(2, 2, 24, 16), 1); fill(box(5, 5, 7, 5)); break;
        case HUIWidgetType::VerticalBox: fill(box(4, 2, 20, 4)); fill(box(4, 8, 20, 4)); fill(box(4, 14, 20, 4)); break;
        case HUIWidgetType::HorizontalBox: fill(box(3, 3, 6, 14)); fill(box(11, 3, 6, 14)); fill(box(19, 3, 6, 14)); break;
        case HUIWidgetType::Grid: fill(box(5, 1, 8, 8)); fill(box(15, 1, 8, 8)); fill(box(5, 11, 8, 8)); fill(box(15, 11, 8, 8)); break;
        case HUIWidgetType::Text: fill(box(3, 5, 22, 3)); fill(box(3, 12, 14, 3)); break;
        case HUIWidgetType::Button: outline(box(2, 4, 24, 12), 6); fill(box(9, 9, 10, 2)); break;
        case HUIWidgetType::Image: outline(box(2, 2, 24, 16), 2); fill(box(6, 6, 4, 4)); break;
    }
    painter.end();
    return QIcon(pixmap);
}

QString WidgetTypeKey(HUIWidgetType type)
{
    switch (type) {
        case HUIWidgetType::Panel: return QStringLiteral("Panel");
        case HUIWidgetType::Canvas: return QStringLiteral("Canvas");
        case HUIWidgetType::VerticalBox: return QStringLiteral("VerticalBox");
        case HUIWidgetType::HorizontalBox: return QStringLiteral("HorizontalBox");
        case HUIWidgetType::Grid: return QStringLiteral("Grid");
        case HUIWidgetType::Text: return QStringLiteral("Text");
        case HUIWidgetType::Button: return QStringLiteral("Button");
        case HUIWidgetType::Image: return QStringLiteral("Image");
    }
    return QStringLiteral("Panel");
}

bool IsContainerType(HUIWidgetType type)
{
    return type == HUIWidgetType::Panel || type == HUIWidgetType::Canvas ||
           type == HUIWidgetType::VerticalBox || type == HUIWidgetType::HorizontalBox ||
           type == HUIWidgetType::Grid;
}

QString BaseIdForType(HUIWidgetType type)
{
    switch (type) {
        case HUIWidgetType::Panel: return QStringLiteral("panel");
        case HUIWidgetType::Canvas: return QStringLiteral("canvas");
        case HUIWidgetType::VerticalBox: return QStringLiteral("vbox");
        case HUIWidgetType::HorizontalBox: return QStringLiteral("hbox");
        case HUIWidgetType::Grid: return QStringLiteral("grid");
        case HUIWidgetType::Text: return QStringLiteral("label");
        case HUIWidgetType::Button: return QStringLiteral("button");
        case HUIWidgetType::Image: return QStringLiteral("image");
    }
    return QStringLiteral("widget");
}

QString AnchorLabel(UIAnchorPreset preset)
{
    switch (preset) {
        case UIAnchorPreset::TopLeft: return QStringLiteral("TopLeft");
        case UIAnchorPreset::TopCenter: return QStringLiteral("TopCenter");
        case UIAnchorPreset::TopRight: return QStringLiteral("TopRight");
        case UIAnchorPreset::CenterLeft: return QStringLiteral("CenterLeft");
        case UIAnchorPreset::Center: return QStringLiteral("Center");
        case UIAnchorPreset::CenterRight: return QStringLiteral("CenterRight");
        case UIAnchorPreset::BottomLeft: return QStringLiteral("BottomLeft");
        case UIAnchorPreset::BottomCenter: return QStringLiteral("BottomCenter");
        case UIAnchorPreset::BottomRight: return QStringLiteral("BottomRight");
        case UIAnchorPreset::Stretch: return QStringLiteral("Stretch");
    }
    return QStringLiteral("TopLeft");
}

UIAnchorPreset AnchorFromLabel(const QString& value)
{
    if (value == "TopCenter") return UIAnchorPreset::TopCenter;
    if (value == "TopRight") return UIAnchorPreset::TopRight;
    if (value == "CenterLeft") return UIAnchorPreset::CenterLeft;
    if (value == "Center") return UIAnchorPreset::Center;
    if (value == "CenterRight") return UIAnchorPreset::CenterRight;
    if (value == "BottomLeft") return UIAnchorPreset::BottomLeft;
    if (value == "BottomCenter") return UIAnchorPreset::BottomCenter;
    if (value == "BottomRight") return UIAnchorPreset::BottomRight;
    if (value == "Stretch") return UIAnchorPreset::Stretch;
    return UIAnchorPreset::TopLeft;
}

QString HAlignLabel(UIHAlignment value)
{
    switch (value) {
        case UIHAlignment::Left: return QStringLiteral("Left");
        case UIHAlignment::Center: return QStringLiteral("Center");
        case UIHAlignment::Right: return QStringLiteral("Right");
        case UIHAlignment::Stretch: return QStringLiteral("Stretch");
    }
    return QStringLiteral("Left");
}

UIHAlignment HAlignFromLabel(const QString& value)
{
    if (value == "Center") return UIHAlignment::Center;
    if (value == "Right") return UIHAlignment::Right;
    if (value == "Stretch") return UIHAlignment::Stretch;
    return UIHAlignment::Left;
}

QString VAlignLabel(UIVAlignment value)
{
    switch (value) {
        case UIVAlignment::Top: return QStringLiteral("Top");
        case UIVAlignment::Center: return QStringLiteral("Center");
        case UIVAlignment::Bottom: return QStringLiteral("Bottom");
        case UIVAlignment::Stretch: return QStringLiteral("Stretch");
    }
    return QStringLiteral("Top");
}

UIVAlignment VAlignFromLabel(const QString& value)
{
    if (value == "Center") return UIVAlignment::Center;
    if (value == "Bottom") return UIVAlignment::Bottom;
    if (value == "Stretch") return UIVAlignment::Stretch;
    return UIVAlignment::Top;
}

QString TextAlignLabel(UITextAlignment value)
{
    switch (value) {
        case UITextAlignment::Left: return QStringLiteral("Left");
        case UITextAlignment::Center: return QStringLiteral("Center");
        case UITextAlignment::Right: return QStringLiteral("Right");
    }
    return QStringLiteral("Left");
}

UITextAlignment TextAlignFromLabel(const QString& value)
{
    if (value == "Center") return UITextAlignment::Center;
    if (value == "Right") return UITextAlignment::Right;
    return UITextAlignment::Left;
}

QString ScaleModeLabel(UIImageScaleMode value)
{
    switch (value) {
        case UIImageScaleMode::Stretch: return QStringLiteral("Stretch");
        case UIImageScaleMode::Fit: return QStringLiteral("Fit");
        case UIImageScaleMode::Fill: return QStringLiteral("Fill");
        case UIImageScaleMode::Center: return QStringLiteral("Center");
    }
    return QStringLiteral("Stretch");
}

UIImageScaleMode ScaleModeFromLabel(const QString& value)
{
    if (value == "Fit") return UIImageScaleMode::Fit;
    if (value == "Fill") return UIImageScaleMode::Fill;
    if (value == "Center") return UIImageScaleMode::Center;
    return UIImageScaleMode::Stretch;
}

QColor ToQColor(const UIColor& color)
{
    return QColor::fromRgbF(color.r, color.g, color.b, color.a);
}

UIColor ToUIColor(const QColor& color)
{
    return {
        static_cast<float>(color.redF()),
        static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF()),
        static_cast<float>(color.alphaF())
    };
}

QColor PickColor(QWidget* parent, const QColor& initial)
{
    QColor picked = QColorDialog::getColor(initial, parent, QObject::tr("Choose Color"), QColorDialog::ShowAlphaChannel);
    return picked.isValid() ? picked : initial;
}

void StyleInspectorColorPickerButton(QPushButton* button, const QColor& color, const QString& toolTip)
{
    // Approximate how the swatch reads on top of the dark inspector panel when alpha < 1.
    constexpr int pr = 44, pg = 47, pb = 54;
    const float a = std::clamp(color.alphaF(), 0.0f, 1.0f);
    const float br = color.redF() * a + static_cast<float>(pr) / 255.0f * (1.0f - a);
    const float bg = color.greenF() * a + static_cast<float>(pg) / 255.0f * (1.0f - a);
    const float bb = color.blueF() * a + static_cast<float>(pb) / 255.0f * (1.0f - a);
    const double lum = 0.2126 * static_cast<double>(br) + 0.7152 * static_cast<double>(bg) + 0.0722 * static_cast<double>(bb);
    const QString fg = lum > 0.55 ? QStringLiteral("#141414") : QStringLiteral("#f0f0f0");

    button->setMinimumHeight(26);
    button->setText(color.name(QColor::HexArgb));
    button->setToolTip(toolTip);
    button->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 4px;"
        "  padding: 4px 8px;"
        "  font-family: monospace;"
        "}"
        "QPushButton:hover { border: 1px solid %4; }"
    ).arg(color.name(QColor::HexArgb), fg, Theme::Color(Theme::Token::BorderDefault).name(),
          Theme::Color(Theme::Token::BorderFocus).name()));
}

UIStyle EditorDefaultButtonBaseStyle()
{
    UIStyle s;
    s.backgroundColor = {0.22f, 0.52f, 0.82f, 1.0f};
    s.cornerRadius = 8.0f;
    s.textColor = {1.0f, 1.0f, 1.0f, 1.0f};
    s.fontSize = 18.0f;
    return s;
}

UIStyle ButtonEditorBaseStyle(const HUIWidgetNode& node)
{
    if (node.hasNormalStyle) {
        return node.normalStyle;
    }
    if (node.hasStyle) {
        return node.style;
    }
    return EditorDefaultButtonBaseStyle();
}

void SeedHoveredButtonStyle(HUIWidgetNode& node)
{
    if (node.hasHoveredStyle) {
        return;
    }
    node.hoveredStyle = ButtonEditorBaseStyle(node);
    node.hasHoveredStyle = true;
}

void SeedPressedButtonStyle(HUIWidgetNode& node)
{
    if (node.hasPressedStyle) {
        return;
    }
    node.pressedStyle = ButtonEditorBaseStyle(node);
    node.hasPressedStyle = true;
}

void ClearLayout(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        if (item->layout()) {
            ClearLayout(item->layout());
            delete item->layout();
        }
        delete item;
    }
}

QVector<int> ParsePath(const QString& path)
{
    QVector<int> indices;
    if (path.isEmpty()) return indices;
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    for (const QString& part : parts) indices.push_back(part.toInt());
    return indices;
}

QString ParentPathOf(const QString& path)
{
    int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.left(slash) : QString();
}

template <typename NodeT>
NodeT* NodeForPath(NodeT& root, const QString& path)
{
    if (path.isEmpty()) return &root;
    NodeT* current = &root;
    const QVector<int> indices = ParsePath(path);
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(current->children.size())) return nullptr;
        current = &current->children[static_cast<size_t>(index)];
    }
    return current;
}

QString MakeChildPath(const QString& parentPath, int childIndex)
{
    return parentPath.isEmpty() ? QString::number(childIndex) : parentPath + "/" + QString::number(childIndex);
}

void CollectIds(const HUIWidgetNode& node, std::unordered_map<std::string, int>& counts)
{
    if (!node.id.empty()) counts[node.id]++;
    for (const auto& child : node.children) CollectIds(child, counts);
}

void EnsureUniqueIds(HUIWidgetNode& node, std::unordered_map<std::string, int>& counts)
{
    if (!node.id.empty()) {
        QString base = QString::fromStdString(node.id);
        QString candidate = base;
        int suffix = 1;
        while (counts[ToStdString(candidate)] > 0) {
            candidate = base + "_" + QString::number(suffix++);
        }
        node.id = ToStdString(candidate);
        counts[node.id]++;
    }
    for (auto& child : node.children) EnsureUniqueIds(child, counts);
}

HUIWidgetNode MakeDefaultNode(HUIWidgetType type)
{
    HUIWidgetNode node;
    node.type = type;
    switch (type) {
        case HUIWidgetType::Panel:
            node.size = {220.0f, 120.0f};
            node.hasStyle = true;
            node.style.backgroundColor = {0.18f, 0.20f, 0.26f, 1.0f};
            node.style.cornerRadius = 8.0f;
            break;
        case HUIWidgetType::Canvas:
            node.size = {320.0f, 200.0f};
            break;
        case HUIWidgetType::VerticalBox:
            node.size = {320.0f, 240.0f};
            node.spacing = 12.0f;
            node.childHAlignment = UIHAlignment::Stretch;
            break;
        case HUIWidgetType::HorizontalBox:
            node.size = {320.0f, 80.0f};
            node.spacing = 12.0f;
            node.childVAlignment = UIVAlignment::Stretch;
            break;
        case HUIWidgetType::Grid:
            node.size = {320.0f, 220.0f};
            node.gridColumns = 2;
            node.gridHSpacing = 8.0f;
            node.gridVSpacing = 8.0f;
            break;
        case HUIWidgetType::Text:
            node.size = {220.0f, 40.0f};
            node.text = "Label";
            node.hasStyle = true;
            node.style.fontSize = 24.0f;
            node.style.textColor = {1.0f, 1.0f, 1.0f, 1.0f};
            break;
        case HUIWidgetType::Button:
            node.size = {220.0f, 52.0f};
            node.text = "Button";
            node.hasNormalStyle = true;
            node.normalStyle.backgroundColor = {0.22f, 0.52f, 0.82f, 1.0f};
            node.normalStyle.cornerRadius = 8.0f;
            node.normalStyle.textColor = {1.0f, 1.0f, 1.0f, 1.0f};
            node.normalStyle.fontSize = 18.0f;
            break;
        case HUIWidgetType::Image:
            node.size = {128.0f, 128.0f};
            node.imageTint = UIColor::White();
            break;
    }
    return node;
}

struct PreviewElementInfo {
    QString path;
    QString parentPath;
    QRectF rect;
    bool draggable = false;
    int depth = 0;
    int zOrder = 0;
};

enum class HUIResizeHandle {
    None,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

constexpr float kMinWidgetSize = 8.0f;

float SnapScalar(float v, float step, bool snapEnabled, bool altHeld, float threshold)
{
    if (!snapEnabled || altHeld || step <= 0.0f) {
        return v;
    }
    const float nearest = std::round(v / step) * step;
    if (std::abs(v - nearest) <= threshold) {
        return nearest;
    }
    return v;
}

QPointF SnapPointToGrid(const QPointF& p, float step, bool snapEnabled, bool altHeld, float threshold)
{
    return {SnapScalar(static_cast<float>(p.x()), step, snapEnabled, altHeld, threshold),
            SnapScalar(static_cast<float>(p.y()), step, snapEnabled, altHeld, threshold)};
}

QRectF ToQRectF(const UIRect& rect)
{
    return QRectF(rect.x, rect.y, rect.width, rect.height);
}

QColor ApplyOpacity(const QColor& color, float opacity)
{
    QColor result = color;
    result.setAlphaF(std::clamp(color.alphaF() * opacity, 0.0f, 1.0f));
    return result;
}

QColor ApplyOpacity(const UIColor& color, float opacity)
{
    return ApplyOpacity(ToQColor(color), opacity);
}

QString ResolveImageAssetPath(const std::string& imageId)
{
    if (imageId.empty()) {
        return {};
    }

    const QString raw = QString::fromStdString(imageId);
    QFileInfo directInfo(raw);
    if (directInfo.isAbsolute() && directInfo.exists()) {
        return directInfo.absoluteFilePath();
    }

    auto& assetMgr = AssetManager::GetInstance();
    if (assetMgr.HasValidAssetRoot()) {
        const QString assetPath = QString::fromStdString(assetMgr.ToAbsolutePath(imageId));
        if (QFileInfo::exists(assetPath)) {
            return assetPath;
        }

        const QString projectPath = QString::fromStdString(assetMgr.GetProjectRoot()) + "/" + raw;
        if (QFileInfo::exists(projectPath)) {
            return QFileInfo(projectPath).absoluteFilePath();
        }
    }

    if (directInfo.exists()) {
        return directInfo.absoluteFilePath();
    }

    return {};
}

void CollectPreviewElementInfos(const HUIWidgetNode& node,
                                UIElement* runtimeElement,
                                const QString& path,
                                const QString& parentPath,
                                bool draggable,
                                int depth,
                                std::vector<PreviewElementInfo>& out)
{
    if (!runtimeElement) {
        return;
    }

    out.push_back({path, parentPath, ToQRectF(runtimeElement->GetLayoutRect()), draggable, depth, node.zOrder});

    const auto& runtimeChildren = runtimeElement->GetChildren();
    const size_t docChildCount = node.children.size();
    const size_t runtimeChildCount = runtimeChildren.size();
    const size_t runtimeOffset = runtimeChildCount > docChildCount ? (runtimeChildCount - docChildCount) : 0;

    for (size_t i = 0; i < docChildCount; ++i) {
        const size_t runtimeIndex = runtimeOffset + i;
        if (runtimeIndex >= runtimeChildCount || !runtimeChildren[runtimeIndex]) {
            continue;
        }

        const QString childPath = MakeChildPath(path, static_cast<int>(i));
        const bool parentAllowsEditorDrag =
            node.type == HUIWidgetType::Canvas || node.type == HUIWidgetType::Panel;
        CollectPreviewElementInfos(node.children[i],
                                   runtimeChildren[runtimeIndex].get(),
                                   childPath,
                                   path,
                                   parentAllowsEditorDrag,
                                   depth + 1,
                                   out);
    }
}

bool ParentAllowsFreeformChildDrag(const HUIDocument& doc, const QString& path)
{
    if (path.isEmpty()) {
        return false;
    }
    const HUIWidgetNode* p = NodeForPath(doc.root, ParentPathOf(path));
    // Canvas and Panel leave child position/size to the document. Layout containers overwrite
    // child transforms each frame (VerticalBox, HorizontalBox, Grid) — do not drag those children.
    return p && (p->type == HUIWidgetType::Canvas || p->type == HUIWidgetType::Panel);
}

bool NodeEligibleForFreeformTransform(const HUIWidgetNode& n)
{
    return n.anchor == UIAnchorPreset::TopLeft && n.hAlignment == UIHAlignment::Left && n.vAlignment == UIVAlignment::Top;
}

QRectF UnionPreviewRects(const std::vector<PreviewElementInfo>& elements, const QSet<QString>& paths)
{
    QRectF u;
    bool any = false;
    for (const auto& el : elements) {
        if (paths.contains(el.path)) {
            u = any ? u.united(el.rect) : el.rect;
            any = true;
        }
    }
    return u;
}

QRectF RectForPathInPreview(const std::vector<PreviewElementInfo>& elements, const QString& path)
{
    for (const auto& el : elements) {
        if (el.path == path) {
            return el.rect;
        }
    }
    return {};
}

class HUIHierarchyTreeWidget : public QTreeWidget {
public:
    std::function<void()> afterDrop;

protected:
    void dropEvent(QDropEvent* event) override
    {
        QTreeWidget::dropEvent(event);
        if (event->isAccepted() && afterDrop) afterDrop();
    }
};

class HUIDesignCanvas : public QWidget {
public:
    explicit HUIDesignCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setMinimumSize(900, 700);
    }

    void setPreviewSize(const QSize& size)
    {
        m_previewSize = size;
        update();
    }

    void setZoom(double zoom)
    {
        m_zoom = std::max(0.25, std::min(zoom, 3.0));
        update();
    }

    double zoomLevel() const { return m_zoom; }

    void setPreviewMode(bool preview)
    {
        m_previewMode = preview;
        update();
    }

    void setPrimarySelection(const QString& path)
    {
        m_primaryPath = path;
        update();
    }

    void setSelectionSet(const QSet<QString>& paths)
    {
        m_selectionSet = paths;
        update();
    }

    void setSelectionUnionDesign(const QRectF& unionRect, bool showResizeHandles)
    {
        m_selectionUnionDesign = unionRect;
        m_showResizeHandles = showResizeHandles;
        update();
    }

    void setGridOptions(bool visible, int stepPx, bool showCenterGuides)
    {
        m_gridVisible = visible;
        m_gridStep = std::max(2, stepPx);
        m_showCenterGuides = showCenterGuides;
        update();
    }

    void setAlignmentGuides(const QList<QLineF>& linesDesign, float alpha)
    {
        m_guideLines = linesDesign;
        m_guideAlpha = std::clamp(alpha, 0.0f, 1.0f);
        update();
    }

    void setMarqueeScreenRect(const QRectF& rect)
    {
        m_marqueeScreen = rect;
        update();
    }

    void setPreviewElements(std::vector<PreviewElementInfo> elements)
    {
        m_elements = std::move(elements);
        update();
    }

    void setDrawCommands(std::vector<UIDrawCommand> commands)
    {
        m_drawCommands = std::move(commands);
        update();
    }

    std::function<void(double)> onZoomChanged;

    std::function<void(QMouseEvent*)> onCanvasMousePress;
    std::function<void(QMouseEvent*)> onCanvasMouseMove;
    std::function<void(QMouseEvent*)> onCanvasMouseRelease;
    std::function<void(const QPointF&)> onPointerMove;
    std::function<void(const QPointF&, int)> onPointerPress;
    std::function<void(const QPointF&, int)> onPointerRelease;

    [[nodiscard]] QRectF previewRectPublic() const { return previewRect(); }

    [[nodiscard]] QRectF designRectToScreen(const QRectF& designRect) const { return designToScreen(designRect); }

    [[nodiscard]] QPointF designPointToScreen(const QPointF& designPos) const { return designToScreen(designPos); }

    [[nodiscard]] std::optional<QPointF> screenPointToDesign(const QPointF& screenPos) const
    {
        return screenToDesign(screenPos);
    }

    [[nodiscard]] QString hitTestDraggable(const QPointF& screenPos) const
    {
        const PreviewElementInfo* best = nullptr;
        for (const auto& el : m_elements) {
            if (!el.draggable || !designToScreen(el.rect).contains(screenPos)) {
                continue;
            }
            if (!best || previewElementIsMoreOnTop(el, *best)) {
                best = &el;
            }
        }
        return best ? best->path : QString{};
    }

    /** Prefer higher z, then deeper in tree, then smaller rect (typical leaf on top of panel). */
    [[nodiscard]] static bool previewElementIsMoreOnTop(const PreviewElementInfo& cand, const PreviewElementInfo& cur)
    {
        if (cand.zOrder != cur.zOrder) {
            return cand.zOrder > cur.zOrder;
        }
        if (cand.depth != cur.depth) {
            return cand.depth > cur.depth;
        }
        const double a = cand.rect.width() * cand.rect.height();
        const double b = cur.rect.width() * cur.rect.height();
        return a < b;
    }

    [[nodiscard]] QString hitTestTopmost(const QPointF& screenPos) const
    {
        const PreviewElementInfo* best = nullptr;
        for (const auto& el : m_elements) {
            if (!designToScreen(el.rect).contains(screenPos)) {
                continue;
            }
            if (!best) {
                best = &el;
                continue;
            }
            if (previewElementIsMoreOnTop(el, *best)) {
                best = &el;
            }
        }
        return best ? best->path : QString{};
    }

    [[nodiscard]] QRectF rectDesignForPath(const QString& path) const
    {
        for (const PreviewElementInfo& element : m_elements) {
            if (element.path == path) {
                return element.rect;
            }
        }
        return {};
    }

    [[nodiscard]] HUIResizeHandle hitTestResizeHandle(const QPointF& screenPos) const
    {
        if (!m_showResizeHandles || m_selectionUnionDesign.isNull()) {
            return HUIResizeHandle::None;
        }
        const QRectF r = designToScreen(m_selectionUnionDesign);
        const qreal h = std::clamp(6.0, 4.0, 10.0 * m_zoom);
        const qreal hh = h * 0.5;

        const auto corners = std::array<QPointF, 8>{
            r.topLeft(),     r.topRight(),     r.bottomLeft(),     r.bottomRight(),
            QPointF(r.center().x(), r.top()), QPointF(r.center().x(), r.bottom()),
            QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y())};
        const auto kinds = std::array<HUIResizeHandle, 8>{
            HUIResizeHandle::TopLeft, HUIResizeHandle::TopRight, HUIResizeHandle::BottomLeft, HUIResizeHandle::BottomRight,
            HUIResizeHandle::Top, HUIResizeHandle::Bottom, HUIResizeHandle::Left, HUIResizeHandle::Right};

        for (size_t i = 0; i < corners.size(); ++i) {
            if (QRectF(corners[i].x() - hh, corners[i].y() - hh, h, h).contains(screenPos)) {
                return kinds[i];
            }
        }
        return HUIResizeHandle::None;
    }

    [[nodiscard]] Qt::CursorShape cursorForHover(const QPointF& screenPos) const
    {
        if (m_previewMode) {
            return Qt::ArrowCursor;
        }
        if (m_showResizeHandles && hitTestResizeHandle(screenPos) != HUIResizeHandle::None) {
            switch (hitTestResizeHandle(screenPos)) {
                case HUIResizeHandle::Left:
                case HUIResizeHandle::Right: return Qt::SizeHorCursor;
                case HUIResizeHandle::Top:
                case HUIResizeHandle::Bottom: return Qt::SizeVerCursor;
                case HUIResizeHandle::TopLeft:
                case HUIResizeHandle::BottomRight: return Qt::SizeFDiagCursor;
                case HUIResizeHandle::TopRight:
                case HUIResizeHandle::BottomLeft: return Qt::SizeBDiagCursor;
                default: break;
            }
        }
        const QString hit = hitTestTopmost(screenPos);
        const bool canDragHere = !hit.isEmpty() && hitTestDraggable(screenPos) == hit;
        if (!hit.isEmpty() && canDragHere && (m_selectionSet.contains(hit) || hit == m_primaryPath)) {
            return Qt::OpenHandCursor;
        }
        if (!hit.isEmpty()) {
            return Qt::PointingHandCursor;
        }
        return Qt::ArrowCursor;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Theme::Color(Theme::Token::SurfaceViewport));
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRectF screen = previewRect();
        painter.setPen(QPen(Theme::Color(Theme::Token::BorderDefault), 1.0));
        painter.setBrush(Theme::Color(Theme::Token::SurfaceRaised));
        painter.drawRect(screen);

        painter.save();
        painter.setClipRect(screen);
        int clipDepth = 0;
        for (const UIDrawCommand& command : m_drawCommands) {
            if (std::holds_alternative<UIDrawRectCommand>(command)) {
                drawRectCommand(painter, std::get<UIDrawRectCommand>(command));
            } else if (std::holds_alternative<UIDrawImageCommand>(command)) {
                drawImageCommand(painter, std::get<UIDrawImageCommand>(command));
            } else if (std::holds_alternative<UIDrawTextCommand>(command)) {
                drawTextCommand(painter, std::get<UIDrawTextCommand>(command));
            } else if (std::holds_alternative<UIPushClipRectCommand>(command)) {
                painter.save();
                painter.setClipRect(designToScreen(ToQRectF(std::get<UIPushClipRectCommand>(command).rect)), Qt::IntersectClip);
                ++clipDepth;
            } else if (std::holds_alternative<UIPopClipRectCommand>(command) && clipDepth > 0) {
                painter.restore();
                --clipDepth;
            }
        }
        while (clipDepth-- > 0) {
            painter.restore();
        }

        if (!m_previewMode && m_gridVisible) {
            painter.setOpacity(0.35);
            painter.setPen(QPen(Theme::Color(Theme::Token::BorderDefault), 1.0));
            const float step = static_cast<float>(m_gridStep);
            for (float x = 0.0f; x <= static_cast<float>(m_previewSize.width()) + 0.5f; x += step) {
                const QPointF a = designToScreen(QPointF(x, 0.0));
                const QPointF b = designToScreen(QPointF(x, static_cast<float>(m_previewSize.height())));
                painter.drawLine(a, b);
            }
            for (float y = 0.0f; y <= static_cast<float>(m_previewSize.height()) + 0.5f; y += step) {
                const QPointF a = designToScreen(QPointF(0.0f, y));
                const QPointF b = designToScreen(QPointF(static_cast<float>(m_previewSize.width()), y));
                painter.drawLine(a, b);
            }
            painter.setOpacity(1.0);
        }

        if (!m_previewMode && m_showCenterGuides) {
            const float cx = static_cast<float>(m_previewSize.width()) * 0.5f;
            const float cy = static_cast<float>(m_previewSize.height()) * 0.5f;
            painter.setPen(QPen(Theme::Color(Theme::Token::AccentBrand), 1.0, Qt::DashLine));
            painter.drawLine(designToScreen(QPointF(cx, 0.0)), designToScreen(QPointF(cx, static_cast<float>(m_previewSize.height()))));
            painter.drawLine(designToScreen(QPointF(0.0f, cy)), designToScreen(QPointF(static_cast<float>(m_previewSize.width()), cy)));
        }

        if (!m_guideLines.isEmpty() && m_guideAlpha > 0.01f) {
            painter.setOpacity(m_guideAlpha);
            painter.setPen(QPen(Theme::Color(Theme::Token::AccentBrand), 1.0));
            for (const QLineF& ln : m_guideLines) {
                painter.drawLine(designToScreen(ln.p1()), designToScreen(ln.p2()));
            }
            painter.setOpacity(1.0);
        }

        painter.restore();

        if (!m_marqueeScreen.isNull()) {
            QColor marqueeFill = Theme::Color(Theme::Token::AccentSelection);
            marqueeFill.setAlpha(40);
            painter.setPen(QPen(Theme::Color(Theme::Token::AccentSelection), 1.0, Qt::DashLine));
            painter.setBrush(marqueeFill);
            painter.drawRect(m_marqueeScreen);
        }

        if (!m_previewMode) {
            for (const QString& p : m_selectionSet) {
                if (p == m_primaryPath) {
                    continue;
                }
                const QRectF sr = designToScreen(rectDesignForPath(p));
                if (!sr.isNull()) {
                    painter.setPen(QPen(Theme::Color(Theme::Token::AccentSelection), 1.0));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(sr.adjusted(-1.0, -1.0, 1.0, 1.0));
                }
            }
            if (!m_hoveredPath.isEmpty() && !m_selectionSet.contains(m_hoveredPath)) {
                const QRectF hovered = designToScreen(rectDesignForPath(m_hoveredPath));
                if (!hovered.isNull()) {
                    painter.setPen(QPen(Theme::Color(Theme::Token::TextMuted), 1.0));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(hovered.adjusted(-1.0, -1.0, 1.0, 1.0));
                }
            }
            const QRectF primaryScreen = designToScreen(rectDesignForPath(m_primaryPath));
            if (!primaryScreen.isNull()) {
                painter.setPen(QPen(Theme::Color(Theme::Token::AccentSelection), 1.5));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(primaryScreen.adjusted(-2.0, -2.0, 2.0, 2.0));
            }
            if (m_selectionSet.size() > 1) {
                const QRectF u = designToScreen(m_selectionUnionDesign);
                if (!u.isNull()) {
                    painter.setPen(QPen(Theme::Color(Theme::Token::AccentSelection), 1.0, Qt::DashLine));
                    painter.drawRect(u.adjusted(-2.0, -2.0, 2.0, 2.0));
                }
            }
            if (m_showResizeHandles && !m_selectionUnionDesign.isNull()) {
                const QRectF r = designToScreen(m_selectionUnionDesign);
                const qreal h = std::clamp(6.0, 4.0, 10.0 * m_zoom);
                const qreal hh = h * 0.5;
                painter.setPen(QPen(Theme::Color(Theme::Token::AccentSelection), 1.25));
                painter.setBrush(Theme::Color(Theme::Token::TextOnAction));
                const std::array<QPointF, 8> pts = {
                    r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight(),
                    QPointF(r.center().x(), r.top()), QPointF(r.center().x(), r.bottom()),
                    QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y())};
                for (const QPointF& c : pts) {
                    painter.drawRect(QRectF(c.x() - hh, c.y() - hh, h, h));
                }
            }
        }
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const QRectF before = previewRect();
        const QPointF P = event->position();
        const double z0 = m_zoom;

        QPointF d;
        QPointF anchorScreen;
        if (before.contains(P)) {
            const QPointF tl = before.topLeft();
            d = QPointF((P.x() - tl.x()) / z0, (P.y() - tl.y()) / z0);
            anchorScreen = P;
        } else {
            d = QPointF(m_previewSize.width() * 0.5, m_previewSize.height() * 0.5);
            anchorScreen = before.center();
        }

        double steps = static_cast<double>(event->angleDelta().y()) / 120.0;
        if (qFuzzyIsNull(steps)) {
            const QPoint pd = event->pixelDelta();
            if (pd.y() != 0) {
                steps = pd.y() > 0 ? 0.25 : -0.25;
            }
        }
        if (qFuzzyIsNull(steps)) {
            event->accept();
            return;
        }

        double z1 = z0 * std::pow(1.1, steps);
        z1 = std::max(0.25, std::min(z1, 3.0));
        if (qFuzzyCompare(z1, z0)) {
            event->accept();
            return;
        }

        const QPointF tlPrime = anchorScreen - QPointF(d.x() * z1, d.y() * z1);
        const QSizeF scaled1(m_previewSize.width() * z1, m_previewSize.height() * z1);
        const QPointF defaultTl1((width() - scaled1.width()) * 0.5, (height() - scaled1.height()) * 0.5);
        m_viewPan = tlPrime - defaultTl1;
        m_zoom = z1;
        update();
        if (onZoomChanged) {
            onZoomChanged(m_zoom);
        }
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::MiddleButton) {
            m_panning = true;
            m_lastMousePos = event->position();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        if (onCanvasMousePress) {
            onCanvasMousePress(event);
        }
        if (const auto designPos = screenToDesign(event->position()); designPos && onPointerPress) {
            onPointerPress(*designPos, static_cast<int>(event->button()));
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_panning) {
            const QPointF delta = event->position() - m_lastMousePos;
            m_lastMousePos = event->position();
            m_viewPan += delta;
            event->accept();
            update();
            return;
        }

        if (const auto designPos = screenToDesign(event->position()); designPos) {
            m_hoveredPath = hitTestTopmost(event->position());
            if (onPointerMove) {
                onPointerMove(*designPos);
            }
        } else {
            m_hoveredPath.clear();
            if (onPointerMove) {
                onPointerMove(QPointF(-10000.0, -10000.0));
            }
        }

        if (onCanvasMouseMove) {
            onCanvasMouseMove(event);
        }
        if (!m_panning) {
            setCursor(cursorForHover(event->position()));
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::MiddleButton && m_panning) {
            m_panning = false;
            setCursor(Qt::ArrowCursor);
            event->accept();
            return;
        }

        if (onCanvasMouseRelease) {
            onCanvasMouseRelease(event);
        }
        if (onPointerRelease) {
            if (const auto designPos = screenToDesign(event->position()); designPos) {
                onPointerRelease(*designPos, static_cast<int>(event->button()));
            } else {
                onPointerRelease(QPointF(-10000.0, -10000.0), static_cast<int>(event->button()));
            }
        }
    }

private:
    QRectF previewRect() const
    {
        QSizeF scaled(m_previewSize.width() * m_zoom, m_previewSize.height() * m_zoom);
        QPointF topLeft((width() - scaled.width()) * 0.5 + m_viewPan.x(),
                        (height() - scaled.height()) * 0.5 + m_viewPan.y());
        return QRectF(topLeft, scaled);
    }

    QRectF designToScreen(const QRectF& designRect) const
    {
        const QRectF screen = previewRect();
        return QRectF(screen.left() + designRect.left() * m_zoom,
                      screen.top() + designRect.top() * m_zoom,
                      designRect.width() * m_zoom,
                      designRect.height() * m_zoom);
    }

    QPointF designToScreen(const QPointF& designPos) const
    {
        const QRectF screen = previewRect();
        return QPointF(screen.left() + designPos.x() * m_zoom,
                       screen.top() + designPos.y() * m_zoom);
    }

    std::optional<QPointF> screenToDesign(const QPointF& screenPos) const
    {
        const QRectF screen = previewRect();
        if (!screen.contains(screenPos)) {
            return std::nullopt;
        }
        return QPointF((screenPos.x() - screen.left()) / m_zoom,
                       (screenPos.y() - screen.top()) / m_zoom);
    }

    QPixmap pixmapForImageId(const std::string& imageId) const
    {
        if (imageId.empty()) {
            return {};
        }

        auto it = m_imageCache.find(imageId);
        if (it != m_imageCache.end()) {
            return it->second;
        }

        const QString assetPath = ResolveImageAssetPath(imageId);
        if (assetPath.isEmpty()) {
            m_imageCache.emplace(imageId, QPixmap());
            return {};
        }

        QPixmap pixmap(assetPath);
        m_imageCache.emplace(imageId, pixmap);
        return pixmap;
    }

    QPixmap tintedPixmap(const QPixmap& source, const QColor& tint) const
    {
        if (source.isNull()) {
            return {};
        }

        if (qFuzzyCompare(tint.redF(), 1.0f) &&
            qFuzzyCompare(tint.greenF(), 1.0f) &&
            qFuzzyCompare(tint.blueF(), 1.0f) &&
            qFuzzyCompare(tint.alphaF(), 1.0f)) {
            return source;
        }

        QPixmap tinted = source;
        QPainter tintPainter(&tinted);
        tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tintPainter.fillRect(tinted.rect(), tint);
        return tinted;
    }

    void drawPixmapInRect(QPainter& painter,
                          const QRectF& targetRect,
                          const std::string& imageId,
                          const QColor& tint,
                          UIImageScaleMode scaleMode,
                          float opacity,
                          const QPainterPath* clipPath = nullptr) const
    {
        const QPixmap pixmap = pixmapForImageId(imageId);
        if (pixmap.isNull()) {
            return;
        }

        const QPixmap tinted = tintedPixmap(pixmap, tint);
        painter.save();
        if (clipPath) {
            painter.setClipPath(*clipPath, Qt::IntersectClip);
        }
        painter.setOpacity(std::clamp(opacity, 0.0f, 1.0f));

        QRectF destRect = targetRect;
        if (scaleMode == UIImageScaleMode::Fit) {
            QSizeF scaled = tinted.size();
            scaled.scale(targetRect.size(), Qt::KeepAspectRatio);
            destRect.setSize(scaled);
            destRect.moveCenter(targetRect.center());
            painter.drawPixmap(destRect, tinted, QRectF(QPointF(0, 0), tinted.size()));
        } else if (scaleMode == UIImageScaleMode::Center) {
            QSizeF size = tinted.size();
            destRect.setSize(size);
            destRect.moveCenter(targetRect.center());
            painter.drawPixmap(destRect.topLeft(), tinted);
        } else if (scaleMode == UIImageScaleMode::Fill) {
            const QPixmap scaled = tinted.scaled(targetRect.size().toSize(),
                                                 Qt::KeepAspectRatioByExpanding,
                                                 Qt::SmoothTransformation);
            QRectF sourceRect((scaled.width() - targetRect.width()) * 0.5,
                              (scaled.height() - targetRect.height()) * 0.5,
                              targetRect.width(),
                              targetRect.height());
            painter.drawPixmap(targetRect, scaled, sourceRect);
        } else {
            painter.drawPixmap(targetRect, tinted, QRectF(QPointF(0, 0), tinted.size()));
        }
        painter.restore();
    }

    void drawRectCommand(QPainter& painter, const UIDrawRectCommand& command) const
    {
        const QRectF rect = designToScreen(ToQRectF(command.rect));
        const qreal radius = command.cornerRadius * m_zoom;
        QPainterPath path;
        path.addRoundedRect(rect, radius, radius);

        const QColor fillColor = ApplyOpacity(command.fillColor, command.opacity);
        if (fillColor.alpha() > 0) {
            painter.fillPath(path, fillColor);
        }

        if (!command.backgroundImageId.empty()) {
            drawPixmapInRect(painter,
                             rect,
                             command.backgroundImageId,
                             ToQColor(command.imageTint),
                             command.imageScaleMode,
                             command.opacity,
                             &path);
        }

        if (command.borderWidth > 0.0f) {
            painter.save();
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(ApplyOpacity(command.borderColor, command.opacity),
                                std::max(1.0, static_cast<double>(command.borderWidth * m_zoom))));
            painter.drawPath(path);
            painter.restore();
        }
    }

    void drawImageCommand(QPainter& painter, const UIDrawImageCommand& command) const
    {
        drawPixmapInRect(painter,
                         designToScreen(ToQRectF(command.rect)),
                         command.imageId,
                         ToQColor(command.tint),
                         command.scaleMode,
                         command.opacity);
    }

    void drawTextCommand(QPainter& painter, const UIDrawTextCommand& command) const
    {
        const QRectF rect = designToScreen(ToQRectF(command.rect));
        QFont font = painter.font();
        if (!command.fontFamily.empty()) {
            font.setFamily(QString::fromStdString(command.fontFamily));
        }
        font.setPointSizeF(std::max(1.0, static_cast<double>(command.fontSize * m_zoom)));
        font.setBold(command.bold);
        font.setItalic(command.italic);

        int flags = Qt::AlignVCenter;
        switch (command.alignment) {
            case UITextAlignment::Center: flags |= Qt::AlignHCenter; break;
            case UITextAlignment::Right: flags |= Qt::AlignRight; break;
            case UITextAlignment::Left:
            default: flags |= Qt::AlignLeft; break;
        }
        if (command.wordWrap) {
            flags |= Qt::TextWordWrap;
        }

        painter.save();
        painter.setFont(font);
        if (command.shadow) {
            painter.setPen(ApplyOpacity(command.shadowColor, command.opacity));
            painter.drawText(rect.translated(command.shadowOffset.x * m_zoom, command.shadowOffset.y * m_zoom),
                             flags,
                             QString::fromStdString(command.text));
        }
        painter.setPen(ApplyOpacity(command.textColor, command.opacity));
        painter.drawText(rect, flags, QString::fromStdString(command.text));
        painter.restore();
    }

    QSize m_previewSize = QSize(1280, 720);
    double m_zoom = 1.0;
    QPointF m_viewPan;
    bool m_panning = false;
    bool m_previewMode = false;
    QString m_primaryPath;
    QSet<QString> m_selectionSet;
    QRectF m_selectionUnionDesign;
    bool m_showResizeHandles = false;
    bool m_gridVisible = true;
    int m_gridStep = 8;
    bool m_showCenterGuides = true;
    QList<QLineF> m_guideLines;
    float m_guideAlpha = 0.0f;
    QRectF m_marqueeScreen;
    QString m_hoveredPath;
    QPointF m_lastMousePos;
    std::vector<PreviewElementInfo> m_elements;
    std::vector<UIDrawCommand> m_drawCommands;
    mutable std::unordered_map<std::string, QPixmap> m_imageCache;
};

class HUIVisualEditorDialog : public QDialog {
public:
    HUIVisualEditorDialog(QWidget* parent, QString absolutePath)
        : QDialog(parent)
        , m_absolutePath(std::move(absolutePath))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setFocusPolicy(Qt::StrongFocus);
        resize(1600, 950);
        buildUi();
        setupShortcuts();
        loadFromDisk(m_absolutePath);
    }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (!confirmClose()) {
            event->ignore();
            return;
        }
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildUi()
    {
        auto* rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);

        m_toolbar = new QToolBar(this);
        m_toolbar->setIconSize(QSize(16, 16));
        rootLayout->addWidget(m_toolbar);

        auto addTextAction = [this](QAction* action) {
            m_toolbar->addAction(action);
            if (auto* button = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(action))) {
                button->setToolButtonStyle(action->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
            }
        };
        auto addMenuButton = [this](const QString& text, const QString& toolTip, QMenu* menu) {
            auto* button = new QToolButton(m_toolbar);
            button->setText(text);
            button->setToolTip(toolTip);
            button->setMenu(menu);
            button->setPopupMode(QToolButton::InstantPopup);
            m_toolbar->addWidget(button);
            return button;
        };

        QAction* saveAction = new QAction(Theme::Icon(QStringLiteral("save")), tr("Save"), this);
        saveAction->setToolTip(tr("Save (Ctrl+S)"));
        QAction* saveAsAction = new QAction(tr("Save As..."), this);
        auto* saveButton = new QToolButton(m_toolbar);
        saveButton->setDefaultAction(saveAction);
        auto* saveMenu = new QMenu(saveButton);
        saveMenu->addAction(saveAsAction);
        saveButton->setMenu(saveMenu);
        saveButton->setPopupMode(QToolButton::MenuButtonPopup);
        m_toolbar->addWidget(saveButton);
        m_toolbar->addSeparator();

        QAction* undoAction = m_toolbar->addAction(Theme::Icon(QStringLiteral("undo")), tr("Undo"));
        undoAction->setToolTip(tr("Undo (Ctrl+Z)"));
        QAction* redoAction = m_toolbar->addAction(Theme::Icon(QStringLiteral("redo")), tr("Redo"));
        redoAction->setToolTip(tr("Redo (Ctrl+Y)"));
        QAction* duplicateAction = m_toolbar->addAction(Theme::Icon(QStringLiteral("copy")), tr("Duplicate"));
        duplicateAction->setToolTip(tr("Duplicate (Ctrl+D)"));
        QAction* deleteAction = new QAction(tr("Delete"), this);
        deleteAction->setToolTip(tr("Delete the selected widgets (Del)"));
        addTextAction(deleteAction);
        m_toolbar->addSeparator();

        auto* arrangeMenu = new QMenu(this);
        QAction* bringForwardAction = arrangeMenu->addAction(tr("Bring forward"));
        QAction* sendBackAction = arrangeMenu->addAction(tr("Send back"));
        arrangeMenu->addSeparator();
        QAction* groupAction = arrangeMenu->addAction(tr("Group"));
        groupAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
        groupAction->setShortcutVisibleInContextMenu(true);
        QAction* ungroupAction = arrangeMenu->addAction(tr("Ungroup"));
        ungroupAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
        ungroupAction->setShortcutVisibleInContextMenu(true);
        for (QAction* action : arrangeMenu->actions()) {
            action->setShortcutContext(Qt::WidgetShortcut);
        }
        addMenuButton(tr("Arrange"), tr("Z-order and grouping"), arrangeMenu);
        m_toolbar->addSeparator();

        {
            QSettings ds(QStringLiteral("Hephaestus"), QStringLiteral("HUIVisualEditor"));
            m_showGrid = ds.value(QStringLiteral("gridVisible"), true).toBool();
            m_snapToGrid = ds.value(QStringLiteral("snapToGrid"), true).toBool();
            m_snapToGuides = ds.value(QStringLiteral("snapToGuides"), true).toBool();
            m_gridStepSetting = ds.value(QStringLiteral("gridStep"), 8).toInt();
            m_showCenterGuidesSetting = ds.value(QStringLiteral("centerGuides"), true).toBool();
        }

        m_gridVisibleAction = new QAction(Theme::Icon(QStringLiteral("grid")), tr("Grid"), m_toolbar);
        m_gridVisibleAction->setToolTip(tr("Show grid"));
        m_gridVisibleAction->setCheckable(true);
        m_gridVisibleAction->setChecked(m_showGrid);
        m_toolbar->addAction(m_gridVisibleAction);

        m_snapGridAction = new QAction(Theme::Icon(QStringLiteral("snap")), tr("Snap to grid"), m_toolbar);
        m_snapGridAction->setCheckable(true);
        m_snapGridAction->setChecked(m_snapToGrid);
        m_toolbar->addAction(m_snapGridAction);

        m_gridStepSpin = new QSpinBox(m_toolbar);
        m_gridStepSpin->setRange(2, 128);
        m_gridStepSpin->setPrefix(tr("Grid "));
        m_gridStepSpin->setSuffix(tr(" px"));
        m_gridStepSpin->setToolTip(tr("Grid size in pixels"));
        m_gridStepSpin->setValue(m_gridStepSetting);
        m_toolbar->addWidget(m_gridStepSpin);

        auto* guidesMenu = new QMenu(this);
        m_snapGuidesAction = guidesMenu->addAction(tr("Snap to guides"));
        m_snapGuidesAction->setCheckable(true);
        m_snapGuidesAction->setChecked(m_snapToGuides);
        m_centerGuidesAction = guidesMenu->addAction(tr("Canvas center guides"));
        m_centerGuidesAction->setCheckable(true);
        m_centerGuidesAction->setChecked(m_showCenterGuidesSetting);
        addMenuButton(tr("Guides"), tr("Guide snapping and canvas center guides"), guidesMenu);

        auto* spacer = new QWidget(m_toolbar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_toolbar->addWidget(spacer);

        m_resolutionCombo = new QComboBox(m_toolbar);
        m_resolutionCombo->setToolTip(tr("Preview resolution"));
        m_resolutionCombo->addItem("1280x720");
        m_resolutionCombo->addItem("1920x1080");
        m_resolutionCombo->addItem("800x600");
        m_toolbar->addWidget(m_resolutionCombo);

        m_zoomCombo = new QComboBox(m_toolbar);
        m_zoomCombo->setToolTip(tr("Zoom"));
        m_zoomCombo->addItems({"50%", "75%", "100%", "150%", "200%"});
        m_zoomCombo->setEditable(true);
        m_zoomCombo->lineEdit()->setReadOnly(true);
        m_zoomCombo->setCurrentText("100%");
        m_toolbar->addWidget(m_zoomCombo);
        m_toolbar->addSeparator();

        QAction* openIdeAction = new QAction(Theme::Icon(QStringLiteral("code")), tr("Open in IDE"), this);
        addTextAction(openIdeAction);
        m_previewAction = new QAction(Theme::Icon(QStringLiteral("play")), tr("Preview"), this);
        m_previewAction->setToolTip(tr("Interact with the UI as it runs"));
        m_previewAction->setCheckable(true);
        addTextAction(m_previewAction);

        auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
        rootLayout->addWidget(mainSplitter, 1);

        mainSplitter->setChildrenCollapsible(false);
        auto* leftPanel = new QWidget(mainSplitter);
        leftPanel->setObjectName(QStringLiteral("EditorPanel"));
        leftPanel->setAttribute(Qt::WA_StyledBackground, true);
        auto* leftLayout = new QVBoxLayout(leftPanel);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->setSpacing(0);
        leftLayout->addWidget(CreatePanelHeader(tr("Hierarchy"), leftPanel));

        m_hierarchyTree = new HUIHierarchyTreeWidget();
        m_hierarchyTree->setHeaderHidden(true);
        m_hierarchyTree->setDragDropMode(QAbstractItemView::InternalMove);
        m_hierarchyTree->setDefaultDropAction(Qt::MoveAction);
        m_hierarchyTree->setSelectionMode(QAbstractItemView::SingleSelection);
        m_hierarchyTree->header()->setStretchLastSection(true);
        m_hierarchyTree->setFrameShape(QFrame::NoFrame);
        m_hierarchyTree->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_hierarchyTree, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            QTreeWidgetItem* item = m_hierarchyTree->itemAt(pos);
            if (item) {
                m_hierarchyTree->setCurrentItem(item);
            }
            QMenu menu(m_hierarchyTree);
            menu.addAction(tr("Duplicate"), this, [this]() { duplicateSelected(); });
            menu.addAction(tr("Delete"), this, [this]() { deleteSelected(); });
            menu.addSeparator();
            menu.addAction(tr("Group"), this, [this]() { groupSelectedWidgets(); });
            menu.addAction(tr("Ungroup"), this, [this]() { ungroupSelectedWidgets(); });
            menu.addSeparator();
            menu.addAction(tr("Bring Forward"), this, [this]() { bringForwardSelection(); });
            menu.addAction(tr("Send Backward"), this, [this]() { sendBackwardSelection(); });
            menu.exec(m_hierarchyTree->viewport()->mapToGlobal(pos));
        });
        leftLayout->addWidget(m_hierarchyTree, 2);

        leftLayout->addWidget(CreatePanelHeader(tr("Palette"), leftPanel));

        m_paletteList = new QListWidget(leftPanel);
        m_paletteList->setObjectName(QStringLiteral("WidgetPalette"));
        m_paletteList->setViewMode(QListView::IconMode);
        m_paletteList->setMovement(QListView::Static);
        m_paletteList->setResizeMode(QListView::Adjust);
        m_paletteList->setIconSize(QSize(28, 20));
        m_paletteList->setGridSize(QSize(120, 60));
        m_paletteList->setSpacing(4);
        m_paletteList->setWordWrap(true);
        m_paletteList->setFrameShape(QFrame::NoFrame);
        m_paletteList->setToolTip(tr("Double-click to add to the selected container."));
        for (HUIWidgetType type : {HUIWidgetType::Panel, HUIWidgetType::Canvas, HUIWidgetType::VerticalBox,
                                   HUIWidgetType::HorizontalBox, HUIWidgetType::Grid, HUIWidgetType::Text,
                                   HUIWidgetType::Button, HUIWidgetType::Image}) {
            auto* item = new QListWidgetItem(WidgetTypeIcon(type, QSize(28, 20)), WidgetTypeLabel(type), m_paletteList);
            item->setData(Qt::UserRole, WidgetTypeKey(type));
            item->setSizeHint(QSize(116, 56));
        }
        leftLayout->addWidget(m_paletteList, 1);

        auto* centerFrame = new QFrame(mainSplitter);
        auto* centerLayout = new QVBoxLayout(centerFrame);
        centerLayout->setContentsMargins(0, 0, 0, 0);
        centerLayout->setSpacing(0);

        m_canvas = new HUIDesignCanvas(centerFrame);
        centerLayout->addWidget(m_canvas, 1);

        auto* controllerBar = new QWidget(centerFrame);
        controllerBar->setObjectName(QStringLiteral("EditorPanel"));
        controllerBar->setAttribute(Qt::WA_StyledBackground, true);
        auto* controllerRow = new QHBoxLayout(controllerBar);
        controllerRow->setContentsMargins(12, 8, 12, 8);
        controllerRow->setSpacing(8);
        controllerRow->addWidget(new QLabel(tr("Controller"), controllerBar));
        m_controllerEdit = new QLineEdit(controllerBar);
        m_controllerEdit->setPlaceholderText(tr("Optional Lua controller path"));
        controllerRow->addWidget(m_controllerEdit, 1);
        centerLayout->addWidget(controllerBar);

        m_previewThrottleTimer = new QTimer(this);
        m_previewThrottleTimer->setSingleShot(true);
        connect(m_previewThrottleTimer, &QTimer::timeout, this, [this]() { flushThrottledPreviewRebuild(); });

        m_nudgeUndoTimer = new QTimer(this);
        m_nudgeUndoTimer->setSingleShot(true);
        connect(m_nudgeUndoTimer, &QTimer::timeout, this, [this]() { m_nudgeUndoSessionActive = false; });

        auto* inspectorPanel = new QWidget(mainSplitter);
        inspectorPanel->setObjectName(QStringLiteral("EditorPanel"));
        inspectorPanel->setAttribute(Qt::WA_StyledBackground, true);
        auto* inspectorLayout = new QVBoxLayout(inspectorPanel);
        inspectorLayout->setContentsMargins(0, 0, 0, 0);
        inspectorLayout->setSpacing(0);
        inspectorLayout->addWidget(CreatePanelHeader(tr("Details"), inspectorPanel));

        m_inspectorScroll = new QScrollArea(inspectorPanel);
        m_inspectorScroll->setWidgetResizable(true);
        m_inspectorScroll->setFrameShape(QFrame::NoFrame);
        m_inspectorContainer = new QWidget(m_inspectorScroll);
        m_inspectorFormLayout = new QVBoxLayout(m_inspectorContainer);
        m_inspectorFormLayout->setContentsMargins(12, 4, 12, 12);
        m_inspectorFormLayout->setSpacing(4);
        m_inspectorFormLayout->addStretch();
        m_inspectorScroll->setWidget(m_inspectorContainer);
        inspectorLayout->addWidget(m_inspectorScroll, 1);

        mainSplitter->setStretchFactor(0, 0);
        mainSplitter->setStretchFactor(1, 1);
        mainSplitter->setStretchFactor(2, 0);
        mainSplitter->setSizes({264, 960, 380});

        connect(saveAction, &QAction::triggered, this, [this]() { save(); });
        connect(saveAsAction, &QAction::triggered, this, [this]() { saveAs(); });
        connect(undoAction, &QAction::triggered, this, [this]() { undo(); });
        connect(redoAction, &QAction::triggered, this, [this]() { redo(); });
        connect(duplicateAction, &QAction::triggered, this, [this]() { duplicateSelected(); });
        connect(deleteAction, &QAction::triggered, this, [this]() { deleteSelected(); });
        connect(m_previewAction, &QAction::toggled, this, [this](bool enabled) {
            m_canvas->setPreviewMode(enabled);
            m_previewMode = enabled;
            if (!enabled) {
                rebuildCanvas();
            }
        });
        connect(openIdeAction, &QAction::triggered, this, [this]() { openInIde(); });

        connect(m_gridVisibleAction, &QAction::toggled, this, [this](bool v) {
            m_showGrid = v;
            persistDesignSettings();
            updateCanvasGridOptionsOnly();
        });
        connect(m_snapGridAction, &QAction::toggled, this, [this](bool v) {
            m_snapToGrid = v;
            persistDesignSettings();
        });
        connect(m_snapGuidesAction, &QAction::toggled, this, [this](bool v) {
            m_snapToGuides = v;
            persistDesignSettings();
        });
        connect(m_centerGuidesAction, &QAction::toggled, this, [this](bool v) {
            m_showCenterGuidesSetting = v;
            persistDesignSettings();
            updateCanvasGridOptionsOnly();
        });
        connect(m_gridStepSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
            m_gridStepSetting = v;
            persistDesignSettings();
            updateCanvasGridOptionsOnly();
        });
        connect(bringForwardAction, &QAction::triggered, this, [this]() { bringForwardSelection(); });
        connect(sendBackAction, &QAction::triggered, this, [this]() { sendBackwardSelection(); });
        connect(groupAction, &QAction::triggered, this, [this]() { groupSelectedWidgets(); });
        connect(ungroupAction, &QAction::triggered, this, [this]() { ungroupSelectedWidgets(); });

        connect(m_hierarchyTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
            auto items = m_hierarchyTree->selectedItems();
            if (items.isEmpty()) return;
            m_selectedPath = items.front()->data(0, Qt::UserRole).toString();
            syncCanvasSelectionFromHierarchy();
            refreshSelection();
        });
        m_hierarchyTree->afterDrop = [this]() { syncDocumentFromHierarchyDrop(); };

        connect(m_paletteList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
            addWidgetByKey(item->data(Qt::UserRole).toString());
        });
        connect(m_controllerEdit, &QLineEdit::editingFinished, this, [this]() {
            if (m_document.controller != ToStdString(m_controllerEdit->text())) {
                pushUndoSnapshot();
                m_document.controller = ToStdString(m_controllerEdit->text());
                markDirty();
            }
        });
        connect(m_resolutionCombo, &QComboBox::currentTextChanged, this, [this](const QString& value) {
            const QStringList parts = value.split('x');
            if (parts.size() == 2) {
                m_previewSize = QSize(parts[0].toInt(), parts[1].toInt());
                rebuildCanvas();
            }
        });
        connect(m_zoomCombo, &QComboBox::currentTextChanged, this, [this](const QString& value) {
            QString trimmed = value;
            trimmed.remove('%');
            m_canvas->setZoom(trimmed.toDouble() / 100.0);
        });

        m_canvas->onZoomChanged = [this](double z) {
            QSignalBlocker blocker(m_zoomCombo);
            m_zoomCombo->setEditText(QString::number(qRound(z * 100.0)) + QLatin1Char('%'));
        };

        m_canvas->onCanvasMousePress = [this](QMouseEvent* e) { handleDesignCanvasMousePress(e); };
        m_canvas->onCanvasMouseMove = [this](QMouseEvent* e) { handleDesignCanvasMouseMove(e); };
        m_canvas->onCanvasMouseRelease = [this](QMouseEvent* e) { handleDesignCanvasMouseRelease(e); };
        m_canvas->onPointerMove = [this](const QPointF& designPos) {
            // Design mode: skip routing pointer into UIScreen::Update so widgets are not interactable
            // and hover/press does not fight canvas hit-testing. Preview toolbar mode enables this.
            if (!m_previewMode) {
                return;
            }
            updatePreviewFrame({UIMouseEvent{UIMouseEventType::Move,
                                             {static_cast<float>(designPos.x()), static_cast<float>(designPos.y())},
                                             0}});
        };
        m_canvas->onPointerPress = [this](const QPointF& designPos, int button) {
            if (!m_previewMode) {
                return;
            }
            updatePreviewFrame({UIMouseEvent{UIMouseEventType::Press,
                                             {static_cast<float>(designPos.x()), static_cast<float>(designPos.y())},
                                             button}});
        };
        m_canvas->onPointerRelease = [this](const QPointF& designPos, int button) {
            if (!m_previewMode) {
                return;
            }
            updatePreviewFrame({UIMouseEvent{UIMouseEventType::Release,
                                             {static_cast<float>(designPos.x()), static_cast<float>(designPos.y())},
                                             button}});
        };
    }

    void setupShortcuts()
    {
        const auto addChildShortcut = [this](const QKeySequence& seq, auto fn) {
            auto* sc = new QShortcut(seq, this, std::move(fn));
            sc->setContext(Qt::WidgetWithChildrenShortcut);
            return sc;
        };
        addChildShortcut(QKeySequence::Save, [this]() { save(); });
        addChildShortcut(QKeySequence::Undo, [this]() { undo(); });
        addChildShortcut(QKeySequence::Redo, [this]() { redo(); });
        addChildShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), [this]() { duplicateSelected(); });
        addChildShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), [this]() { groupSelectedWidgets(); });
        addChildShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), [this]() { ungroupSelectedWidgets(); });
        addChildShortcut(QKeySequence::Delete, [this]() { deleteSelected(); });
    }

    bool loadFromDisk(const QString& absolutePath)
    {
        QFile file(absolutePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Cannot Open UI"), tr("Failed to open %1").arg(absolutePath));
            return false;
        }

        const std::string contents = file.readAll().toStdString();
        file.close();

        HUIParser parser;
        HUIDocument document;
        if (!parser.ParseString(contents, document)) {
            QMessageBox::warning(this, tr("Invalid .hui"), QString::fromStdString(parser.GetErrorString()));
            return false;
        }

        m_document = std::move(document);
        m_absolutePath = absolutePath;
        m_controllerEdit->setText(ToQString(m_document.controller));
        m_dirty = false;
        m_undoStack.clear();
        m_redoStack.clear();
        if (m_document.root.id.empty()) m_document.root.id = "root";
        if (m_selectedPath.isEmpty()) m_selectedPath.clear();
        m_canvasSelection.clear();
        rebuildAll();
        return true;
    }

    bool save()
    {
        QFile file(m_absolutePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QMessageBox::warning(this, tr("Save Failed"), tr("Failed to save %1").arg(m_absolutePath));
            return false;
        }
        const std::string payload = serializeHUIDocument(m_document);
        file.write(payload.c_str(), static_cast<qint64>(payload.size()));
        file.close();
        m_dirty = false;
        updateWindowTitle();
        return true;
    }

    bool saveAs()
    {
        const QString target = QFileDialog::getSaveFileName(this, tr("Save UI As"), m_absolutePath, tr("Hephaestus UI (*.hui)"));
        if (target.isEmpty()) return false;
        m_absolutePath = target;
        return save();
    }

    bool confirmClose()
    {
        if (!m_dirty) return true;
        QMessageBox::StandardButton result = QMessageBox::question(
            this,
            tr("Unsaved Changes"),
            tr("Save changes to %1 before closing?").arg(QFileInfo(m_absolutePath).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (result == QMessageBox::Cancel) return false;
        if (result == QMessageBox::Save) return save();
        return true;
    }

    void updateWindowTitle()
    {
        QString name = QFileInfo(m_absolutePath).fileName();
        setWindowTitle(tr("Visual UI Editor - %1%2").arg(name, m_dirty ? "*" : ""));
    }

    void rebuildAll()
    {
        rebuildHierarchy();
        rebuildCanvas();
        rebuildInspector();
        updateWindowTitle();
    }

    void rebuildHierarchy()
    {
        m_hierarchyTree->clear();
        auto* rootItem = new QTreeWidgetItem();
        populateHierarchyItem(rootItem, m_document.root, QString());
        rootItem->setExpanded(true);
        rootItem->setFlags((rootItem->flags() | Qt::ItemIsDropEnabled) & ~Qt::ItemIsDragEnabled);
        m_hierarchyTree->addTopLevelItem(rootItem);
        if (!m_selectedPath.isEmpty()) selectHierarchyPath(m_selectedPath);
        else m_hierarchyTree->setCurrentItem(rootItem);
    }

    void populateHierarchyItem(QTreeWidgetItem* item, const HUIWidgetNode& node, const QString& path)
    {
        const QString displayName = node.id.empty() ? WidgetTypeLabel(node.type) : ToQString(node.id);
        item->setText(0, displayName);
        item->setIcon(0, WidgetTypeIcon(node.type, QSize(16, 16)));
        item->setToolTip(0, WidgetTypeLabel(node.type));
        item->setData(0, Qt::UserRole, path);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsSelectable | Qt::ItemIsEnabled);

        for (size_t i = 0; i < node.children.size(); ++i) {
            auto* childItem = new QTreeWidgetItem(item);
            populateHierarchyItem(childItem, node.children[i], MakeChildPath(path, static_cast<int>(i)));
        }
    }

    void selectHierarchyPath(const QString& path)
    {
        QList<QTreeWidgetItem*> all = m_hierarchyTree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive);
        for (QTreeWidgetItem* item : all) {
            if (item->data(0, Qt::UserRole).toString() == path) {
                m_hierarchyTree->setCurrentItem(item);
                return;
            }
        }
    }

    void rebuildCanvas()
    {
        m_canvas->setPreviewSize(m_previewSize);
        HUIBuilder builder;
        HUIBuilder::BuildResult previewBuild = builder.Build(m_document);
        m_previewScreen = previewBuild.screen;
        updatePreviewFrame({});
    }

    void updatePreviewFrame(const std::vector<UIMouseEvent>& mouseEvents)
    {
        if (!m_previewScreen) {
            m_previewElements.clear();
            m_previewCommands.clear();
            m_canvas->setPreviewElements({});
            m_canvas->setDrawCommands({});
            applyCanvasChrome();
            return;
        }

        UIContext context;
        context.SetViewportSize({static_cast<float>(m_previewSize.width()), static_cast<float>(m_previewSize.height())});
        context.SetDeltaTimeSeconds(0.0f);
        m_previewScreen->SetViewportSize(context.GetViewportSize());
        m_previewScreen->Update(context, mouseEvents);

        m_previewRenderer.Reset();
        m_previewScreen->Draw(m_previewRenderer);
        m_previewCommands = m_previewRenderer.GetDrawCommands();
        m_previewElements.clear();

        if (UICanvas* rootCanvas = m_previewScreen->GetRootCanvas()) {
            const auto& children = rootCanvas->GetChildren();
            if (!children.empty() && children.front()) {
                CollectPreviewElementInfos(m_document.root, children.front().get(), QString(), QString(), false, 0, m_previewElements);
            }
        }

        m_canvas->setDrawCommands(m_previewCommands);
        m_canvas->setPreviewElements(m_previewElements);
        applyCanvasChrome();
    }

    void refreshSelection()
    {
        selectHierarchyPath(m_selectedPath);
        rebuildCanvas();
        if (!m_designInteractionActive) {
            rebuildInspector();
        }
    }

    void rebuildInspector()
    {
        ClearLayout(m_inspectorFormLayout);
        HUIWidgetNode* node = NodeForPath(m_document.root, m_selectedPath);
        if (!node) {
            m_inspectorFormLayout->addWidget(new QLabel(tr("Select a widget to edit its properties."), m_inspectorContainer));
            m_inspectorFormLayout->addStretch();
            return;
        }

        auto addSection = [this](const QString& title) {
            auto* heading = new QLabel(title.toUpper(), m_inspectorContainer);
            heading->setProperty("role", QStringLiteral("section"));
            m_inspectorFormLayout->addWidget(heading);
            auto* section = new QWidget(m_inspectorContainer);
            auto* form = new QFormLayout(section);
            form->setContentsMargins(0, 0, 0, 0);
            form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            m_inspectorFormLayout->addWidget(section);
            return form;
        };

        auto makeSpin = [this](double value, double min, double max, double step = 1.0) {
            auto* spin = new QDoubleSpinBox(m_inspectorContainer);
            spin->setRange(min, max);
            spin->setSingleStep(step);
            spin->setDecimals(2);
            spin->setValue(value);
            return spin;
        };

        auto transformTargets = [this]() -> QSet<QString> {
            QSet<QString> t;
            if (!m_canvasSelection.isEmpty()) {
                t = m_canvasSelection;
            } else if (!m_selectedPath.isEmpty()) {
                t.insert(m_selectedPath);
            }
            return t;
        };

        auto applyCanvasTransform = [this, transformTargets](auto fn) {
            pushUndoSnapshot();
            const QSet<QString> paths = transformTargets();
            for (const QString& p : paths) {
                if (!ParentAllowsFreeformChildDrag(m_document, p)) {
                    continue;
                }
                if (HUIWidgetNode* live = NodeForPath(m_document.root, p)) {
                    fn(*live);
                }
            }
            markDirty();
            rebuildAll();
        };

        QFormLayout* transform = addSection(tr("Transform"));
        if (m_canvasSelection.size() > 1) {
            transform->addRow(QString(),
                                new QLabel(tr("Multiple selection: transform applies to each Canvas child."), m_inspectorContainer));
        }

        auto* posX = makeSpin(node->position.x, -5000.0, 5000.0);
        auto* posY = makeSpin(node->position.y, -5000.0, 5000.0);
        auto* posRow = new QWidget(m_inspectorContainer);
        auto* posLayout = new QHBoxLayout(posRow);
        posLayout->setContentsMargins(0, 0, 0, 0);
        posLayout->addWidget(posX);
        posLayout->addWidget(posY);
        transform->addRow(tr("Position"), posRow);

        auto* sizeW = makeSpin(node->size.x, 0.0, 5000.0);
        auto* sizeH = makeSpin(node->size.y, 0.0, 5000.0);
        auto* sizeRow = new QWidget(m_inspectorContainer);
        auto* sizeLayout = new QHBoxLayout(sizeRow);
        sizeLayout->setContentsMargins(0, 0, 0, 0);
        sizeLayout->addWidget(sizeW);
        sizeLayout->addWidget(sizeH);
        transform->addRow(tr("Size"), sizeRow);

        auto* anchorCombo = new QComboBox(m_inspectorContainer);
        anchorCombo->addItems({"TopLeft","TopCenter","TopRight","CenterLeft","Center","CenterRight","BottomLeft","BottomCenter","BottomRight","Stretch"});
        anchorCombo->setCurrentText(AnchorLabel(node->anchor));
        transform->addRow(tr("Anchor"), anchorCombo);

        auto* hAlignCombo = new QComboBox(m_inspectorContainer);
        hAlignCombo->addItems({"Left","Center","Right","Stretch"});
        hAlignCombo->setCurrentText(HAlignLabel(node->hAlignment));
        transform->addRow(tr("H Align"), hAlignCombo);

        auto* vAlignCombo = new QComboBox(m_inspectorContainer);
        vAlignCombo->addItems({"Top","Center","Bottom","Stretch"});
        vAlignCombo->setCurrentText(VAlignLabel(node->vAlignment));
        transform->addRow(tr("V Align"), vAlignCombo);

        auto* presetRow = new QWidget(m_inspectorContainer);
        auto* presetLayout = new QHBoxLayout(presetRow);
        presetLayout->setContentsMargins(0, 0, 0, 0);
        presetLayout->setSpacing(4);
        auto addAlignPreset = [this, presetLayout, applyCanvasTransform](const QString& label, UIHAlignment ha, UIVAlignment va) {
            auto* b = new QPushButton(label, m_inspectorContainer);
            b->setFixedHeight(26);
            QObject::connect(b, &QPushButton::clicked, this, [applyCanvasTransform, ha, va]() {
                applyCanvasTransform([ha, va](HUIWidgetNode& n) {
                    n.hAlignment = ha;
                    n.vAlignment = va;
                });
            });
            presetLayout->addWidget(b);
        };
        addAlignPreset(QStringLiteral("LT"), UIHAlignment::Left, UIVAlignment::Top);
        addAlignPreset(QStringLiteral("CT"), UIHAlignment::Center, UIVAlignment::Top);
        addAlignPreset(QStringLiteral("RT"), UIHAlignment::Right, UIVAlignment::Top);
        addAlignPreset(QStringLiteral("LC"), UIHAlignment::Left, UIVAlignment::Center);
        addAlignPreset(QStringLiteral("CC"), UIHAlignment::Center, UIVAlignment::Center);
        addAlignPreset(QStringLiteral("RC"), UIHAlignment::Right, UIVAlignment::Center);
        transform->addRow(tr("Align presets"), presetRow);

        auto* zSpin = new QSpinBox(m_inspectorContainer);
        zSpin->setRange(-100000, 100000);
        zSpin->setValue(node->zOrder);
        transform->addRow(tr("Z-order"), zSpin);

        QFormLayout* identity = addSection(tr("Identity"));
        identity->addRow(tr("Type"), new QLabel(WidgetTypeLabel(node->type), m_inspectorContainer));
        auto* idEdit = new QLineEdit(ToQString(node->id), m_inspectorContainer);
        identity->addRow(tr("ID"), idEdit);
        auto* classEdit = new QLineEdit(ToQString(node->styleClass), m_inspectorContainer);
        identity->addRow(tr("Class"), classEdit);

        connect(idEdit, &QLineEdit::editingFinished, this, [this, idEdit, node]() {
            if (node->id == ToStdString(idEdit->text())) return;
            pushUndoSnapshot();
            node->id = ToStdString(idEdit->text());
            markDirty();
            rebuildAll();
        });
        connect(classEdit, &QLineEdit::editingFinished, this, [this, classEdit, node]() {
            if (node->styleClass == ToStdString(classEdit->text())) return;
            pushUndoSnapshot();
            node->styleClass = ToStdString(classEdit->text());
            markDirty();
            rebuildAll();
        });

        QFormLayout* layout = addSection(tr("Layout"));

        auto* opacitySpin = makeSpin(node->opacity, 0.0, 1.0, 0.05);
        layout->addRow(tr("Opacity"), opacitySpin);

        auto* visibleCheck = new QCheckBox(tr("Visible"), m_inspectorContainer);
        visibleCheck->setChecked(node->visible);
        layout->addRow(QString(), visibleCheck);

        auto* enabledCheck = new QCheckBox(tr("Enabled"), m_inspectorContainer);
        enabledCheck->setChecked(node->enabled);
        layout->addRow(QString(), enabledCheck);

        auto* interactCheck = new QCheckBox(tr("Interactable"), m_inspectorContainer);
        interactCheck->setChecked(node->interactable);
        layout->addRow(QString(), interactCheck);

        auto* clippingCheck = new QCheckBox(tr("Clip Children"), m_inspectorContainer);
        clippingCheck->setChecked(node->clippingEnabled);
        layout->addRow(QString(), clippingCheck);

        auto applyNodeMutation = [this](auto fn) {
            pushUndoSnapshot();
            HUIWidgetNode* live = NodeForPath(m_document.root, m_selectedPath);
            if (!live) return;
            fn(*live);
            markDirty();
            rebuildAll();
        };

        connect(posX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCanvasTransform](double value) {
            applyCanvasTransform([value](HUIWidgetNode& n) { n.position.x = static_cast<float>(value); });
        });
        connect(posY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCanvasTransform](double value) {
            applyCanvasTransform([value](HUIWidgetNode& n) { n.position.y = static_cast<float>(value); });
        });
        connect(sizeW, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCanvasTransform](double value) {
            applyCanvasTransform([value](HUIWidgetNode& n) { n.size.x = static_cast<float>(value); });
        });
        connect(sizeH, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyCanvasTransform](double value) {
            applyCanvasTransform([value](HUIWidgetNode& n) { n.size.y = static_cast<float>(value); });
        });
        connect(opacitySpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyNodeMutation](double value) {
            applyNodeMutation([value](HUIWidgetNode& n) { n.opacity = static_cast<float>(value); });
        });
        connect(anchorCombo, &QComboBox::currentTextChanged, this, [applyCanvasTransform](const QString& value) {
            applyCanvasTransform([value](HUIWidgetNode& n) { n.anchor = AnchorFromLabel(value); });
        });
        connect(hAlignCombo, &QComboBox::currentTextChanged, this, [applyCanvasTransform](const QString& value) {
            applyCanvasTransform([value](HUIWidgetNode& n) { n.hAlignment = HAlignFromLabel(value); });
        });
        connect(vAlignCombo, &QComboBox::currentTextChanged, this, [applyCanvasTransform](const QString& value) {
            applyCanvasTransform([value](HUIWidgetNode& n) { n.vAlignment = VAlignFromLabel(value); });
        });
        connect(zSpin, qOverload<int>(&QSpinBox::valueChanged), this, [applyCanvasTransform](int value) {
            applyCanvasTransform([value](HUIWidgetNode& n) { n.zOrder = value; });
        });
        connect(visibleCheck, &QCheckBox::toggled, this, [applyNodeMutation](bool checked) {
            applyNodeMutation([checked](HUIWidgetNode& n) { n.visible = checked; });
        });
        connect(enabledCheck, &QCheckBox::toggled, this, [applyNodeMutation](bool checked) {
            applyNodeMutation([checked](HUIWidgetNode& n) { n.enabled = checked; });
        });
        connect(interactCheck, &QCheckBox::toggled, this, [applyNodeMutation](bool checked) {
            applyNodeMutation([checked](HUIWidgetNode& n) { n.interactable = checked; });
        });
        connect(clippingCheck, &QCheckBox::toggled, this, [applyNodeMutation](bool checked) {
            applyNodeMutation([checked](HUIWidgetNode& n) { n.clippingEnabled = checked; });
        });

        if (node->type == HUIWidgetType::Text || node->type == HUIWidgetType::Button || node->type == HUIWidgetType::Image ||
            node->type == HUIWidgetType::VerticalBox || node->type == HUIWidgetType::HorizontalBox || node->type == HUIWidgetType::Grid) {
            QFormLayout* content = addSection(tr("Content"));

            if (node->type == HUIWidgetType::Text || node->type == HUIWidgetType::Button) {
                auto* textEdit = new QLineEdit(ToQString(node->text), m_inspectorContainer);
                content->addRow(tr("Text"), textEdit);
                connect(textEdit, &QLineEdit::editingFinished, this, [this, textEdit]() {
                    HUIWidgetNode* live = NodeForPath(m_document.root, m_selectedPath);
                    if (!live || live->text == ToStdString(textEdit->text())) return;
                    pushUndoSnapshot();
                    live->text = ToStdString(textEdit->text());
                    markDirty();
                    rebuildAll();
                });

                auto* textAlignCombo = new QComboBox(m_inspectorContainer);
                textAlignCombo->addItems({"Left","Center","Right"});
                textAlignCombo->setCurrentText(TextAlignLabel(node->textAlignment));
                content->addRow(tr("Text Align"), textAlignCombo);
                connect(textAlignCombo, &QComboBox::currentTextChanged, this, [applyNodeMutation](const QString& value) {
                    applyNodeMutation([value](HUIWidgetNode& n) { n.textAlignment = TextAlignFromLabel(value); });
                });
            }

            if (node->type == HUIWidgetType::Image) {
                auto* imageIdEdit = new QLineEdit(ToQString(node->imageId), m_inspectorContainer);
                content->addRow(tr("Image"), imageIdEdit);
                connect(imageIdEdit, &QLineEdit::editingFinished, this, [this, imageIdEdit]() {
                    HUIWidgetNode* live = NodeForPath(m_document.root, m_selectedPath);
                    if (!live || live->imageId == ToStdString(imageIdEdit->text())) return;
                    pushUndoSnapshot();
                    live->imageId = ToStdString(imageIdEdit->text());
                    markDirty();
                    rebuildAll();
                });
                auto* scaleCombo = new QComboBox(m_inspectorContainer);
                scaleCombo->addItems({"Stretch","Fit","Fill","Center"});
                scaleCombo->setCurrentText(ScaleModeLabel(node->imageScaleMode));
                content->addRow(tr("Scale Mode"), scaleCombo);
                connect(scaleCombo, &QComboBox::currentTextChanged, this, [applyNodeMutation](const QString& value) {
                    applyNodeMutation([value](HUIWidgetNode& n) { n.imageScaleMode = ScaleModeFromLabel(value); });
                });
            }

            if (node->type == HUIWidgetType::VerticalBox || node->type == HUIWidgetType::HorizontalBox) {
                auto* spacingSpin = makeSpin(node->spacing, 0.0, 200.0);
                content->addRow(tr("Spacing"), spacingSpin);
                connect(spacingSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyNodeMutation](double value) {
                    applyNodeMutation([value](HUIWidgetNode& n) { n.spacing = static_cast<float>(value); });
                });
            }

            if (node->type == HUIWidgetType::Grid) {
                auto* columnsSpin = new QSpinBox(m_inspectorContainer);
                columnsSpin->setRange(1, 12);
                columnsSpin->setValue(node->gridColumns);
                content->addRow(tr("Columns"), columnsSpin);
                connect(columnsSpin, qOverload<int>(&QSpinBox::valueChanged), this, [applyNodeMutation](int value) {
                    applyNodeMutation([value](HUIWidgetNode& n) { n.gridColumns = value; });
                });
            }
        }

        QFormLayout* styleSection = addSection(tr("Primary Style"));
        auto editableStyle = [node]() -> std::pair<UIStyle*, bool*> {
            if (node->type == HUIWidgetType::Button) return {&node->normalStyle, &node->hasNormalStyle};
            return {&node->style, &node->hasStyle};
        }();

        auto* bgColorButton = new QPushButton(m_inspectorContainer);
        auto* textColorButton = new QPushButton(m_inspectorContainer);
        StyleInspectorColorPickerButton(bgColorButton, ToQColor(editableStyle.first->backgroundColor),
            tr("Click to choose background color"));
        StyleInspectorColorPickerButton(textColorButton, ToQColor(editableStyle.first->textColor),
            tr("Click to choose text color"));
        auto* cornerSpin = makeSpin(editableStyle.first->cornerRadius, 0.0, 64.0);
        auto* fontSizeSpin = makeSpin(editableStyle.first->fontSize, 0.0, 96.0);
        styleSection->addRow(tr("Background"), bgColorButton);
        styleSection->addRow(tr("Text Color"), textColorButton);
        styleSection->addRow(tr("Corner Radius"), cornerSpin);
        styleSection->addRow(tr("Font Size"), fontSizeSpin);

        connect(bgColorButton, &QPushButton::clicked, this, [this]() {
            HUIWidgetNode* live = NodeForPath(m_document.root, m_selectedPath);
            if (!live) return;
            UIStyle* style = (live->type == HUIWidgetType::Button) ? &live->normalStyle : &live->style;
            bool* hasStyle = (live->type == HUIWidgetType::Button) ? &live->hasNormalStyle : &live->hasStyle;
            QColor picked = PickColor(this, ToQColor(style->backgroundColor));
            pushUndoSnapshot();
            *hasStyle = true;
            style->backgroundColor = ToUIColor(picked);
            markDirty();
            rebuildAll();
        });
        connect(textColorButton, &QPushButton::clicked, this, [this]() {
            HUIWidgetNode* live = NodeForPath(m_document.root, m_selectedPath);
            if (!live) return;
            UIStyle* style = (live->type == HUIWidgetType::Button) ? &live->normalStyle : &live->style;
            bool* hasStyle = (live->type == HUIWidgetType::Button) ? &live->hasNormalStyle : &live->hasStyle;
            QColor picked = PickColor(this, ToQColor(style->textColor));
            pushUndoSnapshot();
            *hasStyle = true;
            style->textColor = ToUIColor(picked);
            markDirty();
            rebuildAll();
        });
        connect(cornerSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
            HUIWidgetNode* live = NodeForPath(m_document.root, m_selectedPath);
            if (!live) return;
            pushUndoSnapshot();
            if (live->type == HUIWidgetType::Button) {
                live->hasNormalStyle = true;
                live->normalStyle.cornerRadius = static_cast<float>(value);
            } else {
                live->hasStyle = true;
                live->style.cornerRadius = static_cast<float>(value);
            }
            markDirty();
            rebuildAll();
        });
        connect(fontSizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
            HUIWidgetNode* live = NodeForPath(m_document.root, m_selectedPath);
            if (!live) return;
            pushUndoSnapshot();
            if (live->type == HUIWidgetType::Button) {
                live->hasNormalStyle = true;
                live->normalStyle.fontSize = static_cast<float>(value);
            } else {
                live->hasStyle = true;
                live->style.fontSize = static_cast<float>(value);
            }
            markDirty();
            rebuildAll();
        });

        if (node->type == HUIWidgetType::Button) {
            QFormLayout* hoveredSection = addSection(tr("Hovered"));
            auto* hoverBgButton = new QPushButton(m_inspectorContainer);
            auto* hoverTextButton = new QPushButton(m_inspectorContainer);
            {
                const UIStyle hoverBase = node->hasHoveredStyle ? node->hoveredStyle : ButtonEditorBaseStyle(*node);
                StyleInspectorColorPickerButton(hoverBgButton, ToQColor(hoverBase.backgroundColor),
                    tr("Click to choose hovered background color"));
                StyleInspectorColorPickerButton(hoverTextButton, ToQColor(hoverBase.textColor),
                    tr("Click to choose hovered text color"));
            }
            hoveredSection->addRow(tr("Background"), hoverBgButton);
            hoveredSection->addRow(tr("Text Color"), hoverTextButton);

            connect(hoverBgButton, &QPushButton::clicked, this, [this]() {
                HUIWidgetNode* live = NodeForPath(m_document.root, m_selectedPath);
                if (!live || live->type != HUIWidgetType::Button) {
                    return;
                }
                const UIColor initialColor =
                    live->hasHoveredStyle ? live->hoveredStyle.backgroundColor : ButtonEditorBaseStyle(*live).backgroundColor;
                QColor picked = PickColor(this, ToQColor(initialColor));
                pushUndoSnapshot();
                SeedHoveredButtonStyle(*live);
                live->hoveredStyle.backgroundColor = ToUIColor(picked);
                markDirty();
                rebuildAll();
            });
            connect(hoverTextButton, &QPushButton::clicked, this, [this]() {
                HUIWidgetNode* live = NodeForPath(m_document.root, m_selectedPath);
                if (!live || live->type != HUIWidgetType::Button) {
                    return;
                }
                const UIColor initialColor =
                    live->hasHoveredStyle ? live->hoveredStyle.textColor : ButtonEditorBaseStyle(*live).textColor;
                QColor picked = PickColor(this, ToQColor(initialColor));
                pushUndoSnapshot();
                SeedHoveredButtonStyle(*live);
                live->hoveredStyle.textColor = ToUIColor(picked);
                markDirty();
                rebuildAll();
            });

            QFormLayout* pressedSection = addSection(tr("Pressed"));
            auto* pressedBgButton = new QPushButton(m_inspectorContainer);
            auto* pressedTextButton = new QPushButton(m_inspectorContainer);
            {
                const UIStyle pressedBase = node->hasPressedStyle ? node->pressedStyle : ButtonEditorBaseStyle(*node);
                StyleInspectorColorPickerButton(pressedBgButton, ToQColor(pressedBase.backgroundColor),
                    tr("Click to choose pressed background color"));
                StyleInspectorColorPickerButton(pressedTextButton, ToQColor(pressedBase.textColor),
                    tr("Click to choose pressed text color"));
            }
            pressedSection->addRow(tr("Background"), pressedBgButton);
            pressedSection->addRow(tr("Text Color"), pressedTextButton);

            connect(pressedBgButton, &QPushButton::clicked, this, [this]() {
                HUIWidgetNode* live = NodeForPath(m_document.root, m_selectedPath);
                if (!live || live->type != HUIWidgetType::Button) {
                    return;
                }
                const UIColor initialColor =
                    live->hasPressedStyle ? live->pressedStyle.backgroundColor : ButtonEditorBaseStyle(*live).backgroundColor;
                QColor picked = PickColor(this, ToQColor(initialColor));
                pushUndoSnapshot();
                SeedPressedButtonStyle(*live);
                live->pressedStyle.backgroundColor = ToUIColor(picked);
                markDirty();
                rebuildAll();
            });
            connect(pressedTextButton, &QPushButton::clicked, this, [this]() {
                HUIWidgetNode* live = NodeForPath(m_document.root, m_selectedPath);
                if (!live || live->type != HUIWidgetType::Button) {
                    return;
                }
                const UIColor initialColor =
                    live->hasPressedStyle ? live->pressedStyle.textColor : ButtonEditorBaseStyle(*live).textColor;
                QColor picked = PickColor(this, ToQColor(initialColor));
                pushUndoSnapshot();
                SeedPressedButtonStyle(*live);
                live->pressedStyle.textColor = ToUIColor(picked);
                markDirty();
                rebuildAll();
            });
        }

        m_inspectorFormLayout->addStretch();
    }

    void pushUndoSnapshot()
    {
        const std::string snapshot = serializeHUIDocument(m_document);
        if (!m_undoStack.empty() && m_undoStack.back() == snapshot) return;
        m_undoStack.push_back(snapshot);
        m_redoStack.clear();
    }

    void markDirty()
    {
        m_dirty = true;
        updateWindowTitle();
    }

    void undo()
    {
        if (m_undoStack.empty()) return;
        m_redoStack.push_back(serializeHUIDocument(m_document));
        restoreSnapshot(m_undoStack.back());
        m_undoStack.pop_back();
    }

    void redo()
    {
        if (m_redoStack.empty()) return;
        m_undoStack.push_back(serializeHUIDocument(m_document));
        restoreSnapshot(m_redoStack.back());
        m_redoStack.pop_back();
    }

    void restoreSnapshot(const std::string& snapshot)
    {
        HUIParser parser;
        HUIDocument restored;
        if (!parser.ParseString(snapshot, restored)) {
            QMessageBox::warning(this, tr("Undo Failed"), QString::fromStdString(parser.GetErrorString()));
            return;
        }
        m_document = std::move(restored);
        m_controllerEdit->setText(ToQString(m_document.controller));
        m_dirty = true;
        rebuildAll();
    }

    void addWidgetByKey(const QString& key)
    {
        HUIWidgetType type = HUIWidgetType::Panel;
        if (key == "Canvas") type = HUIWidgetType::Canvas;
        else if (key == "VerticalBox") type = HUIWidgetType::VerticalBox;
        else if (key == "HorizontalBox") type = HUIWidgetType::HorizontalBox;
        else if (key == "Grid") type = HUIWidgetType::Grid;
        else if (key == "Text") type = HUIWidgetType::Text;
        else if (key == "Button") type = HUIWidgetType::Button;
        else if (key == "Image") type = HUIWidgetType::Image;

        QString parentPath = m_selectedPath;
        HUIWidgetNode* parent = NodeForPath(m_document.root, parentPath);
        if (!parent || !IsContainerType(parent->type)) {
            parentPath = ParentPathOf(m_selectedPath);
            parent = NodeForPath(m_document.root, parentPath);
        }
        if (!parent || !IsContainerType(parent->type)) {
            parentPath.clear();
            parent = &m_document.root;
        }

        pushUndoSnapshot();
        HUIWidgetNode node = MakeDefaultNode(type);
        node.id = ToStdString(makeUniqueId(BaseIdForType(type)));
        parent->children.push_back(std::move(node));
        m_selectedPath = MakeChildPath(parentPath, static_cast<int>(parent->children.size() - 1));
        markDirty();
        rebuildAll();
    }

    QString makeUniqueId(const QString& base) const
    {
        std::unordered_map<std::string, int> counts;
        CollectIds(m_document.root, counts);
        QString candidate = base;
        int suffix = 1;
        while (counts.find(ToStdString(candidate)) != counts.end()) {
            candidate = base + "_" + QString::number(suffix++);
        }
        return candidate;
    }

    void duplicateSelected()
    {
        if (m_canvasSelection.size() > 1) {
            const QString parentPath = ParentPathOf(*m_canvasSelection.constBegin());
            for (const QString& p : m_canvasSelection) {
                if (ParentPathOf(p) != parentPath) {
                    return;
                }
            }
            HUIWidgetNode* parent = NodeForPath(m_document.root, parentPath);
            if (!parent) {
                return;
            }
            QVector<int> ix;
            for (const QString& p : m_canvasSelection) {
                ix.push_back(ParsePath(p).last());
            }
            std::sort(ix.begin(), ix.end());
            pushUndoSnapshot();
            std::unordered_map<std::string, int> counts;
            CollectIds(m_document.root, counts);
            for (int k = ix.size() - 1; k >= 0; --k) {
                const int i = ix[k];
                HUIWidgetNode dup = parent->children[static_cast<size_t>(i)];
                EnsureUniqueIds(dup, counts);
                parent->children.insert(parent->children.begin() + i + 1, std::move(dup));
            }
            m_canvasSelection.clear();
            markDirty();
            rebuildAll();
            return;
        }

        if (m_selectedPath.isEmpty()) return;
        QString parentPath = ParentPathOf(m_selectedPath);
        HUIWidgetNode* parent = NodeForPath(m_document.root, parentPath);
        HUIWidgetNode* node = NodeForPath(m_document.root, m_selectedPath);
        if (!parent || !node) return;

        pushUndoSnapshot();
        HUIWidgetNode duplicate = *node;
        std::unordered_map<std::string, int> counts;
        CollectIds(m_document.root, counts);
        EnsureUniqueIds(duplicate, counts);
        const int index = ParsePath(m_selectedPath).last();
        parent->children.insert(parent->children.begin() + index + 1, std::move(duplicate));
        m_selectedPath = MakeChildPath(parentPath, index + 1);
        markDirty();
        rebuildAll();
    }

    void deleteSelected()
    {
        if (m_canvasSelection.size() > 1) {
            const QString parentPath = ParentPathOf(*m_canvasSelection.constBegin());
            for (const QString& p : m_canvasSelection) {
                if (ParentPathOf(p) != parentPath) {
                    return;
                }
            }
            HUIWidgetNode* parent = NodeForPath(m_document.root, parentPath);
            if (!parent) {
                return;
            }
            QVector<int> ix;
            for (const QString& p : m_canvasSelection) {
                ix.push_back(ParsePath(p).last());
            }
            std::sort(ix.begin(), ix.end(), std::greater<int>());
            pushUndoSnapshot();
            for (int i : ix) {
                parent->children.erase(parent->children.begin() + i);
            }
            m_canvasSelection.clear();
            m_selectedPath = parentPath;
            syncHierarchyToSelection();
            markDirty();
            rebuildAll();
            return;
        }

        if (m_selectedPath.isEmpty()) return;
        QString parentPath = ParentPathOf(m_selectedPath);
        HUIWidgetNode* parent = NodeForPath(m_document.root, parentPath);
        if (!parent) return;
        const QVector<int> indices = ParsePath(m_selectedPath);
        if (indices.isEmpty()) return;
        const int index = indices.last();
        if (index < 0 || index >= static_cast<int>(parent->children.size())) return;

        pushUndoSnapshot();
        parent->children.erase(parent->children.begin() + index);
        m_selectedPath = parentPath;
        markDirty();
        rebuildAll();
    }

    void syncDocumentFromHierarchyDrop()
    {
        if (m_hierarchyTree->topLevelItemCount() == 0) return;
        std::unordered_map<std::string, HUIWidgetNode> nodeCopies;
        copyNodeMap(m_document.root, QString(), nodeCopies);

        QTreeWidgetItem* rootItem = m_hierarchyTree->topLevelItem(0);
        const std::string rootPath = ToStdString(rootItem->data(0, Qt::UserRole).toString());
        if (nodeCopies.find(rootPath) == nodeCopies.end()) return;

        pushUndoSnapshot();
        m_document.root = rebuildNodeFromItem(rootItem, nodeCopies);
        markDirty();
        rebuildAll();
    }

    void copyNodeMap(const HUIWidgetNode& node, const QString& path, std::unordered_map<std::string, HUIWidgetNode>& out)
    {
        out[ToStdString(path)] = node;
        for (size_t i = 0; i < node.children.size(); ++i) {
            copyNodeMap(node.children[i], MakeChildPath(path, static_cast<int>(i)), out);
        }
    }

    HUIWidgetNode rebuildNodeFromItem(QTreeWidgetItem* item, const std::unordered_map<std::string, HUIWidgetNode>& source)
    {
        HUIWidgetNode node = source.at(ToStdString(item->data(0, Qt::UserRole).toString()));
        node.children.clear();
        for (int i = 0; i < item->childCount(); ++i) {
            node.children.push_back(rebuildNodeFromItem(item->child(i), source));
        }
        return node;
    }

    void persistDesignSettings();
    void updateCanvasGridOptionsOnly();
    void applyCanvasChrome();
    void syncCanvasSelectionFromHierarchy();
    void syncHierarchyToSelection();
    void scheduleThrottledPreviewRebuild();
    void flushThrottledPreviewRebuild();
    void captureDragStartState();
    float snapThresholdDesign() const;
    void applySnapAfterMove(bool altHeld);
    void applyResizeStep(const QPointF& designDelta, bool altHeld);
    [[nodiscard]] bool selectionHasFreeformDragTargets() const;
    void handleDesignCanvasMousePress(QMouseEvent* e);
    void handleDesignCanvasMouseMove(QMouseEvent* e);
    void handleDesignCanvasMouseRelease(QMouseEvent* e);
    void applyMarqueeSelection(const QRectF& screenRect, bool addToSelection);
    void bringForwardSelection();
    void sendBackwardSelection();
    void groupSelectedWidgets();
    void ungroupSelectedWidgets();
    void nudgeCanvasSelection(int dx, int dy, int stepPx);
    void beginNudgeUndoSession();

    void openInIde()
    {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_absolutePath))) {
            QMessageBox::warning(this, tr("Open in IDE"),
                                 tr("Failed to open %1 with its default application.").arg(m_absolutePath));
        }
    }

    QString m_absolutePath;
    HUIDocument m_document;
    QString m_selectedPath;
    QSet<QString> m_canvasSelection;
    bool m_dirty = false;
    bool m_previewMode = false;
    QSize m_previewSize = QSize(1280, 720);
    std::vector<std::string> m_undoStack;
    std::vector<std::string> m_redoStack;
    std::shared_ptr<UIScreen> m_previewScreen;
    UIRenderer m_previewRenderer;
    std::vector<UIDrawCommand> m_previewCommands;
    std::vector<PreviewElementInfo> m_previewElements;

    enum class DesignDragKind { None, Marquee, Move, Resize };
    DesignDragKind m_designDragKind = DesignDragKind::None;
    bool m_designInteractionActive = false;
    bool m_undoPushedForDragSession = false;
    QPointF m_marqueeStartScreen;
    QPointF m_lastScreenForDrag;
    HUIResizeHandle m_activeResizeHandle = HUIResizeHandle::None;
    QString m_resizeTargetPath;
    QPointF m_resizeStartPos;
    QPointF m_resizeStartSize;
    std::map<QString, QRectF> m_dragStartLayoutRects;
    std::map<QString, QPointF> m_dragStartPositions;
    QList<QLineF> m_liveGuideLines;
    float m_liveGuideAlpha = 0.0f;

    bool m_showGrid = true;
    bool m_snapToGrid = true;
    bool m_snapToGuides = true;
    int m_gridStepSetting = 8;
    bool m_showCenterGuidesSetting = true;

    QTimer* m_previewThrottleTimer = nullptr;
    bool m_previewRebuildPending = false;
    QTimer* m_nudgeUndoTimer = nullptr;
    bool m_nudgeUndoSessionActive = false;

    QAction* m_gridVisibleAction = nullptr;
    QAction* m_snapGridAction = nullptr;
    QAction* m_snapGuidesAction = nullptr;
    QAction* m_centerGuidesAction = nullptr;
    QSpinBox* m_gridStepSpin = nullptr;

    QToolBar* m_toolbar = nullptr;
    QAction* m_previewAction = nullptr;
    QComboBox* m_resolutionCombo = nullptr;
    QComboBox* m_zoomCombo = nullptr;
    HUIHierarchyTreeWidget* m_hierarchyTree = nullptr;
    QListWidget* m_paletteList = nullptr;
    QLineEdit* m_controllerEdit = nullptr;
    HUIDesignCanvas* m_canvas = nullptr;
    QScrollArea* m_inspectorScroll = nullptr;
    QWidget* m_inspectorContainer = nullptr;
    QVBoxLayout* m_inspectorFormLayout = nullptr;
};

void HUIVisualEditorDialog::persistDesignSettings()
{
    QSettings s(QStringLiteral("Hephaestus"), QStringLiteral("HUIVisualEditor"));
    s.setValue(QStringLiteral("gridVisible"), m_showGrid);
    s.setValue(QStringLiteral("snapToGrid"), m_snapToGrid);
    s.setValue(QStringLiteral("snapToGuides"), m_snapToGuides);
    s.setValue(QStringLiteral("gridStep"), m_gridStepSetting);
    s.setValue(QStringLiteral("centerGuides"), m_showCenterGuidesSetting);
}

void HUIVisualEditorDialog::updateCanvasGridOptionsOnly()
{
    m_canvas->setGridOptions(m_showGrid, m_gridStepSetting, m_showCenterGuidesSetting);
    m_canvas->update();
}

void HUIVisualEditorDialog::applyCanvasChrome()
{
    m_canvas->setPrimarySelection(m_selectedPath);
    m_canvas->setSelectionSet(m_canvasSelection);
    const QRectF u = UnionPreviewRects(m_previewElements, m_canvasSelection);
    bool showHandles = false;
    if (m_canvasSelection.size() == 1 && ParentAllowsFreeformChildDrag(m_document, m_selectedPath)) {
        if (HUIWidgetNode* n = NodeForPath(m_document.root, m_selectedPath)) {
            showHandles = NodeEligibleForFreeformTransform(*n);
        }
    }
    m_canvas->setSelectionUnionDesign(u, showHandles);
    m_canvas->setGridOptions(m_showGrid, m_gridStepSetting, m_showCenterGuidesSetting);
    m_canvas->setAlignmentGuides(m_liveGuideLines, m_liveGuideAlpha);
}

void HUIVisualEditorDialog::syncCanvasSelectionFromHierarchy()
{
    m_canvasSelection.clear();
    if (!m_selectedPath.isEmpty()) {
        m_canvasSelection.insert(m_selectedPath);
    }
}

void HUIVisualEditorDialog::syncHierarchyToSelection()
{
    if (m_selectedPath.isEmpty()) {
        m_hierarchyTree->clearSelection();
        return;
    }
    selectHierarchyPath(m_selectedPath);
}

void HUIVisualEditorDialog::scheduleThrottledPreviewRebuild()
{
    m_previewRebuildPending = true;
    if (!m_previewThrottleTimer->isActive()) {
        m_previewThrottleTimer->start(14);
    }
}

void HUIVisualEditorDialog::flushThrottledPreviewRebuild()
{
    m_previewThrottleTimer->stop();
    if (!m_previewRebuildPending) {
        return;
    }
    m_previewRebuildPending = false;
    rebuildCanvas();
}

void HUIVisualEditorDialog::captureDragStartState()
{
    m_dragStartLayoutRects.clear();
    m_dragStartPositions.clear();
    for (const QString& p : m_canvasSelection) {
        HUIWidgetNode* n = NodeForPath(m_document.root, p);
        if (!n) {
            continue;
        }
        m_dragStartPositions[p] = QPointF(n->position.x, n->position.y);
        m_dragStartLayoutRects[p] = RectForPathInPreview(m_previewElements, p);
    }
    m_resizeTargetPath.clear();
    m_resizeStartPos = QPointF(0.0, 0.0);
    m_resizeStartSize = QPointF(0.0, 0.0);
    if (m_canvasSelection.size() == 1) {
        const QString only = *m_canvasSelection.constBegin();
        if (HUIWidgetNode* n = NodeForPath(m_document.root, only)) {
            m_resizeTargetPath = only;
            m_resizeStartPos = QPointF(n->position.x, n->position.y);
            m_resizeStartSize = QPointF(n->size.x, n->size.y);
        }
    }
}

float HUIVisualEditorDialog::snapThresholdDesign() const
{
    return std::max(2.0f, 5.0f / static_cast<float>(m_canvas->zoomLevel()));
}

void HUIVisualEditorDialog::applySnapAfterMove(bool altHeld)
{
    if (m_canvasSelection.isEmpty()) {
        return;
    }
    const float gridStep = static_cast<float>(m_gridStepSetting);
    const float thresh = snapThresholdDesign();

    QRectF u;
    bool any = false;
    for (const QString& p : m_canvasSelection) {
        auto itR = m_dragStartLayoutRects.find(p);
        auto itP = m_dragStartPositions.find(p);
        HUIWidgetNode* n = NodeForPath(m_document.root, p);
        if (!n || itR == m_dragStartLayoutRects.end() || itP == m_dragStartPositions.end()) {
            continue;
        }
        const QPointF d(n->position.x - static_cast<float>(itP->second.x()),
                        n->position.y - static_cast<float>(itP->second.y()));
        const QRectF r = itR->second.translated(d);
        u = any ? u.united(r) : r;
        any = true;
    }
    if (!any) {
        return;
    }

    const QString refParent = ParentPathOf(*m_canvasSelection.constBegin());
    std::vector<float> candX;
    std::vector<float> candY;
    const float W = static_cast<float>(m_previewSize.width());
    const float H = static_cast<float>(m_previewSize.height());
    if (m_snapToGuides && !altHeld) {
        candX.push_back(0.0f);
        candX.push_back(W * 0.5f);
        candX.push_back(W);
        candY.push_back(0.0f);
        candY.push_back(H * 0.5f);
        candY.push_back(H);
        for (const auto& el : m_previewElements) {
            if (el.path == refParent) {
                continue;
            }
            if (m_canvasSelection.contains(el.path)) {
                continue;
            }
            if (ParentPathOf(el.path) != refParent) {
                continue;
            }
            candX.push_back(static_cast<float>(el.rect.left()));
            candX.push_back(static_cast<float>(el.rect.right()));
            candX.push_back(static_cast<float>(el.rect.center().x()));
            candY.push_back(static_cast<float>(el.rect.top()));
            candY.push_back(static_cast<float>(el.rect.bottom()));
            candY.push_back(static_cast<float>(el.rect.center().y()));
        }
    }

    float bestDx = 0.0f;
    float bestDy = 0.0f;
    float bestDistX = 1e9f;
    float bestDistY = 1e9f;

    auto considerX = [&](float edge) {
        for (float c : candX) {
            const float dist = std::abs(edge - c);
            if (dist < thresh && dist < bestDistX) {
                bestDistX = dist;
                bestDx = c - edge;
            }
        }
    };
    auto considerY = [&](float edge) {
        for (float c : candY) {
            const float dist = std::abs(edge - c);
            if (dist < thresh && dist < bestDistY) {
                bestDistY = dist;
                bestDy = c - edge;
            }
        }
    };

    if (m_snapToGuides && !altHeld) {
        considerX(static_cast<float>(u.left()));
        considerX(static_cast<float>(u.right()));
        considerX(static_cast<float>(u.center().x()));
        considerY(static_cast<float>(u.top()));
        considerY(static_cast<float>(u.bottom()));
        considerY(static_cast<float>(u.center().y()));
    }

    if (bestDistX < 1e8f) {
        for (const QString& p : m_canvasSelection) {
            if (HUIWidgetNode* n = NodeForPath(m_document.root, p)) {
                n->position.x += bestDx;
            }
        }
    }
    if (bestDistY < 1e8f) {
        for (const QString& p : m_canvasSelection) {
            if (HUIWidgetNode* n = NodeForPath(m_document.root, p)) {
                n->position.y += bestDy;
            }
        }
    }

    if (m_snapToGrid && !altHeld) {
        for (const QString& p : m_canvasSelection) {
            if (HUIWidgetNode* n = NodeForPath(m_document.root, p)) {
                n->position.x = SnapScalar(n->position.x, gridStep, true, altHeld, thresh);
                n->position.y = SnapScalar(n->position.y, gridStep, true, altHeld, thresh);
            }
        }
    }

    if (m_snapToGuides && !altHeld && (bestDistX < 1e8f || bestDistY < 1e8f)) {
        QRectF u2;
        bool any2 = false;
        for (const QString& p : m_canvasSelection) {
            auto itR = m_dragStartLayoutRects.find(p);
            auto itP = m_dragStartPositions.find(p);
            HUIWidgetNode* n = NodeForPath(m_document.root, p);
            if (!n || itR == m_dragStartLayoutRects.end() || itP == m_dragStartPositions.end()) {
                continue;
            }
            const QPointF d(n->position.x - static_cast<float>(itP->second.x()),
                            n->position.y - static_cast<float>(itP->second.y()));
            const QRectF r = itR->second.translated(d);
            u2 = any2 ? u2.united(r) : r;
            any2 = true;
        }
        m_liveGuideLines.clear();
        if (any2) {
            const float H = static_cast<float>(m_previewSize.height());
            const float W = static_cast<float>(m_previewSize.width());
            if (bestDistX < 1e8f) {
                const float x = static_cast<float>(u2.left());
                m_liveGuideLines.append(QLineF(x, 0.0f, x, H));
            }
            if (bestDistY < 1e8f) {
                const float y = static_cast<float>(u2.top());
                m_liveGuideLines.append(QLineF(0.0f, y, W, y));
            }
        }
    } else {
        m_liveGuideLines.clear();
    }
}

void HUIVisualEditorDialog::applyResizeStep(const QPointF& designDelta, bool altHeld)
{
    HUIWidgetNode* n = NodeForPath(m_document.root, m_resizeTargetPath);
    if (!n || !NodeEligibleForFreeformTransform(*n)) {
        return;
    }
    const float dx = static_cast<float>(designDelta.x());
    const float dy = static_cast<float>(designDelta.y());
    switch (m_activeResizeHandle) {
        case HUIResizeHandle::Right:
            n->size.x = std::max(kMinWidgetSize, n->size.x + dx);
            break;
        case HUIResizeHandle::Left: {
            const float nw = n->size.x - dx;
            if (nw >= kMinWidgetSize) {
                n->position.x += dx;
                n->size.x = nw;
            }
            break;
        }
        case HUIResizeHandle::Bottom:
            n->size.y = std::max(kMinWidgetSize, n->size.y + dy);
            break;
        case HUIResizeHandle::Top: {
            const float nh = n->size.y - dy;
            if (nh >= kMinWidgetSize) {
                n->position.y += dy;
                n->size.y = nh;
            }
            break;
        }
        case HUIResizeHandle::BottomRight:
            n->size.x = std::max(kMinWidgetSize, n->size.x + dx);
            n->size.y = std::max(kMinWidgetSize, n->size.y + dy);
            break;
        case HUIResizeHandle::BottomLeft: {
            const float nw = n->size.x - dx;
            if (nw >= kMinWidgetSize) {
                n->position.x += dx;
                n->size.x = nw;
            }
            n->size.y = std::max(kMinWidgetSize, n->size.y + dy);
            break;
        }
        case HUIResizeHandle::TopRight:
            n->size.x = std::max(kMinWidgetSize, n->size.x + dx);
            {
                const float nh = n->size.y - dy;
                if (nh >= kMinWidgetSize) {
                    n->position.y += dy;
                    n->size.y = nh;
                }
            }
            break;
        case HUIResizeHandle::TopLeft: {
            const float nw = n->size.x - dx;
            if (nw >= kMinWidgetSize) {
                n->position.x += dx;
                n->size.x = nw;
            }
            const float nh = n->size.y - dy;
            if (nh >= kMinWidgetSize) {
                n->position.y += dy;
                n->size.y = nh;
            }
            break;
        }
        default:
            break;
    }
    if (m_snapToGrid && !altHeld) {
        const float th = snapThresholdDesign();
        const float step = static_cast<float>(m_gridStepSetting);
        n->position.x = SnapScalar(n->position.x, step, true, altHeld, th);
        n->position.y = SnapScalar(n->position.y, step, true, altHeld, th);
        n->size.x = std::max(kMinWidgetSize, SnapScalar(n->size.x, step, true, altHeld, th));
        n->size.y = std::max(kMinWidgetSize, SnapScalar(n->size.y, step, true, altHeld, th));
    }
}

bool HUIVisualEditorDialog::selectionHasFreeformDragTargets() const
{
    for (const QString& p : m_canvasSelection) {
        if (ParentAllowsFreeformChildDrag(m_document, p)) {
            return true;
        }
    }
    return false;
}

void HUIVisualEditorDialog::handleDesignCanvasMousePress(QMouseEvent* e)
{
    if (m_previewMode || e->button() != Qt::LeftButton) {
        return;
    }
    m_designInteractionActive = true;
    m_liveGuideLines.clear();
    m_liveGuideAlpha = 0.0f;
    const QPointF sp = e->position();
    const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);

    if (m_canvasSelection.size() == 1) {
        if (HUIWidgetNode* primaryNode = NodeForPath(m_document.root, m_selectedPath)) {
            if (ParentAllowsFreeformChildDrag(m_document, m_selectedPath) && NodeEligibleForFreeformTransform(*primaryNode)) {
                const HUIResizeHandle h = m_canvas->hitTestResizeHandle(sp);
                if (h != HUIResizeHandle::None) {
                    m_designDragKind = DesignDragKind::Resize;
                    m_activeResizeHandle = h;
                    m_undoPushedForDragSession = false;
                    m_lastScreenForDrag = sp;
                    return;
                }
            }
        }
    }

    const QString hit = m_canvas->hitTestTopmost(sp);
    if (hit.isEmpty()) {
        m_designDragKind = DesignDragKind::Marquee;
        m_marqueeStartScreen = sp;
        m_canvas->setMarqueeScreenRect(QRectF(sp, QSizeF(0.0, 0.0)));
        if (!shift) {
            m_canvasSelection.clear();
            m_selectedPath.clear();
            syncHierarchyToSelection();
        }
        m_undoPushedForDragSession = false;
        m_lastScreenForDrag = sp;
        return;
    }

    const QString hitParent = ParentPathOf(hit);
    if (!m_canvasSelection.isEmpty()) {
        const QString refParent = ParentPathOf(*m_canvasSelection.constBegin());
        if (refParent != hitParent) {
            m_canvasSelection.clear();
        }
    }

    if (shift) {
        if (m_canvasSelection.contains(hit)) {
            m_canvasSelection.remove(hit);
        } else {
            m_canvasSelection.insert(hit);
        }
        if (m_canvasSelection.isEmpty()) {
            m_selectedPath.clear();
        } else if (!m_canvasSelection.contains(m_selectedPath)) {
            m_selectedPath = hit;
        }
        syncHierarchyToSelection();
        applyCanvasChrome();
        m_canvas->update();
        m_designDragKind = selectionHasFreeformDragTargets() ? DesignDragKind::Move : DesignDragKind::None;
        if (m_designDragKind == DesignDragKind::Move) {
            captureDragStartState();
        }
        m_undoPushedForDragSession = false;
        m_lastScreenForDrag = sp;
        return;
    }

    if (!m_canvasSelection.contains(hit)) {
        m_canvasSelection = QSet<QString>{hit};
        m_selectedPath = hit;
        syncHierarchyToSelection();
    }
    m_designDragKind = selectionHasFreeformDragTargets() ? DesignDragKind::Move : DesignDragKind::None;
    if (m_designDragKind == DesignDragKind::Move) {
        captureDragStartState();
    }
    m_undoPushedForDragSession = false;
    m_lastScreenForDrag = sp;
}

void HUIVisualEditorDialog::handleDesignCanvasMouseMove(QMouseEvent* e)
{
    if (m_previewMode || m_designDragKind == DesignDragKind::None) {
        return;
    }
    const QPointF sp = e->position();
    const bool altHeld = e->modifiers().testFlag(Qt::AltModifier);

    if (m_designDragKind == DesignDragKind::Marquee) {
        m_canvas->setMarqueeScreenRect(QRectF(m_marqueeStartScreen, sp).normalized());
        return;
    }

    const QPointF screenDelta = sp - m_lastScreenForDrag;
    m_lastScreenForDrag = sp;
    const QPointF designDelta(screenDelta.x() / m_canvas->zoomLevel(), screenDelta.y() / m_canvas->zoomLevel());

    if (m_designDragKind == DesignDragKind::Move) {
        if (!m_undoPushedForDragSession) {
            pushUndoSnapshot();
            m_undoPushedForDragSession = true;
        }
        for (const QString& p : m_canvasSelection) {
            HUIWidgetNode* n = NodeForPath(m_document.root, p);
            if (!n || !ParentAllowsFreeformChildDrag(m_document, p)) {
                continue;
            }
            n->position.x += static_cast<float>(designDelta.x());
            n->position.y += static_cast<float>(designDelta.y());
        }
        applySnapAfterMove(altHeld);
        m_liveGuideAlpha = 0.9f;
        markDirty();
        scheduleThrottledPreviewRebuild();
        return;
    }

    if (m_designDragKind == DesignDragKind::Resize) {
        if (!m_undoPushedForDragSession) {
            pushUndoSnapshot();
            m_undoPushedForDragSession = true;
        }
        applyResizeStep(designDelta, altHeld);
        m_liveGuideAlpha = 0.85f;
        markDirty();
        scheduleThrottledPreviewRebuild();
    }
}

void HUIVisualEditorDialog::handleDesignCanvasMouseRelease(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) {
        return;
    }

    if (m_designDragKind == DesignDragKind::Marquee) {
        const QRectF rs = QRectF(m_marqueeStartScreen, e->position()).normalized();
        applyMarqueeSelection(rs, e->modifiers().testFlag(Qt::ShiftModifier));
        m_canvas->setMarqueeScreenRect({});
    }

    m_designDragKind = DesignDragKind::None;
    m_activeResizeHandle = HUIResizeHandle::None;
    m_designInteractionActive = false;
    m_undoPushedForDragSession = false;
    m_liveGuideAlpha = 0.0f;
    m_liveGuideLines.clear();
    flushThrottledPreviewRebuild();
    rebuildInspector();
    applyCanvasChrome();
    m_canvas->update();
}

void HUIVisualEditorDialog::applyMarqueeSelection(const QRectF& screenRect, bool addToSelection)
{
    const auto tl = m_canvas->screenPointToDesign(screenRect.topLeft());
    const auto br = m_canvas->screenPointToDesign(screenRect.bottomRight());
    if (!tl || !br) {
        return;
    }
    QRectF d = QRectF(*tl, *br).normalized();
    if (d.width() < 2.0 && d.height() < 2.0) {
        if (!addToSelection) {
            m_canvasSelection.clear();
            m_selectedPath.clear();
            syncHierarchyToSelection();
        }
        return;
    }

    QSet<QString> picked;
    QString parentRef;
    for (const auto& el : m_previewElements) {
        if (!d.intersects(el.rect)) {
            continue;
        }
        const QString pp = ParentPathOf(el.path);
        if (picked.isEmpty()) {
            parentRef = pp;
        }
        if (pp != parentRef) {
            continue;
        }
        picked.insert(el.path);
    }
    if (picked.isEmpty()) {
        if (!addToSelection) {
            m_canvasSelection.clear();
            m_selectedPath.clear();
        }
        syncHierarchyToSelection();
        return;
    }
    if (addToSelection) {
        m_canvasSelection.unite(picked);
    } else {
        m_canvasSelection = picked;
    }
    m_selectedPath = *m_canvasSelection.constBegin();
    syncHierarchyToSelection();
    markDirty();
}

void HUIVisualEditorDialog::bringForwardSelection()
{
    if (m_selectedPath.isEmpty()) {
        return;
    }
    pushUndoSnapshot();
    if (HUIWidgetNode* n = NodeForPath(m_document.root, m_selectedPath)) {
        n->zOrder += 1;
    }
    markDirty();
    rebuildAll();
}

void HUIVisualEditorDialog::sendBackwardSelection()
{
    if (m_selectedPath.isEmpty()) {
        return;
    }
    pushUndoSnapshot();
    if (HUIWidgetNode* n = NodeForPath(m_document.root, m_selectedPath)) {
        n->zOrder -= 1;
    }
    markDirty();
    rebuildAll();
}

void HUIVisualEditorDialog::groupSelectedWidgets()
{
    if (m_canvasSelection.size() < 2) {
        return;
    }
    QString parentPath = ParentPathOf(*m_canvasSelection.constBegin());
    for (const QString& p : m_canvasSelection) {
        if (ParentPathOf(p) != parentPath) {
            return;
        }
    }
    HUIWidgetNode* containerParent = NodeForPath(m_document.root, parentPath);
    if (!containerParent ||
        (containerParent->type != HUIWidgetType::Canvas && containerParent->type != HUIWidgetType::Panel)) {
        return;
    }
    for (const QString& p : m_canvasSelection) {
        if (HUIWidgetNode* n = NodeForPath(m_document.root, p)) {
            if (!NodeEligibleForFreeformTransform(*n)) {
                return;
            }
        }
    }

    QRectF u;
    bool any = false;
    for (const QString& p : m_canvasSelection) {
        const QRectF r = RectForPathInPreview(m_previewElements, p);
        if (r.isNull()) {
            return;
        }
        u = any ? u.united(r) : r;
        any = true;
    }
    const QRectF containerLayout = RectForPathInPreview(m_previewElements, parentPath);
    if (containerLayout.isNull()) {
        return;
    }
    const float padL = containerParent->padding.left;
    const float padT = containerParent->padding.top;
    const QPointF contentOrigin(containerLayout.left() + padL, containerLayout.top() + padT);

    QVector<QPair<int, QString>> ordered;
    ordered.reserve(static_cast<int>(m_canvasSelection.size()));
    for (const QString& p : m_canvasSelection) {
        ordered.push_back(qMakePair(ParsePath(p).last(), p));
    }
    std::sort(ordered.begin(), ordered.end(), [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
        return a.first < b.first;
    });
    QVector<int> ix;
    ix.reserve(ordered.size());
    for (const auto& pr : ordered) {
        ix.push_back(pr.first);
    }
    std::sort(ix.begin(), ix.end(), std::greater<int>());

    pushUndoSnapshot();
    std::vector<HUIWidgetNode> bucket;
    bucket.reserve(static_cast<size_t>(ix.size()));
    for (int i : ix) {
        bucket.push_back(std::move(containerParent->children[static_cast<size_t>(i)]));
        containerParent->children.erase(containerParent->children.begin() + i);
    }
    std::reverse(bucket.begin(), bucket.end());

    const int insertAt = ix.empty() ? 0 : ix.back();

    HUIWidgetNode panel = MakeDefaultNode(HUIWidgetType::Panel);
    panel.id = ToStdString(makeUniqueId(QStringLiteral("group")));
    panel.position.x = static_cast<float>(u.left() - contentOrigin.x());
    panel.position.y = static_cast<float>(u.top() - contentOrigin.y());
    panel.size.x = static_cast<float>(u.width());
    panel.size.y = static_cast<float>(u.height());
    panel.hasStyle = true;
    panel.style.backgroundColor = {0.15f, 0.16f, 0.2f, 0.4f};
    panel.style.cornerRadius = 4.0f;

    for (int k = 0; k < ordered.size(); ++k) {
        const QString& pth = ordered[k].second;
        const QRectF cr = RectForPathInPreview(m_previewElements, pth);
        bucket[static_cast<size_t>(k)].position.x = static_cast<float>(cr.left() - u.left());
        bucket[static_cast<size_t>(k)].position.y = static_cast<float>(cr.top() - u.top());
    }

    panel.children = std::move(bucket);
    containerParent->children.insert(containerParent->children.begin() + insertAt, std::move(panel));

    m_selectedPath = MakeChildPath(parentPath, insertAt);
    m_canvasSelection = QSet<QString>{m_selectedPath};
    markDirty();
    rebuildAll();
}

void HUIVisualEditorDialog::ungroupSelectedWidgets()
{
    if (m_selectedPath.isEmpty()) {
        return;
    }
    HUIWidgetNode* panelNode = NodeForPath(m_document.root, m_selectedPath);
    if (!panelNode || panelNode->type != HUIWidgetType::Panel || panelNode->children.empty()) {
        return;
    }
    const QString containerPath = ParentPathOf(m_selectedPath);
    HUIWidgetNode* containerParent = NodeForPath(m_document.root, containerPath);
    if (!containerParent ||
        (containerParent->type != HUIWidgetType::Canvas && containerParent->type != HUIWidgetType::Panel)) {
        return;
    }
    const QVector<int> pathIx = ParsePath(m_selectedPath);
    if (pathIx.isEmpty()) {
        return;
    }
    const int panelIndex = pathIx.last();

    pushUndoSnapshot();
    const size_t nKids = panelNode->children.size();
    std::vector<HUIWidgetNode> kids = std::move(panelNode->children);
    const float ppx = panelNode->position.x;
    const float ppy = panelNode->position.y;
    for (auto& ch : kids) {
        ch.position.x += ppx;
        ch.position.y += ppy;
    }
    containerParent->children.erase(containerParent->children.begin() + panelIndex);
    containerParent->children.insert(containerParent->children.begin() + panelIndex, std::make_move_iterator(kids.begin()),
                                       std::make_move_iterator(kids.end()));

    m_canvasSelection.clear();
    for (size_t i = 0; i < nKids; ++i) {
        m_canvasSelection.insert(MakeChildPath(containerPath, panelIndex + static_cast<int>(i)));
    }
    if (!m_canvasSelection.isEmpty()) {
        m_selectedPath = *m_canvasSelection.constBegin();
    }
    markDirty();
    rebuildAll();
}

void HUIVisualEditorDialog::beginNudgeUndoSession()
{
    if (!m_nudgeUndoSessionActive) {
        pushUndoSnapshot();
        m_nudgeUndoSessionActive = true;
    }
    m_nudgeUndoTimer->stop();
    m_nudgeUndoTimer->start(400);
}

void HUIVisualEditorDialog::nudgeCanvasSelection(int dx, int dy, int stepPx)
{
    if (m_canvasSelection.isEmpty()) {
        return;
    }
    const bool altHeld = QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier);
    beginNudgeUndoSession();
    for (const QString& p : m_canvasSelection) {
        if (!ParentAllowsFreeformChildDrag(m_document, p)) {
            continue;
        }
        if (HUIWidgetNode* n = NodeForPath(m_document.root, p)) {
            n->position.x += static_cast<float>(dx * stepPx);
            n->position.y += static_cast<float>(dy * stepPx);
            if (m_snapToGrid && !altHeld) {
                const float th = snapThresholdDesign();
                const float step = static_cast<float>(m_gridStepSetting);
                n->position.x = SnapScalar(n->position.x, step, true, altHeld, th);
                n->position.y = SnapScalar(n->position.y, step, true, altHeld, th);
            }
        }
    }
    markDirty();
    rebuildAll();
}

void HUIVisualEditorDialog::keyPressEvent(QKeyEvent* event)
{
    if (!m_previewMode && !m_canvasSelection.isEmpty() &&
        (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right || event->key() == Qt::Key_Up ||
         event->key() == Qt::Key_Down) &&
        !(event->modifiers() & Qt::ControlModifier)) {
        const int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        int dx = 0;
        int dy = 0;
        switch (event->key()) {
            case Qt::Key_Left: dx = -1; break;
            case Qt::Key_Right: dx = 1; break;
            case Qt::Key_Up: dy = -1; break;
            case Qt::Key_Down: dy = 1; break;
            default: break;
        }
        nudgeCanvasSelection(dx, dy, step);
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

} // namespace

bool OpenHUIVisualEditorDialog(QWidget* parent, const QString& absolutePath)
{
    auto* dialog = new HUIVisualEditorDialog(parent, absolutePath);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    return true;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
