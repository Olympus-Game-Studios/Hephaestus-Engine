#include "McpJsonRpc.h"

#include <utility>

namespace Hephaestus::Automation::Mcp {

Json MakeError(const Json& id, int code, std::string message, Json data)
{
    Json error = {{"code", code}, {"message", std::move(message)}};
    if (!data.is_null()) error["data"] = std::move(data);
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(error)}};
}

Json MakeResult(const Json& id, Json result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

ParseResult ParseClientMessage(std::string_view body)
{
    Json value;
    try {
        value = Json::parse(body.begin(), body.end());
    } catch (...) {
        return {std::nullopt, MakeError(nullptr, ParseError, "Parse error")};
    }

    Json id = value.is_object() && value.contains("id") ? value["id"] : Json(nullptr);
    if (!value.is_object() || value.value("jsonrpc", "") != "2.0" ||
        !value.contains("method") || !value["method"].is_string()) {
        return {std::nullopt, MakeError(id, InvalidRequest, "Invalid Request")};
    }
    if (value.contains("id") &&
        !(value["id"].is_string() || value["id"].is_number_integer() ||
          value["id"].is_number_unsigned())) {
        return {std::nullopt, MakeError(nullptr, InvalidRequest, "Request id must be a string or integer")};
    }
    if (value.contains("params") && !value["params"].is_object()) {
        return {std::nullopt, MakeError(id, InvalidParams, "Params must be an object")};
    }

    ParsedMessage message;
    message.value = std::move(value);
    message.notification = !message.value.contains("id");
    message.id = message.notification ? Json(nullptr) : message.value["id"];
    message.method = message.value["method"].get<std::string>();
    if (message.value.contains("params")) message.params = message.value["params"];
    return {std::move(message), Json()};
}

} // namespace Hephaestus::Automation::Mcp
