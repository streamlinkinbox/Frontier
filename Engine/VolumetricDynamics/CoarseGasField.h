//==============================================================================================================================================
//                                                           COARSEGASFIELD.H
//==============================================================================================================================================
// 📦 The 32-cubed reproducible gas reading: fixed step, fixed sweeps, integer noise — the one gas the physics and the network may read.
//
// Every gas effect runs two solvers from the same emissions and the same preset:
//
//    | Property            | This field                                  | The displayed volume                |
//    |---------------------|---------------------------------------------|-------------------------------------|
//    | Where               | CPU, off the render thread                  | GPU                                 |
//    | Lattice             | 32 cubed, always                            | 32 to 128 cubed by quality rung     |
//    | Step                | 1/60 s, always                              | the rung's step division            |
//    | Reproducible        | yes, bit for bit                            | no, and it never needs to be        |
//    | Read by             | physics forces, sight queries, the network  | the raymarch, and nothing else      |
//
// 💡 THE SPLIT IS WHAT MAKES TWO-WAY COUPLING AFFORDABLE AND REPRODUCIBLE AT THE SAME TIME.
//    Pushing rigid bodies from the displayed volume would mean reading a 128-cubed velocity image back off the
//    device every frame — roughly 16 MB, a pipeline stall, and a result that differs between two vendors. This
//    field is already on the CPU where Jolt is, so there is no readback at all: not a smaller one, none. And
//    because its step, its sweep count and its noise are all fixed, the forces it produces are identical on
//    every machine, which is the entire multiplayer story. See GasWindContribution.h for the force itself.
//
// 📐 Reproducibility is a property of the arithmetic, not an intention:
//    ① the step is a compile-time constant, so no measured frame duration ever reaches the solver,
//    ② the sweep count is a compile-time constant, so no convergence criterion can terminate early,
//    ③ turbulence is seeded by integer hashing, never by std::sin, whose last bits differ between libraries,
//    ④ every accumulation runs in ascending index order, because float addition is not associative,
//    ⑤ nothing here is threaded; a threaded version must reduce in this same order or it is a different field.
//
// ⚠️ 32 cubed cannot resolve the eddies that are visible in a 128-cubed render, so a leaf will not swirl exactly
//    where the drawn smoke curls. For pushing debris, cloth and ragdolls that difference is invisible. The two
//    readings agree in bulk because they are driven from the same emissions, not because one samples the other.
//
// 💾 Storage is 6 live readings and 5 scratch readings of 32768 floats — about 1.4 MB per field, heap-held.

#pragma once

// 📝 For RemainingAfter(), which converts a loss declared per second into the multiplier for one step. The
//    solver is not permitted to make that conversion itself; see the file header of the allowance.
#include "GasQualityAllowance.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     FIXED DIMENSIONS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t CoarseExtent         = 32u;                       // [voxels] - cubic, and never configurable
constexpr uint32_t CoarseVoxelCount     = CoarseExtent * CoarseExtent * CoarseExtent;
constexpr float    CoarseStepInterval   = 1.0f / 60.0f;              // [s]      - the fixed step, reason ① above
constexpr uint32_t CoarsePressureSweeps = 12u;                       // [-]      - fixed count, reason ② above


// One source of gas, resolved into the field each advance. A fracture piece and a slipping tyre both reduce to
//    this, which is what lets neither of them know that a solver exists.
struct GasEmission
{
    float Position[3]      = { 0.0f, 0.0f, 0.0f };   // [m]     - world centre
    float Radius           = 0.5f;                   // [m]     - gaussian falloff radius
    float SmokeRate        = 1.0f;                   // [1/s]   - density added per second at the centre
    float TemperatureRate  = 1.0f;                   // [K/s]   - heat added per second at the centre, above ambient
    float Velocity[3]      = { 0.0f, 0.0f, 0.0f };   // [m/s]   - momentum the source imparts
};


// The tuning the field shares with the displayed volume. Losses are declared per second and converted through
//    RemainingAfter(), which is why none of them is named "per step".
struct CoarseGasSettings
{
    float BuoyancyLift        = 2.4f;    // [m/s²/K] - upward acceleration per kelvin above ambient
    float SmokeWeight         = 0.18f;   // [m/s²]   - downward pull of suspended particulate
    float SmokeLossPerSecond  = 0.22f;   // [1/s]    - fraction of density dissipated in one second
    float CoolingPerSecond    = 0.55f;   // [1/s]    - fraction of excess temperature shed in one second
    float DampingPerSecond    = 0.08f;   // [1/s]    - fraction of velocity lost to viscosity in one second
    float TurbulenceStrength  = 0.35f;   // [m/s]    - amplitude of the reproducible swirl
    float TurbulenceScale     = 3.0f;    // [1/m]    - spatial frequency of that swirl
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE FIELD
//------------------------------------------------------------------------------------------------------------------------

// Axis 2 is up, matching WindField.h, which reads altitude from Position[2]. The field is a cube in world space
//    so that one span covers all three axes and the voxel size is a single number.
struct CoarseGasField
{
    float Origin[3]  = { 0.0f, 0.0f, 0.0f };   // [m] - minimum corner
    float Span       = 8.0f;                   // [m] - edge length of the cube

    std::vector<float> VelocityX;              // [m/s]
    std::vector<float> VelocityY;              // [m/s]
    std::vector<float> VelocityZ;              // [m/s]
    std::vector<float> Smoke;                  // [-]  - normalised density
    std::vector<float> Temperature;            // [K]  - above ambient
    std::vector<float> Occupancy;              // [-]  - 1 where solid, 0 where open; see GasCollisionIntake.h

    std::vector<float> ScratchX;               // [m/s] - advection reads the previous reading while writing this
    std::vector<float> ScratchY;               // [m/s]
    std::vector<float> ScratchZ;               // [m/s]
    std::vector<float> Pressure;               // [-]
    std::vector<float> PressureNext;           // [-]
    std::vector<float> Divergence;             // [1/s]

    uint64_t AdvanceNumber = 0ull;             // [-]  - monotonic since Reset; seeds the reproducible turbulence
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     LATTICE ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

inline uint32_t VoxelIndex(uint32_t X, uint32_t Y, uint32_t Z) noexcept
{
    return (Z * CoarseExtent + Y) * CoarseExtent + X;
}

inline float VoxelSize(const CoarseGasField& Field) noexcept
{
    return Field.Span / static_cast<float>(CoarseExtent);
}

inline float ClampUnit(float Reading, float Low, float High) noexcept
{
    return Reading < Low ? Low : (Reading > High ? High : Reading);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   REPRODUCIBLE NOISE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Integer avalanche, exact on every machine that has 32-bit unsigned arithmetic — which is the point. The
//    trigonometric hash used by WindField.h is fine for a cosmetic field and unusable here, because its last
//    bits depend on the maths library and two machines would then push a crate different distances.
inline uint32_t IntegerAvalanche(uint32_t Seed) noexcept
{
    Seed ^= Seed >> 16;
    Seed *= 0x7feb352du;
    Seed ^= Seed >> 15;
    Seed *= 0x846ca68bu;
    Seed ^= Seed >> 16;
    return Seed;
}

inline float SignedHash(int32_t X, int32_t Y, int32_t Z, uint32_t Salt) noexcept
{
    uint32_t Seed = IntegerAvalanche(static_cast<uint32_t>(X) * 0x9e3779b9u
                                   ^ IntegerAvalanche(static_cast<uint32_t>(Y) * 0x85ebca6bu
                                   ^ IntegerAvalanche(static_cast<uint32_t>(Z) * 0xc2b2ae35u ^ Salt)));
    // 24 mantissa bits into [-1,1]; the division is by a power of two and is therefore exact.
    return static_cast<float>(Seed >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Sizes every reading to the fixed voxel count and clears the field to still, cold, empty air.
/// in    Field      [-]  the field to prepare; any previous contents are discarded
/// in    Origin     [m]  minimum corner of the cube
/// in    Span       [m]  edge length of the cube
/// out   -
/// err   a non-positive span is raised to one metre rather than producing an infinite voxel size
/// cost  🚩  eleven allocations of 32768 floats on first use; none afterwards
/// tag   api, allocating, nonthrowing
inline void ResetField(CoarseGasField& Field, const float Origin[3], float Span) noexcept
{
    Field.Origin[0] = Origin[0];
    Field.Origin[1] = Origin[1];
    Field.Origin[2] = Origin[2];
    Field.Span      = Span > 0.0f ? Span : 1.0f;

    std::vector<float>* const Readings[11] = {
        &Field.VelocityX, &Field.VelocityY, &Field.VelocityZ, &Field.Smoke, &Field.Temperature,
        &Field.Occupancy, &Field.ScratchX, &Field.ScratchY, &Field.ScratchZ, &Field.Pressure,
        &Field.PressureNext };
    for (std::vector<float>* Reading : Readings)
    {
        Reading->assign(CoarseVoxelCount, 0.0f);
    }
    Field.Divergence.assign(CoarseVoxelCount, 0.0f);
    Field.AdvanceNumber = 0ull;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SAMPLING
//------------------------------------------------------------------------------------------------------------------------

// Trilinear read in lattice coordinates, clamped at the walls. Written once and reused by advection and by every
//    public sampler, because two interpolators would be two fields.
inline float ReadTrilinear(const std::vector<float>& Reading, float X, float Y, float Z) noexcept
{
    const float Limit = static_cast<float>(CoarseExtent) - 1.0f;
    X = ClampUnit(X, 0.0f, Limit);
    Y = ClampUnit(Y, 0.0f, Limit);
    Z = ClampUnit(Z, 0.0f, Limit);

    const uint32_t X0 = static_cast<uint32_t>(X), Y0 = static_cast<uint32_t>(Y), Z0 = static_cast<uint32_t>(Z);
    const uint32_t X1 = X0 + 1u < CoarseExtent ? X0 + 1u : X0;
    const uint32_t Y1 = Y0 + 1u < CoarseExtent ? Y0 + 1u : Y0;
    const uint32_t Z1 = Z0 + 1u < CoarseExtent ? Z0 + 1u : Z0;
    const float Fx = X - static_cast<float>(X0), Fy = Y - static_cast<float>(Y0), Fz = Z - static_cast<float>(Z0);

    const float C000 = Reading[VoxelIndex(X0, Y0, Z0)], C100 = Reading[VoxelIndex(X1, Y0, Z0)];
    const float C010 = Reading[VoxelIndex(X0, Y1, Z0)], C110 = Reading[VoxelIndex(X1, Y1, Z0)];
    const float C001 = Reading[VoxelIndex(X0, Y0, Z1)], C101 = Reading[VoxelIndex(X1, Y0, Z1)];
    const float C011 = Reading[VoxelIndex(X0, Y1, Z1)], C111 = Reading[VoxelIndex(X1, Y1, Z1)];

    const float Low  = (C000 * (1.0f - Fx) + C100 * Fx) * (1.0f - Fy) + (C010 * (1.0f - Fx) + C110 * Fx) * Fy;
    const float High = (C001 * (1.0f - Fx) + C101 * Fx) * (1.0f - Fy) + (C011 * (1.0f - Fx) + C111 * Fx) * Fy;
    return Low * (1.0f - Fz) + High * Fz;
}


/// 📦 World position into continuous lattice coordinates. Outside the cube the result is outside [0, extent-1]
///    and the caller is expected to notice; the samplers below clamp, which is correct for them and would be
///    wrong here.
inline void LatticeCoordinate(const CoarseGasField& Field, const float Position[3], float OutCoordinate[3]) noexcept
{
    const float Size = VoxelSize(Field);
    for (int Axis = 0; Axis < 3; ++Axis)
        OutCoordinate[Axis] = (Position[Axis] - Field.Origin[Axis]) / Size - 0.5f;
}


/// 📦 True when a world position lies inside the field's cube.
/// in    Field      [-]  the field
/// in    Position   [m]  world position
/// out   bool       [-]  inclusive of the faces
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool PositionEnclosed(const CoarseGasField& Field, const float Position[3]) noexcept
{
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        if (Position[Axis] < Field.Origin[Axis]) return false;
        if (Position[Axis] > Field.Origin[Axis] + Field.Span) return false;
    }
    return true;
}


/// 📦 Gas velocity at a world position.
/// in    Field         [-]    the field
/// in    Position      [m]    world position; outside the cube the nearest face reading is returned
/// out   OutVelocity   [m/s]  three components
/// use   the drag term in GasWindContribution.h, and any sight or audio query that wants flow
/// cost  ✔️  three trilinear reads
/// tag   api, nonallocating, nonthrowing
inline void SampleGasVelocity(const CoarseGasField& Field, const float Position[3], float OutVelocity[3]) noexcept
{
    float Coordinate[3];
    LatticeCoordinate(Field, Position, Coordinate);
    OutVelocity[0] = ReadTrilinear(Field.VelocityX, Coordinate[0], Coordinate[1], Coordinate[2]);
    OutVelocity[1] = ReadTrilinear(Field.VelocityY, Coordinate[0], Coordinate[1], Coordinate[2]);
    OutVelocity[2] = ReadTrilinear(Field.VelocityZ, Coordinate[0], Coordinate[1], Coordinate[2]);
}


/// 📦 Smoke density at a world position, normalised.
/// in    Field      [-]  the field
/// in    Position   [m]  world position
/// out   float      [-]  0 is clear air; 1 is as dense as an emission makes it
/// use   the sight query, if a design ever asks whether smoke blocks a line
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline float SampleGasSmoke(const CoarseGasField& Field, const float Position[3]) noexcept
{
    float Coordinate[3];
    LatticeCoordinate(Field, Position, Coordinate);
    return ReadTrilinear(Field.Smoke, Coordinate[0], Coordinate[1], Coordinate[2]);
}


/// 📦 Total smoke held by the field — the conservation quantity the checks assert against.
/// in    Field      [-]  the field
/// out   float      [-]  summed in ascending index order, reason ④ in the file header
/// cost  🚩  one pass over 32768 voxels
/// tag   api, nonallocating, nonthrowing
inline float TotalSmoke(const CoarseGasField& Field) noexcept
{
    float Total = 0.0f;
    for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot) Total += Field.Smoke[Slot];
    return Total;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       EMISSION
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Adds one source's contribution for one step: density, heat and momentum, on a gaussian falloff.
/// in    Field      [-]  the field being driven
/// in    Source     [-]  where, how wide, how hard
/// in    Interval   [s]  Δτ of this advance
/// out   -
/// note  sources outside the cube contribute nothing rather than smearing onto the nearest wall
/// cost  🚩  touches only the voxels within three radii of the source
/// tag   api, nonallocating, nonthrowing
inline void InjectEmission(CoarseGasField& Field, const GasEmission& Source, float Interval) noexcept
{
    const float Size   = VoxelSize(Field);
    const float Radius = Source.Radius > 0.0f ? Source.Radius : Size;
    float Centre[3];
    LatticeCoordinate(Field, Source.Position, Centre);

    const float ReachVoxels = 3.0f * Radius / Size;
    const int32_t Low[3]  = { static_cast<int32_t>(std::floor(Centre[0] - ReachVoxels)),
                              static_cast<int32_t>(std::floor(Centre[1] - ReachVoxels)),
                              static_cast<int32_t>(std::floor(Centre[2] - ReachVoxels)) };
    const int32_t High[3] = { static_cast<int32_t>(std::ceil(Centre[0] + ReachVoxels)),
                              static_cast<int32_t>(std::ceil(Centre[1] + ReachVoxels)),
                              static_cast<int32_t>(std::ceil(Centre[2] + ReachVoxels)) };

    const float Spread = Radius / Size;
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
                const float Squared = (Dx * Dx + Dy * Dy + Dz * Dz) / (Spread * Spread);
                if (Squared > 9.0f) continue;
                const float Weight = std::exp(-Squared) * Interval;

                const uint32_t Slot = VoxelIndex(static_cast<uint32_t>(X), static_cast<uint32_t>(Y),
                                                 static_cast<uint32_t>(Z));
                Field.Smoke[Slot]       += Source.SmokeRate * Weight;
                Field.Temperature[Slot] += Source.TemperatureRate * Weight;
                Field.VelocityX[Slot]   += Source.Velocity[0] * Weight;
                Field.VelocityY[Slot]   += Source.Velocity[1] * Weight;
                Field.VelocityZ[Slot]   += Source.Velocity[2] * Weight;
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ADVANCE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Advances the field one fixed step: buoyancy, reproducible swirl, semi-Lagrangian transport, a fixed number
///    of Jacobi sweeps to remove divergence, then the per-second losses converted for this step.
/// in    Field      [-]  the field, advanced in place; AdvanceNumber increments by one
/// in    Settings   [-]  the shared tuning
/// out   -
/// err   solid voxels hold zero velocity throughout, so a source buried inside geometry cannot inject momentum
/// note  🔴 the step is CoarseStepInterval and is not a parameter. A caller with a variable frame duration
///       accumulates the remainder and advances a whole number of times, which is what keeps the reading
///       reproducible and is the reason this routine refuses a duration argument.
/// cost  🔴  one transport pass and CoarsePressureSweeps Jacobi passes over 32768 voxels
/// tag   api, nonallocating, nonthrowing
inline void AdvanceField(CoarseGasField& Field, const CoarseGasSettings& Settings) noexcept
{
    constexpr float Interval = CoarseStepInterval;
    const float Size  = VoxelSize(Field);
    const uint32_t Salt = static_cast<uint32_t>(Field.AdvanceNumber & 0xffffffffull);

    // ① Buoyancy and the reproducible swirl, both written straight into the live velocity.
    for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
    for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
    for (uint32_t X = 0u; X < CoarseExtent; ++X)
    {
        const uint32_t Slot = VoxelIndex(X, Y, Z);
        if (Field.Occupancy[Slot] > 0.5f) continue;

        const float Lift = Settings.BuoyancyLift * Field.Temperature[Slot]
                         - Settings.SmokeWeight  * Field.Smoke[Slot];
        Field.VelocityZ[Slot] += Lift * Interval;

        if (Settings.TurbulenceStrength > 0.0f && Field.Smoke[Slot] > 0.001f)
        {
            const int32_t Nx = static_cast<int32_t>(static_cast<float>(X) * Settings.TurbulenceScale * Size);
            const int32_t Ny = static_cast<int32_t>(static_cast<float>(Y) * Settings.TurbulenceScale * Size);
            const int32_t Nz = static_cast<int32_t>(static_cast<float>(Z) * Settings.TurbulenceScale * Size);
            const float Amount = Settings.TurbulenceStrength * Field.Smoke[Slot] * Interval;
            Field.VelocityX[Slot] += SignedHash(Nx, Ny, Nz, Salt ^ 0x1u) * Amount;
            Field.VelocityY[Slot] += SignedHash(Nx, Ny, Nz, Salt ^ 0x2u) * Amount;
            Field.VelocityZ[Slot] += SignedHash(Nx, Ny, Nz, Salt ^ 0x3u) * Amount;
        }
    }

    // ② Semi-Lagrangian transport. Scratch holds the next reading while the previous one is still being read,
    //    which is why the channels are doubled; writing in place would sample voxels already overwritten.
    const float StepInVoxels = Interval / Size;
    for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
    for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
    for (uint32_t X = 0u; X < CoarseExtent; ++X)
    {
        const uint32_t Slot = VoxelIndex(X, Y, Z);
        const float Back[3] = { static_cast<float>(X) - Field.VelocityX[Slot] * StepInVoxels,
                                static_cast<float>(Y) - Field.VelocityY[Slot] * StepInVoxels,
                                static_cast<float>(Z) - Field.VelocityZ[Slot] * StepInVoxels };
        Field.ScratchX[Slot]     = ReadTrilinear(Field.VelocityX,   Back[0], Back[1], Back[2]);
        Field.ScratchY[Slot]     = ReadTrilinear(Field.VelocityY,   Back[0], Back[1], Back[2]);
        Field.ScratchZ[Slot]     = ReadTrilinear(Field.VelocityZ,   Back[0], Back[1], Back[2]);
        Field.Pressure[Slot]     = ReadTrilinear(Field.Smoke,       Back[0], Back[1], Back[2]);
        Field.PressureNext[Slot] = ReadTrilinear(Field.Temperature, Back[0], Back[1], Back[2]);
    }
    Field.VelocityX.swap(Field.ScratchX);
    Field.VelocityY.swap(Field.ScratchY);
    Field.VelocityZ.swap(Field.ScratchZ);
    Field.Smoke.swap(Field.Pressure);
    Field.Temperature.swap(Field.PressureNext);

    // ③ Solid voxels hold still air, so transport cannot carry momentum through geometry.
    for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot)
    {
        if (Field.Occupancy[Slot] <= 0.5f) continue;
        Field.VelocityX[Slot] = 0.0f;
        Field.VelocityY[Slot] = 0.0f;
        Field.VelocityZ[Slot] = 0.0f;
    }

    // ④ Divergence, then a fixed number of Jacobi sweeps. A convergence criterion is deliberately absent:
    //    terminating early on a residual would make the sweep count depend on the contents, and two machines
    //    rounding differently would then run different numbers of sweeps and diverge for good.
    for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot)
    {
        Field.Pressure[Slot]     = 0.0f;
        Field.PressureNext[Slot] = 0.0f;
    }
    for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
    for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
    for (uint32_t X = 0u; X < CoarseExtent; ++X)
    {
        const uint32_t Xl = X > 0u ? X - 1u : X, Xh = X + 1u < CoarseExtent ? X + 1u : X;
        const uint32_t Yl = Y > 0u ? Y - 1u : Y, Yh = Y + 1u < CoarseExtent ? Y + 1u : Y;
        const uint32_t Zl = Z > 0u ? Z - 1u : Z, Zh = Z + 1u < CoarseExtent ? Z + 1u : Z;
        Field.Divergence[VoxelIndex(X, Y, Z)] = -0.5f * Size *
            ( Field.VelocityX[VoxelIndex(Xh, Y, Z)] - Field.VelocityX[VoxelIndex(Xl, Y, Z)]
            + Field.VelocityY[VoxelIndex(X, Yh, Z)] - Field.VelocityY[VoxelIndex(X, Yl, Z)]
            + Field.VelocityZ[VoxelIndex(X, Y, Zh)] - Field.VelocityZ[VoxelIndex(X, Y, Zl)] );
    }

    for (uint32_t Sweep = 0u; Sweep < CoarsePressureSweeps; ++Sweep)
    {
        for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
        for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
        for (uint32_t X = 0u; X < CoarseExtent; ++X)
        {
            const uint32_t Slot = VoxelIndex(X, Y, Z);
            const uint32_t Xl = X > 0u ? X - 1u : X, Xh = X + 1u < CoarseExtent ? X + 1u : X;
            const uint32_t Yl = Y > 0u ? Y - 1u : Y, Yh = Y + 1u < CoarseExtent ? Y + 1u : Y;
            const uint32_t Zl = Z > 0u ? Z - 1u : Z, Zh = Z + 1u < CoarseExtent ? Z + 1u : Z;
            Field.PressureNext[Slot] = ( Field.Divergence[Slot]
                + Field.Pressure[VoxelIndex(Xl, Y, Z)] + Field.Pressure[VoxelIndex(Xh, Y, Z)]
                + Field.Pressure[VoxelIndex(X, Yl, Z)] + Field.Pressure[VoxelIndex(X, Yh, Z)]
                + Field.Pressure[VoxelIndex(X, Y, Zl)] + Field.Pressure[VoxelIndex(X, Y, Zh)] ) / 6.0f;
        }
        Field.Pressure.swap(Field.PressureNext);
    }

    for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
    for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
    for (uint32_t X = 0u; X < CoarseExtent; ++X)
    {
        const uint32_t Slot = VoxelIndex(X, Y, Z);
        if (Field.Occupancy[Slot] > 0.5f) continue;
        const uint32_t Xl = X > 0u ? X - 1u : X, Xh = X + 1u < CoarseExtent ? X + 1u : X;
        const uint32_t Yl = Y > 0u ? Y - 1u : Y, Yh = Y + 1u < CoarseExtent ? Y + 1u : Y;
        const uint32_t Zl = Z > 0u ? Z - 1u : Z, Zh = Z + 1u < CoarseExtent ? Z + 1u : Z;
        const float Scale = 0.5f / Size;
        Field.VelocityX[Slot] -= Scale * (Field.Pressure[VoxelIndex(Xh, Y, Z)] - Field.Pressure[VoxelIndex(Xl, Y, Z)]);
        Field.VelocityY[Slot] -= Scale * (Field.Pressure[VoxelIndex(X, Yh, Z)] - Field.Pressure[VoxelIndex(X, Yl, Z)]);
        Field.VelocityZ[Slot] -= Scale * (Field.Pressure[VoxelIndex(X, Y, Zh)] - Field.Pressure[VoxelIndex(X, Y, Zl)]);
    }

    // ⑤ The per-second losses, converted once for this step. 🔴 Never applied as written; see the file header
    //    of GasQualityAllowance.h for why a per-step loss silently changes how every preset looks.
    const float SmokeRemaining       = RemainingAfter(Settings.SmokeLossPerSecond, Interval);
    const float TemperatureRemaining = RemainingAfter(Settings.CoolingPerSecond,   Interval);
    const float VelocityRemaining    = RemainingAfter(Settings.DampingPerSecond,   Interval);
    for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot)
    {
        const float Solid = Field.Occupancy[Slot] > 0.5f ? 0.0f : 1.0f;
        Field.Smoke[Slot]       *= SmokeRemaining * Solid;
        Field.Temperature[Slot] *= TemperatureRemaining * Solid;
        Field.VelocityX[Slot]   *= VelocityRemaining * Solid;
        Field.VelocityY[Slot]   *= VelocityRemaining * Solid;
        Field.VelocityZ[Slot]   *= VelocityRemaining * Solid;
    }

    ++Field.AdvanceNumber;
}


/// 📦 Advances a whole number of fixed steps from an accumulated duration, returning the remainder to carry.
/// in    Field        [-]  the field
/// in    Settings     [-]  shared tuning
/// in    Accumulated  [s]  time owed, including whatever the previous call returned
/// in    StepCeiling  [-]  most steps to take in one call, so a stall cannot spiral
/// out   float        [s]  time still owed; hand it back on the next call
/// use   this is the only routine a variable-rate caller should use
/// note  on hitting StepCeiling the remainder is discarded rather than carried, because a field that is already
///       behind can never catch up by trying harder
/// cost  🔴  StepCeiling advances
/// tag   api, nonallocating, nonthrowing
inline float AdvanceAccumulated(CoarseGasField& Field, const CoarseGasSettings& Settings,
                                float Accumulated, uint32_t StepCeiling) noexcept
{
    uint32_t Taken = 0u;
    while (Accumulated >= CoarseStepInterval && Taken < StepCeiling)
    {
        AdvanceField(Field, Settings);
        Accumulated -= CoarseStepInterval;
        ++Taken;
    }
    return Taken >= StepCeiling ? 0.0f : Accumulated;
}

}   // namespace Frontier
