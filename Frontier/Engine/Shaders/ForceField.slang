//==========================================================================================================================================
//                                                      FORCE FIELD SLANG
//==========================================================================================================================================
// Zero-noise analytic ForceField evaluation safe inside volumetric raymarching:
// 1. Ekman altitude-sheared planetary wind + coherent gust wave
// 2. Curved-spine Burgers-Rott VortexColumn (tornado helical domain twist + cyclostrophic Δp condensation funnel)
// 3. Surface saltation dust/sand/debris skirt (tornado touch-down debris sheath + haboob traveling gust front)

static const uint kForceSourceStride = 4u;

uint ForceSourceSlotOffset(StructuredBuffer<float4> Ensemble, uint GlobalForceIdx)
{
    uint TableOffset = uint(Ensemble[9].z + 0.5f);
    return TableOffset + GlobalForceIdx * kForceSourceStride;
}

// Evaluates Ekman sheared wind displacement + coherent tornado helical domain twist & cyclostrophic funnel density.
// Zero 3D noise fetches inside this helper; caller supplies existing rotated-FBM/Worley cellular fields.
void EvaluateForceFieldCloudDomain(StructuredBuffer<float4> Ensemble,
                                   uint                     RealmId,
                                   float3                   PointMeters,
                                   inout float3             InOutAdvectedMeters,
                                   out float                OutFunnelCondensation,
                                   out float                OutWallCollarBoost)
{
    OutFunnelCondensation = 0.0f;
    OutWallCollarBoost    = 0.0f;

    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R16         = Ensemble[RealmOffset + 16u]; // BearingRad, SpeedMps, ShearPerKm, VeerRadPerKm
    float4 R17         = Ensemble[RealmOffset + 17u]; // ForceFirst, ForceCount, GustAmplitude, WindTimeS

    // Ekman vertical wind turning & speed shear when SpeedMps > 0
    if (R16.y > 1.0e-3f && R17.w != 0.0f)
    {
        float GroundY    = Ensemble[RealmOffset + 4u].y;
        float AltKm      = max(0.0f, (PointMeters.y - GroundY) * 1.0e-3f);
        float Bearing    = R16.x + R16.w * AltKm;
        float2 WindDirXZ = float2(cos(Bearing), sin(Bearing));
        float ShearGain  = max(0.2f, 1.0f + R16.z * min(AltKm, 12.0f));
        float GustPhase  = 0.52f * R17.w - 0.008f * dot(PointMeters.xz, WindDirXZ);
        float GustFactor = 1.0f + R17.z * (0.65f * sin(GustPhase) + 0.35f * sin(2.13f * GustPhase + 1.1f));
        float2 DriftXZ   = WindDirXZ * (R16.y * ShearGain * GustFactor * R17.w);
        InOutAdvectedMeters.xz -= DriftXZ;
    }

    uint ForceFirst = uint(R17.x + 0.5f);
    uint ForceCount = uint(R17.y + 0.5f);
    if (ForceCount == 0u)
    {
        return;
    }

    for (uint Idx = 0u; Idx < ForceCount; ++Idx)
    {
        uint   FOffset = ForceSourceSlotOffset(Ensemble, ForceFirst + Idx);
        float4 F0      = Ensemble[FOffset + 0u];
        uint   Kind    = uint(F0.w + 0.5f);
        if (Kind == 4u) // LocalizedGustPuff scud/cloud shear
        {
            float4 F1  = Ensemble[FOffset + 1u];
            float4 F2  = Ensemble[FOffset + 2u];
            float4 F3  = Ensemble[FOffset + 3u];
            float  Rxz = max(1.5f, F1.x);
            float  Ry  = max(1.0f, F1.y);
            float3 D   = float3((PointMeters.x - F0.x) / Rxz, (PointMeters.y - F0.y) / Ry, (PointMeters.z - F0.z) / Rxz);
            float  D2  = dot(D, D);
            if (D2 < 9.0f)
            {
                float Gauss = exp(-0.5f * D2);
                InOutAdvectedMeters -= float3(F1.z, F2.x, F1.w) * (Gauss * min(F3.w, 6.0f) * 0.45f);
            }
            continue;
        }
        if (Kind == 6u) // ThermalUpdraftPlume localized cumulus turret boost
        {
            float4 F1   = Ensemble[FOffset + 1u];
            float4 F3   = Ensemble[FOffset + 3u];
            float  RelY = PointMeters.y - F0.y;
            float  ColH = max(F1.x, 15.0f);
            if (RelY >= 0.0f && RelY <= ColH * 1.45f)
            {
                float HNorm  = saturate(RelY / ColH);
                float PlumeR = max(2.0f, F1.y * (1.0f + 0.45f * HNorm));
                float ρNorm  = length(PointMeters.xz - F0.xz) / PlumeR;
                if (ρNorm < 3.5f)
                {
                    float CoreEnv = exp(-0.5f * ρNorm * ρNorm) * (1.0f - smoothstep(1.0f, 1.40f, RelY / ColH));
                    InOutAdvectedMeters.y -= F1.z * CoreEnv * 0.8f;
                    OutWallCollarBoost = max(OutWallCollarBoost, F3.x * CoreEnv * smoothstep(0.55f, 0.95f, HNorm));
                }
            }
            continue;
        }
        if (Kind != 1u)
        {
            continue;
        }

        float4 F1    = Ensemble[FOffset + 1u]; // ColumnHeightM, GroundCoreR, CollarCoreR, SpineLeanX
        float4 F2    = Ensemble[FOffset + 2u]; // PeakTangMps, RadialInflowMps, UpdraftMps, SpineLeanZ
        float4 F3    = Ensemble[FOffset + 3u]; // EnvCondensation, DebrisSkirtH, EnvDebrisExt, SwirlPhaseRad

        float RelY  = PointMeters.y - F0.y;
        float SpanH = max(F1.x, 10.0f);
        if (RelY < 0.0f || RelY > SpanH * 1.45f)
        {
            continue;
        }

        float  HNorm   = saturate(RelY / SpanH);
        float  TopFade = 1.0f - smoothstep(SpanH, SpanH * 1.42f, RelY);
        float2 SpineXZ = F0.xz + float2(F1.w, F2.w) * (HNorm * HNorm);
        float2 RelXZ   = PointMeters.xz - SpineXZ;
        float  Radius  = length(RelXZ);
        float  CoreR   = max(1.0f, lerp(F1.y, F1.z, smoothstep(0.0f, 1.0f, HNorm)));
        float  ρNorm   = Radius / CoreR;

        if (ρNorm < 9.5f)
        {
            // Burgers-Rott azimuthal twist around curved vortex spine: tightest spin at core, decaying smoothly outward
            float TwistAngle = (F3.w + 3.4f * HNorm) / (1.0f + 0.24f * ρNorm * ρNorm);
            float cosθ       = cos(TwistAngle);
            float sinθ       = sin(TwistAngle);
            float2 TwistedXZ = float2(RelXZ.x * cosθ - RelXZ.y * sinθ, RelXZ.x * sinθ + RelXZ.y * cosθ);
            InOutAdvectedMeters.xz = SpineXZ + TwistedXZ;
            InOutAdvectedMeters.y -= 18.0f * HNorm / (1.0f + 0.5f * ρNorm * ρNorm);

            // Rotating mesocyclone wall-cloud lowering boost around the upper collar
            OutWallCollarBoost = max(OutWallCollarBoost,
                                     F3.x * TopFade * smoothstep(0.55f, 0.98f, HNorm) * exp(-0.14f * ρNorm * ρNorm));
        }

        if (F3.x > 1.0e-4f && ρNorm < 2.35f && RelY <= SpanH * 1.18f)
        {
            // Cyclostrophic pressure-drop condensation funnel with helical cellular striations
            float3 HelicalCoord = float3(InOutAdvectedMeters.x * 0.042f,
                                         PointMeters.y * 0.024f - F3.w * 0.35f,
                                         InOutAdvectedMeters.z * 0.042f);
            float  Striation    = 0.62f + 0.38f * FbmRotated3D(HelicalCoord, 3, 709u);
            float  SheathWall   = exp(-0.92f * ρNorm * ρNorm);
            float  GroundTouch  = smoothstep(0.0f, 0.025f * SpanH, RelY);
            float  Funnel       = F3.x * TopFade * GroundTouch * SheathWall * Striation;
            OutFunnelCondensation = max(OutFunnelCondensation, Funnel);
        }
    }
}

// Computes ray interval [OutVortexNearT, OutVortexFarT] through any active tornado bounding cylinder so
// VolumetricClouds marches the sub-cloud condensation funnel only on pixels whose rays actually cross a vortex.
bool IntersectActiveVortexColumns(StructuredBuffer<float4> Ensemble,
                                  uint                     RealmId,
                                  float3                   RayOrigin,
                                  float3                   RayDir,
                                  float                    MaxDistM,
                                  out float                OutNearT,
                                  out float                OutFarT)
{
    OutNearT = 1.0e30f;
    OutFarT  = 0.0f;

    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R17         = Ensemble[RealmOffset + 17u];
    uint   ForceFirst  = uint(R17.x + 0.5f);
    uint   ForceCount  = uint(R17.y + 0.5f);
    if (ForceCount == 0u)
    {
        return false;
    }

    bool HitAny = false;
    for (uint Idx = 0u; Idx < ForceCount; ++Idx)
    {
        uint   FOffset = ForceSourceSlotOffset(Ensemble, ForceFirst + Idx);
        float4 F0      = Ensemble[FOffset + 0u];
        if (uint(F0.w + 0.5f) != 1u)
        {
            continue;
        }
        float4 F1 = Ensemble[FOffset + 1u];
        float4 F2 = Ensemble[FOffset + 2u];
        float4 F3 = Ensemble[FOffset + 3u];
        if (F3.x <= 1.0e-4f)
        {
            continue;
        }

        float2 MidCenterXZ = F0.xz + 0.5f * float2(F1.w, F2.w);
        float  BoundRadM   = max(F1.y, F1.z) * 2.6f + 0.6f * length(float2(F1.w, F2.w));
        float3 BoxMin      = float3(MidCenterXZ.x - BoundRadM, F0.y, MidCenterXZ.y - BoundRadM);
        float3 BoxMax      = float3(MidCenterXZ.x + BoundRadM, F0.y + F1.x * 1.18f, MidCenterXZ.y + BoundRadM);

        float3 InvDir = 1.0f / select(abs(RayDir) > 1.0e-6f, RayDir, float3(1.0e-6f));
        float3 T0v    = (BoxMin - RayOrigin) * InvDir;
        float3 T1v    = (BoxMax - RayOrigin) * InvDir;
        float3 TMin   = min(T0v, T1v);
        float3 TMax   = max(T0v, T1v);
        float  NearC  = max(0.0f, max(TMin.x, max(TMin.y, TMin.z)));
        float  FarC   = min(MaxDistM, min(TMax.x, min(TMax.y, TMax.z)));
        if (FarC > NearC)
        {
            OutNearT = min(OutNearT, NearC);
            OutFarT  = max(OutFarT, FarC);
            HitAny   = true;
        }
    }
    return HitAny;
}

// Evaluates lofted ground dust, sand, and swirling tornado debris extinction inside FogSpace.
float EvaluateForceFieldParticulateExtinction(StructuredBuffer<float4> Ensemble,
                                              uint                     RealmId,
                                              float3                   PointMeters,
                                              float                    TimeSeconds,
                                              out float3               OutTintAlbedo,
                                              out float                OutPhaseG)
{
    OutTintAlbedo = float3(0.54f, 0.44f, 0.32f);
    OutPhaseG     = 0.42f;

    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R17         = Ensemble[RealmOffset + 17u];
    uint   ForceFirst  = uint(R17.x + 0.5f);
    uint   ForceCount  = uint(R17.y + 0.5f);
    if (ForceCount == 0u)
    {
        return 0.0f;
    }

    float  Totalσ      = 0.0f;
    float3 WeightedCol = float3(0.0f);

    for (uint Idx = 0u; Idx < ForceCount; ++Idx)
    {
        uint   FOffset = ForceSourceSlotOffset(Ensemble, ForceFirst + Idx);
        float4 F0      = Ensemble[FOffset + 0u];
        float4 F1      = Ensemble[FOffset + 1u];
        float4 F2      = Ensemble[FOffset + 2u];
        float4 F3      = Ensemble[FOffset + 3u];
        uint   Kind    = uint(F0.w + 0.5f);
        if (F3.z <= 1.0e-6f)
        {
            continue;
        }

        if (Kind == 1u) // Tornado / Dust-Devil swirling ground debris skirt
        {
            float RelY   = PointMeters.y - F0.y;
            float SkirtH = max(F3.y, 4.0f);
            if (RelY >= 0.0f && RelY <= SkirtH)
            {
                float  HNorm  = saturate(RelY / max(F1.x, 10.0f));
                float2 Spine  = F0.xz + float2(F1.w, F2.w) * (HNorm * HNorm);
                float2 RelXZ  = PointMeters.xz - Spine;
                float  CoreR  = max(1.0f, F1.y * (1.0f + 0.85f * saturate(RelY / SkirtH)));
                float  ρNorm  = length(RelXZ) / CoreR;
                if (ρNorm < 4.2f)
                {
                    float  Angle   = F3.w * 1.15f / (1.0f + 0.2f * ρNorm * ρNorm);
                    float2 SpinXZ  = float2(RelXZ.x * cos(Angle) - RelXZ.y * sin(Angle),
                                            RelXZ.x * sin(Angle) + RelXZ.y * cos(Angle));
                    float  Billow  = 0.55f + 0.65f * FbmRotated3D(float3(SpinXZ * 0.075f, RelY * 0.06f), 3, 887u);
                    float  Sheath  = exp(-0.42f * (ρNorm - 0.90f) * (ρNorm - 0.90f));
                    float  VertEnv = 1.0f - smoothstep(0.15f * SkirtH, SkirtH, RelY);
                    float  σLocal  = F3.z * Sheath * VertEnv * Billow;
                    Totalσ      += σLocal;
                    WeightedCol += σLocal * float3(0.62f, 0.44f, 0.28f);
                }
            }
        }
        else if (Kind == 2u) // TravelingGustFront / Haboob rolling sand wall (3D bow-echo outflow lobe)
        {
            float  RelY        = PointMeters.y - F0.y;
            float  VertH       = max(F1.x, 5.0f);
            float  CrestD      = max(F1.y, 5.0f);
            float2 PropXZ      = F1.zw;
            float2 PerpXZ      = float2(-PropXZ.y, PropXZ.x);
            float  LateralDist = abs(dot(PointMeters.xz - F0.xz, PerpXZ));
            float  MaxSpan     = 2.6f * CrestD;
            // Slight bow-echo curvature along the gust front arc
            float  DistBehind  = dot(F0.xz - PointMeters.xz, PropXZ) + 0.14f * (LateralDist * LateralDist) / max(CrestD, 10.0f);
            if (RelY >= 0.0f && RelY <= VertH * 1.2f && LateralDist <= MaxSpan &&
                DistBehind >= -0.25f * CrestD && DistBehind <= 2.2f * CrestD)
            {
                float LateralEnv = 1.0f - smoothstep(1.2f * CrestD, MaxSpan, LateralDist);
                float Nose       = smoothstep(-0.25f * CrestD, 0.12f * CrestD, DistBehind);
                float Wake       = exp(-max(0.0f, DistBehind - 0.1f * CrestD) / CrestD);
                float VertEnv    = 1.0f - smoothstep(0.18f * VertH, VertH * 1.15f, RelY);
                float Lobes      = 0.50f + 0.70f * FbmRotated3D(PointMeters * 0.045f - float3(PropXZ.x, 0.15f, PropXZ.y) * TimeSeconds * 0.8f, 3, 953u);
                float σLocal     = F3.z * LateralEnv * Nose * Wake * VertEnv * Lobes;
                Totalσ      += σLocal;
                WeightedCol += σLocal * F2.yzw;
            }
        }
        else if (Kind == 3u) // RadialMicroburst surface dust ring
        {
            float RelY  = PointMeters.y - F0.y;
            float RingR = max(F1.z, 10.0f);
            float Rad   = length(PointMeters.xz - F0.xz);
            if (RelY >= 0.0f && RelY <= F3.y && Rad < RingR * 2.6f)
            {
                float Ring   = (Rad / RingR) * exp(-0.5f * (Rad * Rad) / (RingR * RingR));
                float σLocal = F3.z * Ring * exp(-RelY / max(F3.y * 0.45f, 4.0f));
                Totalσ      += σLocal;
                WeightedCol += σLocal * float3(F2.z, F2.w, F3.w);
            }
        }
        else if (Kind == 4u) // LocalizedGustPuff drifting dust/mist wisp
        {
            float  Rxz = max(1.5f, F1.x);
            float  Ry  = max(1.0f, F1.y);
            float3 D   = float3((PointMeters.x - F0.x) / Rxz, (PointMeters.y - F0.y) / Ry, (PointMeters.z - F0.z) / Rxz);
            float  D2  = dot(D, D);
            if (D2 < 9.0f)
            {
                float Wisp   = 0.55f + 0.65f * FbmRotated3D(PointMeters * 0.08f, 3, 733u);
                float σLocal = F3.z * exp(-0.5f * D2) * Wisp;
                Totalσ      += σLocal;
                WeightedCol += σLocal * F2.yzw;
            }
        }
        else if (Kind == 5u) // AerodynamicWake vehicle/body rooster-tail dust & spray plume
        {
            float2 FwdXZ       = F1.xy;
            float2 RightXZ     = float2(FwdXZ.y, -FwdXZ.x);
            float2 RelXZ       = PointMeters.xz - F0.xz;
            float  RelY        = PointMeters.y - F0.y;
            float  TrailDist   = -dot(RelXZ, FwdXZ);
            float  LateralDist = dot(RelXZ, RightXZ);
            float  HalfW       = max(0.4f, F1.z);
            float  WakeLen     = max(4.0f, F1.w);
            float  WakeH       = max(0.8f, F2.x);
            if (TrailDist >= -0.5f * HalfW && TrailDist <= 2.2f * WakeLen && RelY >= 0.0f && RelY <= 2.4f * WakeH)
            {
                float Spread   = 1.0f + 0.85f * max(0.0f, TrailDist) / WakeLen;
                float LatNorm  = LateralDist / (HalfW * Spread);
                float LongEnv  = smoothstep(-0.5f * HalfW, 0.5f * HalfW, TrailDist) * exp(-max(0.0f, TrailDist) / WakeLen);
                float LatEnv   = exp(-0.5f * LatNorm * LatNorm);
                float VertEnv  = 1.0f - smoothstep(0.15f * WakeH * Spread, 1.8f * WakeH * Spread, RelY);
                float Billow   = 0.50f + 0.70f * FbmRotated3D(PointMeters * 0.22f, 3, 811u);
                float σLocal   = F3.z * LongEnv * LatEnv * VertEnv * Billow;
                Totalσ        += σLocal;
                WeightedCol   += σLocal * float3(F2.z, F2.w, F3.w);
            }
        }
        else if (Kind == 6u) // ThermalUpdraftPlume dust/ash column
        {
            float RelY = PointMeters.y - F0.y;
            float ColH = max(F1.x, 15.0f);
            if (RelY >= 0.0f && RelY <= ColH * 1.25f)
            {
                float HNorm  = saturate(RelY / ColH);
                float PlumeR = max(2.0f, F1.y * (1.0f + 0.45f * HNorm));
                float ρNorm  = length(PointMeters.xz - F0.xz) / PlumeR;
                if (ρNorm < 3.2f)
                {
                    float Billow = 0.55f + 0.65f * FbmRotated3D(float3(PointMeters.xz * 0.05f, RelY * 0.04f - TimeSeconds * 0.6f), 3, 919u);
                    float σLocal = F3.z * exp(-0.5f * ρNorm * ρNorm) * (1.0f - smoothstep(0.85f, 1.25f, RelY / ColH)) * Billow;
                    Totalσ      += σLocal;
                    WeightedCol += σLocal * F2.yzw;
                }
            }
        }
        else if (Kind == 7u) // DirectionalJetCorridor particulate stream
        {
            float3 Axis   = F1.xyz;
            float  LenM   = max(2.0f, F1.w);
            float  BaseR  = max(0.5f, F2.x);
            float3 Rel    = PointMeters - F0.xyz;
            float  AxialS = dot(Rel, Axis);
            if (AxialS >= -0.2f * BaseR && AxialS <= 1.25f * LenM)
            {
                float3 Perp   = Rel - Axis * AxialS;
                float  JetR   = BaseR * (1.0f + 0.35f * max(0.0f, AxialS) / LenM);
                float  RadSq  = dot(Perp, Perp);
                if (RadSq < 9.0f * JetR * JetR)
                {
                    float AxialEnv = smoothstep(-0.2f * BaseR, 0.2f * BaseR, AxialS) * (1.0f - smoothstep(0.85f * LenM, 1.25f * LenM, AxialS));
                    float σLocal   = F3.z * AxialEnv * exp(-0.5f * RadSq / (JetR * JetR));
                    Totalσ        += σLocal;
                    WeightedCol   += σLocal * float3(F2.z, F2.w, F3.w);
                }
            }
        }
        else if (Kind == 8u) // SphericalImpulseShell expanding dust/condensation shock shell
        {
            float RFront = F1.x;
            float Thick  = max(0.5f, F1.y);
            float Dist   = length(PointMeters - F0.xyz);
            float ShellD = (Dist - RFront) / Thick;
            if (abs(ShellD) < 3.0f)
            {
                float σLocal = F3.z * exp(-0.5f * ShellD * ShellD);
                Totalσ      += σLocal;
                WeightedCol += σLocal * F2.xyz;
            }
        }
    }

    if (Totalσ > 1.0e-7f)
    {
        OutTintAlbedo = WeightedCol / Totalσ;
    }
    return Totalσ;
}
