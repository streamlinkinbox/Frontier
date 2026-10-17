//============================================================================================================================================
//                                                            TRACERECORDS.WGSL
//============================================================================================================================================
// 🧩 The ray-tracing scene at group 1 — BVH nodes, triangles, instances, the light table, the materials and the sky —
//    shared by every path-tracing program (TraceStructure.js builds and uploads it).

//! include "FrameDescriptor.wgsl"
//! include "PackingCodec.wgsl"
//! include "SkyProjection.wgsl"
//! include "ClusteredLights.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "DiscrepancySequence.wgsl"

struct TraceRecord
{
    TlasRoot              : u32,   // [-]  node index of the instance hierarchy's root
    InstanceCount         : u32,   // [-]
    PointCount            : u32,   // [-]  clustered point lights leading the light table
    EmissiveCount         : u32,   // [-]  emissive-triangle entries following them
    SunSolidAngle         : f32,   // [sr]
    SunCosine             : f32,   // [-]  cosine of the disc's angular radius
    EmissivePower         : f32,   // [-]  Σ power of the emissive entries
    SunAbove              : u32,   // [-]  1 when the sun is above the horizon
    PathBounces           : u32,   // [-]  surface vertices after the primary
    DirectCandidates      : u32,   // [-]  light candidates per pixel (ReSTIR DI)
    DirectSpatialCount    : u32,   // [-]
    PathSpatialCount      : u32,   // [-]
    DirectSpatialRadius   : f32,   // [px] trace pixels
    PathSpatialRadius     : f32,   // [px]
    DirectTallyLimit      : f32,   // [-]  M clamp
    PathTallyLimit        : f32,   // [-]
    ReconnectionRoughness : f32,   // [-]  lobe roughness a reconnection vertex needs on both sides
    ReconnectionDistance  : f32,   // [m]  shortest segment a reconnection may span
    FireflyClamp          : f32,   // [-]  luminance clamp of a single path's contribution (0 = none)
    Flags                 : u32,   // [-]  TraceFlag*
    FrameIndex            : u32,   // [-]
    SpecularLobeFloor     : f32,   // [-]
    EmissiveCapacity      : u32,   // [-]
    MaxPointLights        : u32,   // [-]  LightRecord slots in the light table
    Reserved              : array<vec4u, 2>,
}

const TraceFlagReplay         : u32 = 1u;    // [-] random-replay shifts for paths whose reconnection is past x1
const TraceFlagPairwise       : u32 = 2u;    // [-] pairwise MIS (else 1/M-style constant weights)
const TraceFlagBsdfCandidate  : u32 = 4u;    // [-] a BSDF-sampled candidate joins the DI candidates
const TraceFlagMisVisibility  : u32 = 8u;    // [-] shifted paths trace their visibility
const TraceFlagRussianRoulette: u32 = 16u;
const TraceFlagReflection     : u32 = 32u;   // [-] specular lobes sampled at the primary vertex

struct BvhNode
{
    Minimum     : vec3f,
    LeftOrFirst : u32,     // interior: left child (right at +1); leaf: first primitive
    Maximum     : vec3f,
    Count       : u32,     // 0 interior, else leaf primitive count
}

struct TriangleRecord
{
    V0     : vec3f,
    Packed : u32,    // family | occupant offset << 16
    E1     : vec3f,
    N0     : u32,    // octahedral corner normals
    E2     : vec3f,
    N1     : u32,
    N2     : u32,
    Source : u32,    // authored facet index
    Pad0   : u32,
    Pad1   : u32,
}

struct InstanceRecord
{
    WorldToLocal : mat4x4f,
    BlasRoot     : u32,
    Occupant     : u32,
    Flags        : u32,
    Pad          : u32,
}

const InstanceFlagVehicle  : u32 = 1u;
const InstanceFlagSkipTyre : u32 = 2u;
const InstanceFlagFragment : u32 = 4u;

struct EmissiveEntry
{
    Occupant : u32,
    Triangle : u32,
    Cdf      : f32,
    Power    : f32,
}

@group(1) @binding(0) var<uniform> Trace                 : TraceRecord;
@group(1) @binding(1) var<uniform> Materials             : array<MaterialRecord, 128>;
@group(1) @binding(2) var<storage, read> Nodes           : array<BvhNode>;
@group(1) @binding(3) var<storage, read> Triangles       : array<TriangleRecord>;
@group(1) @binding(4) var<storage, read> Instances       : array<InstanceRecord>;
@group(1) @binding(5) var<storage, read> LightTable      : array<vec4u>;
@group(1) @binding(6) var<storage, read> Occupants       : array<OccupantRecord>;
@group(1) @binding(7) var SkyView                        : texture_2d<f32>;
@group(1) @binding(8) var SkySampler                     : sampler;

const TraceGlassFamily : u32 = 7u;   // [-] CourseMaterial.VehicleGlass: rays pass through
const TraceTyreFamily  : u32 = 9u;   // [-] CourseMaterial.Tyre: gone on a shredded wheel

fn FetchMaterial(Family: u32) -> MaterialRecord
{
    return Materials[min(Family, 127u)];
}

fn FetchPointLight(Index: u32) -> LightRecord
{
    var Light: LightRecord;
    let Base = Index * 4u;
    Light.PositionType    = bitcast<vec4f>(LightTable[Base]);
    Light.DirectionReach  = bitcast<vec4f>(LightTable[Base + 1u]);
    Light.ColourIntensity = bitcast<vec4f>(LightTable[Base + 2u]);
    Light.Params          = bitcast<vec4f>(LightTable[Base + 3u]);
    return Light;
}

fn FetchEmissive(Index: u32) -> EmissiveEntry
{
    let Words = LightTable[Trace.MaxPointLights * 4u + Index];
    var Entry: EmissiveEntry;
    Entry.Occupant = Words.x;
    Entry.Triangle = Words.y;
    Entry.Cdf      = bitcast<f32>(Words.z);
    Entry.Power    = bitcast<f32>(Words.w);
    return Entry;
}

fn SampleSkyRadiance(Direction: vec3f) -> vec3f
{
    let Coordinates = ProjectSkyCoordinates(normalize(Direction));
    return textureSampleLevel(SkyView, SkySampler, Coordinates, 0.0).rgb * Frame.SkyScale + Frame.NightSkyRadiance;
}

// 📝 The solar disc's radiance: illuminance spread over its solid angle (the sky view excludes the disc).
fn ResolveSunRadiance() -> vec3f
{
    return Frame.SunIlluminance / max(Trace.SunSolidAngle, 1.0e-8);
}

fn InsideSunDisc(Direction: vec3f) -> bool
{
    return Trace.SunAbove != 0u && dot(Direction, Frame.SunDirection) >= Trace.SunCosine;
}

// 📝 Radiance an escaped ray sees: the sky view plus the solar disc when the direction falls inside it.
fn ResolveEscapeRadiance(Direction: vec3f) -> vec3f
{
    var Radiance = SampleSkyRadiance(Direction);
    if (InsideSunDisc(Direction))
    {
        Radiance += ResolveSunRadiance();
    }
    return Radiance;
}

fn TraceFlag(Bit: u32) -> bool
{
    return (Trace.Flags & Bit) != 0u;
}

// 📐 A stateless stream: the n-th dimension of the b-th bounce of a path seeded s, so a path can be replayed exactly.
fn PathRandom(Seed: u32, Bounce: u32, Dimension: u32) -> f32
{
    let Mixed = PermuteUnsigned(Seed ^ PermuteUnsigned(Bounce * 0x9E3779B9u + Dimension * 0x85EBCA6Bu + 0x27D4EB2Fu));
    return f32(Mixed >> 8u) * (1.0 / 16777216.0);
}

fn PathRandomTriple(Seed: u32, Bounce: u32, Dimension: u32) -> vec3f
{
    return vec3f(PathRandom(Seed, Bounce, Dimension), PathRandom(Seed, Bounce, Dimension + 1u), PathRandom(Seed, Bounce, Dimension + 2u));
}
