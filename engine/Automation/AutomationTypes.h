#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace Hephaestus::Automation {

using Json = nlohmann::json;
using RequestToken = std::uint64_t;

enum class HostMask : std::uint8_t {
    None = 0,
    Editor = 1,
    Runtime = 2,
    Any = 3,
};

constexpr HostMask operator|(HostMask left, HostMask right)
{
    return static_cast<HostMask>(static_cast<std::uint8_t>(left) |
                                 static_cast<std::uint8_t>(right));
}

constexpr bool HasHost(HostMask mask, HostMask host)
{
    return (static_cast<std::uint8_t>(mask) & static_cast<std::uint8_t>(host)) != 0;
}

enum class PlayRequirement : std::uint8_t {
    Any,
    EditorOnly,
    PlayModeActive,
    PlayingOnly,
};

enum class ErrorCode : std::uint16_t {
    InvalidArguments,
    UnknownTool,
    ToolUnavailable,
    NoActiveScene,
    EntityNotFound,
    EntityRefExpired,
    AmbiguousEntityName,
    UnknownComponent,
    UnknownProperty,
    InvalidPropertyType,
    PropertyOutOfRange,
    ReadOnlyProperty,
    ComponentAlreadyPresent,
    ComponentMissing,
    PrefabStructuralOverrideUnsupported,
    PlayModeRequired,
    PlayModeActive,
    StepInProgress,
    ServiceShuttingDown,
    Cancelled,
    InternalError,
};

const char* ErrorCodeName(ErrorCode code) noexcept;

struct AutomationError {
    ErrorCode code = ErrorCode::InternalError;
    std::string message;
    Json data = Json::object();
};

struct ToolResult {
    Json value = Json::object();
    std::optional<AutomationError> error;

    [[nodiscard]] bool Ok() const noexcept { return !error.has_value(); }

    static ToolResult Success(Json value = Json::object())
    {
        return ToolResult{std::move(value), std::nullopt};
    }

    static ToolResult Failure(ErrorCode code, std::string message,
                              Json data = Json::object())
    {
        return ToolResult{Json::object(), AutomationError{code, std::move(message), std::move(data)}};
    }
};

struct ToolCall {
    std::string name;
    Json arguments = Json::object();
};

} // namespace Hephaestus::Automation
