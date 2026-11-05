#version 460 core

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in float vLayer;
layout(location = 4) in float ao;
layout(location = 5) in vec4 vLight;

layout(location = 0) out vec4 FragColor;

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
layout(binding = 0) uniform sampler2DArray uDiffuse;
#else
layout(set = 1, binding = 0) uniform sampler2DArray uDiffuse;
#endif

void main() {
    float time = global.timeOfDay;
    float waveX = sin(time * 1.5 + FragPos.z * 0.5) * 0.002;
    float waveY = cos(time * 1.2 + FragPos.x * 0.5) * 0.002;
    
    // 限制扰动范围
    vec2 finalUV = clamp(uv + vec2(waveX, waveY), vec2(0.0), vec2(1.0)); 
    
    // 使用 sampler2DArray 采样
    vec4 texColor4 = texture(uDiffuse, vec3(finalUV, vLayer));
    
    vec3 texColor = texColor4.rgb;
    if (length(texColor) < 0.1) {
        texColor = vec3(0.1, 0.4, 0.8);
    }

    vec3 norm = normalize(Normal);
    float skyLight = vLight.r;
    vec3 blockLight = vLight.gba;

    vec3 lightDirN = normalize(global.lightDir);
    float diff = max(dot(norm, lightDirN), 0.0);
    vec3 lit = (global.lightAmbient + diff * global.lightColor * global.lightIntensity) * skyLight * texColor;
    
    vec3 waterAmbient = texColor * 0.6; 
    vec3 finalLight = max(lit, waterAmbient);

    float faceShade = 1.0;
    if (norm.y < -0.5) faceShade = 0.7; 
    
    // 输出半透明颜色
    FragColor = vec4(finalLight * faceShade, 0.7);
}