#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 1.0e-4f;
}

const EnvironmentComponent* FirstEnvironment(Scene& scene) {
    for (auto handle : scene.GetRegistry().view<EnvironmentComponent>()) {
        return &scene.GetRegistry().get<EnvironmentComponent>(handle);
    }
    return nullptr;
}

} // namespace

int main() {
    bool pass = true;

    {
        const EnvironmentComponent::PostProcess post;
        pass &= Check(Near(post.exposure, 0.0f) && Near(post.contrast, 1.0f) && Near(post.saturation, 1.0f) &&
                          Near(post.colorFilter.x, 1.0f) && Near(post.colorFilter.y, 1.0f) &&
                          Near(post.colorFilter.z, 1.0f) && Near(post.vignette, 0.0f) && Near(post.grain, 0.0f) &&
                          Near(post.scanlines, 0.0f) && Near(post.bloomIntensity, 0.0f),
                      "a new environment has no post processing");
    }

    {
        Scene scene("RoundTrip");
        Entity entity = scene.CreateEntity("Environment");
        auto& env = entity.AddComponent<EnvironmentComponent>();
        env.post.exposure = -0.5f;
        env.post.contrast = 1.3f;
        env.post.saturation = 0.8f;
        env.post.colorFilter = Vec3(1.0f, 0.8f, 0.6f);
        env.post.vignette = 0.7f;
        env.post.grain = 0.25f;
        env.post.scanlines = 0.1f;
        env.post.bloomIntensity = 1.5f;
        env.post.bloomThreshold = 2.0f;
        env.post.bloomRadius = 0.4f;

        const std::string saved = SceneSerializer(scene).SerializeToString();
        Scene loaded("Loaded");
        pass &= Check(SceneSerializer::DeserializeFromString(loaded, saved), "scene round trip loads");
        const EnvironmentComponent* copy = FirstEnvironment(loaded);
        pass &= Check(copy != nullptr, "the environment survives the round trip");
        if (copy) {
            const auto& post = copy->post;
            pass &= Check(Near(post.exposure, -0.5f) && Near(post.contrast, 1.3f) && Near(post.saturation, 0.8f) &&
                              Near(post.colorFilter.y, 0.8f) && Near(post.colorFilter.z, 0.6f) &&
                              Near(post.vignette, 0.7f) && Near(post.grain, 0.25f) && Near(post.scanlines, 0.1f) &&
                              Near(post.bloomIntensity, 1.5f) && Near(post.bloomThreshold, 2.0f) &&
                              Near(post.bloomRadius, 0.4f),
                          "the round trip keeps every post-processing value");
        }
    }

    {
        // Scenes saved before post processing existed load without any.
        const std::string legacy = R"({
  "name": "Legacy",
  "entities": [
    { "id": 1, "name": "Environment", "parent": null, "components": {
      "Environment": { "type": "SolidColor", "solidColor": [0.1, 0.2, 0.3] } } }
  ]
})";
        Scene loaded("Legacy");
        pass &= Check(SceneSerializer::DeserializeFromString(loaded, legacy), "legacy scene loads");
        const EnvironmentComponent* env = FirstEnvironment(loaded);
        pass &= Check(env && Near(env->post.contrast, 1.0f) && Near(env->post.vignette, 0.0f),
                      "a scene without post processing keeps the defaults");
    }

    if (pass) {
        std::cout << "PostProcessSettingsTests passed\n";
    }
    return pass ? 0 : 1;
}
