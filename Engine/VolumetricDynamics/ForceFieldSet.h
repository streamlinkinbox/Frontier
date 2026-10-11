//============================================================================================================================================
// 📦 Engine/VolumetricDynamics/ForceFieldSet.h — every field in a scene, sorted by what it returns
//============================================================================================================================================
// Ported from Experimental/ParticleEditor/js/forcefields.js, which is the design reference and is held to
//    it by Tools/Build/ForceFieldParity.py. The browser page was written first, as the gas inspector was.
//
//--------------------------------------------------------------------------------------------------------------------------------------------
// 🔴 THE DISTINCTION THAT MATTERS IS NOT WHAT A FIELD IS CALLED. IT IS WHAT A FIELD RETURNS.
//--------------------------------------------------------------------------------------------------------------------------------------------
// "Wind, and later gravity, and later attraction" is one entity seen three times, but it is not one
//    QUANTITY. Three contributions exist and every field is exactly one of them:
//
//        Flow         [m/s]   relaxed toward, at the receiver's own coupling:   V += (F - V) * k*dt
//        Accelerate   [m/s²]  added, and the coupling has no say:               V += F * dt
//        Damp         [1/s]   scales velocity down:                             V *= exp(-F*dt)
//
// ⚠️ WHY GRAVITY CANNOT BE A WIND. If gravity were a Flow, a receiver with coupling 0 would IGNORE it,
//    and one with a high coupling would reach its speed within a frame and stop accelerating. Both are
//    wrong, and both read as a tuning problem rather than as a category error. Conversely a Flow has a
//    terminal speed by construction — ten seconds in an 8 m/s wind leaves you at 8 m/s — while the same
//    number as an acceleration is past 70 m/s and climbing. One list could not have produced both.
//
// 💡 AND IT PAYS FOR ITSELF. Flow fields sum into one velocity lattice per frame and are then one sample
//    however many there are. Acceleration fields cannot be summed that way: the useful ones (gravity, a
//    planet's pull) are unbounded, and a bounded lattice would clip them. Bakeable() below is the only
//    place that judgement is made, so the two halves of the engine cannot disagree about it.
//
// 📝 WHAT THIS DOES NOT DO. It does not replace WindField.h. That header is the ATMOSPHERIC MODEL — Ekman
//    shear and veer, the gust envelope, curl turbulence, and a carefully split cheap/expensive sampling
//    interface that exists because calling noise inside a raymarch step cost 1500 evaluations a pixel.
//    It is tested and it is correct. It becomes the solver BEHIND the flow kinds; this sits above it.
//
// Header-only and dependency-free for the same reason WindField.h is: the identical arithmetic has to run
//    in the CPU raster, in the headless proofs, and be transcribed into shader source, and none of those
//    three can share a binary. One definition is what stops the copies drifting.

#pragma once

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT A FIELD RETURNS
//------------------------------------------------------------------------------------------------------------------------

enum class ForceContribution : uint8_t
{
    Flow = 0,         // [m/s]
    Accelerate = 1,   // [m/s²]
    Damp = 2,         // [1/s]
};

enum class ForceReach : uint8_t
{
    Everywhere = 0,   // 🔴 NOT a very large sphere. No centre, no edge; the case a lattice cannot hold.
    Sphere = 1,
    Box = 2,
    Cone = 3,
};

enum class ForceFalloff : uint8_t
{
    None = 0,
    Linear = 1,
    Smooth = 2,          // (1 - q²)², the shape the wind kinds already used
    InverseSquare = 3,
};

// The kinds, in the order the browser lists them. The first eight are not new: four are the components
//    the wind lattice already sums and four are the force types the particle shader already evaluates.
//    They sorted cleanly into Flow and Accelerate, which is the evidence that this taxonomy describes
//    what exists rather than wishing for something else.
enum class ForceFieldKind : uint8_t
{
    Prevailing = 0,
    Gust = 1,
    Tornado = 2,
    Outflow = 3,
    Attract = 4,
    Repel = 5,
    Lift = 6,
    Magnetic = 7,
    Gravity = 8,
    Orbit = 9,
    Drag = 10,
    Current = 11,
    Count = 12,
};

struct ForceFieldKindFacts
{
    const char*       Id;
    const char*       Name;
    ForceContribution Give;
    ForceReach        Reaches;
    const char*       Unit;
    int8_t            Lattice;   // slot in the flow lattice, or -1
    int8_t            Force;     // slot in the acceleration payload, or -1
    bool              Inverts;   // packs with its sign flipped — see Gravity
};

//---------------------------------------------------------------------------------------------------------
// 📦 What one kind is. Keyed by the enum, so a missing entry is a compile-time hole rather than a lookup
//    that quietly returns nothing.
//---------------------------------------------------------------------------------------------------------
inline const ForceFieldKindFacts& FactsOf(ForceFieldKind Kind) noexcept
{
    static const ForceFieldKindFacts Known[static_cast<int>(ForceFieldKind::Count)] =
    {
        { "prevailing", "Prevailing wind", ForceContribution::Flow,       ForceReach::Everywhere, "m/s",   0, -1, false },
        { "gust",       "Gust front",      ForceContribution::Flow,       ForceReach::Sphere,     "m/s",   1, -1, false },
        { "tornado",    "Tornado",         ForceContribution::Flow,       ForceReach::Sphere,     "m/s",   2, -1, false },
        { "outflow",    "Blast outflow",   ForceContribution::Flow,       ForceReach::Sphere,     "m/s",   3, -1, false },
        { "attract",    "Attractor",       ForceContribution::Accelerate, ForceReach::Sphere,     "m/s2", -1,  0, false },
        { "repel",      "Repulsor",        ForceContribution::Accelerate, ForceReach::Sphere,     "m/s2", -1,  1, false },
        { "lift",       "Lift",            ForceContribution::Accelerate, ForceReach::Sphere,     "m/s2", -1,  2, false },
        { "magnetic",   "Magnetic dipole", ForceContribution::Accelerate, ForceReach::Sphere,     "m/s2", -1,  3, false },
        // Gravity reaches the shader as a downward Lift, the one force type that is already a fixed world
        //    axis. That is a mapping and not a merge: it is authored as gravity and stays Accelerate.
        { "gravity",    "Gravity",         ForceContribution::Accelerate, ForceReach::Everywhere, "m/s2", -1,  2, true  },
        { "orbit",      "Orbit",           ForceContribution::Accelerate, ForceReach::Sphere,     "m/s2", -1,  0, false },
        // 📝 No shader path yet. Declared anyway, because the taxonomy is the design and a kind that
        //    cannot be packed should be caught by a proof rather than by a user watching nothing happen.
        { "drag",       "Drag volume",     ForceContribution::Damp,       ForceReach::Box,        "1/s",  -1, -1, false },
        { "current",    "Current",         ForceContribution::Flow,       ForceReach::Box,        "m/s",  -1, -1, false },
    };
    const int At = static_cast<int>(Kind);
    return Known[(At >= 0 && At < static_cast<int>(ForceFieldKind::Count)) ? At : 0];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        A FIELD
//------------------------------------------------------------------------------------------------------------------------

struct ForceField
{
    ForceFieldKind Kind      = ForceFieldKind::Prevailing;
    char           Name[48]  = "Prevailing wind";
    bool           Enabled   = true;
    float          Centre[3] = { 0.0f, 0.0f, 0.0f };
    float          Direction[3] = { 0.0f, -1.0f, 0.0f };
    float          Strength  = 5.0f;
    float          Radius    = 8.0f;
    ForceReach     Reaches   = ForceReach::Sphere;
    ForceFalloff   Fades     = ForceFalloff::Smooth;
    float          Swirl     = 0.0f;
    float          Rate      = 0.0f;    // [Hz] gust pulse rate; the lattice's `freq`
    float          Swallow   = 0.0f;    // [m] a receiver this close to an attractor is removed

    // 🔴 WHO RESPONDS IS CONFIGURATION, NOT CODE. The rule the gas colliders already follow. A field
    //    names the channels it acts on; zero means everything. Hardcoding "gravity affects debris" is
    //    how an engine comes to need a recompile to make a balloon float.
    uint32_t       Acts      = 0u;      // bit per channel; 0 = all

    float          Begins    = 0.0f;
    float          Lasts     = 0.0f;    // 0 = forever
    float          Repeats   = 0.0f;    // 0 = once
};

//---------------------------------------------------------------------------------------------------------
// 📦 How much of a field reaches a point: 0 outside, 1 at the centre. Never negative — a falloff that
//    goes below zero pushes where it should pull, and does it only at the edges where nobody looks.
//---------------------------------------------------------------------------------------------------------
inline float ForceFalls(ForceFalloff Shape, float Distance, float Radius) noexcept
{
    if (Shape == ForceFalloff::None) return 1.0f;
    const float Span = (Radius > 1e-3f) ? Radius : 1e-3f;
    float q = Distance / Span;
    if (q < 0.0f) q = 0.0f;
    if (q > 1.0f) q = 1.0f;
    if (Shape == ForceFalloff::Linear)        return 1.0f - q;
    if (Shape == ForceFalloff::InverseSquare) return 1.0f / (1.0f + 8.0f * q * q);
    const float Soft = 1.0f - q * q;
    return Soft * Soft;
}

inline bool ForceAwake(const ForceField& Field, float Now) noexcept
{
    if (!Field.Enabled) return false;
    if (Field.Lasts <= 0.0f) return Now >= Field.Begins;
    const float Since = Now - Field.Begins;
    if (Since < 0.0f) return false;
    if (Field.Repeats > 0.0f)
    {
        const float Into = Since - Field.Repeats * std::floor(Since / Field.Repeats);
        return Into < Field.Lasts;
    }
    return Since < Field.Lasts;
}

inline bool ForceActsOn(const ForceField& Field, uint32_t Channels) noexcept
{
    if (Field.Acts == 0u) return true;
    return (Field.Acts & Channels) != 0u;
}

// 📦 Is the point inside the field's extent at all, however weak it is there? Decided by reach, never by
//    the sampled magnitude — see the note on Flowing in ForceCrossing.
inline bool ForceReaching(const ForceField& Field, const float At[3]) noexcept
{
    if (Field.Reaches == ForceReach::Everywhere) return true;
    const float dx = At[0] - Field.Centre[0];
    const float dy = At[1] - Field.Centre[1];
    const float dz = At[2] - Field.Centre[2];
    return (dx * dx + dy * dy + dz * dz) < (Field.Radius * Field.Radius);
}

//---------------------------------------------------------------------------------------------------------
// 📦 What one field contributes at a point, in the units of its own contribution. The caller must read
//    the kind's Give before using it; three different quantities cannot simply be added together.
//---------------------------------------------------------------------------------------------------------
inline void SampleForceField(const ForceField& Field, const float At[3], float Now, float Out[3]) noexcept
{
    Out[0] = Out[1] = Out[2] = 0.0f;
    if (!ForceAwake(Field, Now)) return;

    const float dx = At[0] - Field.Centre[0];
    const float dy = At[1] - Field.Centre[1];
    const float dz = At[2] - Field.Centre[2];
    const float Span = std::sqrt(dx * dx + dy * dy + dz * dz);

    float Share = 1.0f;
    if (Field.Reaches != ForceReach::Everywhere)
    {
        if (Span >= Field.Radius) return;
        Share = ForceFalls(Field.Fades, Span, Field.Radius);
    }

    const float Scale = Field.Strength * Share;

    switch (Field.Kind)
    {
    case ForceFieldKind::Gravity: Out[1] = -Scale; return;
    case ForceFieldKind::Lift:    Out[1] =  Scale; return;
    case ForceFieldKind::Drag:    Out[0] = Out[1] = Out[2] = Scale; return;
    default: break;
    }

    if (Field.Kind == ForceFieldKind::Attract || Field.Kind == ForceFieldKind::Repel
        || Field.Kind == ForceFieldKind::Orbit)
    {
        const float Reach = (Span > 1e-3f) ? Span : 1e-3f;
        const float Ox = dx / Reach, Oy = dy / Reach, Oz = dz / Reach;
        if (Field.Kind == ForceFieldKind::Repel)
        {
            Out[0] = Ox * Scale; Out[1] = Oy * Scale; Out[2] = Oz * Scale;
            return;
        }
        float Inward[3] = { -Ox * Scale, -Oy * Scale, -Oz * Scale };
        if (Field.Kind == ForceFieldKind::Attract && Field.Swirl == 0.0f)
        {
            Out[0] = Inward[0]; Out[1] = Inward[1]; Out[2] = Inward[2];
            return;
        }
        // Tangential about world up, which is how a drain behaves and how the black hole already behaves.
        float Tx = -Oz, Tz = Ox;
        float Across = std::sqrt(Tx * Tx + Tz * Tz);
        if (Across < 1e-6f) Across = 1.0f;
        const float Turn = (Field.Kind == ForceFieldKind::Orbit) ? Scale : Field.Swirl * Share;
        const float Rx = Tx / Across * Turn, Rz = Tz / Across * Turn;
        if (Field.Kind == ForceFieldKind::Orbit)
        {
            Out[0] = Rx; Out[1] = 0.0f; Out[2] = Rz;
            return;
        }
        Out[0] = Inward[0] + Rx; Out[1] = Inward[1]; Out[2] = Inward[2] + Rz;
        return;
    }

    // The flow kinds and the magnetic drive point along their own axis.
    float Length = std::sqrt(Field.Direction[0] * Field.Direction[0]
                           + Field.Direction[1] * Field.Direction[1]
                           + Field.Direction[2] * Field.Direction[2]);
    if (Length < 1e-6f) Length = 1.0f;
    Out[0] = Field.Direction[0] / Length * Scale;
    Out[1] = Field.Direction[1] / Length * Scale;
    Out[2] = Field.Direction[2] / Length * Scale;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 EVERYTHING ACTING AT A POINT
//------------------------------------------------------------------------------------------------------------------------

struct ForceCrossing
{
    float Flow[3]       = { 0.0f, 0.0f, 0.0f };
    // 🔴 NOT BOOKKEEPING. A flow of zero and NO FLOW AT ALL must be told apart, because relaxing toward a
    //    zero flow is a brake: a receiver standing where no wind reaches would be slowed to a stop by the
    //    mere absence of wind, and nothing would show why. Still air that drags is a Damp field somebody
    //    placed on purpose, never a side effect of an empty list. This was found by the browser checks,
    //    not by reading the code, which is the argument for writing the reference first.
    bool  Flowing       = false;
    float Accelerate[3] = { 0.0f, 0.0f, 0.0f };
    float Damp          = 0.0f;
};

inline ForceCrossing ResolveForces(const ForceField* Fields, uint32_t Count, const float At[3],
                                   float Now, uint32_t Channels) noexcept
{
    ForceCrossing Acting;
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        const ForceField& Field = Fields[Index];
        if (!ForceActsOn(Field, Channels)) continue;
        if (!ForceAwake(Field, Now)) continue;

        const ForceFieldKindFacts& Facts = FactsOf(Field.Kind);
        float Given[3];

        if (Facts.Give == ForceContribution::Damp)
        {
            SampleForceField(Field, At, Now, Given);
            Acting.Damp += Given[0];
            continue;
        }
        if (Facts.Give == ForceContribution::Flow)
        {
            if (!ForceReaching(Field, At)) continue;
            SampleForceField(Field, At, Now, Given);
            Acting.Flow[0] += Given[0]; Acting.Flow[1] += Given[1]; Acting.Flow[2] += Given[2];
            Acting.Flowing = true;
            continue;
        }
        SampleForceField(Field, At, Now, Given);
        Acting.Accelerate[0] += Given[0];
        Acting.Accelerate[1] += Given[1];
        Acting.Accelerate[2] += Given[2];
    }
    return Acting;
}

//---------------------------------------------------------------------------------------------------------
// 📦 Advance one receiver by one step. The only function that knows how the three contributions differ in
//    APPLICATION, and deliberately tiny: it is the paragraph at the top of this file, executable.
//
//    ⚠️ THE ORDER IS OBSERVABLE AND IS THEREFORE FIXED: relax toward the flow, then add acceleration,
//       then damp. Any other order gives different numbers for the same scene, and a replication peer
//       that chose differently would drift.
//
//    in     Coupling   [1/s] how fast this receiver is dragged to the flow. 0 ignores wind entirely.
//---------------------------------------------------------------------------------------------------------
inline void AdvanceByForces(const ForceCrossing& Acting, float Coupling, float Step, float Velocity[3]) noexcept
{
    if (Acting.Flowing)
    {
        float Pull = Coupling * Step;
        if (Pull < 0.0f) Pull = 0.0f;
        if (Pull > 1.0f) Pull = 1.0f;
        Velocity[0] += (Acting.Flow[0] - Velocity[0]) * Pull;
        Velocity[1] += (Acting.Flow[1] - Velocity[1]) * Pull;
        Velocity[2] += (Acting.Flow[2] - Velocity[2]) * Pull;
    }
    // Added after the relaxation and NOT scaled by the coupling. This one line is the whole reason Flow
    //    and Accelerate are separate contributions.
    Velocity[0] += Acting.Accelerate[0] * Step;
    Velocity[1] += Acting.Accelerate[1] * Step;
    Velocity[2] += Acting.Accelerate[2] * Step;

    if (Acting.Damp > 0.0f)
    {
        const float Keep = std::exp(-Acting.Damp * Step);
        Velocity[0] *= Keep; Velocity[1] *= Keep; Velocity[2] *= Keep;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              WHAT CAN BE SUMMED INTO A LATTICE
//------------------------------------------------------------------------------------------------------------------------

// 📦 Can this field be summed into the shared flow lattice, or must it be evaluated per receiver?
//    💡 A flow field that names channels cannot be baked either: one shared texture cannot both include
//       and exclude the same receiver. That is the real cost of selectivity, and it is worth knowing
//       before somebody adds an Acts list to the prevailing wind and wonders why the frame time moved.
inline bool ForceBakeable(const ForceField& Field) noexcept
{
    const ForceFieldKindFacts& Facts = FactsOf(Field.Kind);
    if (Facts.Give != ForceContribution::Flow) return false;
    if (Facts.Lattice < 0) return false;
    return Field.Acts == 0u;
}

// 📦 Does this kind reach the shaders at all yet?
inline bool ForcePackable(ForceFieldKind Kind) noexcept
{
    const ForceFieldKindFacts& Facts = FactsOf(Kind);
    if (Facts.Give == ForceContribution::Flow)       return Facts.Lattice >= 0;
    if (Facts.Give == ForceContribution::Accelerate) return Facts.Force >= 0;
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   REACHING THE SHADERS
//------------------------------------------------------------------------------------------------------------------------

// One entry of the flow lattice's component array — the eight floats the lattice builder reads.
struct ForceLatticeEntry
{
    float X = 0.0f, Z = 0.0f, Radius = 0.0f, Kind = 0.0f;
    float Strength = 0.0f, Bearing = 0.0f, Rate = 0.0f, Enabled = 0.0f;
};

// One entry of the acceleration payload — the three vec4s the particle shader walks.
struct ForceAccelerationEntry
{
    float Centre[3] = { 0.0f, 0.0f, 0.0f };
    float Radius = 0.0f;         // ⚠️ 0 means EVERYWHERE to the shader, not "a sphere of nothing"
    float Kind = 0.0f, Strength = 0.0f, Swirl = 0.0f, Swallow = 0.0f;
    float Begins = 0.0f, Lasts = 0.0f, Repeats = 0.0f, Spare = 0.0f;
};

// 📦 The compass bearing a directional field points along, in degrees, always on [0, 360).
inline float ForceBearing(const ForceField& Field) noexcept
{
    constexpr float kPi = 3.14159265358979323846f;
    const float Degrees = std::atan2(Field.Direction[0], Field.Direction[2]) * 180.0f / kPi;
    return (Degrees < 0.0f) ? Degrees + 360.0f : Degrees;
}

inline void ForceAlong(float Degrees, float OutDirection[3]) noexcept
{
    constexpr float kPi = 3.14159265358979323846f;
    const float Turn = Degrees * kPi / 180.0f;
    OutDirection[0] = std::sin(Turn);
    OutDirection[1] = 0.0f;
    OutDirection[2] = std::cos(Turn);
}

inline bool PackForceLattice(const ForceField& Field, ForceLatticeEntry& Out) noexcept
{
    const ForceFieldKindFacts& Facts = FactsOf(Field.Kind);
    if (Facts.Give != ForceContribution::Flow || Facts.Lattice < 0) return false;
    Out.X        = Field.Centre[0];
    Out.Z        = Field.Centre[2];
    Out.Radius   = Field.Radius;
    Out.Kind     = static_cast<float>(Facts.Lattice);
    Out.Strength = Field.Strength;
    Out.Bearing  = ForceBearing(Field);
    Out.Rate     = Field.Rate;
    Out.Enabled  = Field.Enabled ? 1.0f : 0.0f;
    return true;
}

inline bool PackForceAcceleration(const ForceField& Field, ForceAccelerationEntry& Out) noexcept
{
    const ForceFieldKindFacts& Facts = FactsOf(Field.Kind);
    if (Facts.Give != ForceContribution::Accelerate || Facts.Force < 0) return false;
    const bool Everywhere = (Field.Reaches == ForceReach::Everywhere);
    Out.Centre[0] = Field.Centre[0];
    Out.Centre[1] = Field.Centre[1];
    Out.Centre[2] = Field.Centre[2];
    Out.Radius    = Everywhere ? 0.0f : Field.Radius;
    Out.Kind      = static_cast<float>(Facts.Force);
    Out.Strength  = Facts.Inverts ? -Field.Strength : Field.Strength;
    Out.Swirl     = Field.Swirl;
    Out.Swallow   = Field.Swallow;
    Out.Begins    = Field.Begins;
    Out.Lasts     = (Field.Lasts > 0.0f) ? Field.Lasts : 1e9f;
    Out.Repeats   = Field.Repeats;
    Out.Spare     = 0.0f;
    return true;
}

} // namespace Frontier
