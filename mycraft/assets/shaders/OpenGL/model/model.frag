#version 460 core

// ==================== 输入 ====================
in vec3 FragPos;
in vec3 Normal;
in vec2 uv;

out vec4 FragColor;

// ==================== 纹理 ====================
layout (binding = 0) uniform sampler2D uDiffuseTexture;
layout (binding = 1) uniform sampler2D uSpecularTexture;

// ==================== Global UBO (binding = 0) ====================
layout (std140, binding = 0) uniform GlobalData {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPos;
    float _pad0;
    vec3 uLightDir;
    float uLightIntensity;
    vec3 uLightColor;
    float uLightAmbient;
};

// ==================== Material UBO (binding = 1) ====================
layout (std140, binding = 1) uniform MaterialData {
    float uShininess;
    float _pad[3];
};

void main() {
    vec3 texColor = texture(uDiffuseTexture, uv).rgb;

    // 环境光 + 漫反射
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-uLightDir);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 result = uLightAmbient * texColor
                + diff * uLightColor * uLightIntensity * texColor;

    FragColor = vec4(result, 1.0);
}