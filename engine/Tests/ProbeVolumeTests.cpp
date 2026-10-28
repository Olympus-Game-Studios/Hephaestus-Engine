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

} // namespace

int main() {
    bool pass = true;

    {
        ProbeVolumeComponent volume;
        volume.size = Vec3(10.0f, 4.0f, 6.0f);
        volume.probeCountX = 3;
        volume.probeCountY = 1;
        volume.probeCountZ = 2;
        const Vec3 first = volume.ProbeOffset(0, 0, 0);
        const Vec3 last = volume.ProbeOffset(2, 0, 1);
        const Vec3 middle = volume.ProbeOffset(1, 0, 0);
        pass &= Check(Near(first.x, -5.0f) && Near(first.z, -3.0f), "first probe sits on the minimum corner");
        pass &= Check(Near(last.x, 5.0f) && Near(last.z, 3.0f), "last probe sits on the maximum corner");
        pass &= Check(Near(middle.x, 0.0f), "odd counts put a probe on the centre");
        pass &= Check(Near(first.y, 0.0f), "a single probe on an axis sits at the centre");
    }

    {
        Scene scene("RoundTrip");
        Entity entity = scene.CreateEntity("GI");
        auto& volume = entity.AddComponent<ProbeVolumeComponent>();
        volume.enabled = false;
        volume.size = Vec3(24.0f, 6.5f, 12.0f);
        volume.probeCountX = 16;
        volume.probeCountY = 5;
        volume.probeCountZ = 9;
        volume.intensity = 1.75f;
        volume.bakePath = "assets/scenes/RoundTrip.GI.hpprobes";
        volume.hardwareRayTracing = true;
        scene.CreateEntity("Plain");

        const std::string saved = SceneSerializer(scene).SerializeToString();
        pass &= Check(saved.find("\"ProbeVolume\"") != std::string::npos, "scene writes the probe volume");
        Scene loaded("Loaded");
        pass &= Check(SceneSerializer::DeserializeFromString(loaded, saved), "scene round trip loads");
        int count = 0;
        for (auto handle : loaded.GetRegistry().view<ProbeVolumeComponent>()) {
            const auto& copy = loaded.GetRegistry().get<ProbeVolumeComponent>(handle);
            ++count;
            pass &= Check(!copy.enabled && Near(copy.size.x, 24.0f) && Near(copy.size.y, 6.5f) &&
                              Near(copy.size.z, 12.0f) && copy.probeCountX == 16 && copy.probeCountY == 5 &&
                              copy.probeCountZ == 9 && Near(copy.intensity, 1.75f) &&
                              copy.bakePath == "assets/scenes/RoundTrip.GI.hpprobes" && copy.hardwareRayTracing,
                          "round trip keeps every probe volume field");
        }
        pass &= Check(count == 1, "only the authored entity gets a probe volume");
    }

    {
        Scene loaded("Clamped");
        const std::string json = R"({ "name": "Clamped", "entities": [ { "id": 1, "name": "GI", "parent": null,
            "components": { "ProbeVolume": { "probeCounts": [0, -3, 4] } } } ] })";
        pass &= Check(SceneSerializer::DeserializeFromString(loaded, json), "hand-written probe volume loads");
        pass &= Check(loaded.GetRegistry().view<ProbeVolumeComponent>().size() == 1,
                      "hand-written scene has one probe volume");
        for (auto handle : loaded.GetRegistry().view<ProbeVolumeComponent>()) {
            const auto& copy = loaded.GetRegistry().get<ProbeVolumeComponent>(handle);
            pass &= Check(copy.probeCountX == 1 && copy.probeCountY == 1 && copy.probeCountZ == 4,
                          "probe counts below one load as one");
            pass &= Check(copy.enabled && Near(copy.size.x, 10.0f) && Near(copy.intensity, 1.0f) &&
                              !copy.hardwareRayTracing,
                          "missing fields keep their defaults");
        }
    }

    if (pass) {
        std::cout << "ProbeVolumeTests passed\n";
    }
    return pass ? 0 : 1;
}
