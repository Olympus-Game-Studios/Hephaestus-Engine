#pragma once

#include "Automation/AutomationTypes.h"

#include <optional>
#include <string_view>

namespace Hephaestus::Automation::Mcp {

constexpr int ParseError = -32700;
constexpr int InvalidRequest = -32600;
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams = -32602;
constexpr int InternalError = -32603;
constexpr int HeaderMismatch = -32020;
constexpr int MissingRequiredClientCapability = -32021;
constexpr int UnsupportedProtocolVersion = -32022;

struct ParsedMessage {
    Json value;
    Json id = nullptr;
    std::string method;
    Json params = Json::object();
    bool notification = false;
};

struct ParseResult {
    std::optional<ParsedMessage> message;
    Json errorResponse;
};

ParseResult ParseClientMessage(std::string_view body);
Json MakeError(const Json& id, int code, std::string message,
               Json data = Json());
Json MakeResult(const Json& id, Json result);

} // namespace Hephaestus::Automation::Mcp
