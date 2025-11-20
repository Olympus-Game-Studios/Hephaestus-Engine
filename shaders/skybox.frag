#version 450

layout(set = 0, binding = 1) uniform sampler2D environmentMap;

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
    vec3 envColor = sampleEquirectangular(normalize(fragTexCoord));
    
    // Simple tone mapping for HDR
    envColor = envColor / (envColor + vec3(1.0));
    
    // Gamma correction
    envColor = pow(envColor, vec3(1.0/2.2));
    
    outColor = vec4(envColor, 1.0);
}
