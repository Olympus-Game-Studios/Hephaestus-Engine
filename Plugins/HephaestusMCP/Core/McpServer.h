#pragma once

#include "McpHttpTransport.h"
#include "McpLegacySession.h"

namespace Hephaestus::Automation::Mcp {

class McpServer {
public:
    McpServer(AutomationService& service, SessionConfig config = {});
    ~McpServer();

    bool Start(int port = 0);
    void Stop();
    [[nodiscard]] bool IsRunning() const noexcept { return m_Transport.IsRunning(); }
    [[nodiscard]] int Port() const noexcept { return m_Transport.Port(); }

    HttpResponse Handle(const HttpRequest& request);

private:
    McpSession m_Modern;
#if HEPHAESTUS_MCP_LEGACY_COMPAT
    McpLegacySession m_Legacy;
#endif
    McpHttpTransport m_Transport;
};

} // namespace Hephaestus::Automation::Mcp
