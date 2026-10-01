#version 460 core

// ==================== 输入 ====================
in vec3 FragPos;
in vec3 Normal;
in vec2 uv;
in float ao;

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
    float _pad1;
    vec3 uLightAmbient;
    float _pad2;
};

// ==================== Material UBO (binding = 1) ====================
layout (std140, binding = 1) uniform MaterialData {
    float uShininess;
    float _pad[3];
};

uniform float uAOStrength = 1.0;

void main() {
    vec3 texColor = texture(uDiffuseTexture, uv).rgb;
    vec3 norm = normalize(Normal);

    // 平行光
    vec3 lightDir = normalize(-uLightDir);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 lit = uLightAmbient * texColor
             + diff * uLightColor * uLightIntensity * texColor;

    // 面朝向固定亮度
    float faceShade;
    if      (norm.y >  0.5) faceShade = 1.00;
    else if (norm.y < -0.5) faceShade = 0.55;
    else if (abs(norm.x) > 0.5) faceShade = 0.75;
    else                        faceShade = 0.85;

    vec3 result = lit * faceShade;

    FragColor = vec4(result * mix(1.0, ao, uAOStrength), 1.0);
}