// ==================== 全局 UBO (set 0, binding 0) ====================
struct GlobalData {
    float4x4 view;
    float4x4 projection;
    float3   viewPos;
    float    aoStrength;
    float3   lightDir;
    float    lightIntensity;
    float3   lightColor;
    float    timeOfDay;
    float3   lightAmbient;
    float    _pad2;
};

[[vk::binding(0, 0)]]
ConstantBuffer<GlobalData> global;

// ==================== 顶点输入 ====================
struct VSInput {
    float3 inPosition : POSITION;
};

// ==================== 顶点输出 ====================
struct VSOutput {
    float4 pos : SV_Position;
    float3 dir;
};

// ==================== 顶点着色器 ====================
[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput o;

    // 去掉 view 的平移部分 —— 天空盒跟随相机
    float3x3 viewRot = float3x3(
        global.view[0].xyz,
        global.view[1].xyz,
        global.view[2].xyz
    );
    float3 rotated = mul(viewRot, input.inPosition);
    float4 clip = mul(global.projection, float4(rotated, 1.0));

    // z = w → NDC z = 1（最远），配合 pipeline 的 LEQUAL 深度测试
    o.pos = float4(clip.xy, clip.w, clip.w);
    o.dir = input.inPosition;
    return o;
}

// ==================== 星星 hash ====================
float hash13(float3 p) {
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

// ==================== 片元着色器 ====================
[shader("fragment")]
float4 fragMain(VSOutput i) : SV_TARGET {
    float3 dir = normalize(i.dir);

    float angle = (global.timeOfDay / 24.0) * 6.2831853 - 1.5707963;
    float3 sunDir = normalize(float3(cos(angle), sin(angle), 0.3));

    float sunH = sunDir.y;
    float dayFactor = saturate(sunH * 2.0);
    float twilight = saturate(1.0 - abs(sunH) * 3.0);

    // ---- 基础渐变 ----
    float3 dayHorizon = float3(0.65, 0.78, 0.90);
    float3 dayZenith = float3(0.22, 0.42, 0.85);
    float3 nightHorizon = float3(0.03, 0.04, 0.09);
    float3 nightZenith = float3(0.01, 0.01, 0.04);

    float3 horizon = lerp(nightHorizon, dayHorizon, dayFactor);
    float3 zenith = lerp(nightZenith, dayZenith, dayFactor);

    float t = saturate(dir.y * 0.5 + 0.5);
    float3 col = lerp(horizon, zenith, pow(t, 0.6));

    // ---- 日出/日落地平线辉光 ----
    float sunDot = max(dot(dir, sunDir), 0.0);
    float glow = pow(sunDot, 8.0) * twilight;
    col += float3(1.0, 0.45, 0.15) * glow * 1.5;

    // ---- 太阳圆盘 ----
    float sunDisc = pow(sunDot, 800.0);
    col += float3(1.0, 0.95, 0.85) * sunDisc * saturate(sunH * 4.0);


    // ---- 夜间星星 ----
    // 替换星星那段
    float starNight = saturate(-sunH * 4.0);
    // 在星星之前
    float3 nebulaP = dir * 3.0;
    float nebula = 0.0;
    nebula += hash13(nebulaP + 0.0) * 0.5;
    nebula += hash13(nebulaP * 2.0 + 7.0) * 0.25;
    nebula += hash13(nebulaP * 4.0 + 13.0) * 0.125;
    col += float3(0.15, 0.1, 0.2) * nebula * starNight * 0.3;
    if (starNight > 0.0 && dir.y > 0.0) {
        // ★ 更细网格
        float3 grid = floor(dir * 400.0);
        float3 local = frac(dir * 400.0); // 格子内归一化坐标
        float h = hash13(grid);

        // ★ 严阈值（星星更稀）
        if (h > 0.997) {
            // ★ 用格子内到中心的距离做点状衰减
            float3 c = local - 0.5;
            float d2 = dot(c, c);
            float star = exp(-d2 * 80.0); // 高斯点
            col += float3(star, star, star) * starNight;
        }
    }

    return float4(col, 1.0);
}
