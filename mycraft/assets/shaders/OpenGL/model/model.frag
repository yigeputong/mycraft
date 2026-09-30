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
    // 采样纹理
    vec3 texColor = texture(uDiffuseTexture, uv).rgb;

    // ---- 环境光 ----
    vec3 ambient = uLightAmbient * texColor;

    // ---- 漫反射 ----
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-uLightDir);          // 指向光源的方向
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor * uLightIntensity * texColor;

    // ---- 镜面高光 ----
    vec3 viewDir = normalize(uViewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), uShininess);
    vec3 specular = spec * uLightColor * 0.3;

    // ---- 合成 ----
    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 1.0);
}