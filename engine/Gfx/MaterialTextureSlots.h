#pragma once

#include <cstdint>
#include <vector>

/// Size of the material texture array (descriptor binding 3). Must match MAX_TEXTURE_SLOTS in
/// shaders/triangle.frag and shaders/shadowmap_masked.frag.
inline constexpr uint32_t kMaxMaterialTextureSlots = 512;
inline constexpr uint32_t kNoMaterialTextureSlot = UINT32_MAX;

/// When the array is full: the least recently used slot (by the frame it was last drawn with) that no frame still
/// in flight can be reading, never `keep` (the default texture); kNoMaterialTextureSlot when all are still in use.
inline uint32_t PickIdleMaterialTextureSlot(const std::vector<uint64_t>& lastUsedFrames, uint32_t keep, uint64_t frame,
                                            uint64_t framesInFlight)
{
    uint32_t best = kNoMaterialTextureSlot;
    for (uint32_t slot = 0; slot < lastUsedFrames.size(); ++slot) {
        if (slot == keep || frame - lastUsedFrames[slot] < framesInFlight) continue;
        if (best == kNoMaterialTextureSlot || lastUsedFrames[slot] < lastUsedFrames[best]) best = slot;
    }
    return best;
}
