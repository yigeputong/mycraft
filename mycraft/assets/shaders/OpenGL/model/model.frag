#version 460 core

in vec3 FragPos;
in vec3 Normal;
in vec2 uv;

out vec4 FragColor;

uniform sampler2D uDiffuseTexture;
uniform vec3 uViewPos;

// ★ 平行光
uniform vec3 uLightDir;        // 光照方向（从物体指向光源，需归一化）
uniform vec3 uLightColor;      // 光照颜色
uniform float uLightAmbient;   // 环境光强度
uniform float uLightIntensity; // 光照强度

void main() {
    vec3 texColor = texture(uDiffuseTexture, uv).rgb;

    // 环境光
    vec3 ambient = uLightAmbient * texColor;

    // 漫反射：光线方向固定
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-uLightDir);   // 指向光源的方向
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor * uLightIntensity * texColor;

    // 镜面高光
    vec3 viewDir = normalize(uViewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = spec * uLightColor * 0.5;

    FragColor = vec4(ambient + diffuse + specular, 1.0);
}