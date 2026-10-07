#pragma once
namespace Hephaestus::Automation {
class AutomationService;
void RegisterViewportAutomation(AutomationService& service);
void RegisterRuntimeCapture(AutomationService& service);
}
