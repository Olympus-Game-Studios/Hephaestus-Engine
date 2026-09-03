#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QObject>
#include <QString>
#include <unordered_map>
#include <functional>
#include <filesystem>

#include "../../Assets/OpenDocumentRegistry.h"

namespace Hephaestus {
namespace QtEditor {

class CharacterEditorWindow;
class EditorMainWindow;

using CharacterSavedNotification = std::function<void(const std::filesystem::path&)>;

class CharacterEditorManager : public QObject
{
    Q_OBJECT

public:
    explicit CharacterEditorManager(EditorMainWindow* mainWindow, QObject* parent = nullptr);
    ~CharacterEditorManager() override;

    CharacterEditorWindow* OpenCharacter(const QString& absolutePath);

    void CloseAll();
    bool RequestCloseWindow(const QString& path);
    void CloseWindow(const QString& path);

    bool HasOpenWindow(const QString& path) const;
    CharacterEditorWindow* GetWindow(const QString& path) const;
    int OpenWindowCount() const;

    void SetCharacterSavedNotification(CharacterSavedNotification callback);
    void RetargetOpenWindow(const std::string& oldAbsolute, const std::string& newAbsolute);

signals:
    void characterSaved(const QString& path);
    void allWindowsClosed();

private slots:
    void onWindowClosed(const QString& path);
    void onCharacterSaved(const QString& path);

private:
    EditorMainWindow* m_mainWindow = nullptr;
    std::unordered_map<std::string, CharacterEditorWindow*> m_openWindows;
    CharacterSavedNotification m_savedNotification;
    OpenDocumentListenerId m_saveListenerId = 0;
    OpenDocumentListenerId m_retargetListenerId = 0;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif
