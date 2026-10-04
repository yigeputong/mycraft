struct VSInput {
    float3 inPosition : POSITION;
    float3 inNormal   : NORMAL;
    float2 inUV       : TEXCOORD0;
    float  inAO       : TEXCOORD1;
};

struct GlobalData {
    float4x4 view;
    float4x4 projection;
    float3 viewPos;
    float aoStrength; // ★ 原 _pad0
    float3 lightDir;
    float lightIntensity;
    float3 lightColor;
    float timeOfDay; // ★ 原 _pad1
    float3 lightAmbient;
    float _pad2;
};
[[vk::binding(0, 0)]]
ConstantBuffer<GlobalData> global;

[[vk::binding(0, 1)]]
Sampler2D uDiffuse;

// ====== model 矩阵：VK 走 push_constant，GL 走普通 uniform ======
#ifdef GL_TARGET
uniform float4x4 uModel;
#else
struct PushConstants { float4x4 model; };
[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;
#endif

struct VSOutput {
    float4 pos    : SV_Position;
    float3 normal;
    float2 uv;
    float  ao;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput o;
#ifdef GL_TARGET
    float4x4 modelMat = uModel;
#else
    float4x4 modelMat = pc.model;
#endif
    float4 world = mul(modelMat, float4(input.inPosition, 1.0));
    o.pos    = mul(global.projection, mul(global.view, world));
    o.normal = input.inNormal;
    o.uv     = input.inUV;
    o.ao     = input.inAO;
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput inVert) : SV_TARGET {
    float4 sampled = uDiffuse.Sample(inVert.uv);
    float3 texColor = sampled.rgb;
    float3 norm = normalize(inVert.normal);

    // 平行光（和 OpenGL 版一致）
    float3 lightDir = normalize(-global.lightDir);
    float diff = max(dot(norm, lightDir), 0.0);
    float3 lit = global.lightAmbient * texColor
               + diff * global.lightColor * global.lightIntensity * texColor;

    // 面朝向固定亮度
    float faceShade;
    if (norm.y > 0.5) faceShade = 1.00;
    else if (norm.y < -0.5) faceShade = 0.55;
    else if (abs(norm.x) > 0.5) faceShade = 0.75;
    else faceShade = 0.85;

    float3 result = lit * faceShade;

    // AO 混合
    float ao = lerp(1.0, inVert.ao, global.aoStrength);
    return float4(result * ao, 1.0);
}