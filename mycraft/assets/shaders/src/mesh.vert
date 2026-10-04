#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aAO;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 uv;
layout(location = 3) out float ao;

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

#ifdef USE_OPENGL
layout(location = 0) uniform mat4 uModel;
#define MODEL_MATRIX uModel
#else
layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;
#define MODEL_MATRIX pc.model
#endif

void main() {
    vec4 worldPos = MODEL_MATRIX * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal  = mat3(MODEL_MATRIX) * aNormal;
    uv      = aUV;
    ao      = aAO;
    gl_Position = global.projection * global.view * worldPos;
}