#version 460 core

layout(location = 0) in vec3 aPos;

layout(location = 0) out vec3 vDir;

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

void main() {
    vDir = aPos;

    mat4 viewNoTrans = mat4(mat3(uView));
    vec4 clip = uProjection * viewNoTrans * vec4(aPos, 1.0);
    gl_Position = clip.xyww;
}