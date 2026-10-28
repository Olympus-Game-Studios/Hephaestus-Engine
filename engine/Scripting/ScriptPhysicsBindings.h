#pragma once

#include <functional>

#include "LuaScriptEngine.h"

class Scene;
namespace Physics {
class JoltPhysicsEngine;
class JoltPhysicsSystem;
class CharacterControllerSystem;
}

// Systems a host currently exposes to scripts; null members are unavailable.
struct ScriptPhysicsAccess {
    Scene* scene = nullptr;
    Physics::JoltPhysicsEngine* physicsEngine = nullptr;
    Physics::JoltPhysicsSystem* physicsSystem = nullptr;
    Physics::CharacterControllerSystem* characterControllers = nullptr;
};

using ScriptPhysicsAccessor = std::function<ScriptPhysicsAccess()>;

PhysicsBinding MakeScriptPhysicsBinding(ScriptPhysicsAccessor access);
CharacterBinding MakeScriptCharacterBinding(ScriptPhysicsAccessor access);
