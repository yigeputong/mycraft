#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 0) out vec3 vDir;

layout (std140, binding = 0) uniform GlobalData {
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
    vDir = aPos;

    // 使用 global.view 和 global.projection
    mat4 viewNoTrans = mat4(mat3(global.view));
    vec4 clip = global.projection * viewNoTrans * vec4(aPos, 1.0);
    
    // 强制将 Z 推向最远 (xyww)，确保天空盒永远在所有地形之后
    // 在 Vulkan 中，NDC 的 Z 范围是 [0, 1]，所以 z=w 代表深度 1.0（最远）
    gl_Position = vec4(clip.xy, clip.w, clip.w); 
}