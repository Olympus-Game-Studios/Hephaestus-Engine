#if HEPHAESTUS_EDITOR_QT_UI

#include "PrefabEditorManager.h"
#include "PrefabEditorWindow.h"
#include "EditorMainWindow.h"

#include <QApplication>
#include <iostream>

namespace Hephaestus {
namespace QtEditor {

PrefabEditorManager::PrefabEditorManager(EditorMainWindow* mainWindow, QObject* parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
{
}

PrefabEditorManager::~PrefabEditorManager() {
    CloseAll();
}

PrefabEditorWindow* PrefabEditorManager::OpenPrefab(const QString& absolutePath) {
    std::string key = absolutePath.toStdString();

    auto it = m_openWindows.find(key);
    if (it != m_openWindows.end() && it->second) {
        it->second->raise();
        it->second->activateWindow();
        return it->second;
    }

    auto* window = new PrefabEditorWindow(absolutePath, m_mainWindow, nullptr);

    connect(window, &PrefabEditorWindow::windowClosed,
            this, &PrefabEditorManager::onWindowClosed);
    connect(window, &PrefabEditorWindow::prefabSaved,
            this, &PrefabEditorManager::onPrefabSaved);

    m_openWindows[key] = window;
    window->show();
    window->raise();
    window->activateWindow();

    std::cout << "[PrefabEditorManager] Opened prefab window: " << key << "\n";
    return window;
}

void PrefabEditorManager::CloseAll() {
    auto copy = m_openWindows;
    for (auto& [key, window] : copy) {
        if (window) {
            window->close();
        }
    }
    m_openWindows.clear();
}

void PrefabEditorManager::CloseWindow(const QString& path) {
    std::string key = path.toStdString();
    auto it = m_openWindows.find(key);
    if (it != m_openWindows.end() && it->second) {
        it->second->close();
    }
}

bool PrefabEditorManager::HasOpenWindow(const QString& path) const {
    return m_openWindows.count(path.toStdString()) > 0;
}

PrefabEditorWindow* PrefabEditorManager::GetWindow(const QString& path) const {
    auto it = m_openWindows.find(path.toStdString());
    if (it != m_openWindows.end()) {
        return it->second;
    }
    return nullptr;
}

int PrefabEditorManager::OpenWindowCount() const {
    return static_cast<int>(m_openWindows.size());
}

void PrefabEditorManager::SetPrefabSavedNotification(PrefabSavedNotification callback) {
    m_savedNotification = std::move(callback);
}

void PrefabEditorManager::RefreshInstancesForPrefab(const std::filesystem::path& /*prefabPath*/) {
    // Implemented by the main editor when a prefab is saved.
    // The main window should re-instantiate all scene instances that reference this prefab.
}

void PrefabEditorManager::onWindowClosed(const QString& path) {
    std::string key = path.toStdString();
    m_openWindows.erase(key);
    std::cout << "[PrefabEditorManager] Closed prefab window: " << key << "\n";

    if (m_openWindows.empty()) {
        emit allWindowsClosed();
    }
}

void PrefabEditorManager::onPrefabSaved(const QString& path) {
    std::cout << "[PrefabEditorManager] Prefab saved: " << path.toStdString() << "\n";

    std::filesystem::path fsPath(path.toStdString());
    if (m_savedNotification) {
        m_savedNotification(fsPath);
    }

    emit prefabSaved(path);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
