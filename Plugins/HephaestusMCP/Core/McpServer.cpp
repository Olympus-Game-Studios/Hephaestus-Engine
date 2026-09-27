#include "McpServer.h"

namespace Hephaestus::Automation::Mcp {
namespace {

bool HasModernMetadata(const ParsedMessage& message)
{
    if (!message.params.is_object()) return false;
    const auto meta = message.params.find("_meta");
    return meta != message.params.end() && meta->is_object() &&
           meta->contains("io.modelcontextprotocol/protocolVersion");
}

} // namespace

McpServer::McpServer(AutomationService& service, SessionConfig config)
    : m_Modern(service, std::move(config))
#if HEPHAESTUS_MCP_LEGACY_COMPAT
    , m_Legacy(m_Modern)
#endif
{
}

McpServer::~McpServer() { Stop(); }

bool McpServer::Start(int port)
{
    return m_Transport.Start(port, [this](const HttpRequest& request) {
        return Handle(request);
    });
}

void McpServer::Stop() { m_Transport.Stop(); }

HttpResponse McpServer::Handle(const HttpRequest& request)
{
    if (request.method == "GET") return HttpResponse{405, {{"Allow", "POST"}}, {}};
    if (request.method == "DELETE") {
#if HEPHAESTUS_MCP_LEGACY_COMPAT
        const std::string sessionId = request.Header("Mcp-Session-Id");
        if (m_Legacy.IsValid(sessionId)) return m_Legacy.HandleDelete(sessionId);
#endif
        return HttpResponse{405, {{"Allow", "POST"}}, {}};
    }
    if (request.method != "POST") return HttpResponse{405, {{"Allow", "POST"}}, {}};

    ParseResult parsed = ParseClientMessage(request.body);
#if HEPHAESTUS_MCP_LEGACY_COMPAT
    if (parsed.message && !parsed.message->notification && parsed.message->method == "initialize") {
        return m_Legacy.Initialize(request, *parsed.message);
    }
    const std::string sessionId = request.Header("Mcp-Session-Id");
    if (parsed.message && m_Legacy.IsValid(sessionId) && !HasModernMetadata(*parsed.message)) {
        return m_Legacy.HandlePost(request, *parsed.message);
    }
#endif
    // Missing modern metadata is never evidence of a legacy client.
    return m_Modern.HandlePost(request);
}

} // namespace Hephaestus::Automation::Mcp
