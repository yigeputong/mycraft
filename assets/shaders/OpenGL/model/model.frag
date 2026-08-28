#version 330 core

in vec3 Normal;
in vec2 uv;

out vec4 FragColor;

uniform sampler2D texture1;

void main() {
    FragColor = texture(texture1, uv);
}