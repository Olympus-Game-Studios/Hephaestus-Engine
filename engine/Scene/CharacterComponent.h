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
    Hephaestus::Skeleton skeleton;
    std::vector<Hephaestus::AnimationClip> clips;
    Hephaestus::AnimationPlayer player;
    std::vector<Hephaestus::BoneLocalPose> localPoses;
    std::vector<Mat4> localMatrices;
    std::vector<Mat4> worldPose;
    std::vector<Mat4> bonePalette;
    std::string lastError;
};
