//==========================================================================================================================================
//                                                      PREVIEW RASTER
//==========================================================================================================================================
// 🧩 Editor & Development ONLY GPU Vulkan 1.3 preview & inspection rasterizer with modular fast shadow selection:
//      0 = None, 1 = ContactEllipsoid, 2 = OrthoClusterDepth (1024x1024 atomic solar depth + 4-tap PCF),
//      3 = SignedDistanceCone (.sdfspace sphere-traced cone), 4 = HybridOrthoAndDistance.
//    Strictly excluded from Shipping builds.

#include "PlanetaryRecords.slang"
#include "VolumeDistanceSampling.slang"

[[vk::push_constant]] PushDispatchHeader Push;

// Header layout in ClusterSceneHeader:
//   [0]: {CamOrigin.xyz, TanHalfV}
//   [1]: {CamRight.xyz, TanHalfH}
//   [2]: {CamUp.xyz, BaseErrorThresholdPixels}
//   [3]: {CamFwd.xyz, MaxTierIndex}
//   [4]: {ZenithSkyRgb.xyz, float(ShadowMode)}
//   [5]: {HorizonSkyRgb.xyz, float(ShadowMapRes)}
//   [6]: {ShadowOrig.xyz, InvHalfSpan}
//   [7]: {ShadowRight.xyz, InvDepthSpan}
//   [8]: {ShadowUp.xyz, HalfSpanM}
//   [9]: {ShadowLightFwd.xyz, DepthSpanM}
//   [10 + ClIdx * 4 + 0]: {ScreenMinX, ScreenMinY, ScreenMaxX, ScreenMaxY}
//   [10 + ClIdx * 4 + 1]: {TriStart, TriCount, GlobalClusterId, TierIndex}
//   [10 + ClIdx * 4 + 2]: {Albedo.r, Albedo.g, Albedo.b, Roughness}
//   [10 + ClIdx * 4 + 3]: {Metallic, ClearcoatOrIor, PackedFlags, ProjectedErrorPx}

[[vk::binding(0, 0)]] StructuredBuffer<float4>                     ClusterSceneHeader;
[[vk::binding(1, 0)]] StructuredBuffer<float4>                     TriangleGeometry;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint>                     TileClusterBins;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint>                     SolarShadowDepth;
[[vk::binding(4, 0)]] StructuredBuffer<float4>                     DistanceVolumeTable;
[[vk::binding(5, 0)]] [format("rgba16f")] RWTexture2DArray<float4> PreviewTarget;

static const uint kClusterBaseSlot   = 10u;
static const uint kClusterStride     = 4u;
static const uint kTriangleStride    = 4u;
static const uint kTileSizePx        = 16u;
static const uint kTileSlotStride    = 64u;
static const uint kShadowDepthMaxInt = 16777215u;

float3 EvaluateSimplePreviewSky(float3 RayDir, float3 SunDir, float3 ZenithRgb, float3 HorizonRgb)
{
    float  UpFactor    = saturate(RayDir.y);
    float3 SkyGrad     = lerp(HorizonRgb, ZenithRgb, pow(UpFactor, 0.58f));
    float  GroundBlend = smoothstep(-0.08f, 0.02f, RayDir.y);
    float3 GroundTone  = HorizonRgb * float3(0.24f, 0.25f, 0.28f);
    float3 BaseSky     = lerp(GroundTone, SkyGrad, GroundBlend);

    float  SunCos      = dot(RayDir, SunDir);
    float  MieGlow     = pow( max(0.0f, SunCos), 12.0f) * 0.35f + pow(max(0.0f, SunCos), 96.0f) * 1.4f;
    float  SolarDisc   = smoothstep(0.9994f, 0.99985f, SunCos) * 14.0f;
    return BaseSky + float3(1.0f, 0.92f, 0.78f) * (MieGlow + SolarDisc) * GroundBlend;
}

float3 HashClusterColor(uint ClusterId)
{
    float Hue = frac(float(ClusterId) * 0.61803398875f + 0.13f);
    float R   = abs(Hue * 6.0f - 3.0f) - 1.0f;
    float G   = 2.0f - abs(Hue * 6.0f - 2.0f);
    float B   = 2.0f - abs(Hue * 6.0f - 4.0f);
    return lerp(float3(0.22f), saturate(float3(R, G, B)), 0.78f);
}

float3 EvaluateHeatGradient(float NormalizedT)
{
    float  T  = saturate(NormalizedT);
    float3 C0 = float3(0.14f, 0.36f, 0.82f);
    float3 C1 = float3(0.16f, 0.74f, 0.66f);
    float3 C2 = float3(0.94f, 0.76f, 0.22f);
    float3 C3 = float3(0.92f, 0.28f, 0.22f);
    if (T < 0.333f)
    {
        return lerp(C0, C1, T * 3.0f);
    }
    if (T < 0.666f)
    {
        return lerp(C1, C2, (T - 0.333f) * 3.0f);
    }
    return lerp(C2, C3, (T - 0.666f) * 3.0f);
}

float SampleOrthoClusterShadow(float3 WorldPos, float3 WorldNormal, float3 SunDir)
{
    uint   ShadowRes    = uint(ClusterSceneHeader[5].w + 0.5f);
    float3 ShadowOrig   = ClusterSceneHeader[6].xyz;
    float  InvHalfSpan  = ClusterSceneHeader[6].w;
    float3 ShadowRight  = ClusterSceneHeader[7].xyz;
    float  InvDepthSpan = ClusterSceneHeader[7].w;
    float3 ShadowUp     = ClusterSceneHeader[8].xyz;
    float3 LightFwd     = ClusterSceneHeader[9].xyz;

    // Normal + slope-scaled receiver bias to prevent self-shadow acne
    float  NdotL      = saturate(dot(WorldNormal, SunDir));
    float  BiasMeters = lerp(0.032f, 0.010f, NdotL);
    float3 BiasedPos  = WorldPos + WorldNormal * BiasMeters + SunDir * 0.008f;
    float3 Rel        = BiasedPos - ShadowOrig;

    float U = 0.5f + 0.5f * (dot(Rel, ShadowRight) * InvHalfSpan);
    float V = 0.5f - 0.5f * (dot(Rel, ShadowUp) * InvHalfSpan);
    float Z = dot(Rel, LightFwd) * InvDepthSpan;

    if (U <= 0.002f || U >= 0.998f || V <= 0.002f || V >= 0.998f || Z <= 0.0f || Z >= 1.0f)
    {
        return 1.0f;
    }

    uint   ReceiverDepth = uint(saturate(Z) * float(kShadowDepthMaxInt));
    float2 BaseTexel     = float2(U, V) * float(ShadowRes);
    float2 Offsets[5]    = {
        float2( 0.0f,  0.0f),
        float2(-0.85f, -0.35f),
        float2( 0.35f, -0.85f),
        float2( 0.85f,  0.35f),
        float2(-0.35f,  0.85f),
    };

    float LitAccum = 0.0f;
    [unroll]
    for (int Tap = 0; Tap < 5; ++Tap)
    {
        int2 Texel = clamp(int2(BaseTexel + Offsets[Tap]), int2(0, 0), int2(int(ShadowRes) - 1, int(ShadowRes) - 1));
        uint StoredDepth = SolarShadowDepth[uint(Texel.y) * ShadowRes + uint(Texel.x)];
        LitAccum += (ReceiverDepth <= StoredDepth + 48u) ? 1.0f : 0.0f;
    }
    return LitAccum * 0.20f;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ClearPreviewShadowMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }
    SolarShadowDepth[Tid.y * Push.Counts.x + Tid.x] = 0xFFFFFFFFu;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ProjectPreviewShadowMain(uint3 Tid : SV_DispatchThreadID)
{
    uint TriIdx = Tid.x;
    if (TriIdx >= Push.Counts.z)
    {
        return;
    }

    uint   ShadowRes    = Push.Counts.x;
    float3 ShadowOrig   = ClusterSceneHeader[6].xyz;
    float  InvHalfSpan  = ClusterSceneHeader[6].w;
    float3 ShadowRight  = ClusterSceneHeader[7].xyz;
    float  InvDepthSpan = ClusterSceneHeader[7].w;
    float3 ShadowUp     = ClusterSceneHeader[8].xyz;
    float3 LightFwd     = ClusterSceneHeader[9].xyz;

    uint   TriBase = TriIdx * kTriangleStride;
    float3 P0      = TriangleGeometry[TriBase + 0u].xyz - ShadowOrig;
    float3 P1      = TriangleGeometry[TriBase + 1u].xyz - ShadowOrig;
    float3 P2      = TriangleGeometry[TriBase + 2u].xyz - ShadowOrig;

    float2 S0 = float2(0.5f + 0.5f * dot(P0, ShadowRight) * InvHalfSpan,
                       0.5f - 0.5f * dot(P0, ShadowUp) * InvHalfSpan) * float(ShadowRes);
    float2 S1 = float2(0.5f + 0.5f * dot(P1, ShadowRight) * InvHalfSpan,
                       0.5f - 0.5f * dot(P1, ShadowUp) * InvHalfSpan) * float(ShadowRes);
    float2 S2 = float2(0.5f + 0.5f * dot(P2, ShadowRight) * InvHalfSpan,
                       0.5f - 0.5f * dot(P2, ShadowUp) * InvHalfSpan) * float(ShadowRes);

    float Z0 = dot(P0, LightFwd) * InvDepthSpan;
    float Z1 = dot(P1, LightFwd) * InvDepthSpan;
    float Z2 = dot(P2, LightFwd) * InvDepthSpan;

    int MinX = max(0, int(floor(min(S0.x, min(S1.x, S2.x)))));
    int MaxX = min(int(ShadowRes) - 1, int(ceil(max(S0.x, max(S1.x, S2.x)))));
    int MinY = max(0, int(floor(min(S0.y, min(S1.y, S2.y)))));
    int MaxY = min(int(ShadowRes) - 1, int(ceil(max(S0.y, max(S1.y, S2.y)))));
    if (MinX > MaxX || MinY > MaxY)
    {
        return;
    }

    float Area = (S1.x - S0.x) * (S2.y - S0.y) - (S1.y - S0.y) * (S2.x - S0.x);
    if (abs(Area) <= 1.0e-5f)
    {
        return;
    }
    float InvArea = 1.0f / Area;

    for (int Py = MinY; Py <= MaxY; ++Py)
    {
        for (int Px = MinX; Px <= MaxX; ++Px)
        {
            float2 SamplePt = float2(float(Px) + 0.5f, float(Py) + 0.5f);
            float  W0       = ((S1.x - SamplePt.x) * (S2.y - SamplePt.y) - (S1.y - SamplePt.y) * (S2.x - SamplePt.x)) * InvArea;
            float  W1       = ((S2.x - SamplePt.x) * (S0.y - SamplePt.y) - (S2.y - SamplePt.y) * (S0.x - SamplePt.x)) * InvArea;
            float  W2       = 1.0f - W0 - W1;
            if (W0 >= -1.0e-4f && W1 >= -1.0e-4f && W2 >= -1.0e-4f)
            {
                float Depth01 = W0 * Z0 + W1 * Z1 + W2 * Z2;
                if (Depth01 > 0.0f && Depth01 < 1.0f)
                {
                    uint Quantized = uint(Depth01 * float(kShadowDepthMaxInt));
                    InterlockedMin(SolarShadowDepth[uint(Py) * ShadowRes + uint(Px)], Quantized);
                }
            }
        }
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void BinPreviewTilesMain(uint3 Tid : SV_DispatchThreadID)
{
    uint TilesX = (Push.Counts.x + kTileSizePx - 1u) / kTileSizePx;
    uint TilesY = (Push.Counts.y + kTileSizePx - 1u) / kTileSizePx;
    if (Tid.x >= TilesX || Tid.y >= TilesY)
    {
        return;
    }

    float TileMinX = float(Tid.x * kTileSizePx);
    float TileMinY = float(Tid.y * kTileSizePx);
    float TileMaxX = TileMinX + float(kTileSizePx);
    float TileMaxY = TileMinY + float(kTileSizePx);

    uint BaseIdx        = (Tid.y * TilesX + Tid.x) * kTileSlotStride;
    uint MatchedCount   = 0u;
    uint ActiveClusters = Push.Counts.z;

    for (uint ClIdx = 0u; ClIdx < ActiveClusters; ++ClIdx)
    {
        float4 Bounds = ClusterSceneHeader[kClusterBaseSlot + ClIdx * kClusterStride + 0u];
        if (Bounds.z < TileMinX || Bounds.x > TileMaxX || Bounds.w < TileMinY || Bounds.y > TileMaxY)
        {
            continue;
        }
        if (MatchedCount + 1u < kTileSlotStride)
        {
            ++MatchedCount;
            TileClusterBins[BaseIdx + MatchedCount] = ClIdx;
        }
    }
    TileClusterBins[BaseIdx] = MatchedCount;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ResolvePreviewViewMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    float3 CamOrigin  = ClusterSceneHeader[0].xyz;
    float  TanHalfV   = ClusterSceneHeader[0].w;
    float3 CamRight   = ClusterSceneHeader[1].xyz;
    float  TanHalfH   = ClusterSceneHeader[1].w;
    float3 CamUp      = ClusterSceneHeader[2].xyz;
    float  ErrRefPx   = max(ClusterSceneHeader[2].w, 0.1f);
    float3 CamFwd     = ClusterSceneHeader[3].xyz;
    float  MaxTier    = max(ClusterSceneHeader[3].w, 1.0f);
    float3 ZenithRgb  = ClusterSceneHeader[4].xyz;
    uint   ShadowMode = uint(ClusterSceneHeader[4].w + 0.5f);
    float3 HorizRgb   = ClusterSceneHeader[5].xyz;

    float2 PixelCenter = float2(Tid.xy) + 0.5f;
    float  NdcX        = (PixelCenter.x / float(Push.Counts.x)) * 2.0f - 1.0f;
    float  NdcY        = 1.0f - (PixelCenter.y / float(Push.Counts.y)) * 2.0f;
    float3 RayDir      = SafeNormalize(CamFwd + CamRight * (NdcX * TanHalfH) + CamUp * (NdcY * TanHalfV));
    float3 SunDir      = SafeNormalize(Push.Tuning.xyz);
    uint   ViewMode    = Push.Counts.w;

    uint TilesX       = (Push.Counts.x + kTileSizePx - 1u) / kTileSizePx;
    uint TileX        = Tid.x / kTileSizePx;
    uint TileY        = Tid.y / kTileSizePx;
    uint TileBase     = (TileY * TilesX + TileX) * kTileSlotStride;
    uint TileClusters = TileClusterBins[TileBase];

    float  OpaqueT          = 1.0e30f;
    uint   OpaqueClusterIdx = 0u;
    float3 OpaqueNormal     = float3(0.0f, 1.0f, 0.0f);
    float2 OpaqueUv         = float2(0.0f);
    float  OpaqueEdgePx     = 1.0e6f;

    float  GlassFrontT      = 1.0e30f;
    float  GlassBackT       = 0.0f;
    uint   GlassLayerCount  = 0u;
    uint   GlassClusterIdx  = 0u;
    float3 GlassFrontNormal = float3(0.0f, 1.0f, 0.0f);
    float  GlassEdgePx      = 1.0e6f;
    float  GlassAbsorbRate  = 0.12f;

    for (uint Slot = 0u; Slot < TileClusters; ++Slot)
    {
        uint   ClIdx    = TileClusterBins[TileBase + 1u + Slot];
        uint   ClBase   = kClusterBaseSlot + ClIdx * kClusterStride;
        float4 Meta1    = ClusterSceneHeader[ClBase + 1u];
        float4 Meta3    = ClusterSceneHeader[ClBase + 3u];
        uint   TriStart = uint(Meta1.x + 0.5f);
        uint   TriCount = uint(Meta1.y + 0.5f);
        uint   Flags    = uint(Meta3.z + 0.5f);
        bool   IsGlass  = (Flags & 1u) != 0u;

        for (uint LocalTri = 0u; LocalTri < TriCount; ++LocalTri)
        {
            uint   TriBase = (TriStart + LocalTri) * kTriangleStride;
            float4 T0      = TriangleGeometry[TriBase + 0u];
            float4 T1      = TriangleGeometry[TriBase + 1u];
            float4 T2      = TriangleGeometry[TriBase + 2u];
            float4 T3      = TriangleGeometry[TriBase + 3u];

            float3 P0   = T0.xyz;
            float3 P1   = T1.xyz;
            float3 P2   = T2.xyz;
            float3 E1   = P1 - P0;
            float3 E2   = P2 - P0;
            float3 Pvec = cross(RayDir, E2);
            float  Det  = dot(E1, Pvec);
            if (abs(Det) <= 1.0e-9f)
            {
                continue;
            }
            float  InvDet = 1.0f / Det;
            float3 Tvec   = CamOrigin - P0;
            float  BaryU  = dot(Tvec, Pvec) * InvDet;
            if (BaryU < 0.0f || BaryU > 1.0f)
            {
                continue;
            }
            float3 Qvec  = cross(Tvec, E1);
            float  BaryV = dot(RayDir, Qvec) * InvDet;
            if (BaryV < 0.0f || (BaryU + BaryV) > 1.0f)
            {
                continue;
            }
            float HitT = dot(E2, Qvec) * InvDet;
            if (HitT <= 0.04f)
            {
                continue;
            }

            float  BaryW     = 1.0f - BaryU - BaryV;
            float  PixelSize = (2.0f * TanHalfV * HitT) / max(float(Push.Counts.y), 1.0f);
            float  Area2     = length(cross(E1, E2));
            float  H0        = Area2 / max(length(P2 - P1), 1.0e-5f);
            float  H1        = Area2 / max(length(E2), 1.0e-5f);
            float  H2        = Area2 / max(length(E1), 1.0e-5f);
            float  EdgeDistM = min(BaryW * H0, min(BaryU * H1, BaryV * H2));
            float  EdgePx    = EdgeDistM / max(PixelSize, 1.0e-5f);
            float3 GeoNorm   = SafeNormalize(cross(E1, E2));
            if (dot(GeoNorm, RayDir) > 0.0f)
            {
                GeoNorm = -GeoNorm;
            }

            if (IsGlass)
            {
                ++GlassLayerCount;
                if (HitT < GlassFrontT)
                {
                    GlassBackT       = min(GlassFrontT, 1.0e20f);
                    GlassFrontT      = HitT;
                    GlassClusterIdx  = ClIdx;
                    GlassFrontNormal = GeoNorm;
                    GlassEdgePx      = EdgePx;
                    GlassAbsorbRate  = T3.w;
                }
                else if (HitT > GlassFrontT + 0.005f)
                {
                    GlassBackT = max(GlassBackT, HitT);
                }
            }
            else if (HitT < OpaqueT)
            {
                OpaqueT          = HitT;
                OpaqueClusterIdx = ClIdx;
                OpaqueNormal     = GeoNorm;
                OpaqueEdgePx     = EdgePx;
                float2 Uv0       = float2(T0.w, T1.w);
                float2 Uv1       = float2(T2.w, T3.x);
                float2 Uv2       = float2(T3.y, T3.z);
                OpaqueUv         = Uv0 * BaryW + Uv1 * BaryU + Uv2 * BaryV;
            }
        }
    }

    bool HasOpaque = (OpaqueT < 1.0e25f);
    bool HasGlass  = (GlassFrontT < OpaqueT);

    float3 SkyRadiance = EvaluateSimplePreviewSky(RayDir, SunDir, ZenithRgb, HorizRgb);
    if (!HasOpaque && !HasGlass)
    {
        if (ViewMode == 2u)
        {
            SkyRadiance = float3(0.032f, 0.038f, 0.048f);
        }
        PreviewTarget[uint3(Tid.xy, 0u)] = float4(SkyRadiance, 1.0f);
        return;
    }

    float3 ResolvedColor = SkyRadiance;

    if (HasOpaque)
    {
        uint   ClBase    = kClusterBaseSlot + OpaqueClusterIdx * kClusterStride;
        float4 Meta1     = ClusterSceneHeader[ClBase + 1u];
        float4 Meta2     = ClusterSceneHeader[ClBase + 2u];
        float4 Meta3     = ClusterSceneHeader[ClBase + 3u];
        uint   GlobalId  = uint(Meta1.z + 0.5f);
        float  TierIndex = Meta1.w;
        float3 Albedo    = Meta2.xyz;
        float  Roughness = clamp(Meta2.w, 0.04f, 1.0f);
        float  Metallic  = saturate(Meta3.x);
        float  Clearcoat = saturate(Meta3.y);
        uint   Flags     = uint(Meta3.z + 0.5f);
        uint   MatTier   = (Flags >> 1u) & 3u;
        float  ProjErrPx = Meta3.w;
        float3 HitPos    = CamOrigin + RayDir * OpaqueT;

        if (MatTier == 0u)
        {
            float2 GridCell = frac(OpaqueUv * 4.0f);
            float  Micro    = 0.94f + 0.10f * smoothstep(0.04f, 0.12f, min(GridCell.x, GridCell.y));
            Albedo *= Micro;
        }

        float3 VDir  = -RayDir;
        float3 HVec  = SafeNormalize(SunDir + VDir);
        float  NdotL = max(0.0f, dot(OpaqueNormal, SunDir));
        float  NdotV = max(0.02f, dot(OpaqueNormal, VDir));
        float  NdotH = max(0.0f, dot(OpaqueNormal, HVec));

        // Modular Fast Preview Shadow Evaluation
        float DirectShadow = 1.0f;
        float AmbientAo    = 1.0f;
        if (ShadowMode == 1u) // ContactEllipsoid
        {
            if (HitPos.y < 0.08f)
            {
                float2 Ellip = float2(HitPos.x / 1.32f, HitPos.z / 3.15f);
                float  Rad2  = dot(Ellip, Ellip);
                DirectShadow = lerp(0.18f, 1.0f, smoothstep(0.42f, 1.32f, Rad2));
                AmbientAo    = lerp(0.42f, 1.0f, smoothstep(0.35f, 1.25f, Rad2));
            }
        }
        else if (ShadowMode == 2u) // OrthoClusterDepth (1024x1024 directional cluster shadow + 5-tap PCF)
        {
            if (NdotL > 0.0f)
            {
                DirectShadow = SampleOrthoClusterShadow(HitPos, OpaqueNormal, SunDir);
            }
            if (HitPos.y < 0.08f)
            {
                float2 Ellip = float2(HitPos.x / 1.18f, HitPos.z / 2.85f);
                AmbientAo    = lerp(0.55f, 1.0f, smoothstep(0.30f, 1.15f, dot(Ellip, Ellip)));
            }
        }
        else if (ShadowMode == 3u) // SignedDistanceCone (.sdfspace sphere-traced solar cone shadow)
        {
            if (NdotL > 0.0f)
            {
                DirectShadow = TraceGpuVolumeConeShadow(DistanceVolumeTable, HitPos, OpaqueNormal, SunDir, 10.0f, 9.5f, 16);
            }
            AmbientAo = EvaluateGpuVolumeAmbientOcclusion(DistanceVolumeTable, HitPos, OpaqueNormal, 0.85f);
        }
        else if (ShadowMode == 4u) // HybridOrthoAndDistance
        {
            if (NdotL > 0.0f)
            {
                float OrthoS = SampleOrthoClusterShadow(HitPos, OpaqueNormal, SunDir);
                float SdfS   = TraceGpuVolumeConeShadow(DistanceVolumeTable, HitPos, OpaqueNormal, SunDir, 8.0f, 16.0f, 10);
                DirectShadow = min(OrthoS, lerp(0.25f, 1.0f, SdfS));
            }
            AmbientAo = EvaluateGpuVolumeAmbientOcclusion(DistanceVolumeTable, HitPos, OpaqueNormal, 0.85f);
        }

        float  Alpha    = Roughness * Roughness;
        float  AlphaSq  = Alpha * Alpha;
        float  DenomD   = NdotH * NdotH * (AlphaSq - 1.0f) + 1.0f;
        float  DistGGX  = AlphaSq / max(π * DenomD * DenomD, 1.0e-5f);
        float3 F0       = lerp(float3(0.04f), Albedo, Metallic);
        float3 Fresnel  = F0 + (1.0f - F0) * pow(1.0f - max(0.0f, dot(HVec, VDir)), 5.0f);
        float3 Diffuse  = Albedo * (1.0f - Metallic) * (1.0f / π);
        float3 Specular = Fresnel * (DistGGX * 0.25f / max(NdotV, 0.08f));

        if (Clearcoat > 0.01f && MatTier < 2u)
        {
            float CoatAlphaSq = 0.0036f;
            float CoatDen     = NdotH * NdotH * (CoatAlphaSq - 1.0f) + 1.0f;
            float CoatD       = CoatAlphaSq / max(π * CoatDen * CoatDen, 1.0e-5f);
            Specular         += float3(0.04f) * (Clearcoat * CoatD * 0.18f);
        }

        float3 SkyHemi    = lerp(float3(0.10f, 0.11f, 0.13f), ZenithRgb * 0.85f + HorizRgb * 0.45f, 0.5f + 0.5f * OpaqueNormal.y);
        float3 EnvRefl    = EvaluateSimplePreviewSky(reflect(RayDir, OpaqueNormal), SunDir, ZenithRgb, HorizRgb);
        float3 LitSurface = (Diffuse + Specular) * float3(2.95f, 2.80f, 2.62f) * NdotL * DirectShadow +
                            Albedo * (1.0f - 0.65f * Metallic) * SkyHemi * AmbientAo +
                            EnvRefl * Fresnel * (1.0f - 0.72f * Roughness) * (0.35f + 0.45f * Clearcoat) * lerp(0.55f, 1.0f, AmbientAo);

        float WireMask = 1.0f - smoothstep(0.45f, 1.25f, OpaqueEdgePx);

        if (ViewMode == 0u) // ShadedPreview
        {
            ResolvedColor = LitSurface;
        }
        else if (ViewMode == 1u) // ShadedWireframe
        {
            float3 EdgeTint = lerp(float3(0.86f, 0.92f, 1.00f), EvaluateHeatGradient(TierIndex / MaxTier), 0.45f);
            ResolvedColor   = lerp(LitSurface, EdgeTint, WireMask * 0.82f);
        }
        else if (ViewMode == 2u) // WireframeTopology
        {
            float3 BaseSlate = float3(0.055f, 0.065f, 0.082f) * (0.55f + 0.45f * NdotL);
            float3 WireColor = lerp(float3(0.38f, 0.72f, 0.98f), float3(0.95f, 0.72f, 0.28f), saturate(TierIndex / MaxTier));
            ResolvedColor    = lerp(BaseSlate, WireColor, WireMask);
        }
        else if (ViewMode == 3u) // ClusterIdentity
        {
            float3 ClusterCol = HashClusterColor(GlobalId) * (0.38f + 0.62f * (0.4f + 0.6f * NdotL));
            ResolvedColor     = lerp(ClusterCol, ClusterCol * 0.25f, WireMask * 0.55f);
        }
        else if (ViewMode == 4u) // RefinementTierHeat
        {
            float3 Heat   = EvaluateHeatGradient(1.0f - saturate(TierIndex / MaxTier));
            ResolvedColor = Heat * (0.42f + 0.58f * (0.35f + 0.65f * NdotL));
        }
        else if (ViewMode == 5u) // ProjectedErrorHeat
        {
            float3 Heat   = EvaluateHeatGradient(saturate(ProjErrPx / (ErrRefPx * 1.5f)));
            ResolvedColor = Heat * (0.42f + 0.58f * (0.35f + 0.65f * NdotL));
        }
        else if (ViewMode == 6u) // MaterialTierHeat
        {
            float3 MatCol = (MatTier == 0u) ? float3(0.20f, 0.74f, 0.96f)
                          : ((MatTier == 1u) ? float3(0.94f, 0.68f, 0.22f) : float3(0.56f, 0.42f, 0.84f));
            ResolvedColor = MatCol * (0.40f + 0.60f * (0.35f + 0.65f * NdotL));
        }
        else if (ViewMode == 7u) // SurfaceNormals
        {
            ResolvedColor = 0.5f + 0.5f * OpaqueNormal;
        }
        else if (ViewMode == 8u) // TextureCoordinates
        {
            int2   Cell    = int2(floor(OpaqueUv * 4.0f));
            float  Checker = float((Cell.x + Cell.y) & 1);
            float3 UvTint  = float3(frac(OpaqueUv.x), frac(OpaqueUv.y), 0.35f + 0.45f * Checker);
            ResolvedColor  = UvTint * (0.45f + 0.55f * (0.35f + 0.65f * NdotL));
        }
        else // ViewMode == 9u: GlassLayerPeel
        {
            ResolvedColor = LitSurface * 0.78f;
        }
    }

    if (HasGlass)
    {
        uint   GClBase   = kClusterBaseSlot + GlassClusterIdx * kClusterStride;
        float4 GMeta2    = ClusterSceneHeader[GClBase + 2u];
        float4 GMeta3    = ClusterSceneHeader[GClBase + 3u];
        float  Ior       = max(1.02f, GMeta3.y);
        float  CosThetaI = saturate(dot(-RayDir, GlassFrontNormal));
        float  F0Scalar  = pow((Ior - 1.0f) / (Ior + 1.0f), 2.0f);
        float  FresnelG  = F0Scalar + (1.0f - F0Scalar) * pow(1.0f - CosThetaI, 5.0f);

        float  ExitT     = (GlassBackT > GlassFrontT && GlassBackT < OpaqueT) ? GlassBackT : (GlassFrontT + 0.018f);
        float  Thickness = clamp(ExitT - GlassFrontT, 0.008f, 0.45f);
        float3 BeerTr    = exp(-float3(0.85f, 0.24f, 0.30f) * (GlassAbsorbRate * 6.0f * Thickness)) * GMeta2.xyz;
        float3 GlassRefl = EvaluateSimplePreviewSky(reflect(RayDir, GlassFrontNormal), SunDir, ZenithRgb, HorizRgb);
        float  GlassWire = 1.0f - smoothstep(0.45f, 1.25f, GlassEdgePx);

        if (ViewMode == 3u)
        {
            ResolvedColor = lerp(ResolvedColor * float3(0.72f, 0.94f, 1.0f), float3(0.24f, 0.86f, 0.98f), 0.34f + 0.45f * GlassWire);
        }
        else if (ViewMode == 9u)
        {
            float3 PeelTint = (GlassLayerCount >= 2u) ? float3(0.96f, 0.76f, 0.24f) : float3(0.22f, 0.82f, 0.96f);
            ResolvedColor   = lerp(ResolvedColor, PeelTint, 0.46f + 0.35f * GlassWire);
        }
        else if (ViewMode == 2u)
        {
            ResolvedColor = lerp(ResolvedColor, float3(0.32f, 0.90f, 0.98f), max(0.18f, GlassWire));
        }
        else
        {
            ResolvedColor = ResolvedColor * BeerTr * (1.0f - FresnelG) + GlassRefl * FresnelG;
            if (ViewMode == 1u)
            {
                ResolvedColor = lerp(ResolvedColor, float3(0.52f, 0.92f, 1.00f), GlassWire * 0.72f);
            }
        }
    }

    PreviewTarget[uint3(Tid.xy, 0u)] = float4(max(ResolvedColor, float3(0.0f)), 1.0f);
}
