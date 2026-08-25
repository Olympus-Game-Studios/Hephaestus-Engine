/**
 * @file QtViewportWindow.cpp
 * @brief Implementation of the native Qt window for Vulkan viewport.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "QtViewportWindow.h"

#include <QCursor>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QExposeEvent>
#include <cmath>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Hephaestus {
namespace QtEditor {

QtViewportWindow::QtViewportWindow(QWindow* parent)
    : QWindow(parent)
{
    // Ensure we get a native window handle
    // RasterSurface is sufficient - we don't need Qt's OpenGL/Vulkan integration
    setSurfaceType(QWindow::RasterSurface);
    
    // Set initial size
    resize(800, 600);
    
    // Enable strong focus so we can receive keyboard input
    // Note: QWindow doesn't have setFocusPolicy; handled by requestActivate on click
    
    std::cout << "[QtViewport] Window created\n";
}

QtViewportWindow::~QtViewportWindow()
{
    std::cout << "[QtViewport] Window destroyed\n";
}

NativeSurface QtViewportWindow::GetNativeSurface() const
{
    NativeSurface surface;
    
#ifdef _WIN32
    // On Windows, winId() returns the HWND
    surface.windowHandle = reinterpret_cast<void*>(const_cast<QtViewportWindow*>(this)->winId());
    surface.displayHandle = nullptr; // Not used on Windows
#else
    // TODO: Support other platforms
    surface.windowHandle = nullptr;
    surface.displayHandle = nullptr;
#endif
    
    // Use physical pixel dimensions for Vulkan (multiply by DPI scale factor)
    qreal dpr = devicePixelRatio();
    surface.width = static_cast<uint32_t>(width() * dpr);
    surface.height = static_cast<uint32_t>(height() * dpr);
    
    return surface;
}

bool QtViewportWindow::IsReadyForRendering() const
{
    return m_exposed && width() > 0 && height() > 0;
}

void QtViewportWindow::SetInputEventCallback(InputEventCallback callback)
{
    m_inputCallback = std::move(callback);
}

void QtViewportWindow::SetResizeCallback(ResizeEventCallback callback)
{
    m_resizeCallback = std::move(callback);
}

bool QtViewportWindow::ShouldForwardInput() const
{
    return m_forceForwardInput || isActive() || m_mouseInside;
}

void QtViewportWindow::exposeEvent(QExposeEvent* event)
{
    QWindow::exposeEvent(event);
    
    bool wasExposed = m_exposed;
    m_exposed = isExposed();
    
    if (m_exposed && !wasExposed) {
        std::cout << "[QtViewport] Window exposed (" << width() << "x" << height() << ")\n";
        
        // Emit ready signal only once
        if (!m_readySent && width() > 0 && height() > 0) {
            m_readySent = true;
            emit viewportReady();
        }
    }
}

void QtViewportWindow::resizeEvent(QResizeEvent* event)
{
    QWindow::resizeEvent(event);
    
    // Use physical pixel dimensions for Vulkan (multiply by DPI scale factor)
    qreal dpr = devicePixelRatio();
    uint32_t w = static_cast<uint32_t>(event->size().width() * dpr);
    uint32_t h = static_cast<uint32_t>(event->size().height() * dpr);
    
    std::cout << "[QtViewport] Resized to " << w << "x" << h << " (DPR=" << dpr << ")\n";
    
    // Emit resize signal
    emit viewportResized(w, h);
    
    // Call resize callback if set
    if (m_resizeCallback && w > 0 && h > 0) {
        m_resizeCallback(w, h);
    }

    if (m_cursorCaptured && !m_captureSuspended) {
        applyWin32CursorClip();
        recenterCursor();
    }
}

void QtViewportWindow::emitInputEvent(const InputEvent& event)
{
    if (m_inputCallback) {
        m_inputCallback(event);
    }
}

void QtViewportWindow::mousePressEvent(QMouseEvent* event)
{
    // Request focus when clicked
    if (!isActive()) {
        requestActivate();
    }
    
    if (ShouldForwardInput()) {
        InputEvent ie;
        ie.type = InputEvent::Type::MousePress;
        ie.button = static_cast<int>(event->button());
        ie.modifiers = static_cast<int>(event->modifiers());
        ie.x = (m_cursorCaptured && !m_captureSuspended)
            ? static_cast<float>(m_virtualMouseX)
            : static_cast<float>(event->position().x());
        ie.y = (m_cursorCaptured && !m_captureSuspended)
            ? static_cast<float>(m_virtualMouseY)
            : static_cast<float>(event->position().y());
        emitInputEvent(ie);
    }
    
    event->accept();
}

void QtViewportWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (ShouldForwardInput()) {
        InputEvent ie;
        ie.type = InputEvent::Type::MouseRelease;
        ie.button = static_cast<int>(event->button());
        ie.modifiers = static_cast<int>(event->modifiers());
        ie.x = (m_cursorCaptured && !m_captureSuspended)
            ? static_cast<float>(m_virtualMouseX)
            : static_cast<float>(event->position().x());
        ie.y = (m_cursorCaptured && !m_captureSuspended)
            ? static_cast<float>(m_virtualMouseY)
            : static_cast<float>(event->position().y());
        emitInputEvent(ie);
    }
    
    event->accept();
}

void QtViewportWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_ignoreNextMouseMove) {
        m_ignoreNextMouseMove = false;
        event->accept();
        return;
    }

    if (m_cursorCaptured && !m_captureSuspended) {
        const float centerX = static_cast<float>(width()) * 0.5f;
        const float centerY = static_cast<float>(height()) * 0.5f;
        const float dx = static_cast<float>(event->position().x()) - centerX;
        const float dy = static_cast<float>(event->position().y()) - centerY;
        if (std::abs(dx) >= 0.5f || std::abs(dy) >= 0.5f) {
            m_virtualMouseX += static_cast<double>(dx);
            m_virtualMouseY += static_cast<double>(dy);

            if (ShouldForwardInput()) {
                InputEvent ie;
                ie.type = InputEvent::Type::MouseMove;
                ie.button = static_cast<int>(event->buttons());
                ie.modifiers = static_cast<int>(event->modifiers());
                ie.x = static_cast<float>(m_virtualMouseX);
                ie.y = static_cast<float>(m_virtualMouseY);
                emitInputEvent(ie);
            }

            recenterCursor();
        }

        event->accept();
        return;
    }

    if (ShouldForwardInput()) {
        InputEvent ie;
        ie.type = InputEvent::Type::MouseMove;
        ie.button = static_cast<int>(event->buttons());
        ie.modifiers = static_cast<int>(event->modifiers());
        ie.x = static_cast<float>(event->position().x());
        ie.y = static_cast<float>(event->position().y());
        emitInputEvent(ie);
    }
    
    event->accept();
}

void QtViewportWindow::wheelEvent(QWheelEvent* event)
{
    if (ShouldForwardInput()) {
        InputEvent ie;
        ie.type = InputEvent::Type::MouseWheel;
        ie.button = static_cast<int>(event->buttons());  // Pass held buttons
        ie.modifiers = static_cast<int>(event->modifiers());
        ie.x = static_cast<float>(event->position().x());
        ie.y = static_cast<float>(event->position().y());
        ie.wheelDelta = static_cast<float>(event->angleDelta().y()) / 120.0f;
        emitInputEvent(ie);
    }
    
    event->accept();
}

void QtViewportWindow::keyPressEvent(QKeyEvent* event)
{
    // Only forward key events when we have focus
    if (isActive()) {
        InputEvent ie;
        ie.type = InputEvent::Type::KeyPress;
        ie.key = event->key();
        ie.modifiers = static_cast<int>(event->modifiers());
        emitInputEvent(ie);
    }
    
    event->accept();
}

void QtViewportWindow::keyReleaseEvent(QKeyEvent* event)
{
    // Only forward key events when we have focus
    if (isActive()) {
        InputEvent ie;
        ie.type = InputEvent::Type::KeyRelease;
        ie.key = event->key();
        ie.modifiers = static_cast<int>(event->modifiers());
        emitInputEvent(ie);
    }
    
    event->accept();
}

void QtViewportWindow::focusInEvent(QFocusEvent* event)
{
    QWindow::focusInEvent(event);

    if (m_cursorCaptured && m_captureSuspended) {
        m_captureSuspended = false;
        applyCapturedCursorVisuals();
        setMouseGrabEnabled(true);
        applyWin32CursorClip();
        recenterCursor();
    }
    
    InputEvent ie;
    ie.type = InputEvent::Type::FocusIn;
    emitInputEvent(ie);
    
    std::cout << "[QtViewport] Focus in\n";
}

void QtViewportWindow::focusOutEvent(QFocusEvent* event)
{
    QWindow::focusOutEvent(event);

    if (m_cursorCaptured && !m_captureSuspended) {
        m_captureSuspended = true;
        setMouseGrabEnabled(false);
        clearWin32CursorClip();
        restoreCapturedCursorVisuals();
    }
    
    InputEvent ie;
    ie.type = InputEvent::Type::FocusOut;
    emitInputEvent(ie);
    
    std::cout << "[QtViewport] Focus out\n";
}

bool QtViewportWindow::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::Enter:
        m_mouseInside = true;
        {
            InputEvent ie;
            ie.type = InputEvent::Type::Enter;
            emitInputEvent(ie);
        }
        return true;
        
    case QEvent::Leave:
        m_mouseInside = false;
        {
            InputEvent ie;
            ie.type = InputEvent::Type::Leave;
            emitInputEvent(ie);
        }
        return true;
        
    default:
        return QWindow::event(event);
    }
}

void QtViewportWindow::recenterCursor()
{
    if (width() <= 0 || height() <= 0) {
        return;
    }

#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(winId());
    RECT rect;
    GetClientRect(hwnd, &rect);
    POINT center = {
        (rect.left + rect.right) / 2,
        (rect.top + rect.bottom) / 2
    };
    ClientToScreen(hwnd, &center);
    SetCursorPos(center.x, center.y);
#else
    QCursor::setPos(mapToGlobal(QPoint(width() / 2, height() / 2)));
#endif
}

void QtViewportWindow::applyWin32CursorClip()
{
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(winId());
    RECT rect;
    GetClientRect(hwnd, &rect);
    POINT topLeft = { rect.left, rect.top };
    POINT bottomRight = { rect.right, rect.bottom };
    ClientToScreen(hwnd, &topLeft);
    ClientToScreen(hwnd, &bottomRight);
    rect.left = topLeft.x;
    rect.top = topLeft.y;
    rect.right = bottomRight.x;
    rect.bottom = bottomRight.y;
    ClipCursor(&rect);
#endif
}

void QtViewportWindow::clearWin32CursorClip()
{
#ifdef _WIN32
    ClipCursor(nullptr);
#endif
}

void QtViewportWindow::applyCapturedCursorVisuals()
{
    setCursor(Qt::BlankCursor);
#ifdef _WIN32
    ShowCursor(FALSE);
#endif
}

void QtViewportWindow::restoreCapturedCursorVisuals()
{
    unsetCursor();
#ifdef _WIN32
    ShowCursor(TRUE);
#endif
}

void QtViewportWindow::CaptureCursor()
{
    if (m_cursorCaptured) {
        if (m_captureSuspended && isActive()) {
            m_captureSuspended = false;
            applyCapturedCursorVisuals();
            setMouseGrabEnabled(true);
            applyWin32CursorClip();
            recenterCursor();
        }
        return;
    }

#ifdef _WIN32
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    m_savedCursorX = cursorPos.x;
    m_savedCursorY = cursorPos.y;
#else
    const QPoint cursorPos = QCursor::pos();
    m_savedCursorX = cursorPos.x();
    m_savedCursorY = cursorPos.y();
#endif

    m_virtualMouseX = static_cast<double>(width()) * 0.5;
    m_virtualMouseY = static_cast<double>(height()) * 0.5;

    applyCapturedCursorVisuals();
    applyWin32CursorClip();
    setMouseGrabEnabled(true);
    m_cursorCaptured = true;
    m_captureSuspended = false;

    requestActivate();
    recenterCursor();
    
    std::cout << "[QtViewport] Cursor captured\n";
}

void QtViewportWindow::ReleaseCursor()
{
    if (!m_cursorCaptured) {
        return;
    }

    if (!m_captureSuspended) {
        setMouseGrabEnabled(false);
        clearWin32CursorClip();
        restoreCapturedCursorVisuals();
    }

#ifdef _WIN32
    SetCursorPos(m_savedCursorX, m_savedCursorY);
#else
    QCursor::setPos(m_savedCursorX, m_savedCursorY);
#endif
    
    m_cursorCaptured = false;
    m_captureSuspended = false;
    m_ignoreNextMouseMove = true;
    
    std::cout << "[QtViewport] Cursor released\n";
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
