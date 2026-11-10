/**
 * @file CharacterControllerComponent.h
 * @brief Authored locomotion settings for the general character controller.
 *
 * This is not CharacterComponent. CharacterComponent owns the skeletal asset
 * and the animation Lua slot. A capsule with only this component can simulate.
 */

#pragma once

#include "../Physics/PhysicsLayerConfig.h"

#include <cstdint>

struct CharacterControllerComponent {
    bool enabled = true;

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
    float coyoteTime = 0.0f;      // Seconds after walking off a ledge in which a jump still works.
    float jumpBufferTime = 0.0f;  // Seconds a jump pressed in the air waits to happen on landing.
    float gravityScale = 1.0f;
    float maxFallSpeed = 40.0f;

    std::uint16_t collisionLayer = Physics::kLayerCharacter;
    float mass = 80.0f;
    float pushStrength = 1000.0f;
};
