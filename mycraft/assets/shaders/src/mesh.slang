// ---- UBO 定义不变 ----
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

[[vk::binding(0, 1)]]
Sampler2D uDiffuse;

struct PushConstants { float4x4 model; };
[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

struct VSInput {
    float3 inPosition : POSITION;
    float3 inNormal   : NORMAL;
    float2 inUV       : TEXCOORD0;
    float  inAO       : TEXCOORD1;
};

struct VSOutput {
    float4 pos    : SV_Position;
    float3 normal;
    float2 uv;
    float  ao;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput o;
    float4 world = mul(pc.model, float4(input.inPosition, 1.0));
    o.pos    = mul(global.projection, mul(global.view, world));
    o.normal = input.inNormal;
    o.uv     = input.inUV;
    o.ao     = input.inAO;
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput inVert) : SV_TARGET {
    float3 texColor = uDiffuse.Sample(inVert.uv).rgb;
    float3 norm = normalize(inVert.normal);

    // 平行光：global.lightDir 是"从表面指向太阳"的方向
    float3 lightDir = normalize(global.lightDir);
    float diff = max(dot(norm, lightDir), 0.0);

    float3 lit = global.lightAmbient * texColor
               + diff * global.lightColor * global.lightIntensity * texColor;

    float faceShade;
    if      (norm.y >  0.5) faceShade = 1.00;
    else if (norm.y < -0.5) faceShade = 0.65;
    else if (abs(norm.x) > 0.5) faceShade = 0.85;
    else                        faceShade = 0.92;

    float3 result = lit * faceShade;
    result = max(result, texColor * 0.35);

    float ao = lerp(1.0, inVert.ao, global.aoStrength);
    return float4(result * ao, 1.0);
}