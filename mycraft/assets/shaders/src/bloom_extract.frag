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
    vec3 color = texture(tex0, vUv).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    
    // 使用 smoothstep 做软提取
    float soft = clamp(brightness - pc.threshold, 0.0, 1.0);
    soft = soft * soft * 0.5; 
    
    // 提取出的高光乘以 soft 系数，避免过亮
    FragColor = vec4(color * soft, 1.0);
}