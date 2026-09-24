#include "PackagedAutomation.h"

#if HEPHAESTUS_RUNTIME_AUTOMATION

#include "../Automation/AutomationBackend.h"
#include "../Automation/AutomationService.h"
#include "../Automation/Mcp/McpServer.h"
#include "../Automation/PlayAutomation.h"
#include "../Automation/RuntimeAutomation.h"
#include "../Input/InputSystem.h"
#include "../Scene/Scene.h"

#include <iostream>
#include <string>
#include <utility>

namespace Hephaestus::Runtime {

class PackagedAutomation::RuntimeBackend final : public Automation::AutomationBackend {
public:
    RuntimeBackend(Scene& scene, InputSystem& input, std::string projectName)
        : m_scene(scene), m_input(input), m_projectName(std::move(projectName)) {}

    Automation::HostMask Host() const noexcept override { return Automation::HostMask::Runtime; }
    Automation::SceneAccess ActiveScene() noexcept override { return {&m_scene}; }
    Hephaestus::PlayState GetPlayState() const noexcept override
    {
        return Hephaestus::PlayState::Playing;
    }

    Automation::Json GetProjectInfo() const override
    {
        return {{"name", m_projectName}, {"host", "runtime"}};
    }

    bool SetActionOverride(std::string_view action, float value, std::string&) override
    {
        m_input.SetActionOverride(std::string(action), value);
        return true;
    }
    bool SetAxisOverride(std::string_view axis, float value, std::string&) override
    {
        m_input.SetAxisOverride(std::string(axis), value);
        return true;
    }
    bool ClearInputOverrides(std::string&) override
    {
        m_input.ClearOverrides();
        return true;
    }

private:
    Scene& m_scene;
    InputSystem& m_input;
    std::string m_projectName;
};

PackagedAutomation::PackagedAutomation(Scene& scene, InputSystem& input, std::string projectName)
    : m_backend(std::make_unique<RuntimeBackend>(scene, input, std::move(projectName))) {}

PackagedAutomation::~PackagedAutomation() { Stop(); }

bool PackagedAutomation::Start(int port)
{
    if (m_service) return true;
    m_service = std::make_unique<Automation::AutomationService>(*m_backend);
    Automation::RegisterPlayAutomation(*m_service);
    Automation::RegisterRuntimeAutomation(*m_service);
    m_server = std::make_unique<Automation::Mcp::McpServer>(*m_service);
    if (!m_server->Start(port)) {
        m_server.reset();
        m_service.reset();
        return false;
    }
    std::cout << "[MCP] " << "http://127.0.0.1:" << m_server->Port() << "/mcp\n";
    return true;
}

void PackagedAutomation::Tick()
{
    if (m_service) m_service->Tick();
}

void PackagedAutomation::Stop()
{
    if (m_service) m_service->Shutdown();
    if (m_server) m_server->Stop();
    m_server.reset();
    m_service.reset();
}

bool PackagedAutomation::IsRunning() const noexcept
{
    return m_server && m_server->IsRunning();
}

int PackagedAutomation::Port() const noexcept
{
    return m_server ? m_server->Port() : 0;
}

} // namespace Hephaestus::Runtime

#endif // HEPHAESTUS_RUNTIME_AUTOMATION
