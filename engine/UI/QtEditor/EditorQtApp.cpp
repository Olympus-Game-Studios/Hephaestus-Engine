/**
 * @file EditorQtApp.cpp
 * @brief Implementation of the Qt-based Editor UI entry point.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorQtApp.h"
#include "EditorMainWindow.h"

#include <QApplication>
#include <QDir>
#include <iostream>
#include <cassert>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace {

bool HasHeprojectExtension(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".heproject";
}

std::string ExtractProjectPathArg(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        static constexpr const char* kProjectArgPrefix = "--project=";
        if (arg.rfind(kProjectArgPrefix, 0) == 0 && arg.size() > std::char_traits<char>::length(kProjectArgPrefix)) {
            return arg.substr(std::char_traits<char>::length(kProjectArgPrefix));
        }

        if ((arg == "--project" || arg == "-p") && (i + 1 < argc)) {
            return argv[i + 1];
        }
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!arg.empty() && arg[0] == '-') {
            continue;
        }

        std::filesystem::path candidate(arg);
        if (HasHeprojectExtension(candidate)) {
            return arg;
        }
    }

    return {};
}

} // namespace

// Guard flag to prevent accidental renderer initialization in Qt editor mode.
// This is set before creating the Qt application.
static bool g_QtEditorModeActive = false;

bool IsQtEditorModeActive()
{
    return g_QtEditorModeActive;
}

int RunQtEditor(int argc, char** argv)
{
    // Mark that we're in Qt editor mode to prevent renderer initialization
    g_QtEditorModeActive = true;

    // Sanity check: ensure we haven't already started any renderer systems
    // This would indicate a bug in the argument routing logic
    assert(g_QtEditorModeActive && "Qt editor mode flag not set correctly");

    std::cout << "Starting Hephaestus in Editor Mode (Qt)" << std::endl;

    const std::string startupProjectPath = ExtractProjectPathArg(argc, argv);

    // Qt 6 enables high-DPI scaling by default. Use PassThrough rounding so
    // fractional scale factors (e.g. 125%, 150%) are honoured precisely,
    // avoiding blurry text on laptop displays.
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // Create Qt application
    QApplication app(argc, argv);

    // Normalize working directory so relative shader/resource paths are stable.
    // "Open with" launches can start with CWD set to the .heproject folder.
    const QString executableDir = QCoreApplication::applicationDirPath();
    if (!executableDir.isEmpty()) {
        if (!QDir::setCurrent(executableDir)) {
            std::cerr << "[QtEditor] Warning: Failed to set working directory to executable directory: "
                      << executableDir.toStdString() << std::endl;
        }
    }
    
    // Set application metadata
    QApplication::setApplicationName("Hephaestus Engine Editor");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("Hephaestus");

    // Create and show the main editor window
    Hephaestus::QtEditor::EditorMainWindow mainWindow;
    mainWindow.show();

    if (!startupProjectPath.empty()) {
        mainWindow.OpenProjectFromPath(QString::fromStdString(startupProjectPath));
    }

    // Enter the Qt event loop
    // This blocks until the main window is closed
    int result = app.exec();

    std::cout << "Exiting Hephaestus Editor Mode (Qt)" << std::endl;

    return result;
}

#endif // HEPHAESTUS_EDITOR_QT_UI
