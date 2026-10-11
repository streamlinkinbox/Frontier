//==============================================================================================================================================
//                                                        NATIVEGASOBSTRUCTION.CPP
//==============================================================================================================================================
// 📦 Executed proof for all three collision levels: the primitive, the engine's own distance field, and the two-way switch that stays off.
//
// Level ② is bound to the real Engine/GeometricRaster/GlobalDistanceFieldSpace here — an L-shaped wall is baked
//    into a DistanceFieldSpace, registered, rasterised, and then obstructs the gas with no primitive anywhere.
//    That is the claim the seam makes, and a mock distance function would not have tested it.
//
//    ① WHAT HAS NOT CONSENTED DOES NOT OBSTRUCT. The opt-in is the deliverable; a default-on world is one
//       where every field walks every collider in the level and turning that off means editing code.
//    ② THE CULL IS BY SCENE ORDER, NOT BY ARRIVAL. A cull that dropped a different collider on two machines
//       would obstruct differently on each, and the gas would not match across a network.
//    ③ OCCUPANCY IS REBUILT, NEVER ACCUMULATED. A moved obstruction that left its old voxels solid trails a
//       wake the player can see and nobody can explain.
//    ④ A MOVING OBSTRUCTION PUSHES THROUGH ITS BOUNDARY. Writing velocity inside a solid does nothing: the
//       solver stills it in the same advance, and the push silently never happens.
//    ⑤ THE TWO-WAY SWITCH IS OFF AND IS READ FROM ONE PLACE. Two copies of a toggle is how a feature that is
//       off everywhere in the editor turns out to be on for one crate in one level.

#include "CoarseGasField.h"
#include "GasCollisionIntake.h"
#include "GasObstructionConsent.h"
#include "GasPresetLibrary.h"
#include "GasSceneDistanceIntake.h"
#include "GasWindContribution.h"
#include "VolumeRaymarch.h"

#include "PngWriteCounterpart.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
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

VolumeSample ReadCoarseField(const void* Context, const float Coordinate[3])
{
    const CoarseGasField& Field = *static_cast<const CoarseGasField*>(Context);
    const float Lattice[3] = { Coordinate[0] * static_cast<float>(CoarseExtent) - 0.5f,
                               Coordinate[1] * static_cast<float>(CoarseExtent) - 0.5f,
                               Coordinate[2] * static_cast<float>(CoarseExtent) - 0.5f };
    VolumeSample Reading;
    Reading.Smoke       = ReadTrilinear(Field.Smoke,       Lattice[0], Lattice[1], Lattice[2]);
    Reading.Temperature = ReadTrilinear(Field.Temperature, Lattice[0], Lattice[1], Lattice[2]);
    return Reading;
}

// Smoke above a height, which is where an obstruction either stopped the plume or did not.
float SmokeAbove(const CoarseGasField& Field, uint32_t Layer)
{
    float Total = 0.0f;
    for (uint32_t Z = Layer; Z < CoarseExtent; ++Z)
    for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
    for (uint32_t X = 0u; X < CoarseExtent; ++X)
    {
        Total += Field.Smoke[VoxelIndex(X, Y, Z)];
    }
    return Total;
}

float SmokeInsideSolids(const CoarseGasField& Field)
{
    float Total = 0.0f;
    for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot)
    {
        if (Field.Occupancy[Slot] > 0.5f) Total += Field.Smoke[Slot];
    }
    return Total;
}

// A camp fire under whatever obstruction the caller admitted. Returns the field after Advances steps.
void BurnBeneath(CoarseGasField& Field, const GasObstructionConsent* Consents, uint32_t ConsentCount,
                 uint32_t Advances)
{
    const GasSettings Preset = ConstructPresetSettings("camp_fire");

    GasEmission Wood;
    Wood.Position[0] = Field.Origin[0] + Field.Span * 0.5f;
    Wood.Position[1] = Field.Origin[1] + Field.Span * 0.5f;
    Wood.Position[2] = Field.Origin[2] + Field.Span * 0.12f;
    Wood.Radius          = Field.Span * 0.08f;
    Wood.SmokeRate       = Preset.EmitterSmoke;
    Wood.TemperatureRate = Preset.EmitterTemperature;
    Wood.Velocity[2]     = Preset.EmitterUpwardVelocity * 0.3f;

    CoarseGasSettings Solver;
    Solver.BuoyancyLift = Preset.Buoyancy * 0.4f;
    Solver.SmokeWeight  = Preset.SmokeWeight * 0.45f;

    GasCollider Scratch[8];
    for (uint32_t Step = 0u; Step < Advances; ++Step)
    {
        AdmitObstructions(Field, Consents, ConsentCount, nullptr, nullptr, Scratch, 8u);
        InjectEmission(Field, Wood, CoarseStepInterval);
        AdvanceField(Field, Solver);
    }
}

struct Capture
{
    uint32_t Extent = 0u;
    std::vector<unsigned char> Pixels;   // RGB
};

Capture Render(const CoarseGasField& Field, uint32_t Extent, float DomainDiagonal)
{
    VolumeLighting Lighting;
    const GasSettings Preset = ConstructPresetSettings("camp_fire");
    Lighting.DensityExtinction = Preset.DensityExtinction;
    Lighting.SmokeAlbedo       = Preset.SmokeAlbedo;
    Lighting.FireIntensity     = Preset.FireIntensity;
    Lighting.Exposure          = Preset.Exposure;
    const float Sun[3] = { 0.4f, 0.3f, 0.86f };

    Capture Shot;
    Shot.Extent = Extent;
    Shot.Pixels.assign(static_cast<std::size_t>(Extent) * Extent * 3u, 0u);

    const float Origin[3] = { 0.5f + 1.9f, 0.5f, 0.62f };
    float Forward[3] = { 0.5f - Origin[0], 0.5f - Origin[1], 0.5f - Origin[2] };
    const float Reach = std::sqrt(Forward[0] * Forward[0] + Forward[1] * Forward[1] + Forward[2] * Forward[2]);
    for (int Axis = 0; Axis < 3; ++Axis) Forward[Axis] /= Reach;
    float Right[3] = { Forward[1], -Forward[0], 0.0f };
    const float RightReach = std::sqrt(Right[0] * Right[0] + Right[1] * Right[1]);
    for (int Axis = 0; Axis < 3; ++Axis) Right[Axis] /= RightReach;
    const float Up[3] = { Right[1] * Forward[2] - Right[2] * Forward[1],
                          Right[2] * Forward[0] - Right[0] * Forward[2],
                          Right[0] * Forward[1] - Right[1] * Forward[0] };

    for (uint32_t Row = 0u; Row < Extent; ++Row)
    for (uint32_t Column = 0u; Column < Extent; ++Column)
    {
        const float ScreenX = (static_cast<float>(Column) + 0.5f) / static_cast<float>(Extent) * 2.0f - 1.0f;
        const float ScreenY = 1.0f - (static_cast<float>(Row) + 0.5f) / static_cast<float>(Extent) * 2.0f;
        float Direction[3];
        for (int Axis = 0; Axis < 3; ++Axis)
            Direction[Axis] = Forward[Axis] + Right[Axis] * ScreenX * 0.36f + Up[Axis] * ScreenY * 0.36f;
        const float Length = std::sqrt(Direction[0] * Direction[0] + Direction[1] * Direction[1]
                                     + Direction[2] * Direction[2]);
        for (int Axis = 0; Axis < 3; ++Axis) Direction[Axis] /= Length;

        const MarchedRay Marched = MarchVolume(&ReadCoarseField, &Field, Origin, Direction, Sun,
                                               Lighting, DomainDiagonal);
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            Shot.Pixels[(static_cast<std::size_t>(Row) * Extent + Column) * 3u
                        + static_cast<std::size_t>(Channel)] =
                TransferToDisplay(Marched.Radiance[Channel], Lighting.Exposure);
        }
    }
    return Shot;
}

void Write(const Capture& Shot, const char* Path)
{
    const int Written = PngWriteCounterpart::WritePng(Path, static_cast<int>(Shot.Extent),
                                                      static_cast<int>(Shot.Extent), 3,
                                                      Shot.Pixels.data(), static_cast<int>(Shot.Extent) * 3);
    if (Written == 0) throw std::runtime_error("could not write a capture");
    std::printf("    wrote %s\n", Path);
}

}   // namespace


int main()
{
    std::printf("NATIVE GAS OBSTRUCTION - the three collision levels, and who consented to each\n");
    std::filesystem::create_directories("Exhibits/Gallery/GasObstruction");

    const float Origin[3] = { 0.0f, 0.0f, 0.0f };

    //---------------------------------------------------------------------------------------------------------
    Banner("① The opt-in is the deliverable: nothing obstructs until an object says so");
    {
        GasObstructionConsent Crate;
        Crate.Carrier = 7u;
        Check(!Crate.Obstructs && !Crate.Pushed,
              "a freshly made record obstructs nothing and is pushed by nothing - every default is off");
        Check(!ObstructsByPrimitive(Crate) && !ObstructsByDistanceReading(Crate) && !PushedByGas(Crate),
              "and all three levels agree about it");

        Crate.Obstructs = true;
        Check(!ObstructsByPrimitive(Crate),
              "ticking the box without giving it a shape still obstructs nothing - that is an unfinished crate, not an error");

        Crate.Shape = GasColliderShape::Box;
        Check(ObstructsByPrimitive(Crate), "with a box it obstructs through level ①");
        Check(!ObstructsByDistanceReading(Crate), "and not through level ②, which is a different answer");

        Crate.ByDistanceReading = true;
        Check(ObstructsByDistanceReading(Crate) && !ObstructsByPrimitive(Crate),
              "an object sent to the distance field is not also walked as a primitive - that would be two admissions of one crate");

        Check(!PushedByGas(Crate), "⑤ and none of that turned the two-way switch on");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The routing rule: analytic shapes take level ①, everything else takes level ②");
    {
        // A crate is a box. A box is six signed distances, so it is routed to the primitive and never asks
        //    the raster anything.
        GasObstructionConsent Crate;
        Crate.Obstructs = true;
        AssignObstructionLevel(Crate, GasColliderShape::Box);
        Check(ObstructsByPrimitive(Crate) && !ObstructsByDistanceReading(Crate),
              "a crate is a box, and a box is level ① - 32768 clipmap samples to rediscover six planes is the wrong answer");

        // Terrain is not any of the five shapes, so it goes to the field the raster already baked.
        GasObstructionConsent Terrain;
        Terrain.Obstructs = true;
        AssignObstructionLevel(Terrain, GasColliderShape::None);
        Check(ObstructsByDistanceReading(Terrain) && !ObstructsByPrimitive(Terrain),
              "terrain is not a sphere, so it is level ② - and it never moves, so the clipmap never restages");

        // The rule is one fact, applied the same way every time, and it is reversible.
        AssignObstructionLevel(Terrain, GasColliderShape::Cylinder);
        Check(ObstructsByPrimitive(Terrain),
              "giving an object a primitive moves it to level ① in the same breath, with no second switch to forget");
        AssignObstructionLevel(Terrain, GasColliderShape::None);
        Check(ObstructsByDistanceReading(Terrain), "and taking it away moves it back");

        GasObstructionConsent Unasked;
        AssignObstructionLevel(Unasked, GasColliderShape::Sphere);
        Check(!ObstructsByPrimitive(Unasked),
              "① routing an object is not consenting for it: it still obstructs nothing until somebody ticks the box");

        // ⚠️ The caveat the table cannot state: a fast mesh is better off approximated.
        GasObstructionConsent Car;
        Car.Obstructs = true;
        AssignObstructionLevel(Car, GasColliderShape::None);
        Check(!PrefersLocalReading(Car, 0.0f),
              "a car that is parked for good may be composited into the world like any other scenery");
        Check(PrefersLocalReading(Car, 28.0f),
              "⚠️ but a car at 100 km/h is read in its own space instead - the composited world is stale the moment it moves");
        Check(PrefersLocalReading(Car, 0.9f) && PrefersLocalReading(Car, -0.9f),
              "and so is one creeping either way: anything that moves perceptibly is better asked than rebaked");

        GasObstructionConsent Wheel;
        Wheel.Obstructs = true;
        AssignObstructionLevel(Wheel, GasColliderShape::TyreRing);
        Check(!PrefersLocalReading(Wheel, 28.0f),
              "a wheel that is already a ring is analytic and has nothing to read locally");
        Check(ObstructsByPrimitive(Wheel),
              "which is what GasColliderShape::TyreRing was for: a thing this simple stays level ①");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("⑤ The two-way switch is read from exactly one place");
    {
        GasObstructionConsent Barrel;
        Barrel.Obstructs = true;
        Barrel.Shape     = GasColliderShape::Cylinder;

        GasCouplingConsent Authored;                // what the artist tuned: a cylinder's drag, its mass
        Authored.DragCoefficient = 0.82f;
        Authored.ReferenceArea   = 0.6f;
        Authored.Mass            = 18.0f;
        Authored.CouplingEnabled = true;            // 🚩 a stale copy of the toggle, as a saved file might carry

        const GasCouplingConsent Resolved = ConstructCouplingConsent(Barrel, Authored);
        Check(!Resolved.CouplingEnabled,
              "the object's own record wins over a stale toggle in the tuning - one switch, one place");
        Check(Resolved.DragCoefficient == 0.82f && Resolved.Mass == 18.0f, "while the authored tuning survives intact");

        float Force[3] = { 1.0f, 1.0f, 1.0f };
        const float Flow[3] = { 6.0f, 0.0f, 2.0f }, Still[3] = { 0.0f, 0.0f, 0.0f };
        ResolveDragForce(Resolved, Flow, Still, CoarseStepInterval, Force);
        Check(Force[0] == 0.0f && Force[1] == 0.0f && Force[2] == 0.0f,
              "so a barrel nobody opted in feels exactly zero newtons in a six metre a second flow");

        Barrel.Pushed = true;
        const GasCouplingConsent Opted = ConstructCouplingConsent(Barrel, Authored);
        ResolveDragForce(Opted, Flow, Still, CoarseStepInterval, Force);
        Check(Force[0] > 0.0f, "and the same barrel, opted in, is pushed along the flow");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("② The cull is by scene order, and drops its tail rather than reordering");
    {
        CoarseGasField Field;
        ResetField(Field, Origin, 4.0f);

        GasObstructionConsent Scene[6];
        for (uint32_t Index = 0u; Index < 6u; ++Index)
        {
            Scene[Index].Carrier       = Index;
            Scene[Index].Obstructs     = true;
            Scene[Index].Shape         = GasColliderShape::Sphere;
            Scene[Index].Dimensions[0] = 0.3f;
            Scene[Index].Centre[0]     = 0.5f + static_cast<float>(Index) * 0.5f;
            Scene[Index].Centre[1]     = 2.0f;
            Scene[Index].Centre[2]     = 2.0f;
        }
        Scene[2].Centre[0] = 400.0f;            // across the level
        Scene[4].Obstructs = false;             // never opted in
        Scene[5].ByDistanceReading = true;      // level ② instead

        GasCollider Walked[8];
        const uint32_t Count = ResolveObstructions(Scene, 6u, Field, Walked, 8u);
        Check(Count == 3u, "three of six are walked: one is far away, one never opted in, one is level ②");
        Check(Walked[0].Centre[0] == 0.5f && Walked[1].Centre[0] == 1.0f && Walked[2].Centre[0] == 2.0f,
              "and they come out in scene order with the gaps closed, not in whatever order they were found");

        GasCollider Cramped[2];
        Check(ResolveObstructions(Scene, 6u, Field, Cramped, 2u) == 2u, "a cramped list is filled to its room");
        Check(Cramped[0].Centre[0] == 0.5f && Cramped[1].Centre[0] == 1.0f,
              "② and it is the TAIL that is dropped - the same two survive on every machine with this scene");

        Check(ResolveObstructions(nullptr, 6u, Field, Walked, 8u) == 0u, "a scene with no records walks nothing");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Level ①: a sphere in the plume, and the plume goes around it");

    CoarseGasField Open, Blocked;
    {
        GasObstructionConsent Boulder;
        Boulder.Carrier       = 1u;
        Boulder.Obstructs     = true;
        Boulder.Shape         = GasColliderShape::Sphere;
        Boulder.Dimensions[0] = 0.55f;
        Boulder.Centre[0]     = 2.0f;
        Boulder.Centre[1]     = 2.0f;
        Boulder.Centre[2]     = 1.5f;

        ResetField(Open, Origin, 4.0f);
        BurnBeneath(Open, nullptr, 0u, 150u);

        ResetField(Blocked, Origin, 4.0f);
        BurnBeneath(Blocked, &Boulder, 1u, 150u);

        Check(OccupiedVoxels(Blocked) > 0u, "the boulder is in the lattice");
        Check(OccupiedVoxels(Open) == 0u, "and an unobstructed field has nothing solid in it at all");
        Check(SmokeInsideSolids(Blocked) == 0.0f,
              "🔴 no smoke is inside the boulder - gas inside geometry is a hole in the world the player can see through");
        Check(SmokeAbove(Blocked, 24u) < SmokeAbove(Open, 24u),
              "less smoke reaches the top of the box than without it, because some of it went around");
        Check(SmokeAbove(Blocked, 24u) > 0.0f,
              "but not none: a 55 cm boulder splits a plume, it does not cap a chimney");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("③ Occupancy is rebuilt every advance, so a moved obstruction leaves no wake");
    {
        CoarseGasField Field;
        ResetField(Field, Origin, 4.0f);

        GasObstructionConsent Lift;
        Lift.Obstructs     = true;
        Lift.Shape         = GasColliderShape::Box;
        Lift.Dimensions[0] = Lift.Dimensions[1] = 0.4f;
        Lift.Dimensions[2] = 0.3f;
        Lift.Centre[0] = Lift.Centre[1] = 2.0f;
        Lift.Centre[2] = 1.0f;

        GasCollider Scratch[4];
        const uint32_t Low = AdmitObstructions(Field, &Lift, 1u, nullptr, nullptr, Scratch, 4u);
        Check(Low > 0u, "a platform at one metre fills some voxels");

        Lift.Centre[2] = 2.6f;
        const uint32_t High = AdmitObstructions(Field, &Lift, 1u, nullptr, nullptr, Scratch, 4u);
        // Not exactly equal: a box half a voxel off the lattice catches a different number of centres. The
        //    claim is that it is the same platform, not two platforms' worth of voxels.
        Check(High > 0u && High < Low + Low / 2u && Low < High + High / 2u,
              "raised, it fills about the same number - nothing like the sum of where it has been");

        bool LowerClear = true;
        const float Size = VoxelSize(Field);
        for (uint32_t Z = 0u; Z < CoarseExtent && LowerClear; ++Z)
        {
            if (Origin[2] + (static_cast<float>(Z) + 0.5f) * Size > 1.4f) continue;
            for (uint32_t Y = 0u; Y < CoarseExtent && LowerClear; ++Y)
            for (uint32_t X = 0u; X < CoarseExtent && LowerClear; ++X)
            {
                LowerClear = Field.Occupancy[VoxelIndex(X, Y, Z)] <= 0.5f;
            }
        }
        Check(LowerClear, "③ and where it used to be is open air again, with no solid wake behind it");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("④ A moving obstruction pushes the air in front of it, through its boundary");
    {
        CoarseGasField Field;
        ResetField(Field, Origin, 4.0f);

        GasObstructionConsent Door;
        Door.Obstructs     = true;
        Door.Shape         = GasColliderShape::Box;
        Door.Dimensions[0] = 0.2f;
        Door.Dimensions[1] = 0.8f;
        Door.Dimensions[2] = 0.9f;
        Door.Centre[0] = 1.2f; Door.Centre[1] = 2.0f; Door.Centre[2] = 1.2f;

        GasCollider Scratch[4];
        AdmitObstructions(Field, &Door, 1u, nullptr, nullptr, Scratch, 4u);
        float Moved = 0.0f;
        for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot) Moved += std::fabs(Field.VelocityX[Slot]);
        Check(Moved == 0.0f, "a door standing still moves no air at all");

        Door.Velocity[0] = 3.5f;
        const uint32_t Pushed = AdmitBoundaryMotion(Field, &Door, 1u);
        Check(Pushed > 0u, "④ swung at three and a half metres a second, it writes into the air around it");

        uint32_t InsideWritten = 0u, OutsideWritten = 0u;
        const GasCollider Shape = ConstructCollider(Door);
        const float Size = VoxelSize(Field);
        for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
        for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
        for (uint32_t X = 0u; X < CoarseExtent; ++X)
        {
            const uint32_t Slot = VoxelIndex(X, Y, Z);
            if (Field.VelocityX[Slot] == 0.0f) continue;
            const float Position[3] = { Origin[0] + (static_cast<float>(X) + 0.5f) * Size,
                                        Origin[1] + (static_cast<float>(Y) + 0.5f) * Size,
                                        Origin[2] + (static_cast<float>(Z) + 0.5f) * Size };
            if (ColliderDistance(Shape, Position) < 0.0f) ++InsideWritten; else ++OutsideWritten;
        }
        Check(InsideWritten == 0u,
              "④ and never inside itself, where the solver would still the air in the same advance");
        Check(OutsideWritten == Pushed, "every push landed in open air, which is the only place it can carry from");

        // And the push is real: smoke beside a moving door is carried, smoke beside a parked one is not.
        CoarseGasField Carried, Parked;
        for (CoarseGasField* One : { &Carried, &Parked })
        {
            ResetField(*One, Origin, 4.0f);
            GasEmission Puff;
            Puff.Position[0] = 1.7f; Puff.Position[1] = 2.0f; Puff.Position[2] = 1.2f;
            Puff.Radius = 0.25f; Puff.SmokeRate = 3.0f;
            InjectEmission(*One, Puff, 0.25f);
        }
        CoarseGasSettings Still;
        Still.BuoyancyLift = 0.0f; Still.SmokeWeight = 0.0f; Still.TurbulenceStrength = 0.0f;

        GasObstructionConsent Parkedoor = Door;
        Parkedoor.Velocity[0] = 0.0f;
        for (uint32_t Step = 0u; Step < 30u; ++Step)
        {
            AdmitObstructions(Carried, &Door,      1u, nullptr, nullptr, Scratch, 4u);
            AdvanceField(Carried, Still);
            AdmitObstructions(Parked,  &Parkedoor, 1u, nullptr, nullptr, Scratch, 4u);
            AdvanceField(Parked, Still);
        }
        float CarriedFar = 0.0f, ParkedFar = 0.0f;
        for (uint32_t Z = 0u; Z < CoarseExtent; ++Z)
        for (uint32_t Y = 0u; Y < CoarseExtent; ++Y)
        for (uint32_t X = 24u; X < CoarseExtent; ++X)     // the far side, away from the door
        {
            CarriedFar += Carried.Smoke[VoxelIndex(X, Y, Z)];
            ParkedFar  += Parked.Smoke[VoxelIndex(X, Y, Z)];
        }
        Check(CarriedFar > ParkedFar, "and smoke beside a swinging door is driven away from it, where a parked one leaves it");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Level ②: the engine's own global distance field, with no primitive anywhere");

    CoarseGasField Walled;
    {
        // An L-shaped wall, baked as a signed distance into the raster's own structure. Nothing about this
        //    shape could be expressed as one of the five primitives, which is the entire point of level ②.
        DistanceFieldSpace Wall(32u, 32u, 32u, Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 4.0f, 4.0f, 4.0f });
        for (uint32_t Z = 0u; Z < 32u; ++Z)
        for (uint32_t Y = 0u; Y < 32u; ++Y)
        for (uint32_t X = 0u; X < 32u; ++X)
        {
            const float Px = (static_cast<float>(X) + 0.5f) * 0.125f;
            const float Py = (static_cast<float>(Y) + 0.5f) * 0.125f;
            const float Pz = (static_cast<float>(Z) + 0.5f) * 0.125f;

            // Two slabs meeting at a corner, both standing above the fire.
            const float Along = std::fabs(Px - 2.3f) - 0.22f;              // a wall across X
            const float Cross = std::fabs(Py - 2.3f) - 0.22f;              // and one across Y
            const float Height = std::fabs(Pz - 2.1f) - 0.75f;
            const float First  = Along > Height ? Along : Height;
            const float Second = Cross > Height ? Cross : Height;
            Wall.SetVoxelSample(X, Y, Z, First < Second ? First : Second);
        }

        GlobalDistanceFieldSpace Scene(48u, 48u, 48u, Vector3{ 0.0f, 0.0f, 0.0f }, Vector3{ 4.0f, 4.0f, 4.0f });
        Scene.RegisterPlacement(&Wall, Vector3{ 0.0f, 0.0f, 0.0f });
        Scene.UpdateGlobalGrid();

        CoarseGasField Probe;
        ResetField(Probe, Origin, 4.0f);
        const uint32_t Marked = AdmitSceneGeometry(Probe, &Scene, 0.0f);
        Check(Marked > 0u,
              "🔴 an L-shaped wall obstructs the gas with no primitive authored for it - the raster already knew the shape");
        Check(AdmitSceneGeometry(Probe, nullptr, 0.0f) == 0u,
              "and a level with no distance field admits nothing rather than refusing to run");

        // The same wall, now obstructing a burning plume for real.
        GasObstructionConsent Cathedral;
        Cathedral.Carrier           = 3u;
        Cathedral.Obstructs         = true;
        Cathedral.ByDistanceReading = true;

        const GasSettings Preset = ConstructPresetSettings("camp_fire");
        GasEmission Wood;
        Wood.Position[0] = 2.0f; Wood.Position[1] = 2.0f; Wood.Position[2] = 0.5f;
        Wood.Radius          = 0.3f;
        Wood.SmokeRate       = Preset.EmitterSmoke;
        Wood.TemperatureRate = Preset.EmitterTemperature;
        Wood.Velocity[2]     = Preset.EmitterUpwardVelocity * 0.3f;
        CoarseGasSettings Solver;
        Solver.BuoyancyLift = Preset.Buoyancy * 0.4f;
        Solver.SmokeWeight  = Preset.SmokeWeight * 0.45f;

        ResetField(Walled, Origin, 4.0f);
        GasCollider Scratch[4];
        for (uint32_t Step = 0u; Step < 150u; ++Step)
        {
            AdmitObstructions(Walled, &Cathedral, 1u, &ReadSceneDistance, &Scene, Scratch, 4u);
            InjectEmission(Walled, Wood, CoarseStepInterval);
            AdvanceField(Walled, Solver);
        }
        Check(OccupiedVoxels(Walled) > 0u, "the wall is in the gas lattice as well as in the raster");
        Check(SmokeInsideSolids(Walled) == 0.0f, "and no smoke is inside it");
        Check(SmokeAbove(Walled, 24u) < SmokeAbove(Open, 24u),
              "the plume reaches the ceiling less readily with a wall over it than without one");

        // Levels ① and ② compose: the boulder and the wall in one field, admitted in one call.
        GasObstructionConsent Both[2];
        Both[0] = Cathedral;
        Both[1].Carrier       = 4u;
        Both[1].Obstructs     = true;
        Both[1].Shape         = GasColliderShape::Sphere;
        Both[1].Dimensions[0] = 0.45f;
        Both[1].Centre[0] = 1.2f; Both[1].Centre[1] = 1.2f; Both[1].Centre[2] = 1.2f;

        CoarseGasField Together;
        ResetField(Together, Origin, 4.0f);
        const uint32_t Mixed = AdmitObstructions(Together, Both, 2u, &ReadSceneDistance, &Scene, Scratch, 4u);
        Check(Mixed > 0u, "the two levels compose in one admission, in one field");
        CoarseGasField WallOnly;
        ResetField(WallOnly, Origin, 4.0f);
        const uint32_t Alone = AdmitObstructions(WallOnly, Both, 1u, &ReadSceneDistance, &Scene, Scratch, 4u);
        Check(Mixed > Alone, "and the sphere adds solid voxels the wall did not already own");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("🔴 A moving body is asked in its own space, never restaged - and so it may also turn");
    {
        // A plank: long in local X, short in local Y. Baked ONCE, in its own space, centred on its origin.
        //    Nothing below ever touches it again, however the body moves.
        DistanceFieldSpace Plank(32u, 32u, 32u, Vector3{ -1.0f, -1.0f, -1.0f }, Vector3{ 1.0f, 1.0f, 1.0f });
        for (uint32_t Z = 0u; Z < 32u; ++Z)
        for (uint32_t Y = 0u; Y < 32u; ++Y)
        for (uint32_t X = 0u; X < 32u; ++X)
        {
            const float Lx = -1.0f + (static_cast<float>(X) + 0.5f) * 0.0625f;
            const float Ly = -1.0f + (static_cast<float>(Y) + 0.5f) * 0.0625f;
            const float Lz = -1.0f + (static_cast<float>(Z) + 0.5f) * 0.0625f;
            const float Dx = std::fabs(Lx) - 0.85f, Dy = std::fabs(Ly) - 0.12f, Dz = std::fabs(Lz) - 0.12f;
            const float Widest = Dx > Dy ? (Dx > Dz ? Dx : Dz) : (Dy > Dz ? Dy : Dz);
            Plank.SetVoxelSample(X, Y, Z, Widest);
        }

        GasRigidDistanceBody Body;
        Body.Local = &Plank;
        Body.Translation[0] = 2.0f; Body.Translation[1] = 2.0f; Body.Translation[2] = 2.0f;

        // Along local X, 0.6 m from the origin, is inside the plank. Along local Y it is well outside.
        const float AlongX[3] = { 2.6f, 2.0f, 2.0f };
        const float AlongY[3] = { 2.0f, 2.6f, 2.0f };
        Check(ReadBodyDistance(Body, AlongX) < 0.0f, "a point along the plank's length is inside it");
        Check(ReadBodyDistance(Body, AlongY) > 0.0f, "and one along its width is outside it");

        // Now turn the body a quarter turn about axis 2. Nothing is rebaked; the QUERY rotates.
        GasRigidDistanceBody Turned = Body;
        Turned.Basis[0] = 0.0f; Turned.Basis[1] = 1.0f; Turned.Basis[2] = 0.0f;   // local X now points along world Y
        Turned.Basis[3] = -1.0f; Turned.Basis[4] = 0.0f; Turned.Basis[5] = 0.0f;
        Check(ReadBodyDistance(Turned, AlongY) < 0.0f,
              "🔴 turned a quarter turn, the plank is now inside where it was outside - rotation the composited world cannot express at all");
        Check(ReadBodyDistance(Turned, AlongX) > 0.0f, "and outside where it was inside");

        // Translating it is the same: one subtraction, no rebake.
        GasRigidDistanceBody Shifted = Body;
        Shifted.Translation[0] = 1.0f;
        Check(ReadBodyDistance(Shifted, AlongX) > 0.0f
              && ReadBodyDistance(Shifted, AlongX) > ReadBodyDistance(Body, AlongX),
              "and sliding the body slides what the gas sees, with nothing restaged anywhere");

        // Scale is carried out of the local space as well as into it, so the answer stays metres.
        GasRigidDistanceBody Halved = Body;
        Halved.Scale = 0.5f;
        Check(ReadBodyDistance(Halved, AlongX) > 0.0f, "a half-size plank no longer reaches 0.6 m along itself");

        // The obstruction follows the body through the lattice, advance by advance, with no restage call.
        CoarseGasField Field;
        ResetField(Field, Origin, 4.0f);
        GasRigidDistanceScene Moving;
        Moving.Bodies = &Body;
        Moving.Count  = 1u;

        ClearOccupancy(Field);
        const uint32_t Here = AdmitDistanceReading(Field, &ReadRigidSceneDistance, &Moving, 0.0f);
        Check(Here > 0u, "the plank is in the gas lattice, read straight out of its own space");

        GasRigidDistanceBody Elsewhere = Body;
        Elsewhere.Translation[1] = 3.2f;
        GasRigidDistanceScene Later;
        Later.Bodies = &Elsewhere;
        Later.Count  = 1u;
        CoarseGasField Second;
        ResetField(Second, Origin, 4.0f);
        ClearOccupancy(Second);
        const uint32_t There = AdmitDistanceReading(Second, &ReadRigidSceneDistance, &Later, 0.0f);
        Check(There > 0u && std::memcmp(Field.Occupancy.data(), Second.Occupancy.data(),
                                        CoarseVoxelCount * sizeof(float)) != 0,
              "and moving it moves the solid voxels - without one call to UpdateGlobalGrid in this whole section");

        // A spinning body drags air around itself even when its origin is not going anywhere.
        GasRigidDistanceBody Spinning = Body;
        Spinning.Spin[2] = 12.0f;
        GasRigidDistanceScene Turning;
        Turning.Bodies = &Spinning;
        Turning.Count  = 1u;

        CoarseGasField Stirred;
        ResetField(Stirred, Origin, 4.0f);
        ClearOccupancy(Stirred);
        AdmitDistanceReading(Stirred, &ReadRigidSceneDistance, &Turning, 0.0f);
        const uint32_t Dragged = AdmitBodyMotion(Stirred, Turning, 1.25f);
        Check(Dragged > 0u, "a plank spinning on the spot still drags the air beside it");

        float Swirl = 0.0f;
        for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot)
        {
            Swirl += std::fabs(Stirred.VelocityX[Slot]) + std::fabs(Stirred.VelocityY[Slot]);
        }
        Check(Swirl > 0.0f, "with a velocity that comes from the spin crossed into the arm, not from a translation it has not made");

        GasRigidDistanceBody Parked = Body;
        GasRigidDistanceScene Still;
        Still.Bodies = &Parked;
        Still.Count  = 1u;
        CoarseGasField Quiet;
        ResetField(Quiet, Origin, 4.0f);
        ClearOccupancy(Quiet);
        AdmitDistanceReading(Quiet, &ReadRigidSceneDistance, &Still, 0.0f);
        Check(AdmitBodyMotion(Quiet, Still, 1.25f) == 0u, "while a plank that is doing nothing moves no air at all");

        // Bodies and the static world are not alternatives: a car drives across terrain.
        GasRigidDistanceScene Both;
        Both.Bodies = &Body;
        Both.Count  = 1u;
        Both.Static = nullptr;
        const float Far[3] = { 0.2f, 0.2f, 3.8f };
        Check(ReadRigidSceneDistance(&Both, Far) > 0.0f, "a point far from everything is outside everything");
        Check(ReadRigidSceneDistance(nullptr, Far) > 1000.0f,
              "and a reading with no scene behind it answers 'nothing near' rather than crashing a solver");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The whole obstruction step is reproducible");
    {
        GasObstructionConsent Boulder;
        Boulder.Obstructs     = true;
        Boulder.Shape         = GasColliderShape::Sphere;
        Boulder.Dimensions[0] = 0.55f;
        Boulder.Centre[0] = 2.0f; Boulder.Centre[1] = 2.0f; Boulder.Centre[2] = 1.5f;
        Boulder.Velocity[1] = 0.8f;

        CoarseGasField First, Second;
        ResetField(First,  Origin, 4.0f);
        ResetField(Second, Origin, 4.0f);
        BurnBeneath(First,  &Boulder, 1u, 60u);
        BurnBeneath(Second, &Boulder, 1u, 60u);

        Check(std::memcmp(First.Smoke.data(), Second.Smoke.data(), CoarseVoxelCount * sizeof(float)) == 0
              && std::memcmp(First.VelocityX.data(), Second.VelocityX.data(), CoarseVoxelCount * sizeof(float)) == 0
              && std::memcmp(First.Occupancy.data(), Second.Occupancy.data(), CoarseVoxelCount * sizeof(float)) == 0,
              "sixty advances past a moving boulder are bit-identical on a repeat, occupancy included");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Captures");
    {
        Write(Render(Open,    280u, 4.0f * 1.732f), "Exhibits/Gallery/GasObstruction/PlumeUnobstructed.png");
        Write(Render(Blocked, 280u, 4.0f * 1.732f), "Exhibits/Gallery/GasObstruction/PlumeAroundSphere.png");
        Write(Render(Walled,  280u, 4.0f * 1.732f), "Exhibits/Gallery/GasObstruction/PlumeUnderDistanceField.png");
        Check(true, "three captures: no obstruction, a primitive, and the engine's own distance field");
    }

    std::printf("\nPASS %u\n", Checks);
    return 0;
}
