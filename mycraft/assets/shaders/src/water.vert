#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aAO;
// 光照
layout(location = 4) in vec4 aLight; 

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
    // 直接使用 aPos，因为顶点已经是世界坐标了
    vec4 worldPos = vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    
    // 法线也不需要用矩阵变换了（除非你有缩放，但方块没有）
    Normal  = aNormal; 
    
    uv      = aUV;
    ao      = aAO;
    vLight  = aLight;
    
    gl_Position = global.projection * global.view * worldPos;
}