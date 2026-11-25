#if HEPHAESTUS_EDITOR_QT_UI

#include "RuntimeUIOverlayWidget.h"

#include "../../Assets/AssetManager.h"
#include "../HephaestusUI/Rendering/HUIGeometryBuilder.h"
#include "../HephaestusUI/UIDynamicImages.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Hephaestus {
namespace QtEditor {

namespace {

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

QColor toQColor(const Hephaestus::HephaestusUI::UIColor& color)
{
    return QColor::fromRgbF(clamp01(color.r), clamp01(color.g), clamp01(color.b), clamp01(color.a));
}

// The Qt family for the font file the game renderer would use, registered with Qt on first use.
QString GameFontFamily(const std::string& family, bool bold, bool italic)
{
    static std::unordered_map<std::string, QString> registered;
    const std::string file = Hephaestus::HephaestusUI::UIFontAtlas::ResolveFontFile(
        family, bold, italic, Hephaestus::HephaestusUI::HUIFontSearchPaths(AssetManager::GetInstance().GetProjectRoot()));
    if (file.empty()) {
        return QString::fromStdString(family);
    }
    auto it = registered.find(file);
    if (it == registered.end()) {
        const int id = QFontDatabase::addApplicationFont(QString::fromStdString(file));
        const QStringList families = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList{};
        it = registered.emplace(file, families.isEmpty() ? QString::fromStdString(family) : families.front()).first;
    }
    return it->second;
}

QRectF toQRectF(const Hephaestus::HephaestusUI::UIRect& rect)
{
    return QRectF(rect.x, rect.y, rect.width, rect.height);
}

int toQtAlign(Hephaestus::HephaestusUI::UITextAlignment alignment, bool wordWrap)
{
    int flags = Qt::AlignVCenter;
    switch (alignment) {
    case Hephaestus::HephaestusUI::UITextAlignment::Left:
        flags |= Qt::AlignLeft;
        break;
    case Hephaestus::HephaestusUI::UITextAlignment::Center:
        flags |= Qt::AlignHCenter;
        break;
    case Hephaestus::HephaestusUI::UITextAlignment::Right:
        flags |= Qt::AlignRight;
        break;
    }

    if (wordWrap) {
        flags |= Qt::TextWordWrap;
    }

    return flags;
}

} // namespace

RuntimeUIOverlayWidget::RuntimeUIOverlayWidget(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint |
                      Qt::NoDropShadowWindowHint | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAutoFillBackground(false);
    setMouseTracking(true);
}

void RuntimeUIOverlayWidget::SetMouseEventsEnabled(bool enabled)
{
    m_mouseEventsEnabled = enabled;
    setAttribute(Qt::WA_TransparentForMouseEvents, !enabled);
}

void RuntimeUIOverlayWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_mouseEventsEnabled && m_mouseCallback) {
        qreal dpr = devicePixelRatioF();
        UIMouseEventData data;
        data.type = UIMouseEventData::Type::Press;
        data.x = static_cast<float>(event->position().x() * dpr);
        data.y = static_cast<float>(event->position().y() * dpr);
        data.button = static_cast<int>(event->button());
        m_mouseCallback(data);
        event->accept();
    } else {
        event->ignore();
    }
}

void RuntimeUIOverlayWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_mouseEventsEnabled && m_mouseCallback) {
        qreal dpr = devicePixelRatioF();
        UIMouseEventData data;
        data.type = UIMouseEventData::Type::Release;
        data.x = static_cast<float>(event->position().x() * dpr);
        data.y = static_cast<float>(event->position().y() * dpr);
        data.button = static_cast<int>(event->button());
        m_mouseCallback(data);
        event->accept();
    } else {
        event->ignore();
    }
}

void RuntimeUIOverlayWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_mouseEventsEnabled && m_mouseCallback) {
        qreal dpr = devicePixelRatioF();
        UIMouseEventData data;
        data.type = UIMouseEventData::Type::Move;
        data.x = static_cast<float>(event->position().x() * dpr);
        data.y = static_cast<float>(event->position().y() * dpr);
        data.button = static_cast<int>(event->buttons());
        m_mouseCallback(data);
        event->accept();
    } else {
        event->ignore();
    }
}

void RuntimeUIOverlayWidget::SetDrawCommands(const std::vector<Hephaestus::HephaestusUI::UIDrawCommand>& commands)
{
    m_drawCommands = commands;
    update();
}

void RuntimeUIOverlayWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_drawCommands.empty()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    qreal dpiScale = devicePixelRatioF();
    if (dpiScale != 1.0f) {
        painter.scale(1.0f / dpiScale, 1.0f / dpiScale);
    }

    int clipDepth = 0;

    for (const auto& command : m_drawCommands) {
        if (std::holds_alternative<Hephaestus::HephaestusUI::UIDrawRectCommand>(command)) {
            const auto& drawRect = std::get<Hephaestus::HephaestusUI::UIDrawRectCommand>(command);
            const QRectF rect = toQRectF(drawRect.rect);

            QColor fill = toQColor(drawRect.fillColor);
            fill.setAlphaF(clamp01(fill.alphaF() * drawRect.opacity));
            QColor border = toQColor(drawRect.borderColor);
            border.setAlphaF(clamp01(border.alphaF() * drawRect.opacity));

            const bool drawFill = fill.alpha() > 0;
            const bool drawBorder = drawRect.borderWidth > 0.0f && border.alpha() > 0;
            if (!drawFill && !drawBorder) {
                continue;
            }

            painter.setBrush(drawFill ? QBrush(fill) : Qt::NoBrush);
            if (drawBorder) {
                QPen pen(border);
                pen.setWidthF(drawRect.borderWidth);
                painter.setPen(pen);
            } else {
                painter.setPen(Qt::NoPen);
            }

            const float radius = std::max(0.0f, drawRect.cornerRadius);
            if (radius > 0.0f) {
                painter.drawRoundedRect(rect, radius, radius);
            } else {
                painter.drawRect(rect);
            }
            continue;
        }

        if (std::holds_alternative<Hephaestus::HephaestusUI::UIDrawTextCommand>(command)) {
            const auto& drawText = std::get<Hephaestus::HephaestusUI::UIDrawTextCommand>(command);
            QFont font = painter.font();
            font.setFamily(GameFontFamily(drawText.fontFamily.empty() ? "Default" : drawText.fontFamily,
                                          drawText.bold, drawText.italic));
            // The game renderer sizes fonts so ascender to descender spans fontSize pixels (stb_truetype's
            // pixel height); Qt sizes the em, so scale by the font's own ascent + descent per em.
            font.setPixelSize(100);
            const QFontMetricsF reference(font, painter.device());
            const qreal emPixels = std::max(1.0f, drawText.fontSize) * 100.0 /
                                   std::max<qreal>(1.0, reference.ascent() + reference.descent());
            font.setPointSizeF(emPixels * 72.0 / std::max(1, painter.device()->logicalDpiY()));
            font.setBold(drawText.bold);
            font.setItalic(drawText.italic);
            painter.setFont(font);

            QRectF rect = toQRectF(drawText.rect);
            const int alignFlags = toQtAlign(drawText.alignment, drawText.wordWrap);
            if (!drawText.wordWrap) {
                // Qt centres the whole line box; centre the capitals instead, as the game renderer does.
                const QFontMetricsF metrics(font);
                const qreal lineCentre = (metrics.ascent() - metrics.descent()) * 0.5;
                rect.translate(0.0, lineCentre - metrics.capHeight() * 0.5);
            }

            if (drawText.shadow) {
                QColor shadowColor = toQColor(drawText.shadowColor);
                shadowColor.setAlphaF(clamp01(shadowColor.alphaF() * drawText.opacity));
                painter.setPen(shadowColor);
                QRectF shadowRect = rect.translated(drawText.shadowOffset.x, drawText.shadowOffset.y);
                painter.drawText(shadowRect, alignFlags, QString::fromStdString(drawText.text));
            }

            QColor color = toQColor(drawText.textColor);
            color.setAlphaF(clamp01(color.alphaF() * drawText.opacity));
            painter.setPen(color);
            painter.drawText(rect, alignFlags, QString::fromStdString(drawText.text));
            continue;
        }

        if (std::holds_alternative<Hephaestus::HephaestusUI::UIDrawImageCommand>(command)) {
            const auto& drawImage = std::get<Hephaestus::HephaestusUI::UIDrawImageCommand>(command);
            const QImage& image = TintedImage(drawImage.imageId, drawImage.tint);
            if (!image.isNull()) {
                const auto placement = Hephaestus::HephaestusUI::PlaceImage(
                    drawImage.rect, static_cast<float>(image.width()), static_cast<float>(image.height()),
                    drawImage.scaleMode);
                painter.save();
                painter.setOpacity(clamp01(drawImage.tint.a * drawImage.opacity));
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.drawImage(toQRectF(placement.destination), image, toQRectF(placement.source));
                painter.restore();
                continue;
            }
            // Missing image: tinted solid placeholder, as the game renderer draws it.
            QColor placeholder = toQColor(drawImage.tint);
            placeholder.setAlphaF(clamp01(placeholder.alphaF() * drawImage.opacity));
            painter.setPen(Qt::NoPen);
            painter.setBrush(placeholder);
            painter.drawRect(toQRectF(drawImage.rect));
            continue;
        }

        if (std::holds_alternative<Hephaestus::HephaestusUI::UIPushClipRectCommand>(command)) {
            const auto& pushClip = std::get<Hephaestus::HephaestusUI::UIPushClipRectCommand>(command);
            painter.save();
            painter.setClipRect(toQRectF(pushClip.rect), Qt::IntersectClip);
            ++clipDepth;
            continue;
        }

        if (std::holds_alternative<Hephaestus::HephaestusUI::UIPopClipRectCommand>(command) && clipDepth > 0) {
            painter.restore();
            --clipDepth;
            continue;
        }
    }

    while (clipDepth > 0) {
        painter.restore();
        --clipDepth;
    }
}

const QImage& RuntimeUIOverlayWidget::TintedImage(const std::string& imageId,
                                                   const Hephaestus::HephaestusUI::UIColor& tint)
{
    static const QImage none;
    if (imageId.empty()) {
        return none;
    }
    // A dynamic image (a playing video): its latest pixels, refreshed as the store's version moves on.
    if (const uint64_t version = Hephaestus::HephaestusUI::UIDynamicImages::Instance().Version(imageId); version != 0) {
        uint64_t& shown = m_dynamicVersions[imageId];
        Hephaestus::HephaestusUI::UIDynamicImages::Image pixels;
        if (Hephaestus::HephaestusUI::UIDynamicImages::Instance().CopyIfNewer(imageId, shown, pixels)) {
            QImage frame(pixels.rgba.data(), static_cast<int>(pixels.width), static_cast<int>(pixels.height),
                         static_cast<int>(pixels.width * 4), QImage::Format_RGBA8888);
            m_images[imageId] = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            const std::string prefix = imageId + "|";
            for (auto it = m_tintedImages.begin(); it != m_tintedImages.end();) {
                it = it->first.rfind(prefix, 0) == 0 ? m_tintedImages.erase(it) : std::next(it);
            }
            shown = pixels.version;
        }
    }
    auto loaded = m_images.find(imageId);
    if (loaded == m_images.end()) {
        const std::string path =
            Hephaestus::HephaestusUI::ResolveHUIImagePath(AssetManager::GetInstance().GetProjectRoot(), imageId);
        QImage image;
        if (!path.empty()) {
            image.load(QString::fromStdString(path));
        }
        loaded = m_images.emplace(imageId, image.isNull() ? QImage() : image.convertToFormat(QImage::Format_ARGB32_Premultiplied)).first;
    }
    if (loaded->second.isNull()) {
        return none;
    }
    const QColor color = toQColor(tint);
    const std::string key = imageId + "|" + std::to_string(color.rgb());
    auto tinted = m_tintedImages.find(key);
    if (tinted == m_tintedImages.end()) {
        // Multiply by the tint's colour (its alpha is applied as opacity when drawing), keeping the image's alpha.
        QImage out = loaded->second;
        QPainter tintPainter(&out);
        tintPainter.setCompositionMode(QPainter::CompositionMode_Multiply);
        tintPainter.fillRect(out.rect(), QColor(color.red(), color.green(), color.blue()));
        tintPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        tintPainter.drawImage(0, 0, loaded->second);
        tintPainter.end();
        tinted = m_tintedImages.emplace(key, std::move(out)).first;
    }
    return tinted->second;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
