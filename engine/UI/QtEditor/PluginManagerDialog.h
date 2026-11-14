#pragma once
#if HEPHAESTUS_EDITOR_QT_UI
#include <QDialog>
class QTableWidget;
class QTextEdit;
namespace Hephaestus::Plugins { class PluginSystem; }
namespace Hephaestus::QtEditor {
class EditorPluginHost;
class PluginManagerDialog final : public QDialog {
public:
    /// host, when given, adds Show/Hide for each plugin's title bar icons to the list's right-click menu.
    explicit PluginManagerDialog(Plugins::PluginSystem&, EditorPluginHost* host = nullptr, QWidget* parent = nullptr);
private:
    void Rebuild();
    void ShowDetails(int row);
    void ShowContextMenu(const QPoint& position);
    Plugins::PluginSystem& system_;
    EditorPluginHost* host_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTextEdit* details_ = nullptr;
};
}
#endif
