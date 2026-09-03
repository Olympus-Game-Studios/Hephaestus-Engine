#if HEPHAESTUS_EDITOR_QT_UI

#include "CharacterEditorManager.h"
#include "CharacterEditorWindow.h"
#include "EditorMainWindow.h"
#include "EditorAssetServices.h"

#include <iostream>

namespace Hephaestus {
namespace QtEditor {

CharacterEditorManager::CharacterEditorManager(EditorMainWindow* mainWindow, QObject* parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
{
    auto& docs = EditorAssetServices::Instance().Documents();
    m_saveListenerId = docs.AddSaveListener([this](const OpenDocumentInfo& info, std::string* errorOut) -> bool {
        if (info.kind != DocumentKind::Character) {
            return false;
        }
        CharacterEditorWindow* window = GetWindow(QString::fromStdString(info.absolutePath));
        if (!window) {
            return false;
        }
        std::string error;
        if (!window->SaveDocument(error)) {
            if (errorOut) {
                *errorOut = error;
            }
            return false;
        }
        return true;
    });
    m_retargetListenerId = docs.AddRetargetListener([this](const OpenDocumentInfo& before, const OpenDocumentInfo& after) {
        if (after.kind != DocumentKind::Character) {
            return;
        }
        RetargetOpenWindow(before.absolutePath, after.absolutePath);
    });
}

CharacterEditorManager::~CharacterEditorManager() {
    auto& docs = EditorAssetServices::Instance().Documents();
    if (m_saveListenerId != 0) {
        docs.RemoveSaveListener(m_saveListenerId);
        m_saveListenerId = 0;
    }
    if (m_retargetListenerId != 0) {
        docs.RemoveRetargetListener(m_retargetListenerId);
        m_retargetListenerId = 0;
    }
    CloseAll();
}

CharacterEditorWindow* CharacterEditorManager::OpenCharacter(const QString& absolutePath) {
    std::string key = absolutePath.toStdString();
    auto it = m_openWindows.find(key);
    if (it != m_openWindows.end() && it->second) {
        it->second->raise();
        it->second->activateWindow();
        return it->second;
    }

    auto* window = new CharacterEditorWindow(absolutePath, m_mainWindow, nullptr);
    connect(window, &CharacterEditorWindow::windowClosed, this, &CharacterEditorManager::onWindowClosed);
    connect(window, &CharacterEditorWindow::characterSaved, this, &CharacterEditorManager::onCharacterSaved);
    m_openWindows[key] = window;
    window->show();
    window->raise();
    window->activateWindow();
    return window;
}

void CharacterEditorManager::CloseAll() {
    auto copy = m_openWindows;
    for (auto& [key, window] : copy) {
        if (window) {
            window->close();
        }
    }
    m_openWindows.clear();
}

bool CharacterEditorManager::RequestCloseWindow(const QString& path) {
    CharacterEditorWindow* window = GetWindow(path);
    if (!window) {
        return true;
    }
    window->close();
    return !HasOpenWindow(path);
}

void CharacterEditorManager::CloseWindow(const QString& path) {
    std::string key = path.toStdString();
    auto it = m_openWindows.find(key);
    if (it != m_openWindows.end() && it->second) {
        it->second->close();
    }
}

bool CharacterEditorManager::HasOpenWindow(const QString& path) const {
    return m_openWindows.count(path.toStdString()) > 0;
}

CharacterEditorWindow* CharacterEditorManager::GetWindow(const QString& path) const {
    auto it = m_openWindows.find(path.toStdString());
    if (it != m_openWindows.end()) {
        return it->second;
    }
    return nullptr;
}

int CharacterEditorManager::OpenWindowCount() const {
    return static_cast<int>(m_openWindows.size());
}

void CharacterEditorManager::SetCharacterSavedNotification(CharacterSavedNotification callback) {
    m_savedNotification = std::move(callback);
}

void CharacterEditorManager::RetargetOpenWindow(const std::string& oldAbsolute, const std::string& newAbsolute) {
    auto it = m_openWindows.find(oldAbsolute);
    if (it == m_openWindows.end() || !it->second) {
        it = m_openWindows.find(QString::fromStdString(oldAbsolute).toStdString());
    }
    CharacterEditorWindow* window = nullptr;
    std::string oldKey;
    for (auto& [key, openWindow] : m_openWindows) {
        if (openWindow && (key == oldAbsolute || openWindow->absolutePath() == QString::fromStdString(oldAbsolute))) {
            window = openWindow;
            oldKey = key;
            break;
        }
    }
    if (!window) {
        return;
    }
    m_openWindows.erase(oldKey);
    window->RetargetPath(QString::fromStdString(newAbsolute));
    m_openWindows[newAbsolute] = window;
}

void CharacterEditorManager::onWindowClosed(const QString& path) {
    m_openWindows.erase(path.toStdString());
    if (m_openWindows.empty()) {
        emit allWindowsClosed();
    }
}

void CharacterEditorManager::onCharacterSaved(const QString& path) {
    if (m_savedNotification) {
        m_savedNotification(std::filesystem::path(path.toStdString()));
    }
    emit characterSaved(path);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
