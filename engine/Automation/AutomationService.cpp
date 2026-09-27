#include "AutomationService.h"

#include <cassert>
#include <exception>
#include <utility>

namespace Hephaestus::Automation {
namespace {

ToolResult PlayRequirementFailure(PlayRequirement requirement)
{
    if (requirement == PlayRequirement::EditorOnly) {
        return ToolResult::Failure(ErrorCode::PlayModeActive,
                                   "Tool is unavailable while Play Mode is active");
    }
    return ToolResult::Failure(ErrorCode::PlayModeRequired,
                               "Tool requires active Play Mode");
}

bool MeetsPlayRequirement(PlayRequirement requirement, PlayState state)
{
    switch (requirement) {
    case PlayRequirement::Any: return true;
    case PlayRequirement::EditorOnly: return state == PlayState::Editor;
    case PlayRequirement::PlayModeActive: return state != PlayState::Editor;
    case PlayRequirement::PlayingOnly: return state == PlayState::Playing;
    }
    return false;
}

ToolResult ShuttingDownResult()
{
    return ToolResult::Failure(ErrorCode::ServiceShuttingDown,
                               "Automation service is shutting down");
}

} // namespace

AutomationService::AutomationService(AutomationBackend& backend)
    : m_Backend(backend), m_MainThread(std::this_thread::get_id())
{
}

AutomationService::~AutomationService()
{
    Shutdown();
}

Plugins::ToolFuture AutomationService::PostCall(ToolCall call)
{
    PostedCall posted = Post(std::move(call));
    return std::move(posted.result);
}

PostedCall AutomationService::Post(ToolCall call)
{
    PendingCall pending;
    std::future<ToolResult> future = pending.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        pending.token = m_NextToken++;
        if (!m_Accepting.load()) {
            pending.promise.set_value(ShuttingDownResult());
            return PostedCall{pending.token, std::move(future)};
        }
        pending.call = std::move(call);
        const RequestToken token = pending.token;
        m_Queue.push_back(std::move(pending));
        return PostedCall{token, std::move(future)};
    }
}

ToolResult AutomationService::ValidateCall(const ToolCall& call,
                                           const ToolDefinition*& outDefinition) const
{
    outDefinition = m_Registry.Find(call.name);
    if (!outDefinition) {
        return ToolResult::Failure(ErrorCode::UnknownTool,
                                   "Unknown automation tool: " + call.name);
    }
    if (!HasHost(outDefinition->hostMask, m_Backend.Host())) {
        return ToolResult::Failure(ErrorCode::ToolUnavailable,
                                   "Tool is unavailable for this host");
    }
    if (!MeetsPlayRequirement(outDefinition->playRequirement, m_Backend.GetPlayState())) {
        return PlayRequirementFailure(outDefinition->playRequirement);
    }
    if (!call.arguments.is_object()) {
        return ToolResult::Failure(ErrorCode::InvalidArguments,
                                   "Tool arguments must be a JSON object");
    }
    return ToolResult::Success();
}

std::size_t AutomationService::Tick()
{
    assert(std::this_thread::get_id() == m_MainThread);
    ++m_FrameIndex;
    m_Entities.ObserveActiveScene(m_Backend.ActiveScene().scene);

    std::deque<PendingCall> calls;
    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        calls.swap(m_Queue);
    }

    ToolContext context(m_Backend, m_Entities, m_FrameIndex);
    std::size_t completed = 0;
    for (auto& pending : calls) {
        const ToolDefinition* definition = nullptr;
        ToolResult validation = ValidateCall(pending.call, definition);
        if (!validation.Ok()) {
            pending.promise.set_value(std::move(validation));
            ++completed;
            continue;
        }

        try {
            ToolOutcome outcome = definition->handler(context, pending.call.arguments);
            if (auto* result = std::get_if<ToolResult>(&outcome)) {
                pending.promise.set_value(std::move(*result));
                ++completed;
            } else {
                auto operation = std::move(std::get<std::unique_ptr<DeferredOperation>>(outcome));
                if (!operation) {
                    pending.promise.set_value(ToolResult::Failure(
                        ErrorCode::InternalError, "Tool returned an empty deferred operation"));
                    ++completed;
                } else {
                    m_Deferred.push_back(PendingDeferred{
                        pending.token, std::move(operation), std::move(pending.promise)});
                }
            }
        } catch (const std::exception& exception) {
            pending.promise.set_value(ToolResult::Failure(ErrorCode::InternalError,
                                                          exception.what()));
            ++completed;
        } catch (...) {
            pending.promise.set_value(ToolResult::Failure(ErrorCode::InternalError,
                                                          "Unhandled tool exception"));
            ++completed;
        }
    }

    for (std::size_t index = 0; index < m_Deferred.size();) {
        try {
            std::optional<ToolResult> result = m_Deferred[index].operation->Poll(context);
            if (!result) {
                ++index;
                continue;
            }
            m_Deferred[index].promise.set_value(std::move(*result));
        } catch (const std::exception& exception) {
            m_Deferred[index].promise.set_value(
                ToolResult::Failure(ErrorCode::InternalError, exception.what()));
        } catch (...) {
            m_Deferred[index].promise.set_value(
                ToolResult::Failure(ErrorCode::InternalError, "Unhandled deferred operation exception"));
        }
        m_Deferred.erase(m_Deferred.begin() + static_cast<std::ptrdiff_t>(index));
        ++completed;
    }
    return completed;
}

void AutomationService::Shutdown()
{
    if (!m_Accepting.exchange(false)) return;

    std::deque<PendingCall> calls;
    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        calls.swap(m_Queue);
    }
    for (auto& pending : calls) {
        pending.promise.set_value(ShuttingDownResult());
    }
    for (auto& pending : m_Deferred) {
        pending.operation->Cancel();
        pending.promise.set_value(ToolResult::Failure(ErrorCode::Cancelled,
                                                      "Automation request was cancelled"));
    }
    m_Deferred.clear();
    m_Entities.Reset();
}

} // namespace Hephaestus::Automation
