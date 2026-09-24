#include "AutomationTypes.h"

namespace Hephaestus::Automation {

const char* ErrorCodeName(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::InvalidArguments: return "INVALID_ARGUMENTS";
    case ErrorCode::UnknownTool: return "UNKNOWN_TOOL";
    case ErrorCode::ToolUnavailable: return "TOOL_UNAVAILABLE";
    case ErrorCode::NoActiveScene: return "NO_ACTIVE_SCENE";
    case ErrorCode::EntityNotFound: return "ENTITY_NOT_FOUND";
    case ErrorCode::EntityRefExpired: return "ENTITY_REF_EXPIRED";
    case ErrorCode::AmbiguousEntityName: return "AMBIGUOUS_ENTITY_NAME";
    case ErrorCode::UnknownComponent: return "UNKNOWN_COMPONENT";
    case ErrorCode::UnknownProperty: return "UNKNOWN_PROPERTY";
    case ErrorCode::InvalidPropertyType: return "INVALID_PROPERTY_TYPE";
    case ErrorCode::PropertyOutOfRange: return "PROPERTY_OUT_OF_RANGE";
    case ErrorCode::ReadOnlyProperty: return "READ_ONLY_PROPERTY";
    case ErrorCode::ComponentAlreadyPresent: return "COMPONENT_ALREADY_PRESENT";
    case ErrorCode::ComponentMissing: return "COMPONENT_MISSING";
    case ErrorCode::PrefabStructuralOverrideUnsupported: return "PREFAB_STRUCTURAL_OVERRIDE_UNSUPPORTED";
    case ErrorCode::PlayModeRequired: return "PLAY_MODE_REQUIRED";
    case ErrorCode::PlayModeActive: return "PLAY_MODE_ACTIVE";
    case ErrorCode::StepInProgress: return "STEP_IN_PROGRESS";
    case ErrorCode::ServiceShuttingDown: return "SERVICE_SHUTTING_DOWN";
    case ErrorCode::Cancelled: return "CANCELLED";
    case ErrorCode::InternalError: return "INTERNAL_ERROR";
    }
    return "INTERNAL_ERROR";
}

} // namespace Hephaestus::Automation
