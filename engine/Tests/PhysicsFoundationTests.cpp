#include "Core/EngineLog.h"
#include "Physics/PhysicsContactEvents.h"
#include "Physics/PhysicsLayerConfig.h"
#include "Physics/PhysicsStepClock.h"
#include "Physics/PhysicsWorldPose.h"
#include "Scene/Components.h"

#include <filesystem>
#include <fstream>
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

bool TestCompatibilityAndSeed()
{
    bool pass = true;
    const Physics::PhysicsLayerConfig compatibility = Physics::PhysicsLayerConfig::Compatibility();
    pass &= Check(compatibility.Layers().size() == 1, "missing-file shape is one layer");
    pass &= Check(compatibility.HasLayer(0) && compatibility.Collides(0, 0), "Default collides with itself");
    pass &= Check(!compatibility.HasLayer(1), "compatibility config has no World layer");

    Physics::PhysicsLayerConfig missing;
    const auto missingResult = Physics::PhysicsLayerConfig::LoadFromFile("missing-physics-layers.json", missing);
    pass &= Check(missingResult.missingFile && missingResult.usedCompatibility, "missing file uses compatibility");
    pass &= Check(missing.Collides(0, 0) && missing.Layers().size() == 1, "missing file matrix");

    const Physics::PhysicsLayerConfig seed = Physics::PhysicsLayerConfig::Seed();
    pass &= Check(seed.Layers().size() == 4, "seed has four layers");
    pass &= Check(seed.FindById(0) && seed.FindById(0)->name == "Default" && seed.FindById(0)->builtin, "Default builtin");
    pass &= Check(seed.Collides(0, 0) && seed.Collides(0, 1) && seed.Collides(0, 2) && seed.Collides(0, 3),
                  "Default pairs start on");
    pass &= Check(!seed.Collides(3, 3), "Trigger-vs-Trigger is off");
    pass &= Check(!seed.Collides(1, 1), "World-vs-World is off");
    return pass;
}

bool TestMatrixRoundTrip()
{
    bool pass = true;
    Physics::PhysicsLayerConfig config = Physics::PhysicsLayerConfig::Seed();
    config.SetCollides(Physics::kLayerDefault, Physics::kLayerWorld, false);
    config.SetCollides(Physics::kLayerDefault, Physics::kLayerCharacter, false);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "hephaestus-physics-layers.json";
    std::string error;
    pass &= Check(config.SaveToFile(path, error), "save layer file: " + error);

    Physics::PhysicsLayerConfig loaded;
    const auto result = Physics::PhysicsLayerConfig::LoadFromFile(path, loaded);
    pass &= Check(result.error.empty() && !result.usedCompatibility, "reload layer file");
    pass &= Check(!loaded.Collides(0, 1), "Default-vs-World false survives load");
    pass &= Check(!loaded.Collides(0, 2), "Default-vs-Character false round-trips");
    pass &= Check(loaded.Collides(0, 0), "Default-vs-Default stays on");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return pass;
}

bool TestLayerIdentity()
{
    bool pass = true;
    Physics::PhysicsLayerConfig config = Physics::PhysicsLayerConfig::Seed();
    std::string error;
    pass &= Check(!config.DeleteUserLayer(0, error), "Default cannot be deleted");
    pass &= Check(!config.DeleteUserLayer(1, error), "World cannot be deleted");

    pass &= Check(config.RenameLayer(0, "General", error), "rename keeps the layer");
    pass &= Check(config.FindById(0) && config.FindById(0)->name == "General" && config.FindById(0)->id == 0,
                  "rename does not change id");

    pass &= Check(config.AddUserLayer("Bullet", error), "add user layer: " + error);
    const std::uint16_t bullet = config.FindIdByName("Bullet");
    pass &= Check(bullet == 4, "first user layer is id 4");
    pass &= Check(config.Collides(bullet, 0), "new layer collides with Default");

    pass &= Check(config.DeleteUserLayer(bullet, error), "tombstone user layer");
    pass &= Check(config.IsTombstoned(bullet) && !config.HasLayer(bullet), "id 4 is tombstoned");
    pass &= Check(config.AddUserLayer("Rocket", error), "add after tombstone");
    pass &= Check(config.FindIdByName("Rocket") == 5, "tombstoned id is not reused");

    for (int i = 0; i < 16; ++i) {
        config.AddUserLayer("Extra" + std::to_string(i), error);
    }
    pass &= Check(!config.AddUserLayer("Overflow", error), "17th layer is rejected");
    pass &= Check(config.Resolve(99) == 0, "unknown id resolves to 0");
    pass &= Check(config.Resolve(bullet) == 0, "tombstoned id resolves to 0");
    return pass;
}

bool Near(float value, float expected)
{
    const float delta = value - expected;
    return delta < 1.0e-4f && delta > -1.0e-4f;
}

bool TestAccumulator()
{
    bool pass = true;
    const auto one = Physics::AdvancePhysicsAccumulator(0.0f, Physics::kPhysicsFixedDt);
    pass &= Check(one.substepCount == 1 && !one.stalled && Near(one.accumulator, 0.0f),
                  "dt 1/60 takes one step");

    const auto two = Physics::AdvancePhysicsAccumulator(0.0f, Physics::kPhysicsFixedDt * 2.0f);
    pass &= Check(two.substepCount == 2 && !two.stalled && Near(two.accumulator, 0.0f),
                  "dt 1/30 takes two steps");

    const auto exactCap = Physics::AdvancePhysicsAccumulator(0.0f, Physics::kPhysicsFixedDt * 4.0f);
    pass &= Check(exactCap.substepCount == 4 && !exactCap.stalled && Near(exactCap.accumulator, 0.0f),
                  "exactly four steps does not stall");

    const auto hitch = Physics::AdvancePhysicsAccumulator(0.0f, 0.1f);
    pass &= Check(hitch.substepCount == 4 && hitch.stalled && hitch.accumulator == 0.0f,
                  "dt 0.1 takes four steps and stalls");

    auto carry = Physics::AdvancePhysicsAccumulator(0.0f, 0.01f);
    pass &= Check(carry.substepCount == 0 && carry.accumulator > 0.009f && carry.accumulator < 0.011f,
                  "leftover under a step carries");
    carry = Physics::AdvancePhysicsAccumulator(carry.accumulator, 0.01f);
    pass &= Check(carry.substepCount == 1 && carry.accumulator > 0.0f && carry.accumulator < Physics::kPhysicsFixedDt,
                  "carried leftover completes on the next frame");

    const auto cleared = Physics::AdvancePhysicsAccumulator(0.0f, 0.0f);
    pass &= Check(cleared.substepCount == 0 && Near(cleared.accumulator, 0.0f) && !cleared.stalled,
                  "a cleared accumulator stays idle");

    const auto ignored = Physics::AdvancePhysicsAccumulator(0.01f, -1.0f);
    pass &= Check(ignored.substepCount == 0 && Near(ignored.accumulator, 0.01f),
                  "a negative frame dt does not rewind the accumulator");
    return pass;
}

bool TestWorldPose()
{
    bool pass = true;
    Physics::RigidTransformSample root;
    root.position = Vec3(0.0f, 5.0f, 0.0f);
    Physics::RigidTransformSample child;
    child.position = Vec3(0.0f, 1.0f, 0.0f);
    const Physics::RigidTransformSample translated[] = {root, child};
    Physics::WorldPose pose;
    pass &= Check(Physics::TryComposeWorldPose(translated, 2, pose), "parent translation decomposes");
    pass &= Check(Near(pose.position.y, 6.0f) && Near(pose.position.x, 0.0f),
                  "parent translation plus local offset is the world position");

    root.scale = Vec3(2.0f, 2.0f, 2.0f);
    child.scale = Vec3(0.5f, 0.5f, 0.5f);
    const Physics::RigidTransformSample scaled[] = {root, child};
    pass &= Check(Physics::TryComposeWorldPose(scaled, 2, pose), "uniform parent scale decomposes");
    pass &= Check(Near(pose.shapeScale.x, 1.0f) && Near(pose.shapeScale.y, 1.0f) && Near(pose.shapeScale.z, 1.0f),
                  "uniform parent scale multiplies shape scale");
    pass &= Check(Near(pose.position.y, 7.0f), "uniform parent scale multiplies the child offset");

    child.scale = Vec3(-2.0f, 1.0f, 0.5f);
    root.scale = Vec3(1.0f, 1.0f, 1.0f);
    const Physics::RigidTransformSample negative[] = {root, child};
    pass &= Check(Physics::TryComposeWorldPose(negative, 2, pose), "negative local scale decomposes");
    pass &= Check(Near(pose.shapeScale.x, 2.0f) && Near(pose.shapeScale.y, 1.0f) && Near(pose.shapeScale.z, 0.5f),
                  "shape scale uses the absolute local scale");

    root.scale = Vec3(2.0f, 1.0f, 2.0f);
    const Physics::RigidTransformSample sheared[] = {root, child};
    pass &= Check(!Physics::TryComposeWorldPose(sheared, 2, pose), "non-uniform ancestor scale fails");

    root.scale = Vec3(1.0f, 1.0f, 1.0f);
    root.rotation = Vec3(0.0f, 1.5707963f, 0.0f);
    child.position = Vec3(1.0f, 0.0f, 0.0f);
    child.scale = Vec3(1.0f, 1.0f, 1.0f);
    const Physics::RigidTransformSample rotated[] = {root, child};
    pass &= Check(Physics::TryComposeWorldPose(rotated, 2, pose), "rotated parent decomposes");
    const Vec3 recovered = Physics::WorldPointToLocal(root.position, Physics::PoseQuatFromEulerXYZ(root.rotation),
                                                      Vec3(1.0f, 1.0f, 1.0f), pose.position);
    pass &= Check(Near(recovered.x, 1.0f) && Near(recovered.y, 0.0f) && Near(recovered.z, 0.0f),
                  "dynamic write-back recovers local position from the body world position");

    const Vec3 tilted(0.3f, 0.4f, 0.5f);
    const Vec3 roundTrip = Physics::PoseQuatToEulerXYZ(Physics::PoseQuatFromEulerXYZ(tilted));
    pass &= Check(Near(roundTrip.x, tilted.x) && Near(roundTrip.y, tilted.y) && Near(roundTrip.z, tilted.z),
                  "Euler XYZ survives a quaternion round trip about all three axes");
    TransformComponent fromMatrix;
    TransformComponent source;
    source.rotation = tilted;
    fromMatrix.SetFromMatrix(source.GetLocalMatrix());
    pass &= Check(Near(fromMatrix.rotation.x, roundTrip.x) && Near(fromMatrix.rotation.y, roundTrip.y)
                      && Near(fromMatrix.rotation.z, roundTrip.z),
                  "quaternion Euler extraction matches the transform matrix convention");
    return pass;
}

bool TestContactCollapse()
{
    bool pass = true;
    std::unordered_map<std::uint64_t, Physics::PhysicsOverlapState> overlap;
    std::vector<Physics::PhysicsContactEvent> events;
    Physics::PhysicsContactRecord added;
    added.kind = Physics::PhysicsContactKind::Added;
    added.body1 = 1;
    added.body2 = 2;
    added.entity1 = 10;
    added.entity2 = 20;
    added.hasPoint = true;
    added.normalY = 1.0f;
    added.point2Y = 1.0f;
    added.penetration = 0.25f;
    added.vel1X = 1.0f;
    Physics::CollapseContactRecords({added}, overlap, events);
    pass &= Check(events.size() == 2 && overlap.size() == 1, "a new solid pair emits one enter for each body");
    const auto body1 = events[0].receiver == 10 ? events[0] : events[1];
    const auto body2 = events[0].receiver == 20 ? events[0] : events[1];
    pass &= Check(body1.kind == Physics::PhysicsEventKind::CollisionEnter
                      && body2.kind == Physics::PhysicsEventKind::CollisionEnter,
                  "solid contacts are collision events");
    pass &= Check(body1.normalY < 0.0f && body2.normalY > 0.0f, "body 1 receives the negated Jolt normal");
    pass &= Check(body1.relativeVelocityX < 0.0f && body2.relativeVelocityX > 0.0f,
                  "relative velocity is the other body minus the receiver");
    pass &= Check(body1.pointY > 0.4f && body1.pointY < 0.6f && body1.hasContact,
                  "the contact point is the midpoint of the deepest pair");

    Physics::PhysicsContactRecord again = added;
    Physics::CollapseContactRecords({again}, overlap, events);
    pass &= Check(events.empty(), "a second add for a pair that is already open is ignored");

    Physics::PhysicsContactRecord persisted = added;
    persisted.kind = Physics::PhysicsContactKind::Persisted;
    Physics::CollapseContactRecords({persisted}, overlap, events);
    pass &= Check(events.size() == 2 && events[0].kind == Physics::PhysicsEventKind::CollisionStay,
                  "a persisted solid pair emits stay");

    Physics::PhysicsContactRecord speculative = added;
    speculative.body1 = 3;
    speculative.body2 = 4;
    speculative.entity1 = 30;
    speculative.entity2 = 40;
    speculative.penetration = -0.1f;
    Physics::CollapseContactRecords({speculative}, overlap, events);
    pass &= Check(events.size() == 2 && !events[0].hasContact && !events[1].hasContact,
                  "a negative penetration still enters and carries no contact");

    Physics::PhysicsContactRecord removed;
    removed.kind = Physics::PhysicsContactKind::Removed;
    removed.body1 = 1;
    removed.body2 = 2;
    removed.entity1 = 10;
    removed.entity2 = 20;
    removed.lastContact = false;
    Physics::CollapseContactRecords({removed}, overlap, events);
    pass &= Check(events.empty() && overlap.count(Physics::PhysicsPairKey(1, 2)) == 1,
                  "a removed subshape does not exit while the bodies are still in contact");
    removed.lastContact = true;
    removed.entity2Live = false;
    Physics::CollapseContactRecords({removed}, overlap, events);
    pass &= Check(events.size() == 1 && events[0].receiver == 10
                      && events[0].kind == Physics::PhysicsEventKind::CollisionExit
                      && !events[0].hasContact && !events[0].otherAlive,
                  "the last removal exits the live receiver without a contact");

    overlap.clear();
    Physics::PhysicsContactRecord sensor = added;
    sensor.sensor = true;
    sensor.body1 = 7;
    sensor.body2 = 8;
    sensor.entity1 = 70;
    sensor.entity2 = 80;
    Physics::CollapseContactRecords({sensor}, overlap, events);
    pass &= Check(events.size() == 2 && events[0].kind == Physics::PhysicsEventKind::TriggerEnter,
                  "a sensor pair emits trigger events to both bodies");
    return pass;
}

} // namespace

int main()
{
    bool pass = true;
    pass &= TestCompatibilityAndSeed();
    pass &= TestMatrixRoundTrip();
    pass &= TestLayerIdentity();
    pass &= TestAccumulator();
    pass &= TestWorldPose();
    pass &= TestContactCollapse();
    if (!pass) {
        std::cerr << "[FAIL] PhysicsFoundationTests\n";
        return 1;
    }
    std::cout << "[PASS] PhysicsFoundationTests\n";
    return 0;
}
