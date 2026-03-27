#if HEPHAESTUS_EDITOR_QT_UI

#include "HUIVisualEditorDialog.h"

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
#include <QGroupBox>
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
#include <QProcess>
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

#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <unordered_map>

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
};

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

    out.push_back({path, parentPath, ToQRectF(runtimeElement->GetLayoutRect()), draggable, depth});

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
        CollectPreviewElementInfos(node.children[i],
                                   runtimeChildren[runtimeIndex].get(),
                                   childPath,
                                   path,
                                   node.type == HUIWidgetType::Canvas,
                                   depth + 1,
                                   out);
    }
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

    void setPreviewMode(bool preview)
    {
        m_previewMode = preview;
        update();
    }

    void setSelectionPath(const QString& path)
    {
        m_selectedPath = path;
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

    std::function<void(const QString&)> onSelect;
    std::function<void(const QString&, const QPointF&, bool)> onDrag;
    std::function<void(const QPointF&)> onPointerMove;
    std::function<void(const QPointF&, int)> onPointerPress;
    std::function<void(const QPointF&, int)> onPointerRelease;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(27, 28, 32));
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRectF screen = previewRect();
        painter.setPen(QPen(QColor(70, 74, 82), 1.0));
        painter.setBrush(QColor(36, 39, 46));
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
        painter.restore();

        if (!m_previewMode) {
            if (!m_hoveredPath.isEmpty() && m_hoveredPath != m_selectedPath) {
                const QRectF hovered = rectForPath(m_hoveredPath);
                if (!hovered.isNull()) {
                    painter.setPen(QPen(QColor(130, 170, 255, 160), 1.5));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(hovered.adjusted(-1.0, -1.0, 1.0, 1.0));
                }
            }
            const QRectF selected = rectForPath(m_selectedPath);
            if (!selected.isNull()) {
                painter.setPen(QPen(QColor(255, 194, 64), 2.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(selected.adjusted(-2.0, -2.0, 2.0, 2.0));
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) return;
        const QString hit = hitTest(event->position());
        if (!hit.isEmpty() && onSelect) onSelect(hit);
        m_dragPath = hit;
        m_lastMousePos = event->position();
        if (const auto designPos = screenToDesign(event->position()); designPos && onPointerPress) {
            onPointerPress(*designPos, static_cast<int>(event->button()));
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (const auto designPos = screenToDesign(event->position()); designPos) {
            m_hoveredPath = hitTest(event->position());
            if (onPointerMove) onPointerMove(*designPos);
        } else {
            m_hoveredPath.clear();
            if (onPointerMove) onPointerMove(QPointF(-10000.0, -10000.0));
        }

        if ((event->buttons() & Qt::LeftButton) && !m_dragPath.isEmpty() && onDrag) {
            const QPointF deltaPx = event->position() - m_lastMousePos;
            m_lastMousePos = event->position();
            onDrag(m_dragPath, QPointF(deltaPx.x() / m_zoom, deltaPx.y() / m_zoom), false);
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (onPointerRelease) {
            if (const auto designPos = screenToDesign(event->position()); designPos) {
                onPointerRelease(*designPos, static_cast<int>(event->button()));
            } else {
                onPointerRelease(QPointF(-10000.0, -10000.0), static_cast<int>(event->button()));
            }
        }
        if (event->button() == Qt::LeftButton && !m_dragPath.isEmpty() && onDrag) {
            onDrag(m_dragPath, QPointF(), true);
            m_dragPath.clear();
        }
    }

private:
    QRectF previewRect() const
    {
        QSizeF scaled(m_previewSize.width() * m_zoom, m_previewSize.height() * m_zoom);
        QPointF topLeft((width() - scaled.width()) * 0.5, (height() - scaled.height()) * 0.5);
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

    QString hitTest(const QPointF& pos) const
    {
        for (auto it = m_elements.rbegin(); it != m_elements.rend(); ++it) {
            if (designToScreen(it->rect).contains(pos)) return it->path;
        }
        return {};
    }

    QRectF rectForPath(const QString& path) const
    {
        for (const PreviewElementInfo& element : m_elements) {
            if (element.path == path) {
                return designToScreen(element.rect);
            }
        }
        return {};
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
    bool m_previewMode = false;
    QString m_selectedPath;
    QString m_hoveredPath;
    QString m_dragPath;
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

private:
    void buildUi()
    {
        auto* rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);

        m_toolbar = new QToolBar(this);
        m_toolbar->setIconSize(QSize(18, 18));
        rootLayout->addWidget(m_toolbar);

        QAction* saveAction = m_toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Save"));
        QAction* saveAsAction = m_toolbar->addAction(tr("Save As"));
        QAction* undoAction = m_toolbar->addAction(tr("Undo"));
        QAction* redoAction = m_toolbar->addAction(tr("Redo"));
        QAction* duplicateAction = m_toolbar->addAction(tr("Duplicate"));
        QAction* deleteAction = m_toolbar->addAction(tr("Delete"));
        m_toolbar->addSeparator();
        m_previewAction = m_toolbar->addAction(tr("Preview"));
        m_previewAction->setCheckable(true);
        QAction* openIdeAction = m_toolbar->addAction(tr("Open in IDE"));
        m_toolbar->addSeparator();

        m_resolutionCombo = new QComboBox(m_toolbar);
        m_resolutionCombo->addItem("1280x720");
        m_resolutionCombo->addItem("1920x1080");
        m_resolutionCombo->addItem("800x600");
        m_toolbar->addWidget(new QLabel(tr("Preview: "), m_toolbar));
        m_toolbar->addWidget(m_resolutionCombo);

        m_zoomCombo = new QComboBox(m_toolbar);
        m_zoomCombo->addItems({"50%", "75%", "100%", "150%", "200%"});
        m_zoomCombo->setCurrentText("100%");
        m_toolbar->addSeparator();
        m_toolbar->addWidget(new QLabel(tr("Zoom: "), m_toolbar));
        m_toolbar->addWidget(m_zoomCombo);

        auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
        rootLayout->addWidget(mainSplitter, 1);

        auto* leftPanel = new QWidget(mainSplitter);
        auto* leftLayout = new QVBoxLayout(leftPanel);
        leftLayout->setContentsMargins(8, 8, 8, 8);
        leftLayout->setSpacing(8);

        auto* hierarchyLabel = new QLabel(tr("Hierarchy"), leftPanel);
        hierarchyLabel->setStyleSheet("font-weight: 600;");
        leftLayout->addWidget(hierarchyLabel);

        m_hierarchyTree = new HUIHierarchyTreeWidget();
        m_hierarchyTree->setHeaderHidden(true);
        m_hierarchyTree->setDragDropMode(QAbstractItemView::InternalMove);
        m_hierarchyTree->setDefaultDropAction(Qt::MoveAction);
        m_hierarchyTree->setSelectionMode(QAbstractItemView::SingleSelection);
        m_hierarchyTree->header()->setStretchLastSection(true);
        leftLayout->addWidget(m_hierarchyTree, 2);

        auto* paletteLabel = new QLabel(tr("Palette"), leftPanel);
        paletteLabel->setStyleSheet("font-weight: 600;");
        leftLayout->addWidget(paletteLabel);

        m_paletteList = new QListWidget(leftPanel);
        for (HUIWidgetType type : {HUIWidgetType::Panel, HUIWidgetType::Canvas, HUIWidgetType::Text,
                                   HUIWidgetType::Button, HUIWidgetType::Image, HUIWidgetType::VerticalBox,
                                   HUIWidgetType::HorizontalBox, HUIWidgetType::Grid}) {
            auto* item = new QListWidgetItem(WidgetTypeLabel(type), m_paletteList);
            item->setData(Qt::UserRole, WidgetTypeKey(type));
        }
        leftLayout->addWidget(m_paletteList, 1);

        auto* centerFrame = new QFrame(mainSplitter);
        auto* centerLayout = new QVBoxLayout(centerFrame);
        centerLayout->setContentsMargins(8, 8, 8, 8);
        centerLayout->setSpacing(8);

        auto* controllerRow = new QHBoxLayout();
        controllerRow->addWidget(new QLabel(tr("Controller"), centerFrame));
        m_controllerEdit = new QLineEdit(centerFrame);
        m_controllerEdit->setPlaceholderText(tr("Optional Lua controller path"));
        controllerRow->addWidget(m_controllerEdit, 1);
        centerLayout->addLayout(controllerRow);

        m_canvas = new HUIDesignCanvas(centerFrame);
        centerLayout->addWidget(m_canvas, 1);

        auto* inspectorPanel = new QWidget(mainSplitter);
        auto* inspectorLayout = new QVBoxLayout(inspectorPanel);
        inspectorLayout->setContentsMargins(8, 8, 8, 8);
        inspectorLayout->setSpacing(8);
        auto* detailsLabel = new QLabel(tr("Details"), inspectorPanel);
        detailsLabel->setStyleSheet("font-weight: 600;");
        inspectorLayout->addWidget(detailsLabel);

        m_inspectorScroll = new QScrollArea(inspectorPanel);
        m_inspectorScroll->setWidgetResizable(true);
        m_inspectorContainer = new QWidget(m_inspectorScroll);
        m_inspectorFormLayout = new QVBoxLayout(m_inspectorContainer);
        m_inspectorFormLayout->setContentsMargins(4, 4, 4, 4);
        m_inspectorFormLayout->setSpacing(10);
        m_inspectorFormLayout->addStretch();
        m_inspectorScroll->setWidget(m_inspectorContainer);
        inspectorLayout->addWidget(m_inspectorScroll, 1);

        mainSplitter->setStretchFactor(0, 0);
        mainSplitter->setStretchFactor(1, 1);
        mainSplitter->setStretchFactor(2, 0);
        mainSplitter->setSizes({300, 900, 360});

        connect(saveAction, &QAction::triggered, this, [this]() { save(); });
        connect(saveAsAction, &QAction::triggered, this, [this]() { saveAs(); });
        connect(undoAction, &QAction::triggered, this, [this]() { undo(); });
        connect(redoAction, &QAction::triggered, this, [this]() { redo(); });
        connect(duplicateAction, &QAction::triggered, this, [this]() { duplicateSelected(); });
        connect(deleteAction, &QAction::triggered, this, [this]() { deleteSelected(); });
        connect(m_previewAction, &QAction::toggled, this, [this](bool enabled) {
            m_canvas->setPreviewMode(enabled);
            m_previewMode = enabled;
        });
        connect(openIdeAction, &QAction::triggered, this, [this]() { openInIde(); });

        connect(m_hierarchyTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
            auto items = m_hierarchyTree->selectedItems();
            if (items.isEmpty()) return;
            m_selectedPath = items.front()->data(0, Qt::UserRole).toString();
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

        m_canvas->onSelect = [this](const QString& path) {
            m_selectedPath = path;
            refreshSelection();
        };
        m_canvas->onDrag = [this](const QString& path, const QPointF& delta, bool finished) {
            handleCanvasDrag(path, delta, finished);
        };
        m_canvas->onPointerMove = [this](const QPointF& designPos) {
            updatePreviewFrame({UIMouseEvent{UIMouseEventType::Move,
                                             {static_cast<float>(designPos.x()), static_cast<float>(designPos.y())},
                                             0}});
        };
        m_canvas->onPointerPress = [this](const QPointF& designPos, int button) {
            updatePreviewFrame({UIMouseEvent{UIMouseEventType::Press,
                                             {static_cast<float>(designPos.x()), static_cast<float>(designPos.y())},
                                             button}});
        };
        m_canvas->onPointerRelease = [this](const QPointF& designPos, int button) {
            updatePreviewFrame({UIMouseEvent{UIMouseEventType::Release,
                                             {static_cast<float>(designPos.x()), static_cast<float>(designPos.y())},
                                             button}});
        };
    }

    void setupShortcuts()
    {
        new QShortcut(QKeySequence::Save, this, [this]() { save(); });
        new QShortcut(QKeySequence::Undo, this, [this]() { undo(); });
        new QShortcut(QKeySequence::Redo, this, [this]() { redo(); });
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this, [this]() { duplicateSelected(); });
        new QShortcut(QKeySequence::Delete, this, [this]() { deleteSelected(); });
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
        item->setText(0, QStringLiteral("%1 (%2)").arg(displayName, WidgetTypeLabel(node.type)));
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
            m_canvas->setSelectionPath(m_selectedPath);
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
        m_canvas->setSelectionPath(m_selectedPath);
    }

    void refreshSelection()
    {
        selectHierarchyPath(m_selectedPath);
        rebuildCanvas();
        rebuildInspector();
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
            auto* box = new QGroupBox(title, m_inspectorContainer);
            auto* form = new QFormLayout(box);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            box->setLayout(form);
            m_inspectorFormLayout->addWidget(box);
            return form;
        };

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
        auto makeSpin = [this](double value, double min, double max, double step = 1.0) {
            auto* spin = new QDoubleSpinBox(m_inspectorContainer);
            spin->setRange(min, max);
            spin->setSingleStep(step);
            spin->setDecimals(2);
            spin->setValue(value);
            return spin;
        };

        auto* posX = makeSpin(node->position.x, -5000.0, 5000.0);
        auto* posY = makeSpin(node->position.y, -5000.0, 5000.0);
        auto* posRow = new QWidget(m_inspectorContainer);
        auto* posLayout = new QHBoxLayout(posRow);
        posLayout->setContentsMargins(0, 0, 0, 0);
        posLayout->addWidget(posX);
        posLayout->addWidget(posY);
        layout->addRow(tr("Position"), posRow);

        auto* sizeW = makeSpin(node->size.x, 0.0, 5000.0);
        auto* sizeH = makeSpin(node->size.y, 0.0, 5000.0);
        auto* sizeRow = new QWidget(m_inspectorContainer);
        auto* sizeLayout = new QHBoxLayout(sizeRow);
        sizeLayout->setContentsMargins(0, 0, 0, 0);
        sizeLayout->addWidget(sizeW);
        sizeLayout->addWidget(sizeH);
        layout->addRow(tr("Size"), sizeRow);

        auto* opacitySpin = makeSpin(node->opacity, 0.0, 1.0, 0.05);
        layout->addRow(tr("Opacity"), opacitySpin);

        auto* anchorCombo = new QComboBox(m_inspectorContainer);
        anchorCombo->addItems({"TopLeft","TopCenter","TopRight","CenterLeft","Center","CenterRight","BottomLeft","BottomCenter","BottomRight","Stretch"});
        anchorCombo->setCurrentText(AnchorLabel(node->anchor));
        layout->addRow(tr("Anchor"), anchorCombo);

        auto* hAlignCombo = new QComboBox(m_inspectorContainer);
        hAlignCombo->addItems({"Left","Center","Right","Stretch"});
        hAlignCombo->setCurrentText(HAlignLabel(node->hAlignment));
        layout->addRow(tr("H Align"), hAlignCombo);

        auto* vAlignCombo = new QComboBox(m_inspectorContainer);
        vAlignCombo->addItems({"Top","Center","Bottom","Stretch"});
        vAlignCombo->setCurrentText(VAlignLabel(node->vAlignment));
        layout->addRow(tr("V Align"), vAlignCombo);

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

        connect(posX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyNodeMutation](double value) {
            applyNodeMutation([value](HUIWidgetNode& n) { n.position.x = static_cast<float>(value); });
        });
        connect(posY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyNodeMutation](double value) {
            applyNodeMutation([value](HUIWidgetNode& n) { n.position.y = static_cast<float>(value); });
        });
        connect(sizeW, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyNodeMutation](double value) {
            applyNodeMutation([value](HUIWidgetNode& n) { n.size.x = static_cast<float>(value); });
        });
        connect(sizeH, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyNodeMutation](double value) {
            applyNodeMutation([value](HUIWidgetNode& n) { n.size.y = static_cast<float>(value); });
        });
        connect(opacitySpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [applyNodeMutation](double value) {
            applyNodeMutation([value](HUIWidgetNode& n) { n.opacity = static_cast<float>(value); });
        });
        connect(anchorCombo, &QComboBox::currentTextChanged, this, [applyNodeMutation](const QString& value) {
            applyNodeMutation([value](HUIWidgetNode& n) { n.anchor = AnchorFromLabel(value); });
        });
        connect(hAlignCombo, &QComboBox::currentTextChanged, this, [applyNodeMutation](const QString& value) {
            applyNodeMutation([value](HUIWidgetNode& n) { n.hAlignment = HAlignFromLabel(value); });
        });
        connect(vAlignCombo, &QComboBox::currentTextChanged, this, [applyNodeMutation](const QString& value) {
            applyNodeMutation([value](HUIWidgetNode& n) { n.vAlignment = VAlignFromLabel(value); });
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

        auto* bgColorButton = new QPushButton(tr("Background"), m_inspectorContainer);
        auto* textColorButton = new QPushButton(tr("Text"), m_inspectorContainer);
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

    void handleCanvasDrag(const QString& path, const QPointF& delta, bool finished)
    {
        HUIWidgetNode* node = NodeForPath(m_document.root, path);
        HUIWidgetNode* parent = NodeForPath(m_document.root, ParentPathOf(path));
        if (!node || !parent || parent->type != HUIWidgetType::Canvas) return;

        if (!finished) {
            if (m_dragPath != path) {
                pushUndoSnapshot();
                m_dragPath = path;
            }
            node->position.x += static_cast<float>(delta.x());
            node->position.y += static_cast<float>(delta.y());
            markDirty();
            rebuildCanvas();
            rebuildInspector();
        } else {
            m_dragPath.clear();
        }
    }

    void openInIde()
    {
        QString editorPath = QSettings(QStringLiteral("Hephaestus"), QStringLiteral("Launcher"))
                                 .value(QStringLiteral("settings/editorPath"))
                                 .toString()
                                 .trimmed();
        if (!editorPath.isEmpty() && QFileInfo::exists(editorPath)) {
            if (!QProcess::startDetached(editorPath, {m_absolutePath})) {
                QMessageBox::warning(this, tr("Open in IDE"), tr("Failed to launch the configured editor."));
            }
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_absolutePath));
    }

    QString m_absolutePath;
    HUIDocument m_document;
    QString m_selectedPath;
    QString m_dragPath;
    bool m_dirty = false;
    bool m_previewMode = false;
    QSize m_previewSize = QSize(1280, 720);
    std::vector<std::string> m_undoStack;
    std::vector<std::string> m_redoStack;
    std::shared_ptr<UIScreen> m_previewScreen;
    UIRenderer m_previewRenderer;
    std::vector<UIDrawCommand> m_previewCommands;
    std::vector<PreviewElementInfo> m_previewElements;

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
