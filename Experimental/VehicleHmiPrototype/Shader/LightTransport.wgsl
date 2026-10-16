// Rewritten local-light transport. One photometric/attenuation/shadow function
// is used by screen direct, card direct (thus multi-bounce GI) and mirrors.
//! include "FrameDescriptor.wgsl"
//! include "DistanceTraversal.wgsl"
//! include "LightRecords.wgsl"

@group(2) @binding(0) var<storage, read> LocalLights : array<LocalLight>;
@group(2) @binding(1) var<storage, read> ClusterMembers : array<u32>;
@group(2) @binding(2) var<uniform> Cluster : ClusterSettings;
@group(2) @binding(3) var<storage, read> Photometry : array<f32>;

struct LocalArrival
{
    Direction : vec3f,   // surface → fixture
    Illuminance : vec3f, // normal-incidence irradiance, after visibility
}

fn LoadPhotometric(Profile: u32, Azimuth: u32, Polar: u32) -> f32
{
    let Stride = PhotometryAzimuthBins * PhotometryPolarBins;
    return Photometry[Profile * Stride + Azimuth * PhotometryPolarBins + Polar];
}

// Type C polar angle is measured FROM the optical axis; C0 is Up around it.
// Bilinear reconstruction removes banding at the cutoff and round the fixture.
fn SamplePhotometric(Light: LocalLight, FromFixture: vec3f) -> f32
{
    let Profile = u32(Light.ColourProfile.w + 0.5);
    if (Profile == 0u) { return 1.0; }
    let Axis = Light.AxisInner.xyz;
    let Right = normalize(cross(Axis, Light.UpOuter.xyz));
    let Up = cross(Right, Axis);
    let Polar = acos(clamp(dot(FromFixture, Axis), -1.0, 1.0)) * (f32(PhotometryPolarBins - 1u) / Pi);
    var Angle = atan2(dot(FromFixture, Right), dot(FromFixture, Up));
    if (Angle < 0.0) { Angle += 2.0 * Pi; }
    let Azimuth = Angle * (f32(PhotometryAzimuthBins - 1u) / (2.0 * Pi));
    let H0 = min(u32(floor(Azimuth)), PhotometryAzimuthBins - 2u);
    let V0 = min(u32(floor(Polar)), PhotometryPolarBins - 2u);
    let H = clamp(Azimuth - f32(H0), 0.0, 1.0);
    let V = clamp(Polar - f32(V0), 0.0, 1.0);
    let A = mix(LoadPhotometric(Profile, H0, V0), LoadPhotometric(Profile, H0, V0 + 1u), V);
    let B = mix(LoadPhotometric(Profile, H0 + 1u, V0), LoadPhotometric(Profile, H0 + 1u, V0 + 1u), V);
    return mix(A, B, H);
}

fn ResolveLocalArrival(Position: vec3f, Normal: vec3f, Index: u32, CastShadow: bool) -> LocalArrival
{
    var Result: LocalArrival;
    Result.Direction = vec3f(0.0, 0.0, 1.0);
    Result.Illuminance = vec3f(0.0);
    let Light = LocalLights[Index];
    let Toward = Light.PositionReach.xyz - Position;
    let Squared = dot(Toward, Toward);
    let Reach = Light.PositionReach.w;
    if (Reach <= 0.0 || Squared >= Reach * Reach) { return Result; }
    let Range = sqrt(Squared);
    Result.Direction = Toward / max(Range, 0.0001);
    if (dot(Normal, Result.Direction) <= 0.0) { return Result; }

    let FromFixture = -Result.Direction;
    var Angular = 1.0;
    if (Light.UpOuter.w > -0.999)
    {
        let Cosine = dot(Light.AxisInner.xyz, FromFixture);
        if (Cosine <= Light.UpOuter.w) { return Result; }
        Angular = smoothstep(Light.UpOuter.w, Light.AxisInner.w, Cosine)
                * SamplePhotometric(Light, FromFixture);
    }
    let Window = 1.0 - Squared / (Reach * Reach);
    Result.Illuminance = Light.ColourProfile.rgb * Frame.EmissionScale
        * (Angular * Window * Window / max(Squared, 0.16));
    // Do not march expensive shadows for an imperceptible tail of a light.
    if (CastShadow && FeatureEnabled(Frame, FeatureDirectShadow)
        && dot(Result.Illuminance, LuminanceRec709) > 0.002)
    {
        let Bias = ResolveSurfaceBias(Position);
        let Stop = max(Range - max(0.25, 1.5 * Bias), 0.0); // don't intersect the luminous die/casing
        if (Stop > 1.5 * Bias)
        {
            let Visibility = TraverseSceneShadow(Position + Normal * Bias, Result.Direction, 0.5 * Bias, Stop, 5.0);
            Result.Illuminance *= Visibility;
        }
    }
    return Result;
}

// Only the primary camera uses clustered membership. Card texels are in world
// space and mirrors have different view projections: both traverse all lights.
fn ResolveClusterIndex(Pixel: vec2u, Position: vec3f) -> u32
{
    let Depth = ResolveViewDepth(Position);
    if (Depth < Cluster.DepthRange.x || Depth >= Cluster.DepthRange.y) { return 0xFFFFFFFFu; }
    let Slice = min(u32(max(0.0, floor(log(Depth / Cluster.DepthRange.x) * Cluster.LogScale))), Cluster.SliceCount - 1u);
    let Tile = min(Pixel / ClusterTilePixels, Cluster.TileCount - vec2u(1u));
    return (Slice * Cluster.TileCount.y + Tile.y) * Cluster.TileCount.x + Tile.x;
}
