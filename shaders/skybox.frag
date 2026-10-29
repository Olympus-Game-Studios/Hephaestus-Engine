#version 450

layout(set = 0, binding = 1) uniform sampler2D environmentMap;

layout(push_constant) uniform SkyboxPushConstants {
    float intensity;
    float solidColorR;
    float solidColorG;
    float solidColorB;
    int   useSolidColor; // 1 = solid color, 0 = HDRI
} pc;

layout(location = 0) in vec3 fragTexCoord;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// Sample equirectangular environment map
vec3 sampleEquirectangular(vec3 direction) {
    // Convert direction to spherical coordinates
    vec2 uv;
    uv.x = atan(direction.z, direction.x) / (2.0 * PI) + 0.5;
    uv.y = asin(direction.y) / PI + 0.5;
    
    return texture(environmentMap, uv).rgb;
}

void main() {
    vec3 color;
    
    if (pc.useSolidColor != 0) {
        color = vec3(pc.solidColorR, pc.solidColorG, pc.solidColorB);
    } else {
        color = sampleEquirectangular(normalize(fragTexCoord)) * pc.intensity;
    }

    outColor = vec4(color, 1.0);
}
