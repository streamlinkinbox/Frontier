//============================================================================================================================================
//                                                            SMOKELATTICE.WGSL
//============================================================================================================================================
// 🧩 Bindings and helpers every smoke transport program shares: the tick record, obstacles, brick flags, the coarse flow.

// 📝 Group 0 is the tick (record at a dynamic offset, one per tick in a frame), group 1 the scene distance field,
//    group 2 the coarse flow as an (In, Out) pair of buffers whose roles alternate by tick, group 3 is per program.
//    The brick flags and the summary live in the programs' own group 3, which keeps the pressure programs (three
//    buffers of their own) within the default eight storage buffers per stage.

//! include "SmokeRecords.wgsl"
//! include "DistanceTraversal.wgsl"
//! include "SkyProjection.wgsl"

@group(0) @binding(0) var<uniform> Smoke                    : SmokeRecord;
@group(0) @binding(1) var<storage, read_write> Obstacles    : array<vec4f>;   // xyz solid velocity [m/s], w solid
@group(0) @binding(2) var RepeatSampler                     : sampler;
@group(0) @binding(3) var SkyView                           : texture_2d<f32>;
@group(0) @binding(4) var SkySampler                        : sampler;

@group(2) @binding(0) var<storage, read> VelocityIn         : array<vec4f>;   // xyz velocity [m/s], w heat [K]
@group(2) @binding(1) var<storage, read_write> VelocityOut  : array<vec4f>;

//------------------------------------------------------------------------------------------------------------------------
//                                                  COARSE LATTICE
//------------------------------------------------------------------------------------------------------------------------

fn ResolveCoarseExtent() -> vec3i
{
    return vec3i(Smoke.FineExtent / 2u);
}

fn ResolveCoarseOrigin() -> vec3i
{
    return Smoke.FineOrigin / 2;
}

fn ResolveCoarseSlot(WorldCell: vec3i) -> u32
{
    let Extent = ResolveCoarseExtent();
    let Wrapped = WrapLattice(WorldCell, Extent);
    return u32((Wrapped.z * Extent.y + Wrapped.y) * Extent.x + Wrapped.x);
}

fn ResolveCoarseCentre(WorldCell: vec3i) -> vec3f
{
    return (vec3f(WorldCell) + 0.5) * Smoke.CoarseCell;
}

// 📝 The outermost layer of either window is held at zero, so a hardware-filtered tap at the edge never blends in
//    the cells the toroidal wrap puts on the far side.
fn ClassifyCoarseShell(DomainCell: vec3i) -> bool
{
    return any(DomainCell <= vec3i(0)) || any(DomainCell >= ResolveCoarseExtent() - vec3i(1));
}

fn ClassifyPriorCoarse(WorldCell: vec3i) -> bool
{
    let Local = WorldCell - Smoke.PriorFineOrigin / 2;
    return all(Local >= vec3i(0)) && all(Local < ResolveCoarseExtent());
}

fn ClassifyCoarseInside(WorldCell: vec3i) -> bool
{
    let Local = WorldCell - ResolveCoarseOrigin();
    return all(Local >= vec3i(0)) && all(Local < ResolveCoarseExtent());
}

// 📐 Trilinear coarse flow from VelocityIn, the taps clamped into the window (whose shell is still air).
fn SampleCoarseFlow(Position: vec3f) -> vec4f
{
    let Continuous = Position / Smoke.CoarseCell - 0.5;
    let Base = vec3i(floor(Continuous));
    let Fraction = Continuous - vec3f(Base);
    let Lowest = ResolveCoarseOrigin();
    let Highest = Lowest + ResolveCoarseExtent() - vec3i(1);
    var Sum = vec4f(0.0);
    for (var Corner = 0u; Corner < 8u; Corner++)
    {
        let Offset = vec3u(Corner & 1u, (Corner >> 1u) & 1u, Corner >> 2u);
        let Cell = clamp(Base + vec3i(Offset), Lowest, Highest);
        let Weight = select(vec3f(1.0) - Fraction, Fraction, Offset == vec3u(1u));
        Sum += VelocityIn[ResolveCoarseSlot(Cell)] * (Weight.x * Weight.y * Weight.z);
    }
    return Sum;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SOURCES
//------------------------------------------------------------------------------------------------------------------------

// 📐 Gaussian footprint of one swept source at a point: the path is a capsule from Start to End, so a fast tyre lays
//    a continuous trail rather than separated puffs; its strength was normalised on the host by the path length.
fn ResolveSourceWeight(Source: SmokeSource, Position: vec3f, RadiusFloor: f32) -> f32
{
    let Radius = max(Source.Start.w, RadiusFloor);
    let Segment = Source.End.xyz - Source.Start.xyz;
    let Along = clamp(dot(Position - Source.Start.xyz, Segment) / max(dot(Segment, Segment), 1.0e-6), 0.0, 1.0);
    let Offset = (Position - (Source.Start.xyz + Segment * Along)) / Radius;
    let Squared = dot(Offset, Offset);
    return select(exp(-1.7 * Squared), 0.0, Squared > 12.25);
}

fn ClassifySourceReach(Minimum: vec3f, Maximum: vec3f) -> bool
{
    return Smoke.SourceCount > 0u && all(Maximum >= Smoke.SourceMinimum) && all(Minimum <= Smoke.SourceMaximum);
}
