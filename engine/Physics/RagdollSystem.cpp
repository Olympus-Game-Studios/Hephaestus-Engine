#include "RagdollSystem.h"

#include "CharacterControllerSystem.h"
#include "JoltPhysicsEngine.h"
#include "JoltPhysicsSystem.h"
#include "../Assets/CharacterRuntime.h"
#include "../Scene/CharacterComponent.h"
#include "../Scene/CharacterControllerComponent.h"
#include "../Scene/Components.h"
#include "../Scene/Scene.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Physics {

namespace {

constexpr float kPi = 3.14159265358979f;

// A capsule from `from` to `to` (or fallbackLength along the parent), jointed to `parent` at `from`.
struct PartTemplate {
    const char* from;
    const char* to;
    float radius;
    int parent;
    float coneDegrees;
    float twistDegrees;
    float fallbackLength;
};

// The pelvis spans the hips sideways and drives "Hips"; see kPelvisDriver.
const PartTemplate kParts[] = {
    {"LeftUpLeg", "RightUpLeg", 0.12f, -1, 0.0f, 0.0f, 0.0f},
    {"Spine1", "Neck", 0.14f, 0, 25.0f, 20.0f, 0.0f},
    {"Head", "HeadTop_End", 0.1f, 1, 40.0f, 45.0f, 0.2f},
    {"LeftArm", "LeftForeArm", 0.055f, 1, 80.0f, 35.0f, 0.0f},
    {"LeftForeArm", "LeftHand", 0.045f, 3, 75.0f, 20.0f, 0.0f},
    {"RightArm", "RightForeArm", 0.055f, 1, 80.0f, 35.0f, 0.0f},
    {"RightForeArm", "RightHand", 0.045f, 5, 75.0f, 20.0f, 0.0f},
    {"LeftUpLeg", "LeftLeg", 0.08f, 0, 55.0f, 20.0f, 0.0f},
    {"LeftLeg", "LeftFoot", 0.06f, 7, 70.0f, 10.0f, 0.0f},
    {"RightUpLeg", "RightLeg", 0.08f, 0, 55.0f, 20.0f, 0.0f},
    {"RightLeg", "RightFoot", 0.06f, 9, 70.0f, 10.0f, 0.0f},
};
constexpr int kPartCount = static_cast<int>(sizeof(kParts) / sizeof(kParts[0]));
constexpr const char* kPelvisDriver = "Hips";

std::string BaseName(const std::string& name) {
    const size_t colon = name.find_last_of(':');
    std::string base = colon == std::string::npos ? name : name.substr(colon + 1);
    std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return std::tolower(c); });
    return base;
}

int FindBone(const Hephaestus::Skeleton& skeleton, const char* name) {
    std::string wanted = name;
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return std::tolower(c); });
    for (int i = 0; i < skeleton.BoneCount(); ++i) {
        if (BaseName(skeleton.bones[static_cast<size_t>(i)].name) == wanted) {
            return i;
        }
    }
    return -1;
}

Vec3 Origin(const Mat4& m) {
    return Vec3(m.m[12], m.m[13], m.m[14]);
}

Vec3 Sub(const Vec3& a, const Vec3& b) {
    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

float Length(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

JPH::Vec3 ToJolt(const Vec3& v) {
    return JPH::Vec3(v.x, v.y, v.z);
}

Mat4 FromJolt(const JPH::RMat44& in) {
    Mat4 out;
    for (int c = 0; c < 4; ++c) {
        const JPH::Vec4 column = c < 3 ? JPH::Vec4(in.GetColumn3(c), 0.0f)
                                       : JPH::Vec4(JPH::Vec3(in.GetTranslation()), 1.0f);
        out.m[c * 4 + 0] = column.GetX();
        out.m[c * 4 + 1] = column.GetY();
        out.m[c * 4 + 2] = column.GetZ();
        out.m[c * 4 + 3] = column.GetW();
    }
    return out;
}

float DistanceToSegment(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = Sub(b, a);
    const float lengthSq = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    float t = 0.0f;
    if (lengthSq > 1e-8f) {
        const Vec3 ap = Sub(p, a);
        t = std::clamp((ap.x * ab.x + ap.y * ab.y + ap.z * ab.z) / lengthSq, 0.0f, 1.0f);
    }
    return Length(Sub(p, Vec3(a.x + ab.x * t, a.y + ab.y * t, a.z + ab.z * t)));
}

Vec3 Midpoint(const Vec3& a, const Vec3& b) {
    return Vec3((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f);
}

float Dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Scaled(const Vec3& v, float s) {
    return Vec3(v.x * s, v.y * s, v.z * s);
}

Vec3 Add(const Vec3& a, const Vec3& b) {
    return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

Vec3 Normalized(const Vec3& v, const Vec3& fallback) {
    const float length = Length(v);
    return length > 1e-6f ? Scaled(v, 1.0f / length) : fallback;
}

Vec3 Cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

float Percentile(std::vector<float>& values, float q) {
    const size_t index = std::min(values.size() - 1, static_cast<size_t>(q * static_cast<float>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

struct PartShape {
    Vec3 center{0.0f, 0.0f, 0.0f};
    JPH::Quat rotation = JPH::Quat::sIdentity();
    JPH::Ref<JPH::Shape> shape;
};

constexpr size_t kMinFitPoints = 30;
constexpr size_t kMaxSampledVertices = 40000;

// The character's skinned vertices in its current pose, in world space, sorted by the part of their main bone.
void CollectPartPoints(const CharacterComponent& character, const Mat4& entityWorld,
                       const std::vector<int>& partOfBone, std::vector<std::vector<Vec3>>& outPoints) {
    const std::vector<Mat4>& palette = character.bonePalette;
    Hephaestus::CharacterRuntime::VisitSkinnedMeshes(
        character.characterPath, [&](const std::vector<Hephaestus::SkinnedMeshPart>& meshes) {
            size_t total = 0;
            for (const auto& mesh : meshes) {
                total += mesh.vertices.size();
            }
            const size_t stride = std::max<size_t>(1, total / kMaxSampledVertices);
            size_t index = 0;
            for (const auto& mesh : meshes) {
                for (const auto& vertex : mesh.vertices) {
                    if (index++ % stride != 0) {
                        continue;
                    }
                    int main = -1;
                    float best = 0.0f;
                    Vec3 posed(0.0f, 0.0f, 0.0f);
                    const Vec3 rest(vertex.pos[0], vertex.pos[1], vertex.pos[2]);
                    for (int k = 0; k < Hephaestus::kMaxBoneInfluences; ++k) {
                        const int joint = vertex.joints[k];
                        const float weight = vertex.weights[k];
                        if (weight <= 0.0f || joint >= static_cast<int>(partOfBone.size())
                            || joint >= static_cast<int>(palette.size())) {
                            continue;
                        }
                        posed = Add(posed, Scaled(Mat4::TransformPoint(palette[static_cast<size_t>(joint)], rest), weight));
                        if (weight > best) {
                            best = weight;
                            main = joint;
                        }
                    }
                    if (main >= 0) {
                        outPoints[static_cast<size_t>(partOfBone[static_cast<size_t>(main)])].push_back(
                            Mat4::TransformPoint(entityWorld, posed));
                    }
                }
            }
        });
}

PartShape TemplateCapsule(const Vec3& a, const Vec3& b, float radius) {
    const Vec3 axis = Sub(b, a);
    PartShape out;
    out.center = Midpoint(a, b);
    out.rotation = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), ToJolt(Normalized(axis, Vec3(0.0f, 1.0f, 0.0f))));
    out.shape = new JPH::CapsuleShape(std::max(0.5f * Length(axis) - radius, 0.01f), radius);
    return out;
}

// A capsule along a -> b around the part's vertices: centred on them, as long as they reach along the axis, with
// the radius most of them sit at (gear and straps stick out further).
bool FitCapsule(const std::vector<Vec3>& points, const Vec3& a, const Vec3& b, PartShape& out) {
    if (points.size() < kMinFitPoints) {
        return false;
    }
    const Vec3 u = Normalized(Sub(b, a), Vec3(0.0f, 1.0f, 0.0f));
    std::vector<float> along;
    along.reserve(points.size());
    Vec3 meanPerp(0.0f, 0.0f, 0.0f);
    for (const Vec3& point : points) {
        const Vec3 rel = Sub(point, a);
        const float t = Dot(rel, u);
        along.push_back(t);
        meanPerp = Add(meanPerp, Sub(rel, Scaled(u, t)));
    }
    meanPerp = Scaled(meanPerp, 1.0f / static_cast<float>(points.size()));
    std::vector<float> radial;
    radial.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        const Vec3 rel = Sub(points[i], a);
        radial.push_back(Length(Sub(Sub(rel, Scaled(u, along[i])), meanPerp)));
    }
    const float radius = std::clamp(Percentile(radial, 0.6f), 0.025f, 0.25f);
    const float low = Percentile(along, 0.02f);
    const float high = Percentile(along, 0.98f);
    out.center = Add(Add(a, Scaled(u, 0.5f * (low + high))), meanPerp);
    out.rotation = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), ToJolt(u));
    out.shape = new JPH::CapsuleShape(std::max(0.5f * (high - low) - radius, 0.01f), radius);
    return true;
}

// A box around the part's vertices. `axis` is the box's X (acrossFirst) or Y; `second` is the other of the two.
bool FitBox(const std::vector<Vec3>& points, const Vec3& origin, const Vec3& axis, const Vec3& second,
            bool acrossFirst, PartShape& out) {
    if (points.size() < kMinFitPoints) {
        return false;
    }
    const Vec3 first = Normalized(axis, Vec3(1.0f, 0.0f, 0.0f));
    const Vec3 other = Normalized(Sub(second, Scaled(first, Dot(second, first))), Vec3(0.0f, 0.0f, 1.0f));
    const Vec3 x = acrossFirst ? first : other;
    const Vec3 y = acrossFirst ? other : first;
    const Vec3 z = Cross(x, y);
    const Vec3 axes[3] = {x, y, z};
    Vec3 center = origin;
    float half[3];
    for (int i = 0; i < 3; ++i) {
        std::vector<float> coords;
        coords.reserve(points.size());
        for (const Vec3& point : points) {
            coords.push_back(Dot(Sub(point, origin), axes[i]));
        }
        const float low = Percentile(coords, 0.03f);
        const float high = Percentile(coords, 0.97f);
        center = Add(center, Scaled(axes[i], 0.5f * (low + high)));
        half[i] = std::max(0.5f * (high - low), 0.06f);
    }
    JPH::Mat44 basis = JPH::Mat44::sIdentity();
    basis.SetColumn3(0, ToJolt(x));
    basis.SetColumn3(1, ToJolt(y));
    basis.SetColumn3(2, ToJolt(z));
    out.center = center;
    out.rotation = basis.GetQuaternion().Normalized();
    out.shape = new JPH::BoxShape(JPH::Vec3(half[0], half[1], half[2]));
    return true;
}

bool IsDescendant(entt::registry& registry, entt::entity entity, entt::entity ancestor) {
    entt::entity current = entity;
    for (int depth = 0; depth < 64 && current != entt::null && registry.valid(current); ++depth) {
        const auto* transform = registry.try_get<TransformComponent>(current);
        if (!transform) {
            return false;
        }
        current = transform->parent;
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

} // namespace

struct RagdollSystem::Ragdoll {
    entt::entity entity = entt::null;
    std::vector<JPH::BodyID> bodies;
    std::vector<Mat4> bodyToBone;           // per part: body frame to its driven bone's world frame
    std::vector<int> drivenBone;            // per part: skeleton evaluation index
    std::vector<int> boneToPart;            // per bone: part index or -1
    std::vector<Mat4> local;                // bone local matrices at the start, kept for undriven bones
    std::vector<Mat4> startModel;           // bone model-space matrices at the start
    std::vector<JPH::Ref<JPH::Constraint>> constraints;
    JPH::Ref<JPH::GroupFilterTable> filter;
};

RagdollSystem::RagdollSystem(JoltPhysicsEngine& engine, JoltPhysicsSystem& physics)
    : m_Engine(engine), m_Physics(physics) {}

RagdollSystem::~RagdollSystem() {
    Clear();
}

bool RagdollSystem::IsActive(entt::entity entity) const {
    return std::any_of(m_Ragdolls.begin(), m_Ragdolls.end(),
                       [entity](const std::unique_ptr<Ragdoll>& r) { return r->entity == entity; });
}

int RagdollSystem::ActiveCount() const {
    return static_cast<int>(m_Ragdolls.size());
}

std::vector<JPH::BodyID> RagdollSystem::PartBodies(entt::entity entity) const {
    for (const auto& ragdoll : m_Ragdolls) {
        if (ragdoll->entity == entity) {
            return ragdoll->bodies;
        }
    }
    return {};
}

bool RagdollSystem::Start(Scene& scene, entt::entity entity, const RagdollStart& start, std::string& outError) {
    outError.clear();
    entt::registry& registry = scene.GetRegistry();
    JPH::PhysicsSystem* physics = m_Engine.GetPhysicsSystem();
    if (!physics) {
        outError = "Physics is not running.";
        return false;
    }
    if (!registry.valid(entity) || !registry.all_of<CharacterComponent, TransformComponent>(entity)) {
        outError = "Entity has no Character component.";
        return false;
    }
    if (IsActive(entity)) {
        return true;
    }
    CharacterComponent& character = registry.get<CharacterComponent>(entity);
    const Hephaestus::Skeleton& skeleton = character.skeleton;
    if (!character.runtimeReady || character.worldPose.size() != static_cast<size_t>(skeleton.BoneCount())) {
        outError = "Character has no evaluated pose.";
        return false;
    }

    // Resolve every template bone before touching the scene.
    int from[kPartCount];
    int to[kPartCount];
    for (int p = 0; p < kPartCount; ++p) {
        from[p] = FindBone(skeleton, kParts[p].from);
        to[p] = FindBone(skeleton, kParts[p].to);
        if (from[p] < 0 || (to[p] < 0 && kParts[p].fallbackLength <= 0.0f)) {
            outError = std::string("Skeleton has no '") + (from[p] < 0 ? kParts[p].from : kParts[p].to)
                + "' bone; ragdolls need a humanoid skeleton with Mixamo bone names.";
            return false;
        }
    }
    const int pelvisBone = FindBone(skeleton, kPelvisDriver);
    if (pelvisBone < 0) {
        outError = "Skeleton has no 'Hips' bone; ragdolls need a humanoid skeleton with Mixamo bone names.";
        return false;
    }

    Vec3 inheritedVelocity(0.0f, 0.0f, 0.0f);
    std::uint16_t gameplayLayer = 0;
    if (auto* controller = registry.try_get<CharacterControllerComponent>(entity)) {
        gameplayLayer = controller->collisionLayer;
        CharacterControllerState state;
        if (CharacterControllerSystem* controllers = m_Physics.GetCharacterController();
            controllers && controllers->GetState(entity, state)) {
            inheritedVelocity = state.velocity;
        }
        // The capsule would overlap the new parts, so it goes before they are created.
        controller->enabled = false;
        if (CharacterControllerSystem* controllers = m_Physics.GetCharacterController()) {
            controllers->SyncLifecycle(scene);
        }
    }
    std::vector<entt::entity> childColliders;
    for (auto handle : registry.view<RigidBodyComponent>()) {
        if (IsDescendant(registry, handle, entity)) {
            childColliders.push_back(handle);
        }
    }
    for (entt::entity child : childColliders) {
        registry.remove<RigidBodyComponent>(child);
    }
    if (!childColliders.empty()) {
        m_Physics.PrepareSceneBodies(scene);
    }

    auto ragdoll = std::make_unique<Ragdoll>();
    ragdoll->entity = entity;
    ragdoll->local = character.localMatrices;
    ragdoll->startModel = character.worldPose;
    ragdoll->boneToPart.assign(static_cast<size_t>(skeleton.BoneCount()), -1);
    ragdoll->filter = new JPH::GroupFilterTable(kPartCount);
    for (int p = 0; p < kPartCount; ++p) {
        if (kParts[p].parent >= 0) {
            ragdoll->filter->DisableCollision(static_cast<JPH::CollisionGroup::SubGroupID>(p),
                                              static_cast<JPH::CollisionGroup::SubGroupID>(kParts[p].parent));
        }
    }
    // Shoulders and thighs sit close to their grandparents too.
    ragdoll->filter->DisableCollision(3, 0);
    ragdoll->filter->DisableCollision(5, 0);
    ragdoll->filter->DisableCollision(7, 1);
    ragdoll->filter->DisableCollision(9, 1);
    ragdoll->filter->DisableCollision(7, 9);
    const JPH::CollisionGroup::GroupID group = m_NextGroup++;

    const Mat4 entityWorld = scene.GetWorldTransform(Entity(entity, &scene));
    auto boneWorld = [&](int bone) { return Mat4::Multiply(entityWorld, character.worldPose[static_cast<size_t>(bone)]); };
    JPH::BodyInterface& bodies = m_Engine.GetBodyInterface();
    const JPH::ObjectLayer layer = m_Engine.ObjectLayerFor(gameplayLayer, false);

    Vec3 segmentStart[kPartCount];
    Vec3 segmentEnd[kPartCount];
    for (int p = 0; p < kPartCount; ++p) {
        const Vec3 a = Origin(boneWorld(from[p]));
        Vec3 b;
        if (to[p] >= 0) {
            b = Origin(boneWorld(to[p]));
        } else {
            const int parent = kParts[p].parent;
            Vec3 dir = parent >= 0 ? Sub(segmentEnd[parent], segmentStart[parent]) : Vec3(0.0f, 1.0f, 0.0f);
            const float len = std::max(Length(dir), 1e-4f);
            b = Vec3(a.x + dir.x / len * kParts[p].fallbackLength, a.y + dir.y / len * kParts[p].fallbackLength,
                     a.z + dir.z / len * kParts[p].fallbackLength);
        }
        segmentStart[p] = a;
        segmentEnd[p] = b;
    }

    // Each bone belongs to the part that drives it or its nearest driven ancestor.
    std::vector<int> partOfBone(static_cast<size_t>(skeleton.BoneCount()), 0);
    for (int p = 0; p < kPartCount; ++p) {
        partOfBone[static_cast<size_t>(p == 0 ? pelvisBone : from[p])] = -2 - p;
    }
    for (int b = 0; b < skeleton.BoneCount(); ++b) {
        int& part = partOfBone[static_cast<size_t>(b)];
        if (part <= -2) {
            part = -2 - part;
        } else {
            const int parent = skeleton.bones[static_cast<size_t>(b)].parentEval;
            part = parent >= 0 ? partOfBone[static_cast<size_t>(parent)] : 0;
        }
    }
    std::vector<std::vector<Vec3>> points(kPartCount);
    CollectPartPoints(character, entityWorld, partOfBone, points);

    const Vec3 up = Sub(Origin(boneWorld(from[1])), Origin(boneWorld(pelvisBone)));
    const Vec3 across = Sub(segmentStart[5], segmentStart[3]);
    for (int p = 0; p < kPartCount; ++p) {
        const Vec3 a = segmentStart[p];
        const Vec3 b = segmentEnd[p];
        PartShape fitted;
        bool haveFit = false;
        if (p <= 1) {
            // Pelvis: X across the hips, Y up the spine. Chest: Y up the spine, X across the shoulders.
            haveFit = p == 0 ? FitBox(points[p], Midpoint(a, b), Sub(b, a), up, true, fitted)
                             : FitBox(points[p], Midpoint(a, b), Sub(b, a), across, false, fitted);
        } else {
            haveFit = FitCapsule(points[p], a, b, fitted);
        }
        if (!haveFit) {
            fitted = TemplateCapsule(a, b, kParts[p].radius);
        }
        const JPH::RVec3 center(fitted.center.x, fitted.center.y, fitted.center.z);
        const JPH::Quat rotation = fitted.rotation;

        JPH::BodyCreationSettings settings(fitted.shape, center, rotation, JPH::EMotionType::Dynamic, layer);
        settings.mCollisionGroup = JPH::CollisionGroup(ragdoll->filter, group,
                                                       static_cast<JPH::CollisionGroup::SubGroupID>(p));
        settings.mLinearDamping = 0.1f;
        settings.mAngularDamping = 0.6f;
        settings.mFriction = 0.8f;
        settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
        settings.mLinearVelocity = ToJolt(inheritedVelocity);
        const JPH::BodyID id = bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
        if (id.IsInvalid()) {
            for (JPH::BodyID created : ragdoll->bodies) {
                bodies.RemoveBody(created);
                bodies.DestroyBody(created);
            }
            outError = "Out of physics bodies.";
            return false;
        }
        ragdoll->bodies.push_back(id);
        m_Physics.RegisterExternalBody(entity, id);

        const int driven = p == 0 ? pelvisBone : from[p];
        const Mat4 bodyWorld = FromJolt(bodies.GetWorldTransform(id));
        ragdoll->bodyToBone.push_back(Mat4::Multiply(Mat4::Invert(bodyWorld), boneWorld(driven)));
        ragdoll->drivenBone.push_back(driven);
        ragdoll->boneToPart[static_cast<size_t>(driven)] = p;
    }

    for (int p = 1; p < kPartCount; ++p) {
        const int parent = kParts[p].parent;
        const Vec3 pivot = segmentStart[p];
        JPH::Vec3 twist = ToJolt(Sub(segmentEnd[p], segmentStart[p]));
        twist = twist.LengthSq() > 1e-8f ? twist.Normalized() : JPH::Vec3::sAxisY();
        const JPH::Vec3 plane = twist.GetNormalizedPerpendicular();
        JPH::SwingTwistConstraintSettings joint;
        joint.mSpace = JPH::EConstraintSpace::WorldSpace;
        joint.mPosition1 = joint.mPosition2 = JPH::RVec3(pivot.x, pivot.y, pivot.z);
        joint.mTwistAxis1 = joint.mTwistAxis2 = twist;
        joint.mPlaneAxis1 = joint.mPlaneAxis2 = plane;
        joint.mNormalHalfConeAngle = joint.mPlaneHalfConeAngle = kParts[p].coneDegrees * kPi / 180.0f;
        joint.mTwistMinAngle = -kParts[p].twistDegrees * kPi / 180.0f;
        joint.mTwistMaxAngle = kParts[p].twistDegrees * kPi / 180.0f;
        joint.mMaxFrictionTorque = 2.0f;
        JPH::Ref<JPH::Constraint> constraint =
            bodies.CreateConstraint(&joint, ragdoll->bodies[static_cast<size_t>(parent)],
                                    ragdoll->bodies[static_cast<size_t>(p)]);
        physics->AddConstraint(constraint);
        ragdoll->constraints.push_back(constraint);
    }

    if (Length(start.impulse) > 0.0f) {
        int target = 0;
        if (start.hasImpulsePoint) {
            float best = 1e30f;
            for (int p = 0; p < kPartCount; ++p) {
                const float d = DistanceToSegment(start.impulsePoint, segmentStart[p], segmentEnd[p]);
                if (d < best) {
                    best = d;
                    target = p;
                }
            }
            bodies.AddImpulse(ragdoll->bodies[static_cast<size_t>(target)], ToJolt(start.impulse),
                              JPH::RVec3(start.impulsePoint.x, start.impulsePoint.y, start.impulsePoint.z));
        } else {
            bodies.AddImpulse(ragdoll->bodies[0], ToJolt(start.impulse));
        }
    }

    character.externalPose = true;
    m_Ragdolls.push_back(std::move(ragdoll));
    return true;
}

void RagdollSystem::AfterStep(Scene& scene) {
    entt::registry& registry = scene.GetRegistry();
    JPH::BodyInterface& bodies = m_Engine.GetBodyInterface();
    for (size_t i = 0; i < m_Ragdolls.size();) {
        Ragdoll& ragdoll = *m_Ragdolls[i];
        CharacterComponent* character = registry.valid(ragdoll.entity)
            ? registry.try_get<CharacterComponent>(ragdoll.entity) : nullptr;
        if (!character || character->skeleton.BoneCount() != static_cast<int>(ragdoll.boneToPart.size())) {
            Destroy(ragdoll);
            m_Ragdolls.erase(m_Ragdolls.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        const Mat4 toModel = Mat4::Invert(scene.GetWorldTransform(Entity(ragdoll.entity, &scene)));
        const Hephaestus::Skeleton& skeleton = character->skeleton;
        std::vector<Mat4>& world = character->worldPose;
        for (int b = 0; b < skeleton.BoneCount(); ++b) {
            const int part = ragdoll.boneToPart[static_cast<size_t>(b)];
            const int parent = skeleton.bones[static_cast<size_t>(b)].parentEval;
            if (part >= 0) {
                const Mat4 bodyWorld = FromJolt(bodies.GetWorldTransform(ragdoll.bodies[static_cast<size_t>(part)]));
                world[static_cast<size_t>(b)] =
                    Mat4::Multiply(toModel, Mat4::Multiply(bodyWorld, ragdoll.bodyToBone[static_cast<size_t>(part)]));
            } else if (parent >= 0 && static_cast<size_t>(b) < ragdoll.local.size()) {
                world[static_cast<size_t>(b)] =
                    Mat4::Multiply(world[static_cast<size_t>(parent)], ragdoll.local[static_cast<size_t>(b)]);
            } else {
                world[static_cast<size_t>(b)] = ragdoll.startModel[static_cast<size_t>(b)];
            }
        }
        Hephaestus::SkeletonBuilder::FillBonePalette(skeleton, world, character->bonePalette);
        ++i;
    }
}

void RagdollSystem::Destroy(Ragdoll& ragdoll) {
    JPH::PhysicsSystem* physics = m_Engine.GetPhysicsSystem();
    if (!physics) {
        return;
    }
    for (const auto& constraint : ragdoll.constraints) {
        physics->RemoveConstraint(constraint);
    }
    ragdoll.constraints.clear();
    JPH::BodyInterface& bodies = m_Engine.GetBodyInterface();
    for (JPH::BodyID id : ragdoll.bodies) {
        m_Physics.UnregisterExternalBody(id);
        if (bodies.IsAdded(id)) {
            bodies.RemoveBody(id);
        }
        bodies.DestroyBody(id);
    }
    ragdoll.bodies.clear();
}

void RagdollSystem::Clear() {
    for (auto& ragdoll : m_Ragdolls) {
        Destroy(*ragdoll);
    }
    m_Ragdolls.clear();
}

} // namespace Physics
