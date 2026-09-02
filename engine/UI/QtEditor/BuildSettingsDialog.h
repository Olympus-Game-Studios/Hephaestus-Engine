#pragma once

#if HEPHAESTUS_EDITOR_QT_UI

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include "../../Build/BuildTypes.h"

namespace Hephaestus {
namespace QtEditor {

struct BuildSettings {
    Hephaestus::Build::BuildConfiguration configuration =
        Hephaestus::Build::BuildConfiguration::Development;
    Hephaestus::Build::BuildAction action = Hephaestus::Build::BuildAction::Package;
    QString startScene;     // Relative path like "assets/scenes/MyScene.hpscene"
    QString outputDirectory;
    QString windowTitle;
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;
    bool vsync = true;
    bool validateFirst = true;
    bool openOutputFolder = true;
    bool createZip = false;
    bool reportUnusedAssets = false;
};

class BuildSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BuildSettingsDialog(const QString& projectName,
                                 const QStringList& sceneFiles,
                                 const QString& startupScene,
                                 const QString& outputDirectory,
                                 const BuildSettings& defaults,
                                 QWidget* parent = nullptr);

    BuildSettings GetSettings() const;

signals:
    void openStartupSceneRequested(const QString& path);
    void revealStartupSceneRequested(const QString& path);

private:
    void chooseOutputDirectory();
    void acceptAction(Hephaestus::Build::BuildAction action);

    QComboBox* m_configurationCombo = nullptr;
    QComboBox* m_sceneCombo = nullptr;
    QLineEdit* m_outputEdit = nullptr;
    QLineEdit* m_titleEdit = nullptr;
    QSpinBox*  m_widthSpin = nullptr;
    QSpinBox*  m_heightSpin = nullptr;
    QCheckBox* m_fullscreenCheck = nullptr;
    QCheckBox* m_vsyncCheck = nullptr;
    QCheckBox* m_validateCheck = nullptr;
    QCheckBox* m_openOutputCheck = nullptr;
    QCheckBox* m_createZipCheck = nullptr;
    QCheckBox* m_unusedAssetsCheck = nullptr;
    Hephaestus::Build::BuildAction m_action = Hephaestus::Build::BuildAction::Package;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
