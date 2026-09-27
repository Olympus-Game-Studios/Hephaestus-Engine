#pragma once

#include "AutomationBackend.h"
#include "EntityRef.h"
#include "ToolRegistry.h"
#include "PluginAPI/PluginFirstParty.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Hephaestus::Automation {

class ToolContext {
public:
    ToolContext(AutomationBackend& backend, EntityRefTable& entities,
                std::uint64_t frameIndex)
        : m_Backend(backend), m_Entities(entities), m_FrameIndex(frameIndex)
    {
    }

    AutomationBackend& Backend() noexcept { return m_Backend; }
    EntityRefTable& Entities() noexcept { return m_Entities; }
    [[nodiscard]] std::uint64_t FrameIndex() const noexcept { return m_FrameIndex; }

private:
    AutomationBackend& m_Backend;
    EntityRefTable& m_Entities;
    std::uint64_t m_FrameIndex = 0;
};

struct PostedCall {
    RequestToken token = 0;
    std::future<ToolResult> result;
};

class AutomationService : public Plugins::IAutomationService {
public:
    explicit AutomationService(AutomationBackend& backend);
    ~AutomationService() override;

    AutomationService(const AutomationService&) = delete;
    AutomationService& operator=(const AutomationService&) = delete;

    ToolRegistry& Registry() noexcept { return m_Registry; }
    const ToolRegistry& Registry() const noexcept { return m_Registry; }
    EntityRefTable& Entities() noexcept { return m_Entities; }

    // ── Plugins::IAutomationService ────────────────────────────────────────
    [[nodiscard]] const ToolRegistry& GetRegistry() const override { return m_Registry; }
    [[nodiscard]] HostMask GetHost() const override { return m_Backend.Host(); }
    [[nodiscard]] PlayState GetPlayState() const override { return m_Backend.GetPlayState(); }
    Plugins::ToolFuture PostCall(ToolCall call) override;

    PostedCall Post(ToolCall call);
    std::size_t Tick();
    void Shutdown();

    [[nodiscard]] bool IsAcceptingCalls() const noexcept { return m_Accepting.load(); }
    [[nodiscard]] std::uint64_t FrameIndex() const noexcept { return m_FrameIndex; }
    [[nodiscard]] HostMask CurrentHost() const noexcept { return m_Backend.Host(); }
    [[nodiscard]] PlayState CurrentPlayState() const noexcept { return m_Backend.GetPlayState(); }

private:
    struct PendingCall {
        RequestToken token = 0;
        ToolCall call;
        std::promise<ToolResult> promise;
    };

    struct PendingDeferred {
        RequestToken token = 0;
        std::unique_ptr<DeferredOperation> operation;
        std::promise<ToolResult> promise;
    };

    ToolResult ValidateCall(const ToolCall& call, const ToolDefinition*& outDefinition) const;

    AutomationBackend& m_Backend;
    ToolRegistry m_Registry;
    EntityRefTable m_Entities;
    std::thread::id m_MainThread;
    std::atomic<bool> m_Accepting{true};
    mutable std::mutex m_QueueMutex;
    std::deque<PendingCall> m_Queue;
    std::vector<PendingDeferred> m_Deferred;
    RequestToken m_NextToken = 1;
    std::uint64_t m_FrameIndex = 0;
};

} // namespace Hephaestus::Automation
