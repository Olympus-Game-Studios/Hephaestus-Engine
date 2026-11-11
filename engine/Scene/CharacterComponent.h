/**
 * @file CharacterComponent.h
 * @brief Designer-facing Character ECS component. Controller Lua runs through LuaScriptSlot::CharacterController.
 */

#pragma once

#include "../Animation/AnimationClip.h"
#include "../Animation/Skeleton.h"
#include "../Core/PropertyValue.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// A pose laid over the character's main animation on the bones its mask covers, such as strafing legs under an
/// aiming upper body. Its clips play at one shared phase (0..1) so their steps stay in time, mixed by weight.
struct CharacterAnimationLayer {
    struct Clip {
        std::string id;
        float weight = 1.0f;
        bool reverse = false; ///< play backwards, such as a forward walk for walking backwards
    };
    std::string name;
    std::vector<Clip> clips;
    std::string maskRoot;      ///< bone (by name without prefix) whose subtree the layer drives; empty = all
    std::string maskExclude;   ///< subtree left to the layers below
    float excludeBlend = 0.0f; ///< share the excluded bone still takes, halving for each bone below it
    float weight = 0.0f;
    float targetWeight = 1.0f;
    float fadeSeconds = 0.0f;  ///< time to reach targetWeight from 0 or back
    float rate = 1.0f;
    float phase = 0.0f;
    bool removeWhenFaded = false;
    std::vector<float> boneWeights;
};

struct CharacterComponent {
    std::string characterPath;
    std::string defaultAnimation;
    float playbackSpeed = 1.0f;
    bool runController = true;
    std::string controllerScriptPath;
    std::unordered_map<std::string, PropertyValue> controllerVariables;
    std::unordered_set<std::string> authoredControllerVariables;
    bool authoredControllerVariablesInitialized = false;
    std::vector<std::string> materialOverrides;

    bool runtimeReady = false;
    bool suppressAutoPlay = false;
    bool externalPose = false; ///< A ragdoll writes worldPose; animation does not tick.
    Hephaestus::Skeleton skeleton;
    std::vector<Hephaestus::AnimationClip> clips;
    Hephaestus::AnimationPlayer player;
    std::vector<CharacterAnimationLayer> layers;
    std::vector<Hephaestus::BoneLocalPose> localPoses;
    std::vector<Mat4> localMatrices;
    std::vector<Mat4> worldPose;
    std::vector<Mat4> bonePalette;
    std::string lastError;
};
