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
    vec4 texColor4 = texture(uDiffuse, uv);
    if (texColor4.a < 0.5) discard;
    
    vec3 texColor = texColor4.rgb;
    vec3 norm = normalize(Normal);
    
    float skyLight = vLight.r;
    vec3 blockLight = vLight.gba;

    // 1. 经典基础光照（方向光 + 环境光）
    vec3 lightDirN = normalize(global.lightDir);
    float diff = max(dot(norm, lightDirN), 0.0);
    
    // 将环境光与方向光乘以天空光，洞穴里会自然变暗
    vec3 lit = (global.lightAmbient + diff * global.lightColor * global.lightIntensity) * skyLight * texColor;

    // 2. 方块光（火把光）叠加
    vec3 torchLight = blockLight * texColor * 1.5;

    // 3. 取最大值，给个保底亮度 0.05
    vec3 finalLight = max(max(lit, torchLight), texColor * 0.05);

    // 4. 面阴影（模拟伪立体感）
    float faceShade;
    if      (norm.y >  0.5) faceShade = 1.00;
    else if (norm.y < -0.5) faceShade = 0.65;
    else if (abs(norm.x) > 0.5) faceShade = 0.85;
    else                        faceShade = 0.92;

    // 5. 结合 AO 输出
    vec3 result = finalLight * faceShade;
    FragColor = vec4(result * mix(1.0, ao, global.aoStrength), 1.0);
}