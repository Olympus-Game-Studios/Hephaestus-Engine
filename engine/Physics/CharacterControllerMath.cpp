#include "CharacterControllerMath.h"

#include <algorithm>
#include <cmath>

namespace Physics {
namespace {

constexpr float kWishEpsilon = 1.0e-8f;
constexpr float kJumpAwayTolerance = 0.1f;
constexpr float kGateEpsilon = 1.0e-5f;

bool Finite(float value)
{
    return std::isfinite(value);
}

float ClampNonNegative(float value, float fallback)
{
    if (!Finite(value)) {
        return fallback;
    }
    return std::max(0.0f, value);
}

Vec3 Planar(const Vec3& value)
{
    return Vec3(value.x, 0.0f, value.z);
}

float LengthXZ(const Vec3& value)
{
    return std::sqrt(value.x * value.x + value.z * value.z);
}

Vec3 MoveTowards(const Vec3& current, const Vec3& target, float maxDelta)
{
    const Vec3 delta(target.x - current.x, 0.0f, target.z - current.z);
    const float length = LengthXZ(delta);
    if (length <= maxDelta || length <= kWishEpsilon) {
        return target;
    }
    const float scale = maxDelta / length;
    return Vec3(current.x + delta.x * scale, 0.0f, current.z + delta.z * scale);
}

void ClampDownward(Vec3& velocity, float maxFallSpeed)
{
    if (velocity.y < -maxFallSpeed) {
        velocity.y = -maxFallSpeed;
    }
}

} // namespace

PlanarWish NormalizePlanarWish(float x, float z)
{
    if (!Finite(x) || !Finite(z)) {
        return {};
    }
    const float lengthSq = x * x + z * z;
    if (lengthSq <= kWishEpsilon) {
        return {};
    }
    const float inverse = 1.0f / std::sqrt(lengthSq);
    return PlanarWish{x * inverse, z * inverse};
}

PlanarWish ProjectPlanarWish(float x, float y, float z)
{
    (void)y;
    return NormalizePlanarWish(x, z);
}

ControllerAuthoredValues ClampControllerAuthored(const ControllerAuthoredValues& in, bool& outClamped)
{
    ControllerAuthoredValues out = in;
    outClamped = false;

    const float radius = ClampNonNegative(in.radius, 0.3f);
    out.radius = std::max(0.05f, radius);
    out.height = std::max(ClampNonNegative(in.height, 1.8f), 2.0f * out.radius + 0.05f);

    float crouched = ClampNonNegative(in.crouchedHeight, 1.1f);
    const float minCrouch = 2.0f * out.radius + 0.05f;
    crouched = std::max(crouched, minCrouch);
    if (crouched >= out.height) {
        crouched = std::max(minCrouch, out.height - 0.05f);
        if (crouched >= out.height) {
            out.height = crouched + 0.05f;
        }
    }
    out.crouchedHeight = crouched;

    out.stepHeight = std::clamp(ClampNonNegative(in.stepHeight, 0.30f), 0.0f, 0.5f * out.height);
    out.groundSnapDistance = std::max(ClampNonNegative(in.groundSnapDistance, 0.40f), out.stepHeight);

    float slope = Finite(in.maxSlopeAngleDegrees) ? in.maxSlopeAngleDegrees : 45.0f;
    out.maxSlopeAngleDegrees = std::clamp(slope, 0.0f, 89.0f);

    out.walkSpeed = ClampNonNegative(in.walkSpeed, 5.0f);
    out.acceleration = ClampNonNegative(in.acceleration, 40.0f);
    out.deceleration = ClampNonNegative(in.deceleration, 50.0f);
    out.airAcceleration = ClampNonNegative(in.airAcceleration, 12.0f);
    float airControl = Finite(in.airControl) ? in.airControl : 0.35f;
    out.airControl = std::clamp(airControl, 0.0f, 1.0f);
    out.jumpSpeed = ClampNonNegative(in.jumpSpeed, 5.5f);
    out.coyoteTime = ClampNonNegative(in.coyoteTime, 0.0f);
    out.jumpBufferTime = ClampNonNegative(in.jumpBufferTime, 0.0f);
    out.gravityScale = Finite(in.gravityScale) ? in.gravityScale : 1.0f;
    out.maxFallSpeed = ClampNonNegative(in.maxFallSpeed, 40.0f);
    out.mass = std::max(0.01f, ClampNonNegative(in.mass, 80.0f));
    out.pushStrength = ClampNonNegative(in.pushStrength, 1000.0f);

    outClamped = out.radius != in.radius
        || out.height != in.height
        || out.crouchedHeight != in.crouchedHeight
        || out.maxSlopeAngleDegrees != in.maxSlopeAngleDegrees
        || out.stepHeight != in.stepHeight
        || out.groundSnapDistance != in.groundSnapDistance
        || out.walkSpeed != in.walkSpeed
        || out.acceleration != in.acceleration
        || out.deceleration != in.deceleration
        || out.airAcceleration != in.airAcceleration
        || out.airControl != in.airControl
        || out.jumpSpeed != in.jumpSpeed
        || out.coyoteTime != in.coyoteTime
        || out.jumpBufferTime != in.jumpBufferTime
        || out.gravityScale != in.gravityScale
        || out.maxFallSpeed != in.maxFallSpeed
        || out.mass != in.mass
        || out.pushStrength != in.pushStrength;
    return out;
}

bool IsSlopeTooSteep(float normalDotUp, float maxSlopeAngleRadians)
{
    if (!Finite(normalDotUp) || !Finite(maxSlopeAngleRadians)) {
        return true;
    }
    return normalDotUp < std::cos(maxSlopeAngleRadians);
}

VelocityStepResult IntegrateCharacterVelocity(const VelocityStepInput& input)
{
    VelocityStepResult result;
    result.velocity = input.velocity;
    const float dt = std::max(0.0f, input.fixedDt);
    const PlanarWish wish = NormalizePlanarWish(input.wish.x, input.wish.z);
    const bool hasWish = wish.x != 0.0f || wish.z != 0.0f;
    const Vec3 desired(wish.x * input.targetSpeed, 0.0f, wish.z * input.targetSpeed);
    const Vec3 gravityStep(input.gravity.x * input.gravityScale * dt,
                           input.gravity.y * input.gravityScale * dt,
                           input.gravity.z * input.gravityScale * dt);

    const bool onGround = input.ground == CharacterGroundState::OnGround;
    const float relativeUp = input.velocity.y - input.groundVelocity.y;
    const bool jumpLegal = input.jump && onGround && relativeUp < kJumpAwayTolerance;

    auto planarRelative = [&](const Vec3& base) {
        if (input.instantPlanarSpeed) {
            return desired;
        }
        Vec3 relative = Planar(base);
        if (hasWish) {
            const float rate = onGround ? input.acceleration : input.airAcceleration * input.airControl;
            relative = MoveTowards(relative, desired, rate * dt);
        } else if (onGround) {
            relative = MoveTowards(relative, Vec3{}, input.deceleration * dt);
        }
        return relative;
    };

    if (onGround) {
        const Vec3 relative = planarRelative(Planar(vsub(input.velocity, input.groundVelocity)));
        result.velocity.x = input.groundVelocity.x + relative.x;
        result.velocity.z = input.groundVelocity.z + relative.z;
        result.velocity.y = input.groundVelocity.y;
        if (jumpLegal) {
            result.velocity.y = input.groundVelocity.y + input.jumpSpeed;
            result.velocity.x += gravityStep.x;
            result.velocity.y += gravityStep.y;
            result.velocity.z += gravityStep.z;
            ClampDownward(result.velocity, input.maxFallSpeed);
            result.jumped = true;
        }
        return result;
    }

    Vec3 relative = planarRelative(Planar(input.velocity));
    if (input.jump && input.coyoteJump) {
        result.velocity.y = input.jumpSpeed;
        result.jumped = true;
    }
    if (input.instantPlanarSpeed) {
        result.velocity.x = desired.x;
        result.velocity.z = desired.z;
    } else {
        result.velocity.x = relative.x;
        result.velocity.z = relative.z;
    }
    result.velocity.x += gravityStep.x;
    result.velocity.y += gravityStep.y;
    result.velocity.z += gravityStep.z;
    ClampDownward(result.velocity, input.maxFallSpeed);
    return result;
}

JumpDecision StepJumpGate(JumpGate& gate, bool requested, CharacterGroundState ground, float coyoteTime,
                          float bufferTime, float dt)
{
    JumpDecision decision;
    const bool grounded = ground == CharacterGroundState::OnGround;
    if (grounded) {
        gate.sinceGrounded = 0.0f;
        gate.jumpedSinceGrounded = false;
    } else {
        gate.sinceGrounded += std::max(0.0f, dt);
    }
    if (requested) {
        gate.bufferRemaining = std::max(gate.bufferRemaining, std::max(0.0f, bufferTime) + kGateEpsilon);
    }
    if (gate.bufferRemaining <= 0.0f) {
        return decision;
    }
    const bool coyote = !grounded && !gate.jumpedSinceGrounded && gate.sinceGrounded <= coyoteTime + kGateEpsilon &&
                        coyoteTime > 0.0f;
    if (grounded || coyote) {
        decision.jump = true;
        decision.coyote = coyote;
        gate.bufferRemaining = 0.0f;
        gate.jumpedSinceGrounded = true;
        return decision;
    }
    gate.bufferRemaining = std::max(0.0f, gate.bufferRemaining - std::max(0.0f, dt));
    return decision;
}

const char* CharacterGroundStateName(CharacterGroundState state)
{
    switch (state) {
    case CharacterGroundState::OnGround: return "OnGround";
    case CharacterGroundState::OnSteepGround: return "OnSteepGround";
    case CharacterGroundState::NotSupported: return "NotSupported";
    case CharacterGroundState::InAir: return "InAir";
    }
    return "InAir";
}

} // namespace Physics
