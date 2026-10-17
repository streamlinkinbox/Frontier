//============================================================================================================================================
//                                                           PATHRESERVOIRS.WGSL
//============================================================================================================================================
// 🧩 ReSTIR PT: the path reservoir, the path tracer that streams every light-carrying path it finds into it, and the
//    hybrid shift mapping (reconnection where the geometry allows it, random replay where it does not) that moves a
//    stored path to another pixel's primary vertex for temporal and spatial reuse (Lin et al. 2022).

// 📝 A path sample is kept as what is needed to re-evaluate it elsewhere, not as a colour:
//      · its seed, so the bounces before the reconnection vertex can be replayed from the same random numbers;
//      · the reconnection vertex x_k — position, normal, occupant (for motion), material — or the light vertex when
//        the path reconnects straight to it, or a direction when that light is the sun or the sky;
//      · the outgoing direction at x_k and the radiance L_out arriving along it, with the light pdf needed to redo
//        the MIS weight when x_{k+1} is itself the light (its BSDF pdf changes with the new incoming direction);
//      · the direction and the "rest" (everything past the primary BSDF) at the pixel that holds it, so the final
//        resolve can split the primary reflectance into diffuse and specular without tracing again.
//
//    Measures. The prefix x_1 … x_{k−1} lives in primary sample space (its f/p products are part of the integrand F,
//    Jacobian 1 under replay); x_k in area measure (a fixed point, Jacobian 1 under reconnection) or solid angle for
//    a fixed direction; x_{k+1} in solid angle about the fixed x_k. Everything past it is folded into L_out. In this
//    hybrid measure the initial weight is W = 1/p with p the pdf of only the non-replayed part, and p̂ = lum(F).
//
//    Direct light is NOT part of these paths — the ReSTIR DI passes own it — so no emission is counted at x_1 and no
//    escape to the sky at the first bounce; every path here has at least one interior vertex.
//
//    Reconnection criterion (material-based, so it is a function of the path and not of the lobe that was drawn):
//    the segment x_{k−1} → x_k is at least ReconnectionDistance long, and x_{k−1} (and x_k, when it is a surface)
//    reflect mostly through lobes rougher than ReconnectionRoughness. A shift is rejected when the criterion would
//    have fired earlier on the shifted path or fails where the base path reconnected, keeping the mapping invertible.

//! include "LightSampling.wgsl"

const PathFlagNee           : u32 = 65536u;     // bit 16: the terminal light vertex was light-sampled (else BSDF-hit)
const PathFlagRcLight       : u32 = 131072u;    // bit 17: the reconnection vertex is the light vertex
const PathFlagRcDirectional : u32 = 262144u;    // bit 18: … and it is a direction (sun / sky); RcPosition holds it
const PathFlagOutTerminal   : u32 = 524288u;    // bit 19: x_{k+1} is the light vertex: redo its MIS weight
const PathFlagOutDelta      : u32 = 1048576u;   // bit 20: … and it is a point light (no MIS)
const PathFlagRcDelta       : u32 = 2097152u;   // bit 21: the reconnection light vertex is a point light (RcIdentity)

struct PathSample
{
    Seed       : u32,
    Rc         : u32,     // reconnection vertex index k (0: none, full replay)
    Terminal   : u32,     // index j of the last surface vertex before the light event
    Flags      : u32,
    RcIdentity : u32,     // occupant of x_k (surface or emissive point) or point-light index
    RcFamily   : u32,
    RcPosition : vec3f,   // or the direction for a directional light vertex
    RcNormal   : vec3f,
    RcOut      : vec3f,
    Radiance   : vec3f,   // L_out along RcOut, or L_e of the reconnection light vertex
    LightPdf   : f32,     // out-terminal: solid-angle light pdf at x_k; emissive rc light: its area pdf
    Rest       : vec3f,   // at the holding pixel: F without the primary reflectance
    Omega      : vec3f,   // at the holding pixel: the first bounce direction
}

struct PathReservoir
{
    Sample     : PathSample,
    SumWeights : f32,
    Tally      : f32,
    TargetPdf  : f32,
    Weight     : f32,
}

struct PathRecord   // 96 B
{
    Seed       : u32,
    Flags      : u32,    // Rc | Terminal << 8 | flag bits
    RcIdentity : u32,
    RcFamily   : u32,
    RcPosition : vec3f,
    RcNormal   : u32,
    RcOut      : u32,
    Omega      : u32,
    LightPdf   : f32,
    Weight     : f32,
    // 📝 Radiance and Rest stay f32: the solar disc's radiance (illuminance / 6.9e-5 sr) and the hybrid-measure
    //    throughput (the ordinary estimate × the non-replayed pdf) overflow half floats.
    Radiance   : vec3f,
    Tally      : f32,
    Rest       : vec3f,
    TargetPdf  : f32,
    Pad0       : u32,
    Pad1       : u32,
    Pad2       : u32,
    Pad3       : u32,
}

struct PathVertex
{
    Position : vec3f,
    Normal   : vec3f,
    Family   : u32,
    Surface  : SurfaceSample,
    View     : vec3f,   // [-] toward the previous vertex
}

struct ShiftResult
{
    Valid  : bool,
    Target : f32,
    Rest   : vec3f,
    Omega  : vec3f,
}

fn ConstructEmptyPathSample() -> PathSample
{
    var Sample: PathSample;
    Sample.Seed = 0u;
    Sample.Rc = 0u;
    Sample.Terminal = 0u;
    Sample.Flags = 0u;
    Sample.RcIdentity = 0u;
    Sample.RcFamily = 0u;
    Sample.RcPosition = vec3f(0.0);
    Sample.RcNormal = vec3f(0.0, 0.0, 1.0);
    Sample.RcOut = vec3f(0.0, 0.0, 1.0);
    Sample.Radiance = vec3f(0.0);
    Sample.LightPdf = 0.0;
    Sample.Rest = vec3f(0.0);
    Sample.Omega = vec3f(0.0, 0.0, 1.0);
    return Sample;
}

fn ConstructEmptyPathReservoir() -> PathReservoir
{
    var Reservoir: PathReservoir;
    Reservoir.Sample = ConstructEmptyPathSample();
    Reservoir.SumWeights = 0.0;
    Reservoir.Tally = 0.0;
    Reservoir.TargetPdf = 0.0;
    Reservoir.Weight = 0.0;
    return Reservoir;
}

fn PackPath(Reservoir: PathReservoir) -> PathRecord
{
    let S = Reservoir.Sample;
    var Record: PathRecord;
    Record.Seed = S.Seed;
    Record.Flags = (S.Rc & 0xFFu) | ((S.Terminal & 0xFFu) << 8u) | (S.Flags & 0xFFFF0000u);
    Record.RcIdentity = S.RcIdentity;
    Record.RcFamily = S.RcFamily;
    Record.RcPosition = S.RcPosition;
    Record.RcNormal = PackOctahedral(S.RcNormal);
    Record.RcOut = PackOctahedral(S.RcOut);
    Record.Omega = PackOctahedral(S.Omega);
    Record.Radiance = S.Radiance;
    Record.Rest = S.Rest;
    Record.LightPdf = S.LightPdf;
    Record.Weight = Reservoir.Weight;
    Record.Tally = Reservoir.Tally;
    Record.TargetPdf = Reservoir.TargetPdf;
    Record.Pad0 = 0u;
    Record.Pad1 = 0u;
    Record.Pad2 = 0u;
    Record.Pad3 = 0u;
    return Record;
}

fn UnpackPath(Record: PathRecord) -> PathReservoir
{
    var Reservoir = ConstructEmptyPathReservoir();
    Reservoir.Sample.Seed = Record.Seed;
    Reservoir.Sample.Rc = Record.Flags & 0xFFu;
    Reservoir.Sample.Terminal = (Record.Flags >> 8u) & 0xFFu;
    Reservoir.Sample.Flags = Record.Flags & 0xFFFF0000u;
    Reservoir.Sample.RcIdentity = Record.RcIdentity;
    Reservoir.Sample.RcFamily = Record.RcFamily;
    Reservoir.Sample.RcPosition = Record.RcPosition;
    Reservoir.Sample.RcNormal = UnpackOctahedral(Record.RcNormal);
    Reservoir.Sample.RcOut = UnpackOctahedral(Record.RcOut);
    Reservoir.Sample.Omega = UnpackOctahedral(Record.Omega);
    Reservoir.Sample.Radiance = Record.Radiance;
    Reservoir.Sample.Rest = Record.Rest;
    Reservoir.Sample.LightPdf = Record.LightPdf;
    Reservoir.Weight = Record.Weight;
    Reservoir.Tally = Record.Tally;
    Reservoir.TargetPdf = Record.TargetPdf;
    if (!IsFiniteScalar(Reservoir.Weight) || !IsFiniteScalar(Reservoir.TargetPdf) || !IsFiniteScalar(Reservoir.Tally))
    {
        Reservoir.Weight = 0.0;
        Reservoir.TargetPdf = 0.0;
        Reservoir.Tally = 0.0;
    }
    return Reservoir;
}

fn PathHasFlag(Sample: PathSample, Flag: u32) -> bool
{
    return (Sample.Flags & Flag) != 0u;
}

// 📝 Weighted reservoir sampling; the caller supplies w = m · p̂ · W (or lum(E) for an initial candidate).
fn StreamPath(Reservoir: ptr<function, PathReservoir>, Candidate: PathSample, Weight: f32, TargetPdf: f32, Random: f32) -> bool
{
    (*Reservoir).SumWeights += Weight;
    if (Weight > 0.0 && Random * (*Reservoir).SumWeights <= Weight)
    {
        (*Reservoir).Sample = Candidate;
        (*Reservoir).TargetPdf = TargetPdf;
        return true;
    }
    return false;
}

fn FinalisePath(Reservoir: ptr<function, PathReservoir>)
{
    let Weight = select(0.0, (*Reservoir).SumWeights / (*Reservoir).TargetPdf, (*Reservoir).TargetPdf > 0.0);
    let Sample = (*Reservoir).Sample;
    let Finite = IsFiniteScalar(Weight) && IsFiniteScalar((*Reservoir).TargetPdf) && all(Sample.Rest == Sample.Rest) && all(Sample.Radiance == Sample.Radiance);
    (*Reservoir).Weight = select(0.0, Weight, Finite);
}

// 📝 A reconnection vertex riding a moving occupant is carried from where it was last frame to where it is now.
fn CarryPathSample(Sample: PathSample) -> PathSample
{
    var Carried = Sample;
    if (PathHasFlag(Sample, PathFlagRcDirectional) || PathHasFlag(Sample, PathFlagRcDelta) || Sample.Rc == 0u)
    {
        return Carried;
    }
    if (Sample.RcIdentity < arrayLength(&Occupants))
    {
        let Occupant = Occupants[Sample.RcIdentity];
        if ((Occupant.Roles & RoleDynamic) != 0u)
        {
            let Local = Occupant.PreviousWorldToLocal * vec4f(Sample.RcPosition, 1.0);
            Carried.RcPosition = (Occupant.LocalToWorld * Local).xyz;
            let LocalNormal = (Occupant.PreviousWorldToLocal * vec4f(Sample.RcNormal, 0.0)).xyz;
            Carried.RcNormal = normalize((Occupant.LocalToWorld * vec4f(LocalNormal, 0.0)).xyz);
            let LocalOut = (Occupant.PreviousWorldToLocal * vec4f(Sample.RcOut, 0.0)).xyz;
            Carried.RcOut = normalize((Occupant.LocalToWorld * vec4f(LocalOut, 0.0)).xyz);
        }
    }
    return Carried;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHARED PIECES
//------------------------------------------------------------------------------------------------------------------------

fn ConstructVertexFromHit(Hit: TraceHit, Direction: vec3f) -> PathVertex
{
    var Vertex: PathVertex;
    Vertex.Position = Hit.Position;
    Vertex.Normal = Hit.Normal;
    Vertex.Family = Hit.Family;
    Vertex.Surface = ProjectSurfaceSample(FetchMaterial(Hit.Family));
    Vertex.View = -Direction;
    return Vertex;
}

fn ConstructPrimaryVertex(Position: vec3f, Normal: vec3f, Family: u32) -> PathVertex
{
    var Vertex: PathVertex;
    Vertex.Position = Position;
    Vertex.Normal = Normal;
    Vertex.Family = Family;
    Vertex.Surface = ProjectSurfaceSample(FetchMaterial(Family));
    Vertex.View = normalize(Frame.CameraPosition - Position);
    return Vertex;
}

fn AllowSpecularAt(Bounce: u32) -> bool
{
    return Bounce > 0u || TraceFlag(TraceFlagReflection);
}

// 📝 The reflectance evaluated at a vertex: at the primary, specular lobes only when they are sampled there.
fn EvaluateVertexReflectance(Vertex: PathVertex, Bounce: u32, Light: vec3f) -> vec3f
{
    if (AllowSpecularAt(Bounce))
    {
        return EvaluateReflectance(Vertex.Surface, Vertex.Normal, Vertex.View, Light);
    }
    return ResolveDiffuseAlbedo(Vertex.Surface) * EvaluateReflectanceSplit(Vertex.Surface, Vertex.Normal, Vertex.View, Light).Diffuse;
}

fn VertexReconnectable(Vertex: PathVertex) -> bool
{
    return ResolveReconnectable(Vertex.Surface, max(dot(Vertex.Normal, Vertex.View), 1.0e-4));
}

// 📝 The reconnection criterion for a segment between two surfaces, to a light point, or to a direction.
fn ReconnectCriterionSurface(From: PathVertex, To: PathVertex, Travel: f32) -> bool
{
    return Travel >= Trace.ReconnectionDistance && VertexReconnectable(From) && VertexReconnectable(To);
}

fn ReconnectCriterionLight(From: PathVertex, Travel: f32) -> bool
{
    return Travel >= Trace.ReconnectionDistance && VertexReconnectable(From);
}

// 📝 Russian roulette with a fixed schedule, so a replayed prefix takes the same decisions as the base path.
fn ResolveRouletteSurvival(Bounce: u32) -> f32
{
    if (!TraceFlag(TraceFlagRussianRoulette) || Bounce < 2u)
    {
        return 1.0;
    }
    return select(0.6, 0.8, Bounce == 2u);
}

fn ResolveMisWeight(Nee: bool, Delta: bool, LightPdf: f32, BsdfPdf: f32) -> f32
{
    if (Delta)
    {
        return 1.0;
    }
    let Sum = LightPdf + BsdfPdf;
    if (Sum <= 0.0)
    {
        return 0.0;
    }
    return select(BsdfPdf / Sum, LightPdf / Sum, Nee);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PATH TRACING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Traces one path from the primary vertex and streams every candidate (a NEE connection or an emitter hit at any
//    depth ≥ 1) into the reservoir with w = lum(E), E the candidate's ordinary estimate. Leaves the survivor's
//    Rest / Omega at this pixel in the sample and p̂ = lum(E)·p in the reservoir.
fn TracePath(Primary: PathVertex, Seed: u32, Reservoir: ptr<function, PathReservoir>, Stream: ptr<function, u32>)
{
    var Vertex = Primary;
    var Throughput = vec3f(1.0);            // Π f·cos/p from x_0 (ordinary estimate)
    var RestPrefix = vec3f(1.0);            // Π f·cos/p with the primary's f left out: 1/p_0 · Π_{b≥1} …
    var Omega1 = vec3f(0.0, 0.0, 1.0);
    var SinceRc = vec3f(1.0);               // Π f·cos/p over bounces after the reconnection vertex's own

    var Found = false;                      // a reconnection vertex is fixed
    var Rc = 0u;
    var RcVertex: PathVertex;
    var RcIdentity = 0u;
    var RcAreaPdf = 0.0;                    // p_b(ω_{k−1→k}) · cos_k / d²
    var RcOut = vec3f(0.0, 0.0, 1.0);
    var RcOutPdf = 0.0;                     // p_b(ω_out | ω_in) at x_k

    let Bounces = Trace.PathBounces;
    for (var Bounce = 0u; Bounce < Bounces; Bounce++)
    {
        // ① next-event estimation at x_b, b ≥ 1
        if (Bounce >= 1u)
        {
            let Random = PathRandomTriple(Seed, Bounce, 4u);
            let Extra = PathRandom(Seed, Bounce, 7u);
            let Light = SampleLight(Vertex.Position, Vertex.Normal, Random, Extra);
            if (Light.Valid && dot(Vertex.Normal, Light.Direction) > 0.0)
            {
                let Reflectance = EvaluateReflectance(Vertex.Surface, Vertex.Normal, Vertex.View, Light.Direction);
                if (ResolveLuminance(Reflectance) > 0.0 && ResolveLightVisible(Light, Vertex.Position, Vertex.Normal))
                {
                    let BsdfPdf = PdfReflectance(Vertex.Surface, Vertex.Normal, Vertex.View, Light.Direction, true) * Light.Geometry;
                    let Mis = ResolveMisWeight(true, Light.Delta, Light.Pdf, BsdfPdf);
                    let Arrival = Light.Radiance * Light.Geometry * (Mis / Light.Pdf);
                    let Estimate = Throughput * Reflectance * Arrival;
                    var Candidate = ConstructEmptyPathSample();
                    Candidate.Seed = Seed;
                    Candidate.Terminal = Bounce;
                    Candidate.Flags = PathFlagNee;
                    var Pdf = 1.0;
                    if (Found)
                    {
                        Candidate.Rc = Rc;
                        Candidate.RcPosition = RcVertex.Position;
                        Candidate.RcNormal = RcVertex.Normal;
                        Candidate.RcIdentity = RcIdentity;
                        Candidate.RcFamily = RcVertex.Family;
                        if (Rc == Bounce)
                        {
                            // the light is x_{k+1}: solid angle about x_k
                            Candidate.Flags |= PathFlagOutTerminal | select(0u, PathFlagOutDelta, Light.Delta);
                            Candidate.RcOut = Light.Direction;
                            Candidate.Radiance = Light.Radiance;
                            let SolidPdf = select(Light.Pdf, Light.Pdf / max(Light.Geometry, 1.0e-12), Light.Kind == LightKindEmissive);
                            Candidate.LightPdf = SolidPdf;
                            Pdf = RcAreaPdf * SolidPdf;
                        }
                        else
                        {
                            Candidate.RcOut = RcOut;
                            Candidate.Radiance = SinceRc * Reflectance * Arrival;
                            Pdf = RcAreaPdf * RcOutPdf;
                        }
                    }
                    else if (ReconnectCriterionLight(Vertex, Light.Distance))
                    {
                        // the light vertex is the reconnection vertex
                        Candidate.Rc = Bounce + 1u;
                        Candidate.Flags |= PathFlagRcLight;
                        if (Light.Kind == LightKindEmissive)
                        {
                            Candidate.RcPosition = Light.Position;
                            Candidate.RcNormal = Light.Normal;
                            Candidate.RcIdentity = Light.Occupant;
                            Candidate.RcFamily = 0u;
                            Candidate.Radiance = Light.Radiance;
                            Candidate.LightPdf = Light.Pdf;   // area
                        }
                        else if (Light.Delta)
                        {
                            Candidate.Flags |= PathFlagRcDelta;
                            Candidate.RcIdentity = Light.Index;
                            Candidate.RcPosition = Light.Position;
                            Candidate.Radiance = Light.Radiance;
                        }
                        else
                        {
                            Candidate.Flags |= PathFlagRcDirectional;
                            Candidate.RcPosition = Light.Direction;
                            Candidate.Radiance = Light.Radiance;
                        }
                        Pdf = Light.Pdf;
                    }
                    // 📝 Rest is F / f_0 in the hybrid measure: the ordinary estimate times the non-replayed pdf.
                    Candidate.Rest = RestPrefix * Reflectance * Arrival * Pdf;
                    Candidate.Omega = Omega1;
                    var Luminance = ResolveLuminance(Estimate);
                    if (Trace.FireflyClamp > 0.0 && Luminance > Trace.FireflyClamp)
                    {
                        Candidate.Rest *= Trace.FireflyClamp / Luminance;
                        Luminance = Trace.FireflyClamp;
                    }
                    StreamPath(Reservoir, Candidate, Luminance, Luminance * Pdf, AdvanceUniform(Stream));
                }
            }
        }

        // ② the BSDF bounce from x_b
        let Lobe = SampleReflectance(Vertex.Surface, Vertex.Normal, Vertex.View, PathRandomTriple(Seed, Bounce, 0u), AllowSpecularAt(Bounce));
        if (Lobe.Pdf <= 0.0 || ResolveLuminance(Lobe.Value) <= 0.0)
        {
            break;
        }
        let Survival = ResolveRouletteSurvival(Bounce);
        if (Survival < 1.0 && PathRandom(Seed, Bounce, 3u) > Survival)
        {
            break;
        }
        let Step = Lobe.Value / (Lobe.Pdf * Survival);
        let Hit = TraceClosest(OffsetRayOrigin(Vertex.Position, Vertex.Normal), Lobe.Direction, DirectionalReach);
        if (Bounce == 0u)
        {
            Omega1 = Lobe.Direction;
            RestPrefix = vec3f(1.0 / (Lobe.Pdf * Survival));
        }
        else
        {
            RestPrefix *= Step;
        }
        if (Found && Bounce > Rc)
        {
            SinceRc *= Step;
        }
        if (Found && Bounce == Rc)
        {
            RcOut = Lobe.Direction;
            RcOutPdf = Lobe.Pdf * Survival;
        }
        let PreviousThroughput = Throughput;
        Throughput *= Step;

        if (!Hit.Hit)
        {
            // escape: the environment is the light vertex (never at the first bounce)
            if (Bounce >= 1u)
            {
                let Radiance = ResolveEscapeRadiance(Lobe.Direction);
                let LightPdf = ResolveEnvironmentPdf(Lobe.Direction, Vertex.Normal);
                let Mis = ResolveMisWeight(false, false, LightPdf, Lobe.Pdf);
                let Estimate = Throughput * Radiance * Mis;
                var Candidate = ConstructEmptyPathSample();
                Candidate.Seed = Seed;
                Candidate.Terminal = Bounce;
                var Pdf = 1.0;
                if (Found)
                {
                    Candidate.Rc = Rc;
                    Candidate.RcPosition = RcVertex.Position;
                    Candidate.RcNormal = RcVertex.Normal;
                    Candidate.RcIdentity = RcIdentity;
                    Candidate.RcFamily = RcVertex.Family;
                    Candidate.RcOut = RcOut;
                    if (Rc == Bounce)
                    {
                        Candidate.Flags |= PathFlagOutTerminal;
                        Candidate.Radiance = Radiance;
                        Candidate.LightPdf = LightPdf;
                        Pdf = RcAreaPdf * RcOutPdf;
                    }
                    else
                    {
                        Candidate.Radiance = SinceRc * Radiance * Mis;
                        Pdf = RcAreaPdf * RcOutPdf;
                    }
                }
                else if (ReconnectCriterionLight(Vertex, DirectionalReach))
                {
                    Candidate.Rc = Bounce + 1u;
                    Candidate.Flags |= PathFlagRcLight | PathFlagRcDirectional;
                    Candidate.RcPosition = Lobe.Direction;
                    Candidate.Radiance = Radiance;
                    Pdf = Lobe.Pdf * Survival;
                }
                Candidate.Rest = RestPrefix * Radiance * Mis * Pdf;
                Candidate.Omega = Omega1;
                var Luminance = ResolveLuminance(Estimate);
                if (Trace.FireflyClamp > 0.0 && Luminance > Trace.FireflyClamp)
                {
                    Candidate.Rest *= Trace.FireflyClamp / Luminance;
                    Luminance = Trace.FireflyClamp;
                }
                StreamPath(Reservoir, Candidate, Luminance, Luminance * Pdf, AdvanceUniform(Stream));
            }
            break;
        }

        let Next = ConstructVertexFromHit(Hit, Lobe.Direction);
        let Geometry = ResolveHitGeometry(Hit, Lobe.Direction);

        // ③ emission at x_{b+1}, b ≥ 1
        let Emission = ResolveHitEmission(Hit);
        if (Bounce >= 1u && ResolveLuminance(Emission) > 0.0)
        {
            let LightAreaPdf = ResolveHitLightPdf(Hit, Vertex.Normal, Lobe.Direction);
            let BsdfAreaPdf = Lobe.Pdf * Geometry;
            let Mis = ResolveMisWeight(false, false, LightAreaPdf, BsdfAreaPdf);
            let Estimate = Throughput * Emission * Mis;
            var Candidate = ConstructEmptyPathSample();
            Candidate.Seed = Seed;
            Candidate.Terminal = Bounce;
            var Pdf = 1.0;
            if (Found)
            {
                Candidate.Rc = Rc;
                Candidate.RcPosition = RcVertex.Position;
                Candidate.RcNormal = RcVertex.Normal;
                Candidate.RcIdentity = RcIdentity;
                Candidate.RcFamily = RcVertex.Family;
                Candidate.RcOut = RcOut;
                if (Rc == Bounce)
                {
                    Candidate.Flags |= PathFlagOutTerminal;
                    Candidate.Radiance = Emission;
                    Candidate.LightPdf = LightAreaPdf / max(Geometry, 1.0e-12);
                    Pdf = RcAreaPdf * RcOutPdf;
                }
                else
                {
                    Candidate.Radiance = SinceRc * Emission * Mis;
                    Pdf = RcAreaPdf * RcOutPdf;
                }
            }
            else if (ReconnectCriterionLight(Vertex, Hit.Travel))
            {
                Candidate.Rc = Bounce + 1u;
                Candidate.Flags |= PathFlagRcLight;
                Candidate.RcPosition = Hit.Position;
                Candidate.RcNormal = Hit.Geometric;
                Candidate.RcIdentity = Hit.Occupant;
                Candidate.RcFamily = Hit.Family;
                Candidate.Radiance = Emission;
                Candidate.LightPdf = LightAreaPdf;
                Pdf = BsdfAreaPdf * Survival;
            }
            Candidate.Rest = RestPrefix * Emission * Mis * Pdf;
            Candidate.Omega = Omega1;
            var Luminance = ResolveLuminance(Estimate);
            if (Trace.FireflyClamp > 0.0 && Luminance > Trace.FireflyClamp)
            {
                Candidate.Rest *= Trace.FireflyClamp / Luminance;
                Luminance = Trace.FireflyClamp;
            }
            StreamPath(Reservoir, Candidate, Luminance, Luminance * Pdf, AdvanceUniform(Stream));
        }

        // ④ does x_{b+1} become the reconnection vertex?
        if (!Found && ReconnectCriterionSurface(Vertex, Next, Hit.Travel))
        {
            Found = true;
            Rc = Bounce + 1u;
            RcVertex = Next;
            RcIdentity = Hit.Occupant;
            RcAreaPdf = Lobe.Pdf * Survival * Geometry;
            SinceRc = vec3f(1.0);
        }
        Vertex = Next;
    }
    (*Reservoir).Tally = 1.0;
    FinalisePath(Reservoir);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHIFT MAPPING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Evaluates a stored path at another primary vertex. Replays the prefix up to the vertex before the reconnection
//    (or to the terminal vertex when there is none), reconnects, and returns F's luminance with the pieces the resolve
//    needs. Visibility of reconnection segments is traced when asked (always for a contribution, optionally for the
//    reverse shifts that only feed MIS weights).
fn EvaluateShift(Sample: PathSample, Destination: PathVertex, TraceVisibility: bool) -> ShiftResult
{
    var Result: ShiftResult;
    Result.Valid = false;
    Result.Target = 0.0;
    Result.Rest = vec3f(0.0);
    Result.Omega = vec3f(0.0, 0.0, 1.0);
    let Nee = PathHasFlag(Sample, PathFlagNee);
    let Stop = select(Sample.Terminal, Sample.Rc - 1u, Sample.Rc > 0u);
    if (Stop > Trace.PathBounces)
    {
        return Result;
    }
    var Vertex = Destination;
    var Prefix = vec3f(1.0);
    var Omega1 = vec3f(0.0, 0.0, 1.0);

    // replay x_1' … x_Stop'
    for (var Bounce = 0u; Bounce < Stop; Bounce++)
    {
        let Lobe = SampleReflectance(Vertex.Surface, Vertex.Normal, Vertex.View, PathRandomTriple(Sample.Seed, Bounce, 0u), AllowSpecularAt(Bounce));
        if (Lobe.Pdf <= 0.0 || ResolveLuminance(Lobe.Value) <= 0.0)
        {
            return Result;
        }
        let Survival = ResolveRouletteSurvival(Bounce);
        if (Survival < 1.0 && PathRandom(Sample.Seed, Bounce, 3u) > Survival)
        {
            return Result;
        }
        let Hit = TraceClosest(OffsetRayOrigin(Vertex.Position, Vertex.Normal), Lobe.Direction, DirectionalReach);
        if (!Hit.Hit)
        {
            return Result;
        }
        if (Bounce == 0u)
        {
            Omega1 = Lobe.Direction;
            Prefix = vec3f(1.0 / (Lobe.Pdf * Survival));
        }
        else
        {
            Prefix *= Lobe.Value / (Lobe.Pdf * Survival);
        }
        let Next = ConstructVertexFromHit(Hit, Lobe.Direction);
        // 📝 Invertibility: the base path did not reconnect here, so neither may the shifted one.
        if (ReconnectCriterionSurface(Vertex, Next, Hit.Travel))
        {
            return Result;
        }
        Vertex = Next;
    }

    var Rest = vec3f(0.0);
    var Connection = vec3f(0.0);   // direction leaving x_Stop'
    if (Sample.Rc == 0u)
    {
        // full replay: the terminal event at x_j'
        if (Nee)
        {
            let Light = SampleLight(Vertex.Position, Vertex.Normal, PathRandomTriple(Sample.Seed, Stop, 4u), PathRandom(Sample.Seed, Stop, 7u));
            if (!Light.Valid || dot(Vertex.Normal, Light.Direction) <= 0.0 || ReconnectCriterionLight(Vertex, Light.Distance))
            {
                return Result;
            }
            Connection = Light.Direction;
            let Reflectance = select(EvaluateVertexReflectance(Vertex, Stop, Light.Direction), vec3f(1.0), Stop == 0u);
            if (TraceVisibility && !ResolveLightVisible(Light, Vertex.Position, Vertex.Normal))
            {
                Result.Valid = true;
                Result.Omega = select(Omega1, Connection, Stop == 0u);
                return Result;
            }
            let BsdfPdf = PdfReflectance(Vertex.Surface, Vertex.Normal, Vertex.View, Light.Direction, AllowSpecularAt(Stop)) * Light.Geometry;
            let Mis = ResolveMisWeight(true, Light.Delta, Light.Pdf, BsdfPdf);
            Rest = Prefix * Reflectance * Light.Radiance * Light.Geometry * (Mis / Light.Pdf);
        }
        else
        {
            let Lobe = SampleReflectance(Vertex.Surface, Vertex.Normal, Vertex.View, PathRandomTriple(Sample.Seed, Stop, 0u), AllowSpecularAt(Stop));
            if (Lobe.Pdf <= 0.0)
            {
                return Result;
            }
            let Survival = ResolveRouletteSurvival(Stop);
            if (Survival < 1.0 && PathRandom(Sample.Seed, Stop, 3u) > Survival)
            {
                return Result;
            }
            Connection = Lobe.Direction;
            let Hit = TraceClosest(OffsetRayOrigin(Vertex.Position, Vertex.Normal), Lobe.Direction, DirectionalReach);
            let Step = select(Lobe.Value, vec3f(1.0), Stop == 0u) / (Lobe.Pdf * Survival);
            if (!Hit.Hit)
            {
                if (ReconnectCriterionLight(Vertex, DirectionalReach))
                {
                    return Result;
                }
                let LightPdf = ResolveEnvironmentPdf(Lobe.Direction, Vertex.Normal);
                Rest = Prefix * Step * ResolveEscapeRadiance(Lobe.Direction) * ResolveMisWeight(false, false, LightPdf, Lobe.Pdf);
            }
            else
            {
                let Next = ConstructVertexFromHit(Hit, Lobe.Direction);
                if (ReconnectCriterionSurface(Vertex, Next, Hit.Travel) || ReconnectCriterionLight(Vertex, Hit.Travel))
                {
                    return Result;
                }
                let Geometry = ResolveHitGeometry(Hit, Lobe.Direction);
                let LightPdf = ResolveHitLightPdf(Hit, Vertex.Normal, Lobe.Direction);
                Rest = Prefix * Step * ResolveHitEmission(Hit) * ResolveMisWeight(false, false, LightPdf, Lobe.Pdf * Geometry);
            }
        }
    }
    else if (PathHasFlag(Sample, PathFlagRcLight))
    {
        // reconnect x_Stop' straight to the light vertex
        if (PathHasFlag(Sample, PathFlagRcDirectional))
        {
            let Direction = Sample.RcPosition;
            if (dot(Vertex.Normal, Direction) <= 0.0 || !ReconnectCriterionLight(Vertex, DirectionalReach))
            {
                return Result;
            }
            Connection = Direction;
            let Reflectance = select(EvaluateVertexReflectance(Vertex, Stop, Direction), vec3f(1.0), Stop == 0u);
            if (TraceVisibility && !ResolveDirectionVisible(Vertex.Position, Vertex.Normal, Direction, DirectionalReach))
            {
                Result.Valid = true;
                Result.Omega = select(Omega1, Connection, Stop == 0u);
                return Result;
            }
            let LightPdf = ResolveEnvironmentPdf(Direction, Vertex.Normal);
            let BsdfPdf = PdfReflectance(Vertex.Surface, Vertex.Normal, Vertex.View, Direction, AllowSpecularAt(Stop));
            let Mis = ResolveMisWeight(Nee, false, LightPdf, BsdfPdf);
            Rest = Prefix * Reflectance * ResolveEscapeRadiance(Direction) * Mis;
        }
        else if (PathHasFlag(Sample, PathFlagRcDelta))
        {
            let Light = ConstructPointSample(Sample.RcIdentity, Vertex.Position);
            if (!Light.Valid || dot(Vertex.Normal, Light.Direction) <= 0.0 || !ReconnectCriterionLight(Vertex, Light.Distance))
            {
                return Result;
            }
            Connection = Light.Direction;
            let Reflectance = select(EvaluateVertexReflectance(Vertex, Stop, Light.Direction), vec3f(1.0), Stop == 0u);
            if (TraceVisibility && !ResolveLightVisible(Light, Vertex.Position, Vertex.Normal))
            {
                Result.Valid = true;
                Result.Omega = select(Omega1, Connection, Stop == 0u);
                return Result;
            }
            Rest = Prefix * Reflectance * Light.Radiance;
        }
        else
        {
            let Toward = Sample.RcPosition - Vertex.Position;
            let Distance = length(Toward);
            let Direction = Toward / max(Distance, 1.0e-6);
            let CosineLight = abs(dot(Sample.RcNormal, Direction));
            if (dot(Vertex.Normal, Direction) <= 0.0 || CosineLight <= 1.0e-4 || !ReconnectCriterionLight(Vertex, Distance))
            {
                return Result;
            }
            Connection = Direction;
            let Reflectance = select(EvaluateVertexReflectance(Vertex, Stop, Direction), vec3f(1.0), Stop == 0u);
            if (TraceVisibility && !ResolveSegmentVisible(Vertex.Position, Vertex.Normal, Sample.RcPosition, Sample.RcNormal))
            {
                Result.Valid = true;
                Result.Omega = select(Omega1, Connection, Stop == 0u);
                return Result;
            }
            let Geometry = CosineLight / max(Distance * Distance, 1.0e-8);
            let BsdfAreaPdf = PdfReflectance(Vertex.Surface, Vertex.Normal, Vertex.View, Direction, AllowSpecularAt(Stop)) * Geometry;
            let Mis = ResolveMisWeight(Nee, false, Sample.LightPdf, BsdfAreaPdf);
            Rest = Prefix * Reflectance * Geometry * Sample.Radiance * Mis;
        }
    }
    else
    {
        // reconnect x_Stop' to the interior vertex x_k, then its stored out-branch
        let Toward = Sample.RcPosition - Vertex.Position;
        let Distance = length(Toward);
        let Direction = Toward / max(Distance, 1.0e-6);
        let CosineRc = dot(Sample.RcNormal, -Direction);
        let RcSurface = ProjectSurfaceSample(FetchMaterial(Sample.RcFamily));
        if (dot(Vertex.Normal, Direction) <= 0.0 || CosineRc <= 1.0e-4 || Distance < Trace.ReconnectionDistance
            || !VertexReconnectable(Vertex) || !ResolveReconnectable(RcSurface, CosineRc))
        {
            return Result;
        }
        Connection = Direction;
        let Reflectance = select(EvaluateVertexReflectance(Vertex, Stop, Direction), vec3f(1.0), Stop == 0u);
        if (TraceVisibility && !ResolveSegmentVisible(Vertex.Position, Vertex.Normal, Sample.RcPosition, Sample.RcNormal))
        {
            Result.Valid = true;
            Result.Omega = select(Omega1, Connection, Stop == 0u);
            return Result;
        }
        let Geometry = CosineRc / max(Distance * Distance, 1.0e-8);
        let ViewAtRc = -Direction;
        let OutReflectance = EvaluateReflectance(RcSurface, Sample.RcNormal, ViewAtRc, Sample.RcOut);
        var Mis = 1.0;
        if (PathHasFlag(Sample, PathFlagOutTerminal))
        {
            let BsdfPdf = PdfReflectance(RcSurface, Sample.RcNormal, ViewAtRc, Sample.RcOut, true);
            Mis = ResolveMisWeight(Nee, PathHasFlag(Sample, PathFlagOutDelta), Sample.LightPdf, BsdfPdf);
        }
        Rest = Prefix * Reflectance * Geometry * OutReflectance * Sample.Radiance * Mis;
    }

    Result.Valid = true;
    Result.Omega = select(Omega1, Connection, Stop == 0u);
    Result.Rest = Rest;
    let Primary = EvaluateVertexReflectance(Destination, 0u, Result.Omega);
    Result.Target = ResolveLuminance(Primary * Rest);
    return Result;
}

// 📝 The integrand's luminance of a sample at its holding pixel, from the stored pieces (no rays).
fn ResolveStoredTarget(Sample: PathSample, Vertex: PathVertex) -> f32
{
    return ResolveLuminance(EvaluateVertexReflectance(Vertex, 0u, Sample.Omega) * Sample.Rest);
}

// 📝 The resolved split of a path reservoir at a primary vertex: (diffuse irradiance, specular radiance).
fn ResolvePathSplit(Reservoir: PathReservoir, Vertex: PathVertex) -> ReflectanceSplit
{
    var Split = EvaluateReflectanceSplit(Vertex.Surface, Vertex.Normal, Vertex.View, Reservoir.Sample.Omega);
    if (!AllowSpecularAt(0u))
    {
        Split.Specular = vec3f(0.0);
    }
    var Scale = Reservoir.Weight;
    let Rest = Reservoir.Sample.Rest;
    if (Trace.FireflyClamp > 0.0 && Scale > 0.0)
    {
        let Luminance = ResolveLuminance((ResolveDiffuseAlbedo(Vertex.Surface) * Split.Diffuse + Split.Specular) * Rest) * Scale;
        Scale *= select(1.0, Trace.FireflyClamp / Luminance, Luminance > Trace.FireflyClamp);
    }
    Split.Diffuse = Split.Diffuse * Rest * (Pi * Scale);
    Split.Specular = Split.Specular * Rest * Scale;
    return Split;
}
