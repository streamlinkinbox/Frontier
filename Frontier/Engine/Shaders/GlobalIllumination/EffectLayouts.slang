//============================================================================================================================================
//                                                          EFFECTLAYOUTS.SLANG
//============================================================================================================================================
// 🧩 The derez / rez effect as both the surface raster and the effect raster read it: luminaires, the vehicle mask, the release clock.

#pragma once
#include "IntrinsicParity.slang"

// 📝 One clock decides everything. A point of the car (or of a cone's box) belongs to a cell of a 16³ lattice over its
//    bounds; the cell's release time is a wave across the bounds plus a hashed jitter. The surface raster discards a
//    car pixel once its cell is released (glowing white-hot, then cyan, just before), and the effect raster launches
//    a cube from every anchor of that cell at the same instant — so the car comes apart exactly where the cubes leave
//    it, with no state common-domain between the two but this function. Rez runs it backwards: cubes fly in and each cell
//    of the car appears as its cubes arrive.
//
//    Colour: with Colour.x set, every cube is the car — each anchor carries the material family of the facet it was
//    spread on, so the lime coat comes away as lime cubes, the glass as glass, the tyres as rubber — and glows in
//    that colour (G-span Attributes.w = 1 + glow: the recombination adds albedo × glow as incandescence). Otherwise the
//    Particle Editor's palette families (cyan glow, white-hot core, glitch pink) stand in.
//
//    Momentum: the car keeps moving while it comes apart, so a cell must leave from where the car is when that cell
//    lets go. EffectSequence samples the car's pose and velocity at 60 Hz from the derez on (Colour.z samples from
//    clock Colour.w); a cube launches from the pose at its release time with the car's velocity there (plus the spin
//    about the centre of mass) — the falling blocks carry on with the car.

struct EffectSource
{
    float4x4 BodyToWorld;    // the luminaire's pose when it started (local → world)
    float4 Velocity;         // xyz [m/s] velocity the cubes inherit, w [s] start on the effect clock
    float4 Angular;          // xyz [rad/s] spin they inherit, w kind (EffectKind*)
    float4 Extent;           // xyz [m] half extent of the local bounds, w particle count
    float4 Centre;           // xyz [m] centre of the local bounds, w seed
};

struct EffectProfile
{
    float Clock;              // [s]  effect clock now
    uint MaskMode;           // [-]  vehicle mask: 0 off, 1 derez, 2 rez, 3 hidden
    float MaskStart;          // [s]  clock the mask started at
    uint Style;              // [-]  0 cubes, 1 glitch
    float4 BodyMinimum;      // xyz [m] the car's local bounds, w hold offset [s]
    float4 BodyMaximum;      // xyz [m], w refresh seconds (motion vectors)
    float4 Motion;           // x burst [m/s], y slowest launch [m/s], z fastest launch [m/s], w drag [1/s]
    float4 Timing;           // x cube life [s], y cube size [m], z wave spread [s], w rez flight [s]
    float4 Colour;           // x face colours (1) or palette (0), y glow strength [-], z pose samples, w first sample's clock [s]
    EffectSource Luminaires[8];    
};

// 📝 One sample of the car's motion during a derez (EffectSequence.RecordPose), 60 Hz on the effect clock.
struct EffectPose
{
    float4 Position;         // xyz [m] centre of mass, w [s] clock
    float4 Rotation;         // body → world quaternion (x, y, z, w)
    float4 Velocity;         // xyz [m/s]
    float4 Angular;          // xyz [rad/s]
};

static const float EffectPoseSpacing = 1.0 / 60.0;   // [s]

float3 RotateByQuaternion(float4 Rotation, float3 Vector)
{
    let T = 2.0 * cross(Rotation.xyz, Vector);
    return Vector + Rotation.w * T + cross(Rotation.xyz, T);
}

float ResolveFaceGlow(float Seconds, float Strength)
{
    // 📝 A flash at release, then an afterglow a third of a second long.
    return Strength * (SelectBranch(0.0, 2.5, Seconds < EffectHotSeconds) + exp(-max(Seconds, 0.0) / 0.35));
}

static const uint EffectKindVehicleDerez = 0u;
static const uint EffectKindVehicleRez = 1u;
static const uint EffectKindBoxDerez = 2u;
static const uint EffectKindBoxRez = 3u;
static const uint EffectKindDebris = 4u;   // a burst tyre's rubber: released at once, thrown hard, never glowing
static const float EffectCellsPerAxis = 16.0;
static const float EffectHotSeconds = 0.07;   // [s] white-hot just before release / after arrival
static const float EffectGlowSeconds = 0.22;   // [s] cyan glow before that

float HashEffect(uint Value)
{
    var State = Value * 747796405u + 2891336453u;
    State = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    State = (State >> 22u) ^ State;
    return float(State) / 4294967296.0;
}

float HashEffectCell(uint3 Cell, uint Seed)
{
    return HashEffect((Cell.x * 73856093u) ^ (Cell.y * 19349663u) ^ (Cell.z * 83492791u) ^ (Seed * 2654435761u));
}

// 📝 Unit: the point within the luminaire's bounds, [0, 1]³. Glitch style uses coarser, uneven 7 × 5 × 7 cells and
//    rises from the floor; cubes style sweeps the diagonal of 16³ cells.
uint3 ResolveEffectCell(float3 Unit, uint Style)
{
    let Divisions = SelectBranch(float3(EffectCellsPerAxis), float3(7.0, 5.0, 7.0), Style == 1u);
    return uint3(clamp(Unit, float3(0.0), float3(0.9999)) * Divisions);
}

float ResolveEffectWave(float3 Unit, uint Style)
{
    let Divisions = SelectBranch(float3(EffectCellsPerAxis), float3(7.0, 5.0, 7.0), Style == 1u);
    let Centre = (float3(ResolveEffectCell(Unit, Style)) + 0.5) / Divisions;
    return SelectBranch((Centre.x + Centre.y + Centre.z) / 3.0, 0.4 * Centre.z, Style == 1u);
}

// 📝 Seconds after the derez starts at which this cell lets go. Scale shortens it for small luminaires (cones).
float ResolveDerezRelease(float3 Unit, uint Seed, uint Style, float Scale)
{
    let Jitter = HashEffectCell(ResolveEffectCell(Unit, Style), Seed);
    let Spread = SelectBranch(0.15 * Jitter, 1.0 * Jitter, Style == 1u);
    return Scale * (Effect.BodyMinimum.w + Effect.Timing.z * (ResolveEffectWave(Unit, Style) + Spread));
}

// 📝 Seconds after the rez starts at which this cell is complete again.
float ResolveRezArrival(float3 Unit, uint Seed, uint Style, float Scale)
{
    let Jitter = HashEffectCell(ResolveEffectCell(Unit, Style), Seed ^ 0x9E3779B9u);
    return Scale * (0.3 + Effect.Timing.w * (ResolveEffectWave(Unit, Style) + 0.15 * Jitter));
}
