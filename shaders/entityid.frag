#version 450

// Entity ID picking fragment shader - outputs entity ID as color

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint entityId;
    uint padding1;
    uint padding2;
    uint padding3;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // Encode entity ID in color channels
    // For R32_UINT format, we'd just output the uint directly
    // For RGBA8, we encode in RGBA channels (supports up to 2^32 entities)
    uint id = pc.entityId;
    float r = float((id >> 0) & 0xFFu) / 255.0;
    float g = float((id >> 8) & 0xFFu) / 255.0;
    float b = float((id >> 16) & 0xFFu) / 255.0;
    float a = float((id >> 24) & 0xFFu) / 255.0;
    
    outColor = vec4(r, g, b, a);
}
