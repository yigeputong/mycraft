struct VSOutput {
    float4 pos : SV_Position;
    float2 uv;
};

// ★ set 0, binding 0 —— fullscreen pipeline 只有一个 set
[[vk::binding(0, 0)]]
Sampler2D uScene;

[shader("vertex")]
VSOutput vertMain(uint vid : SV_VertexID) {
    // 全屏三角形
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };
    float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };

    VSOutput o;
    o.pos = float4(positions[vid], 0.0, 1.0);
    o.uv  = uvs[vid];
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput i) : SV_TARGET {
    float3 color = uScene.Sample(i.uv).rgb;
    // gamma 校正（引擎 FBO 存线性值）
    // color = pow(color, 1.0 / 2.2);
    return float4(color, 1.0);
}