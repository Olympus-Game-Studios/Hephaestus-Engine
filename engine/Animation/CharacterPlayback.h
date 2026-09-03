/**
 * @file CharacterPlayback.h
 * @brief Character clip playback tick: commit Play, then evaluate pose this frame.
 */

#pragma once

#include "AnimationClip.h"

#include <string>
#include <vector>

struct CharacterComponent;
class Scene;

namespace Hephaestus {

class CharacterAnimationSystem {
public:
    static bool BindRuntime(CharacterComponent& character,
                            Skeleton skeleton,
                            std::vector<AnimationClip> clips,
                            std::string& outError);

    static bool Play(CharacterComponent& character, const std::string& name, std::string& outError);
    static bool CrossFade(CharacterComponent& character,
                          const std::string& name,
                          float duration,
                          std::string& outError);
    static void Pause(CharacterComponent& character);
    static void Resume(CharacterComponent& character);
    static void Stop(CharacterComponent& character);
    static void SetSpeed(CharacterComponent& character, float speed);

    /// Commit pending Play/CrossFade, advance (except the commit frame), evaluate pose.
    static void TickEntity(CharacterComponent& character, float deltaTime);

    /// Play-mode host tick. Runs after gameplay Lua onUpdate and before LateUpdate.
    static void Tick(Scene& scene, float deltaTime);
};

} // namespace Hephaestus
