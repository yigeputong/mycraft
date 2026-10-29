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
    vec4 texColor4 = textureLod(uDiffuse, vUV, 0.0);
    // ★ 加上透明裁剪，防止远处的树叶变成黑块
    if (texColor4.a < 0.1) discard;
    
    vec3 texColor = texColor4.rgb;
    vec3 norm = vec3(0.0, 1.0, 0.0);
    vec3 lightDirN = normalize(global.lightDir);
    float diff = max(dot(norm, lightDirN), 0.0);

    // ★ 用一个简单的计算来模拟天空光亮度
    // 白天时 sunH > 0，夜晚变暗
    float skyLight = clamp(global.lightDir.y * 2.0 + 0.3, 0.1, 1.0); 

    // 乘上 skyLight
    vec3 lit = (global.lightAmbient + diff * global.lightColor * global.lightIntensity) * skyLight * texColor;
    vec3 result = lit * 1.0;
    
    // 保底亮度
    result = max(result, texColor * 0.05 * skyLight); 

    FragColor = vec4(result, 1.0);
}