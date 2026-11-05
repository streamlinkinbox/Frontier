//============================================================================================================================================
//                                                       XRAYSCANCAPTURESOLID.SLANG
//============================================================================================================================================
// 🧩 The stand-in solid pass of XRayScanCapture: the hero car drawn flat with a hemisphere and one key light, so the scan's overlay has a solid
//    picture and a raster depth to lie over. It is NOT the illumination port's lighting (that is the ReSTIR pipeline, which needs the whole
//    scene's acceleration structures); what it does take from the engine is the part the scan changes: the vertex deformation of the surface
//    raster and the stochastic fade of ResolveScanFadeDiscard, so the picture fades the way a scanned car does once temporal antialiasing
//    averages the refreshes.
//
//    Capture code: it lives in Exhibits/XRayScan and is not part of the XRayScan archive set.

#include "IntrinsicParity.slang"
#include "SurfaceRaster.slang"
#include "XRayScanOpacity.slang"

  [[vk::binding(6, 0)]] ConstantBuffer<XRayScanView> ScanView;

struct SolidVarying
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 Normal : LOCATION0;
    [[vk::location(1)]] float3 World : LOCATION1;
    [[vk::location(2)]] nointerpolation uint Family : LOCATION2;
    [[vk::location(3)]] nointerpolation uint Occupant : LOCATION3;
};

struct SolidTarget
{
    [[vk::location(0)]] float4 Colour : LOCATION0;
};

[shader("vertex")]
SolidVarying SolidVertex([[vk::location(0)]] float3 Position : LOCATION0, [[vk::location(1)]] uint Family : LOCATION1, [[vk::location(2)]] float3 Normal : LOCATION2,
                         [[vk::location(3)]] uint OccupantOffset : LOCATION3, [[vk::location(4)]] uint OccupantIndex : LOCATION4)
{
    let Index    = OccupantIndex + OccupantOffset;
    let Occupant = Occupants[Index];
    let Moved    = DeformVehicleVertex(Index, Family, Position, Normal);
    let World    = mul(float4(Moved.Position, 1.0), Occupant.LocalToWorld);
    var Out : SolidVarying = {};
    Out.Position = mul(World, Refresh.UnjitteredViewProjection);
    Out.World    = World.xyz;
    Out.Normal   = normalize((mul(float4(Moved.Normal, 0.0), Occupant.LocalToWorld)).xyz);
    Out.Family   = ResolveSurfaceFamily(Family, Occupant.MaterialOverride);
    Out.Occupant = Index;
    return Out;
}

// 📝 Albedo of the car's families (CourseMaterial indices): the paint, the glass, the plastic, the rubber, the lamps and the cabin.
float3 ResolveStandInAlbedo(uint Family)
{
    if (Family == 6u)  { return float3(0.045, 0.19, 0.46); }
    if (Family == 7u)  { return float3(0.02, 0.035, 0.05); }
    if (Family == 8u)  { return float3(0.035, 0.036, 0.04); }
    if (Family == 9u)  { return float3(0.012, 0.012, 0.014); }
    if (Family == 13u) { return float3(0.85, 0.82, 0.70); }
    if (Family == 14u) { return float3(0.55, 0.03, 0.03); }
    if (Family >= 70u && Family <= 80u) { return float3(0.10, 0.085, 0.075); }
    return float3(0.16, 0.16, 0.17);
}

[shader("fragment")]
SolidTarget SolidFragment(SolidVarying Varying)
{
    // 📝 The surface raster's hook: a scanned occupant's fragment is dropped against an interleaved gradient that changes every refresh.
    if (ResolveScanFadeDiscard(ScanView, Varying.Occupant, Varying.Family, Varying.World, Varying.Position.xy, Refresh.RefreshIndex))
    {
        discard;
    }
    let N      = normalize(Varying.Normal);
    let V      = normalize(Refresh.CameraPosition - Varying.World);
    let Key    = normalize(float3(-0.35, -0.5, 0.8));
    let Sky    = lerp(float3(0.10, 0.10, 0.11), float3(0.50, 0.58, 0.70), N.z * 0.5 + 0.5);
    let Lit    = max(dot(N, Key), 0.0);
    let Rim    = pow(1.0 - abs(dot(N, V)), 3.0);
    let Albedo = ResolveStandInAlbedo(Varying.Family);
    var Linear = Albedo * (Sky * 0.75 + float3(1.0, 0.96, 0.9) * Lit * 0.95) + float3(0.12, 0.17, 0.25) * Rim * 0.35;
    // 📝 The lamps carry their own light.
    if (Varying.Family == 13u) { Linear += float3(0.30, 0.28, 0.22); }
    if (Varying.Family == 14u) { Linear += float3(0.45, 0.02, 0.02); }
    var Out : SolidTarget = {};
    Out.Colour = float4(pow(saturate(Linear), float3(1.0 / 2.2)), 1.0);
    return Out;
}
