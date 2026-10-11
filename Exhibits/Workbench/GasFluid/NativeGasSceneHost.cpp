//==============================================================================================================================================
//                                                        NATIVEGASSCENEHOST.CPP
//==============================================================================================================================================
// 📦 Executed proof for the seam between an authored scene and a running one — the conversions, and then every committed sample actually run.
//
// The step-4 codec proof established that a scene file crosses byte for byte. This establishes the thing a
//    byte comparison cannot: that the readings mean the same on both sides once they are in the solver's
//    units. A file can cross perfectly into a simulation that behaves nothing like the one it was authored in,
//    and until something runs it, nothing says so.
//
// 🔴 THREE DEFECTS WERE FOUND BY RUNNING IT, AND NONE OF THEM WOULD HAVE BEEN FOUND ANY OTHER WAY.
//
//    ① THE LOSSES WERE THE WRONG KIND OF NUMBER. An authored loss is the coefficient 𝑘 in exp(−𝑘·Δτ); the
//       solver wants the fraction shed in a second. camp_fire authors 1.35, RemainingAfter clamps anything at
//       or above 1 to zero, and every scene came out with no heat in it at all. The file still crossed byte
//       for byte, the field was still reproducible, and the plume still rose — on smoke weight alone.
//
//    ② THE EMITTER RADIUS WAS THE WRONG SHAPE'S RADIUS. The authored falloff is a smoothstep ending at 1.35
//       radii; the injector's is a gaussian ending at three sigma. Carrying the number across made the source
//       four times wider — sixty times the volume — and the first render came back a flat orange wall.
//
//    ③ A CLOSED CUBE IS A DIFFERENT EFFECT. CoarseGasField conserves smoke, which is right for determinism
//       and wrong for a camp fire; the authoring tool vents its sides and roof unless the scene is enclosed.
//
//    The checks below are written so that each of the three, if reintroduced, fails here rather than being
//    noticed in a screenshot six weeks later.
//
// ⚠️ THE OBSTACLE NUMBERING TRAP HAS ITS OWN SECTION. OBSTACLE_TYPES and GasColliderShape agree on 0, 1 and 5
//    and disagree on everything between, so a cast compiles and turns a deflector slab into a cylinder.

#include "CoarseGasField.h"
#include "GasCollisionIntake.h"
#include "GasPresetLibrary.h"
#include "GasSceneCodec.h"
#include "GasSceneResolve.h"
#include "VolumeRaymarch.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

unsigned Checks = 0;

void Check(bool Condition, const char* Claim)
{
    ++Checks;
    std::printf("  %s  %s\n", Condition ? "PASS" : "FAIL", Claim);
    if (!Condition) throw std::runtime_error(Claim);
}

void Banner(const char* Title)
{
    std::printf("\n%s\n", Title);
}

bool Near(float Reading, float Expected, float Tolerance)
{
    return std::fabs(Reading - Expected) <= Tolerance;
}

std::string ReadWhole(const std::string& Path)
{
    std::FILE* Handle = std::fopen(Path.c_str(), "rb");
    if (Handle == nullptr) throw std::runtime_error("cannot open " + Path);
    std::string Text;
    char Chunk[4096];
    std::size_t Taken = 0u;
    while ((Taken = std::fread(Chunk, 1u, sizeof(Chunk), Handle)) > 0u) Text.append(Chunk, Taken);
    std::fclose(Handle);
    return Text;
}

// The committed corpus, named rather than globbed: a proof that discovers its own inputs cannot notice one
//    going missing. The count is asserted on the browser side as well, for the same reason.
const char* const SampleNames[8] = { "camp_fire_steady", "cold_dust_fall", "deflector_obstacle",
                                     "far_cheap_plume", "fracture_dust_burst", "hero_detonation",
                                     "open_bounds_megaton", "tyre_burnout_ring" };

GasScene OpenSample(const char* Name)
{
    const std::string Path = std::string("Experimental/Fluid/Samples/") + Name + ".gasscene.toml";
    GasScene Scene;
    const GasSceneReading Reading = ReadGasScene(ReadWhole(Path), Scene);
    if (!Reading.Accepted) throw std::runtime_error(Path + ": " + Reading.Refusal);
    return Scene;
}

}   // namespace


int main()
{
    std::printf("NativeGasSceneHost — the authored scene, actually run\n");
    try
    {

    //----------------------------------------------------------------------------------------------------------------
    //                                     ① THE LOSSES, WHICH WERE SILENTLY FATAL
    //----------------------------------------------------------------------------------------------------------------

    Banner("Losses: an authored coefficient is not a fraction");

    {
        const GasSettings Fire = ConstructPresetSettings("camp_fire");
        const CoarseGasSettings Tuning = ResolveSolverTuning(Fire);

        Check(Fire.CoolingRate >= 1.0f,
              "the camp fire authors a cooling coefficient at or above one, which is the case that was fatal");
        Check(Tuning.CoolingPerSecond > 0.0f && Tuning.CoolingPerSecond < 1.0f,
              "and it resolves to a fraction strictly inside (0, 1)");
        Check(RemainingAfter(Tuning.CoolingPerSecond, CoarseStepInterval) > 0.5f,
              "so a single step keeps most of the heat rather than annihilating all of it");

        // The conversion is 1 − exp(−𝑘), and the cooling one carries the linear part of the radiative law.
        Check(Near(Tuning.SmokeLossPerSecond, 1.0f - std::exp(-Fire.SmokeDissipation), 1e-6f),
              "the smoke loss is one minus exp of the authored coefficient, not the coefficient");
        Check(Near(Tuning.CoolingPerSecond, 1.0f - std::exp(-Fire.CoolingRate * 0.55f), 1e-6f),
              "and cooling carries the linear 0.55 term of the authored radiative law");

        // Every preset, because one preset passing says nothing about the other seventeen.
        unsigned Annihilating = 0u;
        for (uint32_t Entry = 0u; Entry < GasPresetCount; ++Entry)
        {
            const CoarseGasSettings Each = ResolveSolverTuning(ConstructPresetSettings(Entry));
            if (RemainingAfter(Each.CoolingPerSecond,   CoarseStepInterval) <= 0.0f) ++Annihilating;
            if (RemainingAfter(Each.SmokeLossPerSecond, CoarseStepInterval) <= 0.0f) ++Annihilating;
            if (RemainingAfter(Each.DampingPerSecond,   CoarseStepInterval) <= 0.0f) ++Annihilating;
        }
        Check(Annihilating == 0u, "and no preset among all eighteen can erase a reading in one step");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                   ② THE SOURCE, WHICH WAS FOUR TIMES TOO WIDE
    //----------------------------------------------------------------------------------------------------------------

    Banner("The source: two falloffs, matched where they end");

    {
        const GasSettings Fire = ConstructPresetSettings("camp_fire");
        const GasDomainMeasure Measure = ResolveDomain(Fire);
        const GasEmission Source = ResolveEmitter(Fire, Measure);

        Check(Source.Radius > 0.0f, "the source has a radius");
        Check(3.0f * Source.Radius < 0.75f * Measure.Span,
              "and the gaussian's three-sigma reach is a fraction of the cube, not most of it");
        // The emitter stands on the floor at the horizontal centre of the cube.
        Check(Near(Source.Position[0], Measure.Origin[0] + 0.5f * Measure.Span, 1e-4f),
              "the source sits at the horizontal centre of the cube");
        Check(Source.Position[2] > Measure.Origin[2] && Source.Position[2] < Measure.Origin[2] + 0.5f * Measure.Span,
              "and low in it, which is where a fire is lit");

        // A disabled emitter is an absence of rates, not an absence of a source: a caller may inject it blind.
        GasSettings Silent = Fire;
        Silent.EmitterEnabled = false;
        const GasEmission Nothing = ResolveEmitter(Silent, Measure);
        Check(Nothing.SmokeRate == 0.0f && Nothing.TemperatureRate == 0.0f,
              "an emitter the scene switched off resolves to zero rates rather than to a special case");

        // Cold dust rises at nothing at all, and a residence time divides by that.
        GasSettings Still = Fire;
        Still.EmitterUpwardVelocity = 0.0f;
        const GasEmission Settling = ResolveEmitter(Still, Measure);
        Check(std::isfinite(Settling.SmokeRate) && Settling.SmokeRate > 0.0f,
              "a source with no upward velocity still resolves, through the settling floor");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                        ③ THE BOUNDARY, WHICH WAS SEALED
    //----------------------------------------------------------------------------------------------------------------

    Banner("The boundary: a domain that is not enclosed vents");

    {
        CoarseGasField Field;
        const float Origin[3] = { 0.0f, 0.0f, 0.0f };
        ResetField(Field, Origin, 4.0f);
        for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot) Field.Smoke[Slot] = 1.0f;

        VentOpenBoundary(Field);

        // Not to zero in one step: at 32 cubed the outermost voxel centre already sits a third of the way
        //    through the band, so the wall keeps about a third of what it had and loses the rest every
        //    advance. Over a plume's lifetime that is an outflow; in one step it is a strong attenuation,
        //    and asserting the former here would be asserting something the arithmetic does not claim.
        Check(Field.Smoke[VoxelIndex(0u, 0u, 0u)] < 0.5f, "the corner of an open domain vents most of its gas");
        Check(Field.Smoke[VoxelIndex(CoarseExtent - 1u, 16u, 16u)] < 0.5f, "so does the side wall");
        Check(Field.Smoke[VoxelIndex(16u, 16u, CoarseExtent - 1u)] < 0.5f, "and the roof");
        Check(Field.Smoke[VoxelIndex(16u, 16u, 0u)] == 1.0f,
              "the floor does not, because gas pools on the ground in the authoring tool too");
        Check(Field.Smoke[VoxelIndex(16u, 16u, 16u)] == 1.0f, "and the middle of the domain is untouched");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                       THE OBSTACLE NUMBERING, WRITTEN OUT
    //----------------------------------------------------------------------------------------------------------------

    Banner("The obstruction: a table, because the two numberings only look alike");

    {
        GasSettings Settings = ConstructPresetSettings("camp_fire");
        Settings.ObstacleRadius = 0.2f;
        const GasDomainMeasure Measure = ResolveDomain(Settings);

        Settings.ObstacleType = 0;
        Check(!ResolveCollider(Settings, Measure).Present, "obstacle type 0 is an absence and stays one");

        Settings.ObstacleType = 1;
        Check(ResolveCollider(Settings, Measure).Collider.Shape == GasColliderShape::Sphere,
              "1 is a sphere at both ends, which is why the cast looks safe");

        Settings.ObstacleType = 2;
        const GasColliderReading Pillar = ResolveCollider(Settings, Measure);
        Check(Pillar.Collider.Shape == GasColliderShape::Cylinder,
              "2 is a vertical cylinder, and the enum's 2 is a capsule — the table says cylinder");
        Check(Pillar.Collider.Dimensions[2] > 0.0f && Pillar.Collider.Centre[2] > Measure.Origin[2],
              "and it stands on the floor rather than floating with a half-height of nothing");

        Settings.ObstacleType = 3;
        const GasColliderReading Lying = ResolveCollider(Settings, Measure);
        Check(Lying.Collider.Shape == GasColliderShape::Box, "3 lies along X, so it is admitted as its bounding box");
        Check(Lying.ShapeApproximated, "and says so, because a diverted plume is not a visible defect in itself");
        Check(Lying.Collider.Dimensions[0] > Lying.Collider.Dimensions[1],
              "with the long half-extent along the axis it actually lies on");

        Settings.ObstacleType = 4;
        const GasColliderReading Slab = ResolveCollider(Settings, Measure);
        Check(Slab.Collider.Shape == GasColliderShape::Box,
              "🔴 4 is a deflector slab and the enum's 4 is a cylinder — the cast would have been silent here");
        Check(Slab.Collider.Dimensions[2] < Slab.Collider.Dimensions[0], "and a slab is thin in the up axis");

        Settings.ObstacleType = 5;
        const GasColliderReading Tyre = ResolveCollider(Settings, Measure);
        Check(Tyre.Collider.Shape == GasColliderShape::TyreRing, "5 is a tyre ring at both ends");
        Check(Tyre.OrientationApproximated, "but an upright ring cannot spin about axis 2, and the reading says so");

        Settings.ObstacleType = 9;
        Check(!ResolveCollider(Settings, Measure).Present, "an obstacle type this build does not know is absent, not guessed");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                          THE SUN AND THE FRAMING
    //----------------------------------------------------------------------------------------------------------------

    Banner("The sun and the camera");

    {
        GasSettings Settings = ConstructPresetSettings("camp_fire");
        Settings.SunAzimuth = 42.0f;
        Settings.SunElevation = 90.0f;
        float Direction[3];
        ResolveSunDirection(Settings, Direction);
        Check(Near(Direction[2], 1.0f, 1e-5f), "the sun at ninety degrees of elevation is straight up axis 2");

        Settings.SunElevation = 0.0f;
        Settings.SunAzimuth = 0.0f;
        ResolveSunDirection(Settings, Direction);
        Check(Near(Direction[1], 1.0f, 1e-5f), "and on the horizon at zero azimuth it lies along axis 1");

        Settings.SunElevation = 33.0f;
        Settings.SunAzimuth = 215.0f;
        ResolveSunDirection(Settings, Direction);
        const float Length = std::sqrt(Direction[0] * Direction[0] + Direction[1] * Direction[1]
                                     + Direction[2] * Direction[2]);
        Check(Near(Length, 1.0f, 1e-5f), "and is a unit vector at any bearing, so it cannot scale the lighting");

        const GasDomainMeasure Measure = ResolveDomain(Settings);
        GasSceneCamera Camera;
        Camera.Theta = 0.72; Camera.Phi = 1.32; Camera.Distance = 3.05;
        const GasSceneEye Eye = ResolveEye(Camera, Measure);

        const float Dot = Eye.Forward[0] * Eye.Right[0] + Eye.Forward[1] * Eye.Right[1] + Eye.Forward[2] * Eye.Right[2];
        Check(Near(Dot, 0.0f, 1e-5f), "the camera basis is orthogonal");
        const float UpLength = std::sqrt(Eye.Up[0] * Eye.Up[0] + Eye.Up[1] * Eye.Up[1] + Eye.Up[2] * Eye.Up[2]);
        Check(Near(UpLength, 1.0f, 1e-5f), "and normalised, so a framing cannot stretch the picture");

        // The eye is outside the cube and looking into it, which is the arrangement the viewport uses.
        const float Reach = std::sqrt((Eye.Origin[0] - 0.5f) * (Eye.Origin[0] - 0.5f)
                                    + (Eye.Origin[1] - 0.5f) * (Eye.Origin[1] - 0.5f)
                                    + (Eye.Origin[2] - 0.5f) * (Eye.Origin[2] - 0.5f));
        Check(Reach > 0.5f, "the eye is outside the cube");
        const float Toward[3] = { 0.5f - Eye.Origin[0], 0.5f - Eye.Origin[1], 0.5f - Eye.Origin[2] };
        const float Facing = Eye.Forward[0] * Toward[0] + Eye.Forward[1] * Toward[1] + Eye.Forward[2] * Toward[2];
        Check(Facing > 0.0f, "and looking at it rather than away");

        // A framing with the eye on the centre is a scene somebody saved mid-drag, not a reason to refuse it.
        GasSceneCamera Degenerate;
        Degenerate.Distance = 0.0; Degenerate.Phi = 0.0; Degenerate.Theta = 0.0;
        Degenerate.Centre[0] = Degenerate.Centre[1] = Degenerate.Centre[2] = 0.0;
        const GasSceneEye Nudged = ResolveEye(Degenerate, Measure);
        Check(std::isfinite(Nudged.Forward[0]) && std::isfinite(Nudged.Up[2]),
              "a degenerate framing is nudged into a usable basis rather than producing a field of NaN");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                       THE CEILING, AND WHAT IT IS WORTH
    //----------------------------------------------------------------------------------------------------------------

    Banner("The source ceiling: the authored reading means the same on both sides");

    {
        const GasScene Scene = OpenSample("camp_fire_steady");
        CoarseGasField Field;
        const GasSceneRun Run = ResolveScene(Scene, Field);
        for (uint32_t Step = 0u; Step < 240u; ++Step) AdvanceScene(Field, Run);

        float Hottest = 0.0f, Densest = 0.0f;
        for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot)
        {
            if (Field.Temperature[Slot] > Hottest) Hottest = Field.Temperature[Slot];
            if (Field.Smoke[Slot]       > Densest) Densest = Field.Smoke[Slot];
        }
        Check(Hottest > 0.0f, "four seconds of camp fire leaves heat in the field");
        Check(Hottest <= Scene.Settings.EmitterTemperature + 1e-3f,
              "and never more heat than the scene authored — measured at five times that before the ceiling existed");
        Check(Densest <= Scene.Settings.EmitterSmoke + 1e-3f, "nor more smoke");
        Check(TotalSmoke(Field) > 1.0f, "while still carrying a real plume rather than having been clamped flat");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                        EVERY COMMITTED SAMPLE, RUN
    //----------------------------------------------------------------------------------------------------------------

    Banner("The committed corpus, advanced");

    {
        for (const char* Name : SampleNames)
        {
            const GasScene Scene = OpenSample(Name);
            CoarseGasField Field;
            const GasSceneRun Run = ResolveScene(Scene, Field);
            if (Run.OneShot) InjectEmission(Field, ResolveBlast(Scene.Settings, Run.Measure), CoarseStepInterval);
            for (uint32_t Step = 0u; Step < 90u; ++Step) AdvanceScene(Field, Run);

            bool Finite = true;
            float Peak = 0.0f;
            for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot)
            {
                if (!std::isfinite(Field.Smoke[Slot]) || !std::isfinite(Field.VelocityZ[Slot])) Finite = false;
                if (Field.Smoke[Slot] > Peak) Peak = Field.Smoke[Slot];
            }
            std::printf("    %-20s  cube %.2f m   smoke %.1f   peak %.3f%s\n", Name, Run.Measure.Span,
                        static_cast<double>(TotalSmoke(Field)), static_cast<double>(Peak),
                        Run.OneShot ? "   (one-shot, detonated)" : "");
            Check(Finite, (std::string(Name) + " holds no infinity and no NaN after ninety advances").c_str());
            Check(TotalSmoke(Field) > 0.0f, (std::string(Name) + " is carrying gas").c_str());
            Check(Run.Measure.Span > 0.0f, (std::string(Name) + " resolves to a cube with a size").c_str());
        }
    }

    //----------------------------------------------------------------------------------------------------------------
    //                               THE OBSTRUCTION ARRIVES, AND THE RUN IS REPRODUCIBLE
    //----------------------------------------------------------------------------------------------------------------

    Banner("One-shot scenes, which say nothing about being one-shot");

    {
        const GasScene Burst = OpenSample("fracture_dust_burst");
        CoarseGasField Field;
        const GasSceneRun Run = ResolveScene(Burst, Field);
        Check(Run.OneShot, "a scene with no emitter and a loaded blast is recognised as a one-shot");

        for (uint32_t Step = 0u; Step < 60u; ++Step) AdvanceScene(Field, Run);
        Check(TotalSmoke(Field) == 0.0f,
              "and advancing it without firing leaves an empty cube — which passes every structural check there is");

        CoarseGasField Fired;
        const GasSceneRun FiredRun = ResolveScene(Burst, Fired);
        InjectEmission(Fired, ResolveBlast(Burst.Settings, FiredRun.Measure), CoarseStepInterval);
        for (uint32_t Step = 0u; Step < 60u; ++Step) AdvanceScene(Fired, FiredRun);
        Check(TotalSmoke(Fired) > 0.0f, "firing it puts dust in the air");

        const GasScene Steady = OpenSample("camp_fire_steady");
        CoarseGasField Burning;
        Check(!ResolveScene(Steady, Burning).OneShot, "and a continuous emitter is not mistaken for one");
    }

    Banner("Obstruction and reproducibility");

    {
        const GasScene Tyre = OpenSample("tyre_burnout_ring");
        CoarseGasField Field;
        const GasSceneRun Run = ResolveScene(Tyre, Field);
        AdvanceScene(Field, Run);
        Check(Run.Obstruction.Present, "the burnout scene carries an obstruction");
        Check(OccupiedVoxels(Field) > 0u, "and it reaches the field as solid voxels rather than as a struct");

        const GasScene Open = OpenSample("far_cheap_plume");
        CoarseGasField Clear;
        const GasSceneRun ClearRun = ResolveScene(Open, Clear);
        AdvanceScene(Clear, ClearRun);
        Check(OccupiedVoxels(Clear) == 0u, "a scene with no obstacle obstructs nothing");

        // Reproducibility, which is the only thing multiplayer rests on, through the whole resolved path.
        CoarseGasField First, Second;
        const GasSceneRun RunA = ResolveScene(Tyre, First);
        const GasSceneRun RunB = ResolveScene(Tyre, Second);
        for (uint32_t Step = 0u; Step < 60u; ++Step) { AdvanceScene(First, RunA); AdvanceScene(Second, RunB); }
        Check(First.Smoke == Second.Smoke && First.VelocityZ == Second.VelocityZ,
              "resolving and advancing the same scene twice gives bit-identical readings");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                 THE SCENE STILL CROSSES AFTER ALL OF THAT
    //----------------------------------------------------------------------------------------------------------------

    Banner("And the file is still the file");

    {
        // Resolving must not touch the scene it was handed. A conversion that writes back into its input is
        //    how a corpus starts drifting one save at a time.
        for (const char* Name : SampleNames)
        {
            const std::string Path = std::string("Experimental/Fluid/Samples/") + Name + ".gasscene.toml";
            const std::string Original = ReadWhole(Path);
            GasScene Scene;
            const GasSceneReading Reading = ReadGasScene(Original, Scene);
            if (!Reading.Accepted) throw std::runtime_error(Path);
            CoarseGasField Field;
            const GasSceneRun Run = ResolveScene(Scene, Field);
            for (uint32_t Step = 0u; Step < 10u; ++Step) AdvanceScene(Field, Run);
            Check(WriteGasScene(Scene) == Original,
                  (std::string(Name) + " still writes back byte for byte after being run").c_str());
        }
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
