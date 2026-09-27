#include "McpToolSchema.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace Hephaestus::Automation::Mcp {
namespace {

bool IsToken(std::string_view value)
{
    if (value.empty()) return false;
    constexpr std::string_view special = "!#$%&'*+-.^_`|~";
    for (const unsigned char character : value) {
        if (!std::isalnum(character) && special.find(static_cast<char>(character)) == std::string_view::npos) {
            return false;
        }
    }
    return true;
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool WalkProperties(const Json& schema, std::vector<std::string>& path,
                    std::set<std::string>& names, std::vector<HeaderParameter>& out,
                    std::string& outError)
{
    if (!schema.is_object()) return true;
    if (schema.contains("x-mcp-header")) {
        if (!schema["x-mcp-header"].is_string() || path.empty()) {
            outError = "x-mcp-header must be a string on a reachable property";
            return false;
        }
        const std::string name = schema["x-mcp-header"].get<std::string>();
        const std::string type = schema.value("type", "");
        if (!IsToken(name) || (type != "string" && type != "integer" && type != "boolean")) {
            outError = "x-mcp-header must name a token and annotate string, integer, or boolean";
            return false;
        }
        if (!names.insert(Lower(name)).second) {
            outError = "x-mcp-header names must be case-insensitively unique";
            return false;
        }
        out.push_back({path, "Mcp-Param-" + name, type});
    }
    const auto properties = schema.find("properties");
    if (properties == schema.end() || !properties->is_object()) return true;
    for (auto it = properties->begin(); it != properties->end(); ++it) {
        path.push_back(it.key());
        if (!WalkProperties(it.value(), path, names, out, outError)) return false;
        path.pop_back();
    }
    return true;
}

} // namespace

Json BuildToolList(const AutomationService& service)
{
    Json tools = Json::array();
    for (const ToolDefinition* definition : service.Registry().Available(
             service.CurrentHost(), service.CurrentPlayState())) {
        Json tool = {{"name", definition->name},
                     {"description", definition->description},
                     {"inputSchema", definition->inputSchema}};
        if (!definition->title.empty()) tool["title"] = definition->title;
        if (definition->outputSchema) tool["outputSchema"] = *definition->outputSchema;
        tools.push_back(std::move(tool));
    }
    return tools;
}

bool CollectHeaderParameters(const Json& schema, std::vector<HeaderParameter>& out,
                             std::string& outError)
{
    out.clear();
    std::vector<std::string> path;
    std::set<std::string> names;
    return WalkProperties(schema, path, names, out, outError);
}

} // namespace Hephaestus::Automation::Mcp
