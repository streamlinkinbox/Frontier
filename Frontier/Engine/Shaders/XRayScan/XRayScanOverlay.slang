//============================================================================================================================================
//                                                          XRAYSCANOVERLAY.SLANG
//============================================================================================================================================
// 🧩 The X-ray scan's display-referred layers, drawn over the tone-mapped picture in the order the reference draws them (Truck.tsx and
//    App.tsx): the ghost shell, the wireframe, the curtain and the particles. A port of ScanOverlay.wgsl. The stage veils and the HUD canvas of
//    that file are interface layers and are not part of this unit.
//
//    All colours blend in encoded space, exactly where the reference blends them: it draws straight into an sRGB canvas, so the overlay is
//    rendered into the non-sRGB view of the same image.

#pragma once
#include "IntrinsicParity.slang"
#include "SurfaceRaster.slang"
#include "XRayScanOpacity.slang"

struct XRayScanProfile
{
    float4 GhostColour;     // rgb holoMat uColor, as the reference ships it (the shader writes it unconverted)
    float4 AccentColour;    // rgb holoMat uAccent
    float4 EdgeColour;      // rgb the wireframe's colour as the canvas receives it (ACES filmic mapped, sRGB encoded), a its opacity (0.55 × amount)
    float4 CurtainColour;   // rgb the curtain's colour, mapped and encoded the same way
    float4 CurtainOpacity;  // x the wide plane, y the narrow plane
    float4 SparkleColour;   // rgb drei's colour, mapped and encoded the same way (its shader includes both fragment chunks), a opacity
    float4 CarOrigin;       // xyz the ground point under the car's middle, w the reference's world height of its ground (−0.62 + bob)
    float4 CarForward;      // xyz unit
    float4 CarRight;        // xyz unit
    float4 CarUp;           // xyz unit
    float4 Front;           // x the front's position along the car [m], y particle clock [s], z pixel ratio, w the scan line's top (stage veil, unused here)
    float4 Layers;          // x particle size, y particle speed, z particle count, w scan HUD drawn (unused here)
};

  [[vk::binding(6, 0)]]  ConstantBuffer<XRayScanProfile> Scan;
  [[vk::binding(7, 0)]]  StructuredBuffer<float4>        ScanEdges;      // per segment: A.xyz + tag (occupant | family << 8), B.xyz + 0
  [[vk::binding(8, 0)]]  StructuredBuffer<float4>        ScanSparkles;   // per particle: xyz offset (along, up, right) from the car's middle on the ground, w unused
  [[vk::binding(9, 0)]]  ConstantBuffer<XRayScanView>    ScanView;
  [[vk::binding(10, 0)]] Texture2D<float>                SceneDepth;     // the picture's raster depth: reversed, infinite far, device depth = near / view depth

struct OverlayTarget
{
    [[vk::location(0)]] float4 Colour : LOCATION0;
};

// 📝 The reference draws the ghost, the wireframe, the curtain and the particles with the depth test on, against the solid car and the room.
//    The picture's depth is the jittered raster's and these layers are not, so a layer lying on a surface meets that surface within a sub-pixel's
//    worth of depth: the test allows two centimetres and half a percent of the distance. Nothing stored (the far plane) occludes nothing.
bool ResolveOccluded(float4 Pixel)
{
    let Stored = LoadTexel(SceneDepth, int2(Pixel.xy), 0);
    if (Stored <= 0.0)
    {
        return false;
    }
    let Nearest = ScanView.Camera.x / Stored;
    return 1.0 / Pixel.w > Nearest + 0.02 + 0.005 * Nearest;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GHOST SHELL
//------------------------------------------------------------------------------------------------------------------------

struct GhostVarying
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 Normal : LOCATION0;
    [[vk::location(1)]] float3 World : LOCATION1;
    [[vk::location(2)]] nointerpolation uint Family : LOCATION2;
    [[vk::location(3)]] nointerpolation uint Occupant : LOCATION3;
};

// 📝 holoVert: world position, world normal, no jitter. The shell is the scanned occupants' own facets, deformed as the solid ones are.
[shader("vertex")]
GhostVarying GhostVertex([[vk::location(0)]] float3 Position : LOCATION0, [[vk::location(1)]] uint Family : LOCATION1, [[vk::location(2)]] float3 Normal : LOCATION2,
                         [[vk::location(3)]] uint OccupantOffset : LOCATION3, [[vk::location(4)]] uint OccupantIndex : LOCATION4)
{
    let Index = OccupantIndex + OccupantOffset;
    var Out : GhostVarying = {};
    Out.Position = float4(0.0, 0.0, 2.0, 1.0);
    if (!OccupantScanned(ScanView, Index))
    {
        return Out;
    }
    let Occupant = Occupants[Index];
    let Moved    = DeformVehicleVertex(Index, Family, Position, Normal);
    let World    = mul(float4(Moved.Position, 1.0), Occupant.LocalToWorld);
    Out.Position = mul(World, Refresh.UnjitteredViewProjection);
    Out.World    = World.xyz;
    Out.Normal   = normalize((mul(float4(Moved.Normal, 0.0), Occupant.LocalToWorld)).xyz);
    Out.Family   = ResolveSurfaceFamily(Family, Occupant.MaterialOverride);
    Out.Occupant = Index;
    return Out;
}

// 📐 holoFrag, line for line. ScanD is the distance past the front (the reference's vWorld.x − uScanX); the lines run in the reference's world
//    height, whose ground sits at CarOrigin.w below the car's own.
[shader("fragment")]
OverlayTarget GhostFragment(GhostVarying Varying)
{
    if (ScanSparesFamily(ScanView, Varying.Family))
    {
        discard;
    }
    let ScanD  = ResolveFrontDistance(ScanView, Varying.World);
    let Region = smoothstep(-0.9, -0.05, ScanD);
    let Vis    = Region * ScanView.ScanState.x;
    if (Vis < 0.003 || ResolveOccluded(Varying.Position))
    {
        discard;
    }
    let N      = normalize(Varying.Normal);
    let V      = normalize(Refresh.CameraPosition - Varying.World);
    let Height = dot(Varying.World - Scan.CarOrigin.xyz, Scan.CarUp.xyz) + Scan.CarOrigin.w;
    let Fres   = pow(1.0 - abs(dot(N, V)), 2.3);
    let Lines  = smoothstep(0.72, 1.0, sin(Height * 85.0) * 0.5 + 0.5);
    // 📝 The reference writes this as pow(x, 2.0) with x negative behind the line; the square is taken as a product, the same number wherever pow
    //    is defined.
    let Reach  = (ScanD + 0.18) * 4.2;
    let Edge   = exp(-(Reach * Reach));
    let Alpha  = (0.028 + Fres * 0.50 + Lines * 0.040 + Edge * 0.30) * Vis;
    let Colour = Scan.GhostColour.rgb + Scan.AccentColour.rgb * Edge * 1.15;
    var Out : OverlayTarget = {};
    Out.Colour = float4(Colour, Alpha);
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WIREFRAME
//------------------------------------------------------------------------------------------------------------------------

struct EdgeVarying
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 World : LOCATION0;
    [[vk::location(1)]] float Across : LOCATION1;
};

static const float EdgeHalfWidth = 1.0;   // [px] a one pixel line, as WebGL's LineBasicMaterial

// 📝 Raster lines are one pixel wide and unantialiased: each segment becomes a screen-space quad, two pixels across, whose coverage falls off
//    linearly from its middle — a one pixel antialiased line. Both ends go through the same vertex deformation as the solid facets.
[shader("vertex")]
EdgeVarying EdgeVertex(uint Corner : SV_VulkanVertexID, uint Segment : SV_VulkanInstanceID)
{
    let A      = ScanEdges[Segment * 2u];
    let B      = ScanEdges[Segment * 2u + 1u];
    let Tag    = asuint(A.w);
    let Index  = Tag & 255u;
    let Family = Tag >> 8u;
    var Out : EdgeVarying = {};
    Out.Position = float4(0.0, 0.0, 2.0, 1.0);
    if (!OccupantScanned(ScanView, Index))
    {
        return Out;
    }
    let Occupant = Occupants[Index];
    let LocalA   = DeformVehicleVertex(Index, Family, A.xyz, float3(0.0, 0.0, 1.0)).Position;
    let LocalB   = DeformVehicleVertex(Index, Family, B.xyz, float3(0.0, 0.0, 1.0)).Position;
    let WorldA   = (mul(float4(LocalA, 1.0), Occupant.LocalToWorld)).xyz;
    let WorldB   = (mul(float4(LocalB, 1.0), Occupant.LocalToWorld)).xyz;
    let ClipA    = mul(float4(WorldA, 1.0), Refresh.UnjitteredViewProjection);
    let ClipB    = mul(float4(WorldB, 1.0), Refresh.UnjitteredViewProjection);
    if (ClipA.w < 1.0e-3 || ClipB.w < 1.0e-3)
    {
        return Out;
    }
    let HalfExtent = 0.5 * Refresh.ScreenExtent;
    let PixelA = ClipA.xy / ClipA.w * HalfExtent;
    let PixelB = ClipB.xy / ClipB.w * HalfExtent;
    var Direction = PixelB - PixelA;
    let Span = length(Direction);
    Direction = SelectBranch(float2(1.0, 0.0), Direction / max(Span, 1.0e-6), Span > 1.0e-4);
    let Normal = float2(-Direction.y, Direction.x);
    let AtEnd  = Corner == 2u || Corner == 3u || Corner == 5u;
    let Side   = SelectBranch(-1.0, 1.0, Corner == 1u || Corner == 4u || Corner == 5u);
    let Pixel  = SelectBranch(PixelA, PixelB, AtEnd) + Normal * Side * EdgeHalfWidth + Direction * SelectBranch(-0.5, 0.5, AtEnd);
    let Clip   = SelectBranch(ClipA, ClipB, AtEnd);
    Out.Position = float4(Pixel / HalfExtent * Clip.w, Clip.z, Clip.w);
    Out.World    = SelectBranch(WorldA, WorldB, AtEnd);
    Out.Across   = Side;
    return Out;
}

// 📝 The reference clips the lines with the plane x = uScanX + 0.05, keeping the swept side.
[shader("fragment")]
OverlayTarget EdgeFragment(EdgeVarying Varying)
{
    if (ResolveFrontDistance(ScanView, Varying.World) < 0.05 || ResolveOccluded(Varying.Position))
    {
        discard;
    }
    let Coverage = clamp(1.0 - abs(Varying.Across) * EdgeHalfWidth, 0.0, 1.0);
    var Out : OverlayTarget = {};
    Out.Colour = float4(Scan.EdgeColour.rgb, Scan.EdgeColour.a * Coverage);
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CURTAIN
//------------------------------------------------------------------------------------------------------------------------

struct CurtainVarying
{
    float4 Position : SV_Position;
    [[vk::location(0)]] nointerpolation uint Plane : LOCATION0;
};

// 📝 Two planes facing along the car, centred on the front at 1.18 m: 2.4 × 2.5 m and 0.55 × 2.5 m.
[shader("vertex")]
CurtainVarying CurtainVertex(uint Corner : SV_VulkanVertexID, uint Plane : SV_VulkanInstanceID)
{
    static const float2 Corners[6] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0), float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0) };
    let Size   = SelectBranch(float2(2.4, 2.5), float2(0.55, 2.5), Plane == 1u);
    let Centre = Scan.CarOrigin.xyz + Scan.CarForward.xyz * Scan.Front.x + Scan.CarUp.xyz * 1.18;
    let World  = Centre + Scan.CarRight.xyz * (Corners[Corner].x * 0.5 * Size.x) + Scan.CarUp.xyz * (Corners[Corner].y * 0.5 * Size.y);
    var Out : CurtainVarying = {};
    Out.Position = mul(float4(World, 1.0), Refresh.UnjitteredViewProjection);
    Out.Plane    = Plane;
    return Out;
}

[shader("fragment")]
OverlayTarget CurtainFragment(CurtainVarying Varying)
{
    if (ResolveOccluded(Varying.Position))
    {
        discard;
    }
    var Out : OverlayTarget = {};
    Out.Colour = float4(Scan.CurtainColour.rgb, SelectBranch(Scan.CurtainOpacity.x, Scan.CurtainOpacity.y, Varying.Plane == 1u));
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PARTICLES
//------------------------------------------------------------------------------------------------------------------------

struct SparkleVarying
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 Point : LOCATION0;
};

// 📝 drei's Sparkles vertex stage: the point sways about its place by 0.2 m along the three axes on one phase,
//    time · speed + x · 100 (noise = 1), and is a square of 25 · size · pixel ratio / view depth pixels.
[shader("vertex")]
SparkleVarying SparkleVertex(uint Corner : SV_VulkanVertexID, uint Index : SV_VulkanInstanceID)
{
    static const float2 Corners[6] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0) };
    let Place  = ScanSparkles[Index].xyz;
    let Phase  = Scan.Front.y * Scan.Layers.y + Place.x * 100.0;
    let Sway   = float3(cos(Phase), sin(Phase), cos(Phase)) * 0.2;
    let Offset = Place + Sway;
    let World  = Scan.CarOrigin.xyz + Scan.CarForward.xyz * Offset.x + Scan.CarUp.xyz * Offset.y + Scan.CarRight.xyz * Offset.z;
    var Out : SparkleVarying = {};
    Out.Position = float4(0.0, 0.0, 2.0, 1.0);
    // 📝 The reference's floor hides what sinks below it.
    if (dot(World - Scan.CarOrigin.xyz, Scan.CarUp.xyz) < -0.01)
    {
        return Out;
    }
    let Clip = mul(float4(World, 1.0), Refresh.UnjitteredViewProjection);
    if (Clip.w < 1.0e-3)
    {
        return Out;
    }
    let Diameter = Scan.Layers.x * 25.0 * Scan.Front.z / Clip.w;
    let Spread   = (Corners[Corner] - float2(0.5, 0.5)) * Diameter;
    let Centre   = Clip.xy / Clip.w;
    let Ndc      = Centre + float2(Spread.x, -Spread.y) * 2.0 / Refresh.ScreenExtent;
    Out.Position = float4(Ndc * Clip.w, Clip.z, Clip.w);
    Out.Point    = Corners[Corner];
    return Out;
}

[shader("fragment")]
OverlayTarget SparkleFragment(SparkleVarying Varying)
{
    if (ResolveOccluded(Varying.Position))
    {
        discard;
    }
    let Strength = 0.05 / max(distance(Varying.Point, float2(0.5, 0.5)), 1.0e-4) - 0.1;
    var Out : OverlayTarget = {};
    Out.Colour = float4(Scan.SparkleColour.rgb, clamp(Strength * Scan.SparkleColour.a, 0.0, 1.0));
    return Out;
}
