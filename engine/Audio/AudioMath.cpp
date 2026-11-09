#include "AudioMath.h"

#include <algorithm>
#include <cmath>

namespace Hephaestus::AudioMath {

float DistanceGain(float distance, float minDistance, float maxDistance)
{
    minDistance = std::max(minDistance, 1.0e-3f);
    maxDistance = std::max(maxDistance, minDistance);
    if (distance <= minDistance) {
        return 1.0f;
    }
    if (distance >= maxDistance) {
        return 0.0f;
    }
    const float inverse = minDistance / distance;
    const float fadeStart = minDistance + 0.8f * (maxDistance - minDistance);
    if (distance <= fadeStart) {
        return inverse;
    }
    return inverse * (maxDistance - distance) / (maxDistance - fadeStart);
}

Spatial Spatialize(const Vec3& listenerPosition, const Vec3& forward, const Vec3& up, const Vec3& sourcePosition,
                   float minDistance, float maxDistance)
{
    Spatial result;
    const Vec3 offset = vsub(sourcePosition, listenerPosition);
    const float distance = vlen(offset);
    result.gain = DistanceGain(distance, minDistance, maxDistance);
    const Vec3 right = vnorm(vcross(forward, up));
    if (distance > 1.0e-4f && vlen(right) > 0.0f) {
        const float side = vdot(vscale(offset, 1.0f / distance), right);
        // A source at the listener's head is not hard-panned.
        const float nearScale = std::min(1.0f, distance / std::max(minDistance, 1.0e-3f));
        result.pan = std::clamp(side * nearScale, -1.0f, 1.0f);
    }
    return result;
}

int ChooseVoiceToSteal(const std::vector<VoiceCandidate>& voices, int newPriority)
{
    int best = -1;
    for (int i = 0; i < static_cast<int>(voices.size()); ++i) {
        const VoiceCandidate& voice = voices[static_cast<size_t>(i)];
        if (voice.priority > newPriority) {
            continue;
        }
        if (best < 0) {
            best = i;
            continue;
        }
        const VoiceCandidate& current = voices[static_cast<size_t>(best)];
        if (voice.priority != current.priority) {
            if (voice.priority < current.priority) best = i;
        } else if (voice.audibility != current.audibility) {
            if (voice.audibility < current.audibility) best = i;
        } else if (voice.startOrder < current.startOrder) {
            best = i;
        }
    }
    return best;
}

float ApplyJitter(float base, float jitter, float unitRandom)
{
    jitter = std::clamp(jitter, 0.0f, 1.0f);
    return base * (1.0f + jitter * (2.0f * unitRandom - 1.0f));
}

} // namespace Hephaestus::AudioMath
