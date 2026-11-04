#include "LauncherWindow.h"
#include "../QtEditor/EditorTheme.h"

#include "../../ToolsCommon/VersionInfo.h"

#include <QApplication>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("HephaestusLauncher"));
    QApplication::setApplicationVersion(Hephaestus::Tools::VersionInfo::EngineVersion());
    QApplication::setOrganizationName(QStringLiteral("Hephaestus"));
    Hephaestus::QtEditor::Theme::Apply(app);

    Hephaestus::Launcher::LauncherWindow window;
    window.show();

    return app.exec();
}
