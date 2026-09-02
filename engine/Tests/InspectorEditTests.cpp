#include "UI/InspectorSelectionModel.h"
#include "UI/ComponentClipboard.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/PrefabComponentIO.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
    }
    return condition;
}

bool Near(float a, float b, float epsilon = 0.001f)
{
    return std::abs(a - b) <= epsilon;
}

bool TestIntersectionAndMixed()
{
    bool ok = true;
    Scene scene;
    Entity a = scene.CreateEntity("A");
    Entity b = scene.CreateEntity("B");
    Entity c = scene.CreateEntity("C");
    a.AddComponent<LightComponent>(LightComponent::CreatePoint(Vec3(1, 1, 1), 5.0f, 10.0f));
    b.AddComponent<LightComponent>(LightComponent::CreatePoint(Vec3(1, 1, 1), 8.0f, 10.0f));
    c.AddComponent<LightComponent>(LightComponent::CreatePoint(Vec3(1, 1, 1), 8.0f, 12.0f));
    a.AddComponent<MeshRendererComponent>();
    b.AddComponent<MeshRendererComponent>();
    c.AddComponent<CameraComponent>();

    std::vector<entt::entity> selected = {
        static_cast<entt::entity>(a),
        static_cast<entt::entity>(b),
        static_cast<entt::entity>(c)
    };
    auto model = Hephaestus::InspectorSelectionModel::Resolve(
        selected, static_cast<entt::entity>(a), false, entt::null, &scene.GetRegistry());
    ok &= Check(model.Count() == 3, "selection count");
    const auto shared = model.SharedComponents(scene.GetRegistry());
    ok &= Check(std::find(shared.begin(), shared.end(), "Light") != shared.end(),
                "shared Light");
    ok &= Check(std::find(shared.begin(), shared.end(), "MeshRenderer") == shared.end(),
                "MeshRenderer not shared");
    const auto partial = model.PartialComponents(scene.GetRegistry());
    ok &= Check(std::find(partial.begin(), partial.end(), "MeshRenderer") != partial.end(),
                "partial MeshRenderer");
    ok &= Check(std::find(partial.begin(), partial.end(), "Camera") != partial.end(),
                "partial Camera");

    std::vector<Vec3> positions = {
        a.GetComponent<TransformComponent>().position,
        b.GetComponent<TransformComponent>().position,
        c.GetComponent<TransformComponent>().position
    };
    auto mixedPos = Hephaestus::MixVec3(positions);
    ok &= Check(!mixedPos.x.mixed && mixedPos.x.value == 0.0, "shared X");

    a.GetComponent<TransformComponent>().position = Vec3(1, 0, 3);
    b.GetComponent<TransformComponent>().position = Vec3(2, 0, 4);
    c.GetComponent<TransformComponent>().position = Vec3(5, 0, 6);
    positions = {
        a.GetComponent<TransformComponent>().position,
        b.GetComponent<TransformComponent>().position,
        c.GetComponent<TransformComponent>().position
    };
    mixedPos = Hephaestus::MixVec3(positions);
    ok &= Check(mixedPos.x.mixed, "mixed X");
    ok &= Check(!mixedPos.y.mixed && mixedPos.y.value == 0.0, "shared Y");
    ok &= Check(mixedPos.z.mixed, "mixed Z");

    Hephaestus::WriteVec3Axis(a.GetComponent<TransformComponent>().position, 1, 2.0f);
    Hephaestus::WriteVec3Axis(b.GetComponent<TransformComponent>().position, 1, 2.0f);
    Hephaestus::WriteVec3Axis(c.GetComponent<TransformComponent>().position, 1, 2.0f);
    ok &= Check(Near(a.GetComponent<TransformComponent>().position.x, 1.0f), "X preserved");
    ok &= Check(Near(a.GetComponent<TransformComponent>().position.y, 2.0f), "Y written");
    ok &= Check(Near(b.GetComponent<TransformComponent>().position.x, 2.0f), "B X preserved");
    return ok;
}

bool TestBatchAddAndPaste()
{
    bool ok = true;
    Scene scene;
    Entity a = scene.CreateEntity("PropA");
    Entity b = scene.CreateEntity("PropB");
    Entity c = scene.CreateEntity("PropC");
    a.AddComponent<MeshRendererComponent>();
    b.AddComponent<MeshRendererComponent>();
    std::vector<entt::entity> entities = {
        static_cast<entt::entity>(a),
        static_cast<entt::entity>(b),
        static_cast<entt::entity>(c)
    };

    auto added = Hephaestus::BatchAddComponent(scene, entities, "PhysicsShape");
    ok &= Check(added.added == 3, "batch add PhysicsShape");
    auto again = Hephaestus::BatchAddComponent(scene, entities, "PhysicsShape");
    ok &= Check(again.added == 0 && again.skippedExisting == 3, "skip existing PhysicsShape");

    auto rb = Hephaestus::BatchAddComponent(scene, { static_cast<entt::entity>(c) }, "RigidBody");
    ok &= Check(rb.added == 1, "add RigidBody");
    ok &= Check(c.HasComponent<PhysicsShapeComponent>(), "RigidBody also adds PhysicsShape");

    a.AddComponent<LightComponent>(LightComponent::CreatePoint(Vec3(1, 0.5f, 0.2f), 12.0f, 7.0f));
    ok &= Check(Hephaestus::ComponentClipboard::Instance().CopyFrom(a, "Light"), "copy Light");
    int pasted = Hephaestus::ComponentClipboard::Instance().PasteComponent(
        scene, { static_cast<entt::entity>(b), static_cast<entt::entity>(c) });
    ok &= Check(pasted == 2, "paste Light onto two entities");
    ok &= Check(b.HasComponent<LightComponent>(), "B received Light");
    ok &= Check(Near(b.GetComponent<LightComponent>().intensity, 12.0f), "pasted intensity");
    ok &= Check(Near(b.GetComponent<LightComponent>().range, 7.0f), "pasted range");

    b.GetComponent<LightComponent>().intensity = 1.0f;
    int values = Hephaestus::ComponentClipboard::Instance().PasteValues(
        scene, { static_cast<entt::entity>(b) });
    ok &= Check(values == 1, "paste values");
    ok &= Check(Near(b.GetComponent<LightComponent>().intensity, 12.0f), "values restored intensity");
    return ok;
}

bool TestAudioSourceIo()
{
    bool ok = true;
    Scene scene;
    Entity entity = scene.CreateEntity("Speaker");
    auto add = Hephaestus::BatchAddComponent(
        scene, { static_cast<entt::entity>(entity) }, "AudioSource");
    ok &= Check(add.added == 1, "add AudioSource");
    ok &= Check(entity.HasComponent<AudioSourceComponent>(), "has AudioSource");
    auto& audio = entity.GetComponent<AudioSourceComponent>();
    audio.clipPath = "assets/audio/hum.wav";
    audio.loop = true;
    audio.volume = 0.4f;

    ok &= Check(Hephaestus::ComponentClipboard::Instance().CopyFrom(entity, "AudioSource"),
                "copy AudioSource");
    Entity other = scene.CreateEntity("Speaker2");
    int pasted = Hephaestus::ComponentClipboard::Instance().PasteComponent(
        scene, { static_cast<entt::entity>(other) });
    ok &= Check(pasted == 1, "paste AudioSource");
    ok &= Check(other.HasComponent<AudioSourceComponent>(), "other has AudioSource");
    if (other.HasComponent<AudioSourceComponent>()) {
        const auto& copied = other.GetComponent<AudioSourceComponent>();
        ok &= Check(copied.clipPath == "assets/audio/hum.wav", "clip path");
        ok &= Check(copied.loop, "loop");
        ok &= Check(Near(copied.volume, 0.4f), "volume");
    }

    PrefabComponentEntry entry;
    ok &= Check(PrefabComponentIO::CaptureComponent(entity, "AudioSource", entry),
                "capture AudioSource");
    Entity third = scene.CreateEntity("Speaker3");
    ok &= Check(PrefabComponentIO::ApplyEntry(third, entry), "apply AudioSource entry");
    ok &= Check(third.HasComponent<AudioSourceComponent>(), "third has AudioSource");
    return ok;
}

bool TestBatchRemove()
{
    bool ok = true;
    Scene scene;
    Entity a = scene.CreateEntity("MeshA");
    Entity b = scene.CreateEntity("MeshB");
    for (Entity entity : {a, b}) {
        entity.AddComponent<MeshRendererComponent>();
        entity.AddComponent<MaterialComponent>();
    }

    const std::vector<entt::entity> entities = {
        static_cast<entt::entity>(a),
        static_cast<entt::entity>(b)
    };
    const auto removed = Hephaestus::BatchRemoveComponent(scene, entities, "MeshRenderer");
    ok &= Check(removed.added == 2, "batch remove MeshRenderer");
    ok &= Check(!a.HasComponent<MeshRendererComponent>() && !b.HasComponent<MeshRendererComponent>(),
                "removed MeshRenderer from all entities");
    ok &= Check(!a.HasComponent<MaterialComponent>() && !b.HasComponent<MaterialComponent>(),
                "removed companion Material components");
    return ok;
}

bool TestPinnedMissing()
{
    bool ok = true;
    Scene scene;
    Entity a = scene.CreateEntity("A");
    auto model = Hephaestus::InspectorSelectionModel::Resolve(
        { static_cast<entt::entity>(a) }, static_cast<entt::entity>(a),
        true, static_cast<entt::entity>(999), &scene.GetRegistry());
    ok &= Check(model.pinnedMissing, "invalid pin is missing");
    ok &= Check(model.Empty(), "missing pin inspects nothing");
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= TestIntersectionAndMixed();
    ok &= TestBatchAddAndPaste();
    ok &= TestAudioSourceIo();
    ok &= TestBatchRemove();
    ok &= TestPinnedMissing();
    if (!ok) {
        std::cerr << "InspectorEditTests failed.\n";
        return 1;
    }
    std::cout << "InspectorEditTests passed.\n";
    return 0;
}
