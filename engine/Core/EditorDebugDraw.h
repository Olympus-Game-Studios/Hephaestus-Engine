#pragma once

#include "Math.h"

#include <vector>

struct EditorDebugLine {
    Vec3 a{};
    Vec3 b{};
    Vec3 color{1.0f, 1.0f, 1.0f};
};

struct EditorDebugPoint {
    Vec3 p{};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float size = 0.025f;
};
