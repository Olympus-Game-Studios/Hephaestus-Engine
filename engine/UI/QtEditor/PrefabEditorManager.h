#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QObject>
#include <QString>
#include <unordered_map>
#include <functional>
#include <filesystem>

namespace Hephaestus {
namespace QtEditor {

class PrefabEditorWindow;
class EditorMainWindow;

using PrefabSavedNotification = std::function<void(const std::filesystem::path&)>;

class PrefabEditorManager : public QObject
{
    Q_OBJECT

public:
    explicit PrefabEditorManager(EditorMainWindow* mainWindow, QObject* parent = nullptr);
    ~PrefabEditorManager() override;

    PrefabEditorWindow* OpenPrefab(const QString& absolutePath);

    void CloseAll();
    void CloseWindow(const QString& path);

    bool HasOpenWindow(const QString& path) const;
    PrefabEditorWindow* GetWindow(const QString& path) const;
    int OpenWindowCount() const;

    void SetPrefabSavedNotification(PrefabSavedNotification callback);

    void RefreshInstancesForPrefab(const std::filesystem::path& prefabPath);

signals:
    void prefabSaved(const QString& path);
    void allWindowsClosed();

private slots:
    void onWindowClosed(const QString& path);
    void onPrefabSaved(const QString& path);

private:
    EditorMainWindow* m_mainWindow = nullptr;
    std::unordered_map<std::string, PrefabEditorWindow*> m_openWindows;
    PrefabSavedNotification m_savedNotification;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
