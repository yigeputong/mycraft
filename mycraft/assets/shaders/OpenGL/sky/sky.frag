#version 460 core

layout (std140, binding = 0) uniform GlobalData {
    mat4 uView;
    mat4 uProjection;
    vec3 uViewPos;
    float aoStrength;      // 原 _pad0
    vec3 uLightDir;
    float uLightIntensity;
    vec3 uLightColor;
    float timeOfDay;       // 原 _pad1
    vec3 uLightAmbient;
    float _pad2;
};

in vec3 vDir;
out vec4 FragColor;

// 太阳方向：6 点从东边升起，18 点从西边落下
vec3 ComputeSunDir(float t) {
    float angle = (t / 24.0) * 6.2831853 - 1.5707963;
    return normalize(vec3(cos(angle), sin(angle), 0.3));
}

// 廉价的 hash 用于星星
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

void main() {
    vec3 dir = normalize(vDir);
    vec3 sunDir = ComputeSunDir(timeOfDay);

    float sunH = sunDir.y;                          // -1 ~ 1
    float dayFactor = clamp(sunH * 2.0, 0.0, 1.0);  // 0 夜，1 昼
    float twilight  = 1.0 - abs(sunH) * 3.0;        // 日出日落窗口
    twilight = clamp(twilight, 0.0, 1.0);

    // --- 基础渐变 ---
    vec3 dayHorizon   = vec3(0.65, 0.78, 0.90);
    vec3 dayZenith    = vec3(0.22, 0.42, 0.85);
    vec3 nightHorizon = vec3(0.03, 0.04, 0.09);
    vec3 nightZenith  = vec3(0.01, 0.01, 0.04);

    vec3 horizon = mix(nightHorizon, dayHorizon, dayFactor);
    vec3 zenith  = mix(nightZenith,  dayZenith,  dayFactor);

    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 col = mix(horizon, zenith, pow(t, 0.6));

    // --- 日出/日落地平线辉光 ---
    float sunDot = max(dot(dir, sunDir), 0.0);
    float glow = pow(sunDot, 8.0) * twilight;
    col += vec3(1.0, 0.45, 0.15) * glow * 1.5;

    // --- 太阳圆盘 ---
    float sunDisc = pow(sunDot, 800.0);
    col += vec3(1.0, 0.95, 0.85) * sunDisc * clamp(sunH * 4.0, 0.0, 1.0);

    // --- 夜间星星 ---
    float starNight = clamp(-sunH * 4.0, 0.0, 1.0);
    if (starNight > 0.0 && dir.y > 0.0) {
        vec3 grid = floor(dir * 120.0);
        float h = hash13(grid);
        float star = smoothstep(0.997, 1.0, h);
        col += vec3(star) * starNight;
    }

    FragColor = vec4(col, 1.0);
}