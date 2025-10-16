# Ray Picking Implementation

## Overview
Ray picking (also called ray casting) allows users to select 3D objects by clicking on them in the viewport. This is implemented by casting a ray from the camera through the clicked pixel and testing which objects it intersects.

## How It Works

### 1. Math Foundation (`Math.h`)
Added new math structures and functions:

- **`Ray`**: Represents a ray with an origin and normalized direction
- **`AABB`**: Axis-Aligned Bounding Box for efficient intersection testing
- **`Mat4::Invert()`**: Matrix inversion for transforming screen to world coordinates
- **`Mat4::TransformPoint()`**: Transform a 3D point by a matrix
- **`Mat4::TransformDirection()`**: Transform a direction vector by a matrix
- **`AABB::IntersectRay()`**: Ray-box intersection using slab method

### 2. Scene Objects (`Transform.h`)
Enhanced `SceneObject` with:

- **`localBounds`**: AABB in local space (default unit cube: -0.5 to 0.5)
- **`GetWorldBounds()`**: Transforms the 8 corners of the local bounding box to world space and computes the world-space AABB

### 3. Ray Picking Functions (`main.cpp`)

#### `ScreenToWorldRay()`
Converts 2D screen coordinates to a 3D ray in world space:
1. Normalize mouse coordinates to NDC space [-1, 1]
2. Transform from NDC → View space (using inverse projection matrix)
3. Transform from View → World space (using inverse view matrix)
4. Extract camera position as ray origin

#### `FindClosestHitObject()`
Tests all objects for intersection:
1. Get world-space bounding box for each object
2. Test ray-AABB intersection
3. Track the closest hit (smallest positive distance)
4. Return the index of the hit object

### 4. Input Handling
Mouse click detection in the main loop:
- Only processes clicks when:
  - Left mouse button is pressed (on rising edge)
  - Not interacting with ImGui windows (`!io.WantCaptureMouse`)
  - Not using ImGuizmo (`!ImGuizmo::IsUsing()`)
  - Not hovering over ImGuizmo (`!ImGuizmo::IsOver()`)

## Usage

### For Users
1. **Select an object**: Left-click on any cube in the viewport
2. **Deselect**: Left-click on empty space
3. **Multiple selection methods**:
   - Click in viewport (ray picking)
   - Click in Scene Hierarchy window
   - Use ImGuizmo to manipulate selected object

### For Developers
To add ray picking to new object types:

```cpp
// 1. Ensure your object has a bounding box
SceneObject myObject("New Object");
myObject.localBounds = AABB(Vec3(-1, -1, -1), Vec3(1, 1, 1)); // Custom size

// 2. The picking system will automatically handle it
// No additional code needed - it works with the existing system
```

## Visual Feedback
- **Scene Hierarchy**: Selected objects appear in green
- **Properties Panel**: Shows selected object details or "No object selected"
- **ImGuizmo**: Gizmo appears on selected objects for manipulation

## Technical Details

### Ray-AABB Intersection Algorithm
Uses the "slab method":
- Tests intersection with 3 pairs of parallel planes (X, Y, Z)
- Computes entry/exit distances for each slab
- Ray intersects box if all slabs overlap

### Coordinate Spaces
- **Screen Space**: Window pixel coordinates (0,0) at top-left
- **NDC Space**: Normalized [-1, 1] with (0,0) at center
- **View Space**: Camera-relative coordinates
- **World Space**: Global 3D coordinates

### Performance Considerations
- Uses AABBs instead of precise mesh intersection (fast and sufficient)
- Only tests visible objects
- O(n) complexity where n = number of objects
- For large scenes, consider spatial partitioning (octree, BVH)

## Future Enhancements
Possible improvements:
1. **Precise mesh intersection**: Test against actual triangle mesh
2. **Multiple selection**: Ctrl+Click to add/remove from selection
3. **Box selection**: Click and drag to select multiple objects
4. **Selection highlighting**: Outline selected objects in viewport
5. **Frustum culling**: Only test objects visible to camera
6. **Spatial partitioning**: Use octree/BVH for large scenes

## Testing
To verify ray picking works:
1. Run the engine
2. Click on "Cube 1", "Cube 2", or "Cube 3"
3. Observe selection changes in Scene Hierarchy (green highlight)
4. Properties panel should show the selected object's details
5. ImGuizmo should appear on the selected object
