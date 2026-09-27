/**
 * @file HephaestusMcpPlugin.cpp
 * @brief Editor plugin that exposes Hephaestus automation over MCP.
 *
 * The plugin is a pure consumer of the internal first-party Automation API. It
 * never touches the scene, renderer, physics, asset or project internals: every
 * automation request is dispatched through Plugins::IAutomationService, which
 * the engine owns and pumps on the main thread.
 */

#include "PluginAPI/PluginAPI.h"
#include "PluginAPI/PluginFirstParty.h"
#include "HephaestusMCP/Core/McpServer.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Hephaestus::Plugins;

namespace {

constexpr const char* kSettingEnabled = "enabled";
constexpr const char* kSettingPort = "port";
constexpr const char* kSettingAutoStart = "autoStart";
constexpr int kDefaultPort = 8765;

} // namespace

class HephaestusMcpPlugin final : public IHephaestusPlugin {
public:
    // Provided by the host before OnLoad through HP_SetFirstPartyContext.
    void SetFirstPartyContext(IFirstPartyPluginContext* context) { firstParty_ = context; }

    bool OnLoad(IPluginContext& context) override
    {
        context_ = &context;
        if (!firstParty_ ||
            firstParty_->GetFirstPartyApiVersion() != HP_FIRST_PARTY_API_VERSION) {
            context_ = nullptr;
            return false;
        }
        automation_ = firstParty_->GetAutomationService();
        if (!automation_) {
            context_ = nullptr;
            return false;
        }

        bool ok = true;
        ok &= context.Settings().Register({kSettingEnabled, "MCP Server Enabled",
            "Whether the MCP endpoint should be running.", "false", SettingScope::Editor});
        ok &= context.Settings().Register({kSettingPort, "MCP Server Port",
            "Loopback port for the MCP endpoint (0 picks an ephemeral port).", "8765",
            SettingScope::Editor});
        ok &= context.Settings().Register({kSettingAutoStart, "Start MCP Server With Editor",
            "Start the MCP endpoint automatically when the editor launches.", "false",
            SettingScope::Editor});

        ok &= RegisterCommand(context, "mcp.start", "Start MCP Server", &StartCommand);
        ok &= RegisterCommand(context, "mcp.stop", "Stop MCP Server", &StopCommand);
        ok &= RegisterCommand(context, "mcp.restart", "Restart MCP Server", &RestartCommand);
        ok &= RegisterCommand(context, "mcp.copyConfig", "Copy MCP Configuration", &CopyCommand);
        ok &= RegisterCommand(context, "mcp.openLog", "Open MCP Log", &LogCommand);

        ok &= context.UI().RegisterPanel({"mcp.status", "MCP Server", "", 2, &CreatePanel, this});
        ok &= context.UI().RegisterMenuAction({"Tools/MCP Server", "mcp.start", "server"});
        ok &= context.UI().RegisterMenuAction({"Tools/MCP Server", "mcp.stop", "server"});
        ok &= context.UI().RegisterMenuAction({"Tools/MCP Server", "mcp.restart", "server"});
        ok &= context.UI().RegisterMenuAction({"Tools/MCP Server", "mcp.copyConfig", "config"});
        ok &= context.UI().RegisterMenuAction({"Tools/MCP Server", "mcp.openLog", "config"});

        server_ = std::make_unique<Hephaestus::Automation::Mcp::McpServer>(*automation_);
        if (ShouldAutoStart()) {
            StartServer();
        } else {
            Log("MCP server is stopped. Use Tools > MCP Server > Start to begin.");
        }
        RefreshPanel();
        return ok;
    }

    void OnUnload() override
    {
        if (refreshTimer_) {
            refreshTimer_->stop();
            refreshTimer_.reset();
        }
        if (server_) {
            server_->Stop();
            server_.reset();
        }
        panel_ = nullptr;
        statusLabel_ = nullptr;
        endpointLabel_ = nullptr;
        portLabel_ = nullptr;
        clientsLabel_ = nullptr;
        buttonsEnabled_.clear();
        automation_ = nullptr;
        context_ = nullptr;
        firstParty_ = nullptr;
    }

private:
    // ── Command plumbing ───────────────────────────────────────────────────
    bool RegisterCommand(IPluginContext& context, const char* id, const char* name,
                         PluginCallback callback)
    {
        return context.Commands().Register({id, name, "Hephaestus MCP server", "", callback, this});
    }

    static void StartCommand(void* data)
    {
        auto* self = static_cast<HephaestusMcpPlugin*>(data);
        self->StartServer();
        self->RefreshPanel();
    }
    static void StopCommand(void* data)
    {
        auto* self = static_cast<HephaestusMcpPlugin*>(data);
        self->StopServer();
        self->RefreshPanel();
    }
    static void RestartCommand(void* data)
    {
        auto* self = static_cast<HephaestusMcpPlugin*>(data);
        self->StopServer();
        self->StartServer();
        self->RefreshPanel();
    }
    static void CopyCommand(void* data)
    {
        auto* self = static_cast<HephaestusMcpPlugin*>(data);
        self->CopyConfiguration();
    }
    static void LogCommand(void* data)
    {
        auto* self = static_cast<HephaestusMcpPlugin*>(data);
        self->ShowLogDialog();
    }

    // ── Server lifecycle ───────────────────────────────────────────────────
    bool ShouldAutoStart() const
    {
        if (firstParty_ && firstParty_->GetStartupOptions().automationRequested) return true;
        return SettingBool(kSettingAutoStart, false);
    }

    int DesiredPort() const
    {
        if (firstParty_ && firstParty_->GetStartupOptions().automationPortSpecified) {
            return firstParty_->GetStartupOptions().automationPort;
        }
        return SettingInt(kSettingPort, kDefaultPort);
    }

    void StartServer()
    {
        if (!server_) return;
        if (server_->IsRunning()) return;
        if (!server_->Start(DesiredPort())) {
            Log("Failed to start MCP server on port " + std::to_string(DesiredPort()));
            return;
        }
        SetSetting(kSettingEnabled, true);
        Log("MCP server listening at " + Endpoint());
    }

    void StopServer()
    {
        if (!server_) return;
        server_->Stop();
        SetSetting(kSettingEnabled, false);
        Log("MCP server stopped");
    }

    std::string Endpoint() const
    {
        if (!server_ || !server_->IsRunning()) return {};
        return "http://127.0.0.1:" + std::to_string(server_->Port()) + "/mcp";
    }

    void CopyConfiguration()
    {
        const std::string url = server_ && server_->IsRunning()
            ? Endpoint()
            : "http://127.0.0.1:" + std::to_string(DesiredPort()) + "/mcp";
        const std::string config =
            "{\n  \"mcpServers\": {\n    \"hephaestus\": {\n      \"url\": \"" + url +
            "\"\n    }\n  }\n}\n";
        if (auto* clipboard = QApplication::clipboard()) {
            clipboard->setText(QString::fromStdString(config));
        }
        Log("Copied MCP configuration for " + url);
        RefreshPanel();
    }

    // ── Settings helpers ───────────────────────────────────────────────────
    std::string SettingValue(const char* id) const
    {
        if (!context_) return {};
        const char* value = context_->Settings().Get(id);
        return value ? std::string(value) : std::string{};
    }

    bool SettingBool(const char* id, bool fallback) const
    {
        const std::string value = SettingValue(id);
        if (value.empty()) return fallback;
        return value == "true" || value == "1" || value == "yes";
    }

    int SettingInt(const char* id, int fallback) const
    {
        const std::string value = SettingValue(id);
        if (value.empty()) return fallback;
        try {
            return std::stoi(value);
        } catch (...) {
            return fallback;
        }
    }

    void SetSetting(const char* id, const std::string& value)
    {
        if (context_) context_->Settings().Set(id, value.c_str());
    }
    void SetSetting(const char* id, bool value)
    {
        SetSetting(id, value ? std::string("true") : std::string("false"));
    }

    // ── Panel ──────────────────────────────────────────────────────────────
    static QWidget* CreatePanel(QWidget* parent, void* data)
    {
        auto* self = static_cast<HephaestusMcpPlugin*>(data);
        auto* widget = new QWidget(parent);
        auto* layout = new QVBoxLayout(widget);

        self->statusLabel_ = new QLabel(widget);
        self->endpointLabel_ = new QLabel(widget);
        self->portLabel_ = new QLabel(widget);
        self->clientsLabel_ = new QLabel(widget);
        for (QLabel* label : {self->statusLabel_, self->endpointLabel_, self->portLabel_,
                              self->clientsLabel_}) {
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            layout->addWidget(label);
        }

        auto* buttonRow = new QHBoxLayout();
        const std::pair<const char*, PluginCallback> actions[] = {
            {"Start", &StartCommand}, {"Stop", &StopCommand}, {"Restart", &RestartCommand}};
        for (const auto& [label, callback] : actions) {
            auto* button = new QPushButton(QString::fromUtf8(label), widget);
            const auto userCallback = callback;
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
            QObject::connect(button, &QPushButton::clicked, widget,
                             [self, userCallback]() { userCallback(self); });
#endif
            self->buttonsEnabled_.push_back(button);
            buttonRow->addWidget(button);
        }
        layout->addLayout(buttonRow);

        auto* copyButton = new QPushButton(QObject::tr("Copy Configuration"), widget);
        QObject::connect(copyButton, &QPushButton::clicked, widget,
                         [self]() { self->CopyConfiguration(); });
        layout->addWidget(copyButton);

        auto* notice = new QLabel(
            QObject::tr("Localhost only: the MCP endpoint binds 127.0.0.1 and never a remote "
                        "interface."),
            widget);
        notice->setWordWrap(true);
        layout->addWidget(notice);
        layout->addStretch();

        self->panel_ = widget;
        self->EnsureRefreshTimer();
        self->RefreshPanel();
        return widget;
    }

    void EnsureRefreshTimer()
    {
        if (refreshTimer_) return;
        refreshTimer_ = std::make_unique<QTimer>();
        refreshTimer_->setInterval(1000);
        QObject::connect(refreshTimer_.get(), &QTimer::timeout, refreshTimer_.get(),
                         [this]() { RefreshPanel(); });
        refreshTimer_->start();
    }

    void RefreshPanel()
    {
        if (!panel_) return;
        const bool running = server_ && server_->IsRunning();
        if (statusLabel_) {
            statusLabel_->setText(QObject::tr("Status: %1").arg(running ? "Running" : "Stopped"));
        }
        if (endpointLabel_) {
            endpointLabel_->setText(QObject::tr("Endpoint: %1")
                                        .arg(running ? QString::fromStdString(Endpoint())
                                                     : QStringLiteral("—")));
        }
        if (portLabel_) {
            const int port = running ? server_->Port() : DesiredPort();
            portLabel_->setText(QObject::tr("Port: %1").arg(port));
        }
        if (clientsLabel_) {
            clientsLabel_->setText(QObject::tr("Connected clients: %1")
                                       .arg(running ? QStringLiteral("—") : QStringLiteral("0")));
        }
        for (QPushButton* button : buttonsEnabled_) {
            if (button) button->setEnabled(true);
        }
    }

    // ── Log ────────────────────────────────────────────────────────────────
    void Log(std::string message)
    {
        messages_.push_back(std::move(message));
        if (messages_.size() > 200) {
            messages_.erase(messages_.begin());
        }
    }

    void ShowLogDialog()
    {
        QDialog dialog(panel_ ? panel_->window() : nullptr);
        dialog.setWindowTitle(QObject::tr("MCP Log"));
        dialog.resize(560, 320);
        auto* layout = new QVBoxLayout(&dialog);
        auto* text = new QPlainTextEdit(&dialog);
        text->setReadOnly(true);
        QString contents;
        for (const std::string& line : messages_) {
            contents += QString::fromStdString(line) + QLatin1Char('\n');
        }
        text->setPlainText(contents);
        layout->addWidget(text);
        dialog.exec();
    }

    IFirstPartyPluginContext* firstParty_ = nullptr;
    IPluginContext* context_ = nullptr;
    IAutomationService* automation_ = nullptr;
    std::unique_ptr<Hephaestus::Automation::Mcp::McpServer> server_;
    std::vector<std::string> messages_;

    QPointer<QWidget> panel_;
    QPointer<QLabel> statusLabel_;
    QPointer<QLabel> endpointLabel_;
    QPointer<QLabel> portLabel_;
    QPointer<QLabel> clientsLabel_;
    std::vector<QPointer<QPushButton>> buttonsEnabled_;
    std::unique_ptr<QTimer> refreshTimer_;
};

HP_DECLARE_PLUGIN(HephaestusMcpPlugin)
HP_DECLARE_FIRST_PARTY_SET_CONTEXT(HephaestusMcpPlugin)
