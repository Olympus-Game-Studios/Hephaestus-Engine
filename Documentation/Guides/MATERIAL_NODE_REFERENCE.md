# Material Node Reference

This guide covers the Qt Material Editor node graph. Nodes serialize into `.hematerial` graph JSON and are evaluated through the editor material graph path. Shader-capable nodes also expose GLSL snippets through the material shader generator so future per-pixel material compilation can reuse the same node definitions.

## What renders today

`MaterialGraphEvaluator` (`engine/UI/MaterialEditor/MaterialGraphEvaluator.cpp`) evaluates the graph once into the material's PBR values and texture slots, and the renderer draws those with the fixed `triangle.frag` shader. Per pixel and time based nodes have no evaluated value, so they currently have no effect on the rendered material: TexCoord, Screen Position, Transform, Texture Tiling Offset, Noise, Gradient, Fresnel, UV Distortion, Time, Camera Vector, View Direction, Reflection Vector, World Position, Object Position, Distance To Camera and Static Switch Parameter. `MaterialShaderGenerator` produces GLSL for these nodes for diagnostics only; it is not compiled to SPIR-V or used by the renderer. The `Refraction` input is reserved.

## PBR

- `Material Output`: final material node. Inputs are Base Color, Metallic, Roughness, Emissive, Normal Map, Roughness Map, AO Map, Specular, Opacity, Opacity Mask, and Refraction. Existing materials that only have the older seven pins keep loading; new pins use default material values when disconnected.

## Inputs

- `Scalar`: constant float output.
- `Vector3`: constant RGB/vector output.
- `Camera Vector`: normalized vector from surface to camera.
- `View Direction`: normalized view direction.
- `Reflection Vector`: reflection vector from view direction and normal.
- `World Position`: fragment world position.
- `Object Position`: object origin position for generated shader paths.
- `Distance To Camera`: distance from fragment to camera.

## Parameters

- `Float Parameter`: named scalar parameter.
- `Color Parameter`: named vector/color parameter.
- `Texture Parameter`: named texture reference.
- `Bool Parameter`: named boolean represented as `0.0` or `1.0`.
- `Vector2 Parameter`: named `vec2` parameter.
- `Static Switch Parameter`: selects between two values using a stored boolean switch. This is future-ready for material instances.

## Textures

- `Texture Sample`: generic texture sampler with optional UV input and RGBA/RGB/R outputs.
- `Texture Object`: texture reference without sampling.
- `Texture Sample From Object`: samples a `Texture Object` or `Texture Parameter`.
- `Texture Tiling Offset`: applies UV tiling and offset.
- `Triplanar Texture`: world-position/world-normal texture projection hook.
- `Normal Unpack`: converts packed normal-map color from `[0,1]` to signed normalized form.
- Legacy texture nodes remain supported: `Albedo Texture`, `Normal Texture`, `Roughness Texture`, `AO Texture`, and `World Aligned Texture`.

## Math

- `Add`, `Subtract`, `Multiply`, `Divide`: binary arithmetic.
- `Clamp`: clamps value between min and max.
- `Saturate`: clamps value to `0..1`.
- `Power`: raises base to exponent.
- `Abs`, `Min`, `Max`, `One Minus`: common scalar utilities.
- `Dot Product`, `Cross Product`, `Normalize`, `Length`: vector operations.

Scalar outputs can connect to compatible vector inputs; the graph compiler expands scalars to matching vector constructors where valid.

## Utility

- `Component Mask`: selects R/G/B/A channels.
- `Append Vector`: combines scalar values into vectors.
- `Break Vector`: splits vector components into scalar outputs.
- `Transform`: transform-space metadata hook for World, Local, and View workflows.
- `TexCoord`: mesh UV coordinates.
- `Screen Position`: fragment screen position.

## Procedural

- `Noise`: deterministic value-noise style shader expression.
- `Gradient`: linear or radial gradient.
- `Fresnel`: view-dependent edge falloff.
- `UV Distortion`: offsets UVs by a vector and strength.
- `Time`: engine time uniform hook.

## Logic

- `If`: chooses between two values based on a comparison.
- `Step`: GLSL-style threshold.
- `Smooth Step`: smooth threshold.
- `Compare`: `>`, `<`, or `==` comparison producing `0.0` or `1.0`.

## Blend

- `Lerp`: linear interpolation.
- `Overlay`: overlay blend mode.
- `Screen`: screen blend mode.
- `Height Blend`: basic height/contrast layer blend.

## Compatibility Notes

The graph keeps `NodeType` string names as stable serialized keys. Unknown node and link payloads are preserved on save. New node settings such as `parameterName`, `operation`, transform spaces, mask channels, and boolean defaults are additive JSON fields.
