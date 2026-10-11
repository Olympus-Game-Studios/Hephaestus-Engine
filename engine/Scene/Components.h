#pragma once
#include "../Core/Math.h"
#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <limits>
#include <utility>
#include <unordered_map>
#include <optional>

// Forward include for Lua scripting
#include "../Scripting/LuaScriptComponent.h"
#include "CharacterComponent.h"
#include "../Core/PropertyValue.h"

// ═══════════════════════════════════════════════════════════════
// CORE COMPONENTS
// ═══════════════════════════════════════════════════════════════

/**
 * Tag component - every entity has a name/tag
 */
struct TagComponent {
    std::string tag;

    TagComponent() = default;
    TagComponent(const TagComponent&) = default;
    TagComponent(const std::string& tag) : tag(tag) {}
};

/**
 * Transform component - position, rotation, scale in 3D space
 * This is the most fundamental component for spatial entities
 */
struct TransformComponent {
    Vec3 position;
    Vec3 rotation; // Euler angles in radians (X, Y, Z)
    Vec3 scale;

    // Parent-child relationship
    entt::entity parent{ entt::null };
    std::vector<entt::entity> children;

    TransformComponent() 
        : position(0, 0, 0), rotation(0, 0, 0), scale(1, 1, 1) {}

    TransformComponent(const Vec3& pos) 
        : position(pos), rotation(0, 0, 0), scale(1, 1, 1) {}

    // Get local transformation matrix
    Mat4 GetLocalMatrix() const {
        Mat4 t = Mat4::Translation(position);
        Mat4 rx = Mat4::RotationX(rotation.x);
        Mat4 ry = Mat4::RotationY(rotation.y);
        Mat4 rz = Mat4::RotationZ(rotation.z);
        Mat4 s = Mat4::Scale(scale);

        // TRS order: Scale -> Rotate -> Translate
        Mat4 result = Mat4::Multiply(rz, s);
        result = Mat4::Multiply(ry, result);
        result = Mat4::Multiply(rx, result);
        result = Mat4::Multiply(t, result);
        return result;
    }

    // Set transform from matrix (decompose TRS)
    void SetFromMatrix(const Mat4& mat) {
        // Extract translation
        position.x = mat.m[12];
        position.y = mat.m[13];
        position.z = mat.m[14];

        // Extract scale
        float sx = std::sqrt(mat.m[0] * mat.m[0] + mat.m[1] * mat.m[1] + mat.m[2] * mat.m[2]);
        float sy = std::sqrt(mat.m[4] * mat.m[4] + mat.m[5] * mat.m[5] + mat.m[6] * mat.m[6]);
        float sz = std::sqrt(mat.m[8] * mat.m[8] + mat.m[9] * mat.m[9] + mat.m[10] * mat.m[10]);
        scale.x = sx;
        scale.y = sy;
        scale.z = sz;

        // Extract rotation (must match GetLocalMatrix order: R = Rx * Ry * Rz)
        if (sx > 0.0001f && sy > 0.0001f && sz > 0.0001f) {
            // R[0][2] = sin(β) in column-major is m[8]
            float r02 = mat.m[8] / sz;
            r02 = std::max(-1.0f, std::min(1.0f, r02)); // clamp for asin safety

            rotation.y = std::asin(r02);
            if (std::cos(rotation.y) > 0.0001f) {
                // R[1][2] = -sin(α)cos(β) → m[9], R[2][2] = cos(α)cos(β) → m[10]
                rotation.x = std::atan2(-mat.m[9] / sz, mat.m[10] / sz);
                // R[0][1] = -cos(β)sin(γ) → m[4], R[0][0] = cos(β)cos(γ) → m[0]
                rotation.z = std::atan2(-mat.m[4] / sy, mat.m[0] / sx);
            } else {
                // Gimbal lock: β ≈ ±π/2, can only determine α±γ; set γ=0
                rotation.x = std::atan2(mat.m[1] / sx, mat.m[5] / sy);
                rotation.z = 0;
            }
        }
    }

    // Helper: Get rotation as degrees
    Vec3 GetRotationDegrees() const {
        return Vec3(
            rotation.x * 57.2958f,
            rotation.y * 57.2958f,
            rotation.z * 57.2958f
        );
    }

    // Helper: Set rotation from degrees
    void SetRotationDegrees(const Vec3& degrees) {
        rotation.x = degrees.x * 0.0174533f;
        rotation.y = degrees.y * 0.0174533f;
        rotation.z = degrees.z * 0.0174533f;
    }
};

/**
 * Mesh Renderer component - marks entities that should be rendered
 * Contains render-specific data like bounds
 */
struct MeshRendererComponent {
    AABB localBounds; // Bounding box in local space
    bool visible = true;
    bool hideInGame = false;
    // Editor-only: when not visible, still draw a selection-style outline and allow viewport picking.
    bool showHiddenOutline = false;
    bool backfaceCulling = false;
    static constexpr size_t InvalidMesh = std::numeric_limits<size_t>::max();
    size_t meshHandle = InvalidMesh;
    std::string meshAssetPath;
    bool meshImported = false;
    std::vector<int> materialIndices;
    std::vector<std::string> appliedMaterialPaths; // Tracks which material file is applied to each slot
    int submeshCount = 0; // Number of submeshes (material slots) in the imported mesh
    /// Runtime-only GPU handles for extra skinned mesh parts (part 0 is meshHandle).
    /// Never authored or serialized.
    std::vector<size_t> meshPartHandles;
    /// Stamped only by CharacterRuntime bind. Never authored or serialized.
    bool runtimeGenerated = false;

    MeshRendererComponent() 
        : localBounds(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f)) {}

    MeshRendererComponent(const AABB& bounds) 
        : localBounds(bounds) {}

    void SetMesh(size_t handle, const AABB& bounds, std::string assetPath = {}, bool imported = false, int submeshes = 0) {
        meshHandle = handle;
        localBounds = bounds;
        meshAssetPath = std::move(assetPath);
        meshImported = imported;
        submeshCount = submeshes;
    }

    bool HasValidMesh() const {
        return meshHandle != InvalidMesh;
    }

    // Get world-space bounding box
    AABB GetWorldBounds(const TransformComponent& transform) const {
        // Transform the 8 corners of the local bounding box
        Vec3 corners[8] = {
            Vec3(localBounds.min.x, localBounds.min.y, localBounds.min.z),
            Vec3(localBounds.max.x, localBounds.min.y, localBounds.min.z),
            Vec3(localBounds.min.x, localBounds.max.y, localBounds.min.z),
            Vec3(localBounds.max.x, localBounds.max.y, localBounds.min.z),
            Vec3(localBounds.min.x, localBounds.min.y, localBounds.max.z),
            Vec3(localBounds.max.x, localBounds.min.y, localBounds.max.z),
            Vec3(localBounds.min.x, localBounds.max.y, localBounds.max.z),
            Vec3(localBounds.max.x, localBounds.max.y, localBounds.max.z),
        };

        Mat4 worldMatrix = transform.GetLocalMatrix();

        // Transform first corner to initialize bounds
        Vec3 transformed = Mat4::TransformPoint(worldMatrix, corners[0]);
        AABB worldBounds(transformed, transformed);

        // Expand bounds to include all corners
        for (int i = 1; i < 8; ++i) {
            transformed = Mat4::TransformPoint(worldMatrix, corners[i]);

            if (transformed.x < worldBounds.min.x) worldBounds.min.x = transformed.x;
            if (transformed.y < worldBounds.min.y) worldBounds.min.y = transformed.y;
            if (transformed.z < worldBounds.min.z) worldBounds.min.z = transformed.z;

            if (transformed.x > worldBounds.max.x) worldBounds.max.x = transformed.x;
            if (transformed.y > worldBounds.max.y) worldBounds.max.y = transformed.y;
            if (transformed.z > worldBounds.max.z) worldBounds.max.z = transformed.z;
        }

        return worldBounds;
    }
};

inline bool IsRuntimeGeneratedVisual(const MeshRendererComponent& mesh) {
    return mesh.runtimeGenerated;
}

inline bool IsRuntimeGeneratedVisual(const MeshRendererComponent* mesh) {
    return mesh && mesh->runtimeGenerated;
}

inline std::vector<size_t> RuntimeMeshPartHandles(const MeshRendererComponent& mesh) {
    if (!mesh.meshPartHandles.empty()) {
        return mesh.meshPartHandles;
    }
    if (mesh.HasValidMesh()) {
        return {mesh.meshHandle};
    }
    return {};
}

struct MeshColliderContact {
    entt::entity other{ entt::null };
    Vec3 point{ 0.0f, 0.0f, 0.0f };
    Vec3 normal{ 0.0f, 0.0f, 0.0f };
    float penetration = 0.0f;
    bool trigger = false;
};

struct MeshColliderComponent {
    bool enabled = true;
    bool isStatic = false;
    bool isTrigger = false;
    bool debugDraw = false;
    bool useComplexCollision = false;
    std::vector<MeshColliderContact> contacts;

    void ClearContacts() {
        contacts.clear();
    }
};

// ═══════════════════════════════════════════════════════════════
// PHYSICS COMPONENTS (Jolt Physics)
// ═══════════════════════════════════════════════════════════════

/**
 * Physics shape types supported by Jolt
 */
enum class PhysicsShapeType {
    Box,
    Sphere,
    Capsule,
    Cylinder,
    Mesh
};

/**
 * Physics shape component - defines the collision shape for an entity
 */
struct PhysicsShapeComponent {
    PhysicsShapeType type = PhysicsShapeType::Box;
    
    // Box
    Vec3 extents;
    
    // Sphere
    float radius = 0.5f;
    
    // Capsule/Cylinder
    float height = 1.0f;
    
    // Offset from entity center
    Vec3 offset;
    
    PhysicsShapeComponent()
        : extents(0.5f, 0.5f, 0.5f)
        , offset(0.0f, 0.0f, 0.0f)
    {
    }
    
    static PhysicsShapeComponent CreateBox(const Vec3& extents, const Vec3& offset = Vec3(0, 0, 0)) {
        PhysicsShapeComponent shape;
        shape.type = PhysicsShapeType::Box;
        shape.extents = extents;
        shape.offset = offset;
        return shape;
    }
    
    static PhysicsShapeComponent CreateSphere(float radius, const Vec3& offset = Vec3(0, 0, 0)) {
        PhysicsShapeComponent shape;
        shape.type = PhysicsShapeType::Sphere;
        shape.radius = radius;
        shape.offset = offset;
        return shape;
    }
    
    static PhysicsShapeComponent CreateCapsule(float height, float radius, const Vec3& offset = Vec3(0, 0, 0)) {
        PhysicsShapeComponent shape;
        shape.type = PhysicsShapeType::Capsule;
        shape.height = height;
        shape.radius = radius;
        shape.offset = offset;
        return shape;
    }
};

/**
 * Rigid body component - defines physics properties for dynamic/static bodies
 * Must be paired with a TransformComponent for position/rotation
 */
enum class RigidBodyMotion : std::uint8_t {
    Dynamic,
    Static,
    Kinematic
};

struct RigidBodyComponent {
    // Physics properties
    float mass = 1.0f;                    // Body mass (kg), 0 = static
    bool isStatic = false;                // If true, body is immovable (mass is ignored)
    bool isTrigger = false;               // If true, body acts as sensor (no physics)
    bool useGravity = true;               // Apply gravity to this body
    bool freezeRotation = false;          // Lock rotation
    RigidBodyMotion motion = RigidBodyMotion::Dynamic;
    
    // Linear motion
    Vec3 linearVelocity;
    Vec3 linearAcceleration;
    float linearDamping = 0.05f;          // Linear velocity damping
    
    // Angular motion
    Vec3 angularVelocity;
    Vec3 angularAcceleration;
    float angularDamping = 0.05f;         // Angular velocity damping
    
    // Collision layer. 0 is Default. Ids are stable; names are display-only.
    std::uint16_t layer = 0;

    // Material properties
    float friction = 0.3f;                // Friction coefficient
    float restitution = 0.0f;             // Bounciness/elasticity
    
    // Debugging
    bool debugDraw = false;               // Draw collision shape in debug
    
    // Jolt Physics body ID (internal use)
    static constexpr uint32_t InvalidBody = 0xFFFFFFFF;
    uint32_t joltBodyId = InvalidBody;    // Invalid ID by default
    
    RigidBodyComponent()
        : linearVelocity(0, 0, 0)
        , linearAcceleration(0, 0, 0)
        , angularVelocity(0, 0, 0)
        , angularAcceleration(0, 0, 0)
    {
    }
    
    bool IsDynamic() const { return motion != RigidBodyMotion::Kinematic && !isStatic && !isTrigger && mass > 0.0f; }
    bool IsStatic() const { return isStatic || mass <= 0.0f; }
    bool IsKinematic() const { return motion == RigidBodyMotion::Kinematic && !IsStatic(); }
    bool IsSensor() const { return isTrigger; }
    bool UsesGravity() const { return useGravity && !IsStatic() && !IsKinematic(); }

    // isStatic is a compatibility mirror of motion. After this returns,
    // isStatic == (motion == Static).
    void SyncStaticMirror() {
        isStatic = motion == RigidBodyMotion::Static;
    }

    // Inspector, presets, and factories write motion here so the mirror cannot drift.
    void SetMotion(RigidBodyMotion next) {
        motion = next;
        if (motion == RigidBodyMotion::Kinematic) {
            isStatic = false;
            useGravity = false;
            if (mass <= 0.0f) {
                mass = 1.0f;
            }
            return;
        }
        if (motion == RigidBodyMotion::Static) {
            isStatic = true;
            mass = 0.0f;
            useGravity = false;
            return;
        }
        motion = RigidBodyMotion::Dynamic;
        isStatic = false;
        if (mass <= 0.0f && !isTrigger) {
            mass = 1.0f;
        }
    }

    // JSON may still say isStatic. An explicit motion string wins; otherwise
    // isStatic or a non-positive mass selects static. isStatic is then rewritten
    // so older readers stay consistent with the motion enum.
    void ApplyAuthoredMotion(bool motionExplicit) {
        if (!motionExplicit) {
            motion = (isStatic || mass <= 0.0f) ? RigidBodyMotion::Static : RigidBodyMotion::Dynamic;
        }
        if (motion == RigidBodyMotion::Kinematic) {
            isStatic = false;
            useGravity = false;
            if (mass <= 0.0f) {
                mass = 1.0f;
            }
            return;
        }
        if (motion == RigidBodyMotion::Static || mass <= 0.0f) {
            motion = RigidBodyMotion::Static;
            isStatic = true;
            mass = 0.0f;
            useGravity = false;
            return;
        }
        isStatic = false;
        if (mass <= 0.0f && !isTrigger) {
            mass = 1.0f;
        }
    }
    const char* MotionTypeName() const {
        switch (motion) {
        case RigidBodyMotion::Static: return "static";
        case RigidBodyMotion::Kinematic: return "kinematic";
        default: return "dynamic";
        }
    }

    bool HasValidBody() const { return joltBodyId != InvalidBody; }
    void InvalidateBodyHandle() { joltBodyId = InvalidBody; }

    static RigidBodyComponent CreateDynamic(float mass = 1.0f, float friction = 0.3f) {
        RigidBodyComponent body;
        body.mass = mass > 0.0f ? mass : 1.0f;
        body.friction = friction;
        body.SetMotion(RigidBodyMotion::Dynamic);
        return body;
    }
    
    static RigidBodyComponent CreateStatic() {
        RigidBodyComponent body;
        body.SetMotion(RigidBodyMotion::Static);
        return body;
    }
    
    static RigidBodyComponent CreateKinematic(float mass = 1.0f) {
        RigidBodyComponent body;
        body.mass = mass > 0.0f ? mass : 1.0f;
        body.freezeRotation = true;
        body.SetMotion(RigidBodyMotion::Kinematic);
        return body;
    }
};

/**
 * Selection component - marks entities as selected in the editor
 */
struct SelectionComponent {
    bool selected = false;

    SelectionComponent() = default;
    SelectionComponent(bool sel) : selected(sel) {}
};

/**
 * Folder component - marks an entity as a World Outliner folder
 */
struct FolderComponent {
    bool isFolder = true;

    FolderComponent() = default;
    FolderComponent(bool folder) : isFolder(folder) {}
};

/**
 * Projection type for cameras
 */
enum class CameraProjection {
    Perspective,
    Orthographic
};

/**
 * Camera component - defines a viewpoint in the scene
 * When Play Mode starts, the engine selects an active camera based on:
 *   1) First camera with isActive=true (by scene hierarchy order)
 *   2) If none active, first camera found
 *   3) If no cameras exist, fallback to editor camera behavior
 */
struct CameraComponent {
    float fov = 60.0f;           // Field of view in degrees (perspective only)
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;    // Increased default for larger scenes
    CameraProjection projection = CameraProjection::Perspective;
    bool isActive = false;       // If true, this camera is the active runtime camera
    bool isPrimary = false;      // Legacy/compat: maps to isActive
    bool showEditorMesh = true;  // Show the Camera.glb preview mesh in editor

    CameraComponent() = default;
    CameraComponent(float fovVal, float nearVal, float farVal) 
        : fov(fovVal), nearPlane(nearVal), farPlane(farVal) {}

    // Factory method to create an active camera
    static CameraComponent CreateActive(float fovVal = 60.0f, float nearVal = 0.1f, float farVal = 1000.0f) {
        CameraComponent cam(fovVal, nearVal, farVal);
        cam.isActive = true;
        return cam;
    }
};

/**
 * Audio source component - declarative scene audio playback.
 * Runtime fields are not serialized and are managed by the audio system.
 */
struct AudioSourceComponent {
    std::string clipPath;       // Relative/absolute path to .wav/.mp3
    bool enabled = true;
    bool playOnStart = true;
    bool loop = false;
    float volume = 1.0f;        // 0..1

    // Runtime-only state
    uint32_t runtimeHandle = 0;
    bool runtimeStarted = false;
    std::string runtimeLastClip;
};

/**
 * Relationship component - tracks parent-child hierarchy
 * This is automatically managed by the Scene graph
 */
struct RelationshipComponent {
    entt::entity parent{ entt::null };
    entt::entity firstChild{ entt::null };
    entt::entity prevSibling{ entt::null };
    entt::entity nextSibling{ entt::null };

    RelationshipComponent() = default;
};

// ═══════════════════════════════════════════════════════════════
// MATERIAL & LIGHTING COMPONENTS
// ═══════════════════════════════════════════════════════════════

/**
 * Material component - defines surface properties for rendering
 * Uses PBR (Physically Based Rendering) workflow with full texture support
 */
struct MaterialComponent {
    // Base material properties
    Vec3 albedo;        // Base color (diffuse color)
    float metallic;     // 0 = dielectric, 1 = metal
    float roughness;    // 0 = smooth/glossy, 1 = rough/matte
    float ao;           // Ambient occlusion (0-1)
    Vec3 emissive;      // Self-illumination color
    float emissiveStrength; // Multiplier for emissive
    float reflectionStrength; // Per-material environment reflection multiplier
    float specular;     // Dielectric specular reflectance scale
    float opacity;      // Surface opacity (1 = opaque)
    float opacityMask;  // Mask threshold/input for cutout workflows
    float refraction;   // Reserved for future translucent/refraction shaders
    
    // Albedo (diffuse) texture
    std::string albedoTexturePath;
    bool useAlbedoTexture = false;
    
    // Normal map texture
    std::string normalTexturePath;
    bool useNormalTexture = false;
    
    // Roughness texture (can be packed in G channel or standalone)
    std::string roughnessTexturePath;
    bool useRoughnessTexture = false;
    
    // Metallic texture (typically B channel of ORM map or standalone)
    std::string metallicTexturePath;
    bool useMetallicTexture = false;
    
    // Ambient occlusion texture
    std::string aoTexturePath;
    bool useAoTexture = false;

    // World-aligned texture settings
    // When enabled, textures use world position instead of UV coordinates
    bool useWorldAlignedTexture = false;
    float worldTextureScale = 1.0f;  // Scale factor for world-space UV (1.0 = 1 meter per texture repeat)

    MaterialComponent()
        : albedo(1.0f, 1.0f, 1.0f)
        , metallic(0.0f)
        , roughness(0.5f)
        , ao(1.0f)
        , emissive(0.0f, 0.0f, 0.0f)
        , emissiveStrength(0.0f)
        , reflectionStrength(1.0f)
        , specular(0.5f)
        , opacity(1.0f)
        , opacityMask(1.0f)
        , refraction(0.0f)
    {}

    MaterialComponent(const Vec3& color)
        : albedo(color)
        , metallic(0.0f)
        , roughness(0.5f)
        , ao(1.0f)
        , emissive(0.0f, 0.0f, 0.0f)
        , emissiveStrength(0.0f)
        , reflectionStrength(1.0f)
        , specular(0.5f)
        , opacity(1.0f)
        , opacityMask(1.0f)
        , refraction(0.0f)
    {}
};

/**
 * Light types supported by the engine
 */
enum class LightType {
    Directional,  // Sunlight, infinite distance
    Point,        // Omni-directional point source
    Spot          // Cone-shaped light
};

/**
 * Light component - adds illumination to the scene
 */
struct LightComponent {
    LightType type;
    bool enabled;          // Whether the light is active
    Vec3 color;
    float intensity;
    
    // Point & Spot light attenuation
    float range;           // Maximum distance
    float constant;        // Constant attenuation factor
    float linear;          // Linear attenuation factor
    float quadratic;       // Quadratic attenuation factor
    
    // Spot light specific
    float innerConeAngle;  // Inner cone in radians
    float outerConeAngle;  // Outer cone in radians
    
    // Directional light specific
    Vec3 direction;        // For directional lights (world space)
    
    // Shadow settings (primarily for directional lights)
    bool castShadows;      // Whether this light casts shadows
    bool shadowAutoFit;    // Automatically fit shadow frustum to camera view distance
    float shadowBias;      // Bias to prevent shadow acne (0.0001 - 0.01)
    float shadowNormalBias; // Normal bias to reduce peter-panning
    float shadowStrength;  // How dark shadows are (0.0 = no shadow, 1.0 = full black)
    float shadowDistance;  // How far shadows extend from camera (used when shadowAutoFit=false)
    float shadowExtent;    // Size of orthographic shadow frustum (used when shadowAutoFit=false)
    float shadowMaxDistance; // Max shadow range when auto-fit is on (caps camera farPlane)

    LightComponent()
        : type(LightType::Point)
        , enabled(true)
        , color(1.0f, 1.0f, 1.0f)
        , intensity(1.0f)
        , range(10.0f)
        , constant(1.0f)
        , linear(0.09f)
        , quadratic(0.032f)
        , innerConeAngle(0.785f)  // 45 degrees
        , outerConeAngle(0.959f)  // 55 degrees
        , direction(0.0f, -1.0f, 0.0f)
        , castShadows(true)
        , shadowAutoFit(true)
        , shadowBias(0.002f)
        , shadowNormalBias(0.01f)
        , shadowStrength(0.7f)
        , shadowDistance(500.0f)
        , shadowExtent(500.0f)
        , shadowMaxDistance(500.0f)
    {}

    // Factory methods for common light types
    static LightComponent CreateDirectional(const Vec3& dir, const Vec3& color = Vec3(1, 1, 1), float intensity = 1.0f, bool castShadows = true) {
        LightComponent light;
        light.type = LightType::Directional;
        light.direction = vnorm(dir);
        light.color = color;
        light.intensity = intensity;
        light.castShadows = castShadows;
        return light;
    }

    static LightComponent CreatePoint(const Vec3& color = Vec3(1, 1, 1), float intensity = 1.0f, float range = 10.0f) {
        LightComponent light;
        light.type = LightType::Point;
        light.color = color;
        light.intensity = intensity;
        light.range = range;
        return light;
    }

    static LightComponent CreateSpot(const Vec3& color = Vec3(1, 1, 1), float intensity = 1.0f, 
                                     float range = 10.0f, float innerAngle = 0.785f, float outerAngle = 0.959f) {
        LightComponent light;
        light.type = LightType::Spot;
        light.color = color;
        light.intensity = intensity;
        light.range = range;
        light.innerConeAngle = innerAngle;
        light.outerConeAngle = outerAngle;
        return light;
    }
};

/**
 * Environment component - controls scene background and environment
 */
struct EnvironmentComponent {
    enum class BackgroundType {
        SolidColor,
        HDRI
    };

    BackgroundType type;
    Vec3 solidColor;           // Used when type == SolidColor
    std::string hdriPath;      // Path to HDRI/EXR file when type == HDRI
    float hdriIntensity;       // Brightness multiplier for HDRI
    float rotationYaw;         // Rotate sky around Y axis in degrees (0..360)
    bool useAsIBL;             // Use HDRI for image-based lighting / reflections
    float reflectionIntensity; // Global environment reflection intensity
    
    // Render distance settings
    bool enableRenderDistance = false;   // Whether to cull objects beyond render distance
    float renderDistance = 500.0f;       // Distance in world units; objects beyond this are not rendered
    bool infiniteRenderDistance = false; // If true, ignores renderDistance and renders all objects

    EnvironmentComponent()
        : type(BackgroundType::SolidColor)
        , solidColor(0.1f, 0.2f, 0.3f)
        , hdriPath("")
        , hdriIntensity(1.0f)
        , rotationYaw(0.0f)
        , useAsIBL(true)
        , reflectionIntensity(1.0f)
        , enableRenderDistance(false)
        , renderDistance(500.0f)
        , infiniteRenderDistance(false)
    {}
};

// ═══════════════════════════════════════════════════════════════
// PREFAB INSTANCING
// ═══════════════════════════════════════════════════════════════

struct PrefabComponentOverride {
    std::unordered_map<std::string, PropertyValue> properties;
};

struct PrefabNodeOverride {
    bool hasTransform = false;
    TransformComponent transform;
    std::unordered_map<std::string, PrefabComponentOverride> componentOverrides;
    std::unordered_map<std::string, PropertyValue> scriptVars;
};

struct PrefabInstanceComponent {
    std::string prefabPath;
    std::string prefabGuid;
    std::string instanceId;
    std::string rootNodeId;
    entt::entity parent = entt::null;
    TransformComponent rootTransform;
    bool hasRootTransform = false;
    bool missingPrefab = false;
    std::string missingReason;
    std::unordered_map<std::string, PrefabNodeOverride> overrides;
};

struct PrefabNodeComponent {
    std::string instanceId;
    std::string nodeId;
    bool isRoot = false;
};

// Legacy aliases for backward compatibility
using BlueprintComponentOverride = PrefabComponentOverride;
using BlueprintNodeOverride = PrefabNodeOverride;
using BlueprintInstanceComponent = PrefabInstanceComponent;
using BlueprintNodeComponent = PrefabNodeComponent;

// Editor-only marker to hide helper entities from hierarchy/selection.
struct EditorOnlyComponent {
    bool hidden = true;
};

// ═══════════════════════════════════════════════════════════════
// EDITOR ICON (BILLBOARD GIZMO) - extensible for any component
// ═══════════════════════════════════════════════════════════════

/**
 * Specifies which billboard icon category an entity belongs to.
 * The renderer maps each category to a texture at init time.
 * Add new entries here for cameras, audio sources, spawn points, etc.
 */
enum class EditorIconType {
    DirectionalLight,
    PointLight,
    SpotLight,
    // Future:
    // Camera,
    // AudioSource,
    // SpawnPoint,
    Count           // Keep last — used for array sizing
};

/**
 * Editor icon component — attaches a billboard gizmo to an entity.
 * Rendered only in editor mode; ignored in play mode and packaged builds.
 * The billboard always faces the camera and maintains a reasonable screen size.
 */
struct EditorIconComponent {
    EditorIconType iconType = EditorIconType::PointLight;
    float          size     = 0.5f;   // Half-extent in world units (base)
    bool           visible  = true;

    static EditorIconComponent ForLight(LightType lt) {
        EditorIconComponent ic;
        switch (lt) {
            case LightType::Directional: ic.iconType = EditorIconType::DirectionalLight; break;
            case LightType::Point:       ic.iconType = EditorIconType::PointLight;       break;
            case LightType::Spot:        ic.iconType = EditorIconType::SpotLight;        break;
        }
        return ic;
    }
};

