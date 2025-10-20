# Quick Reference: Ray Picking Implementation

## Files Modified

### 1. `engine/Core/Math.h`
**Added:**
- `Mat4::Invert()` - Matrix inversion
- `Mat4::TransformPoint()` - Transform point by matrix (w=1)
- `Mat4::TransformDirection()` - Transform direction by matrix (w=0)
- `Ray` struct - Ray with origin and direction
- `AABB` struct - Axis-aligned bounding box with ray intersection test

### 2. `engine/Scene/Transform.h`
**Added:**
- `SceneObject::localBounds` - Local space AABB (default: unit cube)
- `SceneObject::GetWorldBounds()` - Compute world-space AABB

### 3. `engine/Core/main.cpp`
**Added:**
- `ScreenToWorldRay()` - Convert screen coords to world-space ray
- `FindClosestHitObject()` - Find closest object hit by ray
- Mouse click handler - Detects left clicks and performs ray picking
- UI improvements - Green highlight for selected objects

## Key Functions

### ScreenToWorldRay
```cpp
Ray ScreenToWorldRay(double mouseX, double mouseY, 
                     int screenWidth, int screenHeight,
                     const Mat4& viewMatrix, 
                     const Mat4& projMatrix)
```
Converts 2D screen position to 3D world-space ray.

### FindClosestHitObject
```cpp
int FindClosestHitObject(const Ray& ray, 
                        const std::vector<SceneObject>& objects,
                        float& outDistance)
```
Returns index of closest object hit by ray (-1 if none).

### AABB::IntersectRay
```cpp
bool IntersectRay(const Ray& ray, float& tMin, float& tMax) const
```
Tests if ray intersects axis-aligned bounding box.

## Usage Example

```cpp
// Get mouse position
double mouseX, mouseY;
glfwGetCursorPos(window, &mouseX, &mouseY);

// Create view and projection matrices
UniformBufferObject ubo = camera.makeUBO(swap.extent);

// Create picking ray
Ray pickRay = ScreenToWorldRay(
    mouseX, mouseY, 
    swap.extent.width, swap.extent.height,
    ubo.view, ubo.proj
);

// Find hit object
float hitDist;
int hitIndex = FindClosestHitObject(pickRay, sceneObjects, hitDist);

if (hitIndex >= 0) {
    // Select the hit object
    sceneObjects[hitIndex].selected = true;
}
```

## Input Conditions

Ray picking only activates when:
- ✅ Left mouse button pressed (rising edge)
- ✅ Not interacting with ImGui (`!io.WantCaptureMouse`)
- ✅ Not using ImGuizmo (`!ImGuizmo::IsUsing()`)
- ✅ Not hovering ImGuizmo (`!ImGuizmo::IsOver()`)

## Coordinate Transformations

```
Screen Space (pixels)
    ↓ Normalize to [-1, 1]
NDC Space
    ↓ Inverse Projection Matrix
View Space (camera-relative)
    ↓ Inverse View Matrix
World Space
```

## Bounding Box Transform

For accurate picking with scaled/rotated objects:
1. Start with local AABB (e.g., -0.5 to 0.5)
2. Transform all 8 corners to world space
3. Compute new AABB that encompasses transformed corners

## Performance

- **Current**: O(n) where n = number of objects
- **Typical**: 3 objects = ~3 ray-box tests per click
- **Acceptable for**: Scenes with hundreds of objects
- **For larger scenes**: Consider spatial partitioning (octree, BVH)

## Testing Checklist

- [ ] Click on Cube 1 - selects correctly
- [ ] Click on Cube 2 - selects correctly  
- [ ] Click on Cube 3 - selects correctly
- [ ] Click empty space - deselects all
- [ ] Scene Hierarchy shows green highlight
- [ ] Properties panel updates with selection
- [ ] ImGuizmo appears on selected object
- [ ] Selection works with camera movement
- [ ] Selection works with scaled objects
- [ ] Selection works with rotated objects
