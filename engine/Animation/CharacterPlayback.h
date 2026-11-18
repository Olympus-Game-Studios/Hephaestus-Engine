/**
 * @file CharacterPlayback.h
 * @brief Character clip playback tick: commit Play, then evaluate pose this frame.
 */

#pragma once

#include "AnimationClip.h"
#include "../Assets/CharacterAsset.h"
#include "../Core/EditorDebugDraw.h"

#include <string>
#include <vector>

struct CharacterComponent;
struct CharacterAnimationLayer;
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
    /// Adds or updates the layer named in `layer` (clips, mask, target weight, fade and rate; its phase and current
    /// weight carry over when it already exists). Fails when a clip is missing or a mask bone is not found.
    static bool SetLayer(CharacterComponent& character, const CharacterAnimationLayer& layer, std::string& outError);
    /// Fades the layer out over `fadeSeconds` and removes it; false when there is no such layer.
    static bool ClearLayer(CharacterComponent& character, const std::string& name, float fadeSeconds);
    /// Per-bone share of a layer: 1 inside `maskRoot`'s subtree (all bones when empty), 0 outside, and inside
    /// `maskExclude`'s subtree `excludeBlend` halved for each level below it.
    static bool BuildLayerMask(const Skeleton& skeleton, const CharacterAnimationLayer& layer,
                               std::vector<float>& outWeights, std::string& outError);
    static void Pause(CharacterComponent& character);
    static void Resume(CharacterComponent& character);
    static void Stop(CharacterComponent& character);
    static bool SetSpeed(CharacterComponent& character, float speed);

    /// Playing and not paused; with a clip id, only while that clip plays.
    static bool IsPlaying(const CharacterComponent& character, const char* clipId = nullptr);
    /// Trimmed length of the clip, or of the current clip when clipId is null; 0 when unknown.
    static float ClipDuration(const CharacterComponent& character, const char* clipId = nullptr);

    /// Sample `clipId` at absolute clip time (clamped to authored trim) and rebuild pose/palette.
    static bool SampleAtTime(CharacterComponent& character,
                             const std::string& clipId,
                             float time,
                             std::string& outError);

    /// Rebuild local/world/palette from the current player time (or rest pose).
    static void EvaluatePose(CharacterComponent& character);

    /// Commit pending Play/CrossFade, advance (except the commit frame), evaluate pose.
    static void TickEntity(CharacterComponent& character, float deltaTime);

    /// Play-mode host tick. Runs after gameplay Lua onUpdate and before LateUpdate.
    static void Tick(Scene& scene, float deltaTime);
};

Mat4 ComposeSocketLocal(const CharacterSocket& socket);
bool EvaluateSocketWorld(const CharacterComponent& character,
                         const CharacterSocket& socket,
                         Mat4& outWorld,
                         std::string& outError);
void DecomposeSocketLocal(const Mat4& local, CharacterSocket& socket);
void FillSkeletonDebugOverlay(const CharacterComponent& character,
                              const std::string& selectedBone,
                              std::vector<EditorDebugLine>& outLines,
                              std::vector<EditorDebugPoint>& outPoints);

} // namespace Hephaestus
