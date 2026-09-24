#include "McpHttpTransport.h"

#include <httplib/httplib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <thread>

namespace Hephaestus::Automation::Mcp {
namespace {

bool IsAllowedOrigin(const httplib::Request& request)
{
    if (!request.has_header("Origin")) return true;
    std::string origin = request.get_header_value("Origin");
    std::transform(origin.begin(), origin.end(), origin.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto scheme = origin.find("://");
    if (scheme == std::string::npos) return false;
    const std::string schemeName = origin.substr(0, scheme);
    if (schemeName != "http" && schemeName != "https") return false;
    const std::string hostPort = origin.substr(scheme + 3);
    const auto slash = hostPort.find('/');
    const std::string authority = hostPort.substr(0, slash);
    return authority == "127.0.0.1" || authority.starts_with("127.0.0.1:") ||
           authority == "localhost" || authority.starts_with("localhost:") ||
           authority == "[::1]" || authority.starts_with("[::1]:");
}

HttpRequest ConvertRequest(const httplib::Request& request)
{
    HttpRequest converted;
    converted.method = request.method;
    converted.path = request.path;
    converted.body = request.body;
    converted.headers.reserve(request.headers.size());
    for (const auto& [name, value] : request.headers) converted.headers.emplace_back(name, value);
    return converted;
}

void ApplyResponse(const HttpResponse& source, httplib::Response& destination)
{
    destination.status = source.status;
    std::string contentType;
    for (const auto& [name, value] : source.headers) {
        if (httplib::detail::case_ignore::equal(name, "Content-Type")) contentType = value;
        else destination.set_header(name, value);
    }
    if (!source.body.empty()) destination.set_content(source.body, contentType.empty() ? "application/json" : contentType);
}

} // namespace

class McpHttpTransport::Impl {
public:
    httplib::Server server;
    std::thread thread;
    std::atomic<bool> running{false};
    int port = 0;
};

McpHttpTransport::McpHttpTransport() : m_Impl(std::make_unique<Impl>()) {}
McpHttpTransport::~McpHttpTransport() { Stop(); }

bool McpHttpTransport::Start(int port, Handler handler)
{
    if (m_Impl->running.load() || !handler || port < 0 || port > 65535) return false;
    m_Impl->server.set_payload_max_length(4 * 1024 * 1024);
    m_Impl->server.set_pre_routing_handler([](const httplib::Request& request,
                                               httplib::Response& response) {
        if (IsAllowedOrigin(request)) return httplib::Server::HandlerResponse::Unhandled;
        response.status = httplib::StatusCode::Forbidden_403;
        return httplib::Server::HandlerResponse::Handled;
    });
    auto route = [handler](const httplib::Request& request, httplib::Response& response) {
        ApplyResponse(handler(ConvertRequest(request)), response);
    };
    m_Impl->server.Post("/mcp", route);
    m_Impl->server.Get("/mcp", route);
    m_Impl->server.Delete("/mcp", route);

    m_Impl->port = port == 0
        ? m_Impl->server.bind_to_any_port("127.0.0.1")
        : (m_Impl->server.bind_to_port("127.0.0.1", port) ? port : -1);
    if (m_Impl->port <= 0) return false;
    m_Impl->running.store(true);
    m_Impl->thread = std::thread([impl = m_Impl.get()]() {
        impl->server.listen_after_bind();
        impl->running.store(false);
    });
    return true;
}

void McpHttpTransport::Stop()
{
    if (!m_Impl) return;
    m_Impl->server.stop();
    if (m_Impl->thread.joinable()) m_Impl->thread.join();
    m_Impl->running.store(false);
    m_Impl->port = 0;
}

bool McpHttpTransport::IsRunning() const noexcept { return m_Impl->running.load(); }
int McpHttpTransport::Port() const noexcept { return m_Impl->port; }

} // namespace Hephaestus::Automation::Mcp
