/*==========================================================================================================================================
                                                             PHYSICSEXTENSION.H
==========================================================================================================================================*/
// 🧩 The bridge between a level and its PhysicsSpace. Begin surveys every record into the WorldRegister and brings each object
//    into Jolt through the PhysicsAdapter by the part it takes: a Geometry record shown once with Physics on becomes a moving box
//    of its resolved extent, the other objects shown once become fixed colliders, a fracturable object stays a fixed collider over
//    its bounds (no fracture yet), an object with several copies is held (its copies share one transform), RigidBody records become
//    spheres, each simulated Vehicle record becomes a Jolt chassis carrying Project Drive's solver (its Tyre records set the
//    corners), and a ground slab tops out at z = 0. The register is written beside the level as Markdown. The space then steps at
//    a fixed rate (240 Hz with a vehicle, 60 Hz without), on the physics thread or on the caller's; each refresh takes the newest
//    snapshot, writes the moving bodies and vehicles back into their records and publishes the vehicles' live readings. End puts
//    every record back where the simulation found it (the editor's Simulate).

#pragma once

#include "Frontier/Extensions/Physics/PhysicsSpace.h"
#include "Frontier/Extensions/Physics/PhysicsThread.h"
#include "Frontier/Extensions/Portals/PortalSpace.h"
#include "Frontier/Extensions/Physics/PhysicsVehicle.h"

#include "Frontier/Engine/Scene/CollisionSurface.h"
#include "Frontier/Engine/Scene/EngineConfigurations.h"
#include "Frontier/Engine/Scene/RecordToken.h"
#include "Frontier/Engine/Scene/WorldRegister.h"

#include <array>
#include <memory>
#include <vector>

namespace Frontier
{

class WorldSpace;

struct PhysicsExtensionSettings
{
    PhysicsSpaceSettings Space{};
    bool                 Threaded            = false;   // [-] the physics thread steps (the editor); false: Step() steps inline
    double               StepHz              = 0.0;     // [Hz] 0: 240 with a vehicle, 60 without
    int                  CollisionSteps      = 1;       // [-] Jolt broadphase/narrowphase steps per fixed step
    int                  SolverSubsteps      = 1;       // [-] inner vehicle/drivetrain/tyre sub-steps per fixed step
    uint32_t             MaximumCatchUpSteps = 8;       // [-] physics-thread steps per wake before a backlog is dropped
    bool                 WriteRegister       = true;    // [-] <Level>.WorldRegister.md beside a loaded level
    // 📝 The level's collision facets (world-space triangles per record), from the renderer's occupancy. When present, a Geometry
    //    record collides as its own surface — a fixed triangle set, or a moving convex hull — instead of a bounding box. Null
    //    falls back to the bounds box (a level opened without a renderer, or a record the scene did not resolve).
    const CollisionSurfaceArchive* CollisionSurfaces = nullptr;
};

struct PhysicsExtensionTally
{
    size_t Moving    = 0;   // [-] bodies written back to records
    size_t Fixed     = 0;   // [-] colliders, the ground included
    size_t Held      = 0;   // [-] objects with several copies (they keep their place and are not collided against)
    size_t Fracture  = 0;   // [-] fracturable objects, fixed over their bounds
    size_t Vehicles  = 0;   // [-] vehicles on Jolt chassis
    size_t Skipped   = 0;   // [-] objects without a size or hidden
    double StepHz    = 0.0;
    bool   Threaded  = false;
    double StepMilliseconds = 0.0;   // [ms] the caller's last Step (inline: the stepping itself)
};

// 📝 What the physics hands the editor each wake: the linked bodies' readings and every vehicle's view.
struct PhysicsSnapshot
{
    uint64_t                        Steps            = 0;
    double                          SimulatedSeconds = 0.0;
    std::vector<PhysicsBodyReading> Bodies;     // [-] one per moving record, in link order
    std::vector<PhysicsVehicleView> Vehicles;   // [-] one per vehicle
};

class PhysicsExtension
{
  public:
    PhysicsExtension();
    ~PhysicsExtension();

    // 📝 Builds the space from the level and remembers every moved record's transform. Returns the moving bodies and vehicles.
    size_t Begin(WorldSpace& World, const PhysicsExtensionSettings& Settings);
    size_t Begin(WorldSpace& World, const PhysicsSpaceSettings& Settings = {});
    // 📝 Inline: steps the space for DeltaSeconds in fixed steps; threaded: hands the inputs over and takes the newest snapshot.
    //    Either way writes the moving bodies and vehicles back. Returns the records written.
    size_t Step(WorldSpace& World, double DeltaSeconds);
    // 📝 Stops the thread, puts every moved record back and clears the space.
    void End(WorldSpace& World);

    // 📝 Driver controls for the possessed vehicles, and a request to put them back at their start.
    void AssignVehicleInput(const PhysicsVehicleInput& Input);
    void RequestVehicleReset();
    // 📝 Validations: Count fixed steps on the caller's thread now (inline mode only), then a snapshot.
    void AdvanceSteps(uint32_t Count);
    // 📝 Carries moving bodies through paired portal apertures: a front-to-back crossing inside an aperture teleports the body
    //    to the peer, flipped and scaled by the aperture ratio (position, orientation and both velocities), and resizes the
    //    record. Caller-thread, inline mode (the portal transit reads and writes the space and the level together).
    size_t IntegratePortals(WorldSpace& World);
    [[nodiscard]] const PortalSpace& QueryPortals() const noexcept { return Portals; }

    [[nodiscard]] bool                         QueryRunning() const noexcept { return Space != nullptr; }
    [[nodiscard]] const PhysicsSpace*          QuerySpace() const noexcept { return Space.get(); }
    [[nodiscard]] const PhysicsExtensionTally& QueryTally() const noexcept { return Tally; }
    [[nodiscard]] const WorldRegister&         QueryRegister() const noexcept { return Register; }
    [[nodiscard]] const PhysicsSnapshot&       QuerySnapshot() const noexcept { return Latest; }
    [[nodiscard]] double                       QueryStepSeconds() const noexcept { return StepSeconds; }
    [[nodiscard]] PhysicsThreadMeasurements    QueryThreadMeasurements() const { return Thread.QueryMeasurements(); }
    [[nodiscard]] const PhysicsVehicle*        QueryVehicle(size_t Index) const noexcept
    { return Index < Vehicles.size() ? Vehicles[Index].Physics.get() : nullptr; }

  private:
    struct Link
    {
        uint32_t              Body = 0;
        RecordToken           Record{};
        bool                  Geometry = true;               // a Geometry record, else a RigidBody record
        std::array<double, 3> CentreOffset{};                // [m] the shape's centre from the record's origin, in its own axes
        SpatialConfiguration  Saved{};                       // the transform before the simulation
        bool                  SavedDynamic = false;          // the Geometry record's Dynamic flag before the simulation
        std::array<double, 3> PreviousPosition{};            // [m] last step's position, for portal crossing detection
        bool                  PreviousValid = false;         // [-] whether PreviousPosition holds a reading
    };
    struct VehicleLink
    {
        std::unique_ptr<PhysicsVehicle> Physics;
        RecordToken                     Record{};
        std::vector<RecordToken>        Tyres;
        SpatialConfiguration            Saved{};
        bool                            Possessed    = true;
        double                          CentreHeight = 0.53;   // [m] centre of mass above the ground at rest
        Vehicle::TyreCarcassCategory    Carcass      = Vehicle::TyreCarcassCategory::Spring;
        std::array<bool, 4>             CornerFitted{ true, true, true, true };
        int                             SolverSubsteps = 1;
    };
    struct VehicleCommand
    {
        PhysicsVehicleInput Input{};
        uint64_t            ResetSerial = 0;
    };

    void StepOnce(double Seconds);
    void FillSnapshot(PhysicsSnapshot& Snapshot) const;
    size_t ApplySnapshot(WorldSpace& World, const PhysicsSnapshot& Snapshot);
    void AddVehicle(WorldSpace& World, RecordToken Record, WorldRegisterEntry* Entry);

    std::unique_ptr<PhysicsSpace>            Space;
    PortalSpace                              Portals{};        // the level's paired apertures, resolved at Begin
    std::vector<Link>                        Links;
    std::vector<VehicleLink>                 Vehicles;
    WorldRegister                            Register;
    PhysicsExtensionTally                    Tally;
    PhysicsThread                            Thread;
    PhysicsSnapshotExchange<PhysicsSnapshot> Snapshots;
    PhysicsMailbox<VehicleCommand>           Commands;
    VehicleCommand                           Pending{};          // [-] the caller's latest controls
    VehicleCommand                           Applied{};          // [-] physics side: the controls the steps use
    uint64_t                                 AppliedReset = 0;   // [-] physics side: the last reset carried out
    PhysicsSnapshot                          Latest;             // [-] the snapshot the last Step applied
    uint64_t                                 StepCount      = 0;
    double                                   StepSeconds    = 1.0 / 60.0;
    int                                      CollisionSteps = 1;
    int                                      SolverSubsteps = 1;
    double                                   Remainder      = 0.0;   // [s] time not yet stepped (inline)
    bool                                     Threaded       = false;
};

} // namespace Frontier
