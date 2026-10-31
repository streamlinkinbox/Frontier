/*==========================================================================================================================================
                                                  SURFACECARDRADIOSITY.SLANG
==========================================================================================================================================*/
// 🧩 Multi-bounce planar surface-card radiosity atlas solver (CardCapture -> CardDirect -> CardRadiosity) for GTX / non-RT GPUs.

struct SurfaceCardRecord
{
    float4 OriginAndArea;       // xyz = card corner [m], w = texel area [m^2]
    float4 AxisUAndAlbedoR;     // xyz = edge U [m], w = albedo.r
    float4 AxisVAndAlbedoG;     // xyz = edge V [m], w = albedo.g
    float4 NormalAndAlbedoB;    // xyz = unit normal, w = albedo.b
    uint4  AtlasRect;           // xy = atlas origin texel, zw = texel dimensions
};

struct CardRadiosityConstants
{
    float4 SunDirectionAndIntensity;
    float4 SkyAmbientRadiance;
    uint4  CardCounts;          // x = card count, y = total atlas texels, z = bounce index, w = visibility samples
};

[[vk::binding(0, 0)]] StructuredBuffer<SurfaceCardRecord> Cards;
[[vk::binding(1, 0)]] StructuredBuffer<float4>            PreviousCardIrradiance;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4>          NextCardIrradiance;
[[vk::push_constant]] ConstantBuffer<CardRadiosityConstants> Radiosity;

float EvaluateRegularizedFormFactor(float3 FromPos, float3 FromNormal, float FromArea,
                                    float3 ToPos, float3 ToNormal, float ToArea)
{
    float3 Delta  = ToPos - FromPos;
    float  DistSq = dot(Delta, Delta);
    if (DistSq <= 1e-6)
        return 0.0;
    float  InvDist   = rsqrt(DistSq);
    float3 Direction = Delta * InvDist;
    float  CosFrom   = max(0.0, dot(FromNormal, Direction));
    float  CosTo     = max(0.0, dot(ToNormal, -Direction));
    if (CosFrom <= 0.0 || CosTo <= 0.0)
        return 0.0;
    float EffectiveDistSq = DistSq + (FromArea + ToArea) * 0.318309886;
    return (CosFrom * CosTo * ToArea) * 0.318309886 / EffectiveDistSq;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void SolveSurfaceCardBounce(uint3 Dispatch : SV_DispatchThreadID)
{
    uint CardIndex = Dispatch.x;
    if (CardIndex >= Radiosity.CardCounts.x)
        return;

    SurfaceCardRecord Receiver = Cards[CardIndex];
    float3 Albedo = float3(Receiver.AxisUAndAlbedoR.w, Receiver.AxisVAndAlbedoG.w, Receiver.NormalAndAlbedoB.w);
    float3 Center = Receiver.OriginAndArea.xyz + 0.5 * (Receiver.AxisUAndAlbedoR.xyz + Receiver.AxisVAndAlbedoG.xyz);
    float3 Normal = normalize(Receiver.NormalAndAlbedoB.xyz);

    float  SunCos = max(0.0, dot(Normal, normalize(Radiosity.SunDirectionAndIntensity.xyz)));
    float3 Direct = Albedo * (Radiosity.SunDirectionAndIntensity.w * SunCos +
                              Radiosity.SkyAmbientRadiance.xyz * (0.35 + 0.65 * saturate(Normal.z * 0.5 + 0.5)));

    float3 Gathered = float3(0.0, 0.0, 0.0);
    for (uint SenderIndex = 0u; SenderIndex < Radiosity.CardCounts.x; ++SenderIndex)
    {
        if (SenderIndex == CardIndex)
            continue;
        SurfaceCardRecord Sender = Cards[SenderIndex];
        float3 SenderCenter = Sender.OriginAndArea.xyz + 0.5 * (Sender.AxisUAndAlbedoR.xyz + Sender.AxisVAndAlbedoG.xyz);
        float  FormFactor   = EvaluateRegularizedFormFactor(
            Center, Normal, Receiver.OriginAndArea.w,
            SenderCenter, normalize(Sender.NormalAndAlbedoB.xyz), Sender.OriginAndArea.w);
        Gathered += FormFactor * PreviousCardIrradiance[SenderIndex].xyz;
    }

    NextCardIrradiance[CardIndex] = float4(Direct + Albedo * Gathered, 1.0);
}
