//============================================================================================================================================
//                                                           DIRECTRESERVOIR.WGSL
//============================================================================================================================================
// 🧩 The ReSTIR DI reservoir: one light sample (a direction, a point light, or a point on an emissive triangle), its
//    weight W, its tally M and the target value it was last evaluated at.

// 📝 A sample is stored by identity, not by cached radiance: a directional sample as its direction, an emissive sample
//    as its triangle, its occupant and its barycentric coordinates, so it is rebuilt from the live transform wherever
//    it is reused and the target function is always evaluated afresh at the receiving surface.

//! include "LightSampling.wgsl"

struct DirectRecord
{
    Packed    : u32,   // kind << 30 | point-light index
    Pair      : u32,   // directional: octahedral snorm16 pair; emissive: unorm16 barycentric pair
    Weight    : f32,   // W
    Tally     : f32,   // M
    TargetPdf : f32,   // p̂ at the pixel that stored it
    Occupant  : u32,   // emissive: carrying occupant
    Triangle  : u32,   // emissive: global triangle
    Pad       : u32,
}

struct DirectReservoir
{
    Kind       : u32,
    Index      : u32,
    Triangle   : u32,
    Occupant   : u32,
    Pair       : u32,
    SumWeights : f32,
    Tally      : f32,
    TargetPdf  : f32,
    Weight     : f32,
}

fn ConstructEmptyDirect() -> DirectReservoir
{
    var Reservoir: DirectReservoir;
    Reservoir.Kind = LightKindSky;
    Reservoir.Index = 0u;
    Reservoir.Triangle = 0u;
    Reservoir.Occupant = 0u;
    Reservoir.Pair = 0u;
    Reservoir.SumWeights = 0.0;
    Reservoir.Tally = 0.0;
    Reservoir.TargetPdf = 0.0;
    Reservoir.Weight = 0.0;
    return Reservoir;
}

fn PackDirect(Reservoir: DirectReservoir) -> DirectRecord
{
    var Record: DirectRecord;
    Record.Packed = (Reservoir.Kind << 30u) | (Reservoir.Index & 0x3FFFFFFFu);
    Record.Pair = Reservoir.Pair;
    Record.Weight = Reservoir.Weight;
    Record.Tally = Reservoir.Tally;
    Record.TargetPdf = Reservoir.TargetPdf;
    Record.Occupant = Reservoir.Occupant;
    Record.Triangle = Reservoir.Triangle;
    Record.Pad = 0u;
    return Record;
}

fn UnpackDirect(Record: DirectRecord) -> DirectReservoir
{
    var Reservoir: DirectReservoir;
    Reservoir.Kind = Record.Packed >> 30u;
    Reservoir.Index = Record.Packed & 0x3FFFFFFFu;
    Reservoir.Pair = Record.Pair;
    Reservoir.Weight = Record.Weight;
    Reservoir.Tally = Record.Tally;
    Reservoir.TargetPdf = Record.TargetPdf;
    Reservoir.Occupant = Record.Occupant;
    Reservoir.Triangle = Record.Triangle;
    Reservoir.SumWeights = 0.0;
    return Reservoir;
}

// 📝 Stores a light sample's identity into a reservoir (weights untouched).
fn AssignDirectSample(Reservoir: ptr<function, DirectReservoir>, Sample: LightSample)
{
    (*Reservoir).Kind = Sample.Kind;
    (*Reservoir).Index = Sample.Index;
    (*Reservoir).Triangle = Sample.Triangle;
    (*Reservoir).Occupant = Sample.Occupant;
    if (Sample.Kind == LightKindEmissive)
    {
        (*Reservoir).Pair = pack2x16unorm(clamp(Sample.Barycentric, vec2f(0.0), vec2f(1.0)));
    }
    else
    {
        (*Reservoir).Pair = pack2x16snorm(EncodeOctahedral(Sample.Direction));
    }
}

// 📝 Rebuilds the stored sample for a receiving surface.
fn ReconstructDirectSample(Reservoir: DirectReservoir, Position: vec3f, Normal: vec3f) -> LightSample
{
    switch (Reservoir.Kind)
    {
        case 0u, 1u:
        {
            let Direction = DecodeOctahedral(unpack2x16snorm(Reservoir.Pair));
            return ConstructDirectionalSample(Direction, Position, Normal, Reservoir.Kind);
        }
        case 2u:
        {
            return ConstructPointSample(Reservoir.Index, Position);
        }
        default:
        {
            return ConstructTrianglePoint(Reservoir.Triangle, Reservoir.Occupant, unpack2x16unorm(Reservoir.Pair), Position);
        }
    }
}

// 📝 The target function: luminance of the unshadowed contribution f·cos·L at the surface.
fn ResolveDirectTarget(Sample: LightSample, Surface: SurfaceSample, Normal: vec3f, View: vec3f) -> f32
{
    if (!Sample.Valid || dot(Normal, Sample.Direction) <= 0.0)
    {
        return 0.0;
    }
    return ResolveSplitLuminance(EvaluateLightSplit(Sample, Surface, Normal, View), Surface);
}

// 📝 Weighted reservoir sampling step: the candidate replaces the kept sample with probability w / Σw.
fn StreamDirect(Reservoir: ptr<function, DirectReservoir>, Candidate: DirectReservoir, Weight: f32, Tally: f32, TargetPdf: f32, Random: f32) -> bool
{
    (*Reservoir).SumWeights += Weight;
    (*Reservoir).Tally += Tally;
    if (Weight > 0.0 && Random * (*Reservoir).SumWeights <= Weight)
    {
        (*Reservoir).Kind = Candidate.Kind;
        (*Reservoir).Index = Candidate.Index;
        (*Reservoir).Triangle = Candidate.Triangle;
        (*Reservoir).Occupant = Candidate.Occupant;
        (*Reservoir).Pair = Candidate.Pair;
        (*Reservoir).TargetPdf = TargetPdf;
        return true;
    }
    return false;
}

fn FinaliseDirect(Reservoir: ptr<function, DirectReservoir>)
{
    (*Reservoir).Weight = select(0.0, (*Reservoir).SumWeights / (*Reservoir).TargetPdf, (*Reservoir).TargetPdf > 0.0);
}

// 📝 Pairwise MIS canonical weight share for one neighbour (Bitterli 2020 §4 / Lin 2022): with k neighbours,
//    m_i(y) = M_i p̂_i(y) / (M_i p̂_i(y) + (M_c / k) p̂_c(y)) and the canonical sample collects the complements.
fn ResolvePairwiseNeighbour(NeighbourTally: f32, NeighbourTarget: f32, CanonicalTally: f32, CanonicalTarget: f32, Count: f32) -> f32
{
    let Denominator = NeighbourTally * NeighbourTarget + (CanonicalTally / Count) * CanonicalTarget;
    return select(0.0, NeighbourTally * NeighbourTarget / Denominator, Denominator > 0.0);
}

fn ResolvePairwiseCanonical(NeighbourTally: f32, NeighbourTarget: f32, CanonicalTally: f32, CanonicalTarget: f32, Count: f32) -> f32
{
    let Denominator = NeighbourTally * NeighbourTarget + (CanonicalTally / Count) * CanonicalTarget;
    return select(1.0, (CanonicalTally / Count) * CanonicalTarget / Denominator, Denominator > 0.0);
}

// 📝 The surface at a trace pixel, from the guide (normal, view depth, validity) and position (world, family) textures.
struct PrimarySurface
{
    Valid    : bool,
    Position : vec3f,
    Normal   : vec3f,
    Family   : u32,
    ViewDepth : f32,
    ExpectedDepth : f32,
}

fn DecodePrimarySurface(Guide: vec4f, Position: vec4f) -> PrimarySurface
{
    var Surface: PrimarySurface;
    Surface.Valid = Guide.w >= 0.5;
    Surface.Position = Position.xyz;
    Surface.Normal = DecodeOctahedral(Guide.xy);
    Surface.Family = u32(max(Position.w, 0.0) + 0.5);
    Surface.ViewDepth = Guide.z;
    Surface.ExpectedDepth = Guide.w - 1.0;
    return Surface;
}

fn ResolveScreenPixel(TracePixel: vec2u) -> vec2i
{
    return min(vec2i(TracePixel * Frame.TraceScale), vec2i(Frame.ScreenExtent) - vec2i(1));
}

// 📝 Similarity of two primary surfaces for reuse: facing alike, at comparable depth.
fn ResolveSimilar(Normal: vec3f, ViewDepth: f32, OtherNormal: vec3f, OtherDepth: f32) -> bool
{
    return dot(Normal, OtherNormal) > 0.85 && abs(OtherDepth - ViewDepth) < 0.08 * ViewDepth + 0.03;
}
