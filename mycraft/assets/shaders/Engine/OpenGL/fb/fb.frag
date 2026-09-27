#version 460 core
in vec2 uv;
out vec4 FragColor;
uniform sampler2D screenTexture;
uniform float uGamma;
void main() {
    vec3 color = texture(screenTexture, uv).rgb;
    color = pow(color, vec3(1.0 / uGamma));
    FragColor = vec4(color, 1.0);
}