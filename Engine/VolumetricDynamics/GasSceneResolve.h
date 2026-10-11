//==============================================================================================================================================
//                                                          GASSCENERESOLVE.H
//==============================================================================================================================================
// 📦 The seam between an authored scene and a running one: normalised authoring readings in, metres and seconds out.
//
// GasSceneCodec.h gets the file across. That is not the same as getting the *effect* across, and the gap
//    between the two is where a port quietly becomes a different effect wearing the same name. The authoring
//    tool works in a normalised cube with Y up, fractions of a domain, and an emitter that clamps its centre
//    to a target reading every frame. The solver works in metres, with axis 2 up, an origin somewhere in a
//    level, and an injector that adds. Every one of those differences has to be spent somewhere, and this
//    file is where, in one place, written down, rather than three times over in three callers.
//
// 🔴 THE OBSTACLE NUMBERING DOES NOT MATCH THE SHAPE ENUM, AND LOOKS LIKE IT DOES.
//    OBSTACLE_TYPES is { None, Sphere, Vertical cylinder, Horizontal cylinder, Deflector slab, Tyre ring } and
//    GasColliderShape is { None, Sphere, Capsule, Box, Cylinder, TyreRing }. They agree on 0, 1 and 5 and
//    disagree on everything in between, so `static_cast<GasColliderShape>(ObstacleType)` compiles, runs, and
//    silently turns a deflector slab into a cylinder. The cast is therefore refused here and a written table
//    is used instead. This is the exact defect the step-4 byte comparison was built to catch in files; it
//    lives in code, where only a table can catch it.
//
// ⚠️ TWO SHAPES CANNOT BE REPRESENTED AND SAY SO RATHER THAN PRETENDING.
//    GasCollider carries no rotation — every shape is axis-aligned about axis 2 — which is deliberate and
//    documented in GasCollisionIntake.h. The browser's horizontal cylinder lies along X, and its tyre ring
//    spins about a horizontal axis (which is what a rolling tyre does). Neither survives. They are admitted
//    as their closest axis-aligned relative and the reading carries a flag saying so, because an obstruction
//    in the wrong orientation diverts a plume the wrong way, and that reads as the solver being wrong.
//
// 💡 THE CUBE IS SIZED FOR THE LARGEST THE DOMAIN WILL EVER BE, NOT FOR ITS RESTING SIZE.
//    The authoring domain is a box that surges outward on a detonation; the coarse field is a fixed cube that
//    cannot grow. Sizing it to the resting box costs nothing until the blast, and then clips it — and smoke
//    vanishing at a flat boundary is far more visible than the same smoke being slightly coarser throughout.
//
// 📐 Axis correspondence, applied by every routine below:  browser X → 0,  browser Z → 1,  browser Y → 2.
//    The authoring floor sits at browser y = −0.6; engine z = 0 is that floor, so z = y + 0.6.

#pragma once

#include "CoarseGasField.h"
#include "GasCollisionIntake.h"
#include "GasSceneCodec.h"
#include "../DisplayPresentation/VolumeRaymarch.h"

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                      AUTHORING CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// Readings taken from the authoring tool rather than chosen here. Each is a number the browser hard-codes, and
//    each one is why some conversion below is not simply a multiplication.

constexpr float AuthoringFloorHeight   = 0.6f;    // [m] - browser y of the floor, negated; engine z = y + this
constexpr float AuthoringHeightDivisor = 1.2f;    // [-] - domainScale.y is effHeight / this
constexpr float AuthoringMetricFloor   = 0.75f;   // [-] - metricScale never falls below this
constexpr float AuthoringMetricCurve   = 0.55f;   // [-] - metricScale is the domain extent raised to this
constexpr float AuthoringEmitterReach  = 1.35f;   // [-] - the emitter's falloff ends at this many radii
constexpr float AuthoringViewAngle     = 46.0f;   // [deg] - vertical field of view of the authoring viewport

// A settling floor, so a source with no upward velocity still has a residence time. Cold dust leaves its
//    source by falling rather than by rising, and dividing by zero is not a conversion.
constexpr float GasSettlingSpeed = 0.5f;          // [m/s]


//------------------------------------------------------------------------------------------------------------------------
//                                                        THE DOMAIN
//------------------------------------------------------------------------------------------------------------------------

// Where the authored box ends up in world space. The cube is the solver's; Width and Height are the box the
//    authoring tool actually draws, kept so a caller can report how much of the cube that box occupies.
struct GasDomainMeasure
{
    float Span      = 8.0f;                      // [m] - edge of the solver's cube
    float Origin[3] = { 0.0f, 0.0f, 0.0f };      // [m] - minimum corner
    float Width     = 1.85f;                     // [m] - authored box, horizontal, at full surge
    float Height    = 2.1f;                      // [m] - authored box, vertical, at full surge
};


/// 📦 The cube the solver will run in, sized from the authored bounds at full surge.
/// in    Settings   [-]  the scene's settings
/// out   GasDomainMeasure   [m]  cube and authored box, both in metres
/// note  dynamic bounds are taken at their maximum, for the reason in the file header
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasDomainMeasure ResolveDomain(const GasSettings& Settings) noexcept
{
    const float Surge = Settings.DynamicBounds ? Settings.DynamicBoundsMax : 1.0f;

    GasDomainMeasure Measure;
    Measure.Width  = Settings.BoundsWidth  * Surge;
    Measure.Height = Settings.BoundsHeight * (1.0f + (Surge - 1.0f) * 1.15f);
    Measure.Span   = Measure.Width > Measure.Height ? Measure.Width : Measure.Height;

    // Centred horizontally on the authoring origin, standing on the floor. A level will translate this; the
    //    scene file does not carry a world placement, because a scene is an effect and not a position in a map.
    Measure.Origin[0] = -0.5f * Measure.Span;
    Measure.Origin[1] = -0.5f * Measure.Span;
    Measure.Origin[2] = 0.0f;
    return Measure;
}


/// 📦 The authoring tool's metricScale, which is how it keeps a plume the same shape as the box grows.
/// in    Extent   [m]  one component of the domain extent
/// out   float    [-]  the scale that component's offsets are weighted by
/// cost  ✔️
/// tag   internal, nonallocating, nonthrowing
inline float AuthoringMetricScale(float Extent) noexcept
{
    const float Raised = std::pow(Extent, AuthoringMetricCurve);
    return Raised > AuthoringMetricFloor ? Raised : AuthoringMetricFloor;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        THE SOLVER
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The scene's simulation tuning in the solver's units.
/// in    Settings   [-]  the scene's settings
/// out   CoarseGasSettings   [-]  losses per second, accelerations in m/s²
/// note  TimeScale is deliberately NOT folded in here. The step interval is fixed at 1/60 s and multiplying
///       the accelerations instead would change the result of the pressure projection, which is the one place
///       determinism is actually load-bearing. A caller wanting slow motion advances fewer times.
/// note  PressureIterations is likewise not carried: CoarsePressureSweeps is fixed at 12 by construction, and
///       a scene asking for 22 is describing the fine field, which this is not.
/// 🔴 THE LOSSES ARE NOT THE SAME KIND OF NUMBER AT EITHER END, AND THE TYPES AGREE ANYWAY.
///    An authored loss is the coefficient 𝑘 in exp(−𝑘·Δτ) — the browser writes exactly that. CoarseGasSettings
///    wants the *fraction* shed in one second, which is 1 − exp(−𝑘) and can never reach 1. Carrying 𝑘 across
///    unchanged compiles, is dimensionally plausible, and is wrong: camp_fire authors a cooling coefficient of
///    1.35, RemainingAfter clamps anything at or above 1 to zero, and every scene in the corpus came out with
///    no heat in it at all. Nothing else would have caught that — the file crossed byte for byte, the solver
///    was reproducible, and the plume still rose on its own smoke weight. Running one scene end to end did.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline CoarseGasSettings ResolveSolverTuning(const GasSettings& Settings) noexcept
{
    CoarseGasSettings Tuning;
    Tuning.BuoyancyLift = Settings.Buoyancy;

    // The authoring tool weights the downward pull of particulate by 0.45 before applying it; the solver
    //    applies it whole, so the weight is spent here rather than in two solvers' worth of arithmetic.
    Tuning.SmokeWeight = Settings.SmokeWeight * 0.45f;

    Tuning.SmokeLossPerSecond = 1.0f - std::exp(-Settings.SmokeDissipation);
    Tuning.DampingPerSecond   = 1.0f - std::exp(-Settings.VelocityDamping);

    // Cooling is radiative and nonlinear where it is authored: 𝑘·(0.55·T + 0.14·T²). CoarseGasSettings has one
    //    coefficient and no room for the square, so the linear part crosses and the square does not. That makes
    //    very hot gas cool slightly slower here than in the browser, which is the safe direction to be wrong in:
    //    a plume that lingers is a look, a plume that vanishes reads as the solver being broken.
    Tuning.CoolingPerSecond = 1.0f - std::exp(-Settings.CoolingRate * 0.55f);

    // The authored turbulence strength is an amplitude in the normalised cube; the coarse swirl is in m/s over
    //    a cube of Span metres, and 0.09 is the ratio that leaves a camp fire looking like a camp fire. It is a
    //    constant because the two turbulence fields are different functions — this is a match of character, not
    //    a unit conversion, and calling it one would be the dishonest version.
    Tuning.TurbulenceStrength = Settings.TurbulenceStrength * 0.09f;
    Tuning.TurbulenceScale    = Settings.TurbulenceScale;
    return Tuning;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SOURCES
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The scene's continuous emitter as the solver's injector.
/// in    Settings   [-]  the scene's settings
/// in    Measure    [m]  the cube from ResolveDomain
/// out   GasEmission   [-]  position in metres, rates per second
/// note  an emitter the scene has switched off comes back with zero rates rather than as an absence, so a
///       caller can inject it unconditionally and get nothing
/// 📐 RATE CONVERSION. The browser emitter clamps the centre of its source to a target reading every frame;
///    the solver's injector adds. Equating the two at the target is the conversion: gas spends roughly
///    Radius / Speed seconds inside the source before buoyancy carries it out, so adding Target / that
///    residence time per second leaves the same reading standing at the centre. No tuning constant is
///    involved, which is why this one is a conversion and the turbulence above is not.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasEmission ResolveEmitter(const GasSettings& Settings, const GasDomainMeasure& Measure) noexcept
{
    GasEmission Source;

    // 💡 Zeroed up front rather than left at GasEmission's defaults, which are one and one. A disabled emitter
    //    returning early with those would inject at a unit rate — an absence that quietly emits.
    Source.SmokeRate       = 0.0f;
    Source.TemperatureRate = 0.0f;

    const float HorizontalScale = AuthoringMetricScale(Measure.Width);
    const float VerticalScale   = AuthoringMetricScale(Measure.Height / AuthoringHeightDivisor);

    // The browser places the emitter at the horizontal centre, at a height it divides by the vertical scale.
    const float HeightFraction = Settings.EmitterHeight / VerticalScale;
    Source.Position[0] = Measure.Origin[0] + 0.5f * Measure.Span;
    Source.Position[1] = Measure.Origin[1] + 0.5f * Measure.Span;
    Source.Position[2] = Measure.Origin[2] + HeightFraction * Measure.Span;

    // 🔴 THE TWO FALLOFFS ARE DIFFERENT SHAPES AND THE RADIUS IS NOT THE SAME NUMBER.
    //    The authored source is a smoothstep that reaches zero at 1.35 radii. The injector's is a gaussian
    //    that reaches zero at three sigma. Matching the two means matching where they end, so sigma is a
    //    third of the authored reach — and carrying the authored radius straight across instead makes the
    //    source four times wider, which is sixty times the volume and a box full of smoke in two seconds.
    //    That is what the first render through a scene's own camera actually showed.
    Source.Radius = Settings.EmitterRadius * AuthoringEmitterReach / 3.0f / HorizontalScale * Measure.Span;
    if (Source.Radius < 0.01f) Source.Radius = 0.01f;

    if (!Settings.EmitterEnabled || Settings.EmitterRate <= 0.001f) return Source;

    float Speed = Settings.EmitterUpwardVelocity;
    if (Speed < GasSettlingSpeed) Speed = GasSettlingSpeed;
    const float Residence = Source.Radius / Speed;

    Source.SmokeRate       = Settings.EmitterSmoke       * Settings.EmitterRate / Residence;
    Source.TemperatureRate = Settings.EmitterTemperature * Settings.EmitterRate / Residence;
    Source.Velocity[2]     = Settings.EmitterUpwardVelocity;
    return Source;
}


/// 📦 The scene's detonation as one injection, to be made once rather than every advance.
/// in    Settings   [-]  the scene's settings
/// in    Measure    [m]  the cube from ResolveDomain
/// out   GasEmission   [-]  a single burst at the blast centre
/// note  the authored blast has lobes and shrapnel; neither crosses. A lobe count is a shape in the browser's
///       injection shader and the coarse field has no such shader, so admitting the blast as a round burst is
///       the whole of what 32 cubed can carry. Lobes are a fine-field concern and are recorded as such.
/// note  the burst is injected for one step interval by the caller, so the rates below are already per second
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasEmission ResolveBlast(const GasSettings& Settings, const GasDomainMeasure& Measure) noexcept
{
    GasEmission Burst;
    const float HorizontalScale = AuthoringMetricScale(Measure.Width);

    Burst.Position[0] = Measure.Origin[0] + 0.5f * Measure.Span;
    Burst.Position[1] = Measure.Origin[1] + 0.5f * Measure.Span;
    Burst.Position[2] = Measure.Origin[2] + 0.35f * Measure.Span;

    Burst.Radius = Settings.BlastRadius / 3.0f / HorizontalScale * Measure.Span;   // three sigma, as above
    if (Burst.Radius < 0.01f) Burst.Radius = 0.01f;

    // A detonation is instantaneous: the whole of the authored reading arrives in one step, so the per-second
    //    rate is the reading divided by the step it is injected over.
    Burst.SmokeRate       = Settings.BlastSmoke       / CoarseStepInterval;
    Burst.TemperatureRate = Settings.BlastTemperature / CoarseStepInterval;
    Burst.Velocity[2]     = Settings.BlastStrength * Settings.ShockwaveStrength;
    return Burst;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      THE OBSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// What came of trying to express an authored obstacle as a collider. The flags are the honest part: an
//    approximated obstruction is still worth admitting, and is not worth admitting silently.
struct GasColliderReading
{
    GasCollider Collider;
    bool        Present                 = false;   // [-] - false for obstacle type 0
    bool        ShapeApproximated       = false;   // [-] - admitted as a different primitive than authored
    bool        OrientationApproximated = false;   // [-] - admitted about axis 2 when the author meant otherwise
    const char* AuthoredShape           = "None";  // [-] - the OBSTACLE_TYPES label, for a report
};


/// 📦 The scene's obstacle as a collider, through the written table rather than a cast.
/// in    Settings   [-]  the scene's settings
/// in    Measure    [m]  the cube from ResolveDomain
/// out   GasColliderReading   [-]  the collider and what was lost reaching it
/// err   an unrecognised obstacle type comes back absent rather than guessed at
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasColliderReading ResolveCollider(const GasSettings& Settings, const GasDomainMeasure& Measure) noexcept
{
    GasColliderReading Reading;
    if (Settings.ObstacleType <= 0 || Settings.ObstacleType > 5) return Reading;

    // The authored obstacle position is a fraction of the domain on each axis, with browser Y up.
    Reading.Collider.Centre[0] = Measure.Origin[0] + Settings.ObstacleX * Measure.Span;
    Reading.Collider.Centre[1] = Measure.Origin[1] + Settings.ObstacleZ * Measure.Span;
    Reading.Collider.Centre[2] = Measure.Origin[2] + Settings.ObstacleY * Measure.Span;

    // 🔴 The authored radius is compared against offsets already in metres, so it is a length and not a
    //    fraction. It is the one authoring reading on this path that is not normalised, which is exactly the
    //    sort of thing that is wrong for a month before anyone notices.
    const float Radius = Settings.ObstacleRadius;
    Reading.Present = true;

    switch (Settings.ObstacleType)
    {
        case 1:
            Reading.AuthoredShape = "Sphere";
            Reading.Collider.Shape = GasColliderShape::Sphere;
            Reading.Collider.Dimensions[0] = Radius;
            break;

        case 2:
        {
            // A pillar: the browser bounds it above and leaves it open below, so it stands on the floor.
            Reading.AuthoredShape = "Vertical cylinder";
            const float Top    = Reading.Collider.Centre[2] + Radius * 1.8f;
            const float Bottom = Measure.Origin[2];
            Reading.Collider.Shape = GasColliderShape::Cylinder;
            Reading.Collider.Centre[2]     = 0.5f * (Top + Bottom);
            Reading.Collider.Dimensions[0] = Radius * 0.75f;
            Reading.Collider.Dimensions[2] = 0.5f * (Top - Bottom);
            break;
        }

        case 3:
            // Lying along X, which no axis-2 primitive can be. Its bounding box diverts a plume the same way
            //    a cylinder does to within the corners, and the corners are below one coarse voxel.
            Reading.AuthoredShape = "Horizontal cylinder";
            Reading.Collider.Shape = GasColliderShape::Box;
            Reading.Collider.Dimensions[0] = Radius * 1.9f;
            Reading.Collider.Dimensions[1] = Radius * 0.72f;
            Reading.Collider.Dimensions[2] = Radius * 0.72f;
            Reading.ShapeApproximated = true;
            break;

        case 4:
            Reading.AuthoredShape = "Deflector slab";
            Reading.Collider.Shape = GasColliderShape::Box;
            Reading.Collider.Dimensions[0] = Radius * 1.5f;
            Reading.Collider.Dimensions[1] = Radius * 1.5f;
            Reading.Collider.Dimensions[2] = Radius * 0.45f;
            break;

        case 5:
            // A rolling tyre spins about a horizontal axis; GasCollider spins about axis 2. The ring radius
            //    and section survive, the plane it stands in does not.
            Reading.AuthoredShape = "Tyre ring";
            Reading.Collider.Shape = GasColliderShape::TyreRing;
            Reading.Collider.Dimensions[0] = Radius * 0.76f;
            Reading.Collider.Dimensions[1] = Radius * 0.24f;
            Reading.OrientationApproximated = true;
            break;

        default:
            Reading.Present = false;
            break;
    }
    return Reading;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                       THE LIGHTING
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The scene's lighting for the volume integration. A straight carry: VolumeLighting was named after these
///    settings precisely so that this routine would have nothing to decide.
/// in    Settings   [-]  the scene's settings
/// out   VolumeLighting   [-]  ready for MarchVolume
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline VolumeLighting ResolveLighting(const GasSettings& Settings) noexcept
{
    VolumeLighting Lighting;
    Lighting.DensityExtinction  = Settings.DensityExtinction;
    Lighting.SmokeAlbedo        = Settings.SmokeAlbedo;
    Lighting.ShadowDensity      = Settings.ShadowDensity;
    Lighting.FireIntensity      = Settings.FireIntensity;
    Lighting.TemperatureScale   = Settings.TemperatureScale;
    Lighting.InternalScattering = Settings.InternalScattering;
    Lighting.PhaseAnisotropy    = Settings.PhaseAnisotropy;
    Lighting.AmbientIntensity   = Settings.AmbientIntensity;
    Lighting.SunIntensity       = Settings.SunIntensity;
    Lighting.Exposure           = Settings.Exposure;
    Lighting.RaymarchSteps      = Settings.RaymarchSteps;
    Lighting.ShadowSteps        = Settings.ShadowSteps;
    return Lighting;
}


/// 📦 The direction light arrives from, as a unit vector with axis 2 up.
/// in    Settings   [-]  the scene's settings, read for azimuth and elevation in degrees
/// in    OutDirection   [-]  written: unit length, pointing at the sun
/// out   -
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline void ResolveSunDirection(const GasSettings& Settings, float OutDirection[3]) noexcept
{
    constexpr float Radians = 3.14159265358979323846f / 180.0f;
    const float Azimuth   = Settings.SunAzimuth   * Radians;
    const float Elevation = Settings.SunElevation * Radians;

    OutDirection[0] = std::cos(Elevation) * std::sin(Azimuth);
    OutDirection[1] = std::cos(Elevation) * std::cos(Azimuth);   // browser Z
    OutDirection[2] = std::sin(Elevation);                       // browser Y, which is up here
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        THE CAMERA
//------------------------------------------------------------------------------------------------------------------------

// A camera ready to march, in the unit-cube coordinates MarchVolume expects. The basis is orthonormal and
//    right-handed, built the same way the authoring viewport builds it so a framing survives the crossing.
struct GasSceneEye
{
    float Origin[3]    = { 0.5f, -1.5f, 0.5f };
    float Forward[3]   = { 0.0f,  1.0f, 0.0f };
    float Right[3]     = { 1.0f,  0.0f, 0.0f };
    float Up[3]        = { 0.0f,  0.0f, 1.0f };
    float HalfViewTan  = 0.425f;                 // [-] - tangent of half the vertical field of view
};


/// 📦 The scene's orbit camera, in the cube's own coordinates.
/// in    Camera    [-]  the scene's spherical camera, in authoring world units with Y up
/// in    Measure   [m]  the cube from ResolveDomain
/// out   GasSceneEye   [-]  origin and basis, the cube spanning [0,1] on every axis
/// note  the orbit distance is in authoring world units, where the resting domain is about 1.85 wide, so it
///       divides by the span rather than being taken as a count of cube widths
/// err   a degenerate framing (the eye on the centre, or looking straight down the up axis) is nudged rather
///       than refused, because a camera is not worth failing a scene over
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasSceneEye ResolveEye(const GasSceneCamera& Camera, const GasDomainMeasure& Measure) noexcept
{
    const float Phi      = static_cast<float>(Camera.Phi);
    const float Theta    = static_cast<float>(Camera.Theta);
    const float Distance = static_cast<float>(Camera.Distance);

    // Browser world, Y up: the orbit as camera.js computes it.
    const float TargetX = static_cast<float>(Camera.Centre[0]);
    const float TargetY = static_cast<float>(Camera.Centre[1]);
    const float TargetZ = static_cast<float>(Camera.Centre[2]);
    const float EyeX = TargetX + Distance * std::sin(Phi) * std::sin(Theta);
    const float EyeY = TargetY + Distance * std::cos(Phi);
    const float EyeZ = TargetZ + Distance * std::sin(Phi) * std::cos(Theta);

    // Into the cube: axes swapped, the floor raised to z = 0, lengths divided by the span.
    const float Inverse = 1.0f / Measure.Span;
    const float Centre[3] = { 0.5f + TargetX * Inverse,
                              0.5f + TargetZ * Inverse,
                              (TargetY + AuthoringFloorHeight) * Inverse };
    GasSceneEye Eye;
    Eye.Origin[0] = 0.5f + EyeX * Inverse;
    Eye.Origin[1] = 0.5f + EyeZ * Inverse;
    Eye.Origin[2] = (EyeY + AuthoringFloorHeight) * Inverse;

    float Forward[3] = { Centre[0] - Eye.Origin[0], Centre[1] - Eye.Origin[1], Centre[2] - Eye.Origin[2] };
    float Reach = std::sqrt(Forward[0] * Forward[0] + Forward[1] * Forward[1] + Forward[2] * Forward[2]);
    if (Reach < 1e-5f) { Forward[0] = 0.0f; Forward[1] = 1.0f; Forward[2] = 0.0f; Reach = 1.0f; }
    for (int Axis = 0; Axis < 3; ++Axis) Eye.Forward[Axis] = Forward[Axis] / Reach;

    // Right is forward crossed with up, and up is axis 2 here rather than the browser's axis 1.
    float Right[3] = { Eye.Forward[1] * 1.0f - Eye.Forward[2] * 0.0f,
                       Eye.Forward[2] * 0.0f - Eye.Forward[0] * 1.0f,
                       0.0f };
    float RightReach = std::sqrt(Right[0] * Right[0] + Right[1] * Right[1] + Right[2] * Right[2]);
    if (RightReach < 1e-5f) { Right[0] = 1.0f; Right[1] = 0.0f; Right[2] = 0.0f; RightReach = 1.0f; }
    for (int Axis = 0; Axis < 3; ++Axis) Eye.Right[Axis] = Right[Axis] / RightReach;

    Eye.Up[0] = Eye.Right[1] * Eye.Forward[2] - Eye.Right[2] * Eye.Forward[1];
    Eye.Up[1] = Eye.Right[2] * Eye.Forward[0] - Eye.Right[0] * Eye.Forward[2];
    Eye.Up[2] = Eye.Right[0] * Eye.Forward[1] - Eye.Right[1] * Eye.Forward[0];

    constexpr float Radians = 3.14159265358979323846f / 180.0f;
    Eye.HalfViewTan = std::tan(AuthoringViewAngle * 0.5f * Radians);
    return Eye;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                     RUNNING THE SCENE
//------------------------------------------------------------------------------------------------------------------------

// Everything a scene needs to be advanced, resolved once. Held together so a caller cannot advance a field
//    with one scene's tuning and another's obstruction, which is the sort of thing a pair of loose locals
//    eventually allows.
struct GasSceneRun
{
    GasDomainMeasure   Measure;
    CoarseGasSettings  Tuning;
    GasEmission        Emitter;
    GasColliderReading Obstruction;
    VolumeLighting     Lighting;
    float              SunDirection[3] = { 0.0f, 0.0f, 1.0f };

    // The authored readings the source asserts, as opposed to the rates at which it reaches them. See
    //    ApplySourceCeiling below for why both are carried.
    float SmokeCeiling       = 0.0f;   // [-]   - emitterSmoke
    float TemperatureCeiling = 0.0f;   // [K]   - emitterTemperature
    float UpwardCeiling      = 0.0f;   // [m/s] - emitterUpwardVelocity

    bool  Enclosed = false;            // [-]   - enclosedBox; when false the sides and roof vent

    // ⚠️ A scene with no emitter and a loaded blast is a one-shot, and NOTHING IN THE FILE SAYS SO.
    //    The authoring tool knows, because the preset it was built from carries DetonateOnLoad — but a scene
    //    stores its settings, not the preset it came from, so that knowledge does not cross. Two of the eight
    //    committed samples are one-shots, and advancing them without firing anything leaves an empty cube
    //    that passes every structural check there is. Inferring it from the settings is the honest reading of
    //    what is actually in the file; carrying a trigger explicitly is a format question, recorded as one.
    bool  OneShot = false;             // [-]   - fire the blast on the first advance
};


/// 📦 Vents the sides and roof, as the authoring tool does for a domain that is not enclosed.
/// in    Field   [-]  the field, after its advance
/// out   -
/// 🔴 A CLOSED CUBE IS NOT A NEUTRAL DEFAULT; IT IS A DIFFERENT EFFECT.
///    CoarseGasField conserves smoke by construction, which is right for the determinism checks and wrong for
///    a camp fire: a continuous source in a sealed box fills it, and the first render through a scene's own
///    camera came back a flat orange wall at a hundred per cent coverage. The browser is not doing anything
///    cleverer — it multiplies the outer band by a smoothstep every frame, and smoke that reaches the
///    boundary leaves. `enclosedBox` is an authored setting precisely because both behaviours are wanted.
/// note  the floor does not vent. Gas pools on the ground in the authoring tool too, and that is the look.
/// cost  🚩  one pass over 32768 voxels
/// tag   api, nonallocating, nonthrowing
inline void VentOpenBoundary(CoarseGasField& Field) noexcept
{
    constexpr float Band = 0.045f;   // [-] - the authoring tool's band, as a fraction of the domain

    for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
    for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
    for (uint32_t X = 0u; X < CoarseExtent; ++X)
    {
        const float Fx = (static_cast<float>(X) + 0.5f) / static_cast<float>(CoarseExtent);
        const float Fy = (static_cast<float>(Y) + 0.5f) / static_cast<float>(CoarseExtent);
        const float Fz = (static_cast<float>(Z) + 0.5f) / static_cast<float>(CoarseExtent);

        const float EdgeX = Fx < 1.0f - Fx ? Fx : 1.0f - Fx;
        const float EdgeY = Fy < 1.0f - Fy ? Fy : 1.0f - Fy;
        const float Nearest = EdgeX < EdgeY ? EdgeX : EdgeY;
        const float Roof    = 1.0f - Fz;

        const float Side = ClampUnit(Nearest / Band, 0.0f, 1.0f);
        const float Top  = ClampUnit(Roof    / Band, 0.0f, 1.0f);
        const float Sponge = (Side * Side * (3.0f - 2.0f * Side)) * (Top * Top * (3.0f - 2.0f * Top));
        if (Sponge >= 1.0f) continue;

        const uint32_t Slot = VoxelIndex(X, Y, Z);
        Field.Smoke[Slot]       *= Sponge;
        Field.Temperature[Slot] *= Sponge;
    }
}


/// 📦 Holds the source region to the readings the scene authored, after the injector has added to it.
/// in    Field   [-]  the field, just injected into
/// in    Run     [-]  the resolved scene
/// out   -
/// 🔴 WHY A CEILING EXISTS AT ALL, AND WHY LEAVING IT OUT IS NOT A SMALL DIFFERENCE.
///    The authoring emitter *asserts* a reading: every frame it takes the larger of what is there and what it
///    wants, so `emitterSmoke = 1.4` means the source is at 1.4 and stays at 1.4. The solver's injector *adds*
///    a rate, and an additive source only settles where addition and removal balance — which, for a camp fire
///    whose smoke leaves at under a metre a second, is several times the authored reading. Measured before
///    this existed: a 3.5 K emitter reached 17 K and a 3.2 m/s one reached 80 m/s. Those are not
///    near-misses; a plume five times too hot is a different effect with the same name in the outliner.
///    Injecting and then capping gives the authored reading on both sides while leaving the injector additive,
///    which is what a fracture or a tyre needs it to be.
/// note  only the source region is capped. The field away from it is the solver's, and clamping that would be
///       inventing a loss the scene never asked for.
/// cost  🚩  touches only the voxels within three radii of the source
/// tag   api, nonallocating, nonthrowing
inline void ApplySourceCeiling(CoarseGasField& Field, const GasSceneRun& Run) noexcept
{
    if (Run.SmokeCeiling <= 0.0f && Run.TemperatureCeiling <= 0.0f && Run.UpwardCeiling <= 0.0f) return;

    const float Size   = VoxelSize(Field);
    const float Radius = Run.Emitter.Radius > 0.0f ? Run.Emitter.Radius : Size;
    float Centre[3];
    LatticeCoordinate(Field, Run.Emitter.Position, Centre);

    const float Spread      = Radius / Size;
    const float ReachVoxels = 3.0f * Spread;
    const int32_t Low[3]  = { static_cast<int32_t>(std::floor(Centre[0] - ReachVoxels)),
                              static_cast<int32_t>(std::floor(Centre[1] - ReachVoxels)),
                              static_cast<int32_t>(std::floor(Centre[2] - ReachVoxels)) };
    const int32_t High[3] = { static_cast<int32_t>(std::ceil(Centre[0] + ReachVoxels)),
                              static_cast<int32_t>(std::ceil(Centre[1] + ReachVoxels)),
                              static_cast<int32_t>(std::ceil(Centre[2] + ReachVoxels)) };

    for (int32_t Z = Low[2]; Z <= High[2]; ++Z)
    {
        if (Z < 0 || Z >= static_cast<int32_t>(CoarseExtent)) continue;
        for (int32_t Y = Low[1]; Y <= High[1]; ++Y)
        {
            if (Y < 0 || Y >= static_cast<int32_t>(CoarseExtent)) continue;
            for (int32_t X = Low[0]; X <= High[0]; ++X)
            {
                if (X < 0 || X >= static_cast<int32_t>(CoarseExtent)) continue;
                const float Dx = static_cast<float>(X) - Centre[0];
                const float Dy = static_cast<float>(Y) - Centre[1];
                const float Dz = static_cast<float>(Z) - Centre[2];
                if ((Dx * Dx + Dy * Dy + Dz * Dz) / (Spread * Spread) > 9.0f) continue;

                const uint32_t Slot = VoxelIndex(static_cast<uint32_t>(X), static_cast<uint32_t>(Y),
                                                 static_cast<uint32_t>(Z));
                if (Field.Smoke[Slot]       > Run.SmokeCeiling)       Field.Smoke[Slot]       = Run.SmokeCeiling;
                if (Field.Temperature[Slot] > Run.TemperatureCeiling) Field.Temperature[Slot] = Run.TemperatureCeiling;
                if (Field.VelocityZ[Slot]   > Run.UpwardCeiling)      Field.VelocityZ[Slot]   = Run.UpwardCeiling;
            }
        }
    }
}


/// 📦 Resolves every part of a scene at once, and resets the field to the cube it implies.
/// in    Scene   [-]  the scene, as read from a file or constructed from a preset
/// in    Field   [-]  reset and sized here; any previous contents are discarded
/// out   GasSceneRun   [-]  the resolved scene
/// cost  ✔️  one field reset
/// tag   api, allocating, nonthrowing
inline GasSceneRun ResolveScene(const GasScene& Scene, CoarseGasField& Field) noexcept
{
    GasSceneRun Run;
    Run.Measure     = ResolveDomain(Scene.Settings);
    Run.Tuning      = ResolveSolverTuning(Scene.Settings);
    Run.Emitter     = ResolveEmitter(Scene.Settings, Run.Measure);
    Run.Obstruction = ResolveCollider(Scene.Settings, Run.Measure);
    Run.Lighting    = ResolveLighting(Scene.Settings);
    ResolveSunDirection(Scene.Settings, Run.SunDirection);

    if (Scene.Settings.EmitterEnabled)
    {
        Run.SmokeCeiling       = Scene.Settings.EmitterSmoke;
        Run.TemperatureCeiling = Scene.Settings.EmitterTemperature;
        Run.UpwardCeiling      = Scene.Settings.EmitterUpwardVelocity;
    }
    Run.Enclosed = Scene.Settings.EnclosedBox;
    Run.OneShot  = !Scene.Settings.EmitterEnabled
                && (Scene.Settings.BlastSmoke > 0.0f || Scene.Settings.BlastTemperature > 0.0f);

    ResetField(Field, Run.Measure.Origin, Run.Measure.Span);
    return Run;
}


/// 📦 One advance of a resolved scene: obstruction admitted, emitter injected, field stepped.
/// in    Field   [-]  the field, already reset by ResolveScene
/// in    Run     [-]  the resolved scene
/// out   -
/// note  the obstruction is re-admitted every advance rather than once, because occupancy is rewritten and
///       not accumulated — see GasCollisionIntake.h. A collider that never moves pays for that; a collider
///       that does move is correct, and the second is worth more than the first.
/// note  this is the only routine here that is not a pure conversion, and it takes no duration argument, for
///       the determinism reason recorded in CoarseGasField.h
/// cost  🚩  one pressure projection and one occupancy rewrite
/// tag   api, nonallocating, nonthrowing
inline void AdvanceScene(CoarseGasField& Field, const GasSceneRun& Run) noexcept
{
    ClearOccupancy(Field);
    if (Run.Obstruction.Present) AdmitPrimitives(Field, &Run.Obstruction.Collider, 1u);
    if (Run.Emitter.SmokeRate > 0.0f || Run.Emitter.TemperatureRate > 0.0f)
    {
        InjectEmission(Field, Run.Emitter, CoarseStepInterval);
        ApplySourceCeiling(Field, Run);
    }
    AdvanceField(Field, Run.Tuning);
    if (!Run.Enclosed) VentOpenBoundary(Field);
}

}   // namespace Frontier
