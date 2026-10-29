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

const float PI = 3.14159265359;

// ==========================================
// PBR 核心函数
// ==========================================
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return nom / max(denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / max(denom, 0.0001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec4 texColor4 = texture(uDiffuse, uv);
    if (texColor4.a < 0.1) discard;
    
    // 将贴图转换到线性空间（假设贴图是 sRGB）
    vec3 albedo = pow(texColor4.rgb, vec3(2.2)); 
    vec3 norm = normalize(Normal);
    
    float skyLight = vLight.r;
    vec3 blockLight = vLight.gba;

    // ==========================================
    // 材质推断 (因为没有多余的贴图，我们靠经验猜)
    // ==========================================
    // 假设大部分方块是绝缘体(金属度0)，粗糙度0.7
    float metallic = 0.0;
    float roughness = 0.7;
    
    // ★ 比如：你可以根据 UV 坐标或者颜色特征，把雪地变得极其粗糙(不反光)，把冰块变光滑
    float brightness = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    if (brightness > 0.7) {
        roughness = 0.9; // 雪地、白沙，极度粗糙，防止刺眼
    }

    // ==========================================
    // 基础光照向量
    // ==========================================
    vec3 V = normalize(global.viewPos - FragPos);
    vec3 L = normalize(global.lightDir);
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // 平面光照计算
    float NdotL = max(dot(norm, L), 0.0);
    
    // Cook-Torrance 高光计算
    float NDF = DistributionGGX(norm, H, roughness);
    float G   = GeometrySmith(norm, V, L, roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(norm, V), 0.0) * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    // 能量守恒
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    // 计算真实光源辐射度 (Radiance)
    // 乘以 2.0 增加一点阳光强度，以产生更强烈的 HDR 效果
    vec3 radiance = global.lightColor * global.lightIntensity * 2.0; 

    // 最终平行光 Lo (漫反射 + 高光)
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // ==========================================
    // 环境光与方块光
    // ==========================================
    // 环境光受到天空光(skyLight)影响
    vec3 ambient = global.lightAmbient * albedo * skyLight;
    
    // 火把光 (简单的点光/漫反射模拟，不计算高光以节省性能)
    vec3 torchLight = blockLight * albedo * 1.5; 

    // ==========================================
    // 结合 AO 与 面阴影
    // ==========================================
    float faceShade;
    if      (norm.y >  0.5) faceShade = 1.00;
    else if (norm.y < -0.5) faceShade = 0.65;
    else if (abs(norm.x) > 0.5) faceShade = 0.85;
    else                        faceShade = 0.92;

    // 将结果累加，并乘上面阴影和 AO
    vec3 result = ambient + Lo + torchLight;
    result *= faceShade;
    result *= mix(1.0, ao, global.aoStrength);

    // 输出 HDR 颜色（后续会被 ACES 映射）
    FragColor = vec4(result, 1.0);
}