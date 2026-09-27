/**
 * @file EditorQtApp.cpp
 * @brief Implementation of the Qt-based Editor UI entry point.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorQtApp.h"
#include "EditorMainWindow.h"
#include "../../Core/EngineLog.h"
#include "../../ToolsCommon/VersionInfo.h"

#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>
#include <iostream>
#include <cassert>
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

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

struct McpOptions
{
    bool enabled = false;
    bool portSpecified = false;
    int port = 0;
    std::string error;
};

McpOptions ParseMcpOptions(int argc, char** argv)
{
    McpOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--mcp") {
            options.enabled = true;
            continue;
        }
        static constexpr const char* kPortPrefix = "--mcp-port=";
        if (arg.rfind(kPortPrefix, 0) == 0) {
            options.enabled = true;
            options.portSpecified = true;
            const std::string value = arg.substr(std::char_traits<char>::length(kPortPrefix));
            try {
                const long parsed = std::stol(value);
                if (parsed < 0 || parsed > 65535) {
                    options.error = "MCP port must be in the range 0..65535";
                    return options;
                }
                options.port = static_cast<int>(parsed);
            } catch (const std::exception&) {
                options.error = "Invalid MCP port: " + value;
                return options;
            }
            continue;
        }
        if (arg == "--mcp-port") {
            options.enabled = true;
            options.portSpecified = true;
            if (i + 1 >= argc) {
                options.error = "Missing value for --mcp-port";
                return options;
            }
            const std::string value = argv[++i];
            try {
                const long parsed = std::stol(value);
                if (parsed < 0 || parsed > 65535) {
                    options.error = "MCP port must be in the range 0..65535";
                    return options;
                }
                options.port = static_cast<int>(parsed);
            } catch (const std::exception&) {
                options.error = "Invalid MCP port: " + value;
                return options;
            }
        }
    }
    return options;
}

void HephaestusQtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    std::string line = message.toStdString();
    if (context.file && context.file[0] != '\0') {
        line += " (";
        line += context.file;
        line += ":";
        line += std::to_string(context.line);
        line += ")";
    }

    switch (type) {
        case QtDebugMsg:
            Hephaestus::EngineLog::Debug(std::string("[Qt] ") + line);
            break;
        case QtInfoMsg:
            Hephaestus::EngineLog::Info(std::string("[Qt] ") + line);
            break;
        case QtWarningMsg:
            Hephaestus::EngineLog::Warn(std::string("[Qt] ") + line);
            break;
        case QtCriticalMsg:
            Hephaestus::EngineLog::Error(std::string("[Qt] ") + line);
            break;
        case QtFatalMsg:
            Hephaestus::EngineLog::Fatal(std::string("[Qt] ") + line);
            break;
    }
}

void LogQtScreens()
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    Hephaestus::EngineLog::Info(std::string("Qt version: ") + qVersion());
    Hephaestus::EngineLog::Info(std::string("Screen count: ") + std::to_string(screens.size()));
    QScreen* primary = QGuiApplication::primaryScreen();
    for (int i = 0; i < screens.size(); ++i) {
        QScreen* screen = screens[i];
        if (!screen) {
            continue;
        }
        const QRect geo = screen->geometry();
        std::ostringstream oss;
        oss << "Screen[" << i << "] " << screen->name().toStdString()
            << " geometry=" << geo.x() << "," << geo.y()
            << " " << geo.width() << "x" << geo.height()
            << " dpr=" << screen->devicePixelRatio();
        if (screen == primary) {
            oss << " (primary)";
        }
        Hephaestus::EngineLog::Info(oss.str());
    }
}

void ShowFatalStartupError(const std::string& message)
{
    Hephaestus::EngineLog::Fatal(message);
#ifdef _WIN32
    const std::string full = message + "\n\nSee hephaestus.log next to HephaestusEngine.exe.";
    MessageBoxA(nullptr, full.c_str(), "Hephaestus Engine - Startup Failed", MB_OK | MB_ICONERROR);
#endif
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

    qInstallMessageHandler(HephaestusQtMessageHandler);
    Hephaestus::EngineLog::Info("Starting Hephaestus in Editor Mode (Qt)");
    std::cout << "Starting Hephaestus in Editor Mode (Qt)" << std::endl;

    const std::string startupProjectPath = ExtractProjectPathArg(argc, argv);
    if (!startupProjectPath.empty()) {
        Hephaestus::EngineLog::Info(std::string("Startup project: ") + startupProjectPath);
    }

    const McpOptions mcp = ParseMcpOptions(argc, argv);
    if (!mcp.error.empty()) {
        std::cerr << "[QtEditor] Error: " << mcp.error << '\n';
        std::cerr << "Usage: HephaestusEngine [--mcp [--mcp-port <port>]]\n";
        return 2;
    }

    try {
        // Qt 6 enables high-DPI scaling by default. Use PassThrough rounding so
        // fractional scale factors (e.g. 125%, 150%) are honoured precisely,
        // avoiding blurry text on laptop displays.
        Hephaestus::EngineLog::Info("Configuring Qt high-DPI policy");
        QApplication::setHighDpiScaleFactorRoundingPolicy(
            Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

        // Create Qt application. Missing platforms/qwindows.dll fails here.
        Hephaestus::EngineLog::Info("Creating QApplication (requires platforms/qwindows.dll on Windows)");
        QApplication app(argc, argv);
        Hephaestus::EngineLog::Info("QApplication created");
        LogQtScreens();

        // Normalize working directory so relative shader/resource paths are stable.
        // "Open with" launches can start with CWD set to the .heproject folder.
        const QString executableDir = QCoreApplication::applicationDirPath();
        if (!executableDir.isEmpty()) {
            if (!QDir::setCurrent(executableDir)) {
                Hephaestus::EngineLog::Warn(std::string("Failed to set working directory to: ") +
                                            executableDir.toStdString());
                std::cerr << "[QtEditor] Warning: Failed to set working directory to executable directory: "
                          << executableDir.toStdString() << std::endl;
            } else {
                Hephaestus::EngineLog::Info(std::string("Working directory set to: ") +
                                            executableDir.toStdString());
            }
        }

        // Set application metadata from centralized config
        QApplication::setApplicationName(Hephaestus::Tools::VersionInfo::AppName());
        QApplication::setApplicationVersion(Hephaestus::Tools::VersionInfo::EngineVersion());
        QApplication::setOrganizationName(Hephaestus::Tools::VersionInfo::Organization());
        QApplication::setOrganizationDomain(Hephaestus::Tools::VersionInfo::Domain());

        Hephaestus::EngineLog::Info("Constructing EditorMainWindow (UI only, no Vulkan yet)");
        Hephaestus::Plugins::FirstPartyStartupOptions startupOptions;
        startupOptions.automationRequested = mcp.enabled;
        startupOptions.automationPortSpecified = mcp.portSpecified;
        startupOptions.automationPort = mcp.port;
        Hephaestus::QtEditor::EditorMainWindow mainWindow(nullptr, startupOptions);
        Hephaestus::EngineLog::Info("EditorMainWindow constructed, calling show()");

        mainWindow.show();
        mainWindow.raise();
        mainWindow.activateWindow();
        QApplication::processEvents();

        const QRect geo = mainWindow.frameGeometry();
        std::ostringstream winLog;
        winLog << "Main window shown: visible=" << (mainWindow.isVisible() ? "true" : "false")
               << " geometry=" << geo.x() << "," << geo.y()
               << " " << geo.width() << "x" << geo.height();
        Hephaestus::EngineLog::Info(winLog.str());

        Hephaestus::EngineLog::Info("Starting engine / Vulkan after window is visible");
        if (!mainWindow.StartEngine()) {
            Hephaestus::EngineLog::Error("StartEngine failed — window is visible, rendering will not start");
        }

        if (!startupProjectPath.empty()) {
            Hephaestus::EngineLog::Info(std::string("Opening project: ") + startupProjectPath);
            mainWindow.OpenProjectFromPath(QString::fromStdString(startupProjectPath));
        }

        Hephaestus::EngineLog::Info("Entering Qt event loop");
        const int result = app.exec();
        Hephaestus::EngineLog::Info(std::string("Qt event loop exited with code ") + std::to_string(result));
        std::cout << "Exiting Hephaestus Editor Mode (Qt)" << std::endl;
        return result;
    } catch (const std::exception& e) {
        ShowFatalStartupError(std::string("Startup exception: ") + e.what());
        return 1;
    } catch (...) {
        ShowFatalStartupError("Startup failed with an unknown exception");
        return 1;
    }
}

#endif // HEPHAESTUS_EDITOR_QT_UI
