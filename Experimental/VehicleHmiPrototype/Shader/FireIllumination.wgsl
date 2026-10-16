//============================================================================================================================================
//                                                          FIREILLUMINATION.WGSL
//============================================================================================================================================
// 🧩 A burning tyre as a point light on the scene: inverse-square, windowed to its reach, shadowed through the scene distance field.

// 📝 Read by the direct pass (screen) and the card direct pass (the surface cache), so a tyre fire lights the course
//    and the car directly and, through the cards the final gather reads, bounces into the indirect term as well.
//    The shadow ray stops short of the light: the light sits inside the burning wheel, which must not shadow itself.

//! include "FrameDescriptor.wgsl"
//! include "DistanceTraversal.wgsl"

struct FireArrival
{
    Direction   : vec3f,   // [-]  toward the fire
    Illuminance : vec3f,   // [-]  arriving at normal incidence, shadowed and windowed
}

fn ResolveFireArrival(Position: vec3f, Normal: vec3f) -> FireArrival
{
    var Arrival: FireArrival;
    Arrival.Direction = vec3f(0.0, 0.0, 1.0);
    Arrival.Illuminance = vec3f(0.0);
    if (Frame.FireLightIntensity <= 0.0)
    {
        return Arrival;
    }
    let Toward = Frame.FireLightPosition - Position;
    let Squared = dot(Toward, Toward);
    let Reach = Frame.FireLightReach;
    if (Squared >= Reach * Reach)
    {
        return Arrival;
    }
    let Range = sqrt(Squared);
    Arrival.Direction = Toward / max(Range, 1.0e-4);
    if (dot(Normal, Arrival.Direction) <= 0.0)
    {
        return Arrival;
    }
    let Ratio = Squared / (Reach * Reach);
    let Window = (1.0 - Ratio * Ratio) * (1.0 - Ratio * Ratio);
    var Visibility = 1.0;
    if (FeatureEnabled(Frame, FeatureDirectShadow))
    {
        let Bias = ResolveSurfaceBias(Position);
        Visibility = TraverseSceneShadow(Position + Normal * Bias, Arrival.Direction, 0.5 * Bias, max(Range - 0.6, 0.0), 4.0);
    }
    Arrival.Illuminance = Frame.FireLightColour * Frame.FireLightIntensity * Window * Visibility / max(Squared, 0.25);
    return Arrival;
}

// Pooled sparks/derez: bounded inverse-square falloff, distance-field occlusion,
// evaluated by both the direct screen pass and the card cache for bounced light.
fn ResolveTransientArrival(Position: vec3f, Normal: vec3f, Index: u32) -> FireArrival {
    let Light = Frame.TransientLights[Index];
    var Arrival: FireArrival;
    Arrival.Direction = vec3f(0.0, 0.0, 1.0);
    Arrival.Illuminance = vec3f(0.0);
    let Toward = Light.PositionReach.xyz - Position;
    let Squared = dot(Toward, Toward);
    let Reach = Light.PositionReach.w;
    if (Reach <= 0.0 || Squared >= Reach * Reach || all(Light.Colour.rgb <= vec3f(0.0))) { return Arrival; }
    let Range = sqrt(Squared);
    Arrival.Direction = Toward / max(Range, 0.0001);
    if (dot(Normal, Arrival.Direction) <= 0.0) { return Arrival; }
    let Window = pow(1.0 - Squared / (Reach * Reach), 2.0);
    var Visibility = 1.0;
    if (FeatureEnabled(Frame, FeatureDirectShadow)) {
        let Bias = ResolveSurfaceBias(Position);
        Visibility = TraverseSceneShadow(Position + Normal * Bias, Arrival.Direction, Bias * 0.5, max(Range - 0.15, 0.0), 4.0);
    }
    Arrival.Illuminance = Light.Colour.rgb * Frame.EmissionScale * Window * Visibility / max(Squared, 0.16);
    return Arrival;
}

//! include "LocalLighting.wgsl"
fn ResolveShadowedLocalArrival(Position:vec3f,Normal:vec3f,Index:u32)->LocalArrival {
    var A=ResolveLocalArrival(Position,Normal,Index);
    if(any(A.Illuminance>vec3f(0)) && FeatureEnabled(Frame,FeatureDirectShadow)) {
        let Bias=ResolveSurfaceBias(Position);
        let Range=distance(Frame.LocalLights[Index].PositionReach.xyz,Position);
        A.Illuminance*=TraverseSceneShadow(Position+Normal*Bias,A.Direction,Bias*0.5,max(Range-0.12,0.0),4.0);
    }
    return A;
}
