#if HEPHAESTUS_EDITOR_QT_UI

#include "RuntimeUIOverlayWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <algorithm>
#include <iostream>

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
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
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
        std::cout << "[UI Overlay] Mouse press at (" << data.x << ", " << data.y << ") button=" << data.button << std::endl;
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

    // UI viewport size is in device pixels (from Vulkan swapchain extent)
    // but painter works in logical pixels. Scale DOWN to convert.
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

            painter.setBrush(fill);
            if (drawRect.borderWidth > 0.0f) {
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
            if (!drawText.fontFamily.empty()) {
                font.setFamily(QString::fromStdString(drawText.fontFamily));
            }
            font.setPointSizeF(std::max(1.0f, drawText.fontSize));
            painter.setFont(font);

            QColor color = toQColor(drawText.textColor);
            color.setAlphaF(clamp01(color.alphaF() * drawText.opacity));
            painter.setPen(color);

            const QRectF rect = toQRectF(drawText.rect);
            const int alignFlags = toQtAlign(drawText.alignment, drawText.wordWrap);
            painter.drawText(rect, alignFlags, QString::fromStdString(drawText.text));
            continue;
        }

        if (std::holds_alternative<Hephaestus::HephaestusUI::UIDrawImageCommand>(command)) {
            const auto& drawImage = std::get<Hephaestus::HephaestusUI::UIDrawImageCommand>(command);
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

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
