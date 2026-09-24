#pragma once

/**
 * @file PackagedAutomation.h
 * @brief Opt-in MCP automation host for the packaged game runtime.
 *
 * This entire unit is compiled only when the runtime is built with
 * `-DHEPHAESTUS_RUNTIME_AUTOMATION=ON`. A shipping/Release binary built without
 * that define contains no MCP code at all; editing `game.ini` cannot enable it.
 */

#if HEPHAESTUS_RUNTIME_AUTOMATION

#include <cstdint>
#include <memory>
#include <string>

class Scene;

namespace Hephaestus {
class InputSystem;
}

namespace Hephaestus::Automation {
class AutomationService;
namespace Mcp {
class McpServer;
}
}

namespace Hephaestus::Runtime {

class PackagedAutomation {
public:
    PackagedAutomation(Scene& scene, InputSystem& input, std::string projectName);
    ~PackagedAutomation();

    PackagedAutomation(const PackagedAutomation&) = delete;
    PackagedAutomation& operator=(const PackagedAutomation&) = delete;

    bool Start(int port);
    void Tick();
    void Stop();

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] int Port() const noexcept;

private:
    class RuntimeBackend;
    std::unique_ptr<RuntimeBackend> m_backend;
    std::unique_ptr<Automation::AutomationService> m_service;
    std::unique_ptr<Automation::Mcp::McpServer> m_server;
};

} // namespace Hephaestus::Runtime

#endif // HEPHAESTUS_RUNTIME_AUTOMATION
