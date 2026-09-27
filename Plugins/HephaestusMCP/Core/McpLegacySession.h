#pragma once

#include "McpSession.h"

#include <mutex>
#include <string>
#include <unordered_set>

namespace Hephaestus::Automation::Mcp {

class McpLegacySession {
public:
    explicit McpLegacySession(McpSession& modern);

    HttpResponse Initialize(const HttpRequest& request, const ParsedMessage& message);
    HttpResponse HandlePost(const HttpRequest& request, const ParsedMessage& message);
    HttpResponse HandleDelete(std::string_view sessionId);
    [[nodiscard]] bool IsValid(std::string_view sessionId) const;

private:
    std::string MintSessionId();

    McpSession& m_Modern;
    mutable std::mutex m_Mutex;
    std::unordered_set<std::string> m_Sessions;
};

} // namespace Hephaestus::Automation::Mcp
