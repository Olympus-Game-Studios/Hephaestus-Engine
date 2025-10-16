#pragma once
#include <cmath>
#include <cstring>

// Simple 3D vector
struct Vec3 { 
    float x, y, z; 
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
};

inline Vec3 v3(float x, float y, float z) { return Vec3{x,y,z}; }
inline Vec3 vadd(Vec3 a, Vec3 b){ return v3(a.x+b.x,a.y+b.y,a.z+b.z);} 
inline Vec3 vsub(Vec3 a, Vec3 b){ return v3(a.x-b.x,a.y-b.y,a.z-b.z);} 
inline Vec3 vscale(Vec3 a, float s){ return v3(a.x*s,a.y*s,a.z*s);} 
inline float vdot(Vec3 a, Vec3 b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 vcross(Vec3 a, Vec3 b){ return v3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x); }
inline float vlen(Vec3 a){ return std::sqrt(vdot(a,a)); }
inline Vec3 vnorm(Vec3 a){ float l=vlen(a); return l>0? vscale(a, 1.0f/l) : v3(0,0,0); }

// Column-major 4x4 matrix (compatible with GLSL and ImGuizmo)
struct Mat4 { 
    float m[16]; 
    
    Mat4() { std::memset(m, 0, sizeof(m)); }
    
    float* data() { return m; }
    const float* data() const { return m; }
    
    static Mat4 Identity() {
        Mat4 r;
        r.m[0] = 1.0f; r.m[5] = 1.0f; r.m[10] = 1.0f; r.m[15] = 1.0f;
        return r;
    }
    
    static Mat4 Translation(const Vec3& v) {
        Mat4 r = Identity();
        r.m[12] = v.x;
        r.m[13] = v.y;
        r.m[14] = v.z;
        return r;
    }
    
    static Mat4 Scale(const Vec3& v) {
        Mat4 r = Identity();
        r.m[0] = v.x;
        r.m[5] = v.y;
        r.m[10] = v.z;
        return r;
    }
    
    static Mat4 RotationX(float angleRadians) {
        Mat4 r = Identity();
        float c = std::cos(angleRadians);
        float s = std::sin(angleRadians);
        r.m[5] = c;
        r.m[6] = s;
        r.m[9] = -s;
        r.m[10] = c;
        return r;
    }
    
    static Mat4 RotationY(float angleRadians) {
        Mat4 r = Identity();
        float c = std::cos(angleRadians);
        float s = std::sin(angleRadians);
        r.m[0] = c;
        r.m[2] = -s;
        r.m[8] = s;
        r.m[10] = c;
        return r;
    }
    
    static Mat4 RotationZ(float angleRadians) {
        Mat4 r = Identity();
        float c = std::cos(angleRadians);
        float s = std::sin(angleRadians);
        r.m[0] = c;
        r.m[1] = s;
        r.m[4] = -s;
        r.m[5] = c;
        return r;
    }
    
    static Mat4 Multiply(const Mat4& a, const Mat4& b) {
        Mat4 result;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                result.m[col * 4 + row] = 0;
                for (int k = 0; k < 4; ++k) {
                    result.m[col * 4 + row] += a.m[k * 4 + row] * b.m[col * 4 + k];
                }
            }
        }
        return result;
    }
    
    static Mat4 LookAt(Vec3 eye, Vec3 center, Vec3 up)
    {
        Vec3 f = vnorm(vsub(center, eye));
        Vec3 s = vnorm(vcross(f, up));
        Vec3 u = vcross(s, f);

        Mat4 r;
        // First column
        r.m[0] = s.x; r.m[1] = u.x; r.m[2] = -f.x; r.m[3] = 0.0f;
        // Second column
        r.m[4] = s.y; r.m[5] = u.y; r.m[6] = -f.y; r.m[7] = 0.0f;
        // Third column
        r.m[8] = s.z; r.m[9] = u.z; r.m[10] = -f.z; r.m[11] = 0.0f;
        // Fourth column
        r.m[12] = -vdot(s, eye);
        r.m[13] = -vdot(u, eye);
        r.m[14] = vdot(f, eye);
        r.m[15] = 1.0f;
        return r;
    }
    
    // Vulkan-style perspective (depth 0..1)
    static Mat4 Perspective(float fovyRadians, float aspect, float znear, float zfar)
    {
        float f = 1.0f / std::tan(fovyRadians * 0.5f);
        Mat4 r;
        r.m[0] = f / aspect;
        // Flip Y for Vulkan NDC to keep winding consistent with CLOCKWISE front-face
        r.m[5] = -f;
        r.m[10] = zfar / (zfar - znear);
        r.m[11] = 1.0f;
        r.m[14] = -(znear * zfar) / (zfar - znear);
        // r.m[15] stays 0 by default for perspective
        return r;
    }

    // OpenGL-style perspective (depth -1..1, no Y-flip) for tools like ImGuizmo
    static Mat4 PerspectiveGL(float fovyRadians, float aspect, float znear, float zfar)
    {
        float f = 1.0f / std::tan(fovyRadians * 0.5f);
        Mat4 r;
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zfar + znear) / (znear - zfar);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
        r.m[15] = 0.0f;
        return r;
    }
};
