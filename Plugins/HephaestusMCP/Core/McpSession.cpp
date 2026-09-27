#include "McpSession.h"

#include "McpToolSchema.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>

namespace Hephaestus::Automation::Mcp {
namespace {

bool ContainsMediaType(std::string value, std::string_view expected)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.find(expected) != std::string::npos;
}

bool IsSafePlainHeader(std::string_view value)
{
    if (value.empty()) return true;
    if (value.front() == ' ' || value.front() == '\t' || value.back() == ' ' || value.back() == '\t') {
        return false;
    }
    for (const unsigned char character : value) {
        if (character < 0x20 || character > 0x7e) return false;
    }
    return true;
}

int Base64Value(unsigned char character)
{
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

bool DecodeHeaderValue(std::string_view input, std::string& output)
{
    constexpr std::string_view prefix = "=?base64?";
    constexpr std::string_view suffix = "?=";
    if (!input.starts_with(prefix) || !input.ends_with(suffix)) {
        if (!IsSafePlainHeader(input)) return false;
        output.assign(input);
        return true;
    }
    input.remove_prefix(prefix.size());
    input.remove_suffix(suffix.size());
    if (input.empty() || input.size() % 4 != 0) return false;
    output.clear();
    for (std::size_t offset = 0; offset < input.size(); offset += 4) {
        const int a = Base64Value(static_cast<unsigned char>(input[offset]));
        const int b = Base64Value(static_cast<unsigned char>(input[offset + 1]));
        const int c = input[offset + 2] == '=' ? -2 : Base64Value(static_cast<unsigned char>(input[offset + 2]));
        const int d = input[offset + 3] == '=' ? -2 : Base64Value(static_cast<unsigned char>(input[offset + 3]));
        if (a < 0 || b < 0 || c == -1 || d == -1 || (c == -2 && d != -2)) return false;
        output.push_back(static_cast<char>((a << 2) | (b >> 4)));
        if (c >= 0) output.push_back(static_cast<char>(((b & 15) << 4) | (c >> 2)));
        if (d >= 0) output.push_back(static_cast<char>(((c & 3) << 6) | d));
    }
    return true;
}

const Json* ValueAtPath(const Json& root, const std::vector<std::string>& path)
{
    const Json* current = &root;
    for (const std::string& segment : path) {
        if (!current->is_object()) return nullptr;
        const auto it = current->find(segment);
        if (it == current->end()) return nullptr;
        current = &*it;
    }
    return current;
}

bool HasCapabilityPath(const Json& capabilities, std::string_view path)
{
    const Json* current = &capabilities;
    std::size_t offset = 0;
    while (offset < path.size()) {
        const std::size_t separator = path.find('.', offset);
        const std::string key(path.substr(offset, separator == std::string_view::npos
            ? path.size() - offset : separator - offset));
        if (!current->is_object()) return false;
        const auto it = current->find(key);
        if (it == current->end()) return false;
        current = &*it;
        if (separator == std::string_view::npos) return true;
        offset = separator + 1;
    }
    return false;
}

std::string PrimitiveString(const Json& value, std::string_view type)
{
    if (type == "string" && value.is_string()) return value.get<std::string>();
    if (type == "boolean" && value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (type == "integer" && (value.is_number_integer() || value.is_number_unsigned())) {
        return value.dump();
    }
    return {};
}

bool IsToolAvailable(const Plugins::IAutomationService& service, const ToolDefinition* definition)
{
    if (!definition) return false;
    const auto available = service.GetRegistry().Available(service.GetHost(),
                                                           service.GetPlayState());
    return std::find(available.begin(), available.end(), definition) != available.end();
}

} // namespace

McpSession::McpSession(Plugins::IAutomationService& service, SessionConfig config)
    : m_Service(service), m_Config(std::move(config))
{
}

Json McpSession::ServerInfoMeta() const
{
    return {{"io.modelcontextprotocol/serverInfo",
             {{"name", m_Config.serverName}, {"version", m_Config.serverVersion}}}};
}

Json McpSession::DecorateResult(Json result) const
{
    result["resultType"] = result.value("resultType", "complete");
    result["_meta"] = ServerInfoMeta();
    return result;
}

HttpResponse McpSession::JsonError(const Json& id, int code, std::string message,
                                   int httpStatus, Json data) const
{
    HttpResponse response;
    response.status = httpStatus;
    response.SetHeader("Content-Type", "application/json");
    response.body = MakeError(id, code, std::move(message), std::move(data)).dump();
    return response;
}

HttpResponse McpSession::JsonResult(const Json& id, Json result) const
{
    HttpResponse response;
    response.status = 200;
    response.SetHeader("Content-Type", "application/json");
    response.body = MakeResult(id, DecorateResult(std::move(result))).dump();
    return response;
}

bool McpSession::ValidateModernMetadata(const ParsedMessage& message,
                                        HttpResponse& outError) const
{
    if (!message.params.is_object() || !message.params.contains("_meta") ||
        !message.params["_meta"].is_object()) {
        outError = JsonError(message.id, InvalidParams, "Missing required request metadata", 400);
        return false;
    }
    const Json& meta = message.params["_meta"];
    const auto version = meta.find("io.modelcontextprotocol/protocolVersion");
    const auto capabilities = meta.find("io.modelcontextprotocol/clientCapabilities");
    if (version == meta.end() || !version->is_string() ||
        capabilities == meta.end() || !capabilities->is_object()) {
        outError = JsonError(message.id, InvalidParams, "Missing required request metadata fields", 400);
        return false;
    }
    if (version->get<std::string>() != ProtocolVersion) {
        Json supported = Json::array({ProtocolVersion});
        if (m_Config.advertiseLegacy) supported.push_back(LegacyProtocolVersion);
        outError = JsonError(message.id, UnsupportedProtocolVersion,
                             "Unsupported protocol version", 400,
                             {{"supported", std::move(supported)},
                              {"requested", version->get<std::string>()}});
        return false;
    }
    return true;
}

bool McpSession::ValidateModernHeaders(const HttpRequest& request,
                                       const ParsedMessage& message,
                                       HttpResponse& outError) const
{
    const std::string accept = request.Header("Accept");
    if (!ContainsMediaType(accept, "application/json") ||
        !ContainsMediaType(accept, "text/event-stream")) {
        outError = JsonError(message.id, InvalidRequest,
                             "Accept must list application/json and text/event-stream", 406);
        return false;
    }
    const std::string bodyVersion =
        message.params["_meta"]["io.modelcontextprotocol/protocolVersion"].get<std::string>();
    const std::string protocolHeader = request.Header("MCP-Protocol-Version");
    const std::string methodHeader = request.Header("Mcp-Method");
    if (protocolHeader.empty() || methodHeader.empty() ||
        protocolHeader != bodyVersion || methodHeader != message.method) {
        outError = JsonError(message.id, HeaderMismatch,
                             "Required MCP headers are missing or do not match the request body", 400);
        return false;
    }
    if (message.method == "tools/call") {
        if (!message.params.contains("name") || !message.params["name"].is_string()) {
            outError = JsonError(message.id, InvalidParams, "tools/call requires params.name", 400);
            return false;
        }
        std::string decodedName;
        const std::string nameHeader = request.Header("Mcp-Name");
        if (nameHeader.empty() || !DecodeHeaderValue(nameHeader, decodedName) ||
            decodedName != message.params["name"].get<std::string>()) {
            outError = JsonError(message.id, HeaderMismatch,
                                 "Mcp-Name is missing, malformed, or does not match params.name", 400);
            return false;
        }
    }
    return true;
}

bool McpSession::ValidateCustomHeaders(const HttpRequest& request,
                                       const ParsedMessage& message,
                                       const ToolDefinition& definition,
                                       HttpResponse& outError) const
{
    std::vector<HeaderParameter> parameters;
    std::string schemaError;
    if (!CollectHeaderParameters(definition.inputSchema, parameters, schemaError)) {
        outError = JsonError(message.id, InternalError, "Invalid tool header schema", 500);
        return false;
    }
    const Json arguments = message.params.value("arguments", Json::object());
    if (!arguments.is_object()) {
        outError = JsonError(message.id, InvalidParams, "tools/call arguments must be an object", 400);
        return false;
    }
    for (const HeaderParameter& parameter : parameters) {
        const Json* value = ValueAtPath(arguments, parameter.path);
        if (!value || value->is_null()) {
            if (request.HasHeader(parameter.headerName)) {
                outError = JsonError(message.id, HeaderMismatch,
                                     parameter.headerName + " must be omitted when its value is absent", 400);
                return false;
            }
            continue;
        }
        const std::string expected = PrimitiveString(*value, parameter.type);
        std::string actual;
        if (expected.empty() && !(parameter.type == "string" && value->is_string())) {
            outError = JsonError(message.id, InvalidParams, "Header parameter has the wrong JSON type", 400);
            return false;
        }
        if (!request.HasHeader(parameter.headerName) ||
            !DecodeHeaderValue(request.Header(parameter.headerName), actual) || actual != expected) {
            outError = JsonError(message.id, HeaderMismatch,
                                 parameter.headerName + " is missing, malformed, or inconsistent", 400);
            return false;
        }
    }
    return true;
}

Json McpSession::BuildToolsListResult(bool modern) const
{
    Json result = {{"tools", BuildToolList(m_Service)}};
    if (modern) {
        result["resultType"] = "complete";
        result["ttlMs"] = 0;
        result["cacheScope"] = "public";
    }
    return result;
}

Json McpSession::ExecuteToolCall(const Json& id, const Json& params, bool modern,
                                 int& outHttpStatus)
{
    outHttpStatus = 200;
    if (!params.contains("name") || !params["name"].is_string()) {
        outHttpStatus = 400;
        return MakeError(id, InvalidParams, "tools/call requires params.name");
    }
    const std::string name = params["name"].get<std::string>();
    const ToolDefinition* definition = m_Service.GetRegistry().Find(name);
    if (!IsToolAvailable(m_Service, definition)) {
        outHttpStatus = 400;
        return MakeError(id, InvalidParams, "Unknown tool: " + name);
    }
    Json arguments = params.value("arguments", Json::object());
    if (!arguments.is_object()) {
        outHttpStatus = 400;
        return MakeError(id, InvalidParams, "tools/call arguments must be an object");
    }

    Plugins::ToolFuture posted = m_Service.PostCall({name, std::move(arguments)});
    if (posted.wait_for(m_Config.requestTimeout) != std::future_status::ready) {
        Json result = {{"content", Json::array({{{"type", "text"},
                                                  {"text", "Automation request timed out"}}})},
                       {"isError", true}};
        if (modern) result = DecorateResult(std::move(result));
        return MakeResult(id, std::move(result));
    }
    ToolResult toolResult = posted.get();
    Json structured;
    std::string text;
    bool isError = !toolResult.Ok();
    if (toolResult.Ok()) {
        structured = toolResult.value;
        text = toolResult.value.dump();
    } else {
        structured = {{"code", ErrorCodeName(toolResult.error->code)},
                      {"message", toolResult.error->message},
                      {"data", toolResult.error->data}};
        text = std::string(ErrorCodeName(toolResult.error->code)) + ": " + toolResult.error->message;
    }
    Json result = {{"content", Json::array({{{"type", "text"}, {"text", std::move(text)}}})},
                   {"structuredContent", std::move(structured)},
                   {"isError", isError}};
    if (modern) result = DecorateResult(std::move(result));
    return MakeResult(id, std::move(result));
}

HttpResponse McpSession::HandlePost(const HttpRequest& request)
{
    if (!ContainsMediaType(request.Header("Content-Type"), "application/json")) {
        return JsonError(nullptr, InvalidRequest, "Content-Type must be application/json", 415);
    }
    ParseResult parsed = ParseClientMessage(request.body);
    if (!parsed.message) {
        HttpResponse response;
        response.status = 400;
        response.SetHeader("Content-Type", "application/json");
        response.body = parsed.errorResponse.dump();
        return response;
    }
    const ParsedMessage& message = *parsed.message;

    // The 2026-07-28 transport intentionally does not define request metadata
    // header requirements for notification POSTs.
    if (message.notification) {
        return HttpResponse{202, {}, {}};
    }

    HttpResponse validationError;
    if (!ValidateModernMetadata(message, validationError) ||
        !ValidateModernHeaders(request, message, validationError)) {
        return validationError;
    }

    if (message.method == "server/discover") {
        Json versions = Json::array({ProtocolVersion});
        if (m_Config.advertiseLegacy) versions.push_back(LegacyProtocolVersion);
        return JsonResult(message.id,
                          {{"supportedVersions", std::move(versions)},
                           {"capabilities", {{"tools", {{"listChanged", false}}}}},
                           {"instructions", m_Config.instructions},
                           {"ttlMs", 0},
                           {"cacheScope", "public"}});
    }
    if (message.method == "tools/list") {
        Json result = BuildToolsListResult(true);
        result["_meta"] = ServerInfoMeta();
        HttpResponse response;
        response.status = 200;
        response.SetHeader("Content-Type", "application/json");
        response.body = MakeResult(message.id, std::move(result)).dump();
        return response;
    }
    if (message.method == "tools/call") {
        const std::string name = message.params.value("name", "");
        const ToolDefinition* definition = m_Service.GetRegistry().Find(name);
        if (!IsToolAvailable(m_Service, definition)) {
            return JsonError(message.id, InvalidParams, "Unknown tool: " + name, 400);
        }
        const Json& capabilities =
            message.params["_meta"]["io.modelcontextprotocol/clientCapabilities"];
        Json missing = Json::array();
        for (const std::string& capability : definition->requiredClientCapabilities) {
            if (!HasCapabilityPath(capabilities, capability)) missing.push_back(capability);
        }
        if (!missing.empty()) {
            return JsonError(message.id, MissingRequiredClientCapability,
                             "Missing required client capability", 400,
                             {{"requiredCapabilities", std::move(missing)}});
        }
        if (!ValidateCustomHeaders(request, message, *definition, validationError)) return validationError;
        int status = 200;
        Json responseJson = ExecuteToolCall(message.id, message.params, true, status);
        HttpResponse response;
        response.status = status;
        response.SetHeader("Content-Type", "application/json");
        response.body = responseJson.dump();
        return response;
    }
    return JsonError(message.id, MethodNotFound, "Method not found", 404);
}

} // namespace Hephaestus::Automation::Mcp
