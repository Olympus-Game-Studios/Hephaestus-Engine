#include "PackagedAutomation.h"

#if HEPHAESTUS_RUNTIME_AUTOMATION

#include "../Automation/AutomationBackend.h"
#include "../Automation/AutomationService.h"
#include "../Automation/LogAutomation.h"
#include "HephaestusMCP/Core/McpServer.h"
#include "../Automation/PhysicsAutomation.h"
#include "../Automation/PhysicsHostAdapter.h"
#include "../Automation/PlayAutomation.h"
#include "../Automation/RuntimeAutomation.h"
#include "../Automation/RuntimeUIHostAdapter.h"
#include "../Automation/UIAutomation.h"
#include "../Automation/ViewportAutomation.h"
#include "../Gfx/EngineFrameRenderer.h"
#include "../Physics/CharacterControllerSystem.h"
#include "../Physics/JoltPhysicsEngine.h"
#include "../Physics/JoltPhysicsSystem.h"
#include "../Input/InputSystem.h"
#include "../Scene/Scene.h"
#include "../UI/HephaestusUI/UIContext.h"
#include "../UI/HephaestusUI/UISystem.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace Hephaestus::Runtime {

class PackagedAutomation::RuntimeBackend final : public Automation::AutomationBackend {
public:
    RuntimeBackend(Scene& scene, InputSystem& input, HephaestusUI::UISystem& ui,
                   HephaestusUI::UIContext& uiContext, Physics::JoltPhysicsEngine& physicsEngine,
                   Physics::JoltPhysicsSystem& physics, Physics::CharacterControllerSystem& characterControllers,
                   Hephaestus::EngineFrameRenderer& frameRenderer, std::string rootDir, std::string projectName)
        : m_scene(scene), m_input(input), m_ui(ui), m_uiContext(uiContext),
          m_physicsEngine(physicsEngine), m_physics(physics), m_characterControllers(characterControllers),
          m_frameRenderer(frameRenderer), m_rootDir(std::move(rootDir)), m_projectName(std::move(projectName)) {}

    Automation::HostMask Host() const noexcept override { return Automation::HostMask::Runtime; }
    Automation::SceneAccess ActiveScene() noexcept override { return {&m_scene}; }
    Hephaestus::PlayState GetPlayState() const noexcept override
    {
        return Hephaestus::PlayState::Playing;
    }

    Automation::Json GetProjectInfo() const override
    {
        return {{"name", m_projectName}, {"host", "runtime"}, {"rootDir", m_rootDir}};
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

    bool CopyRuntimeUI(std::vector<Automation::AutomationBackend::UIScreenNode>& out,
                       std::string& outError) override
    {
        return Automation::CopyRuntimeUIFrom(m_ui, m_uiContext, out, outError);
    }
    bool ResolveRuntimeUI(const Automation::AutomationBackend::UISelector& selector,
                          Automation::AutomationBackend::UINode& out, std::string& outError) override
    {
        return Automation::ResolveRuntimeUIFrom(m_ui, m_uiContext, selector, out, outError);
    }
    bool QueueRuntimeUIClick(const Automation::AutomationBackend::UISelector& selector,
                             Automation::AutomationBackend::UIBounds& outBounds, float& outX, float& outY,
                             std::uint64_t& outSerial, std::uint64_t& outClickToken,
                             std::string& outId, std::string& outScreen, std::string& outError) override
    {
        if (!Automation::QueueRuntimeUIClickFrom(m_ui, m_uiContext, selector, outBounds, outX, outY,
                                                 outClickToken, outId, outScreen, outError)) {
            return false;
        }
        outSerial = m_uiSerial;
        return true;
    }
    void CancelRuntimeUIClick(std::uint64_t token) override
    {
        m_uiContext.CancelTaggedMouseEvents(token);
    }
    std::uint64_t RuntimeUIUpdateSerial() const override { return m_uiSerial; }

    bool PhysicsRaycast(const Automation::AutomationBackend::PhysicsVec3& origin,
                        const Automation::AutomationBackend::PhysicsVec3& direction, float maxDistance,
                        entt::entity ignore, Automation::AutomationBackend::PhysicsRaycastResult& out,
                        std::string& outError, const std::string* layerName = nullptr) override
    {
        return Automation::CastPhysicsRay(true, &m_physicsEngine, &m_physics, &m_scene, origin, direction,
                                           maxDistance, ignore, out, outError, layerName);
    }
    bool ListPhysicsBodies(std::vector<Automation::AutomationBackend::PhysicsBodySnapshot>& out,
                           std::string& outError) override
    {
        return Automation::ListPhysicsSnapshots(true, &m_physicsEngine, &m_physics, &m_scene, out, outError);
    }
    bool GetPhysicsBody(entt::entity entity, Automation::AutomationBackend::PhysicsBodySnapshot& out,
                        std::string& outError) override
    {
        return Automation::ReadPhysicsSnapshot(true, &m_physicsEngine, &m_physics, &m_scene, entity, out, outError);
    }
    bool GetCharacterState(entt::entity entity, Automation::AutomationBackend::PhysicsBodySnapshot& pose,
                           bool& grounded, std::string& outError) override
    {
        return Automation::ReadCharacterSnapshot(true, &m_physicsEngine, &m_physics, &m_scene, entity, pose,
                                                 grounded, outError, &m_characterControllers);
    }

    bool RequestViewportCapture(std::string& outError) override
    {
        return m_frameRenderer.RequestFrameCapture(outError);
    }
    bool PollViewportCapture(Automation::AutomationBackend::ViewportPixels& pixels, std::string& outError) override
    {
        Hephaestus::EngineFrameRenderer::FrameCapture frame;
        if (!m_frameRenderer.PollFrameCapture(frame, outError)) return false;
        pixels.width = frame.width;
        pixels.height = frame.height;
        pixels.frame = frame.frame;
        pixels.rgba = std::move(frame.rgba);
        return true;
    }
    void CancelViewportCapture() override { m_frameRenderer.CancelFrameCapture(); }

    void NoteUIUpdated() { ++m_uiSerial; }

private:
    Scene& m_scene;
    InputSystem& m_input;
    HephaestusUI::UISystem& m_ui;
    HephaestusUI::UIContext& m_uiContext;
    Physics::JoltPhysicsEngine& m_physicsEngine;
    Physics::JoltPhysicsSystem& m_physics;
    Physics::CharacterControllerSystem& m_characterControllers;
    Hephaestus::EngineFrameRenderer& m_frameRenderer;
    std::string m_rootDir;
    std::string m_projectName;
    std::uint64_t m_uiSerial = 0;
};

PackagedAutomation::PackagedAutomation(Scene& scene, InputSystem& input,
                                       HephaestusUI::UISystem& ui, HephaestusUI::UIContext& uiContext,
                                       Physics::JoltPhysicsEngine& physicsEngine,
                                       Physics::JoltPhysicsSystem& physics,
                                       Physics::CharacterControllerSystem& characterControllers,
                                       Hephaestus::EngineFrameRenderer& frameRenderer, std::string rootDir,
                                       std::string projectName)
    : m_backend(std::make_unique<RuntimeBackend>(scene, input, ui, uiContext, physicsEngine, physics,
                                                 characterControllers, frameRenderer, std::move(rootDir),
                                                 std::move(projectName))) {}

PackagedAutomation::~PackagedAutomation() { Stop(); }

bool PackagedAutomation::Start(int port)
{
    if (m_service) return true;
    m_service = std::make_unique<Automation::AutomationService>(*m_backend);
    Automation::RegisterPlayAutomation(*m_service);
    Automation::RegisterRuntimeAutomation(*m_service);
    Automation::RegisterLogAutomation(*m_service);
    Automation::RegisterUIAutomation(*m_service);
    Automation::RegisterPhysicsAutomation(*m_service);
    Automation::RegisterRuntimeCapture(*m_service);
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

void PackagedAutomation::NoteUIUpdated()
{
    if (m_backend) m_backend->NoteUIUpdated();
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
