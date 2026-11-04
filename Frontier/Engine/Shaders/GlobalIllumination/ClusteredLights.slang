//============================================================================================================================================
//                                                         CLUSTEREDLIGHTS.SLANG
//============================================================================================================================================
// 🧩 3D Clustered Light Grid (16x16x4 cells with 64-bit per-cell light bitmasks) and photometric / IES evaluation for

#pragma once
#include "IntrinsicParity.slang"
//    omnidirectional point lights, roadway batwing street lamps, and automotive headlights / floodlights.

static const uint LightTypeOmni = 0u;
static const uint LightTypeStreetLamp = 1u;
static const uint LightTypeLowBeam = 2u;
static const uint LightTypeHighBeam = 3u;
static const uint LightTypeFog = 4u;
static const uint LightTypeFloodlight = 5u;

static const uint MaxClusterLights = 64u;

struct LuminaireProfile
{
    float4 PositionType;     // .xyz = Position, .w = LightType
    float4 DirectionReach;   // .xyz = Direction, .w = Reach
    float4 ColourIntensity;  // .xyz = Colour, .w = Intensity
    float4 Params;           // .x = InnerCos, .y = OuterCos, .z = IESParam, .w = Active (1.0 or 0.0)
};

struct ClusteredLightRegister
{
    LuminaireProfile Lights[64];       // 64 light profiles (4,096 bytes)
    float4 GridMinimum;                   // .xyz = Minimum, .w = ActiveCount
    float4 GridScale;                     // .xyz = Res / Extent, .w = CullEnabled
    uint4 CellMasks[512];         // 1,024 cells * vec2u (64-bit bitmask per cell)
};

// 📝 O(1) 3D cluster cell index lookup (-1 = empty/outside grid, -2 = culling disabled, 0..1023 = flat cell index).
int ResolveClusterCellIndex(float4 GridMinimum, float4 GridScale, float3 Position)
{
    let Count = ToUnsigned(GridMinimum.w);
    if (Count == 0u)
    {
        return -1;
    }
    if (GridScale.w < 0.5)
    {
        return -2;
    }
    let Rel = (Position - GridMinimum.xyz) * GridScale.xyz;
    if (any(Rel < float3(0.0)) || any(Rel >= float3(16.0, 16.0, 4.0)))
    {
        return -1;
    }
    let Cell = int3(clamp(int3(floor(Rel)), int3(0), int3(15, 15, 3)));
    return (Cell.z * 16 + Cell.y) * 16 + Cell.x;
}

// 📝 Extracts the 64-bit light bitmask (vec2u(lo, hi)) for CellIndex from the corresponding vec4u pair.
uint2 ResolveClusterMaskFromPair(int CellIndex, uint4 Pair, float ActiveCountF)
{
    if (CellIndex == -1)
    {
        return uint2(0u, 0u);
    }
    if (CellIndex == -2)
    {
        let Count = ToUnsigned(ActiveCountF);
        let Lo = SelectBranch(0xFFFFFFFFu, (1u << Count) - 1u, Count < 32u);
        let Hi = SelectBranch(0u, SelectBranch(0xFFFFFFFFu, (1u << (Count - 32u)) - 1u, Count < 64u), Count > 32u);
        return uint2(Lo, Hi);
    }
    let Flat = ToUnsigned(CellIndex);
    return SelectBranch(Pair.xy, Pair.zw, (Flat & 1u) != 0u);
}

// 📝 Analytical IES photometric distribution profile based on polar angle cos(theta) from the light's primary axis.
float EvaluateLightIES(float CosAngle, uint LightType, float InnerCos, float OuterCos, float3 ToSurface, float3 LightDir)
{
    if (LightType == LightTypeOmni)
    {
        return 1.0;
    }

    if (LightType == LightTypeStreetLamp)
    {
        // Roadway batwing IES distribution (Type II / III cobrahead): peak intensity around 50-65 degrees off nadir
        if (CosAngle <= 0.05)
        {
            return 0.0;
        }
        let SinAngle = sqrt(max(0.0, 1.0 - CosAngle * CosAngle));
        let Batwing = 0.38 + 1.35 * pow(SinAngle, 1.6) * smoothstep(0.05, 0.38, CosAngle);
        return Batwing * smoothstep(0.05, 0.22, CosAngle);
    }

    let SpotFactor = smoothstep(OuterCos, InnerCos, CosAngle);
    if (SpotFactor <= 0.0)
    {
        return 0.0;
    }

    if (LightType == LightTypeLowBeam)
    {
        // ECE/SAE sharp horizontal glare cutoff at Z = -0.02
        let Cutoff = smoothstep(0.06, -0.04, ToSurface.z);
        let HotSpot = 1.0 + 1.4 * pow(max(0.0, CosAngle), 12.0);
        return SpotFactor * Cutoff * HotSpot;
    }

    if (LightType == LightTypeHighBeam)
    {
        let PencilCore = 1.0 + 2.8 * pow(max(0.0, CosAngle), 24.0);
        return SpotFactor * PencilCore;
    }

    if (LightType == LightTypeFog)
    {
        let VertDist = abs(ToSurface.z + 0.04);
        let BandMask = exp(-VertDist * VertDist * 28.0);
        return SpotFactor * (0.35 + 1.8 * BandMask);
    }

    if (LightType == LightTypeFloodlight)
    {
        let Lobe1 = pow(max(0.0, CosAngle), 4.0);
        let Lobe2 = smoothstep(OuterCos, lerp(OuterCos, InnerCos, 0.5), CosAngle);
        return 0.65 * Lobe2 + 0.85 * Lobe1;
    }

    return SpotFactor;
}

// 📝 Incident illuminance arriving at SurfacePos from Light (before NdotL, matching SunIlluminance and Fire.Illuminance).
float3 EvaluateLightIlluminance(LuminaireProfile Light, float3 SurfacePos, float3 SurfaceNormal)
{
    if (Light.Params.w < 0.5)
    {
        return float3(0.0);
    }

    let LightPos = Light.PositionType.xyz;
    let Offset = LightPos - SurfacePos;
    let DistSq = dot(Offset, Offset);
    let Reach = Light.DirectionReach.w;
    if (DistSq >= Reach * Reach)
    {
        return float3(0.0);
    }

    let Dist = sqrt(max(DistSq, 1.0e-4));
    let LightDir = Offset / Dist;
    if (dot(SurfaceNormal, LightDir) <= 0.0)
    {
        return float3(0.0);
    }

    let NormDist2 = DistSq / max(Reach * Reach, 1.0e-4);
    let Window = max(0.0, 1.0 - NormDist2 * NormDist2);
    let Falloff = (Window * Window) / (DistSq + 1.25);

    let ToSurface = -LightDir;
    let Axis = normalize(Light.DirectionReach.xyz);
    let CosAngle = dot(ToSurface, Axis);

    let Type = ToUnsigned(Light.PositionType.w);
    let InnerCos = Light.Params.x;
    let OuterCos = Light.Params.y;
    let IES = EvaluateLightIES(CosAngle, Type, InnerCos, OuterCos, ToSurface, Axis);
    if (IES <= 0.0)
    {
        return float3(0.0);
    }

    let Colour = Light.ColourIntensity.xyz;
    let Intensity = Light.ColourIntensity.w;
    return Colour * (Intensity * Falloff * IES);
}

// 📝 Lambertian surface irradiance (Illuminance * NdotL) for surface card direct injection (CardDirect.slang).
float3 EvaluateLightArrival(LuminaireProfile Light, float3 SurfacePos, float3 SurfaceNormal)
{
    let Illuminance = EvaluateLightIlluminance(Light, SurfacePos, SurfaceNormal);
    if (all(Illuminance <= float3(0.0)))
    {
        return float3(0.0);
    }
    let LightDir = normalize(Light.PositionType.xyz - SurfacePos);
    return Illuminance * max(dot(SurfaceNormal, LightDir), 0.0);
}
