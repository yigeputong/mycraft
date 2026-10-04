#version 460 core

in vec3 FragPos;
in vec3 Normal;
in vec2 uv;
in float ao;

out vec4 FragColor;

layout (binding = 0) uniform sampler2D uDiffuseTexture;
layout (binding = 1) uniform sampler2D uSpecularTexture;

layout (std140, binding = 0) uniform GlobalData {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPos;
    float aoStrength;
    vec3 uLightDir;
    float uLightIntensity;
    vec3 uLightColor;
    float timeOfDay;
    vec3 uLightAmbient;
    float _pad2;
};

layout (std140, binding = 1) uniform MaterialData {
    float uShininess;
    float _pad[3];
};

void main() {
    vec3 texColor = texture(uDiffuseTexture, uv).rgb;
    vec3 norm = normalize(Normal);

    // 平行光：uLightDir 是"从表面指向太阳"的方向
    vec3 lightDir = normalize(uLightDir);
    float diff = max(dot(norm, lightDir), 0.0);

    vec3 lit = uLightAmbient * texColor
             + diff * uLightColor * uLightIntensity * texColor;

    // faceShade：模拟 Minecraft 的手绘光照分层
    float faceShade;
    if      (norm.y >  0.5) faceShade = 1.00;   // 顶面
    else if (norm.y < -0.5) faceShade = 0.65;   // 底面
    else if (abs(norm.x) > 0.5) faceShade = 0.85;   // 东西面
    else                        faceShade = 0.92;   // 南北面

    vec3 result = lit * faceShade;

    // 最低亮度保底：任何面至少 35% 纹理色
    result = max(result, texColor * 0.35);

    // AO 混合
    FragColor = vec4(result * mix(1.0, ao, aoStrength), 1.0);
}