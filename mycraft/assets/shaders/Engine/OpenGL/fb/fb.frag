#version 460 core
in vec2 uv;
out vec4 FragColor;
uniform sampler2D screenTexture;

void main() {
    vec3 color = texture(screenTexture, uv).rgb;
    FragColor = vec4(color, 1.0);
}