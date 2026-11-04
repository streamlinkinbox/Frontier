//============================================================================================================================================
//                                                         MATERIALRESOLVE.SLANG
//============================================================================================================================================
// 🧩 Deferred resolve: visibility identity and barycentrics to world position, UV and normal to material; a second entry refracts glass
//    through its front and peeled back layers and gathers the resolved opaque image (MaterialResolve.wgsl, statement for statement).

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"
#include "RefreshDescriptor.slang"

  [[vk::binding(1, 0)]]  StructuredBuffer<OccupantProfile>                     Occupants;
  [[vk::binding(2, 0)]]  StructuredBuffer<float4>                              CornerPos;    // xyz local position, w family bit-cast to f32
  [[vk::binding(3, 0)]]  StructuredBuffer<float4>                              CornerData;   // xy UV, z tier, w unused
  [[vk::binding(4, 0)]]  Texture2D<uint4>                                      OpaqueVisTex;
  [[vk::binding(5, 0)]]  Texture2D<float4>                                     OpaqueBaryTex;
  [[vk::binding(6, 0)]]  [[vk::image_format("rgba16f")]] RWTexture2D<float4>   OpaqueResolved;
  [[vk::binding(7, 0)]]  Texture2D<uint4>                                      GlassVisTex;
  [[vk::binding(8, 0)]]  Texture2D<float4>                                     GlassBaryTex;
  [[vk::binding(9, 0)]]  Texture2D<uint4>                                      GlassBackVisTex;
  [[vk::binding(10, 0)]] Texture2D<float4>                                     GlassBackBaryTex;
  [[vk::binding(11, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>   FinalResolved;
  [[vk::binding(12, 0)]] Texture2D<float4>                                     OpaqueResolvedRead;
  [[vk::binding(13, 0)]] Texture2D<float>                                      HiZTexture;

static const float3 SkyColour      = float3(0.36, 0.48, 0.66);
static const float3 LightDirection = float3(0.375, 0.625, 0.5);
static const float  GlassEta       = 1.52;

float3 FamilyColour(uint Family)
{
    switch (Family % 8u)
    {
        case 0u: { return float3(0.62, 0.65, 0.70); }
        case 1u: { return float3(0.55, 0.48, 0.38); }
        case 2u: { return float3(0.72, 0.74, 0.78); }
        case 3u: { return float3(0.30, 0.34, 0.40); }
        case 4u: { return float3(0.66, 0.30, 0.24); }
        case 5u: { return float3(0.24, 0.44, 0.62); }
        case 6u: { return float3(0.28, 0.56, 0.36); }
        default: { return float3(0.70, 0.58, 0.26); }
    }
}

struct SurfacePoint
{
    bool   Valid;
    float3 World;
    float2 Uv;
    float3 Normal;
    uint   Family;
};

SurfacePoint ResolvePoint(Texture2D<uint4> VisTex, Texture2D<float4> BaryTex, int2 Coord)
{
    var Point : SurfacePoint = {};
    let Id = LoadTexel(VisTex, Coord, 0).xy;
    if (Id.x == 0u) { return Point; }
    let TriangleFirst = Id.x - 1u;
    let Occupant = Occupants[Id.y];
    let Bary = LoadTexel(BaryTex, Coord, 0).xy;
    let Weights = float3(1.0 - Bary.x - Bary.y, Bary.x, Bary.y);
    var World = float3(0.0);
    var Uv = float2(0.0);
    var Corners : float3[3] = { float3(0.0), float3(0.0), float3(0.0) };
    var Family = 0u;
    for (var C = 0u; C < 3u; C = C + 1u)
    {
        let Corner = CornerPos[TriangleFirst + C];
        Corners[C] = (mul(float4(Corner.xyz, 1.0), Occupant.LocalToWorld)).xyz;
        World = World + Corners[C] * Weights[C];
        Uv = Uv + CornerData[TriangleFirst + C].xy * Weights[C];
        Family = asuint(Corner.w);
    }
    Point.Valid = true;
    Point.World = World;
    Point.Uv = Uv;
    Point.Normal = normalize(cross(Corners[1] - Corners[0], Corners[2] - Corners[0]));
    Point.Family = Family;
    return Point;
}

float3 ShadeOpaque(SurfacePoint Point)
{
    var Colour = FamilyColour(Point.Family);
    if (any(Point.Uv != float2(0.0)))
    {
        let Checker = SelectBranch(0.45, 1.0, (ToSigned(floor(Point.Uv.x * 24.0)) + ToSigned(floor(Point.Uv.y * 24.0))) % 2 == 0);
        Colour = Colour * Checker;
    }
    let Diffuse = max(dot(Point.Normal, normalize(LightDirection)), 0.0);
    return Colour * (0.32 + Diffuse);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ResolveOpaqueVisibility(uint3 ID : SV_DispatchThreadID)
{
    let Extent = TextureExtent(OpaqueResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = int2(ID.xy);
    let Point = ResolvePoint(OpaqueVisTex, OpaqueBaryTex, Coord);
    let Colour = SelectBranch(SkyColour, ShadeOpaque(Point), Point.Valid);
    OpaqueResolved[ID.xy] = float4(Colour, 1.0);
}

float3 RefractDirection(float3 ViewDir, float3 Normal, float EtaRatio)
{
    let CosI = -dot(ViewDir, Normal);
    let K = 1.0 - EtaRatio * EtaRatio * (1.0 - CosI * CosI);
    if (K < 0.0) { return float3(0.0); }
    return EtaRatio * ViewDir + (EtaRatio * CosI - sqrt(K)) * Normal;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ResolveRefractionGather(uint3 ID : SV_DispatchThreadID)
{
    let Extent = TextureExtent(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = int2(ID.xy);
    var Colour = LoadTexel(OpaqueResolvedRead, Coord, 0).xyz;
    let Front = ResolvePoint(GlassVisTex, GlassBaryTex, Coord);
    if (Front.Valid)
    {
        let Back = ResolvePoint(GlassBackVisTex, GlassBackBaryTex, Coord);
        var ViewDir = Front.World - Refresh.CameraPosition;
        let ViewLength = length(ViewDir);
        ViewDir = ViewDir / max(ViewLength, 1.0e-6);
        var Normal = Front.Normal;
        if (dot(Normal, ViewDir) > 0.0) { Normal = -Normal; }
        let Inside = RefractDirection(ViewDir, Normal, 1.0 / GlassEta);
        var Gathered = SkyColour;
        var Thickness = 0.25;
        if (any(Inside != float3(0.0)))
        {
            var ExitPoint = Front.World + Inside * Thickness;
            var Outside = Inside;
            if (Back.Valid)
            {
                Thickness = length(Back.World - Front.World);
                ExitPoint = Back.World;
                var ExitNormal = Back.Normal;
                if (dot(ExitNormal, Inside) < 0.0) { ExitNormal = -ExitNormal; }
                let Outgoing = RefractDirection(Inside, -ExitNormal, GlassEta);
                if (any(Outgoing != float3(0.0))) { Outside = Outgoing; }
            }
            let SampleWorld = ExitPoint + Outside * 2.0;
            let Clip = mul(float4(SampleWorld, 1.0), Refresh.UnjitteredViewProjection);
            if (Clip.w > 1.0e-4)
            {
                let Ndc = Clip.xy / Clip.w;
                let Scaled = float2(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5) * float2(Extent);
                let SampleCoord = int2(ToSigned(Scaled.x), ToSigned(Scaled.y));
                if (all(SampleCoord >= int2(0)) && all(SampleCoord < int2(Extent)))
                {
                    Gathered = LoadTexel(OpaqueResolvedRead, SampleCoord, 0).xyz;
                }
            }
        }
        let Absorb = exp(float3(-0.22, -0.10, -0.16) * Thickness);
        let CosView = clamp(abs(dot(Normal, ViewDir)), 0.0, 1.0);
        let F0 = pow((GlassEta - 1.0) / (GlassEta + 1.0), 2.0);
        let Fresnel = F0 + (1.0 - F0) * pow(1.0 - CosView, 5.0);
        Colour = Gathered * Absorb * (1.0 - Fresnel) + SkyColour * Fresnel;
    }
    FinalResolved[ID.xy] = float4(Colour, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                     INSPECTION VIEWS (written into the inspection target, presented un-tonemapped)
//------------------------------------------------------------------------------------------------------------------------

float3 DebugHashColour(uint N)
{
    var X = N * 2654435761u;
    X = (X ^ (X >> 13u)) * 1274126177u;
    X = X ^ (X >> 16u);
    return float3(float((X >> 16u) & 255u), float((X >> 8u) & 255u), float(X & 255u)) / 255.0 * 0.85 + float3(0.12);
}

// 📝 Visibility identities: every cluster triangle gets a stable hash colour; the glass front tints cyan and the glass back magenta.
[shader("compute")]
[numthreads(8, 8, 1)]
void DebugVisibilityIds(uint3 ID : SV_DispatchThreadID)
{
    let Extent = TextureExtent(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = int2(ID.xy);
    let Opaque = LoadTexel(OpaqueVisTex, Coord, 0).xy;
    let Front = LoadTexel(GlassVisTex, Coord, 0).xy;
    let Back = LoadTexel(GlassBackVisTex, Coord, 0).xy;
    var Colour = float3(0.10, 0.12, 0.20);   // visibly non-black: an empty buffer reads as "no coverage", never as a dead view
    if (Opaque.x != 0u) { Colour = DebugHashColour(Opaque.x); }
    if (Front.x != 0u) { Colour = DebugHashColour(Front.x) * float3(0.35, 1.0, 1.0); }
    if (Back.x != 0u && Front.x == 0u) { Colour = DebugHashColour(Back.x) * float3(1.0, 0.35, 1.0); }
    FinalResolved[ID.xy] = float4(Colour, 1.0);
}

// 📝 Glass layers: front green, back red, both (two-layer refraction pixels) yellow.
[shader("compute")]
[numthreads(8, 8, 1)]
void DebugGlassLayers(uint3 ID : SV_DispatchThreadID)
{
    let Extent = TextureExtent(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = int2(ID.xy);
    let Front = LoadTexel(GlassVisTex, Coord, 0).xy;
    let Back = LoadTexel(GlassBackVisTex, Coord, 0).xy;
    var Colour = LoadTexel(OpaqueResolvedRead, Coord, 0).xyz * 0.35;
    if (Front.x != 0u && Back.x != 0u) { Colour = float3(0.95, 0.85, 0.25); }
    else if (Front.x != 0u) { Colour = float3(0.25, 0.9, 0.45); }
    else if (Back.x != 0u) { Colour = float3(0.95, 0.3, 0.3); }
    FinalResolved[ID.xy] = float4(Colour, 1.0);
}

// 📝 Deferred resolve output copied verbatim (binding 12 carries the final resolved texture in this group).
[shader("compute")]
[numthreads(8, 8, 1)]
void DebugCopyResolved(uint3 ID : SV_DispatchThreadID)
{
    let Extent = TextureExtent(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = int2(ID.xy);
    FinalResolved[ID.xy] = float4(LoadTexel(OpaqueResolvedRead, Coord, 0).xyz, 1.0);
}

// 📝 Hi-Z pyramid (reversed-Z): near occluders bright, far or clear dark.
[shader("compute")]
[numthreads(8, 8, 1)]
void DebugHiZView(uint3 ID : SV_DispatchThreadID)
{
    let Extent = TextureExtent(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = int2(ID.xy);
    let Depth = LoadTexel(HiZTexture, Coord, 0);
    FinalResolved[ID.xy] = float4(float3(pow(Depth, 0.25)), 1.0);
}

// 📝 Tiers: which tier the cut picked per pixel, read from the tier stored in corner data z (streamed with the page, so it always
//    matches the drawn triangle). Tier 0 (finest) is warm red and each coarser tier shifts towards blue; the moving band as the
//    camera dollies is the continuous cut at work. Flat geometry may stay one colour: its coarsest tier is already exact.
[shader("compute")]
[numthreads(8, 8, 1)]
void DebugLodLevels(uint3 ID : SV_DispatchThreadID)
{
    let Extent = TextureExtent(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = int2(ID.xy);
    let Opaque = LoadTexel(OpaqueVisTex, Coord, 0).xy;
    let Front = LoadTexel(GlassVisTex, Coord, 0).xy;
    var Colour = float3(0.10, 0.12, 0.20);
    var Vis = Opaque;
    if (Vis.x == 0u) { Vis = Front; }
    if (Vis.x != 0u)
    {
        let Tier = CornerData[Vis.x - 1u].z;
        Colour = 0.55 + 0.45 * cos(6.2831853 * (Tier * 0.14) + float3(0.0, 0.33, 0.67));
        if (Front.x != 0u && Opaque.x == 0u) { Colour = Colour * float3(0.35, 1.0, 1.0); }   // glass keeps the cyan tell
    }
    FinalResolved[ID.xy] = float4(Colour, 1.0);
}
