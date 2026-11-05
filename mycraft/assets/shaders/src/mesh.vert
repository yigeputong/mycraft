#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aLayer; // ★ 新增
layout(location = 4) in float aAO;
layout(location = 5) in vec4 aLight;  // ★ 索引顺延

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 uv;
layout(location = 3) out float vLayer; // ★ 新增
layout(location = 4) out float ao;
layout(location = 5) out vec4 vLight;  // ★ 索引顺延

layout(std140, binding = 0) uniform GlobalData {
    mat4 view;
    mat4 projection;
    vec3 viewPos;
    float aoStrength;
    vec3 lightDir;
    float lightIntensity;
    vec3 lightColor;
    float timeOfDay;
    vec3 lightAmbient;
    float _pad2;
} global;

void main() {
    vec4 worldPos = vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal  = aNormal; 
    uv      = aUV;
    vLayer  = aLayer;
    ao      = aAO;
    vLight  = aLight;
    
    gl_Position = global.projection * global.view * worldPos;
}