#include "HephaestusMCP/Core/McpServer.h"

#include <httplib/httplib.h>

#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace Automation = Hephaestus::Automation;
namespace Mcp = Hephaestus::Automation::Mcp;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

class FakeBackend final : public Automation::AutomationBackend {
public:
    Automation::HostMask Host() const noexcept override { return Automation::HostMask::Editor; }
    Automation::SceneAccess ActiveScene() noexcept override { return {}; }
    Hephaestus::PlayState GetPlayState() const noexcept override
    {
        return Hephaestus::PlayState::Editor;
    }
};

Automation::Json Meta(std::string version = "2026-07-28",
                      Automation::Json capabilities = Automation::Json::object())
{
    return {{"io.modelcontextprotocol/protocolVersion", std::move(version)},
            {"io.modelcontextprotocol/clientCapabilities", std::move(capabilities)},
            {"io.modelcontextprotocol/clientInfo", {{"name", "ConformanceTest"},
                                                     {"version", "1.0"}}}};
}

Automation::Json RequestBody(const Automation::Json& id, std::string method,
                             Automation::Json params = Automation::Json::object())
{
    params["_meta"] = Meta();
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", std::move(method)},
            {"params", std::move(params)}};
}

Mcp::HttpRequest ModernRequest(std::string method, Automation::Json body,
                               std::string name = {})
{
    Mcp::HttpRequest request;
    request.method = "POST";
    request.path = "/mcp";
    request.body = body.dump();
    request.headers = {
        {"Content-Type", "application/json"},
        {"Accept", "application/json, text/event-stream"},
        {"MCP-Protocol-Version", "2026-07-28"},
        {"Mcp-Method", std::move(method)},
    };
    if (!name.empty()) request.headers.emplace_back("Mcp-Name", std::move(name));
    return request;
}

Automation::Json Body(const Mcp::HttpResponse& response)
{
    return response.body.empty() ? Automation::Json() : Automation::Json::parse(response.body);
}

Mcp::HttpResponse CallWithPump(Mcp::McpServer& server, Automation::AutomationService& service,
                               Mcp::HttpRequest request)
{
    auto response = std::async(std::launch::async,
                               [&server, request = std::move(request)]() mutable {
                                   return server.Handle(request);
                               });
    while (response.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
        service.Tick();
    }
    return response.get();
}

bool TestModernProtocol(Mcp::McpServer& server, Automation::AutomationService& service)
{
    bool ok = true;

    auto discover = ModernRequest("server/discover", RequestBody(1, "server/discover"));
    Mcp::HttpResponse response = server.Handle(discover);
    Automation::Json json = Body(response);
    ok &= Check(response.status == 200, "server/discover returns HTTP 200");
    ok &= Check(json["result"]["resultType"] == "complete", "discover resultType");
    ok &= Check(json["result"]["supportedVersions"].size() == 2,
                "discover advertises modern and legacy versions");
    ok &= Check(json["result"]["capabilities"].contains("tools"), "discover tools capability");
    ok &= Check(json["result"]["_meta"].contains("io.modelcontextprotocol/serverInfo"),
                "discover includes server info");

    response = server.Handle(ModernRequest("tools/list", RequestBody(2, "tools/list")));
    json = Body(response);
    ok &= Check(response.status == 200 && json["result"]["resultType"] == "complete",
                "tools/list is a complete result");
    ok &= Check(json["result"]["tools"].size() == 3, "tools/list exposes registered editor tools");
    ok &= Check(json["result"]["tools"][0]["name"] == "capability.tool" &&
                    json["result"]["tools"][1]["name"] == "echo.tool" &&
                    json["result"]["tools"][2]["name"] == "fail.tool",
                "tools/list order is deterministic");

    Mcp::HttpRequest missingMeta;
    missingMeta.method = "POST";
    missingMeta.path = "/mcp";
    missingMeta.headers = {{"Content-Type", "application/json"}};
    missingMeta.body = Automation::Json({{"jsonrpc", "2.0"}, {"id", 3},
                                         {"method", "tools/list"},
                                         {"params", Automation::Json::object()}}).dump();
    response = server.Handle(missingMeta);
    json = Body(response);
    ok &= Check(response.status == 400 && json["error"]["code"] == Mcp::InvalidParams,
                "Missing modern metadata is not reinterpreted as legacy");

    Mcp::HttpRequest missingHeader = ModernRequest("tools/list", RequestBody(4, "tools/list"));
    missingHeader.headers.erase(missingHeader.headers.begin() + 2);
    response = server.Handle(missingHeader);
    ok &= Check(response.status == 400 && Body(response)["error"]["code"] == Mcp::HeaderMismatch,
                "Missing protocol header returns HeaderMismatch");

    Mcp::HttpRequest mismatch = ModernRequest("tools/list", RequestBody(5, "tools/list"));
    for (auto& [name, value] : mismatch.headers) {
        if (name == "Mcp-Method") value = "tools/call";
    }
    response = server.Handle(mismatch);
    ok &= Check(response.status == 400 && Body(response)["error"]["code"] == Mcp::HeaderMismatch,
                "Header/body mismatch returns HeaderMismatch");

    Automation::Json unsupportedBody = RequestBody(6, "tools/list");
    unsupportedBody["params"]["_meta"] = Meta("2099-01-01");
    Mcp::HttpRequest unsupported = ModernRequest("tools/list", std::move(unsupportedBody));
    for (auto& [name, value] : unsupported.headers) {
        if (name == "MCP-Protocol-Version") value = "2099-01-01";
    }
    response = server.Handle(unsupported);
    json = Body(response);
    ok &= Check(response.status == 400 && json["error"]["code"] == Mcp::UnsupportedProtocolVersion,
                "Unsupported version returns protocol error");
    ok &= Check(json["error"]["data"]["supported"].size() == 2,
                "Unsupported version lists supported versions");

    response = server.Handle(ModernRequest("unknown/method", RequestBody(7, "unknown/method")));
    ok &= Check(response.status == 404 && Body(response)["error"]["code"] == Mcp::MethodNotFound,
                "Unknown method returns HTTP 404 and MethodNotFound");

    Mcp::HttpRequest notification;
    notification.method = "POST";
    notification.path = "/mcp";
    notification.headers = {{"Content-Type", "application/json"}};
    notification.body = Automation::Json({{"jsonrpc", "2.0"},
                                           {"method", "notifications/example"}}).dump();
    response = server.Handle(notification);
    ok &= Check(response.status == 202 && response.body.empty(),
                "Modern notification without request-only routing headers returns empty 202");

    Mcp::HttpRequest modernDelete;
    modernDelete.method = "DELETE";
    modernDelete.path = "/mcp";
    response = server.Handle(modernDelete);
    ok &= Check(response.status == 405, "Modern DELETE is Method Not Allowed");

    Automation::Json callBody = RequestBody(8, "tools/call",
                                             {{"name", "echo.tool"},
                                              {"arguments", {{"tenant", "local"}, {"value", 42}}}});
    Mcp::HttpRequest call = ModernRequest("tools/call", std::move(callBody), "echo.tool");
    call.headers.emplace_back("Mcp-Param-Tenant", "local");
    response = CallWithPump(server, service, std::move(call));
    json = Body(response);
    ok &= Check(response.status == 200 && !json["result"]["isError"].get<bool>(),
                "tools/call executes through AutomationService");
    ok &= Check(json["result"]["structuredContent"]["value"] == 42,
                "tools/call returns structured content");

    Automation::Json badHeaderBody = RequestBody(9, "tools/call",
        {{"name", "echo.tool"}, {"arguments", {{"tenant", "body"}}}});
    Mcp::HttpRequest badHeader = ModernRequest("tools/call", std::move(badHeaderBody), "echo.tool");
    badHeader.headers.emplace_back("Mcp-Param-Tenant", "header");
    response = server.Handle(badHeader);
    ok &= Check(response.status == 400 && Body(response)["error"]["code"] == Mcp::HeaderMismatch,
                "Custom routing header is validated against arguments");

    Automation::Json failBody = RequestBody(10, "tools/call",
                                             {{"name", "fail.tool"},
                                              {"arguments", Automation::Json::object()}});
    response = CallWithPump(server, service,
                            ModernRequest("tools/call", std::move(failBody), "fail.tool"));
    json = Body(response);
    ok &= Check(response.status == 200 && json["result"]["isError"].get<bool>(),
                "Tool execution errors are tool results");

    Automation::Json unknownBody = RequestBody(11, "tools/call",
                                                {{"name", "missing.tool"}});
    response = server.Handle(ModernRequest("tools/call", std::move(unknownBody), "missing.tool"));
    ok &= Check(response.status == 400 && Body(response)["error"]["code"] == Mcp::InvalidParams,
                "Unknown tools are protocol InvalidParams errors");

    Automation::Json hiddenBody = RequestBody(111, "tools/call",
                                               {{"name", "runtime.hidden"}});
    response = server.Handle(ModernRequest("tools/call", std::move(hiddenBody), "runtime.hidden"));
    ok &= Check(response.status == 400 && Body(response)["error"]["code"] == Mcp::InvalidParams,
                "Host-incompatible tools cannot be called by name");

    Automation::Json capabilityBody = RequestBody(12, "tools/call",
                                                   {{"name", "capability.tool"}});
    response = server.Handle(ModernRequest("tools/call", std::move(capabilityBody), "capability.tool"));
    ok &= Check(response.status == 400 &&
                    Body(response)["error"]["code"] == Mcp::MissingRequiredClientCapability,
                "Missing client capability returns -32021");

    return ok;
}

bool TestLegacyProtocol(Mcp::McpServer& server)
{
    bool ok = true;
    Mcp::HttpRequest initialize;
    initialize.method = "POST";
    initialize.path = "/mcp";
    initialize.headers = {{"Content-Type", "application/json"},
                          {"Accept", "application/json, text/event-stream"}};
    initialize.body = Automation::Json({
        {"jsonrpc", "2.0"}, {"id", 20}, {"method", "initialize"},
        {"params", {{"protocolVersion", "2025-11-25"},
                    {"capabilities", Automation::Json::object()},
                    {"clientInfo", {{"name", "LegacyTest"}, {"version", "1.0"}}}}}
    }).dump();
    Mcp::HttpResponse response = server.Handle(initialize);
    const std::string sessionId = response.Header("Mcp-Session-Id");
    ok &= Check(response.status == 200 && !sessionId.empty(),
                "Legacy initialize mints a session id");
    ok &= Check(!Body(response)["result"].contains("resultType"),
                "Legacy initialize omits resultType");

    Mcp::HttpRequest list;
    list.method = "POST";
    list.path = "/mcp";
    list.headers = {{"Content-Type", "application/json"}, {"Mcp-Session-Id", sessionId}};
    list.body = Automation::Json({{"jsonrpc", "2.0"}, {"id", 21},
                                  {"method", "tools/list"},
                                  {"params", Automation::Json::object()}}).dump();
    response = server.Handle(list);
    ok &= Check(response.status == 200 && !Body(response)["result"].contains("resultType"),
                "Valid legacy session uses legacy response shape");

    Mcp::HttpRequest modern = ModernRequest("tools/list", RequestBody(22, "tools/list"));
    modern.headers.emplace_back("Mcp-Session-Id", sessionId);
    response = server.Handle(modern);
    ok &= Check(response.status == 200 && Body(response)["result"]["resultType"] == "complete",
                "Modern metadata wins over a legacy session header");
    ok &= Check(!response.HasHeader("Mcp-Session-Id"),
                "Modern response does not echo a legacy session header");

    Mcp::HttpRequest terminate;
    terminate.method = "DELETE";
    terminate.path = "/mcp";
    terminate.headers = {{"Mcp-Session-Id", sessionId}};
    response = server.Handle(terminate);
    ok &= Check(response.status == 204, "DELETE with valid legacy session terminates it");
    response = server.Handle(terminate);
    ok &= Check(response.status == 405, "DELETE with terminated session follows modern 405 behavior");
    return ok;
}

bool TestHttpTransport(Mcp::McpServer& server)
{
    bool ok = true;
    ok &= Check(server.Start(0), "HTTP server starts on an ephemeral loopback port");
    if (!server.IsRunning()) return false;

    httplib::Client client("127.0.0.1", server.Port());
    const Automation::Json body = RequestBody(30, "server/discover");
    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        {"Accept", "application/json, text/event-stream"},
        {"MCP-Protocol-Version", "2026-07-28"},
        {"Mcp-Method", "server/discover"},
        {"Origin", "https://evil.example"},
    };
    auto response = client.Post("/mcp", headers, body.dump(), "application/json");
    ok &= Check(response && response->status == 403, "Invalid Origin is rejected");

    headers.erase("Origin");
    response = client.Post("/mcp", headers, body.dump(), "application/json");
    ok &= Check(response && response->status == 200, "Absent Origin is accepted on loopback");

    auto get = client.Get("/mcp");
    ok &= Check(get && get->status == 405, "GET /mcp returns 405");
    server.Stop();
    ok &= Check(!server.IsRunning(), "HTTP server shuts down cleanly");
    return ok;
}

} // namespace

int main()
{
    FakeBackend backend;
    Automation::AutomationService service(backend);
    std::string error;

    Automation::ToolDefinition echo;
    echo.name = "echo.tool";
    echo.description = "Echo structured arguments.";
    echo.hostMask = Automation::HostMask::Editor;
    echo.inputSchema = {
        {"type", "object"},
        {"properties", {{"tenant", {{"type", "string"}, {"x-mcp-header", "Tenant"}}},
                         {"value", {{"type", "integer"}}}}},
        {"required", Automation::Json::array({"tenant"})},
    };
    echo.handler = [](Automation::ToolContext&, const Automation::Json& arguments) {
        return Automation::ToolResult::Success(arguments);
    };
    service.Registry().Register(std::move(echo), error);

    Automation::ToolDefinition failure;
    failure.name = "fail.tool";
    failure.description = "Return an actionable tool error.";
    failure.hostMask = Automation::HostMask::Editor;
    failure.handler = [](Automation::ToolContext&, const Automation::Json&) {
        return Automation::ToolResult::Failure(Automation::ErrorCode::InvalidArguments,
                                               "Expected test failure");
    };
    service.Registry().Register(std::move(failure), error);

    Automation::ToolDefinition capability;
    capability.name = "capability.tool";
    capability.description = "Requires sampling support.";
    capability.hostMask = Automation::HostMask::Editor;
    capability.requiredClientCapabilities = {"sampling"};
    capability.handler = [](Automation::ToolContext&, const Automation::Json&) {
        return Automation::ToolResult::Success();
    };
    service.Registry().Register(std::move(capability), error);

    Automation::ToolDefinition hidden;
    hidden.name = "runtime.hidden";
    hidden.description = "Must not be exposed by an editor host.";
    hidden.hostMask = Automation::HostMask::Runtime;
    hidden.handler = [](Automation::ToolContext&, const Automation::Json&) {
        return Automation::ToolResult::Success();
    };
    service.Registry().Register(std::move(hidden), error);

    Mcp::SessionConfig config;
    config.serverName = "HephaestusTest";
    config.serverVersion = "1.0.0";
    Mcp::McpServer server(service, std::move(config));

    bool ok = true;
    ok &= TestModernProtocol(server, service);
    ok &= TestLegacyProtocol(server);
    ok &= TestHttpTransport(server);
    service.Shutdown();

    if (ok) {
        std::cout << "McpProtocolTests passed\n";
        return 0;
    }
    return 1;
}
