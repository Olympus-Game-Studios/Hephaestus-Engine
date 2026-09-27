#pragma once

#include "PluginAPI/PluginFirstParty.h"
#include "McpHttpTypes.h"
#include "McpJsonRpc.h"

#include <chrono>
#include <string>
#include <vector>

namespace Hephaestus::Automation::Mcp {

struct SessionConfig {
    std::string serverName = "Hephaestus";
    std::string serverVersion = "1.0.0";
    std::string instructions = "Use semantic Hephaestus tools to inspect and automate the active host.";
    std::chrono::milliseconds requestTimeout{30000};
    bool advertiseLegacy = true;
};

class McpSession {
public:
    static constexpr std::string_view ProtocolVersion = "2026-07-28";
    static constexpr std::string_view LegacyProtocolVersion = "2025-11-25";

    McpSession(Plugins::IAutomationService& service, SessionConfig config = {});

    HttpResponse HandlePost(const HttpRequest& request);
    Json BuildToolsListResult(bool modern) const;
    Json ExecuteToolCall(const Json& id, const Json& params, bool modern,
                         int& outHttpStatus);
    Json ServerInfoMeta() const;
    const SessionConfig& Config() const noexcept { return m_Config; }

private:
    HttpResponse JsonError(const Json& id, int code, std::string message,
                           int httpStatus, Json data = Json()) const;
    HttpResponse JsonResult(const Json& id, Json result) const;
    bool ValidateModernMetadata(const ParsedMessage& message, HttpResponse& outError) const;
    bool ValidateModernHeaders(const HttpRequest& request, const ParsedMessage& message,
                               HttpResponse& outError) const;
    bool ValidateCustomHeaders(const HttpRequest& request, const ParsedMessage& message,
                               const ToolDefinition& definition, HttpResponse& outError) const;
    Json DecorateResult(Json result) const;

    Plugins::IAutomationService& m_Service;
    SessionConfig m_Config;
};

} // namespace Hephaestus::Automation::Mcp
