#version 460 core
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 FragColor;

vec3 ACESFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

#ifdef USE_OPENGL
layout(binding = 0) uniform sampler2D tex0;
layout(binding = 1) uniform sampler2D tex1;
layout(location = 0) uniform float uThreshold;
layout(location = 1) uniform float uExposure;
layout(location = 2) uniform float uBloomStrength;
layout(location = 3) uniform vec2 uDirection;
#else
layout(binding = 0) uniform sampler2D tex0;
layout(binding = 1) uniform sampler2D tex1;
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
#ifdef USE_OPENGL
    float t = uThreshold;
    float e = uExposure;
    float b = uBloomStrength;
    vec2 d = uDirection;
#else
    float t = pc.threshold;
    float e = pc.exposure;
    float b = pc.bloomStrength;
    vec2 d = pc.direction;
#endif

    vec3 scene = texture(tex0, vUv).rgb;
    vec3 bloom = texture(tex1, vUv).rgb;
    
    vec3 result = scene + bloom * b;
    result *= e;
    result = ACESFilm(result);
    
    float luma = dot(result, vec3(0.2126, 0.7152, 0.0722));
    result = mix(vec3(luma), result, 1.2);
    result = pow(result, vec3(1.0 / 2.2));
    
    FragColor = vec4(result, 1.0);
}