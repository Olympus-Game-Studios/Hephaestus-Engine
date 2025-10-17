#version 450

// Input from vertex shader
layout(location = 0) in vec2 inTexCoord;

// Output
layout(location = 0) out vec4 outColor;

// Texture sampler
layout(binding = 1) uniform sampler2D texSampler;

void main() {
    // Sample the texture
    vec4 texColor = texture(texSampler, inTexCoord);
    
    // Discard fully transparent pixels (optional, for performance)
    if (texColor.a < 0.1) {
        discard;
    }
    
    outColor = texColor;
}
