//============================================================================================================================================
//                                                            SMOKERECORDS.WGSL
//============================================================================================================================================
// 🧩 Drift-smoke lattice records: the per-tick transport record, the per-frame view record, toroidal cell and brick addressing.

// 📝 Two world-anchored lattices follow the car, both addressed toroidally like the scene distance field: world cell c
//    lives at texel c mod Extent, so moving the window never copies a retained cell — only the newly covered cells are
//    rewritten (Frontier's SmokeSpace.h copies every channel of every cell on each relocation).
//
//      fine   : FineExtent cells of FineCell (0.25 m) in two rgba16float 3D textures — smoke, heat, flame, soot;
//      coarse : FineExtent/2 cells of CoarseCell (0.5 m) in storage buffers — velocity and heat (the solved flow),
//               pressure, divergence, obstacles; the same cells also hold the per-tick light volume.
//
//    The fine lattice is processed in 8³ bricks. A brick's flag says whether its texels in one fine texture are all
//    zero; a brick whose whole 3×3×3 neighbourhood is empty, and whose target is already empty, is skipped outright.
//    Fine bricks are aligned to SmokeAnchor, so the ground (z = 0) falls two fine cells into the first brick layer.

const SmokeBrickEdge   : i32 = 8;                        // [cell]
const SmokeAnchor      : vec3i = vec3i(0, 0, -2);        // [cell] - world cell at a brick corner
const SmokeSummaryBias : i32 = 1048576;                  // [brick] - keeps signed brick coordinates positive in u32

struct SmokeSource
{
    Start    : vec4f,   // xyz swept path start [m], w radius [m]
    End      : vec4f,   // xyz swept path end [m], w smoke rate [1/s]
    Velocity : vec4f,   // xyz carrier velocity [m/s], w heat rate [K/s]
    Rates    : vec4f,   // x flame rate [1/s], y soot rate [1/s], z momentum rate [1/s], w expansion [1/s]
}

struct SmokeRecord
{
    FineOrigin        : vec3i,   // [cell]  world cell of the fine window's minimum corner
    SourceCount       : u32,     // [-]
    PriorFineOrigin   : vec3i,   // [cell]  the previous tick's window; history outside it is never read
    VehicleOccupants  : u32,     // [-]     identities [0, n) belong to the vehicle, a moving obstacle
    FineExtent        : vec3u,   // [cell]
    BrickCount        : u32,     // [-]
    BrickExtent       : vec3u,   // [brick]
    FineParity        : u32,     // [-]     fine texture this tick writes (its flags are FineParity · BrickCount + slot)
    FineCell          : f32,     // [m]
    CoarseCell        : f32,     // [m]
    Step              : f32,     // [s]     fixed tick
    Time              : f32,     // [s]
    SmokeRemaining    : f32,     // [-]     per-tick survival of each channel
    HeatRemaining     : f32,
    VelocityRemaining : f32,
    FlameRemaining    : f32,
    SootRemaining     : f32,
    Fade              : f32,     // [-]     lifetime fade once every source has stopped
    BuoyancyLift      : f32,     // [m/s²/K]
    SpeedLimit        : f32,     // [m/s]
    CurlStrength      : f32,     // [m/s]
    CurlScale         : f32,     // [1/m]
    TopFade           : f32,     // [-]     survival per tick in the top quarter of the window
    Extinction        : f32,     // [1/m]   per unit density, for self-shadowing
    SourceMinimum     : vec3f,   // [m]     bounds of every source footprint this tick
    FireCount         : u32,     // [-]
    SourceMaximum     : vec3f,   // [m]
    AmbientScale      : f32,     // [-]     sky-view scale
    ChassisPosition   : vec3f,   // [m]     vehicle centre of mass
    Vorticity         : f32,     // [-]     vorticity-confinement strength ε (Fedkiw 2001); 0 disables it
    ChassisLinear     : vec3f,   // [m/s]
    WindCoupling      : f32,     // [1/s]   rate at which open air relaxes toward the wind
    ChassisAngular    : vec3f,   // [rad/s]
    WindX             : f32,     // [m/s]   ambient wind, world x
    SunDirection      : vec3f,   // [-]     toward the key light (sun, or moon at night)
    WindY             : f32,     // [m/s]   ambient wind, world y
    SunIlluminance    : vec3f,   // [lx·k]
    Reserved          : f32,
    FireLights        : array<vec4f, 4>,   // xyz burning tyre [m], w intensity
    FireColour        : vec4f,             // rgb chromaticity of the flame light
    Sources           : array<SmokeSource, 8>,
}

struct SmokeView
{
    FineOrigin     : vec3i,   // [cell]
    Parity         : u32,     // [-]  fine texture holding the latest state
    FineExtent     : vec3u,   // [cell]
    BrickCount     : u32,
    BrickExtent    : vec3u,   // [brick]
    MarchSteps     : u32,     // [-]
    FineCell       : f32,     // [m]
    CoarseCell     : f32,     // [m]
    Time           : f32,     // [s]
    Extinction     : f32,     // [1/m] per unit density
    SmokeAlbedo    : vec3f,   // [-]  friction smoke
    PhaseForward   : f32,     // [-]  forward-lobe anisotropy
    SootAlbedo     : vec3f,   // [-]  combustion soot
    FlameScale     : f32,     // [-]  flame radiance per unit provenance
    DetailScale    : f32,     // [1/m]
    DetailStrength : f32,     // [-]
    StepLength     : f32,     // [m]
    HeatScale      : f32,     // [1/K] heat to flame-palette coordinate
    MarchExtent    : vec2f,   // [px]
    MarchScale     : u32,     // [-]  screen pixels per march pixel along one axis
    Enabled        : u32,
    Wind           : vec3f,   // [m/s] ambient wind: the detail noise drifts with it
    MultiScatter   : f32,     // [-]  weight of the higher scattering octaves
    Erosion        : f32,     // [-]  how strongly the detail eats the thin edges of the plume
    ErosionDensity : f32,     // [-]  density at which smoke counts as core rather than edge
    AmbientScale   : f32,     // [-]  sky, bounce and fire light on the smoke
    Warp           : f32,     // [-]  domain-warp amplitude of the detail, in detail periods
    FlameFlicker   : f32,     // [-]
    FlameTemperature : f32,   // [-]  scale on the mapped flame temperature
    SunScale       : f32,     // [-]  key light on the smoke
    Powder         : f32,     // [-]  in-scattering deficit at the sunward skin of dense smoke
    FlameTip       : f32,     // [K]  the flame's cool fringe
    FlameCore      : f32,     // [K]  its hottest core
    HeatHalf       : f32,     // [-]  heat at which the flame sits halfway between tip and core
    Rise           : f32,     // [m/s] the detail's upward drift with the plume
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

fn WrapLattice(Cell: vec3i, Extent: vec3i) -> vec3i
{
    return ((Cell % Extent) + Extent) % Extent;
}

fn FloorDivide(Numerator: vec3i, Denominator: i32) -> vec3i
{
    let Remainder = ((Numerator % Denominator) + Denominator) % Denominator;
    return (Numerator - Remainder) / Denominator;
}

// 📝 World brick of a world fine cell, and the toroidal slot that brick occupies in a window of BrickExtent bricks.
fn ResolveWorldBrick(WorldCell: vec3i) -> vec3i
{
    return FloorDivide(WorldCell - SmokeAnchor, SmokeBrickEdge);
}

fn ResolveSmokeBrickSlot(WorldBrick: vec3i, BrickExtent: vec3u) -> u32
{
    let Wrapped = vec3u(WrapLattice(WorldBrick, vec3i(BrickExtent)));
    return (Wrapped.z * BrickExtent.y + Wrapped.y) * BrickExtent.x + Wrapped.x;
}

// 📐 Phase of the smoke: a forward Henyey–Greenstein lobe mixed with a weak backward one (Frontier's ResolveSmokePhase).
fn ResolveHenyeyGreenstein(Cosine: f32, Anisotropy: f32) -> f32
{
    let Squared = Anisotropy * Anisotropy;
    return (1.0 - Squared) / (12.56637061436 * pow(max(1.0 + Squared - 2.0 * Anisotropy * Cosine, 1.0e-4), 1.5));
}

// 📐 Frontier's smoke phase generalised (the sibling Plume's): a forward lobe g mixed 72 % with a backward lobe
//    −0.65 g — what gives a back-lit plume its silver lining and a front-lit one its soft, bright face.
fn ResolveSmokePhase(Cosine: f32, Anisotropy: f32) -> f32
{
    return mix(ResolveHenyeyGreenstein(Cosine, -0.65 * Anisotropy), ResolveHenyeyGreenstein(Cosine, Anisotropy), 0.72);
}

// 📐 The colour of a blackbody at Temperature [K], linear Rec.709, unit maximum channel: Helland's fit of the
//    Planckian locus (sRGB-encoded) decoded to linear. A tyre fire's soot glows 1100–1500 K at its tips and near
//    1800 K in its core.
fn ResolveBlackbodyColour(Temperature: f32) -> vec3f
{
    let Hecto = clamp(Temperature, 1000.0, 6600.0) / 100.0;
    let Green = clamp((99.4708025861 * log(Hecto) - 161.1195681661) / 255.0, 0.0, 1.0);
    let Blue = select(clamp((138.5177312231 * log(max(Hecto - 10.0, 1.0)) - 305.0447927307) / 255.0, 0.0, 1.0), 0.0, Hecto <= 19.0);
    return pow(vec3f(1.0, Green, Blue), vec3f(2.2));
}

// 📐 The authored flame palette of Frontier's ResolveSmokeEmission(): black, deep red, orange, yellow-white over a
//    mapped temperature coordinate in [0, 1.6]. Art-directed, not a calibrated Planck spectrum.
fn ResolveFlameColour(Mapped: f32) -> vec3f
{
    let Clamped = clamp(Mapped, 0.0, 1.6);
    if (Clamped <= 0.01)
    {
        return vec3f(0.0);
    }
    let Scaled = Clamped / 1.6 * 3.0;
    let Lower = min(u32(Scaled), 2u);
    let Fraction = Scaled - f32(Lower);
    var Stops = array<vec3f, 4>(vec3f(0.0), vec3f(0.55, 0.03, 0.005), vec3f(1.0, 0.24, 0.01), vec3f(1.0, 0.86, 0.52));
    return mix(Stops[Lower], Stops[Lower + 1u], Fraction);
}

// 📐 Blackbody chromaticity in linear Rec.709, normalised to a unit red channel (Charity's CIE 1931 2° table, sRGB-decoded),
//    over 800–4000 K; the caller multiplies by (T / T₀)⁴ for the Stefan–Boltzmann brightness.
fn ResolveBlackbody(Kelvin: f32) -> vec3f
{
    var Green = array<f32, 10>(0.015, 0.040, 0.087, 0.130, 0.171, 0.209, 0.250, 0.356, 0.456, 0.637);
    var Blue  = array<f32, 10>(0.0,   0.0,   0.0,   0.0,   0.0,   0.0,   0.006, 0.065, 0.147, 0.366);
    var Stops = array<f32, 10>(800.0, 1000.0, 1200.0, 1400.0, 1600.0, 1800.0, 2000.0, 2500.0, 3000.0, 4000.0);
    let Clamped = clamp(Kelvin, 800.0, 4000.0);
    var Lower = 0u;
    for (var Index = 1u; Index < 9u; Index++)
    {
        if (Clamped >= Stops[Index])
        {
            Lower = Index;
        }
    }
    let Fraction = clamp((Clamped - Stops[Lower]) / (Stops[Lower + 1u] - Stops[Lower]), 0.0, 1.0);
    return vec3f(1.0, mix(Green[Lower], Green[Lower + 1u], Fraction), mix(Blue[Lower], Blue[Lower + 1u], Fraction));
}
