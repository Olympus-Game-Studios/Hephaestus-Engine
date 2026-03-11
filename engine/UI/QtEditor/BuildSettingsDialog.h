#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>

namespace Hephaestus {
namespace QtEditor {

struct BuildSettings {
    QString startScene;     // Relative path like "assets/scenes/MyScene.hpscene"
    QString windowTitle;
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;
    bool vsync = true;
};

class BuildSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BuildSettingsDialog(const QString& projectName,
                                 const QStringList& sceneFiles,
                                 QWidget* parent = nullptr);

    BuildSettings GetSettings() const;

private:
    QComboBox* m_sceneCombo = nullptr;
    QLineEdit* m_titleEdit = nullptr;
    QSpinBox*  m_widthSpin = nullptr;
    QSpinBox*  m_heightSpin = nullptr;
    QCheckBox* m_fullscreenCheck = nullptr;
    QCheckBox* m_vsyncCheck = nullptr;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
