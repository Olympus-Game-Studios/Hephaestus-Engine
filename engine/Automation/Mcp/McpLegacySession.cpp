#include "McpLegacySession.h"

#include <array>
#include <iomanip>
#include <random>
#include <sstream>

namespace Hephaestus::Automation::Mcp {

McpLegacySession::McpLegacySession(McpSession& modern) : m_Modern(modern) {}

std::string McpLegacySession::MintSessionId()
{
    std::array<unsigned char, 16> bytes{};
    std::random_device random;
    for (auto& byte : bytes) byte = static_cast<unsigned char>(random());
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) stream << std::setw(2) << static_cast<int>(byte);
    return stream.str();
}

bool McpLegacySession::IsValid(std::string_view sessionId) const
{
    if (sessionId.empty()) return false;
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Sessions.contains(std::string(sessionId));
}

HttpResponse McpLegacySession::Initialize(const HttpRequest&, const ParsedMessage& message)
{
    const std::string sessionId = MintSessionId();
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Sessions.insert(sessionId);
    }
    Json result = {{"protocolVersion", McpSession::LegacyProtocolVersion},
                   {"capabilities", {{"tools", {{"listChanged", false}}}}},
                   {"serverInfo", {{"name", m_Modern.Config().serverName},
                                   {"version", m_Modern.Config().serverVersion}}},
                   {"instructions", m_Modern.Config().instructions}};
    HttpResponse response;
    response.status = 200;
    response.SetHeader("Content-Type", "application/json");
    response.SetHeader("Mcp-Session-Id", sessionId);
    response.body = MakeResult(message.id, std::move(result)).dump();
    return response;
}

HttpResponse McpLegacySession::HandlePost(const HttpRequest&, const ParsedMessage& message)
{
    if (message.notification) return HttpResponse{202, {}, {}};
    HttpResponse response;
    response.SetHeader("Content-Type", "application/json");
    if (message.method == "tools/list") {
        response.status = 200;
        response.body = MakeResult(message.id, m_Modern.BuildToolsListResult(false)).dump();
        return response;
    }
    if (message.method == "tools/call") {
        int status = 200;
        response.body = m_Modern.ExecuteToolCall(message.id, message.params, false, status).dump();
        response.status = status;
        return response;
    }
    response.status = 404;
    response.body = MakeError(message.id, MethodNotFound, "Method not found").dump();
    return response;
}

HttpResponse McpLegacySession::HandleDelete(std::string_view sessionId)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (sessionId.empty() || m_Sessions.erase(std::string(sessionId)) == 0) {
        return HttpResponse{405, {}, {}};
    }
    return HttpResponse{204, {}, {}};
}

} // namespace Hephaestus::Automation::Mcp
