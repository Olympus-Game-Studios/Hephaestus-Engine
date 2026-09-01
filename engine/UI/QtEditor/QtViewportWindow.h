#pragma once

/**
 * @file QtViewportWindow.h
 * @brief Native Qt window for hosting Vulkan rendering in the Qt editor.
 *
 * This is a QWindow subclass that provides a native window handle for Vulkan
 * surface creation. It forwards input events to the engine's input system
 * and only processes input when the viewport has focus or the mouse is inside.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <QWindow>
#include <QPoint>
#include <QTimer>
#include <algorithm>
#include <functional>
#include "../../Gfx/NativeSurface.h"

namespace Hephaestus {
namespace QtEditor {

// Forward declarations for engine types
struct InputEvent {
    enum class Type {
        MousePress,
        MouseRelease,
        MouseMove,
        MouseWheel,
        KeyPress,
        KeyRelease,
        FocusIn,
        FocusOut,
        Enter,
        Leave
    };
    
    Type type;
    int key = 0;               // Qt::Key value for key events
    int button = 0;            // Qt::MouseButton for mouse events
    int modifiers = 0;         // Qt::KeyboardModifiers
    float x = 0.0f;            // Mouse x position (viewport-local)
    float y = 0.0f;            // Mouse y position (viewport-local)
    float wheelDelta = 0.0f;   // Wheel delta (vertical)
    bool accepted = false;
};

/**
 * @brief Callback type for input events.
 * 
 * The engine registers a callback to receive input events from the viewport.
 */
using InputEventCallback = std::function<void(const InputEvent&)>;

/**
 * @brief Callback type for resize events.
 */
using ResizeEventCallback = std::function<void(uint32_t width, uint32_t height)>;

/**
 * @brief Native window for hosting Vulkan viewport in Qt editor.
 *
 * Key responsibilities:
 * - Provide native HWND for Vulkan surface creation
 * - Forward input events to engine only when viewport has focus or mouse inside
 * - Request focus when clicked
 * - Emit resize signals for swapchain recreation
 */
class QtViewportWindow : public QWindow
{
    Q_OBJECT

public:
    explicit QtViewportWindow(QWindow* parent = nullptr);
    ~QtViewportWindow() override;

    /**
     * @brief Get the native surface descriptor for Vulkan.
     * @return NativeSurface with windowHandle set to HWND (on Windows).
     */
    NativeSurface GetNativeSurface() const;

    /**
     * @brief Check if the viewport window is ready for rendering.
     * @return true if the window is exposed and has valid dimensions.
     */
    bool IsReadyForRendering() const;

    /**
     * @brief Set the callback for input events.
     * @param callback Function to call when input events occur.
     */
    void SetInputEventCallback(InputEventCallback callback);

    /**
     * @brief Set the callback for resize events.
     * @param callback Function to call when the viewport is resized.
     */
    void SetResizeCallback(ResizeEventCallback callback);

    /**
     * @brief Check if the mouse is currently inside the viewport.
     */
    bool IsMouseInside() const { return m_mouseInside; }

    /**
     * @brief Check if input should be forwarded to the engine.
     * 
     * Input is forwarded when:
     * - Force forwarding is enabled (play mode), OR
     * - The viewport has keyboard focus, OR
     * - The mouse is inside the viewport (for mouse events)
     */
    bool ShouldForwardInput() const;

    /**
     * @brief Enable/disable forced input forwarding.
     * 
     * When enabled, input events are always forwarded regardless of focus.
     * Used during play mode when UI overlay might intercept events.
     */
    void SetForceInputForwarding(bool force) { m_forceForwardInput = force; }

    /**
     * @brief Capture the mouse cursor for camera / FPS look.
     *
     * Hides the cursor, grabs mouse input, and recenters so look is not limited
     * to the viewport rectangle (virtual unbounded mouse coordinates).
     */
    void CaptureCursor();

    /**
     * @brief Release the mouse cursor.
     * 
     * Shows the cursor and releases mouse grab. Restores cursor to saved position.
     */
    void ReleaseCursor();

    /**
     * @brief Check if the cursor is currently captured.
     */
    bool IsCursorCaptured() const { return m_cursorCaptured; }

    /**
     * @brief Virtual mouse position used while the cursor is captured.
     */
    float GetVirtualMouseX() const { return static_cast<float>(m_virtualMouseX); }
    float GetVirtualMouseY() const { return static_cast<float>(m_virtualMouseY); }

    /**
     * @brief Consume relative OS cursor motion while captured.
     *
     * Polling avoids lost QMouseEvent delivery when the embedded native
     * viewport is covered by a transparent Qt runtime-UI sibling.
     */
    bool ConsumeCapturedMouseDelta(float& deltaX, float& deltaY);

    /**
     * @brief Set whether warp-generated mouse moves should be ignored.
     */
    void SetIgnoreNextMouseMove(bool ignore) {
        if (ignore) {
            m_ignoreMouseMoveCount = std::max(m_ignoreMouseMoveCount, 1);
        } else {
            m_ignoreMouseMoveCount = 0;
        }
    }

    /**
     * @brief Check if next mouse move should be ignored.
     */
    bool ShouldIgnoreNextMouseMove() const { return m_ignoreMouseMoveCount > 0; }

signals:
    /**
     * @brief Emitted when the viewport becomes ready for rendering.
     * 
     * This is emitted after the first expose event when the native handle
     * is available and the window has valid dimensions.
     */
    void viewportReady();

    /**
     * @brief Emitted when the viewport is resized.
     * @param width New width in pixels.
     * @param height New height in pixels.
     */
    void viewportResized(uint32_t width, uint32_t height);

protected:
    // QWindow event overrides
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    
    // Input event overrides
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool event(QEvent* event) override;  // Handle enter/leave via generic event handler

private:
    void emitInputEvent(const InputEvent& event);
    QPoint viewportCenterLocal() const;
    void recenterCursor();
    void applyWin32CursorClip();
    void clearWin32CursorClip();
    void applyCapturedCursorVisuals();
    void restoreCapturedCursorVisuals();

    InputEventCallback m_inputCallback;
    ResizeEventCallback m_resizeCallback;
    
    bool m_exposed = false;
    bool m_mouseInside = false;
    bool m_readySent = false;
    
    // Cursor capture state
    bool m_cursorCaptured = false;
    bool m_captureSuspended = false;
    bool m_recenteringCursor = false;
    int m_ignoreMouseMoveCount = 0;
    int m_savedCursorX = 0;
    int m_savedCursorY = 0;
    float m_lastLocalX = 0.0f;
    float m_lastLocalY = 0.0f;
    double m_virtualMouseX = 0.0;
    double m_virtualMouseY = 0.0;
    
    // Force forwarding mode (for play mode)
    bool m_forceForwardInput = false;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
