# Material & Lighting Visual Reference

## Material Types Demonstrated

### Cube 1 - Metallic Red (Metal)
- **Position**: (0, 0, 0) - Center
- **Color**: Red (0.8, 0.2, 0.2)
- **Metallic**: 0.9 (highly metallic)
- **Roughness**: 0.2 (smooth, shiny)
- **Appearance**: Shiny red metal with strong reflections

### Cube 2 - Plastic Green (Dielectric)
- **Position**: (2, 0, 0) - Right
- **Color**: Green (0.2, 0.8, 0.2)
- **Metallic**: 0.0 (non-metallic plastic)
- **Roughness**: 0.5 (semi-matte)
- **Scale**: 0.5x (smaller cube)
- **Appearance**: Matte green plastic with moderate highlights

### Cube 3 - Rough Blue (Dielectric)
- **Position**: (-2, 0, 0) - Left
- **Color**: Blue (0.2, 0.4, 0.8)
- **Metallic**: 0.0 (non-metallic)
- **Roughness**: 0.9 (very rough/matte)
- **Rotation**: 45° on Y-axis
- **Appearance**: Very matte blue surface with diffuse shading

### Cube 4 - Emissive Yellow (Glowing)
- **Position**: (0, 2, 0) - Above center
- **Color**: Yellow (0.8, 0.8, 0.2)
- **Metallic**: 0.0
- **Roughness**: 0.3
- **Emissive**: Yellow (1.0, 0.8, 0.2)
- **Emissive Strength**: 2.0
- **Scale**: 0.3x (small glowing cube)
- **Appearance**: Self-illuminated yellow cube that glows

## Lighting Setup

### Directional Light (Sun)
- **Direction**: (0.3, -1.0, 0.5) - coming from upper-right
- **Color**: Warm white (1.0, 0.95, 0.9)
- **Intensity**: 0.8
- **Effect**: Provides main illumination, creates visible shading across cube faces

### Point Light 1 (Red)
- **Position**: (3, 2, 2) - right side, elevated
- **Color**: Red (1.0, 0.3, 0.3)
- **Intensity**: 5.0
- **Range**: 10.0 units
- **Effect**: Casts red tinted light on nearby objects, especially visible on right side

### Point Light 2 (Blue)
- **Position**: (-3, 2, 2) - left side, elevated
- **Color**: Blue (0.3, 0.3, 1.0)
- **Intensity**: 5.0
- **Range**: 10.0 units
- **Effect**: Casts blue tinted light on nearby objects, especially visible on left side

### Ambient Light
- **Color**: Cool blue-gray (0.1, 0.1, 0.15)
- **Intensity**: 0.3
- **Effect**: Provides base illumination in shadowed areas

## What to Look For

### Material Differences
1. **Metal vs Dielectric**
   - Compare Cube 1 (metal) with Cube 2/3 (plastic)
   - Metals have colored reflections matching their albedo
   - Dielectrics have white/gray reflections

2. **Roughness Variation**
   - Cube 1 (smooth): Sharp, focused highlights
   - Cube 2 (medium): Softer, spread-out highlights
   - Cube 3 (rough): Very diffuse, almost no visible highlights

3. **Emissive Material**
   - Cube 4 glows even in dark areas
   - Not affected by lights (self-illuminated)
   - Stands out in low ambient lighting

### Lighting Effects
1. **Directional Light**
   - Creates consistent shading across all objects
   - Notice the gradient from light to dark on each cube face
   - Simulates sunlight

2. **Point Lights**
   - Red light tints the right side of objects
   - Blue light tints the left side of objects
   - Light intensity falls off with distance
   - Creates color mixing where both lights overlap

3. **Color Temperature**
   - Directional light is warm (yellowish)
   - Ambient light is cool (bluish)
   - Creates natural-looking lighting contrast

### PBR Features to Observe

1. **Energy Conservation**
   - Rough surfaces appear darker overall (light scatters)
   - Smooth surfaces have bright highlights
   - Total reflected light never exceeds incoming light

2. **Fresnel Effect**
   - Look at cube edges/corners at grazing angles
   - Even rough surfaces show some reflection at edges
   - More pronounced on smooth materials

3. **Multiple Light Interaction**
   - Objects lit by multiple lights show combined colors
   - Red + Blue lights = Purple/magenta tint
   - Warm sun + colored point lights = complex color gradients

## Editor Controls

### Material Editing
Select any cube and adjust in the Properties panel:
- **Albedo**: Change base color
- **Metallic**: 0 = plastic, 1 = metal
- **Roughness**: 0 = mirror-like, 1 = matte
- **Emissive**: Add self-illumination

### Light Editing
Select a light entity to adjust:
- **Type**: Switch between Directional/Point/Spot
- **Color**: Tint the light
- **Intensity**: Brightness
- **Range**: Distance for point/spot lights

### Camera Controls
- **Right Mouse + Move**: Look around
- **Right Mouse + WASD**: Move horizontally
- **Right Mouse + Q/E**: Move up/down
- **Mouse Wheel**: Adjust movement speed

## Testing Suggestions

1. **Test Different Roughness Values**
   - Select a cube and slowly adjust roughness from 0 to 1
   - Watch how highlights change from sharp to diffuse

2. **Test Metallic Transition**
   - Select a cube and adjust metallic from 0 to 1
   - Notice how reflections become colored

3. **Move Lights Around**
   - Select a point light and use the gizmo to move it
   - Watch how lighting changes dynamically

4. **Create New Objects**
   - Add more cubes with different materials
   - Experiment with emissive materials as "light sources"

5. **Change Light Colors**
   - Try different light colors to create moods
   - Green + red lights create interesting contrasts

6. **Adjust Ambient Lighting**
   - In code, modify `lightUBO.ambientColor` and `ambientIntensity`
   - See how it affects shadowed areas

## Performance Tips

- The current setup supports 1 directional + up to 4 point lights
- Each object updates its material buffer every frame
- Transparent/translucent materials not yet supported
- No shadows (objects don't occlude light)

## Troubleshooting

**Objects appear too dark:**
- Increase directional light intensity
- Increase ambient light intensity
- Check that lights are not disabled

**Objects appear flat:**
- Verify normals are correct (check vertex data)
- Ensure lighting UBO is being updated
- Check that shaders compiled successfully

**Wrong colors:**
- Verify albedo colors in material component
- Check light colors are normalized (0-1 range)
- Ensure gamma correction is working (colors should look natural)

**No highlights:**
- Increase light intensity
- Decrease roughness value
- Position camera to see specular reflections
- Check that camera position is being passed to shader
