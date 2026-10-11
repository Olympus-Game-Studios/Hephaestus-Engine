#pragma once

namespace Physics {

inline constexpr float kPhysicsFixedDt = 1.0f / 60.0f;
inline constexpr int kPhysicsMaxSubsteps = 4;

struct PhysicsStepBudget {
    int substepCount = 0;
    float accumulator = 0.0f;
    bool stalled = false;
};

// Advances the fixed-step clock. A frame shorter than kPhysicsFixedDt carries
// the remainder. More than kPhysicsMaxSubsteps discards the leftover and stalls.
inline PhysicsStepBudget AdvancePhysicsAccumulator(float accumulator, float frameDt)
{
    constexpr float kEpsilon = 1.0e-5f;
    if (frameDt > 0.0f) {
        accumulator += frameDt;
    }
    if (accumulator < 0.0f) {
        accumulator = 0.0f;
    }

    int substeps = 0;
    while (substeps < kPhysicsMaxSubsteps && accumulator + kEpsilon >= kPhysicsFixedDt) {
        accumulator -= kPhysicsFixedDt;
        ++substeps;
    }
    if (accumulator < 0.0f && accumulator > -kEpsilon) {
        accumulator = 0.0f;
    }

    bool stalled = false;
    if (substeps == kPhysicsMaxSubsteps && accumulator + kEpsilon >= kPhysicsFixedDt) {
        stalled = true;
        accumulator = 0.0f;
    }
    return PhysicsStepBudget{substeps, accumulator, stalled};
}

} // namespace Physics
