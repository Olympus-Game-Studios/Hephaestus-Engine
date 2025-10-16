#pragma once
#include "../Core/Math.h"

// Transform component for scene objects
struct Transform {
    Vec3 position;
    Vec3 rotation; // Euler angles in radians
    Vec3 scale;
    
    Transform() : position(0, 0, 0), rotation(0, 0, 0), scale(1, 1, 1) {}
    
    Mat4 GetMatrix() const {
        Mat4 t = Mat4::Translation(position);
        Mat4 rx = Mat4::RotationX(rotation.x);
        Mat4 ry = Mat4::RotationY(rotation.y);
        Mat4 rz = Mat4::RotationZ(rotation.z);
        Mat4 s = Mat4::Scale(scale);
        
        // TRS order: first scale, then rotate, then translate
        Mat4 result = Mat4::Multiply(rz, s);
        result = Mat4::Multiply(ry, result);
        result = Mat4::Multiply(rx, result);
        result = Mat4::Multiply(t, result);
        return result;
    }
    
    void SetMatrix(const Mat4& mat) {
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
        
        // Extract rotation (simplified - doesn't handle all cases)
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
};

// Scene object with a name and transform
struct SceneObject {
    const char* name;
    Transform transform;
    bool selected;
    AABB localBounds; // Bounding box in local space
    
    SceneObject(const char* n = "Object") 
        : name(n), transform(), selected(false), 
          localBounds(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f)) {}
    
    // Get world-space bounding box
    AABB GetWorldBounds() const {
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
        
        Mat4 worldMatrix = transform.GetMatrix();
        
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
