#version 460 core

in vec3 FragPos;
in vec3 Normal;
in vec2 uv;

out vec4 FragColor;

uniform sampler2D uDiffuseTexture;
uniform vec3 uLightPos;
uniform vec3 uViewPos;

void main() {
    // 环境光
    vec3 ambient = 0.3 * vec3(texture(uDiffuseTexture, uv));

    // 漫反射
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(uLightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * vec3(texture(uDiffuseTexture, uv));

    // FragColor = vec4(ambient + diffuse, 1.0);
    FragColor = texture(uDiffuseTexture, uv);   
}