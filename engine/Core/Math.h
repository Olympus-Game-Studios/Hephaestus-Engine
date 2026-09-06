#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>

// Simple 2D vector
struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}
};

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
    
    // Transform a Vec4 by this matrix
    static Vec4 TransformVec4(const Mat4& mat, const Vec4& v) {
        Vec4 result;
        result.x = mat.m[0] * v.x + mat.m[4] * v.y + mat.m[8]  * v.z + mat.m[12] * v.w;
        result.y = mat.m[1] * v.x + mat.m[5] * v.y + mat.m[9]  * v.z + mat.m[13] * v.w;
        result.z = mat.m[2] * v.x + mat.m[6] * v.y + mat.m[10] * v.z + mat.m[14] * v.w;
        result.w = mat.m[3] * v.x + mat.m[7] * v.y + mat.m[11] * v.z + mat.m[15] * v.w;
        return result;
    }
    
    static Mat4 LookAt(Vec3 eye, Vec3 center, Vec3 up)
    {
        Vec3 f = vnorm(vsub(center, eye));
        Vec3 s = vnorm(vcross(f, up));
        Vec3 u = vcross(s, f);

        Mat4 r;
        // First column
        r.m[0] = s.x; r.m[1] = u.x; r.m[2] = f.x; r.m[3] = 0.0f;
        // Second column
        r.m[4] = s.y; r.m[5] = u.y; r.m[6] = f.y; r.m[7] = 0.0f;
        // Third column
        r.m[8] = s.z; r.m[9] = u.z; r.m[10] = f.z; r.m[11] = 0.0f;
        // Fourth column (Vulkan: +f means objects in front have positive viewZ)
        r.m[12] = -vdot(s, eye);
        r.m[13] = -vdot(u, eye);
        r.m[14] = -vdot(f, eye);
        r.m[15] = 1.0f;
        return r;
    }
    
    // Create a view matrix for shadow mapping given the light direction
    // lightDir: normalized direction the light is SHINING (from light toward scene)
    // lightPos: position of the "light" for shadow frustum
    static Mat4 ShadowViewMatrix(Vec3 lightDir, Vec3 lightPos)
    {
        // Forward is the light direction (where light shines)
        Vec3 f = vnorm(lightDir);
        
        // Choose an up vector that's not parallel to forward
        Vec3 up = v3(0.0f, 1.0f, 0.0f);
        if (std::abs(vdot(f, up)) > 0.99f) {
            up = v3(1.0f, 0.0f, 0.0f);
        }
        
        // Right vector
        Vec3 s = vnorm(vcross(up, f));
        // Recalculate up to be orthogonal
        Vec3 u = vcross(f, s);

        Mat4 r;
        // Build rotation part (transposed because view matrix)
        r.m[0] = s.x; r.m[1] = u.x; r.m[2] = f.x; r.m[3] = 0.0f;
        r.m[4] = s.y; r.m[5] = u.y; r.m[6] = f.y; r.m[7] = 0.0f;
        r.m[8] = s.z; r.m[9] = u.z; r.m[10] = f.z; r.m[11] = 0.0f;
        // Translation
        r.m[12] = -vdot(s, lightPos);
        r.m[13] = -vdot(u, lightPos);
        r.m[14] = -vdot(f, lightPos);
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

    static Mat4 Orthographic(float left, float right, float bottom, float top,
                             float znear, float zfar)
    {
        Mat4 r = Identity();
        float width = right - left;
        float height = top - bottom;
        float depth = zfar - znear;
        if (width == 0.0f || height == 0.0f || depth == 0.0f) {
            return r;
        }
        r.m[0] = 2.0f / width;
        r.m[5] = -2.0f / height; // Flip Y for Vulkan NDC
        r.m[10] = 1.0f / depth;
        r.m[12] = -(right + left) / width;
        r.m[13] = -(top + bottom) / height;
        r.m[14] = -znear / depth;
        return r;
    }

    // Orthographic projection for shadow mapping (Vulkan 0-1 depth)
    // Matches Vulkan's coordinate system with +z forward (same as LookAt)
    static Mat4 OrthographicShadow(float left, float right, float bottom, float top,
                                   float znear, float zfar)
    {
        Mat4 r = Identity();
        float width = right - left;
        float height = top - bottom;
        float depth = zfar - znear;
        if (width == 0.0f || height == 0.0f || depth == 0.0f) {
            return r;
        }
        r.m[0] = 2.0f / width;
        r.m[5] = -2.0f / height;  // Negative Y for Vulkan (same as regular ortho)
        r.m[10] = 1.0f / depth;   // Positive: +z forward convention (matches LookAt)
        r.m[12] = -(right + left) / width;
        r.m[13] = -(top + bottom) / height;
        r.m[14] = -znear / depth;
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

// Number of shadow cascades for CSM
static constexpr int CSM_CASCADE_COUNT = 4;

// Compute a shadow view-projection matrix for a single cascade slice of the camera frustum.
// sliceNear/sliceFar define the depth range of this cascade in view space.
inline Mat4 ComputeCascadeShadowVP(
    float camPosX, float camPosY, float camPosZ,
    float camYawDeg, float camPitchDeg,
    float sliceNear, float sliceFar,
    float fovRadians, float aspect,
    Vec3 lightDir)
{
    constexpr float D2R = 3.1415926535f / 180.0f;

    float tanHF = std::tan(fovRadians * 0.5f);
    float nH = tanHF * sliceNear;
    float nW = nH * aspect;
    float fH = tanHF * sliceFar;
    float fW = fH * aspect;

    // Camera basis vectors from yaw/pitch
    float yR = camYawDeg * D2R;
    float pR = camPitchDeg * D2R;
    Vec3 fwd = vnorm(Vec3(
        std::cos(yR) * std::cos(pR),
        std::sin(pR),
        std::sin(yR) * std::cos(pR)));
    Vec3 wUp(0.0f, 1.0f, 0.0f);
    if (std::abs(vdot(fwd, wUp)) > 0.99f) wUp = Vec3(0.0f, 0.0f, 1.0f);
    Vec3 right = vnorm(vcross(fwd, wUp));
    Vec3 up = vcross(right, fwd);

    Vec3 camPos(camPosX, camPosY, camPosZ);
    Vec3 nc = vadd(camPos, vscale(fwd, sliceNear));
    Vec3 fc = vadd(camPos, vscale(fwd, sliceFar));

    // 8 frustum corners in world space for this slice
    Vec3 c[8] = {
        vadd(vadd(nc, vscale(up,  nH)), vscale(right, -nW)),
        vadd(vadd(nc, vscale(up,  nH)), vscale(right,  nW)),
        vadd(vadd(nc, vscale(up, -nH)), vscale(right, -nW)),
        vadd(vadd(nc, vscale(up, -nH)), vscale(right,  nW)),
        vadd(vadd(fc, vscale(up,  fH)), vscale(right, -fW)),
        vadd(vadd(fc, vscale(up,  fH)), vscale(right,  fW)),
        vadd(vadd(fc, vscale(up, -fH)), vscale(right, -fW)),
        vadd(vadd(fc, vscale(up, -fH)), vscale(right,  fW)),
    };

    // Frustum center
    Vec3 center(0, 0, 0);
    for (int i = 0; i < 8; i++) center = vadd(center, c[i]);
    center = vscale(center, 1.0f / 8.0f);

    // Use a bounding sphere to keep the shadow map stable during camera rotation
    float radius = 0.0f;
    for (int i = 0; i < 8; i++) {
        float d = vlen(vsub(c[i], center));
        if (d > radius) radius = d;
    }
    // Keep a usable minimum so texel snapping and the ortho depth range stay finite.
    if (radius < 0.5f) radius = 0.5f;

    // Round up to texel size for stability (assuming 2048 per cascade tile)
    float texelsPerUnit = 2048.0f / (radius * 2.0f);
    radius = std::ceil(radius * texelsPerUnit) / texelsPerUnit;

    // Light view: look from behind the frustum toward its center.
    // Depth range must stay proportional to the cascade radius. A huge fixed
    // padding (e.g. +500/+1000) compresses contact shadows into a tiny NDC
    // slice so the default 0.002 bias hides them entirely.
    Vec3 toLight = vscale(lightDir, -1.0f);
    Vec3 lUp(0.0f, 1.0f, 0.0f);
    if (std::abs(vdot(lightDir, lUp)) > 0.99f) lUp = Vec3(1.0f, 0.0f, 0.0f);

    const float extraTowardLight = std::max(radius, 10.0f);
    Vec3 lPos = vadd(center, vscale(toLight, radius + extraTowardLight));
    Mat4 lv = Mat4::LookAt(lPos, center, lUp);

    float nearZ = 0.1f;
    float farZ = extraTowardLight + radius * 2.0f + 1.0f;
    Mat4 lp = Mat4::OrthographicShadow(-radius, radius, -radius, radius, nearZ, farZ);

    return Mat4::Multiply(lp, lv);
}

// Compute cascade split distances using practical split scheme (log/linear blend)
inline void ComputeCascadeSplits(float nearPlane, float farPlane, float splits[CSM_CASCADE_COUNT + 1], float lambda = 0.75f)
{
    splits[0] = nearPlane;
    for (int i = 1; i <= CSM_CASCADE_COUNT; i++) {
        float p = static_cast<float>(i) / static_cast<float>(CSM_CASCADE_COUNT);
        float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
        float linSplit = nearPlane + (farPlane - nearPlane) * p;
        splits[i] = lambda * logSplit + (1.0f - lambda) * linSplit;
    }
}

