#version 460 core

// ==================== 顶点输入 ====================
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

// ==================== 实例矩阵（4 个 vec4 = 1 个 mat4）====================
layout (location = 3) in vec4 aInstanceMatrix0;
layout (location = 4) in vec4 aInstanceMatrix1;
layout (location = 5) in vec4 aInstanceMatrix2;
layout (location = 6) in vec4 aInstanceMatrix3;

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

// ==================== 输出到片段着色器 ====================
out vec3 FragPos;
out vec3 Normal;
out vec2 uv;

void main() {
    // 组装实例矩阵
    mat4 instanceMatrix = mat4(
        aInstanceMatrix0,
        aInstanceMatrix1,
        aInstanceMatrix2,
        aInstanceMatrix3
    );

    // 世界坐标
    vec4 worldPos = instanceMatrix * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;

    // 法线变换（假设实例矩阵无缩放，直接用 mat3）
    // 如果有非均匀缩放，用 mat3(transpose(inverse(instanceMatrix)))
    Normal = mat3(instanceMatrix) * aNormal;

    uv = aUV;

    gl_Position = uProjection * uView * worldPos;
}