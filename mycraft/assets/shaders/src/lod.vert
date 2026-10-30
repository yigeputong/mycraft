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
layout(location = 8) in vec4 aLight; // 我们借用了这个属性来传 uvInfo

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
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vLightInfo; // 输出 uvInfo 给片元

void main() {
    vUV = aUV;
    vNormal = aNormal;
    vLightInfo = aLight; 
    gl_Position = global.projection * global.view * vec4(aPos, 1.0);
}