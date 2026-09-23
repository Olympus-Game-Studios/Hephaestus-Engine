/**
 * @file QtViewportWindow.cpp
 * @brief Implementation of the native Qt window for Vulkan viewport.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "QtViewportWindow.h"

#ifdef __APPLE__
#include "MacNativeSurface.h"
#endif

#include <QCursor>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QExposeEvent>
#include <cmath>
#include <algorithm>
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
#elif defined(__APPLE__)
    // Vulkan's macOS surface is backed by a CAMetalLayer, not the NSView.
    surface.windowHandle = GetMacMetalLayer(const_cast<QtViewportWindow*>(this));
    surface.displayHandle = nullptr;
#else
    // Linux support is provided by the GLFW runtime path for now.
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
    const float x = static_cast<float>(event->position().x());
    const float y = static_cast<float>(event->position().y());

    if (m_recenteringCursor) {
        event->accept();
        return;
    }

    if (m_ignoreMouseMoveCount > 0) {
        --m_ignoreMouseMoveCount;
        event->accept();
        return;
    }

    if (m_cursorCaptured && !m_captureSuspended) {
        const float dx = x - m_lastLocalX;
        const float dy = y - m_lastLocalY;

        // Stale pre-warp sample: do not apply, and do not adopt this position
        // as the baseline or the next event (the actual warp) undoes the look.
        const float maxStep = std::max(96.0f,
            std::min(static_cast<float>(width()), static_cast<float>(height())) * 0.25f);
        if (std::abs(dx) > maxStep || std::abs(dy) > maxStep) {
            event->accept();
            return;
        }

        m_lastLocalX = x;
        m_lastLocalY = y;

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
        }

        // Only warp when the hidden cursor is about to hit the clip edge.
        // Recentering every few dozen pixels applies the return-to-center as
        // look and makes the camera jitter back toward the middle.
        const float margin = 24.0f;
        const float w = static_cast<float>(width());
        const float h = static_cast<float>(height());
        if (x < margin || y < margin || x > w - margin || y > h - margin) {
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

QPoint QtViewportWindow::viewportCenterLocal() const
{
    return QPoint(width() / 2, height() / 2);
}

void QtViewportWindow::recenterCursor()
{
    if (m_recenteringCursor || width() <= 0 || height() <= 0) {
        return;
    }

    const QPoint center = viewportCenterLocal();
    m_ignoreMouseMoveCount = 2;
    m_recenteringCursor = true;
    // QCursor::setPos converts Qt logical coordinates to native pixels.
    // SetCursorPos does not, so feeding it mapToGlobal() on a DPI-scaled
    // display warps the cursor to the wrong place and look deltas explode.
    QCursor::setPos(mapToGlobal(center));
    m_recenteringCursor = false;
    m_lastLocalX = static_cast<float>(center.x());
    m_lastLocalY = static_cast<float>(center.y());
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

bool QtViewportWindow::ConsumeCapturedMouseDelta(float& deltaX, float& deltaY)
{
    deltaX = 0.0f;
    deltaY = 0.0f;
    if (!m_cursorCaptured || m_captureSuspended || width() <= 0 || height() <= 0) {
        return false;
    }

    const QPoint centerLocal = viewportCenterLocal();
    const QPoint centerGlobal = mapToGlobal(centerLocal);
    const QPoint cursorGlobal = QCursor::pos();
    deltaX = static_cast<float>(cursorGlobal.x() - centerGlobal.x());
    deltaY = static_cast<float>(cursorGlobal.y() - centerGlobal.y());

    if (deltaX != 0.0f || deltaY != 0.0f) {
        m_virtualMouseX += static_cast<double>(deltaX);
        m_virtualMouseY += static_cast<double>(deltaY);
        recenterCursor();
    }
    return true;
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

    const QPoint cursorPos = QCursor::pos();
    m_savedCursorX = cursorPos.x();
    m_savedCursorY = cursorPos.y();
    const QPoint localPos = mapFromGlobal(cursorPos);
    m_lastLocalX = static_cast<float>(localPos.x());
    m_lastLocalY = static_cast<float>(localPos.y());

    const QPoint center = viewportCenterLocal();
    m_virtualMouseX = static_cast<double>(center.x());
    m_virtualMouseY = static_cast<double>(center.y());

    applyCapturedCursorVisuals();
    applyWin32CursorClip();
    setMouseGrabEnabled(true);
    m_cursorCaptured = true;
    m_captureSuspended = false;

    requestActivate();
    // Captured play-mode look is sampled relative to the center once per frame.
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

    QCursor::setPos(m_savedCursorX, m_savedCursorY);
    
    m_cursorCaptured = false;
    m_captureSuspended = false;
    m_ignoreMouseMoveCount = 1;
    
    std::cout << "[QtViewport] Cursor released\n";
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
