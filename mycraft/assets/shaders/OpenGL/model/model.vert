#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

layout (location = 3) in vec4 aInstanceMatrix0;
layout (location = 4) in vec4 aInstanceMatrix1;
layout (location = 5) in vec4 aInstanceMatrix2;
layout (location = 6) in vec4 aInstanceMatrix3;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 FragPos;
out vec3 Normal;
out vec2 uv;

void main() {
    mat4 instanceMatrix = mat4(
        aInstanceMatrix0,
        aInstanceMatrix1,
        aInstanceMatrix2,
        aInstanceMatrix3
    );

    vec4 worldPos = instanceMatrix * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal = mat3(instanceMatrix) * aNormal;
    uv = aUV;

    gl_Position = uProjection * uView * worldPos;
}