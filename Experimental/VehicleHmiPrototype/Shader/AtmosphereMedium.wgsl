// Shared physical medium and LUT coordinate contract. Units: metres, inverse metres.
struct AtmosphereRecord
{
    SunDirection     : vec3f,   // [-]  unit, toward the sun
    SolarIlluminance : f32,     // [-]  top-of-atmosphere illuminance scale
    ObserverAltitude : f32,     // [m]
    Pad0             : f32,
    Pad1             : f32,
    Pad2             : f32,
    Medium : vec4f,
    Heights : vec4f,
    SolarTint : vec4f,
    Ground : vec4f,
}

@group(0) @binding(0) var<uniform> Atmosphere : AtmosphereRecord;


const GroundRadius      : f32   = 6360000.0;                                  // [m]
const AtmosphereRadius  : f32   = 6460000.0;                                  // [m]
const RayleighScatter   : vec3f = vec3f(5.802e-6, 13.558e-6, 33.1e-6);       // [1/m]
const RayleighHeight    : f32   = 8000.0;                                     // [m]
const MieScatter        : f32   = 3.996e-6;                                   // [1/m]
const MieExtinction     : f32   = 4.40e-6;                                    // [1/m]
const MieHeight         : f32   = 1200.0;                                     // [m]
const MieAsymmetry      : f32   = 0.8;                                        // [-]
const OzoneAbsorption   : vec3f = vec3f(0.650e-6, 1.881e-6, 0.085e-6);       // [1/m]
const ViewSamples       : u32   = 40u;
const LightSamples      : u32   = 64u;

// 📐 Ray–sphere intersection about the planet centre; returns (near, far), far < 0 when the sphere is missed.
fn IntersectSphere(Origin: vec3f, Direction: vec3f, Radius: f32) -> vec2f
{
    let b = dot(Origin, Direction);
    let c = dot(Origin, Origin) - Radius * Radius;
    let Discriminant = b * b - c;
    if (Discriminant < 0.0)
    {
        return vec2f(-1.0, -1.0);
    }
    let Root = sqrt(Discriminant);
    return vec2f(-b - Root, -b + Root);
}

fn ResolveDensities(Altitude: f32) -> vec3f
{
    let Ozone = max(0.0, 1.0 - abs(Altitude - 25000.0) / 15000.0);
    return vec3f(exp(-Altitude / Atmosphere.Heights.x), exp(-Altitude / Atmosphere.Heights.y), Ozone);
}

fn ResolveExtinction(Densities: vec3f) -> vec3f
{
    return RayleighScatter * Atmosphere.Medium.x * Densities.x + vec3f(MieExtinction * Atmosphere.Medium.y * Densities.y) + OzoneAbsorption * Atmosphere.Medium.z * Densities.z;
}

fn MarchTransmittance(Position: vec3f, Sun: vec3f) -> vec3f
{
    if (IntersectSphere(Position, Sun, GroundRadius).x > 0.0)
    {
        return vec3f(0.0);
    }
    let Exit = IntersectSphere(Position, Sun, AtmosphereRadius).y;
    let Step = Exit / f32(LightSamples);
    var Depth = vec3f(0.0);
    for (var Sample = 0u; Sample < LightSamples; Sample++)
    {
        let Point = Position + Sun * ((f32(Sample) + 0.5) * Step);
        Depth += ResolveExtinction(ResolveDensities(length(Point) - GroundRadius)) * Step;
    }
    return exp(-Depth);
}


fn TransmittanceUV(P: vec3f, Sun: vec3f) -> vec2f {
 return vec2f(dot(normalize(P),Sun)*0.5+0.5,sqrt(clamp((length(P)-GroundRadius)/(AtmosphereRadius-GroundRadius),0.0,1.0)));
}
fn LookupTransmittance(Tex: texture_2d<f32>, S: sampler, P: vec3f, Sun: vec3f) -> vec3f {
 if (IntersectSphere(P,Sun,GroundRadius).x>0.0) { return vec3f(0.0); }
 return textureSampleLevel(Tex,S,TransmittanceUV(P,Sun),0.0).rgb;
}
fn LookupMultiple(Tex: texture_2d<f32>, S: sampler, P: vec3f, Sun: vec3f) -> vec3f {
 return textureSampleLevel(Tex,S,TransmittanceUV(P,Sun),0.0).rgb;
}
