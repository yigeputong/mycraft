#version 460
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
// location = 3 是 layer，实体渲染用不到，直接跳过
layout(location = 4) in float aAO;
layout(location = 5) in vec4 aLight;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 uv;
layout(location = 4) out float ao;
layout(location = 5) out vec4 vLight;

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

#ifdef USE_OPENGL
layout(std140, binding = 2) uniform PerObject {
#else
layout(push_constant) uniform PerObject {
#endif
    mat4 model;
    vec4 tintColor;
    vec4 uvOffsetScale;
} pc;

void main() {
    vec4 worldPos = pc.model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal  = normalize(mat3(pc.model) * aNormal);
    uv      = aUV;
    ao      = aAO;
    vLight  = aLight;
    gl_Position = global.projection * global.view * worldPos;
}