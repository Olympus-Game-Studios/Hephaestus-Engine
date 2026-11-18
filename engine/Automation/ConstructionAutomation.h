#pragma once

namespace Hephaestus::Automation {

class AutomationService;

// construction.create, construction.add_opening, construction.set_params and construction.get.
void RegisterConstructionAutomation(AutomationService& service);

} // namespace Hephaestus::Automation
