#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

uniform mat4 uModel;

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

out vec3 FragPos;
out vec3 Normal;
out vec2 uv;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(uModel) * aNormal;
    uv = aUV;
    gl_Position = uProjection * uView * worldPos;
}