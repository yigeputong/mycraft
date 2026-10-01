#version 460 core

layout(location = 0) in vec3 aPos;

out vec3 vDir;

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

void main() {
    vDir = aPos;

    // 天空盒：去掉平移，强制深度 1.0
    mat4 viewNoTrans = mat4(mat3(uView));
    vec4 clip = uProjection * viewNoTrans * vec4(aPos, 1.0);
    gl_Position = clip.xyww;
}