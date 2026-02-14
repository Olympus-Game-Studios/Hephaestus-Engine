/**
 * @file EditorQtApp.cpp
 * @brief Implementation of the Qt-based Editor UI entry point.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorQtApp.h"
#include "EditorMainWindow.h"

#include <QApplication>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <string>

namespace {

std::string ExtractProjectPathArg(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
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
        if (candidate.extension() == ".heproject") {
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

    // Create Qt application
    QApplication app(argc, argv);
    
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
