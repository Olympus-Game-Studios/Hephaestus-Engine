#pragma once
#include <cmath>
#include <cstring>

// Simple 3D vector
struct Vec3 { 
    float x, y, z; 
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
};

// Simple 4D vector
struct Vec4 {
    float x, y, z, w;
    Vec4() : x(0), y(0), z(0), w(0) {}
    Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
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

    static Mat4 RotationAxis(const Vec3& axis, float angleRadians) {
        Vec3 n = vnorm(axis);
        float c = std::cos(angleRadians);
        float s = std::sin(angleRadians);
        float t = 1.0f - c;

        Mat4 r = Identity();
        r.m[0] = t * n.x * n.x + c;
        r.m[1] = t * n.x * n.y + s * n.z;
        r.m[2] = t * n.x * n.z - s * n.y;

        r.m[4] = t * n.x * n.y - s * n.z;
        r.m[5] = t * n.y * n.y + c;
        r.m[6] = t * n.y * n.z + s * n.x;

        r.m[8] = t * n.x * n.z + s * n.y;
        r.m[9] = t * n.y * n.z - s * n.x;
        r.m[10] = t * n.z * n.z + c;

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
    
    // Invert a matrix (simplified for view/projection matrices)
    static Mat4 Invert(const Mat4& mat) {
        Mat4 inv;
        float* m = (float*)mat.m;
        float* invOut = inv.m;
        
        float inv0 = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15]
                   + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        float inv4 = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15]
                   - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        float inv8 = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15]
                   + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        float inv12 = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14]
                    - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        
        float det = m[0] * inv0 + m[1] * inv4 + m[2] * inv8 + m[3] * inv12;
        
        if (det == 0) return Identity();
        
        det = 1.0f / det;
        
        invOut[0] = inv0 * det;
        invOut[4] = inv4 * det;
        invOut[8] = inv8 * det;
        invOut[12] = inv12 * det;
        
        invOut[1] = (-m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15]
                   - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10]) * det;
        invOut[5] = (m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15]
                   + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10]) * det;
        invOut[9] = (-m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15]
                   - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9]) * det;
        invOut[13] = (m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14]
                    + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9]) * det;
        
        invOut[2] = (m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15]
                   + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6]) * det;
        invOut[6] = (-m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15]
                   - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6]) * det;
        invOut[10] = (m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15]
                    + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5]) * det;
        invOut[14] = (-m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14]
                    - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5]) * det;
        
        invOut[3] = (-m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11]
                   - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6]) * det;
        invOut[7] = (m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11]
                   + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6]) * det;
        invOut[11] = (-m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11]
                    - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5]) * det;
        invOut[15] = (m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10]
                    + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5]) * det;
        
        return inv;
    }
    
    // Transform a point by a matrix (4th component = 1)
    static Vec3 TransformPoint(const Mat4& mat, const Vec3& v) {
        float x = mat.m[0] * v.x + mat.m[4] * v.y + mat.m[8] * v.z + mat.m[12];
        float y = mat.m[1] * v.x + mat.m[5] * v.y + mat.m[9] * v.z + mat.m[13];
        float z = mat.m[2] * v.x + mat.m[6] * v.y + mat.m[10] * v.z + mat.m[14];
        float w = mat.m[3] * v.x + mat.m[7] * v.y + mat.m[11] * v.z + mat.m[15];
        if (w != 0.0f && w != 1.0f) {
            return Vec3(x / w, y / w, z / w);
        }
        return Vec3(x, y, z);
    }
    
    // Transform a direction by a matrix (4th component = 0)
    static Vec3 TransformDirection(const Mat4& mat, const Vec3& v) {
        float x = mat.m[0] * v.x + mat.m[4] * v.y + mat.m[8] * v.z;
        float y = mat.m[1] * v.x + mat.m[5] * v.y + mat.m[9] * v.z;
        float z = mat.m[2] * v.x + mat.m[6] * v.y + mat.m[10] * v.z;
        return Vec3(x, y, z);
    }
};

// Ray structure for ray casting
struct Ray {
    Vec3 origin;
    Vec3 direction; // normalized
    
    Ray() : origin(), direction(0, 0, -1) {}
    Ray(Vec3 o, Vec3 d) : origin(o), direction(vnorm(d)) {}
    
    Vec3 at(float t) const {
        return vadd(origin, vscale(direction, t));
    }
};

// Axis-Aligned Bounding Box
struct AABB {
    Vec3 min;
    Vec3 max;
    
    AABB() : min(0, 0, 0), max(0, 0, 0) {}
    AABB(Vec3 min, Vec3 max) : min(min), max(max) {}
    
    // Ray-AABB intersection test
    // Returns true if ray intersects the box, and sets tMin/tMax to the entry/exit distances
    bool IntersectRay(const Ray& ray, float& tMin, float& tMax) const {
        tMin = 0.0f;
        tMax = 1e30f;
        
        for (int i = 0; i < 3; ++i) {
            float origin_i = (&ray.origin.x)[i];
            float dir_i = (&ray.direction.x)[i];
            float min_i = (&min.x)[i];
            float max_i = (&max.x)[i];
            
            if (std::abs(dir_i) < 1e-8f) {
                // Ray is parallel to slab
                if (origin_i < min_i || origin_i > max_i) {
                    return false;
                }
            } else {
                float t1 = (min_i - origin_i) / dir_i;
                float t2 = (max_i - origin_i) / dir_i;
                
                if (t1 > t2) {
                    float temp = t1;
                    t1 = t2;
                    t2 = temp;
                }
                
                if (t1 > tMin) tMin = t1;
                if (t2 < tMax) tMax = t2;
                
                if (tMin > tMax) return false;
            }
        }
        
        return tMax >= 0.0f;
    }

    Vec3 Center() const {
        return vscale(vadd(min, max), 0.5f);
    }

    Vec3 HalfSize() const {
        return vscale(vsub(max, min), 0.5f);
    }

    void Translate(const Vec3& delta) {
        min = vadd(min, delta);
        max = vadd(max, delta);
    }
};
