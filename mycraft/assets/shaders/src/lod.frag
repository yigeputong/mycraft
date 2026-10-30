#version 460

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vLightInfo; // 接收 uvInfo
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
    // ★ 核心：局部平铺 (Tiling)
    // fract() 会取小数部分，自动把超过 1.0 的 UV 循环映射回 0.0~1.0
    vec2 localUV = fract(vUV); 
    
    // 将局部 0~1 映射到图集中该格子的真实范围内
    vec2 tiledUV = vLightInfo.yz + localUV * vLightInfo.w;

    vec4 texColor4 = textureLod(uDiffuse, tiledUV, 0.0);
    if (texColor4.a < 0.1) discard;
    
    vec3 texColor = texColor4.rgb;
    vec3 norm = normalize(vNormal);
    
    // LOD 永远是露天的
    float skyLight = 1.0;

    // 1. 经典基础光照（方向光 + 环境光）
    vec3 lightDirN = normalize(global.lightDir);
    float diff = max(dot(norm, lightDirN), 0.0);
    
    vec3 lit = (global.lightAmbient + diff * global.lightColor * global.lightIntensity) * skyLight * texColor;

    // 2. 给个保底亮度
    vec3 finalLight = max(lit, texColor * 0.05);

    // 3. 面阴影（伪立体感）
    float faceShade;
    if      (norm.y >  0.5) faceShade = 1.00;
    else if (norm.y < -0.5) faceShade = 0.65;
    else if (abs(norm.x) > 0.5) faceShade = 0.85;
    else                        faceShade = 0.92;

    vec3 result = finalLight * faceShade;
    
    FragColor = vec4(result, 1.0);
}