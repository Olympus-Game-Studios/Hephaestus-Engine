# Lighting and Materials System

## Overview
The Hephaestus Engine now features a complete PBR (Physically Based Rendering) lighting and material system with support for multiple light types and customizable materials.

## Features Added

### 1. Material Component (`MaterialComponent`)
Each entity can now have a material that defines its surface properties:

- **Albedo** - Base color of the material (RGB)
- **Metallic** - How metallic the surface is (0 = dielectric/plastic, 1 = metal)
- **Roughness** - Surface roughness (0 = smooth/glossy, 1 = rough/matte)
- **Ambient Occlusion (AO)** - Indirect lighting approximation (0-1)
- **Emissive** - Self-illumination color
- **Emissive Strength** - Multiplier for emissive color

#### Usage Example:
```cpp
Entity cube = scene.CreateEntity("My Cube");
auto& material = cube.AddComponent<MaterialComponent>(Vec3(0.8f, 0.2f, 0.2f)); // Red
material.metallic = 0.9f;
material.roughness = 0.2f;
```

### 2. Light Component (`LightComponent`)
Three types of lights are supported:

#### Directional Light
- Simulates distant light sources (like the sun)
- Affects all objects equally regardless of distance
- Defined by direction, color, and intensity

```cpp
Entity dirLight = scene.CreateEntity("Sun");
dirLight.AddComponent<LightComponent>(LightComponent::CreateDirectional(
    Vec3(0.3f, -1.0f, 0.5f),  // Direction
    Vec3(1.0f, 0.95f, 0.9f),  // Color (warm white)
    0.8f                       // Intensity
));
```

#### Point Light
- Omnidirectional light source
- Light intensity falls off with distance
- Defined by position, color, intensity, and range
- Supports attenuation (constant, linear, quadratic)

```cpp
Entity pointLight = scene.CreateEntity("Point Light");
auto& transform = pointLight.AddComponent<TransformComponent>();
transform.position = Vec3(3.0f, 2.0f, 2.0f);
pointLight.AddComponent<LightComponent>(LightComponent::CreatePoint(
    Vec3(1.0f, 0.3f, 0.3f),  // Color (red)
    5.0f,                     // Intensity
    10.0f                     // Range
));
```

#### Spot Light
- Cone-shaped light (like a flashlight)
- Defined by position, direction, color, intensity, range, and cone angles
- Has inner and outer cone angles for smooth falloff

```cpp
Entity spotLight = scene.CreateEntity("Spot Light");
spotLight.AddComponent<LightComponent>(LightComponent::CreateSpot(
    Vec3(1.0f, 1.0f, 1.0f),  // Color (white)
    3.0f,                     // Intensity
    15.0f,                    // Range
    0.785f,                   // Inner angle (45°)
    0.959f                    // Outer angle (55°)
));
```

### 3. PBR Shader Implementation
The fragment shader implements a full PBR lighting model:

- **Cook-Torrance BRDF** for specular reflection
- **GGX/Trowbridge-Reitz** normal distribution function
- **Schlick-GGX** geometry function with Smith's method
- **Fresnel-Schlick** approximation for reflectance
- **Energy conservation** between diffuse and specular
- **HDR tone mapping** (Reinhard)
- **Gamma correction** for proper color space

### 4. Scene Lighting Configuration
The lighting system supports:
- 1 directional light (typically used for sun/moon)
- Up to 4 point lights
- Ambient lighting with customizable color and intensity

### 5. Editor Integration
The Properties panel now includes:

#### Material Editor
- Color picker for albedo
- Sliders for metallic (0-1)
- Sliders for roughness (0-1)
- Sliders for ambient occlusion (0-1)
- Color picker for emissive
- Slider for emissive strength (0-10)

#### Light Editor
- Dropdown to select light type
- Color picker for light color
- Slider for intensity
- Range control for point/spot lights
- Attenuation controls (constant, linear, quadratic)
- Direction control for directional lights
- Cone angle controls for spot lights

## Technical Details

### Uniform Buffers
Three uniform buffer objects are used per frame:

1. **Camera UBO** (Binding 0)
   - View matrix
   - Projection matrix
   - Camera position (for specular calculations)

2. **Material UBO** (Binding 1)
   - Albedo, metallic, roughness, AO
   - Emissive color and strength
   - Updated per-object during rendering

3. **Lighting UBO** (Binding 2)
   - Directional light parameters
   - Up to 4 point light parameters
   - Ambient lighting settings
   - Updated once per frame

### Vertex Format
Each vertex now includes:
- Position (vec3)
- Normal (vec3) - required for lighting calculations
- Color (vec3) - tints the albedo

### Shader Pipeline
1. Vertex shader transforms positions and normals to world space
2. Fragment shader receives world position and normal
3. PBR lighting calculations per pixel
4. HDR tone mapping and gamma correction applied

## Example Scene Setup

```cpp
Scene scene("Main Scene");

// Create objects with different materials
Entity metalCube = scene.CreateEntity("Metal Cube");
metalCube.AddComponent<MeshRendererComponent>();
auto& metalMat = metalCube.AddComponent<MaterialComponent>(Vec3(0.8f, 0.2f, 0.2f));
metalMat.metallic = 0.9f;
metalMat.roughness = 0.2f;

Entity plasticCube = scene.CreateEntity("Plastic Cube");
plasticCube.AddComponent<MeshRendererComponent>();
auto& plasticMat = plasticCube.AddComponent<MaterialComponent>(Vec3(0.2f, 0.8f, 0.2f));
plasticMat.metallic = 0.0f;
plasticMat.roughness = 0.5f;

// Add a directional light (sun)
Entity sun = scene.CreateEntity("Sun");
sun.AddComponent<LightComponent>(LightComponent::CreateDirectional(
    Vec3(0.3f, -1.0f, 0.5f), Vec3(1.0f, 0.95f, 0.9f), 0.8f));

// Add colored point lights
Entity redLight = scene.CreateEntity("Red Light");
auto& redPos = redLight.AddComponent<TransformComponent>();
redPos.position = Vec3(3.0f, 2.0f, 2.0f);
redLight.AddComponent<LightComponent>(LightComponent::CreatePoint(
    Vec3(1.0f, 0.3f, 0.3f), 5.0f, 10.0f));
```

## Future Enhancements
Potential improvements for the lighting system:

1. **Shadow Mapping** - Add real-time shadows
2. **Image-Based Lighting (IBL)** - Environment maps for ambient lighting
3. **Normal Mapping** - Per-pixel normal details
4. **Texture Support** - Albedo, roughness, metallic, and normal textures
5. **Light Cookies** - Projected textures for spot lights
6. **Area Lights** - More realistic light sources
7. **Cascaded Shadow Maps** - Better shadows for large scenes
8. **Screen Space Reflections (SSR)** - Real-time reflections
9. **Bloom** - HDR glow effect for bright objects
10. **Deferred Rendering** - Support for many more lights

## Performance Notes

- The current implementation uses forward rendering
- Material uniform buffer is updated per-object (could be optimized with instancing)
- Lighting uniform buffer is updated once per frame
- Shader performs full PBR calculations per pixel
- Point lights have range-based culling to skip distant lights

## References

- [Real Shading in Unreal Engine 4](https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf)
- [PBR Theory - LearnOpenGL](https://learnopengl.com/PBR/Theory)
- [Physically Based Rendering - Marmoset](https://marmoset.co/posts/physically-based-rendering-and-you-can-too/)
