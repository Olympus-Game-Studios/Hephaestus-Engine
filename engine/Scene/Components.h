#pragma once
#include "../Core/Math.h"
#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <limits>
#include <utility>

// Forward include for Lua scripting
#include "../Scripting/LuaScriptComponent.h"

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

        // Extract rotation
        if (sx > 0.0001f && sy > 0.0001f && sz > 0.0001f) {
            float m00 = mat.m[0] / sx;
            float m10 = mat.m[1] / sx;
            float m20 = mat.m[2] / sx;
            float m21 = mat.m[6] / sy;
            float m22 = mat.m[10] / sz;

            rotation.y = std::asin(-m20);
            if (std::cos(rotation.y) > 0.0001f) {
                rotation.x = std::atan2(m21, m22);
                rotation.z = std::atan2(m10, m00);
            } else {
                rotation.x = std::atan2(-mat.m[9], mat.m[5]);
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
    bool backfaceCulling = false;
    static constexpr size_t InvalidMesh = std::numeric_limits<size_t>::max();
    size_t meshHandle = InvalidMesh;
    std::string meshAssetPath;
    bool meshImported = false;
    std::vector<int> materialIndices;
    std::vector<std::string> appliedMaterialPaths; // Tracks which material file is applied to each slot

    MeshRendererComponent() 
        : localBounds(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f)) {}

    MeshRendererComponent(const AABB& bounds) 
        : localBounds(bounds) {}

    void SetMesh(size_t handle, const AABB& bounds, std::string assetPath = {}, bool imported = false) {
        meshHandle = handle;
        localBounds = bounds;
        meshAssetPath = std::move(assetPath);
        meshImported = imported;
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
struct RigidBodyComponent {
    // Physics properties
    float mass = 1.0f;                    // Body mass (kg), 0 = static
    bool isStatic = false;                // If true, body is immovable (mass is ignored)
    bool isTrigger = false;               // If true, body acts as sensor (no physics)
    bool useGravity = true;               // Apply gravity to this body
    bool freezeRotation = false;          // Lock rotation
    
    // Linear motion
    Vec3 linearVelocity;
    Vec3 linearAcceleration;
    float linearDamping = 0.05f;          // Linear velocity damping
    
    // Angular motion
    Vec3 angularVelocity;
    Vec3 angularAcceleration;
    float angularDamping = 0.05f;         // Angular velocity damping
    
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
    
    bool IsDynamic() const { return !isStatic && !isTrigger && mass > 0.0f; }
    bool IsStatic() const { return isStatic || mass <= 0.0f; }
    bool IsSensor() const { return isTrigger; }
    bool UsesGravity() const { return useGravity && !IsStatic(); }
    bool HasValidBody() const { return joltBodyId != InvalidBody; }
    void InvalidateBodyHandle() { joltBodyId = InvalidBody; }

    static RigidBodyComponent CreateDynamic(float mass = 1.0f, float friction = 0.3f) {
        RigidBodyComponent body;
        body.mass = mass;
        body.isStatic = false;
        body.friction = friction;
        return body;
    }
    
    static RigidBodyComponent CreateStatic() {
        RigidBodyComponent body;
        body.mass = 0.0f;
        body.isStatic = true;
        body.useGravity = false;
        return body;
    }
    
    static RigidBodyComponent CreateKinematic(float mass = 1.0f) {
        RigidBodyComponent body;
        body.mass = mass;
        body.isStatic = false;
        body.useGravity = false;
        body.freezeRotation = true;
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
 * Camera component - defines a viewpoint in the scene
 */
struct CameraComponent {
    float fov = 60.0f; // Field of view in degrees
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    bool isPrimary = false; // Is this the main camera?

    CameraComponent() = default;
    CameraComponent(float fov, float near, float far) 
        : fov(fov), nearPlane(near), farPlane(far) {}
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
    
    // Albedo (diffuse) texture
    std::string albedoTexturePath;
    bool useAlbedoTexture = false;
    
    // Normal map texture
    std::string normalTexturePath;
    bool useNormalTexture = false;
    
    // Roughness texture (can be packed in G channel or standalone)
    std::string roughnessTexturePath;
    bool useRoughnessTexture = false;
    
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
    {}

    MaterialComponent(const Vec3& color)
        : albedo(color)
        , metallic(0.0f)
        , roughness(0.5f)
        , ao(1.0f)
        , emissive(0.0f, 0.0f, 0.0f)
        , emissiveStrength(0.0f)
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
    float shadowBias;      // Bias to prevent shadow acne (0.0001 - 0.01)
    float shadowStrength;  // How dark shadows are (0.0 = no shadow, 1.0 = full black)
    float shadowDistance;  // How far shadows extend from camera
    float shadowExtent;    // Size of orthographic shadow frustum

    LightComponent()
        : type(LightType::Point)
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
        , shadowBias(0.002f)
        , shadowStrength(0.7f)
        , shadowDistance(40.0f)
        , shadowExtent(25.0f)
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
    
    // Render distance settings
    bool enableRenderDistance = false;   // Whether to cull objects beyond render distance
    float renderDistance = 500.0f;       // Distance in world units; objects beyond this are not rendered
    bool infiniteRenderDistance = false; // If true, ignores renderDistance and renders all objects

    EnvironmentComponent()
        : type(BackgroundType::SolidColor)
        , solidColor(0.1f, 0.2f, 0.3f)
        , hdriPath("")
        , hdriIntensity(1.0f)
        , enableRenderDistance(false)
        , renderDistance(500.0f)
        , infiniteRenderDistance(false)
    {}
};

