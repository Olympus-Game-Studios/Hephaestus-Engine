#include "ViewportAutomation.h"
#include "AutomationService.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../extern/glfw/deps/stb_image_write.h"

#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace Hephaestus::Automation {
namespace {
void Add(AutomationService& service, std::string name, Json schema, ToolHandler handler)
{
    ToolDefinition tool;
    tool.name = std::move(name);
    tool.description = "Inspect or control the editor viewport.";
    tool.inputSchema = std::move(schema);
    tool.hostMask = HostMask::Editor;
    // Play Mode may drive its own runtime camera. Do not advertise editor-camera
    // controls as control of the rendered camera during that mode.
    if (tool.name == "viewport.set_camera" || tool.name == "viewport.focus_entity" ||
        tool.name == "viewport.focus_selection" || tool.name == "viewport.get_camera")
        tool.playRequirement = PlayRequirement::EditorOnly;
    tool.handler = std::move(handler);
    std::string error;
    if (!service.Registry().Register(std::move(tool), error)) throw std::runtime_error(error);
}
bool Available(ToolContext& context) { return context.Backend().GetViewportState().value("available", false); }
ToolResult Unavailable() { return ToolResult::Failure(ErrorCode::ToolUnavailable, "ViewportUnavailable: viewport is not ready"); }
class CaptureOperation final : public DeferredOperation {
public:
    explicit CaptureOperation(std::string prefix) : m_Prefix(std::move(prefix)) {}

    std::optional<ToolResult> Poll(ToolContext& ctx) override
    {
        if (firstFrame == 0) firstFrame = ctx.FrameIndex();
        AutomationBackend::ViewportPixels pixels;
        std::string error;
        if (!ctx.Backend().PollViewportCapture(pixels, error)) {
            if (ctx.FrameIndex() - firstFrame <= 300) return std::nullopt;
            ctx.Backend().CancelViewportCapture();
            return ToolResult::Failure(ErrorCode::ToolUnavailable,
                                       "NoRenderedFrame: capture did not complete within 300 editor ticks");
        }
        if (!error.empty()) return ToolResult::Failure(ErrorCode::ToolUnavailable, std::move(error));
        if (!pixels.width || !pixels.height ||
            pixels.rgba.size() != static_cast<size_t>(pixels.width) * pixels.height * 4)
            return ToolResult::Failure(ErrorCode::InternalError, "Invalid completed framebuffer dimensions");
        const Json project = ctx.Backend().GetProjectInfo();
        if (!project.contains("rootDir") || !project["rootDir"].is_string() ||
            project["rootDir"].get<std::string>().empty())
            return ToolResult::Failure(ErrorCode::ToolUnavailable, "An open project is required for captures");
        namespace fs = std::filesystem;
        const fs::path root = fs::u8path(project["rootDir"].get<std::string>());
        const fs::path relative = fs::path(".hephaestus") / "automation" / "captures" /
            (m_Prefix + "-" + std::to_string(pixels.frame) + "-" + std::to_string(ctx.FrameIndex()) + ".png");
        std::error_code ec;
        fs::create_directories(root / relative.parent_path(), ec);
        if (ec) return ToolResult::Failure(ErrorCode::ToolUnavailable, "Cannot create capture directory: " + ec.message());
        const fs::path absolute = root / relative;
        if (!stbi_write_png(absolute.string().c_str(), static_cast<int>(pixels.width),
                            static_cast<int>(pixels.height), 4, pixels.rgba.data(),
                            static_cast<int>(pixels.width * 4)))
            return ToolResult::Failure(ErrorCode::ToolUnavailable, "Failed to encode viewport PNG");
        return ToolResult::Success({{"width", pixels.width}, {"height", pixels.height},
                                    {"format", "png"}, {"path", relative.generic_string()},
                                    {"frame", pixels.frame}});
    }
private:
    std::string m_Prefix;
    std::uint64_t firstFrame = 0;
};
}
void RegisterViewportAutomation(AutomationService& service)
{
    const Json empty = {{"type", "object"}, {"properties", Json::object()}, {"additionalProperties", false}};
    Add(service, "viewport.get_state", empty, [](ToolContext& ctx, const Json&) -> ToolOutcome {
        Json state = ctx.Backend().GetViewportState();
        state["selectedEntityCount"] = ctx.Backend().GetSelection().size();
        if (state.value("available", false) && ctx.Backend().GetPlayState() == PlayState::Editor)
            state["camera"] = ctx.Backend().GetViewportCamera();
        return ToolResult::Success(std::move(state));
    });
    Add(service, "viewport.get_camera", empty, [](ToolContext& ctx, const Json&) -> ToolOutcome {
        if (!Available(ctx)) return Unavailable();
        return ToolResult::Success(ctx.Backend().GetViewportCamera());
    });
    Add(service, "viewport.set_camera", {{"type", "object"}, {"properties", {
        {"position", {{"type", "array"}, {"minItems", 3}, {"maxItems", 3}}},
        {"yawDeg", {{"type", "number"}}}, {"pitchDeg", {{"type", "number"}}},
        {"fovDeg", {{"type", "number"}}}}}, {"additionalProperties", false}},
        [](ToolContext& ctx, const Json& args) -> ToolOutcome {
            if (!Available(ctx)) return Unavailable();
            if (args.contains("position")) {
                const Json& pos = args["position"];
                if (!pos.is_array() || pos.size() != 3) return ToolResult::Failure(ErrorCode::InvalidArguments, "position must be three finite numbers");
                for (const auto& coordinate : pos)
                    if (!coordinate.is_number() || !std::isfinite(coordinate.get<double>()) ||
                        std::abs(coordinate.get<double>()) > std::numeric_limits<float>::max())
                        return ToolResult::Failure(ErrorCode::InvalidArguments, "position must be finite");
            }
            for (const char* key : {"yawDeg", "pitchDeg", "fovDeg"})
                if (args.contains(key) && (!args[key].is_number() || !std::isfinite(args[key].get<double>())))
                    return ToolResult::Failure(ErrorCode::InvalidArguments, std::string(key) + " must be finite");
            if ((args.contains("fovDeg") && (args["fovDeg"].get<double>() <= 1 || args["fovDeg"].get<double>() >= 179)) ||
                (args.contains("pitchDeg") && std::abs(args["pitchDeg"].get<double>()) > 89) ||
                (args.contains("yawDeg") && std::abs(args["yawDeg"].get<double>()) > 360))
                return ToolResult::Failure(ErrorCode::InvalidArguments, "FOV or orientation is outside the camera range");
            std::string error;
            if (!ctx.Backend().SetViewportCamera(args, error)) return ToolResult::Failure(ErrorCode::ToolUnavailable, std::move(error));
            return ToolResult::Success(ctx.Backend().GetViewportCamera());
        });
    Add(service, "viewport.focus_entity", {{"type", "object"}, {"properties", {{"entity", {{"type", "string"}}}}},
        {"required", Json::array({"entity"})}, {"additionalProperties", false}},
        [](ToolContext& ctx, const Json& args) -> ToolOutcome {
            if (!Available(ctx)) return Unavailable();
            Scene* scene = ctx.Backend().ActiveScene().scene;
            if (!scene) return ToolResult::Failure(ErrorCode::NoActiveScene, "No active scene");
            if (!args.contains("entity") || !args["entity"].is_string()) return ToolResult::Failure(ErrorCode::InvalidArguments, "entity must be an EntityRef");
            const auto entity = ctx.Entities().Resolve(EntityRef{args["entity"].get<std::string>()}, scene);
            if (!entity.IsResolved()) return ToolResult::Failure(ErrorCode::EntityRefExpired, "Entity reference is unknown, stale, or destroyed");
            std::string error;
            if (!ctx.Backend().FocusEntities({entity.handle}, error)) return ToolResult::Failure(ErrorCode::ToolUnavailable, std::move(error));
            return ToolResult::Success(ctx.Backend().GetViewportCamera());
        });
    Add(service, "viewport.focus_selection", empty, [](ToolContext& ctx, const Json&) -> ToolOutcome {
        if (!Available(ctx)) return Unavailable();
        const auto selected = ctx.Backend().GetSelection();
        if (selected.empty()) return ToolResult::Failure(ErrorCode::InvalidArguments, "Select at least one entity before framing the selection");
        std::string error;
        if (!ctx.Backend().FocusEntities(selected, error)) return ToolResult::Failure(ErrorCode::ToolUnavailable, std::move(error));
        return ToolResult::Success(ctx.Backend().GetViewportCamera());
    });
    Add(service, "viewport.capture", empty, [](ToolContext& ctx, const Json&) -> ToolOutcome {
        if (!Available(ctx)) return Unavailable();
        std::string error;
        if (!ctx.Backend().RequestViewportCapture(error))
            return ToolResult::Failure(ErrorCode::ToolUnavailable, std::move(error));
        return std::make_unique<CaptureOperation>("viewport");
    });
}

void RegisterRuntimeCapture(AutomationService& service)
{
    const Json empty = {{"type", "object"}, {"properties", Json::object()}, {"additionalProperties", false}};
    ToolDefinition tool;
    tool.name = "runtime.capture";
    tool.description = "Capture the current rendered frame as a PNG during Play Mode or in the packaged runtime.";
    tool.inputSchema = empty;
    tool.hostMask = HostMask::Editor | HostMask::Runtime;
    tool.playRequirement = PlayRequirement::PlayModeActive;
    tool.mutatesProject = false;
    tool.handler = [](ToolContext& context, const Json&) -> ToolOutcome {
        std::string error;
        if (!context.Backend().RequestViewportCapture(error)) {
            return ToolResult::Failure(ErrorCode::ToolUnavailable, std::move(error));
        }
        return std::make_unique<CaptureOperation>("runtime");
    };
    std::string error;
    if (!service.Registry().Register(std::move(tool), error)) throw std::runtime_error(error);
}
} // namespace Hephaestus::Automation
