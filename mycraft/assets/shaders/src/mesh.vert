#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aAO;
// 实例矩阵
layout(location = 4) in vec4 aI0;
layout(location = 5) in vec4 aI1;
layout(location = 6) in vec4 aI2;
layout(location = 7) in vec4 aI3;
// 光照
layout(location = 8) in vec4 aLight; 

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 uv;
layout(location = 3) out float ao;
layout(location = 4) out vec4 vLight; // ★ 新增输出

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
    mat4 model = mat4(aI0, aI1, aI2, aI3);
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal  = mat3(model) * aNormal;
    uv      = aUV;
    ao      = aAO;
    vLight  = aLight; // ★ 传给片元
    
    gl_Position = global.projection * global.view * worldPos;
}