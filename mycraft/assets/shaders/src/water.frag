#version 460 core

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in float ao;
layout(location = 4) in vec4 vLight;

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
layout(binding = 0) uniform sampler2D uDiffuse;
#else
layout(set = 1, binding = 0) uniform sampler2D uDiffuse;
#endif

void main() {
    // ★ 1. 停止使用全局 UV 偏移（它在远处会严重越界导致采到别的方块颜色）
    // 如果你想要水流动的效果，用微小的正弦扰动，并且限制范围
    float time = global.timeOfDay;
    float waveX = sin(time * 1.5 + FragPos.z * 0.5) * 0.002;
    float waveY = cos(time * 1.2 + FragPos.x * 0.5) * 0.002;
    vec2 finalUV = clamp(uv + vec2(waveX, waveY), uv - 0.005, uv + 0.005); // 限制扰动范围
    
    vec4 texColor4 = texture(uDiffuse, finalUV);
    
    // ★ 2. 远处的图集 Mipmap 可能导致越界变黑，这里给一个蓝色的保底
    // 如果采样到的颜色太黑（RGB都很小），强制用海洋蓝
    vec3 texColor = texColor4.rgb;
    if (length(texColor) < 0.1) {
        texColor = vec3(0.1, 0.4, 0.8);
    }

    vec3 norm = normalize(Normal);
    float skyLight = vLight.r;
    vec3 blockLight = vLight.gba;

    // 光照计算
    vec3 lightDirN = normalize(global.lightDir);
    float diff = max(dot(norm, lightDirN), 0.0);
    vec3 lit = (global.lightAmbient + diff * global.lightColor * global.lightIntensity) * skyLight * texColor;
    
    // 水底的自发光/环境光保底，让远处不至于黑成一片
    vec3 waterAmbient = texColor * 0.6; 
    vec3 finalLight = max(lit, waterAmbient);

    // 面阴影
    float faceShade = 1.0;
    if (norm.y < -0.5) faceShade = 0.7; // 只有底面稍微暗一点
    
    // 输出半透明颜色，Alpha 设为 0.7 左右比较自然
    FragColor = vec4(finalLight * faceShade, 0.7);
}