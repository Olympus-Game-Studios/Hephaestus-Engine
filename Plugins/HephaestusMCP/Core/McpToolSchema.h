#pragma once

#include "PluginAPI/PluginFirstParty.h"

#include <string>
#include <vector>

namespace Hephaestus::Automation::Mcp {

struct HeaderParameter {
    std::vector<std::string> path;
    std::string headerName;
    std::string type;
};

Json BuildToolList(const Plugins::IAutomationService& service);
bool CollectHeaderParameters(const Json& schema, std::vector<HeaderParameter>& out,
                             std::string& outError);

} // namespace Hephaestus::Automation::Mcp
