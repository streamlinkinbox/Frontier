//============================================================================================================================================
//                                                            LIGHTSAMPLING.WGSL
//============================================================================================================================================
// 🧩 One mixture over every light the scene has — the solar disc, the sky, the clustered point lights with their IES
//    profiles (and a burning tyre), the emissive triangles — with the pdf of any direction under that mixture.

// 📝 Sampling draws a kind by fixed probability, then within the kind: the disc uniformly, the sky by the cosine about
//    the normal, a point light uniformly, an emissive triangle by its power through the CDF the host builds, then a
//    uniform point on it. The sun and the sky are two techniques for the same environment, so a directional sample
//    always carries the environment radiance and the summed pdf of both techniques, and BSDF rays that escape are
//    weighted against that same pdf. Emissive triangles reconstruct their point from the live occupant transform,
//    so a reservoir sample on a tail light follows the car.

//! include "TraceTraversal.wgsl"
//! include "PathReflectance.wgsl"

const LightKindSun      : u32 = 0u;
const LightKindSky      : u32 = 1u;
const LightKindPoint    : u32 = 2u;
const LightKindEmissive : u32 = 3u;
const EmissionFloor     : f32 = 0.5;      // [-] matches TraceStructure.js: fainter families are not sampled lights
const DirectionalReach  : f32 = 1.0e5;    // [m] shadow ray length for the sun and the sky

struct LightSample
{
    Valid       : bool,
    Delta       : bool,
    Kind        : u32,
    Index       : u32,     // point light index, or emissive entry index
    Direction   : vec3f,   // [-] from the shading point toward the light
    Distance    : f32,     // [m] to the light point (DirectionalReach for the sun and the sky)
    Position    : vec3f,   // [m] light point (point and emissive kinds)
    Normal      : vec3f,   // [-] emitter normal (emissive kind)
    Radiance    : vec3f,   // [-] L_e, or a point light's irradiance at normal incidence
    Pdf         : f32,     // [-]  mixture pdf in the sample's measure: solid angle (sun, sky), area (emissive), discrete (point)
    Geometry    : f32,     // [-]  cosθ_l/d² for emissive samples (area → solid angle), 1 otherwise
    Barycentric : vec2f,   // [-] emissive: (u, v) on the triangle
    Triangle    : u32,     // [-] emissive: global triangle index
    Occupant    : u32,     // [-] emissive: occupant carrying the triangle
}

fn ResolvePointTotal() -> u32
{
    return Trace.PointCount + select(0u, 1u, Frame.FireLightIntensity > 0.0);
}

// 📝 Kind probabilities (sun, sky, point, emissive), zero for kinds the scene lacks.
fn ResolveKindProbabilities() -> vec4f
{
    var Weights = vec4f(select(0.0, 0.35, Trace.SunAbove != 0u), 0.15, select(0.0, 0.25, ResolvePointTotal() > 0u),
                        select(0.0, 0.25, Trace.EmissiveCount > 0u && Trace.EmissivePower > 0.0));
    let Total = Weights.x + Weights.y + Weights.z + Weights.w;
    return Weights / max(Total, 1.0e-6);
}

// 📐 pdf, under the mixture, of an escaped direction from a surface with this normal.
fn ResolveEnvironmentPdf(Direction: vec3f, Normal: vec3f) -> f32
{
    let Kinds = ResolveKindProbabilities();
    var Pdf = Kinds.y * max(dot(Normal, Direction), 0.0) * InversePi;
    if (InsideSunDisc(Direction))
    {
        Pdf += Kinds.x / max(Trace.SunSolidAngle, 1.0e-8);
    }
    return Pdf;
}

// 📐 Area pdf, under the mixture, of a point on an emissive triangle of this family: P_e · power / Σpower / area,
//    and power = area × luminance, so the area cancels. Zero for families the table omits.
fn ResolveEmissiveAreaPdf(Family: u32, InstanceFlags: u32) -> f32
{
    let Luminance = ResolveLuminance(FetchMaterial(Family).Emission);
    if (Luminance < EmissionFloor || Trace.EmissivePower <= 0.0 || (InstanceFlags & InstanceFlagFragment) != 0u)
    {
        return 0.0;
    }
    return ResolveKindProbabilities().w * Luminance / Trace.EmissivePower;
}

// 📝 A point light's irradiance at normal incidence (EvaluateLightArrival without the receiver cosine).
fn ResolvePointIrradiance(Light: LightRecord, Position: vec3f) -> vec3f
{
    if (Light.Params.w < 0.5)
    {
        return vec3f(0.0);
    }
    let Toward = Light.PositionType.xyz - Position;
    let Squared = dot(Toward, Toward);
    let Reach = Light.DirectionReach.w;
    if (Squared >= Reach * Reach || Reach <= 0.0)
    {
        return vec3f(0.0);
    }
    let Distance = sqrt(Squared);
    let Ratio = Squared / (Reach * Reach);
    let Window = (1.0 - Ratio * Ratio) * (1.0 - Ratio * Ratio);
    let Profile = EvaluateLightIES(Light, -Toward, Distance);
    return Light.ColourIntensity.rgb * (Light.ColourIntensity.w * Window * Profile / max(Squared, 0.36));
}

fn ResolveFireIrradiance(Position: vec3f) -> vec3f
{
    let Toward = Frame.FireLightPosition - Position;
    let Squared = dot(Toward, Toward);
    let Reach = Frame.FireLightReach;
    if (Squared >= Reach * Reach || Frame.FireLightIntensity <= 0.0)
    {
        return vec3f(0.0);
    }
    let Ratio = Squared / (Reach * Reach);
    let Window = (1.0 - Ratio * Ratio) * (1.0 - Ratio * Ratio);
    return Frame.FireLightColour * Frame.FireLightIntensity * Window / max(Squared, 0.25);
}

// 📝 Point-light sample by index; the index past the clustered lights is the tyre fire.
fn ConstructPointSample(Index: u32, Position: vec3f) -> LightSample
{
    var Sample: LightSample;
    Sample.Valid = false;
    Sample.Delta = true;
    Sample.Kind = LightKindPoint;
    Sample.Index = Index;
    let Total = ResolvePointTotal();
    if (Index >= Total)
    {
        return Sample;
    }
    if (Index < Trace.PointCount)
    {
        let Light = FetchPointLight(Index);
        Sample.Position = Light.PositionType.xyz;
        Sample.Radiance = ResolvePointIrradiance(Light, Position);
    }
    else
    {
        Sample.Position = Frame.FireLightPosition;
        Sample.Radiance = ResolveFireIrradiance(Position);
    }
    let Toward = Sample.Position - Position;
    Sample.Distance = length(Toward);
    Sample.Direction = Toward / max(Sample.Distance, 1.0e-4);
    // 📝 The fire sits inside a wheel: its shadow ray stops short of it (FireIllumination.wgsl).
    if (Index >= Trace.PointCount)
    {
        Sample.Distance = max(Sample.Distance - 0.6, 0.0);
    }
    Sample.Normal = -Sample.Direction;
    Sample.Geometry = 1.0;
    Sample.Pdf = ResolveKindProbabilities().z / f32(Total);
    Sample.Valid = Sample.Pdf > 0.0;
    return Sample;
}

// 📝 Directional sample (sun or sky technique): environment radiance, summed pdf.
fn ConstructDirectionalSample(Direction: vec3f, Position: vec3f, Normal: vec3f, Kind: u32) -> LightSample
{
    var Sample: LightSample;
    Sample.Delta = false;
    Sample.Kind = Kind;
    Sample.Index = 0u;
    Sample.Direction = Direction;
    Sample.Distance = DirectionalReach;
    Sample.Position = Position + Direction * DirectionalReach;
    Sample.Normal = -Direction;
    Sample.Radiance = ResolveEscapeRadiance(Direction);
    Sample.Geometry = 1.0;
    Sample.Pdf = ResolveEnvironmentPdf(Direction, Normal);
    Sample.Valid = Sample.Pdf > 0.0 && dot(Normal, Direction) > 0.0;
    return Sample;
}

// 📝 The point at barycentric (u, v) of a triangle carried by an occupant, from its live transform: radiance,
//    geometry term cosθ_l/d², no pdf (ConstructEmissiveSample adds it; stored samples never need it).
fn ConstructTrianglePoint(Triangle: u32, OccupantIndex: u32, Barycentric: vec2f, Position: vec3f) -> LightSample
{
    var Sample: LightSample;
    Sample.Valid = false;
    Sample.Delta = false;
    Sample.Kind = LightKindEmissive;
    Sample.Index = 0u;
    Sample.Pdf = 0.0;
    Sample.Geometry = 0.0;
    Sample.Barycentric = Barycentric;
    Sample.Triangle = Triangle;
    Sample.Occupant = OccupantIndex;
    Sample.Radiance = vec3f(0.0);
    if (Triangle >= arrayLength(&Triangles) || OccupantIndex >= arrayLength(&Occupants))
    {
        return Sample;
    }
    let Record = Triangles[Triangle];
    let Occupant = Occupants[OccupantIndex];
    if (Occupant.WorldMinimum.x > Occupant.WorldMaximum.x)
    {
        return Sample;   // the occupant left the scene
    }
    let Local = Record.V0 + Record.E1 * Barycentric.x + Record.E2 * Barycentric.y;
    let LocalFace = cross(Record.E1, Record.E2);
    Sample.Position = (Occupant.LocalToWorld * vec4f(Local, 1.0)).xyz;
    Sample.Normal = normalize((Occupant.LocalToWorld * vec4f(LocalFace, 0.0)).xyz);
    let Toward = Sample.Position - Position;
    let Squared = dot(Toward, Toward);
    Sample.Distance = sqrt(Squared);
    Sample.Direction = Toward / max(Sample.Distance, 1.0e-4);
    let Family = ResolveHitFamily(Record, Occupant);
    Sample.Radiance = FetchMaterial(Family).Emission * Frame.EmissionScale;
    let Cosine = abs(dot(Sample.Normal, Sample.Direction));
    if (Cosine <= 1.0e-4 || Squared <= 1.0e-6)
    {
        return Sample;
    }
    Sample.Geometry = Cosine / Squared;
    Sample.Valid = true;
    return Sample;
}

// 📝 Emissive-triangle sample of table entry Index with its area pdf P_e · power / Σpower / area.
fn ConstructEmissiveSample(Index: u32, Barycentric: vec2f, Position: vec3f) -> LightSample
{
    if (Index >= Trace.EmissiveCount)
    {
        var Empty: LightSample;
        Empty.Valid = false;
        Empty.Kind = LightKindEmissive;
        return Empty;
    }
    let Entry = FetchEmissive(Index);
    var Sample = ConstructTrianglePoint(Entry.Triangle, Entry.Occupant, Barycentric, Position);
    Sample.Index = Index;
    if (!Sample.Valid)
    {
        return Sample;
    }
    let Record = Triangles[Entry.Triangle];
    let Area = 0.5 * length(cross(Record.E1, Record.E2));
    if (Area <= 1.0e-9)
    {
        Sample.Valid = false;
        return Sample;
    }
    Sample.Pdf = ResolveKindProbabilities().w * Entry.Power / max(Trace.EmissivePower, 1.0e-6) / Area;
    Sample.Valid = Sample.Pdf > 0.0;
    return Sample;
}

fn SearchEmissive(Random: f32) -> u32
{
    var Low = 0u;
    var High = Trace.EmissiveCount - 1u;
    for (var Step = 0u; Step < 20u && Low < High; Step++)
    {
        let Middle = (Low + High) >> 1u;
        if (FetchEmissive(Middle).Cdf < Random)
        {
            Low = Middle + 1u;
        }
        else
        {
            High = Middle;
        }
    }
    return Low;
}

// 📝 A sample from the mixture for a shading point; Random.x picks the kind, yz (and the extra) place it.
fn SampleLight(Position: vec3f, Normal: vec3f, Random: vec3f, Extra: f32) -> LightSample
{
    let Kinds = ResolveKindProbabilities();
    var Pick = Random.x;
    if (Pick < Kinds.x)
    {
        // uniform direction in the solar cone
        let Cosine = 1.0 - Random.y * (1.0 - Trace.SunCosine);
        let Sine = sqrt(max(0.0, 1.0 - Cosine * Cosine));
        let φ = 2.0 * Pi * Random.z;
        let Direction = normalize(ConstructTangentFrame(Frame.SunDirection) * vec3f(Sine * cos(φ), Sine * sin(φ), Cosine));
        return ConstructDirectionalSample(Direction, Position, Normal, LightKindSun);
    }
    Pick -= Kinds.x;
    if (Pick < Kinds.y)
    {
        let Direction = normalize(ConstructTangentFrame(Normal) * ProjectCosineHemisphere(Random.yz));
        return ConstructDirectionalSample(Direction, Position, Normal, LightKindSky);
    }
    Pick -= Kinds.y;
    if (Pick < Kinds.z)
    {
        let Total = ResolvePointTotal();
        return ConstructPointSample(min(u32(Random.y * f32(Total)), Total - 1u), Position);
    }
    let Entry = SearchEmissive(Random.y);
    let Root = sqrt(max(Random.z, 0.0));
    let Barycentric = vec2f(1.0 - Root, Extra * Root);
    return ConstructEmissiveSample(Entry, Barycentric, Position);
}

// 📝 Unshadowed contribution split of a light sample at a surface: (Diffuse per unit ρ_d, Specular), each × L.
fn EvaluateLightSplit(Sample: LightSample, Surface: SurfaceSample, Normal: vec3f, View: vec3f) -> ReflectanceSplit
{
    var Split = EvaluateReflectanceSplit(Surface, Normal, View, Sample.Direction);
    Split.Diffuse *= Sample.Radiance * Sample.Geometry;
    Split.Specular *= Sample.Radiance * Sample.Geometry;
    return Split;
}

fn ResolveSplitLuminance(Split: ReflectanceSplit, Surface: SurfaceSample) -> f32
{
    return ResolveLuminance(ResolveDiffuseAlbedo(Surface) * Split.Diffuse + Split.Specular);
}

// 📝 Shadow ray for a light sample from a surface point.
fn ResolveLightVisible(Sample: LightSample, Position: vec3f, Normal: vec3f) -> bool
{
    if (Sample.Kind == LightKindSun || Sample.Kind == LightKindSky)
    {
        return ResolveDirectionVisible(Position, Normal, Sample.Direction, DirectionalReach);
    }
    if (Sample.Kind == LightKindEmissive)
    {
        return ResolveSegmentVisible(Position, Normal, Sample.Position, Sample.Normal);
    }
    let Start = OffsetRayOrigin(Position, select(-Normal, Normal, dot(Normal, Sample.Direction) >= 0.0));
    return !TraceOccluded(Start, Sample.Direction, max(Sample.Distance - 0.02, 0.0));
}

// 📝 Light pdf of what a BSDF ray found, in the light's own measure: the environment's solid-angle pdf on an
//    escape, the area pdf of an emissive triangle it hit. Zero when the mixture could not have produced it.
fn ResolveHitLightPdf(Hit: TraceHit, OriginNormal: vec3f, Direction: vec3f) -> f32
{
    if (!Hit.Hit)
    {
        return ResolveEnvironmentPdf(Direction, OriginNormal);
    }
    return ResolveEmissiveAreaPdf(Hit.Family, Instances[Hit.Instance].Flags);
}

// 📐 cosθ_l/d² of a hit: converts a solid-angle pdf at the origin to an area pdf at the hit.
fn ResolveHitGeometry(Hit: TraceHit, Direction: vec3f) -> f32
{
    return abs(dot(Hit.Geometric, Direction)) / max(Hit.Travel * Hit.Travel, 1.0e-8);
}

fn ResolveHitEmission(Hit: TraceHit) -> vec3f
{
    return FetchMaterial(Hit.Family).Emission * Frame.EmissionScale;
}
