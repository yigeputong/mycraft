#version 460 core

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in float ao;
layout(location = 4) in vec4 vLight; // ★ 接收光照

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
    vec4 texColor4 = texture(uDiffuse, uv);
    
    if (texColor4.a < 0.1) {
        discard;
    }
    
    vec3 texColor = texColor4.rgb;
    vec3 norm = normalize(Normal);
    
    // 提取光照数据
    float skyLight = vLight.r;
    vec3 blockLight = vLight.gba;

    // 1. 原有的基础光照，根据天空光进行衰减
    vec3 lightDirN = normalize(global.lightDir);
    float diff = max(dot(norm, lightDirN), 0.0);
    
    // 将环境光与方向光乘以天空光，这样洞穴里就会变暗
    vec3 lit = (global.lightAmbient + diff * global.lightColor * global.lightIntensity) * skyLight * texColor;

    // 2. 方块光（火把光）叠加
    vec3 torchLight = blockLight * texColor * 1.5; // 1.5 是火把亮度系数，可调

    // 3. 取两者最大值，并给个保底亮度 0.05
    vec3 finalLight = max(max(lit, torchLight), texColor * 0.05);

    // 4. 原有的面阴影
    float faceShade;
    if      (norm.y >  0.5) faceShade = 1.00;
    else if (norm.y < -0.5) faceShade = 0.65;
    else if (abs(norm.x) > 0.5) faceShade = 0.85;
    else                        faceShade = 0.92;

    // 5. 结合 AO 输出
    vec3 result = finalLight * faceShade;
    FragColor = vec4(result * mix(1.0, ao, global.aoStrength), 1.0);
}