//==============================================================================================================================================
//                                                        NATIVEGASFIELDCHECKS.CPP
//==============================================================================================================================================
// 📦 Executed proof for the reproducible gas reading: conservation, reproducibility, the per-second conversion, obstruction, and the coupling switch.
//
// Dependency-free and windowless, as every workbench proof is. Five claims are worth more than the rest, and
//    each exists because getting it wrong produces a defect that does not look like its cause:
//
//    ① THE SAME ADVANCE RUN TWICE IS BIT-IDENTICAL. Without it there is no deterministic multiplayer, and the
//       failure shows up as two clients disagreeing about where a crate ended up, hours later and in a
//       different subsystem.
//    ② SMOKE ADVANCED AT 60, 30 AND 15 Hz AGREES AFTER ONE SECOND. Without it every distant plume lingers
//       longer than the near one authored identically, and it reads as an art problem.
//    ③ A SEALED FIELD LOSES SMOKE ONLY TO DISSIPATION. Transport that leaks is a solver that quietly empties.
//    ④ OBSTRUCTION HOLDS. Gas inside geometry is a hole in the world the player can see through.
//    ⑤ A BODY THAT HAS NOT OPTED IN FEELS EXACTLY ZERO. The switch is the deliverable, not a precaution.

#include "CoarseGasField.h"
#include "GasCollisionIntake.h"
#include "GasPresetLibrary.h"
#include "GasQualityAllowance.h"
#include "GasWindContribution.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace Frontier;

namespace
{

unsigned Checks = 0;

void Check(bool Condition, const char* Claim)
{
    ++Checks;
    std::printf("  %s  %s\n", Condition ? "PASS" : "FAIL", Claim);
    if (!Condition)
    {
        throw std::runtime_error(Claim);
    }
}

void Banner(const char* Title)
{
    std::printf("\n%s\n", Title);
}

CoarseGasField SealedField(float Span = 8.0f)
{
    CoarseGasField Field;
    const float Origin[3] = { 0.0f, 0.0f, 0.0f };
    ResetField(Field, Origin, Span);
    return Field;
}

GasEmission CentralPlume(const CoarseGasField& Field, float SmokeRate, float TemperatureRate)
{
    GasEmission Source;
    Source.Position[0]     = Field.Origin[0] + Field.Span * 0.5f;
    Source.Position[1]     = Field.Origin[1] + Field.Span * 0.5f;
    Source.Position[2]     = Field.Origin[2] + Field.Span * 0.25f;
    Source.Radius          = Field.Span * 0.08f;
    Source.SmokeRate       = SmokeRate;
    Source.TemperatureRate = TemperatureRate;
    return Source;
}

// Advances a field for one simulated second with no sources, at the given rate, by advancing the fixed step
//    the matching number of times. The field's own step never changes — the caller's rate does.
void AdvanceOneSecond(CoarseGasField& Field, const CoarseGasSettings& Settings, uint32_t StepsPerSecond)
{
    for (uint32_t Step = 0u; Step < StepsPerSecond; ++Step) AdvanceField(Field, Settings);
}

float ScalarDeviation(const std::vector<float>& Left, const std::vector<float>& Right)
{
    float Largest = 0.0f;
    for (std::size_t Slot = 0u; Slot < Left.size(); ++Slot)
    {
        const float Apart = std::fabs(Left[Slot] - Right[Slot]);
        if (Apart > Largest) Largest = Apart;
    }
    return Largest;
}

// A distance reading standing in for GlobalDistanceFieldSpace: an infinite floor at a bound height. The proof
//    cannot link the raster, and the seam exists precisely so that it does not have to.
float FloorDistance(const void* Context, const float Position[3])
{
    return Position[2] - *static_cast<const float*>(Context);
}

}   // namespace


int main()
{
    std::printf("Gas field checks - the reproducible reading, its budget, its obstruction and its coupling\n");

    try
    {

    //----------------------------------------------------------------------------------------------------------------
    //                                     THE TRANSCRIBED SETTINGS AND PRESETS
    //----------------------------------------------------------------------------------------------------------------

    // 📝 These mirror the three rules Experimental/Fluid/src/SceneMetrics.mjs already enforces in the browser.
    //    They are restated here rather than trusted across the transcription, because a generator that drops a
    //    preset's differences would still produce a header that compiles and presets that resolve -- and the
    //    only visible symptom would be a native plume that no longer looks like the browser one.

    Banner("The transcribed settings and presets");

    Check(GasSettingCount == 85u, "all 85 settings crossed from presets.js");
    Check(GasPresetCount  == 18u, "and all 18 presets");

    {
        const GasPreset* Library = GasPresetLibrary();
        uint32_t Named = 0u, Described = 0u, Detonating = 0u;
        for (uint32_t Slot = 0u; Slot < GasPresetCount; ++Slot)
        {
            if (Library[Slot].Identity[0] != '\0' && Library[Slot].Name[0] != '\0') ++Named;
            if (Library[Slot].Description[0] != '\0') ++Described;
            if (Library[Slot].DetonateOnLoad) ++Detonating;
        }
        Check(Named == GasPresetCount, "every preset carries an identity and a name");
        Check(Described == GasPresetCount, "and a description for the rail beneath it");
        Check(Detonating > 0u, "and at least one opens mid-explosion");

        // Identities must be unique, or a scene storing one of them cannot be reopened unambiguously.
        uint32_t Collisions = 0u;
        for (uint32_t Left = 0u; Left < GasPresetCount; ++Left)
            for (uint32_t Right = Left + 1u; Right < GasPresetCount; ++Right)
                if (std::strcmp(Library[Left].Identity, Library[Right].Identity) == 0) ++Collisions;
        Check(Collisions == 0u, "and no two presets share an identity");

        // Resolving by slot and by identity must agree, since the editor uses one and a scene the other.
        const GasSettings BySlot     = ConstructPresetSettings(3u);
        const GasSettings ByIdentity = ConstructPresetSettings(Library[3].Identity);
        Check(BySlot.LatticeResolution == ByIdentity.LatticeResolution
           && BySlot.BoundsWidth == ByIdentity.BoundsWidth,
              "resolving by slot and by identity give the same settings");

        const GasSettings Unknown = ConstructPresetSettings("a_preset_that_was_renamed");
        Check(Unknown.LatticeResolution == GasSettings{}.LatticeResolution,
              "an unknown identity yields the defaults rather than refusing to open");
        Check(ConstructPresetSettings(static_cast<const char*>(nullptr)).PressureIterations
              == GasSettings{}.PressureIterations, "and a null identity is not dereferenced");
        Check(ConstructPresetSettings(999u).PressureIterations == GasSettings{}.PressureIterations,
              "as does a slot past the end of the library");

        // 🔴 The continuous settings must be declared as floats even where every preset currently lands on
        //    a whole number. timeScale, shockwaveStrength, sunAzimuth and sunElevation are all whole in all
        //    eighteen presets today; typed from those readings they would become integers, and the first
        //    artist to drag one to 0.55 would get a silent truncation to zero and an effect that does not
        //    move. A counted setting stays an integer because a lattice cannot be 64.5 voxels wide.
        static_assert(std::is_same_v<decltype(GasSettings::TimeScale),          float>,
                      "a continuous control must not be declared as an integer");
        static_assert(std::is_same_v<decltype(GasSettings::ShockwaveStrength),  float>,
                      "a continuous control must not be declared as an integer");
        static_assert(std::is_same_v<decltype(GasSettings::SunAzimuth),         float>,
                      "a continuous control must not be declared as an integer");
        static_assert(std::is_same_v<decltype(GasSettings::LatticeResolution),  int32_t>,
                      "a counted setting must not be declared as a float");
        static_assert(std::is_same_v<decltype(GasSettings::RaymarchSteps),      int32_t>,
                      "a counted setting must not be declared as a float");
        static_assert(std::is_same_v<decltype(GasSettings::MacCormackAdvection), bool>,
                      "a switch must stay a switch");

        GasSettings Dragged;
        Dragged.TimeScale = 0.55f;
        Check(Dragged.TimeScale > 0.5f && Dragged.TimeScale < 0.6f,
              "a continuous control holds a fraction rather than truncating it to zero");

        // Every preset resolves to a lattice the solver could actually allocate.
        for (uint32_t Slot = 0u; Slot < GasPresetCount; ++Slot)
        {
            const GasSettings Settings = ConstructPresetSettings(Slot);
            Check(Settings.LatticeResolution >= 16 && Settings.LatticeResolution <= 256,
                  "every preset asks for a lattice between 16 and 256 voxels");
            Check(Settings.PressureIterations > 0 && Settings.PressureIterations <= 64,
                  "and a sweep count the budget can carry");
            Check(Settings.BoundsWidth > 0.0f && Settings.BoundsHeight > 0.0f,
                  "and a domain with a positive extent");
        }
    }

    {
        // Rule 1 - a detonation keeps its voxel density. Enlarging the bounds without shrinking the blast
        //    radius is the defect this guards, and it coarsens the explosion exactly when it matters most.
        const char* const Detonations[5] = { "ue5_pyro_default", "shrapnel_airburst", "tactical_ordnance",
                                             "megaton_open_bounds", "brick_fracture_dust" };
        for (const char* Identity : Detonations)
        {
            const GasSettings Settings = ConstructPresetSettings(Identity);
            const float Density = static_cast<float>(Settings.LatticeResolution) / Settings.BoundsWidth;
            Check(Density >= 28.0f, "a detonation resolves at least 28 voxels per world unit");
            Check(Settings.BlastRadius <= 0.15f, "and fills no more than 0.15 of its domain");
            if (Settings.DynamicBounds)
                Check(Settings.DynamicBoundsMax <= 1.25f, "and does not surge enough to coarsen mid-blast");
        }
    }

    {
        // Rule 2 - a cold effect must not light itself. Every one of these was tuned from a fire preset,
        //    which is exactly why the mistake is easy and the check is cheap.
        const char* const Cold[5] = { "dust_tornado", "ledge_sandfall", "settling_dust",
                                      "small_gust", "brick_fracture_dust" };
        for (const char* Identity : Cold)
        {
            const GasSettings Settings = ConstructPresetSettings(Identity);
            Check(Settings.EmitterFuel == 0.0f, "a cold effect emits no fuel");
            Check(Settings.BlastFuel   == 0.0f, "and bursts none either");
            Check(Settings.BurnRate    <= 0.2f, "and does not burn");
            Check(Settings.SmokeAlbedo >= 0.5f, "and scatters like dust rather than like soot");
        }
    }

    {
        // Rule 3 - the signs of the physics, which no amount of tuning should be able to invert.
        for (const char* Identity : { "ledge_sandfall", "settling_dust" })
        {
            const GasSettings Settings = ConstructPresetSettings(Identity);
            Check(Settings.SmokeWeight > Settings.Buoyancy, "sand and settling dust fall");
        }
        for (const char* Identity : { "lantern_flame", "camp_fire" })
        {
            const GasSettings Settings = ConstructPresetSettings(Identity);
            Check(Settings.Buoyancy > Settings.SmokeWeight, "while flame rises");
            Check(Settings.EmitterFuel > 0.0f, "and has something to burn");
        }
    }

    {
        // The tyre ring the solver obstructs with is the one the preset selects, so the two agree about
        //    which obstruction a burnout is happening against.
        const GasSettings Burnout = ConstructPresetSettings("tyre_burnout");
        Check(Burnout.ObstacleType == static_cast<int32_t>(GasColliderShape::TyreRing),
              "the tyre burnout preset selects the tyre ring, matching GasColliderShape");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                              THE QUALITY LADDER
    //----------------------------------------------------------------------------------------------------------------

    Banner("The quality ladder");

    Check(AllowanceFor(GasQuality::Hero).LatticeExtent == 128u, "Hero simulates at 128 cubed");
    Check(AllowanceFor(GasQuality::Far).LatticeExtent  ==  32u, "Far simulates at 32 cubed");
    Check(AllowanceFor(GasQuality::Flipbook).LatticeExtent == 0u, "Flipbook does not simulate at all");

    // 98.57 MB, which is the figure the plan quotes. A channel layout change must move both together.
    Check(DeviceBytesFor(GasQuality::Hero) == 128ull * 128ull * 128ull * 47ull,
          "Hero occupies 47 bytes a voxel, about 98.6 MB");
    Check(DeviceBytesFor(GasQuality::Flipbook) == 0ull, "a flipbook is shared and charged to no domain");

    Check(SolveCostFor(GasQuality::Hero) > SolveCostFor(GasQuality::Near) * 2ull,
          "the pressure solve scales with the cube, so Hero costs well over twice Near");

    Check(StepIntervalFor(GasQuality::Hero) < StepIntervalFor(GasQuality::Mid),
          "Mid advances less often than Hero");
    Check(std::fabs(StepIntervalFor(GasQuality::Mid) - 1.0f / 30.0f) < 1.0e-6f,
          "Mid advances at 30 Hz, half of the 60 Hz master");
    Check(std::fabs(StepIntervalFor(GasQuality::Far) - 1.0f / 15.0f) < 1.0e-6f,
          "Far advances at 15 Hz, a quarter of the master");
    Check(StepIntervalFor(GasQuality::Flipbook) == 0.0f, "a flipbook has no interval and cannot divide by zero");

    //----------------------------------------------------------------------------------------------------------------
    //                                       CLAIM ② - THE PER-SECOND CONVERSION
    //----------------------------------------------------------------------------------------------------------------

    Banner("Claim 2 - a loss declared per second removes the same amount per second at every rate");

    {
        // Half of everything gone in one second, at three different rates.
        const float Halved = 0.5f;
        float Surviving60 = 1.0f, Surviving30 = 1.0f, Surviving15 = 1.0f;
        for (uint32_t Step = 0u; Step < 60u; ++Step) Surviving60 *= RemainingAfter(Halved, 1.0f / 60.0f);
        for (uint32_t Step = 0u; Step < 30u; ++Step) Surviving30 *= RemainingAfter(Halved, 1.0f / 30.0f);
        for (uint32_t Step = 0u; Step < 15u; ++Step) Surviving15 *= RemainingAfter(Halved, 1.0f / 15.0f);

        Check(std::fabs(Surviving60 - 0.5f) < 1.0e-4f, "60 advances of a per-second half leave exactly a half");
        Check(std::fabs(Surviving30 - 0.5f) < 1.0e-4f, "30 advances leave the same half");
        Check(std::fabs(Surviving15 - 0.5f) < 1.0e-4f, "15 advances leave the same half");

        // 📝 And the bug this prevents, stated numerically so the reason is not merely asserted: applying the
        //    per-second figure once per advance leaves 2^-15 at 15 Hz against 2^-60 at 60 Hz. Four orders of
        //    magnitude apart from one authored preset.
        float Naive15 = 1.0f, Naive60 = 1.0f;
        for (uint32_t Step = 0u; Step < 15u; ++Step) Naive15 *= (1.0f - Halved);
        for (uint32_t Step = 0u; Step < 60u; ++Step) Naive60 *= (1.0f - Halved);
        Check(Naive15 > Naive60 * 1000.0f,
              "applying a per-second loss per step instead would leave distant smoke orders of magnitude denser");

        Check(RemainingAfter(0.0f, 1.0f / 60.0f) == 1.0f, "a zero loss removes nothing");
        Check(RemainingAfter(1.0f, 1.0f / 60.0f) == 0.0f, "a total loss removes everything");
        Check(RemainingAfter(0.5f, 0.0f) == 1.0f, "an unadvanced domain never decays");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                                 THE GOVERNOR
    //----------------------------------------------------------------------------------------------------------------

    Banner("The governor");

    {
        GasBudgetCeiling Ceiling;
        GasDomainClaim Claims[4];
        Claims[0].ViewerDistance =   3.0f;
        Claims[1].ViewerDistance =  12.0f;
        Claims[2].ViewerDistance =  40.0f;
        Claims[3].ViewerDistance = 400.0f;

        Check(AssignQuality(Claims, 4u, Ceiling), "four well-spread domains fit inside the ceilings");
        Check(Claims[0].Assigned == GasQuality::Hero,     "the domain at 3 m is Hero");
        Check(Claims[1].Assigned == GasQuality::Near,     "the domain at 12 m is Near");
        Check(Claims[2].Assigned == GasQuality::Mid,      "the domain at 40 m is Mid");
        Check(Claims[3].Assigned == GasQuality::Flipbook, "the domain at 400 m plays a sheet");
        Check(std::strcmp(AssignmentReason(Claims[0]), "distance") == 0,
              "an undemoted domain reports distance as its reason");
    }

    {
        // Two domains both want Hero; the allowance permits one. The nearer keeps it.
        GasBudgetCeiling Ceiling;
        GasDomainClaim Claims[2];
        Claims[0].ViewerDistance = 6.0f;
        Claims[1].ViewerDistance = 2.0f;

        Check(AssignQuality(Claims, 2u, Ceiling), "two Hero claims resolve");
        Check(Claims[1].Assigned == GasQuality::Hero, "the nearer domain keeps Hero");
        Check(Claims[0].Assigned != GasQuality::Hero, "the further domain gives it up");
        Check(Claims[0].DemotionApplied, "and records that a ceiling, not distance, decided");
        Check(std::strcmp(AssignmentReason(Claims[0]), "distance") != 0,
              "a demoted domain does not claim distance as its reason");
    }

    {
        // A tight device ceiling must still terminate, and must not leave anything above it.
        GasBudgetCeiling Ceiling;
        Ceiling.DeviceBytes = 20ull * 1024ull * 1024ull;
        GasDomainClaim Claims[6];
        for (uint32_t Slot = 0u; Slot < 6u; ++Slot) Claims[Slot].ViewerDistance = 4.0f + static_cast<float>(Slot);

        Check(AssignQuality(Claims, 6u, Ceiling), "six near domains resolve against a 20 MB ceiling");
        uint64_t Total = 0ull;
        for (uint32_t Slot = 0u; Slot < 6u; ++Slot) Total += DeviceBytesFor(Claims[Slot].Assigned);
        Check(Total <= Ceiling.DeviceBytes, "and the assignment respects it");
        Check(Claims[0].Assigned != GasQuality::Flipbook, "while the nearest domain still simulates");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                      CLAIM ① - THE READING IS REPRODUCIBLE
    //----------------------------------------------------------------------------------------------------------------

    Banner("Claim 1 - the same advance run twice is bit-identical");

    {
        const CoarseGasSettings Settings;
        CoarseGasField First = SealedField(), Second = SealedField();
        const GasEmission Source = CentralPlume(First, 4.0f, 6.0f);

        for (uint32_t Step = 0u; Step < 40u; ++Step)
        {
            InjectEmission(First,  Source, CoarseStepInterval);
            InjectEmission(Second, Source, CoarseStepInterval);
            AdvanceField(First,  Settings);
            AdvanceField(Second, Settings);
        }

        Check(std::memcmp(First.Smoke.data(), Second.Smoke.data(), CoarseVoxelCount * sizeof(float)) == 0,
              "40 advances produce byte-identical smoke");
        Check(std::memcmp(First.VelocityX.data(), Second.VelocityX.data(), CoarseVoxelCount * sizeof(float)) == 0,
              "and byte-identical velocity");
        Check(First.AdvanceNumber == Second.AdvanceNumber, "and the same advance number");
        Check(First.AdvanceNumber == 40ull, "which counts every advance taken");

        // Reproducibility must not be an accident of the field being empty.
        Check(TotalSmoke(First) > 1.0f, "the fields compared were genuinely carrying smoke");

        // The swirl is seeded by integer avalanche, so it is exact rather than merely close.
        Check(SignedHash(3, 7, 11, 5u) == SignedHash(3, 7, 11, 5u), "the noise is a pure function of its seed");
        Check(SignedHash(3, 7, 11, 5u) != SignedHash(3, 7, 11, 6u), "and a different advance swirls differently");
        Check(std::fabs(SignedHash(3, 7, 11, 5u)) <= 1.0f, "and stays inside its declared range");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                           CLAIM ② AGAIN - AT THE SOLVER, NOT MERELY AT THE CONVERSION
    //----------------------------------------------------------------------------------------------------------------

    Banner("Claim 2 at the solver - one second of dissipation is one second at any rate");

    {
        CoarseGasSettings Settings;
        Settings.BuoyancyLift       = 0.0f;   // isolate the loss from transport
        Settings.TurbulenceStrength = 0.0f;
        Settings.SmokeLossPerSecond = 0.5f;

        CoarseGasField Field = SealedField();
        const GasEmission Source = CentralPlume(Field, 10.0f, 0.0f);
        InjectEmission(Field, Source, 1.0f);
        const float Started = TotalSmoke(Field);

        AdvanceOneSecond(Field, Settings, 60u);
        const float Ended = TotalSmoke(Field);

        Check(Started > 0.0f, "the field started with smoke in it");
        Check(Ended < Started, "and lost some of it");
        Check(std::fabs(Ended / Started - 0.5f) < 0.02f,
              "one second of a per-second half leaves half the smoke, to within two percent");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                   CLAIM ③ - A SEALED FIELD DOES NOT LEAK
    //----------------------------------------------------------------------------------------------------------------

    Banner("Claim 3 - transport conserves what dissipation does not remove");

    {
        CoarseGasSettings Settings;
        Settings.SmokeLossPerSecond = 0.0f;
        Settings.CoolingPerSecond   = 0.0f;
        Settings.DampingPerSecond   = 0.0f;

        CoarseGasField Field = SealedField();
        const GasEmission Source = CentralPlume(Field, 10.0f, 2.0f);
        InjectEmission(Field, Source, 1.0f);
        const float Started = TotalSmoke(Field);

        for (uint32_t Step = 0u; Step < 30u; ++Step) AdvanceField(Field, Settings);
        const float Ended = TotalSmoke(Field);

        // Semi-Lagrangian transport is not exactly conservative — trilinear resampling smooths a little mass
        //    away every advance. A few percent over thirty advances is the known behaviour; a leak is not.
        Check(Ended < Started * 1.01f, "no smoke is created from nothing");
        Check(Ended > Started * 0.90f, "and no more than a tenth is lost to resampling over 30 advances");
    }

    {
        // Hot gas rises. Axis 2 is up, matching WindField.
        CoarseGasSettings Settings;
        Settings.TurbulenceStrength = 0.0f;

        CoarseGasField Field = SealedField();
        // 📝 90 advances is a second and a half. The emission starts a quarter of the way up an 8 m cube, so
        //    the front has 2 m to climb; at 45 advances it has reached layer 9 of 32 and the upper half is
        //    still empty, which is buoyancy working rather than failing. The check waits for the physics.
        GasEmission Source = CentralPlume(Field, 6.0f, 12.0f);
        for (uint32_t Step = 0u; Step < 90u; ++Step)
        {
            InjectEmission(Field, Source, CoarseStepInterval);
            AdvanceField(Field, Settings);
        }

        float Lower = 0.0f, Upper = 0.0f;
        for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
        for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
        for (uint32_t X = 0u; X < CoarseExtent; ++X)
        {
            const float Reading = Field.Smoke[VoxelIndex(X, Y, Z)];
            if (Z < CoarseExtent / 2u) Lower += Reading; else Upper += Reading;
        }
        Check(Upper > 0.0f, "heat carried smoke into the upper half of the field");
        Check(Upper > Lower * 0.15f, "and a real share of it, not a rounding crumb");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                    CLAIM ④ - OBSTRUCTION, ALL THREE ADMISSIONS
    //----------------------------------------------------------------------------------------------------------------

    Banner("Claim 4 - the world obstructs the gas");

    {
        CoarseGasField Field = SealedField();
        GasCollider Sphere;
        Sphere.Shape = GasColliderShape::Sphere;
        Sphere.Centre[0] = Sphere.Centre[1] = Sphere.Centre[2] = 4.0f;
        Sphere.Dimensions[0] = 1.5f;

        ClearOccupancy(Field);
        AdmitPrimitives(Field, &Sphere, 1u);
        const uint32_t Marked = OccupiedVoxels(Field);
        Check(Marked > 0u, "a sphere marks voxels solid");

        // 4/3 pi r^3 at a 0.25 m voxel: about 14.1 m^3 of voxel, which is 904 voxels. Generous bounds, because
        //    the claim is that the count is physical rather than that it is exact.
        Check(Marked > 600u && Marked < 1300u, "and marks about as many as the sphere's volume implies");

        const float Inside[3]  = { 4.0f, 4.0f, 4.0f };
        const float Outside[3] = { 1.0f, 1.0f, 1.0f };
        Check(ColliderDistance(Sphere, Inside)  < 0.0f, "the centre reads as inside");
        Check(ColliderDistance(Sphere, Outside) > 0.0f, "a far corner reads as outside");

        ClearOccupancy(Field);
        Check(OccupiedVoxels(Field) == 0u, "and occupancy is rewritten, never accumulated");
    }

    {
        // Every shape must be inside at its own centre and outside well away from it, including the ring,
        //    whose centre is the one hole in that rule and is therefore stated separately.
        GasCollider Shapes[5];
        const GasColliderShape Admitted[5] = { GasColliderShape::Sphere, GasColliderShape::Capsule,
                                               GasColliderShape::Box, GasColliderShape::Cylinder,
                                               GasColliderShape::TyreRing };
        for (uint32_t Entry = 0u; Entry < 5u; ++Entry)
        {
            Shapes[Entry].Shape = Admitted[Entry];
            Shapes[Entry].Centre[0] = Shapes[Entry].Centre[1] = Shapes[Entry].Centre[2] = 4.0f;
            Shapes[Entry].Dimensions[0] = 1.0f;
            Shapes[Entry].Dimensions[1] = 0.3f;
            Shapes[Entry].Dimensions[2] = 1.0f;
        }
        const float Away[3] = { 40.0f, 40.0f, 40.0f };
        for (uint32_t Entry = 0u; Entry < 5u; ++Entry)
            Check(ColliderDistance(Shapes[Entry], Away) > 0.0f, "every shape reads as outside from far away");

        const float Centre[3]  = { 4.0f, 4.0f, 4.0f };
        const float OnRing[3]  = { 5.0f, 4.0f, 4.0f };
        Check(ColliderDistance(Shapes[4], Centre) > 0.0f, "the tyre ring is hollow at its axis, as a tyre is");
        Check(ColliderDistance(Shapes[4], OnRing) < 0.0f, "and solid on the ring itself");

        GasCollider Inert;
        const float Anywhere[3] = { 0.0f, 0.0f, 0.0f };
        Check(ColliderDistance(Inert, Anywhere) > 1000.0f, "a None collider obstructs nothing");
    }

    {
        // Admission 2 through the distance reading seam, standing in for the global distance field.
        CoarseGasField Field = SealedField();
        const float FloorHeight = 2.0f;
        ClearOccupancy(Field);
        const uint32_t Marked = AdmitDistanceReading(Field, &FloorDistance, &FloorHeight, 0.0f);

        Check(Marked > 0u, "a distance reading marks the geometry it describes");
        // 📐 Eight of thirty-two layers lie below 2 m in an 8 m cube, and the ninth is marked too: a zero
        //    thickness falls back to half a voxel, 0.125 m, so centres up to 2.125 m are taken as solid. That
        //    growth is the documented defence against a surface slipping between two voxel centres, and the
        //    check asserts the grown count rather than the naive one so the defence cannot be removed quietly.
        Check(Marked == 9u * CoarseExtent * CoarseExtent, "and marks exactly the layers beneath the grown surface");

        CoarseGasField Sharp = SealedField();
        ClearOccupancy(Sharp);
        Check(AdmitDistanceReading(Sharp, &FloorDistance, &FloorHeight, -1.0f) == Marked,
              "a negative thickness falls back to the same half-voxel growth rather than inverting the surface");
        Check(AdmitDistanceReading(Field, nullptr, nullptr, 0.0f) == 0u,
              "an absent distance reading admits nothing and is not an error");
    }

    {
        // The claim that matters: gas does not end up inside geometry, and does not pass through it.
        CoarseGasSettings Settings;
        Settings.SmokeLossPerSecond = 0.0f;
        Settings.TurbulenceStrength = 0.0f;

        CoarseGasField Field = SealedField();
        GasCollider Slab;
        Slab.Shape = GasColliderShape::Box;
        Slab.Centre[0] = 4.0f; Slab.Centre[1] = 4.0f; Slab.Centre[2] = 5.0f;
        Slab.Dimensions[0] = 3.0f; Slab.Dimensions[1] = 3.0f; Slab.Dimensions[2] = 0.6f;

        GasEmission Source = CentralPlume(Field, 8.0f, 20.0f);
        for (uint32_t Step = 0u; Step < 60u; ++Step)
        {
            ClearOccupancy(Field);
            AdmitPrimitives(Field, &Slab, 1u);
            InjectEmission(Field, Source, CoarseStepInterval);
            AdvanceField(Field, Settings);
        }

        float InsideSolid = 0.0f;
        for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot)
            if (Field.Occupancy[Slot] > 0.5f) InsideSolid += Field.Smoke[Slot];
        Check(InsideSolid == 0.0f, "after 60 advances against a slab, no smoke sits inside the geometry");

        float Beneath = 0.0f;
        for (uint32_t Z = 0u; Z < 16u; ++Z)
        for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
        for (uint32_t X = 0u; X < CoarseExtent; ++X)
            Beneath += Field.Smoke[VoxelIndex(X, Y, Z)];
        Check(Beneath > 0.0f, "and the plume is held beneath the slab rather than deleted by it");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                            CLAIM ⑤ - TWO-WAY COUPLING IS OFF UNTIL IT IS SWITCHED ON
    //----------------------------------------------------------------------------------------------------------------

    Banner("Claim 5 - the coupling switch");

    {
        CoarseGasField Field = SealedField();
        const GasEmission Source = CentralPlume(Field, 10.0f, 25.0f);
        CoarseGasSettings Settings;
        for (uint32_t Step = 0u; Step < 30u; ++Step)
        {
            InjectEmission(Field, Source, CoarseStepInterval);
            AdvanceField(Field, Settings);
        }

        const float At[3] = { 4.0f, 4.0f, 3.0f };
        float Flow[3];
        SampleGasVelocity(Field, At, Flow);
        const float FlowSpeed = std::sqrt(Flow[0] * Flow[0] + Flow[1] * Flow[1] + Flow[2] * Flow[2]);
        Check(FlowSpeed > 0.01f, "there is a real flow at the sample point to be pushed by");

        const float Still[3] = { 0.0f, 0.0f, 0.0f };
        float Force[3];

        GasCouplingConsent Resting;
        Check(!Resting.CouplingEnabled, "coupling is off in a default-constructed consent");
        Check(!CouplingAdmitted(Resting), "and a resting body is not admitted");
        ResolveDragForce(Resting, Flow, Still, 1.0f / 60.0f, Force);
        Check(Force[0] == 0.0f && Force[1] == 0.0f && Force[2] == 0.0f,
              "a body that has not opted in feels exactly zero, not merely a small force");
        Check(ResolveBuoyantLift(Resting, Field, At) == 0.0f, "and is not lifted either");

        GasCouplingConsent Consenting;
        Consenting.CouplingEnabled = true;
        Check(CouplingAdmitted(Consenting), "an opted-in body is admitted");
        ResolveDragForce(Consenting, Flow, Still, 1.0f / 60.0f, Force);
        const float Pushed = std::sqrt(Force[0] * Force[0] + Force[1] * Force[1] + Force[2] * Force[2]);
        Check(Pushed > 0.0f, "and feels a force");

        // Dissipative: the force opposes the body's motion through the gas, so it is aligned with the relative
        //    velocity. A force with a component against it would be adding energy, which is the instability.
        const float Alignment = Force[0] * Flow[0] + Force[1] * Flow[1] + Force[2] * Flow[2];
        Check(Alignment > 0.0f, "the force lies along the relative flow, so it can only dissipate");

        // Strength zero is the authored way to disable without changing the switch.
        GasCouplingConsent Silenced = Consenting;
        Silenced.Strength = 0.0f;
        ResolveDragForce(Silenced, Flow, Still, 1.0f / 60.0f, Force);
        Check(Force[0] == 0.0f && Force[1] == 0.0f && Force[2] == 0.0f, "a zero strength is equivalent to not consenting");

        // A body already moving with the gas feels nothing, which is what "relative" means.
        ResolveDragForce(Consenting, Flow, Flow, 1.0f / 60.0f, Force);
        const float Carried = std::sqrt(Force[0] * Force[0] + Force[1] * Force[1] + Force[2] * Force[2]);
        Check(Carried < 1.0e-5f, "a body already travelling with the flow is not pushed further");
    }

    {
        // The stability clamp. A feather in a gale must be carried, never flung past the gas.
        GasCouplingConsent Feather;
        Feather.CouplingEnabled = true;
        Feather.Mass            = 0.002f;
        Feather.ReferenceArea   = 4.0f;
        Feather.DragCoefficient = 1.28f;

        const float Gale[3]  = { 80.0f, 0.0f, 0.0f };
        const float Still[3] = { 0.0f, 0.0f, 0.0f };
        const float Interval = 1.0f / 60.0f;
        float Force[3];
        ResolveDragForce(Feather, Gale, Still, Interval, Force);

        const float Impulse  = std::fabs(Force[0]) * Interval;
        const float Matching = Feather.Mass * 80.0f;
        Check(Impulse <= Matching * 1.0001f,
              "the impulse never exceeds the one that would bring the body to the flow's own velocity");
        Check(Impulse > Matching * 0.5f, "while still carrying a feather along with a gale");

        // The same clamp at a long interval, which is where an unclamped drag diverges fastest.
        ResolveDragForce(Feather, Gale, Still, 1.0f, Force);
        Check(std::fabs(Force[0]) * 1.0f <= Matching * 1.0001f, "and holds at a one-second interval too");

        GasCouplingConsent Weightless = Feather;
        Weightless.Mass = 0.0f;
        ResolveDragForce(Weightless, Gale, Still, Interval, Force);
        Check(Force[0] == 0.0f, "a massless body is refused rather than divided by");
    }

    {
        // The combined flow: weather and gas answered at one sample point.
        CoarseGasField Field = SealedField();
        const GasEmission Source = CentralPlume(Field, 10.0f, 25.0f);
        CoarseGasSettings Settings;
        for (uint32_t Step = 0u; Step < 30u; ++Step)
        {
            InjectEmission(Field, Source, CoarseStepInterval);
            AdvanceField(Field, Settings);
        }

        WindSettings Calm;
        Calm.Speed = 0.0f;
        Calm.Gust = 0.0f;
        Calm.Turbulence = 0.0f;

        const CoarseGasField* Fields[1] = { &Field };
        const float Inside[3]  = { 4.0f, 4.0f, 3.0f };
        const float Distant[3] = { 400.0f, 400.0f, 3.0f };

        float Combined[3];
        SampleCombinedFlow(Calm, Fields, 1u, Inside, 0.0f, Combined);
        const float Within = std::sqrt(Combined[0] * Combined[0] + Combined[1] * Combined[1] + Combined[2] * Combined[2]);
        Check(Within > 0.01f, "inside the field, the gas contributes to the flow a body samples");

        SampleCombinedFlow(Calm, Fields, 1u, Distant, 0.0f, Combined);
        const float Without = std::sqrt(Combined[0] * Combined[0] + Combined[1] * Combined[1] + Combined[2] * Combined[2]);
        Check(Without < 1.0e-5f, "far outside it, in calm weather, the flow is nothing");

        WindSettings Breezy;
        Breezy.Speed = 10.0f;
        Breezy.Turbulence = 0.0f;
        Breezy.Gust = 0.0f;
        SampleCombinedFlow(Breezy, Fields, 1u, Distant, 0.0f, Combined);
        const float Weatherly = std::sqrt(Combined[0] * Combined[0] + Combined[1] * Combined[1] + Combined[2] * Combined[2]);
        Check(Weatherly > 1.0f, "while the weather's own wind still reaches a body outside every gas field");

        const CoarseGasField* Absent[1] = { nullptr };
        SampleCombinedFlow(Calm, Absent, 1u, Inside, 0.0f, Combined);
        Check(Combined[0] == 0.0f && Combined[1] == 0.0f && Combined[2] == 0.0f,
              "a null field is skipped rather than dereferenced");

        Check(PositionEnclosed(Field, Inside), "the cube encloses a point within it");
        Check(!PositionEnclosed(Field, Distant), "and does not enclose a distant one");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                           THE ACCUMULATING CALLER
    //----------------------------------------------------------------------------------------------------------------

    Banner("The accumulating caller - a variable frame duration never reaches the solver");

    {
        const CoarseGasSettings Settings;
        CoarseGasField Field = SealedField();

        // Three ragged frame durations summing to a quarter second: 15 whole advances, with a remainder.
        float Owed = 0.0f;
        Owed = AdvanceAccumulated(Field, Settings, Owed + 0.0131f, 8u);
        Owed = AdvanceAccumulated(Field, Settings, Owed + 0.2104f, 16u);
        Owed = AdvanceAccumulated(Field, Settings, Owed + 0.0265f, 8u);

        Check(Field.AdvanceNumber == 15ull, "a quarter second of ragged frames advances exactly 15 fixed steps");
        Check(Owed >= 0.0f && Owed < CoarseStepInterval, "and the remainder carried is less than one step");

        CoarseGasField Ceilinged = SealedField();
        const float Remainder = AdvanceAccumulated(Ceilinged, Settings, 10.0f, 4u);
        Check(Ceilinged.AdvanceNumber == 4ull, "a stall is capped rather than spiralling");
        Check(Remainder == 0.0f, "and the unpayable remainder is discarded, not carried");
    }

    {
        // Two fields advanced the same ragged way as one advanced evenly: the point of a fixed step.
        const CoarseGasSettings Settings;
        CoarseGasField Even = SealedField(), Ragged = SealedField();
        const GasEmission Source = CentralPlume(Even, 5.0f, 8.0f);

        for (uint32_t Step = 0u; Step < 20u; ++Step)
        {
            InjectEmission(Even, Source, CoarseStepInterval);
            AdvanceField(Even, Settings);
        }

        float Owed = 0.0f;
        uint32_t Advanced = 0u;
        // Five ragged frame durations summing to 0.3400 s, which is 20.4 fixed steps and therefore 20 advances.
        const float Durations[5] = { 0.0210f, 0.0092f, 0.0505f, 0.1180f, 0.1413f };
        for (float Duration : Durations)
        {
            Owed += Duration;
            while (Owed >= CoarseStepInterval && Advanced < 20u)
            {
                InjectEmission(Ragged, Source, CoarseStepInterval);
                AdvanceField(Ragged, Settings);
                Owed -= CoarseStepInterval;
                ++Advanced;
            }
        }

        Check(Advanced == 20u, "the ragged caller took the same 20 advances");
        Check(ScalarDeviation(Even.Smoke, Ragged.Smoke) == 0.0f,
              "and reached a byte-identical field, which is why the step is not a parameter");
    }

    }
    catch (const std::exception& Failure)
    {
        std::printf("\nFAILED after %u checks: %s\n", Checks, Failure.what());
        return 1;
    }

    std::printf("\nPASS %u\n", Checks);
    return 0;
}
