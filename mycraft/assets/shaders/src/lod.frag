#version 460

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

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
layout(binding = 0) uniform sampler2D uDiffuse;
#else
layout(set = 1, binding = 0) uniform sampler2D uDiffuse;
#endif

void main() {
    vec3 texColor = textureLod(uDiffuse, vUV, 0.0).rgb;

    // LOD 顶面法线朝上
    vec3 norm = vec3(0.0, 1.0, 0.0);
    vec3 lightDirN = normalize(global.lightDir);
    float diff = max(dot(norm, lightDirN), 0.0);

    vec3 lit = global.lightAmbient * texColor
             + diff * global.lightColor * global.lightIntensity * texColor;

    // 顶面 faceShade = 1.0，照 mesh.frag 的规则
    vec3 result = lit * 1.0;
    result = max(result, texColor * 0.35);

    FragColor = vec4(result, 1.0);
}