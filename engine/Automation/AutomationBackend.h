#pragma once

#include "AutomationTypes.h"
#include "Core/PlayModeManager.h"

#include <string>
#include <string_view>

class Scene;

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
    virtual bool PrepareEditorMutation(std::string_view, std::string& outError)
    {
        outError = "Editor mutation is unavailable for this host";
        return false;
    }
    virtual void CompleteEditorMutation(bool) {}
    virtual void AbortEditorMutation() {}
};

} // namespace Hephaestus::Automation
