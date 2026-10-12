/**
 * @file CharacterControllerMath.h
 * @brief Wish, acceleration, jump, and parameter clamping. No Jolt types.
 */

#pragma once

#include "../Core/Math.h"

namespace Physics {

enum class CharacterGroundState {
    OnGround,
    OnSteepGround,
    NotSupported,
    InAir
};

struct ControllerAuthoredValues {
    float height = 1.8f;
    float radius = 0.3f;
    float crouchedHeight = 1.1f;
    float maxSlopeAngleDegrees = 45.0f;
    float stepHeight = 0.30f;
    float groundSnapDistance = 0.40f;
    float walkSpeed = 5.0f;
    float acceleration = 40.0f;
    float deceleration = 50.0f;
    float airAcceleration = 12.0f;
    float airControl = 0.35f;
    float jumpSpeed = 5.5f;
    float gravityScale = 1.0f;
    float maxFallSpeed = 40.0f;
    float mass = 80.0f;
    float pushStrength = 1000.0f;
};

struct PlanarWish {
    float x = 0.0f;
    float z = 0.0f;
};

struct VelocityStepInput {
    CharacterGroundState ground = CharacterGroundState::InAir;
    Vec3 velocity{};
    Vec3 groundVelocity{};
    PlanarWish wish{};
    float targetSpeed = 5.0f;
    float acceleration = 40.0f;
    float deceleration = 50.0f;
    float airAcceleration = 12.0f;
    float airControl = 0.35f;
    Vec3 gravity{};
    float gravityScale = 1.0f;
    float fixedDt = 1.0f / 60.0f;
    float maxFallSpeed = 40.0f;
    bool jump = false;
    float jumpSpeed = 5.5f;
    bool instantPlanarSpeed = false;
};

struct VelocityStepResult {
    Vec3 velocity{};
    bool jumped = false;
};

PlanarWish NormalizePlanarWish(float x, float z);
PlanarWish ProjectPlanarWish(float x, float y, float z);

ControllerAuthoredValues ClampControllerAuthored(const ControllerAuthoredValues& in, bool& outClamped);

bool IsSlopeTooSteep(float normalDotUp, float maxSlopeAngleRadians);

VelocityStepResult IntegrateCharacterVelocity(const VelocityStepInput& input);

const char* CharacterGroundStateName(CharacterGroundState state);

} // namespace Physics
