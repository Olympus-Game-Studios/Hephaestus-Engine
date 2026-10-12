#include "Physics/CharacterControllerMath.h"
#include "Scene/CharacterControllerComponent.h"
#include "Scene/Entity.h"
#include "Scene/Json.h"
#include "Scene/PrefabAsset.h"
#include "Scene/PrefabComponentIO.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    std::cout << "[PASS] " << message << "\n";
    return true;
}

bool Near(float a, float b, float tolerance = 1.0e-3f)
{
    return std::fabs(a - b) <= tolerance;
}

Physics::VelocityStepInput GroundStep()
{
    Physics::VelocityStepInput step;
    step.ground = Physics::CharacterGroundState::OnGround;
    step.gravity = Vec3(0.0f, -9.81f, 0.0f);
    step.fixedDt = 1.0f / 60.0f;
    return step;
}

} // namespace

int main()
{
    bool pass = true;

    const Physics::PlanarWish zero = Physics::NormalizePlanarWish(0.0f, 0.0f);
    pass &= Check(zero.x == 0.0f && zero.z == 0.0f, "Zero wish stays zero");
    const Physics::PlanarWish diagonal = Physics::NormalizePlanarWish(3.0f, 4.0f);
    pass &= Check(Near(std::sqrt(diagonal.x * diagonal.x + diagonal.z * diagonal.z), 1.0f),
                  "Diagonal wish has length 1");
    const Physics::PlanarWish projected = Physics::ProjectPlanarWish(0.0f, 9.0f, 2.0f);
    pass &= Check(projected.x == 0.0f && Near(projected.z, 1.0f), "Project wish drops Y");

    {
        Physics::VelocityStepInput step = GroundStep();
        step.wish = Physics::NormalizePlanarWish(1.0f, 0.0f);
        step.targetSpeed = 5.0f;
        Vec3 velocity;
        for (int i = 0; i < 30; ++i) {
            step.velocity = velocity;
            velocity = Physics::IntegrateCharacterVelocity(step).velocity;
        }
        pass &= Check(Near(velocity.x, 5.0f, 0.05f) && std::fabs(velocity.z) < 0.01f,
                      "Ground acceleration reaches walk speed and does not exceed it");
    }

    {
        Physics::VelocityStepInput step = GroundStep();
        step.velocity = Vec3(5.0f, 0.0f, 0.0f);
        int steps = 0;
        while (steps < 30 && std::fabs(step.velocity.x) >= 1.0e-3f) {
            step.velocity = Physics::IntegrateCharacterVelocity(step).velocity;
            ++steps;
        }
        pass &= Check(steps == 6, "Ground deceleration reaches zero in six 1/60 steps");
    }

    {
        Physics::VelocityStepInput step = GroundStep();
        step.ground = Physics::CharacterGroundState::InAir;
        step.wish = Physics::NormalizePlanarWish(1.0f, 0.0f);
        step.velocity = Vec3(0.0f, 2.0f, 0.0f);
        const auto moved = Physics::IntegrateCharacterVelocity(step);
        const float expected = 12.0f * 0.35f * (1.0f / 60.0f);
        pass &= Check(Near(moved.velocity.x, expected, 1.0e-4f), "Air acceleration honors air control");
        step.wish = {};
        step.velocity = Vec3(1.5f, 2.0f, 0.0f);
        const auto coast = Physics::IntegrateCharacterVelocity(step);
        pass &= Check(Near(coast.velocity.x, 1.5f, 1.0e-4f), "Air movement does not brake when the wish is zero");
    }

    {
        Physics::VelocityStepInput step = GroundStep();
        step.groundVelocity = Vec3(8.0f, 1.0f, 0.0f);
        step.velocity = step.groundVelocity;
        step.wish = Physics::NormalizePlanarWish(1.0f, 0.0f);
        const auto moved = Physics::IntegrateCharacterVelocity(step);
        pass &= Check(moved.velocity.x > 8.0f && moved.velocity.x < 8.0f + 5.0f,
                      "Platform velocity is kept and the walk cap is relative");
        pass &= Check(Near(moved.velocity.y, 1.0f), "Grounded motion keeps platform vertical velocity");
    }

    {
        Physics::VelocityStepInput step = GroundStep();
        step.groundVelocity = Vec3(2.0f, 0.4f, 0.0f);
        step.velocity = step.groundVelocity;
        step.jump = true;
        step.jumpSpeed = 5.5f;
        const auto jumped = Physics::IntegrateCharacterVelocity(step);
        pass &= Check(jumped.jumped && Near(jumped.velocity.y, 0.4f + 5.5f - 9.81f / 60.0f, 1.0e-4f),
                      "Jump adds jump speed to ground vertical and then gravity");
        pass &= Check(Near(jumped.velocity.x, 2.0f), "Jump keeps horizontal platform velocity");
        for (Physics::CharacterGroundState state : {Physics::CharacterGroundState::OnSteepGround,
                                                     Physics::CharacterGroundState::NotSupported,
                                                     Physics::CharacterGroundState::InAir}) {
            step.ground = state;
            step.velocity = Vec3(0.0f, 0.0f, 0.0f);
            const auto refused = Physics::IntegrateCharacterVelocity(step);
            pass &= Check(!refused.jumped, std::string("Jump is refused for ") + Physics::CharacterGroundStateName(state));
        }
    }

    {
        Physics::VelocityStepInput step = GroundStep();
        step.ground = Physics::CharacterGroundState::InAir;
        step.velocity = Vec3(0.0f, -100.0f, 0.0f);
        step.maxFallSpeed = 40.0f;
        const auto falling = Physics::IntegrateCharacterVelocity(step);
        pass &= Check(Near(falling.velocity.y, -40.0f), "Downward speed clamps at max fall speed");
        step.velocity = Vec3(0.0f, 12.0f, 0.0f);
        const auto rising = Physics::IntegrateCharacterVelocity(step);
        pass &= Check(rising.velocity.y > 11.0f, "Upward speed is not clamped by max fall speed");
    }

    {
        const float exact = std::cos(45.0f * 3.14159265f / 180.0f);
        pass &= Check(!Physics::IsSlopeTooSteep(exact, 45.0f * 3.14159265f / 180.0f),
                      "A slope at exactly the limit is walkable");
        pass &= Check(Physics::IsSlopeTooSteep(exact - 0.01f, 45.0f * 3.14159265f / 180.0f),
                      "A slightly steeper slope is steep");
    }

    {
        Physics::ControllerAuthoredValues values;
        values.radius = 0.3f;
        values.height = 1.0f;
        values.crouchedHeight = 1.2f;
        values.stepHeight = 0.5f;
        values.groundSnapDistance = 0.1f;
        bool clamped = false;
        const auto fixed = Physics::ClampControllerAuthored(values, clamped);
        pass &= Check(clamped && fixed.crouchedHeight < fixed.height && fixed.groundSnapDistance >= fixed.stepHeight,
                      "Crouch height stays below stand height and snap covers the step");
    }

    {
        Scene scene("RoundTrip");
        Entity entity = scene.CreateEntity("Walker");
        auto& controller = entity.AddComponent<CharacterControllerComponent>();
        controller.walkSpeed = 6.5f;
        controller.jumpSpeed = 4.25f;
        controller.crouchedHeight = 1.05f;
        controller.collisionLayer = 2;
        const std::string saved = SceneSerializer(scene).SerializeToString();
        pass &= Check(saved.find("CharacterController") != std::string::npos, "Scene writes the character controller");
        pass &= Check(saved.find("groundState") == std::string::npos && saved.find("standBlocked") == std::string::npos,
                      "Scene JSON omits runtime controller state");
        Scene loaded("Loaded");
        pass &= Check(SceneSerializer::DeserializeFromString(loaded, saved), "Scene round trip loads");
        bool found = false;
        auto view = loaded.GetRegistry().view<CharacterControllerComponent>();
        for (auto handle : view) {
            const auto& copy = loaded.GetRegistry().get<CharacterControllerComponent>(handle);
            found = Near(copy.walkSpeed, 6.5f) && Near(copy.jumpSpeed, 4.25f) && Near(copy.crouchedHeight, 1.05f);
        }
        pass &= Check(found, "Scene round trip keeps authored controller values");
    }

    {
        Scene scene("Prefab");
        Entity source = scene.CreateEntity("Source");
        auto& controller = source.AddComponent<CharacterControllerComponent>();
        controller.walkSpeed = 7.0f;
        controller.enabled = false;
        PrefabComponentEntry entry;
        pass &= Check(PrefabComponentIO::CaptureComponent(source, "CharacterController", entry), "Prefab capture writes the controller");
        pass &= Check(entry.data.Find("walkSpeed") != nullptr && entry.data.Find("groundState") == nullptr,
                      "Prefab data has authored fields and no runtime state");
        Entity partial = scene.CreateEntity("Partial");
        auto& existing = partial.AddComponent<CharacterControllerComponent>();
        existing.walkSpeed = 9.0f;
        JsonValue onlyJump;
        onlyJump.type = JsonValue::Type::Object;
        JsonValue speed;
        speed.type = JsonValue::Type::Number;
        speed.numberValue = 3.0;
        onlyJump.objectValue.emplace("jumpSpeed", speed);
        pass &= Check(PrefabComponentIO::ApplyComponentJson(partial, "CharacterController", onlyJump, true), "Partial prefab apply succeeds");
        const auto& applied = partial.GetComponent<CharacterControllerComponent>();
        pass &= Check(Near(applied.walkSpeed, 9.0f) && Near(applied.jumpSpeed, 3.0f),
                      "Missing prefab fields keep the existing values");
        Entity fresh = scene.CreateEntity("Fresh");
        JsonValue empty;
        empty.type = JsonValue::Type::Object;
        pass &= Check(PrefabComponentIO::ApplyComponentJson(fresh, "CharacterController", empty, true), "Empty prefab apply adds the component");
        const auto& defaults = fresh.GetComponent<CharacterControllerComponent>();
        pass &= Check(Near(defaults.walkSpeed, 5.0f) && Near(defaults.height, 1.8f),
                      "Missing prefab fields leave component defaults");
    }

    if (!pass) {
        return 1;
    }
    std::cout << "CharacterControllerMathTests passed\n";
    return 0;
}
