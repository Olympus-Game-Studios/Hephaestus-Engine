#pragma once

#include "AssetImportService.h"
#include "AutomationTypes.h"
#include "Core/PlayModeManager.h"

#include <string>
#include <string_view>
#include <vector>
#include <entt/entt.hpp>

class AssetDatabase;
class Scene;

namespace Hephaestus {
class AssetManager;
class AssetOperationService;
class AssetReferenceIndex;
}

namespace Hephaestus::Automation {

struct SceneAccess {
    Scene* scene = nullptr;
};

class AutomationBackend {
public:
    virtual ~AutomationBackend() = default;

    [[nodiscard]] virtual HostMask Host() const noexcept = 0;
    [[nodiscard]] virtual SceneAccess ActiveScene() noexcept = 0;
    [[nodiscard]] virtual PlayState GetPlayState() const noexcept = 0;

    [[nodiscard]] virtual Json GetProjectInfo() const { return Json::object(); }
    [[nodiscard]] virtual std::string ActiveScenePath() const { return {}; }
    virtual bool SaveActiveScene(std::string& outError)
    {
        outError = "Saving is unavailable for this host";
        return false;
    }
    virtual bool SaveActiveSceneAs(std::string_view, std::string& outPath, std::string& outError)
    {
        outError = "Saving as a new scene is unavailable for this host";
        return false;
    }
    virtual bool NewScene(std::string_view, std::string& outError)
    {
        outError = "Creating a scene is unavailable for this host";
        return false;
    }
    virtual bool OpenScene(std::string_view, std::string& outPath, std::string& outError)
    {
        outError = "Opening a scene is unavailable for this host";
        return false;
    }
    [[nodiscard]] virtual Json GetEditorState() const { return Json::object(); }
    [[nodiscard]] virtual std::vector<entt::entity> GetSelection() const { return {}; }
    virtual bool SetSelection(const std::vector<entt::entity>&, std::string& outError)
    {
        outError = "Selection is unavailable for this host";
        return false;
    }
    [[nodiscard]] virtual Json GetViewportState() const { return {{"available", false}}; }
    [[nodiscard]] virtual Json GetViewportCamera() const { return Json::object(); }
    virtual bool SetViewportCamera(const Json&, std::string& outError)
    {
        outError = "Viewport camera is unavailable";
        return false;
    }
    virtual bool FocusEntities(const std::vector<entt::entity>&, std::string& outError)
    {
        outError = "Viewport framing is unavailable";
        return false;
    }
    struct ViewportPixels {
        std::uint32_t width = 0, height = 0;
        std::uint64_t frame = 0;
        std::vector<std::uint8_t> rgba;
    };
    virtual bool RequestViewportCapture(std::string& outError)
    {
        outError = "Viewport capture is unavailable";
        return false;
    }
    // True means finished (either pixels or error); false means still pending.
    virtual bool PollViewportCapture(ViewportPixels&, std::string&) { return false; }
    virtual void CancelViewportCapture() {}
    virtual bool PrepareEditorMutation(std::string_view, std::string& outError)
    {
        outError = "Editor mutation is unavailable for this host";
        return false;
    }
    virtual void CompleteEditorMutation(bool) {}
    virtual void AbortEditorMutation() {}

    // ── Play Mode control (editor host) ────────────────────────────────────

    virtual bool StartPlayMode(std::string& outError)
    {
        outError = "Play Mode control is unavailable for this host";
        return false;
    }
    virtual bool StopPlayMode(std::string& outError)
    {
        outError = "Play Mode control is unavailable for this host";
        return false;
    }
    virtual bool TogglePause(std::string& outError)
    {
        outError = "Play Mode control is unavailable for this host";
        return false;
    }
    virtual bool StepFrames(std::uint32_t count, float fixedDt, std::string& outError)
    {
        outError = "Deterministic frame stepping is unavailable for this host";
        return false;
    }

    // ── Input injection (play mode) ────────────────────────────────────────

    virtual bool SetActionOverride(std::string_view action, float value,
                                   std::string& outError)
    {
        outError = "Input injection is unavailable for this host";
        return false;
    }
    virtual bool SetAxisOverride(std::string_view axis, float value,
                                 std::string& outError)
    {
        outError = "Input injection is unavailable for this host";
        return false;
    }
    virtual bool ClearInputOverrides(std::string& outError)
    {
        outError = "Input injection is unavailable for this host";
        return false;
    }

    // ── Content-authoring services (editor host) ──────────────────────────
    //
    // Narrow accessors around existing engine/editor content subsystems. They
    // default to nullptr so runtime hosts and test backends compile unchanged;
    // content-authoring tools that require a missing service return a
    // structured ToolUnavailable error rather than duplicating the subsystem.

    virtual Hephaestus::AssetManager* Assets() noexcept { return nullptr; }
    virtual Hephaestus::AssetReferenceIndex* AssetReferences() noexcept { return nullptr; }
    virtual Hephaestus::AssetOperationService* AssetOperations() noexcept { return nullptr; }
    virtual IAssetImportService* AssetImport() noexcept { return nullptr; }
    virtual AssetDatabase* ImportDatabase() noexcept { return nullptr; }

    /** Refresh editor views after an asset file operation. */
    virtual void NotifyAssetChanged() {}

    /**
     * @brief Ask the host script runtime to force-reload scripts.
     *
     * Returns false (with a message) when the host has no live script runtime,
     * for example while the editor is not in Play Mode.
     */
    virtual bool RequestScriptReload(std::string& outError)
    {
        outError = "Script reload is unavailable for this host";
        return false;
    }
};

} // namespace Hephaestus::Automation
