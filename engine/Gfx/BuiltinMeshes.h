#pragma once

namespace Hephaestus {

class EngineFrameRenderer;

// Uploads builtin://cube (1x1x1), builtin://plane, builtin://sphere (radius 0.5)
// and builtin://skinned_rest_fixture. Editor and packaged game share these.
void AddBuiltinMeshes(EngineFrameRenderer& renderer);

} // namespace Hephaestus
