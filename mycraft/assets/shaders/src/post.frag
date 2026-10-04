#version 460 core

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uScene;

void main() {
    vec3 color = texture(uScene, vUV).rgb;
    FragColor = vec4(color, 1.0);
}