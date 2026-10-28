#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/PrefabComponentIO.h"
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

} // namespace

int main() {
    bool pass = true;

    pass &= Check(Near(LightComponent().sunAngularDiameter, 1.0f), "a new light has a 1 degree sun");

    {
        Scene scene("RoundTrip");
        Entity sun = scene.CreateEntity("Sun");
        auto& light = sun.AddComponent<LightComponent>(LightComponent::CreateDirectional(Vec3(0, -1, 0)));
        light.sunAngularDiameter = 3.5f;
        light.castShadows = false;

        const std::string saved = SceneSerializer(scene).SerializeToString();
        Scene loaded("Loaded");
        pass &= Check(SceneSerializer::DeserializeFromString(loaded, saved), "scene round trip loads");
        int count = 0;
        for (auto handle : loaded.GetRegistry().view<LightComponent>()) {
            const auto& copy = loaded.GetRegistry().get<LightComponent>(handle);
            ++count;
            pass &= Check(Near(copy.sunAngularDiameter, 3.5f) && !copy.castShadows,
                          "scene round trip keeps the sun size and cast shadows flag");
        }
        pass &= Check(count == 1, "one light after the round trip");

        PrefabComponentEntry entry;
        pass &= Check(PrefabComponentIO::CaptureComponent(sun, "Light", entry), "prefab captures the light");
        Entity target = loaded.CreateEntity("Target");
        target.AddComponent<LightComponent>();
        pass &= Check(PrefabComponentIO::ApplyEntry(target, entry) &&
                          Near(target.GetComponent<LightComponent>().sunAngularDiameter, 3.5f),
                      "prefab data keeps the sun size");
    }

    {
        // Scenes saved before the sun size existed load with the default.
        const std::string legacy = R"({
  "name": "Legacy",
  "entities": [
    { "id": 1, "name": "Sun", "parent": null, "components": {
      "Light": { "type": "Directional", "shadow": { "castShadows": true, "maxDistance": 200 } } } }
  ]
})";
        Scene loaded("Legacy");
        pass &= Check(SceneSerializer::DeserializeFromString(loaded, legacy), "legacy scene loads");
        for (auto handle : loaded.GetRegistry().view<LightComponent>()) {
            const auto& light = loaded.GetRegistry().get<LightComponent>(handle);
            pass &= Check(Near(light.sunAngularDiameter, 1.0f) && Near(light.shadowMaxDistance, 200.0f),
                          "a scene without a sun size keeps the default");
        }
    }

    if (pass) {
        std::cout << "LightShadowSettingsTests passed\n";
    }
    return pass ? 0 : 1;
}
