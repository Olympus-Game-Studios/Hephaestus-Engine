#pragma once

#include "../Core/Math.h"

#include <cstdint>
#include <vector>

namespace Hephaestus::AudioMath {

/// Gain of a source at distance: 1 inside minDistance, inverse-distance beyond it, fading to 0 at maxDistance.
float DistanceGain(float distance, float minDistance, float maxDistance);

struct Spatial {
    float gain = 1.0f;
    float pan = 0.0f; ///< -1 left, +1 right
};

/// Gain and stereo pan of a source for a listener at listenerPosition looking along forward with up.
Spatial Spatialize(const Vec3& listenerPosition, const Vec3& forward, const Vec3& up, const Vec3& sourcePosition,
                   float minDistance, float maxDistance);

struct VoiceCandidate {
    int priority = 0;
    float audibility = 1.0f; ///< current gain the voice is heard at
    uint64_t startOrder = 0; ///< larger is newer
};

/// Index of the voice a new sound of newPriority should replace when the budget is full, or -1 to drop the new
/// sound. Lowest priority goes first, then the quietest, then the oldest; a voice of higher priority than the
/// new sound is never taken.
int ChooseVoiceToSteal(const std::vector<VoiceCandidate>& voices, int newPriority);

/// base scaled by a random factor in [1 - jitter, 1 + jitter]; unitRandom is in [0, 1).
float ApplyJitter(float base, float jitter, float unitRandom);

} // namespace Hephaestus::AudioMath
