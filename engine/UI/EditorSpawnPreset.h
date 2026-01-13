#pragma once

#include <cstdint>

// Shared editor-only spawn presets.
// Keep this header lightweight so it can be used by both engine UI and the
// editor main loop.

enum class EditorSpawnPreset : uint32_t {
  DefaultCube = 0,
  DefaultSphere = 1,
  DirectionalLight = 2,
  PointLight = 3,
  SpotLight = 4,
  EmptyEntity = 5,
  Camera = 6,
};
