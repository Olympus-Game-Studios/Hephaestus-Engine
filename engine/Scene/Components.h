#pragma once
#include "../Core/Math.h"
#include <string>
#include <vector>

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

    MeshRendererComponent() 
        : localBounds(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f)) {}

    MeshRendererComponent(const AABB& bounds) 
        : localBounds(bounds) {}

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

/**
 * Selection component - marks entities as selected in the editor
 */
struct SelectionComponent {
    bool selected = false;

    SelectionComponent() = default;
    SelectionComponent(bool sel) : selected(sel) {}
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
