#pragma once
#if HEPHAESTUS_EDITOR_QT_UI
#include <QDialog>
class QTableWidget;
class QTextEdit;
namespace Hephaestus::Plugins { class PluginSystem; }
namespace Hephaestus::QtEditor {
class PluginManagerDialog final : public QDialog {
public:
    explicit PluginManagerDialog(Plugins::PluginSystem&, QWidget* parent = nullptr);
private:
    void Rebuild();
    void ShowDetails(int row);
    Plugins::PluginSystem& system_;
    QTableWidget* table_ = nullptr;
    QTextEdit* details_ = nullptr;
};
}
#endif
