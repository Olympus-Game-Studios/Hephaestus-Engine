#version 450

// Gizmo fragment shader - flat axis colours with hover, drag and view-angle fades.
// Handle ids match GizmoHandle in engine/Gfx/VulkanGizmo.h.

layout(push_constant) uniform GizmoPushConstants {
    mat4 model;
    vec4 state;      // x = hovered axis, y = active axis, z = gizmo scale, w = rotate arc sweep
} gizmo;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) flat in vec4 gizmoState;
layout(location = 4) in vec3 fragLocalPos;
layout(location = 5) flat in int fragHandle;
layout(location = 6) flat in vec3 toCamera;

layout(location = 0) out vec4 outColor;

const float TWO_PI = 6.28318531;

int interactionId(int handle) {
    if (handle >= 10 && handle <= 12) return handle - 10;
    if (handle == 13) return 3;
    return handle;
}

void main() {
    int handle = fragHandle;
    vec3 N = normalize(fragNormal);
    float diff = max(dot(N, normalize(vec3(0.5, 1.0, 0.5))), 0.0);

    if (handle < 0) {
        outColor = vec4(fragColor * (0.4 + 0.6 * diff), 1.0);
        return;
    }

    if (handle == 20) {
        float a = atan(fragLocalPos.y, fragLocalPos.x);
        float sweep = gizmoState.w;
        bool inside = sweep >= 0.0 ? ((a < 0.0 ? a + TWO_PI : a) <= sweep)
                                   : ((a > 0.0 ? a - TWO_PI : a) >= sweep);
        if (!inside || abs(sweep) < 1e-4) discard;
        outColor = vec4(1.0, 1.0, 1.0, 0.16);
        return;
    }

    vec3 color = fragColor * (0.85 + 0.15 * diff);
    float alpha = 1.0;

    if (handle <= 2) {
        // An axis pointing at the camera cannot be dragged reliably, so it fades out.
        vec3 axis = normalize(mat3(gizmo.model)[handle]);
        alpha *= 1.0 - smoothstep(0.90, 0.97, abs(dot(axis, toCamera)));
    } else if (handle >= 4 && handle <= 6) {
        int normalAxis = handle == 4 ? 2 : (handle == 5 ? 1 : 0);
        vec3 planeNormal = normalize(mat3(gizmo.model)[normalAxis]);
        alpha *= 0.75 * smoothstep(0.10, 0.20, abs(dot(planeNormal, toCamera)));
    } else if (handle >= 10 && handle <= 12) {
        vec3 fromCenter = fragWorldPos - gizmo.model[3].xyz;
        if (dot(fromCenter, toCamera) < -0.02 * length(fromCenter)) {
            alpha *= 0.22;
        }
    }

    int id = interactionId(handle);
    int hoveredId = int(round(gizmoState.x));
    int activeId = int(round(gizmoState.y));

    if (handle >= 30) {
        // Editor icons such as the directional light: active means selected.
        if (id == activeId) color = mix(color, vec3(1.0), 0.3);
        else if (id == hoveredId) color = mix(color, vec3(1.0), 0.15);
        else alpha *= 0.8;
    } else if (activeId >= 0) {
        if (id == activeId) {
            color = mix(color, vec3(1.0), 0.8);
            alpha = max(alpha, 0.9);
        } else if (!(activeId == 3 && id <= 2)) {
            alpha *= 0.25;
        }
    } else if (id == hoveredId) {
        color = mix(color, vec3(1.0), 0.45);
    }

    if (alpha < 0.01) discard;
    outColor = vec4(color, alpha);
}
