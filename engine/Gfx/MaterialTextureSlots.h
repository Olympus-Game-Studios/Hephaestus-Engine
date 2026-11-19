#pragma once

#include <cstdint>

/// Size of the material texture array (descriptor binding 3). Must match MAX_TEXTURE_SLOTS in
/// shaders/triangle.frag and shaders/shadowmap_masked.frag.
inline constexpr uint32_t kMaxMaterialTextureSlots = 512;
