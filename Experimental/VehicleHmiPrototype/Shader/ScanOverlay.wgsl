//============================================================================================================================================
//                                                           SCANOVERLAY.WGSL
//============================================================================================================================================
// 🧩 The X-ray scan's display-referred layers, drawn over the Garage preview after the ReSTIR picture has been tone mapped, in the order the
//    reference draws them (interactive-vehicle-hmi-configurator.zip, Truck.tsx and App.tsx):
//
//      ghost shell      additive, double-sided, behind the front        (holoVert / holoFrag)
//      wireframe        alpha blended, only past the front              (LineBasicMaterial + clipping plane)
//      curtain          two additive planes riding the front            (meshBasicMaterial)
//      particles        130 point sprites                               (drei Sparkles)
//      tags             the four labelled anchors                       (drei Html, drawn on the HUD canvas)
//      vignette         the stage's static vignette                     (CSS radial-gradient)
//      scan tint        screen-blended edge tint                        (mix-blend-screen)
//      dot matrix, scan line                                            (CSS radial-gradient dots, motion.div)
//      frame and labels the corner HUD                                  (HUD canvas)
//
//    All colours blend in encoded space, exactly where the reference blends them: it draws straight into an sRGB canvas, so the overlay is
//    rendered into the non-sRGB view of the same texture.

//! include "SurfaceRaster.wgsl"

struct ScanRecord
{
    GhostColour    : vec4f,   // rgb holoMat uColor, as the reference ships it (the shader writes it unconverted)
    AccentColour   : vec4f,   // rgb holoMat uAccent
    EdgeColour     : vec4f,   // rgb the wireframe's colour as the canvas receives it (ACES filmic mapped, sRGB encoded), a its opacity (0.55 × amount)
    CurtainColour  : vec4f,   // rgb the curtain's colour, mapped and encoded the same way
    CurtainOpacity : vec4f,   // x the wide plane, y the narrow plane
    SparkleColour  : vec4f,   // rgb drei's colour, mapped and encoded the same way (its shader includes both fragment chunks), a opacity
    CarOrigin      : vec4f,   // xyz the ground point under the car's middle, w the reference's world height of its ground (-0.62 + bob)
    CarForward     : vec4f,   // xyz unit
    CarRight       : vec4f,   // xyz unit
    CarUp          : vec4f,   // xyz unit
    Front          : vec4f,   // x the front's position along the car [m], y particle clock [s], z pixel ratio, w the scan line's top, 0…1
    Layers         : vec4f,   // x particle size, y particle speed, z particle count, w scan HUD drawn (1 while the scan toggle is on)
}

@group(0) @binding(6)  var<uniform> Scan : ScanRecord;
@group(0) @binding(7)  var<storage, read> ScanEdges : array<vec4f>;      // per segment: A.xyz + tag (occupant | family << 8), B.xyz + 0
@group(0) @binding(8)  var<storage, read> ScanSparkles : array<vec4f>;   // per particle: xyz offset (along, up, right) from the car's middle on the ground, w unused
@group(0) @binding(9)  var HudTexture : texture_2d<f32>;                 // tags layer above, frame layer below, straight alpha
@group(0) @binding(10) var HudSampler : sampler;
@group(0) @binding(11) var SceneDepth : texture_depth_2d;               // the picture's raster depth: reversed, infinite far, device depth = near / view depth

// 📝 The reference draws the ghost, the wireframe, the curtain and the particles with the depth test on, against the solid car and the room.
//    The picture's depth is the jittered raster's and these layers are not, so a layer lying on a surface meets that surface within a sub-pixel's
//    worth of depth: the test allows two centimetres and half a percent of the distance. Nothing stored (the far plane) occludes nothing.
fn ResolveOccluded(Pixel: vec4f) -> bool
{
    let Stored = textureLoad(SceneDepth, vec2i(Pixel.xy), 0);
    if (Stored <= 0.0)
    {
        return false;
    }
    let Nearest = Frame.CameraNear / Stored;
    return 1.0 / Pixel.w > Nearest + 0.02 + 0.005 * Nearest;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GHOST SHELL
//------------------------------------------------------------------------------------------------------------------------

struct GhostVarying
{
    @builtin(position) Position             : vec4f,
    @location(0) Normal                     : vec3f,
    @location(1) World                      : vec3f,
    @location(2) @interpolate(flat) Family  : u32,
    @location(3) @interpolate(flat) Occupant : u32,
}

// 📝 holoVert: world position, world normal, no jitter. The shell is the scanned occupants' own facets, deformed as the solid ones are.
@vertex
fn GhostVertex(@location(0) Position: vec3f, @location(1) Family: u32, @location(2) Normal: vec3f,
               @location(3) OccupantOffset: u32, @builtin(instance_index) OccupantIndex: u32) -> GhostVarying
{
    let Index = OccupantIndex + OccupantOffset;
    var Out : GhostVarying;
    Out.Position = vec4f(0.0, 0.0, 2.0, 1.0);
    if (!OccupantScanned(Index))
    {
        return Out;
    }
    let Occupant = Occupants[Index];
    let Moved    = DeformVehicleVertex(Index, Family, Position, Normal);
    let World    = Occupant.LocalToWorld * vec4f(Moved.Position, 1.0);
    Out.Position = Frame.UnjitteredViewProjection * World;
    Out.World    = World.xyz;
    Out.Normal   = normalize((Occupant.LocalToWorld * vec4f(Moved.Normal, 0.0)).xyz);
    Out.Family   = select(Family, Occupant.MaterialOverride, Occupant.MaterialOverride != NoMaterialOverride);
    Out.Occupant = Index;
    return Out;
}

// 📐 holoFrag, line for line. scanD is the distance past the front (the reference's vWorld.x − uScanX); the lines run in the reference's world
//    height, whose ground sits at CarOrigin.w below the car's own.
@fragment
fn GhostFragment(Varying: GhostVarying) -> @location(0) vec4f
{
    if (ScanSparesFamily(Varying.Family))
    {
        discard;
    }
    let ScanD  = ResolveScanPassed(Varying.World);
    let Region = smoothstep(-0.9, -0.05, ScanD);
    let Vis    = Region * Frame.ScanState.x;
    if (Vis < 0.003 || ResolveOccluded(Varying.Position))
    {
        discard;
    }
    let N      = normalize(Varying.Normal);
    let V      = normalize(Frame.CameraPosition - Varying.World);
    let Height = dot(Varying.World - Scan.CarOrigin.xyz, Scan.CarUp.xyz) + Scan.CarOrigin.w;
    let Fres   = pow(1.0 - abs(dot(N, V)), 2.3);
    let Lines  = smoothstep(0.72, 1.0, sin(Height * 85.0) * 0.5 + 0.5);
    let Edge   = exp(-pow((ScanD + 0.18) * 4.2, 2.0));
    let Alpha  = (0.028 + Fres * 0.50 + Lines * 0.040 + Edge * 0.30) * Vis;
    let Colour = Scan.GhostColour.rgb + Scan.AccentColour.rgb * Edge * 1.15;
    return vec4f(Colour, Alpha);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WIREFRAME
//------------------------------------------------------------------------------------------------------------------------

struct EdgeVarying
{
    @builtin(position) Position : vec4f,
    @location(0) World          : vec3f,
    @location(1) Across         : f32,
}

const EdgeHalfWidth : f32 = 1.0;   // [px] a one pixel line, as WebGL's LineBasicMaterial

// 📝 WebGPU lines are one pixel wide and unantialiased: each segment becomes a screen-space quad, two pixels across, whose coverage falls off
//    linearly from its middle — a one pixel antialiased line. Both ends go through the same vertex deformation as the solid facets.
@vertex
fn EdgeVertex(@builtin(vertex_index) Corner: u32, @builtin(instance_index) Segment: u32) -> EdgeVarying
{
    let A     = ScanEdges[Segment * 2u];
    let B     = ScanEdges[Segment * 2u + 1u];
    let Tag   = bitcast<u32>(A.w);
    let Index = Tag & 255u;
    let Family = Tag >> 8u;
    var Out : EdgeVarying;
    Out.Position = vec4f(0.0, 0.0, 2.0, 1.0);
    if (!OccupantScanned(Index))
    {
        return Out;
    }
    let Occupant = Occupants[Index];
    let LocalA   = DeformVehicleVertex(Index, Family, A.xyz, vec3f(0.0, 0.0, 1.0)).Position;
    let LocalB   = DeformVehicleVertex(Index, Family, B.xyz, vec3f(0.0, 0.0, 1.0)).Position;
    let WorldA   = (Occupant.LocalToWorld * vec4f(LocalA, 1.0)).xyz;
    let WorldB   = (Occupant.LocalToWorld * vec4f(LocalB, 1.0)).xyz;
    let ClipA    = Frame.UnjitteredViewProjection * vec4f(WorldA, 1.0);
    let ClipB    = Frame.UnjitteredViewProjection * vec4f(WorldB, 1.0);
    if (ClipA.w < 1.0e-3 || ClipB.w < 1.0e-3)
    {
        return Out;
    }
    let HalfExtent = 0.5 * Frame.ScreenExtent;
    let PixelA = ClipA.xy / ClipA.w * HalfExtent;
    let PixelB = ClipB.xy / ClipB.w * HalfExtent;
    var Direction = PixelB - PixelA;
    let Span = length(Direction);
    Direction = select(vec2f(1.0, 0.0), Direction / max(Span, 1.0e-6), Span > 1.0e-4);
    let Normal = vec2f(-Direction.y, Direction.x);
    let AtEnd  = Corner == 2u || Corner == 3u || Corner == 5u;
    let Side   = select(-1.0, 1.0, Corner == 1u || Corner == 4u || Corner == 5u);
    let Pixel  = select(PixelA, PixelB, AtEnd) + Normal * Side * EdgeHalfWidth + Direction * select(-0.5, 0.5, AtEnd);
    let Clip   = select(ClipA, ClipB, AtEnd);
    Out.Position = vec4f(Pixel / HalfExtent * Clip.w, Clip.z, Clip.w);
    Out.World    = select(WorldA, WorldB, AtEnd);
    Out.Across   = Side;
    return Out;
}

// 📝 The reference clips the lines with the plane x = uScanX + 0.05, keeping the swept side.
@fragment
fn EdgeFragment(Varying: EdgeVarying) -> @location(0) vec4f
{
    if (ResolveScanPassed(Varying.World) < 0.05 || ResolveOccluded(Varying.Position))
    {
        discard;
    }
    let Coverage = clamp(1.0 - abs(Varying.Across) * EdgeHalfWidth, 0.0, 1.0);
    return vec4f(Scan.EdgeColour.rgb, Scan.EdgeColour.a * Coverage);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CURTAIN
//------------------------------------------------------------------------------------------------------------------------

struct CurtainVarying
{
    @builtin(position) Position             : vec4f,
    @location(0) @interpolate(flat) Plane   : u32,
}

// 📝 Two planes facing along the car, centred on the front at 1.18 m: 2.4 × 2.5 m and 0.55 × 2.5 m.
@vertex
fn CurtainVertex(@builtin(vertex_index) Corner: u32, @builtin(instance_index) Plane: u32) -> CurtainVarying
{
    var Corners = array<vec2f, 6>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, -1.0), vec2f(1.0, 1.0), vec2f(-1.0, 1.0));
    let Size   = select(vec2f(2.4, 2.5), vec2f(0.55, 2.5), Plane == 1u);
    let Centre = Scan.CarOrigin.xyz + Scan.CarForward.xyz * Scan.Front.x + Scan.CarUp.xyz * 1.18;
    let World  = Centre + Scan.CarRight.xyz * (Corners[Corner].x * 0.5 * Size.x) + Scan.CarUp.xyz * (Corners[Corner].y * 0.5 * Size.y);
    var Out : CurtainVarying;
    Out.Position = Frame.UnjitteredViewProjection * vec4f(World, 1.0);
    Out.Plane    = Plane;
    return Out;
}

@fragment
fn CurtainFragment(Varying: CurtainVarying) -> @location(0) vec4f
{
    if (ResolveOccluded(Varying.Position))
    {
        discard;
    }
    return vec4f(Scan.CurtainColour.rgb, select(Scan.CurtainOpacity.x, Scan.CurtainOpacity.y, Varying.Plane == 1u));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PARTICLES
//------------------------------------------------------------------------------------------------------------------------

struct SparkleVarying
{
    @builtin(position) Position : vec4f,
    @location(0) Point          : vec2f,
}

// 📝 drei's Sparkles vertex stage: the point sways about its place by 0.2 m along the three axes on one phase,
//    time · speed + x · 100 (noise = 1), and is a square of 25 · size · pixel ratio / view depth pixels.
@vertex
fn SparkleVertex(@builtin(vertex_index) Corner: u32, @builtin(instance_index) Index: u32) -> SparkleVarying
{
    var Corners = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0), vec2f(0.0, 0.0), vec2f(1.0, 1.0), vec2f(0.0, 1.0));
    let Place  = ScanSparkles[Index].xyz;
    let Phase  = Scan.Front.y * Scan.Layers.y + Place.x * 100.0;
    let Sway   = vec3f(cos(Phase), sin(Phase), cos(Phase)) * 0.2;
    let Offset = Place + Sway;
    let World  = Scan.CarOrigin.xyz + Scan.CarForward.xyz * Offset.x + Scan.CarUp.xyz * Offset.y + Scan.CarRight.xyz * Offset.z;
    var Out : SparkleVarying;
    Out.Position = vec4f(0.0, 0.0, 2.0, 1.0);
    // 📝 The reference's floor hides what sinks below it.
    if (dot(World - Scan.CarOrigin.xyz, Scan.CarUp.xyz) < -0.01)
    {
        return Out;
    }
    let Clip = Frame.UnjitteredViewProjection * vec4f(World, 1.0);
    if (Clip.w < 1.0e-3)
    {
        return Out;
    }
    let Diameter = Scan.Layers.x * 25.0 * Scan.Front.z / Clip.w;
    let Spread   = (Corners[Corner] - vec2f(0.5)) * Diameter;
    let Centre   = Clip.xy / Clip.w;
    let Ndc      = Centre + vec2f(Spread.x, -Spread.y) * 2.0 / Frame.ScreenExtent;
    Out.Position = vec4f(Ndc * Clip.w, Clip.z, Clip.w);
    Out.Point    = Corners[Corner];
    return Out;
}

@fragment
fn SparkleFragment(Varying: SparkleVarying) -> @location(0) vec4f
{
    if (ResolveOccluded(Varying.Position))
    {
        discard;
    }
    let Strength = 0.05 / max(distance(Varying.Point, vec2f(0.5)), 1.0e-4) - 0.1;
    return vec4f(Scan.SparkleColour.rgb, clamp(Strength * Scan.SparkleColour.a, 0.0, 1.0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                              STAGE OVERLAYS (CSS in the reference)
//------------------------------------------------------------------------------------------------------------------------

struct VeilVarying
{
    @builtin(position) Position : vec4f,
    @location(0) Uv             : vec2f,
}

@vertex
fn VeilVertex(@builtin(vertex_index) Corner: u32) -> VeilVarying
{
    let Uv = vec2f(f32((Corner << 1u) & 2u), f32(Corner & 2u));
    var Out : VeilVarying;
    Out.Position = vec4f(Uv * 2.0 - 1.0, 0.0, 1.0);
    Out.Uv       = vec2f(Uv.x, 1.0 - Uv.y);
    return Out;
}

// 📐 CSS radial-gradient(ellipse at (X, Y)): the ellipse of the farthest-side shape, scaled to pass through the farthest corner. T is the
//    normalised distance, 0 at the centre and 1 on the ending shape.
fn ResolveEllipseDistance(Uv: vec2f, Centre: vec2f) -> f32
{
    let Radius = max(Centre, vec2f(1.0) - Centre) * 1.41421356;
    return length((Uv - Centre) / Radius);
}

// 📝 App.tsx: radial-gradient(ellipse at 50% 42%, transparent 52%, rgba(0,0,0,0.55) 100%): black, linear in alpha.
@fragment
fn VignetteFragment(Varying: VeilVarying) -> @location(0) vec4f
{
    let T = ResolveEllipseDistance(Varying.Uv, vec2f(0.5, 0.42));
    return vec4f(0.0, 0.0, 0.0, 0.55 * clamp((T - 0.52) / 0.48, 0.0, 1.0));
}

// 📝 App.tsx, scan mode: radial-gradient(ellipse at center, transparent 40%, rgba(8,20,30,0.55)) with mix-blend-screen. The pipeline blends
//    One / OneMinusSrc, so the premultiplied colour below is exactly CSS's screen of a layer with alpha.
@fragment
fn TintFragment(Varying: VeilVarying) -> @location(0) vec4f
{
    let T = ResolveEllipseDistance(Varying.Uv, vec2f(0.5, 0.5));
    let Alpha = 0.55 * clamp((T - 0.40) / 0.60, 0.0, 1.0);
    return vec4f(vec3f(8.0, 20.0, 30.0) / 255.0 * Alpha, Alpha);
}

// 📝 App.tsx, scan mode: the dot matrix (24 px tiles, a dot of 1 px solid to 1.4 px clear, rgba(125,211,252,0.35), opacity 0.16, masked by
//    radial-gradient(ellipse at 50% 45%, black 25%, transparent 72%)) and the sweeping line (one pixel, a gradient across, a 14 px glow).
@fragment
fn MatrixFragment(Varying: VeilVarying) -> @location(0) vec4f
{
    let Pixel  = Varying.Position.xy;
    let Extent = Frame.ScreenExtent;
    let Tile   = Pixel - floor(Pixel / 24.0) * 24.0;
    let Dot    = 0.35 * (1.0 - clamp((distance(Tile, vec2f(12.0)) - 1.0) / 0.4, 0.0, 1.0));
    let Mask   = 1.0 - clamp((ResolveEllipseDistance(Varying.Uv, vec2f(0.5, 0.45)) - 0.25) / 0.47, 0.0, 1.0);
    var Colour = vec3f(125.0, 211.0, 252.0) / 255.0;
    var Alpha  = Dot * 0.16 * Mask;
    // The line: one device pixel at the animated top, its gradient 0 → 0.65 at 30 % → 0.65 at 70 % → 0 across the width.
    let Row    = floor(Scan.Front.w * Extent.y);
    let Across = Varying.Uv.x;
    let Profile = 0.65 * clamp(min(Across, 1.0 - Across) / 0.3, 0.0, 1.0);
    let Offset = Pixel.y - (Row + 0.5);
    let Line   = select(0.0, Profile, abs(Offset) < 0.5);
    let Glow   = 0.45 * 0.0570 * exp(-Offset * Offset / 98.0);
    let LineAlpha = Line + (1.0 - Line) * Glow;
    Alpha = Alpha + LineAlpha * (1.0 - Alpha);
    return vec4f(Colour, Alpha);
}

// 📝 The HUD canvas, straight alpha: tags beneath the vignette, the frame and labels above the dot matrix.
@fragment
fn TagsFragment(Varying: VeilVarying) -> @location(0) vec4f
{
    return textureSampleLevel(HudTexture, HudSampler, vec2f(Varying.Uv.x, Varying.Uv.y * 0.5), 0.0);
}

@fragment
fn FrameFragment(Varying: VeilVarying) -> @location(0) vec4f
{
    return textureSampleLevel(HudTexture, HudSampler, vec2f(Varying.Uv.x, 0.5 + Varying.Uv.y * 0.5), 0.0);
}
