#include "LauncherWindow.h"
#include "../QtEditor/EditorTheme.h"

#include "../../ToolsCommon/VersionInfo.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("HephaestusLauncher"));
    QApplication::setApplicationVersion(Hephaestus::Tools::VersionInfo::EngineVersion());
    QApplication::setOrganizationName(QStringLiteral("Hephaestus"));
    Hephaestus::QtEditor::Theme::Apply(app);

    QIcon launcherIcon;
    for (int size : { 16, 32, 48, 64, 128, 256 })
        launcherIcon.addFile(QStringLiteral(":/launcher/icon/%1px.png").arg(size), QSize(size, size));
    app.setWindowIcon(launcherIcon);

    Hephaestus::Launcher::LauncherWindow window;
    window.show();

    return app.exec();
}
