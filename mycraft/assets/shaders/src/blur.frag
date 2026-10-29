#version 460 core
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 FragColor;

#ifdef USE_OPENGL
layout(binding = 0) uniform sampler2D tex0;
layout(location = 0) uniform float uThreshold;
layout(location = 1) uniform float uExposure;
layout(location = 2) uniform float uBloomStrength;
layout(location = 3) uniform vec2 uDirection;

struct PC {
    float threshold;
    float exposure;
    float bloomStrength;
    vec2 direction;
};
PC pc = PC(uThreshold, uExposure, uBloomStrength, uDirection);
#else
layout(binding = 0) uniform sampler2D tex0;
layout(push_constant) uniform PostProcessPC {
    float threshold; 
    float exposure; 
    float bloomStrength; 
    float padding1;
    vec2 direction; 
    vec2 padding2;
} pc;
#endif

void main() {
    vec3 result = vec3(0.0);
    float weights[5] = float[](0.227, 0.194, 0.121, 0.054, 0.016);
    for (int i = -4; i <= 4; ++i) {
        result += texture(tex0, vUv + pc.direction * float(i)).rgb * weights[abs(i)];
    }
    FragColor = vec4(result, 1.0);
}