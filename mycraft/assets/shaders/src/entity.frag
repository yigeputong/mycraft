#version 460 core

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in float ao;
layout(location = 4) in vec4 vLight;

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

// ★ 加上纹理声明
#ifdef USE_OPENGL
layout(binding = 0) uniform sampler2D uDiffuse;
#else
layout(set = 1, binding = 0) uniform sampler2D uDiffuse;
#endif

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
    vec4 texColor4 = texture(uDiffuse, uv);
    if (texColor4.a < 0.1) discard;

    vec3 texColor = texColor4.rgb;
    vec3 norm = normalize(Normal);

    float skyLight = vLight.r;
    vec3 blockLight = vLight.gba;

    vec3 lightDirN = normalize(global.lightDir);
    float diff = max(dot(norm, lightDirN), 0.0);

    vec3 lit = (global.lightAmbient + diff * global.lightColor * global.lightIntensity) * skyLight * texColor;
    vec3 torchLight = blockLight * texColor * 1.5;
    vec3 finalLight = max(max(lit, torchLight), texColor * 0.05);

    float faceShade;
    if      (norm.y >  0.5) faceShade = 1.00;
    else if (norm.y < -0.5) faceShade = 0.65;
    else if (abs(norm.x) > 0.5) faceShade = 0.85;
    else                        faceShade = 0.92;

    vec3 result = finalLight * faceShade;
    
    // 如果想用 tintColor，可以乘上去：
    // FragColor = vec4(result * pc.tintColor.rgb * mix(1.0, ao, global.aoStrength), pc.tintColor.a);
    
    FragColor = vec4(result * mix(1.0, ao, global.aoStrength), 1.0);
}