/*============================================================================================================================================
                                                    PARTICLEBILLBOARDRASTERIZATION.SLANG
============================================================================================================================================*/
// 🧩 Pure Slang billboard quad rasterizer implementing all 20 procedural sprite cutouts (0..19) + StreakThinness + OpticalBlend

import ParticleBuffers;

struct DispatchPushParams
{
    uint SlotOffset;
    uint SlotCount;
    uint ForceSourceCount;
    uint SpawnCursor;
};

[[vk::push_constant]]
DispatchPushParams PushParams;

[[vk::binding(0, 0)]] ConstantBuffer<GlobalParticleUniform> GlobalCfg;
[[vk::binding(1, 0)]] ConstantBuffer<RigParticleUniform>    RigCfg;
[[vk::binding(3, 0)]] StructuredBuffer<ParticleSlot>        ParticleBuffer;

struct BillboardVertexOut
{
    float4 ClipPosition : SV_Position;
    float2 QuadUv       : TEXCOORD0;
    float4 TintRgba     : COLOR0;
    float4 MetaParams   : TEXCOORD1; // x cutoutShape, y ageNorm, z seed, w speed
};

[shader("vertex")]
BillboardVertexOut VertexEntry(uint VertexId : SV_VulkanVertexID, uint InstanceId : SV_VulkanInstanceID)
{
    BillboardVertexOut Out;
    uint GlobalIdx = PushParams.SlotOffset + InstanceId;
    ParticleSlot Slot = ParticleBuffer[GlobalIdx];

    if (InstanceId >= PushParams.SlotCount || Slot.PositionAge.w >= Slot.VelocityLifetime.w)
    {
        Out.ClipPosition = float4(2.0, 2.0, 2.0, 1.0);
        Out.QuadUv       = float2(0.0, 0.0);
        Out.TintRgba     = float4(0.0, 0.0, 0.0, 0.0);
        Out.MetaParams   = float4(0.0, 0.0, 0.0, 0.0);
        return Out;
    }

    static const float2 QuadCorners[6] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0,  1.0),
        float2(-1.0, -1.0), float2(1.0,  1.0), float2(-1.0, 1.0)
    };
    float2 Corner = QuadCorners[VertexId % 6u];
    float Size    = max(0.005, Slot.Attributes.x * max(0.25, RigCfg.ScaleAndModes.x));

    float C = cos(Slot.Attributes.y);
    float S = sin(Slot.Attributes.y);
    float2 RotCorner = float2(Corner.x * C - Corner.y * S, Corner.x * S + Corner.y * C);

    float3 Right = GlobalCfg.CameraRight.xyz;
    float3 Up    = GlobalCfg.CameraUp.xyz;

    uint CutoutShape = uint(RigCfg.CarrierDispatch.z + 0.5);
    float Speed = length(Slot.VelocityLifetime.xyz);
    if (CutoutShape == 0u && Speed > 0.25) // Velocity-aligned streak
    {
        float3 VelAxis = normalize(Slot.VelocityLifetime.xyz);
        float3 ViewDir = normalize(GlobalCfg.CameraPosition.xyz - Slot.PositionAge.xyz);
        float3 Side    = normalize(cross(ViewDir, VelAxis));
        float Thinness = max(0.08, RigCfg.ReactionDamping.w);
        float Stretch  = clamp(1.0 + Speed * 0.45, 1.0, 6.0);
        Right = Side * Thinness;
        Up    = VelAxis * Stretch;
        RotCorner = Corner;
    }

    float3 WorldPos = Slot.PositionAge.xyz + Right * (RotCorner.x * Size) + Up * (RotCorner.y * Size);
    Out.ClipPosition = mul(GlobalCfg.ViewProjection, float4(WorldPos, 1.0));
    Out.QuadUv       = Corner;
    Out.TintRgba     = Slot.ColourRgba;
    float AgeNorm    = clamp(Slot.PositionAge.w / max(1.0e-4, Slot.VelocityLifetime.w), 0.0, 1.0);
    Out.MetaParams   = float4(float(CutoutShape), AgeNorm, Slot.Attributes.w, Speed);
    return Out;
}

[shader("fragment")]
float4 FragmentEntry(BillboardVertexOut In) : SV_Target
{
    float2 P   = In.QuadUv;
    float  R   = length(P);
    if (R > 1.0)
    {
        discard;
    }

    uint Shape = uint(In.MetaParams.x + 0.5);
    float Mask = 0.0;
    float3 Color = In.TintRgba.rgb;

    if (Shape == 0u) // Streak
    {
        Mask = exp(-P.x * P.x * 5.0) * smoothstep(1.0, 0.15, abs(P.y));
    }
    else if (Shape == 1u) // ShadedSphere
    {
        float Z = sqrt(max(0.0, 1.0 - R * R));
        float3 Nrm = normalize(float3(P.x, P.y, Z));
        float Light = 0.35 + 0.65 * max(0.0, dot(Nrm, normalize(float3(0.45, 0.55, 0.7))));
        Color *= Light;
        Mask = smoothstep(1.0, 0.85, R);
    }
    else if (Shape == 2u) // BotanicalLeaf / Paper
    {
        float Leaf = abs(P.x) * 1.45 + abs(P.y) * 0.55 + 0.25 * P.y * P.y;
        Mask = smoothstep(0.95, 0.75, Leaf);
    }
    else if (Shape == 3u || Shape == 19u) // SoftGlow / VolumetricWaterMist
    {
        Mask = pow(max(0.0, 1.0 - R), (Shape == 19u) ? 2.4 : 1.7);
    }
    else if (Shape == 4u) // CubeProjection
    {
        float BoxD = max(abs(P.x), abs(P.y));
        Mask = smoothstep(0.95, 0.82, BoxD);
    }
    else if (Shape == 5u) // MetallicCoin
    {
        float Rim = smoothstep(1.0, 0.86, R) * (0.7 + 0.3 * sin(R * 18.0));
        Color *= 0.85 + 0.35 * max(0.0, P.x + P.y);
        Mask = Rim;
    }
    else if (Shape == 6u) // IridescentBubble
    {
        float Shell = smoothstep(0.55, 0.94, R) * smoothstep(1.0, 0.92, R);
        Color = lerp(Color, float3(0.5 + 0.5 * sin(R * 12.0), 0.7 + 0.3 * cos(P.x * 6.0), 1.0), 0.45);
        Mask = max(Shell, 0.18 * (1.0 - R));
    }
    else if (Shape == 7u) // DendriticSnowflake
    {
        float Ang = atan2(P.y, P.x);
        float Branches = pow(abs(cos(Ang * 3.0)), 6.0);
        float Rings = 0.5 + 0.5 * cos(R * 18.0);
        Mask = smoothstep(1.0, 0.1, R) * clamp(Branches * (0.55 + 0.45 * Rings) + smoothstep(0.24, 0.0, R), 0.0, 1.0);
    }
    else if (Shape == 8u || Shape == 18u) // CrystallineHailstone / GlassShardCrystal
    {
        float Diamond = abs(P.x) + abs(P.y) * 1.15;
        Color *= 0.8 + 0.4 * step(0.0, P.x - P.y);
        Mask = smoothstep(1.05, 0.82, Diamond);
    }
    else if (Shape == 9u) // BokehDisc
    {
        Mask = smoothstep(1.0, 0.88, R) * (0.65 + 0.35 * smoothstep(0.72, 0.92, R));
    }
    else if (Shape == 10u || Shape == 15u) // ShockwaveRing / RadarSonarSweep
    {
        float Ring = exp(-pow((R - 0.78) * 7.5, 2.0));
        Mask = Ring;
    }
    else if (Shape == 11u || Shape == 16u) // PulseDome / EnergyBarrierShield
    {
        float Hex = abs(sin(P.x * 10.0)) * abs(sin(P.y * 10.0));
        Mask = smoothstep(1.0, 0.65, R) * (0.45 + 0.55 * step(0.25, Hex));
    }
    else if (Shape == 12u) // HologramGlitch
    {
        float Scan = 0.5 + 0.5 * sin(P.y * 28.0 + In.MetaParams.z * 40.0);
        Mask = smoothstep(1.0, 0.75, max(abs(P.x), abs(P.y))) * Scan;
    }
    else if (Shape == 13u) // WingedSilhouette
    {
        float Wing = abs(P.x) * 0.85 + abs(P.y - 0.25 * abs(P.x)) * 1.3;
        Mask = smoothstep(0.92, 0.72, Wing);
    }
    else if (Shape == 14u) // ViscousDroplet
    {
        float Drop = length(float2(P.x * (1.0 + 0.35 * P.y), P.y));
        Mask = smoothstep(0.95, 0.72, Drop);
    }
    else // 17 AuroraCurtain
    {
        Mask = exp(-P.x * P.x * 3.5) * smoothstep(1.0, 0.1, abs(P.y));
    }

    float Alpha = clamp(In.TintRgba.a * Mask, 0.0, 1.0);
    if (Alpha <= 0.004)
    {
        discard;
    }
    return float4(Color * Alpha, Alpha);
}
