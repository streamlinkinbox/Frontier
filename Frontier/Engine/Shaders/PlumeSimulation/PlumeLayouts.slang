//============================================================================================================================================
//                                                          PLUMELAYOUTS.SLANG
//============================================================================================================================================
// 🧩 The smoke-and-fire cell lattices: the step profile, the source and fire-light profiles, and toroidal addressing shared by every plume
//    program. A port of PlumeRecords.wgsl.

#pragma once
#include "IntrinsicParity.slang"

// 📝 Two cell lattices follow the vehicle, addressed toroidally exactly as the scene distance field's cascades are:
//    world cell c lives at texel c mod N, so the window moves by relabelling texels, never by copying them.
//
//    | Lattice  | Edge | Carries                                                                                   |
//    |----------|------|-------------------------------------------------------------------------------------------|
//    | velocity | 2h   | xyz velocity [m/s], w 1 where the scene distance field is solid                           |
//    | thermo   | h    | r rubber smoke [-], g temperature above ambient [K], b flame [-], a soot [-]               |
//    | light    | 2h   | r transmittance toward the sun, g toward the zenith, through the plume                    |
//
//    Two particulates, because a burnout and a tyre fire are different smokes: vapourised rubber condenses into a bright, highly scattering
//    white-grey cloud (albedo ≈ 0.9), while burning rubber sheds black soot that absorbs almost everything it meets (albedo ≈ 0.1). Both ride
//    the same velocity; the march mixes their optics per sample. XY wrap; Z does not — the window's floor is fixed under the course and its
//    height covers the plume.

struct PlumeConstants
{
    int4 VelocityOrigin;        // [cell] xyz world coarse cell of the window's minimum corner (z is 0), w step ordinal
    int4 PriorVelocityOrigin;   // [cell] xyz the window the lattice state was written in, w source count
    uint4 VelocityExtent;       // [cell] xyz coarse lattice extent, w unused
    uint4 ThermoExtent;         // [cell] xyz fine lattice extent, w inspection (1 shows the cells)
    float4 Lattice;             // x fine edge h [m], y coarse edge 2h [m], z floor height [m], w step duration [s]
    float4 Forces;              // x buoyancy [m/s²/K], y smoke weight [m/s²], z vorticity confinement [-], w curl turbulence [m/s]
    float4 Retention;           // [-] x smoke, y heat, z flame, w velocity — multipliers over this step
    float4 Wind;                // xyz ambient flow [m/s], w elapsed seconds [s]
    float4 SourceMinimum;       // xyz bounds of every source [m], w refresh duration for moving-solid velocity [s]
    float4 SourceMaximum;       // xyz [m], w flame emission scale [-]
    float4 Optical;             // x smoke extinction per density [1/m], y smoke albedo [-], z soot extinction per density [1/m], w march samples
    float4 Sun;                 // xyz unit direction toward the sun, w soot albedo [-]
    float4 Combustion;          // x soot shed per flame per second [1/s], y heat released per flame per second [K/s], z soot retention [-], w unused
    float4 Scattering;          // x multiple-scattering strength [-], y phase anisotropy g [-], z ambient scale [-], w fire-light scale [-]
    float4 Detail;              // x sub-cell detail strength [-], y detail frequency [1/m], z turbulence frequency [1/m], w unused
    float4 Flame;               // x core temperature [K], y tip temperature [K], z heat at which a cell reaches the core [K], w unused
};

struct PlumeSource
{
    float4 PositionRadius;      // xyz [m] world centre, w [m] Gaussian radius
    float4 Velocity;            // xyz [m/s] injected momentum
    float4 Rates;               // x smoke [1/s], y heat [K/s], z flame [1/s], w soot [1/s]
};

// 📝 Burning tyres and exhaust pops as point lights: the march scatters them inside the smoke and the direct-radiance pass lights the car and
//    the road with them. Radiance is flux per steradian [lx·m²·k], position the flame centre.
struct FireLightConstants
{
    uint4 Count;                // x lights in use
    float4 PositionRadius[4];   // xyz [m], w [m] source radius that softens the inverse square
    float4 Radiance[4];         // rgb intensity, w unused
};

static const float PlumeSpeedCeiling = 40.0;   // [m/s] - no cell is allowed to outrun this, whatever the sources say

// 📝 WGSL atomicStore has no result; the original value of InterlockedExchange lands here.
static int PlumeAtomicSink;   // [-] - sink for the signed workgroup atomics

//------------------------------------------------------------------------------------------------------------------------
//                                                 TOROIDAL ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

int3 WrapLattice(int3 Cell, uint3 Extent)
{
    let Edge = int3(Extent);
    return int3(((Cell.xy % Edge.xy) + Edge.xy) % Edge.xy, Cell.z);
}

// 📝 Texel of a local velocity cell (0 … extent − 1 inside the current window); local may step one cell outside.
int3 ResolveVelocityTexel(PlumeConstants Plume, int3 Local)
{
    return WrapLattice(Plume.VelocityOrigin.xyz + Local, Plume.VelocityExtent.xyz);
}

int3 ResolveThermoTexel(PlumeConstants Plume, int3 Local)
{
    return WrapLattice(int3(2 * Plume.VelocityOrigin.xy, 0) + Local, Plume.ThermoExtent.xyz);
}

float3 ProjectVelocityWorld(PlumeConstants Plume, uint3 Local)
{
    let Cell = float3(Plume.VelocityOrigin.xyz + int3(Local)) + 0.5;
    return float3(Cell.xy * Plume.Lattice.y, Plume.Lattice.z + Cell.z * Plume.Lattice.y);
}

float3 ProjectThermoWorld(PlumeConstants Plume, uint3 Local)
{
    let Cell = float3(int3(2 * Plume.VelocityOrigin.xy, 0) + int3(Local)) + 0.5;
    return float3(Cell.xy * Plume.Lattice.x, Plume.Lattice.z + Cell.z * Plume.Lattice.x);
}

// 📝 Sampling coordinates: the repeat address mode on U and V performs the wrap; W is clamped.
float3 ProjectVelocityCoordinates(PlumeConstants Plume, float3 Position)
{
    let Span = float3(Plume.VelocityExtent.xyz) * Plume.Lattice.y;
    return float3(Position.xy / Span.xy, (Position.z - Plume.Lattice.z) / Span.z);
}

float3 ProjectThermoCoordinates(PlumeConstants Plume, float3 Position)
{
    let Span = float3(Plume.ThermoExtent.xyz) * Plume.Lattice.x;
    return float3(Position.xy / Span.xy, (Position.z - Plume.Lattice.z) / Span.z);
}

// 📝 A position lies in a window's interior when every trilinear tap it takes is a texel of that window — half a cell in from each wrapped face,
//    so a sample beside one edge can never read the opposite edge through the wrap.
bool ClassifyWindowInterior(float3 Position, int3 Origin, uint3 Extent, float Edge, float Floor)
{
    let Local = float3(Position.xy / Edge - float2(Origin.xy), (Position.z - Floor) / Edge);
    return all(Local >= float3(0.5)) && all(Local <= float3(Extent) - float3(0.5));
}

bool ClassifyPriorVelocity(PlumeConstants Plume, float3 Position)
{
    return ClassifyWindowInterior(Position, Plume.PriorVelocityOrigin.xyz, Plume.VelocityExtent.xyz, Plume.Lattice.y, Plume.Lattice.z);
}

bool ClassifyPriorThermo(PlumeConstants Plume, float3 Position)
{
    return ClassifyWindowInterior(Position, int3(2 * Plume.PriorVelocityOrigin.xy, 0), Plume.ThermoExtent.xyz, Plume.Lattice.x, Plume.Lattice.z);
}

bool ClassifySourceBounds(PlumeConstants Plume, float3 Position)
{
    return Plume.PriorVelocityOrigin.w > 0 && all(Position >= Plume.SourceMinimum.xyz) && all(Position <= Plume.SourceMaximum.xyz);
}

// 📐 Gaussian source weight exp(−1.7·d²/r²), r floored at three quarters of a cell so a source narrower than the lattice still deposits; zero
//    beyond 3.5 radii. The centre is lifted 0.55 of a cell: a contact patch lies on the supporting plane, and a Gaussian centred there would
//    spend half its weight in cells the plane makes solid.
float ResolveSourceWeight(PlumeSource Source, float3 Position, float Edge)
{
    let Radius = max(Source.PositionRadius.w, 0.75 * Edge);
    let Offset = (Position - Source.PositionRadius.xyz - float3(0.0, 0.0, 0.55 * Edge)) / Radius;
    let Squared = dot(Offset, Offset);
    return SelectBranch(exp(-1.7 * Squared), 0.0, Squared > 12.25);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FLAME COLOUR
//------------------------------------------------------------------------------------------------------------------------

// 📐 The colour of a blackbody at Temperature [K], linear Rec.709, normalised to unit maximum channel: Helland's fit of the Planckian locus
//    (sRGB-encoded) decoded to linear. Valid 1000–6600 K; a tyre fire's soot glows 1100–1500 K at the tips and approaches 1800 K in the core.
float3 ResolveBlackbodyColour(float Temperature)
{
    let Hecto = clamp(Temperature, 1000.0, 6600.0) / 100.0;
    let Green = clamp((99.4708025861 * log(Hecto) - 161.1195681661) / 255.0, 0.0, 1.0);
    let Blue = SelectBranch(clamp((138.5177312231 * log(max(Hecto - 10.0, 1.0)) - 305.0447927307) / 255.0, 0.0, 1.0), 0.0, Hecto <= 19.0);
    return pow(float3(1.0, Green, Blue), float3(2.2));
}

// 📐 Stefan–Boltzmann in relative form: radiance rises with the fourth power of temperature, referred to 1500 K.
float3 ResolveBlackbodyRadiance(float Temperature)
{
    let Ratio = Temperature / 1500.0;
    return ResolveBlackbodyColour(Temperature) * (Ratio * Ratio * Ratio * Ratio);
}
