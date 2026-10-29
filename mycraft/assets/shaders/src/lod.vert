#version 460
#ifdef USE_OPENGL
#define VK_INDEX gl_VertexID
#else
#define VK_INDEX gl_VertexIndex
#endif

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aAO;
// 光照
layout(location = 8) in vec4 aLight; 

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

layout(location = 0) out vec2 vUV;

void main() {
    vUV = aUV;
    gl_Position = global.projection * global.view * vec4(aPos, 1.0);
}