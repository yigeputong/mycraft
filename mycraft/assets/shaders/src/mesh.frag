#version 460 core

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in float ao;

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
layout(binding = 0) uniform sampler2D uDiffuse;           // GL: texture unit 0
#else
layout(set = 1, binding = 0) uniform sampler2D uDiffuse;  // VK: set 1, binding 0
#endif

void main() {
    vec3 texColor = texture(uDiffuse, uv).rgb;
    vec3 norm = normalize(Normal);

    vec3 lightDirN = normalize(global.lightDir);
    float diff = max(dot(norm, lightDirN), 0.0);

    vec3 lit = global.lightAmbient * texColor
             + diff * global.lightColor * global.lightIntensity * texColor;

    float faceShade;
    if      (norm.y >  0.5) faceShade = 1.00;
    else if (norm.y < -0.5) faceShade = 0.65;
    else if (abs(norm.x) > 0.5) faceShade = 0.85;
    else                        faceShade = 0.92;

    vec3 result = lit * faceShade;
    result = max(result, texColor * 0.35);

    FragColor = vec4(result * mix(1.0, ao, global.aoStrength), 1.0);
}