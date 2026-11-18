#pragma once

#include <QIcon>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>

/// Shows a plugin's tool as its own top-level window (system title bar, taskbar entry), creating it on first use.
/// The window hides when closed, never keeps the editor running, and is deleted by the plugin on unload.
inline void ShowToolWindow(QPointer<QWidget>& window, const std::function<QWidget*()>& create, const QString& title,
                           const QString& iconPath, int width, int height) {
    if (!window) {
        window = create();
        window->setWindowFlag(Qt::Window);
        window->setAttribute(Qt::WA_QuitOnClose, false);
        window->setWindowTitle(title);
        window->setWindowIcon(QIcon(iconPath));
        window->resize(width, height);
    }
    window->showNormal();
    window->raise();
    window->activateWindow();
}
