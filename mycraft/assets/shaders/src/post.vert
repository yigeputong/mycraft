#version 460 core

layout(location = 0) out vec2 vUV;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 uvs[3] = vec2[](
        vec2(0.0, 0.0),
        vec2(2.0, 0.0),
        vec2(0.0, 2.0)
    );
#ifdef USE_OPENGL
    int idx = gl_VertexID;
#else
    int idx = gl_VertexIndex;
#endif
    gl_Position = vec4(positions[idx], 0.0, 1.0);
    vUV = uvs[idx];
}