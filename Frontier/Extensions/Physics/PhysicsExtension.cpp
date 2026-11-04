/*==========================================================================================================================================
                                                            PHYSICSEXTENSION.CPP
==========================================================================================================================================*/
// 🧩 Level records into bodies and vehicles through the register, fixed steps inline or on the physics thread, snapshots written
//    back into records and the vehicles' live readings, and the restoration when the simulation stops.

#include "Frontier/Extensions/Physics/PhysicsExtension.h"

#include "Frontier/Engine/Rendering/GlobalIllumination/TransformProjection.h"
#include "Frontier/Engine/Scene/VehicleReadings.h"
#include "Frontier/Engine/Scene/WorldSpace.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace Frontier
{

namespace
{

std::array<double, 3> ToDouble(const float Coordinates[3])
{
    return { Coordinates[0], Coordinates[1], Coordinates[2] };
}

std::array<double, 3> Conjugated(const std::array<double, 4>& Q, const std::array<double, 3>& Vector)
{
    return RotatePhysicsVector({ -Q[0], -Q[1], -Q[2], Q[3] }, Vector);
}

// 📝 Turns a record's collision surface into the descriptor's shape. A fixed collider keeps the world-space triangles as they
//    are (the body sits at the origin); a moving body takes the corners back into its own frame as a convex hull, so the solver
//    carries it about its centre. Returns false when the surface has no triangles (the caller keeps its bounds box).
bool ApplyCollisionSurface(PhysicsBodyDescriptor& Body, const CollisionSurface& Surface, bool Moving)
{
    if (Surface.Triangles.empty() || Surface.Corners.empty()) return false;
    if (!Moving)
    {
        Body.Shape          = PhysicsShape::Triangles;
        Body.Points         = Surface.Corners;
        Body.Triangles      = Surface.Triangles;
        Body.PositionMetres = { 0.0, 0.0, 0.0 };
        Body.Orientation    = { 0.0, 0.0, 0.0, 1.0 };
        return true;
    }
    const PlacementMatrix Inverse = InvertPlacement(Surface.Placement);
    Body.Shape     = PhysicsShape::Hull;
    Body.Triangles.clear();
    Body.Points.clear();
    Body.Points.reserve(Surface.Corners.size());
    for (const auto& Corner : Surface.Corners)
    {
        const std::array<double, 3> World{ Corner[0], Corner[1], Corner[2] };
        const std::array<double, 3> Local = GlobalIllumination::TransformPoint(Inverse, World);
        Body.Points.push_back({ static_cast<float>(Local[0]), static_cast<float>(Local[1]), static_cast<float>(Local[2]) });
    }
    return true;
}

} // namespace

PhysicsExtension::PhysicsExtension() = default;

PhysicsExtension::~PhysicsExtension()
{
    Thread.Stop();
}

size_t PhysicsExtension::Begin(WorldSpace& World, const PhysicsSpaceSettings& Settings)
{
    PhysicsExtensionSettings Inline;
    Inline.Space = Settings;
    return Begin(World, Inline);
}

size_t PhysicsExtension::Begin(WorldSpace& World, const PhysicsExtensionSettings& Settings)
{
    if (Space) End(World);
    Space = std::make_unique<PhysicsSpace>(Settings.Space);
    Links.clear();
    Vehicles.clear();
    Tally        = {};
    Remainder    = 0.0;
    StepCount    = 0;
    Pending      = {};
    Applied      = {};
    AppliedReset = 0;
    Latest       = {};
    Register.Survey(World);
    const CollisionSurfaceArchive* Surfaces = Settings.CollisionSurfaces;

    // ① the ground: a 4 km slab whose top is z = 0
    PhysicsBodyDescriptor Ground;
    Ground.Shape            = PhysicsShape::Box;
    Ground.HalfExtentMetres = { 2000.0, 2000.0, 1.0 };
    Ground.PositionMetres   = { 0.0, 0.0, -1.0 };
    Ground.Motion           = PhysicsMotion::Fixed;
    Ground.Friction         = 0.8;
    if (Space->AddBody(Ground) != PhysicsNoBody) ++Tally.Fixed;

    // ② Geometry records: Physics on → a moving box; fracturable or a single shown copy → a fixed box over its world bounds
    for (WorldRegisterEntry& Entry : Register.AccessEntries())
    {
        auto* Record = dynamic_cast<GeometryRecord*>(World.FindRecord(Entry.Token));
        if (Record == nullptr) continue;
        GeometryConfiguration&                   Geometry = Record->AccessConfiguration();
        const GeometryConfiguration::Resolution& Resolved = Geometry.Resolved;
        const std::array<double, 3> Minimum = ToDouble(Resolved.BoundsMinimum), Maximum = ToDouble(Resolved.BoundsMaximum);
        const std::array<double, 3> World3  = { Maximum[0] - Minimum[0], Maximum[1] - Minimum[1], Maximum[2] - Minimum[2] };
        const bool                  Bounded = World3[0] > 1.0e-3 && World3[1] > 1.0e-3 && World3[2] > 1.0e-3;
        if (Resolved.Shown > 1)
        {
            Entry.Physics = PhysicsParticipation::Held;
            Entry.Remark  = std::to_string(Resolved.Shown) + " copies share one transform, so they keep their place";
            ++Tally.Held;
            continue;
        }
        if (Geometry.Fracture.Enabled)
        {
            // 📝 No fracture yet: a fracturable object is a fixed collider over its bounds, whatever its Physics switch says.
            if (Geometry.Visible && Bounded)
            {
                PhysicsBodyDescriptor Collider;
                Collider.Record           = Record->QueryToken().Value;
                Collider.HalfExtentMetres = { 0.5 * World3[0], 0.5 * World3[1], 0.5 * World3[2] };
                Collider.PositionMetres   = { 0.5 * (Minimum[0] + Maximum[0]), 0.5 * (Minimum[1] + Maximum[1]), 0.5 * (Minimum[2] + Maximum[2]) };
                Collider.Motion           = PhysicsMotion::Fixed;
                Entry.Body                = Space->AddBody(Collider);
                if (Entry.Body != PhysicsNoBody)
                {
                    Entry.Physics = PhysicsParticipation::Fracture;
                    Entry.Remark  = "fracture deferred: a fixed collider over its bounds";
                    ++Tally.Fracture;
                    continue;
                }
            }
            Entry.Physics = PhysicsParticipation::Skipped;
            ++Tally.Skipped;
            continue;
        }
        if (Geometry.Physics)
        {
            const SpatialConfiguration& Spatial = Geometry.Spatial;
            std::array<double, 3> Size = { Resolved.Extent[0] * std::abs(Spatial.Scale[0]), Resolved.Extent[1] * std::abs(Spatial.Scale[1]),
                                           Resolved.Extent[2] * std::abs(Spatial.Scale[2]) };
            if (!(Size[0] > 1.0e-3 && Size[1] > 1.0e-3 && Size[2] > 1.0e-3))
            {
                // 📝 Not resolved against the scene yet (no extent): its world bounds, or nothing to simulate.
                if (!Bounded)
                {
                    Entry.Physics = PhysicsParticipation::Skipped;
                    ++Tally.Skipped;
                    continue;
                }
                Size = World3;
            }
            const std::array<double, 3> Origin      = ToDouble(Spatial.PositionMetres);
            const std::array<double, 4> Orientation = ResolvePhysicsOrientation(ToDouble(Spatial.OrientationDegrees));
            // 📝 The world bounds' centre is the shape's centre: its offset from the origin, in the object's own axes.
            const std::array<double, 3> Centre = Bounded ? std::array<double, 3>{ 0.5 * (Minimum[0] + Maximum[0]), 0.5 * (Minimum[1] + Maximum[1]),
                                                                                  0.5 * (Minimum[2] + Maximum[2]) }
                                                         : Origin;
            const std::array<double, 3> Offset = Conjugated(Orientation, { Centre[0] - Origin[0], Centre[1] - Origin[1], Centre[2] - Origin[2] });
            PhysicsBodyDescriptor Body;
            Body.Record           = Record->QueryToken().Value;
            Body.Shape            = PhysicsShape::Box;
            Body.HalfExtentMetres = { 0.5 * Size[0], 0.5 * Size[1], 0.5 * Size[2] };
            Body.PositionMetres   = Centre;
            Body.Orientation      = Orientation;
            Body.Motion           = PhysicsMotion::Moving;
            // 📝 The drawn surface, when the scene resolved one: a convex hull of its corners in the record's own frame, so the
            //    crate collides as its shape rather than its bounds box. The bounds-centre position and offset are kept.
            if (Surfaces != nullptr)
                if (const auto Found = Surfaces->find(Record->QueryToken().Value); Found != Surfaces->end())
                    (void)ApplyCollisionSurface(Body, Found->second, true);
            const uint32_t Index  = Space->AddBody(Body);
            if (Index == PhysicsNoBody)
            {
                Entry.Physics = PhysicsParticipation::Skipped;
                ++Tally.Skipped;
                continue;
            }
            Links.push_back({ Index, Record->QueryToken(), true, Offset, Spatial, Geometry.Dynamic });
            Geometry.Dynamic = true;   // 📝 its cards relight every refresh while it moves
            Entry.Physics    = PhysicsParticipation::Moving;
            Entry.Body       = Index;
            ++Tally.Moving;
        }
        else if (Geometry.Visible && Resolved.Shown == 1 && Bounded)
        {
            PhysicsBodyDescriptor Collider;
            Collider.Record           = Record->QueryToken().Value;
            Collider.HalfExtentMetres = { 0.5 * World3[0], 0.5 * World3[1], 0.5 * World3[2] };
            Collider.PositionMetres   = { 0.5 * (Minimum[0] + Maximum[0]), 0.5 * (Minimum[1] + Maximum[1]), 0.5 * (Minimum[2] + Maximum[2]) };
            Collider.Motion           = PhysicsMotion::Fixed;
            // 📝 The drawn surface, when the scene resolved one: its world-space triangles, so the jump ramp is a driveable slope
            //    rather than the box its bounds would make. Without a surface the bounds box stands.
            if (Surfaces != nullptr)
                if (const auto Found = Surfaces->find(Record->QueryToken().Value); Found != Surfaces->end())
                    (void)ApplyCollisionSurface(Collider, Found->second, false);
            Entry.Body                = Space->AddBody(Collider);
            if (Entry.Body != PhysicsNoBody)
            {
                Entry.Physics = PhysicsParticipation::Fixed;
                ++Tally.Fixed;
            }
            else
            {
                Entry.Physics = PhysicsParticipation::Skipped;
                ++Tally.Skipped;
            }
        }
        else
        {
            Entry.Physics = PhysicsParticipation::Skipped;
            Entry.Remark  = Geometry.Visible ? "not resolved against the scene (no bounds)" : "hidden";
            ++Tally.Skipped;
        }
    }

    // ③ RigidBody records: spheres of their volume
    for (WorldRegisterEntry& Entry : Register.AccessEntries())
    {
        auto* Record = dynamic_cast<RigidBodyRecord*>(World.FindRecord(Entry.Token));
        if (Record == nullptr) continue;
        if (!Record->QueryPhysics().Enabled)
        {
            Entry.Physics = PhysicsParticipation::Skipped;
            ++Tally.Skipped;
            continue;
        }
        const PhysicsConfiguration& Physics = Record->QueryPhysics();
        const double Volume = Physics.Mass.MassKilograms / std::max(1.0e-3f, Physics.Mass.DensityKgPerCubicMetre);
        PhysicsBodyDescriptor Body;
        Body.Record              = Record->QueryToken().Value;
        Body.Shape               = PhysicsShape::Sphere;
        Body.HalfExtentMetres[0] = std::clamp(std::cbrt(3.0 * Volume / (4.0 * 3.14159265358979323846)), 0.05, 50.0);
        Body.PositionMetres      = ToDouble(Record->QuerySpatial().PositionMetres);
        Body.Orientation         = ResolvePhysicsOrientation(ToDouble(Record->QuerySpatial().OrientationDegrees));
        Body.Motion              = Physics.DynamicBody ? PhysicsMotion::Moving : PhysicsMotion::Fixed;
        Body.MassKilograms       = Physics.Mass.MassKilograms;
        Body.Friction            = Physics.DynamicFriction;
        Body.Restitution         = Physics.Restitution;
        Body.LinearDamping       = Physics.LinearDamping;
        Body.LinearVelocityMps   = ToDouble(Physics.LinearVelocityMps);
        Body.AngularVelocityRads = ToDouble(Physics.AngularVelocityRadSec);
        const uint32_t Index     = Space->AddBody(Body);
        if (Index == PhysicsNoBody)
        {
            Entry.Physics = PhysicsParticipation::Skipped;
            ++Tally.Skipped;
            continue;
        }
        Entry.Body = Index;
        if (Body.Motion == PhysicsMotion::Moving)
        {
            Links.push_back({ Index, Record->QueryToken(), false, {}, Record->QuerySpatial(), false });
            Entry.Physics = PhysicsParticipation::Moving;
            ++Tally.Moving;
        }
        else
        {
            Entry.Physics = PhysicsParticipation::Fixed;
            ++Tally.Fixed;
        }
    }

    // ④ Vehicles (the fixed colliders are in, so a chassis finds its road)
    for (WorldRegisterEntry& Entry : Register.AccessEntries())
        if (dynamic_cast<VehicleRecord*>(World.FindRecord(Entry.Token)) != nullptr) AddVehicle(World, Entry.Token, &Entry);
    Space->Prepare();

    // ⑤ the register document beside the level
    if (Settings.WriteRegister && !World.QueryLoadedLevelPath().empty())
        (void)Register.WriteMarkdown(WorldRegister::ResolveDocumentPath(World.QueryLoadedLevelPath()));

    // ⑥ the step rate, substepping, and the physics thread when asked for
    double AuthoredVehicleHz = 0.0;
    int    AuthoredCollision = Settings.CollisionSteps;
    int    AuthoredSubsteps  = Settings.SolverSubsteps;
    for (const VehicleLink& Link : Vehicles)
    {
        if (const auto* Record = dynamic_cast<const VehicleRecord*>(World.FindRecord(Link.Record)))
        {
            const VehicleConfiguration& Configuration = Record->QueryConfiguration();
            AuthoredVehicleHz = std::max(AuthoredVehicleHz, static_cast<double>(Configuration.PhysicsStepHz));
            AuthoredCollision = std::max(AuthoredCollision, Configuration.CollisionSteps);
            AuthoredSubsteps  = std::max(AuthoredSubsteps, Configuration.SolverSubsteps);
        }
    }
    const double Rate = Settings.StepHz > 0.0 ? Settings.StepHz : (Vehicles.empty() ? 60.0 : (AuthoredVehicleHz > 0.0 ? AuthoredVehicleHz : 240.0));
    StepSeconds       = 1.0 / Rate;
    CollisionSteps    = std::clamp(AuthoredCollision, 1, 8);
    SolverSubsteps    = std::clamp(AuthoredSubsteps, 1, 8);
    Tally.StepHz      = Rate;
    Threaded          = Settings.Threaded;
    Tally.Threaded    = Threaded;
    FillSnapshot(Latest);
    (void)ApplySnapshot(World, Latest);
    if (Threaded)
    {
        PhysicsThreadConfiguration Configuration;
        Configuration.StepHz              = Rate;
        Configuration.MaximumCatchUpSteps = Settings.MaximumCatchUpSteps;
        Threaded = Thread.Start(
            Configuration, [this](uint64_t, double Seconds) { StepOnce(Seconds); },
            [this](uint64_t)
            {
                FillSnapshot(Snapshots.AccessProduced());
                Snapshots.Publish();
            });
        Tally.Threaded = Threaded;
    }
    Portals.Configure(World);
    for (Link& Link : Links) Link.PreviousValid = false;
    return Tally.Moving + Tally.Vehicles;
}

void PhysicsExtension::AddVehicle(WorldSpace& World, RecordToken Token, WorldRegisterEntry* Entry)
{
    auto* Record = dynamic_cast<VehicleRecord*>(World.FindRecord(Token));
    if (Record == nullptr) return;
    VehicleConfiguration& Configuration = Record->AccessConfiguration();
    if (!Configuration.Physics || !Configuration.Visible)
    {
        if (Entry)
        {
            Entry->Physics = PhysicsParticipation::Skipped;
            Entry->Remark  = Configuration.Visible ? "Physics off" : "hidden";
        }
        ++Tally.Skipped;
        return;
    }
    Vehicle::VehicleSolverConfiguration Solver = Vehicle::ResolveSolverConfiguration(Configuration.Specification);
    // 📝 The tyres it encloses set their corners: cold pressure, compound grip, and the compounds' window for the thermal grip.
    std::vector<RecordToken> Tyres;
    std::array<bool, 4>      CornerFitted{ true, true, true, true };
    Solver.CornerPressures.assign(4, Solver.Tyre.InflationPressure);
    Solver.CornerGrip.assign(4, 1.0f);
    double WindowLow = 0.0, WindowHigh = 0.0;
    int    Fitted    = 0;
    for (const SceneRecordBase* Candidate : World.FindAllByType("Tyre"))
    {
        const auto* Tyre = dynamic_cast<const TyreRecord*>(Candidate);
        if (Tyre == nullptr || Candidate->QueryParentToken() != Token) continue;
        const TyreConfiguration& Fit = Tyre->QueryConfiguration();
        const uint32_t           CornerIndex = Fit.Corner & 3u;
        Tyres.push_back(Candidate->QueryToken());
        if (WorldRegisterEntry* TyreEntry = Register.Find(Candidate->QueryToken()))
            TyreEntry->Physics = Fit.Enclosed ? PhysicsParticipation::Tyre : PhysicsParticipation::Skipped;
        if (!Fit.Enclosed)
        {
            CornerFitted[CornerIndex] = false;
            continue;
        }
        CornerFitted[CornerIndex] = true;
        const TyreCompound& Compound = ResolveTyreCompound(Fit.Compound);
        Solver.CornerPressures[CornerIndex] = Fit.ColdPressure;
        Solver.CornerGrip[CornerIndex]      = Compound.GripScale;
        WindowLow += Compound.WindowLow_C + 273.15;
        WindowHigh += Compound.WindowHigh_C + 273.15;
        ++Fitted;
    }
    if (Fitted > 0)
    {
        Solver.TyreThermal.ColdWindowTemperature = WindowLow / Fitted;
        Solver.TyreThermal.OptimumTemperature    = WindowHigh / Fitted;
        Solver.TyreThermal.GripFadeTemperature   = std::max(Solver.TyreThermal.GripFadeTemperature, Solver.TyreThermal.OptimumTemperature + 40.0);
    }
    VehicleLink Link;
    Link.Physics        = std::make_unique<PhysicsVehicle>();
    Link.Record         = Token;
    Link.Tyres          = Tyres;
    Link.Saved          = Configuration.Spatial;
    Link.Possessed      = Configuration.Possessed;
    Link.CentreHeight   = Configuration.Specification.Geometry.CoMHeight;
    Link.Carcass        = Solver.Carcass;
    Link.CornerFitted   = CornerFitted;
    Link.SolverSubsteps = std::max(1, Configuration.SolverSubsteps);
    std::string Error;
    if (!Link.Physics->Construct(*Space, Token, Configuration.Specification, Solver, ToDouble(Configuration.Spatial.PositionMetres),
                                 Configuration.Spatial.OrientationDegrees[2], &Error))
    {
        if (Entry)
        {
            Entry->Physics = PhysicsParticipation::Skipped;
            Entry->Remark  = Error;
        }
        ++Tally.Skipped;
        return;
    }
    if (Entry)
    {
        Entry->Physics = PhysicsParticipation::Vehicle;
        Entry->Body    = Link.Physics->QueryBody();
        Entry->Remark  = std::string("Project Drive on a Jolt chassis, carcass ") + Vehicle::TyreCarcassName(Solver.Carcass) + ", " +
                         std::to_string(Fitted) + " tyre records fitted";
    }
    Vehicles.push_back(std::move(Link));
    ++Tally.Vehicles;
}

void PhysicsExtension::AssignVehicleInput(const PhysicsVehicleInput& Input)
{
    Pending.Input = Input;
}

void PhysicsExtension::RequestVehicleReset()
{
    ++Pending.ResetSerial;
}

// 📝 One fixed step, on the physics thread or inline: the controls, each vehicle's forces, then the space.
void PhysicsExtension::StepOnce(double Seconds)
{
    // 📝 Threaded, the controls arrive only through the mailbox (the caller's Pending belongs to its own thread).
    if (Threaded)
    {
        VehicleCommand Incoming;
        if (Commands.Peek(Incoming)) Applied = Incoming;
    }
    else
    {
        Applied = Pending;
    }
    const VehicleCommand& Command = Applied;
    const bool Reset = Command.ResetSerial != AppliedReset;
    AppliedReset     = Command.ResetSerial;
    if (Reset)
    {
        for (VehicleLink& Link : Vehicles) Link.Physics->Reset();
    }
    const int    Substeps       = std::max(1, SolverSubsteps);
    const double SubstepSeconds = Seconds / static_cast<double>(Substeps);
    for (int Substep = 0; Substep < Substeps; ++Substep)
    {
        for (VehicleLink& Link : Vehicles)
        {
            Link.Physics->AssignInput(Link.Possessed ? Command.Input : PhysicsVehicleInput{});
            Link.Physics->Step(SubstepSeconds);
        }
        Space->Step(SubstepSeconds, CollisionSteps);
    }
    ++StepCount;
}

void PhysicsExtension::FillSnapshot(PhysicsSnapshot& Snapshot) const
{
    Snapshot.Steps            = StepCount;
    Snapshot.SimulatedSeconds = Space ? Space->QuerySimulatedSeconds() : 0.0;
    Snapshot.Bodies.resize(Links.size());
    for (size_t Index = 0; Index < Links.size(); ++Index) Snapshot.Bodies[Index] = Space->ReadBody(Links[Index].Body);
    Snapshot.Vehicles.resize(Vehicles.size());
    for (size_t Index = 0; Index < Vehicles.size(); ++Index) Vehicles[Index].Physics->ReadView(Snapshot.Vehicles[Index]);
}

void PhysicsExtension::AdvanceSteps(uint32_t Count)
{
    if (!Space || Threaded) return;
    for (uint32_t Step = 0; Step < Count; ++Step) StepOnce(StepSeconds);
    FillSnapshot(Latest);
}

size_t PhysicsExtension::Step(WorldSpace& World, double DeltaSeconds)
{
    if (!Space) return 0;
    const auto Start = std::chrono::steady_clock::now();
    if (Threaded)
    {
        Commands.Write(Pending);
        if (Snapshots.Acquire()) Latest = Snapshots.QueryConsumed();
    }
    else
    {
        Remainder += std::clamp(DeltaSeconds, 0.0, 0.25);
        bool Stepped = false;
        while (Remainder >= StepSeconds)
        {
            StepOnce(StepSeconds);
            Remainder -= StepSeconds;
            Stepped = true;
        }
        if (Stepped) FillSnapshot(Latest);
    }
    const size_t Written = ApplySnapshot(World, Latest);
    Tally.StepMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    return Written;
}

size_t PhysicsExtension::ApplySnapshot(WorldSpace& World, const PhysicsSnapshot& Snapshot)
{
    size_t Written = 0;
    for (size_t Index = 0; Index < Links.size() && Index < Snapshot.Bodies.size(); ++Index)
    {
        const Link&               Moved   = Links[Index];
        const PhysicsBodyReading& Reading = Snapshot.Bodies[Index];
        const std::array<double, 3> Turned = RotatePhysicsVector(Reading.Orientation, Moved.CentreOffset);
        const std::array<double, 3> Euler  = ResolvePhysicsEulerDegrees(Reading.Orientation);
        SpatialConfiguration*       Spatial  = nullptr;
        RevisionCounter*            Revision = nullptr;
        if (Moved.Geometry)
        {
            if (auto* Record = dynamic_cast<GeometryRecord*>(World.FindRecord(Moved.Record)))
            {
                Spatial  = &Record->AccessConfiguration().Spatial;
                Revision = &Record->AccessConfiguration();
            }
        }
        else if (auto* Record = dynamic_cast<RigidBodyRecord*>(World.FindRecord(Moved.Record)))
        {
            Spatial  = &Record->AccessSpatial();
            Revision = &Record->AccessSpatial();
            for (size_t Axis = 0; Axis < 3; ++Axis) Record->AccessPhysics().LinearVelocityMps[Axis] = static_cast<float>(Reading.LinearVelocityMps[Axis]);
        }
        if (Spatial == nullptr) continue;
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            Spatial->PositionMetres[Axis]     = static_cast<float>(Reading.PositionMetres[Axis] - Turned[Axis]);
            Spatial->OrientationDegrees[Axis] = static_cast<float>(Euler[Axis]);
        }
        Revision->AdvanceRevision();
        ++Written;
    }
    // 📝 Vehicles: the record follows the car (the ground point under its centre of mass and its heading); the live reading goes
    //    to the inspectors and the telemetry dock, under the vehicle and each tyre it encloses.
    const PhysicsThreadMeasurements Measured = Threaded ? Thread.QueryMeasurements() : PhysicsThreadMeasurements{};
    for (size_t Index = 0; Index < Vehicles.size() && Index < Snapshot.Vehicles.size(); ++Index)
    {
        const VehicleLink&        Link = Vehicles[Index];
        const PhysicsVehicleView& View = Snapshot.Vehicles[Index];
        if (auto* Record = dynamic_cast<VehicleRecord*>(World.FindRecord(Link.Record)))
        {
            const Vehicle::ChassisKinematics& Chassis = View.Telemetry.Chassis;
            const Vehicle::Vec3 Foot = Chassis.Position - Chassis.Orientation.Rotate({ 0.0f, 0.0f, static_cast<float>(Link.CentreHeight) });
            const std::array<double, 3> Euler = ResolvePhysicsEulerDegrees({ Chassis.Orientation.x, Chassis.Orientation.y, Chassis.Orientation.z, Chassis.Orientation.w });
            SpatialConfiguration& Spatial = Record->AccessConfiguration().Spatial;
            Spatial.PositionMetres[0] = Foot.x;
            Spatial.PositionMetres[1] = Foot.y;
            Spatial.PositionMetres[2] = Foot.z;
            for (size_t Axis = 0; Axis < 3; ++Axis) Spatial.OrientationDegrees[Axis] = static_cast<float>(Euler[Axis]);
            Spatial.AdvanceRevision();
            ++Written;
        }
        VehicleReading Reading;
        Reading.Simulating          = true;
        Reading.Telemetry           = View.Telemetry;
        Reading.SimulatedSeconds    = Snapshot.SimulatedSeconds;
        Reading.Steps               = Snapshot.Steps;
        Reading.StepHz              = 1.0 / StepSeconds;
        Reading.StepMicroseconds    = Measured.MeanStepMicroseconds;
        Reading.RealtimeRatio       = Threaded ? Measured.RealtimeRatio : 1.0;
        Reading.Throttle            = { View.Input.Throttle, View.Input.Brake, View.Input.Steer, View.Input.Handbrake ? 1.0f : 0.0f };
        Reading.Carcass             = Link.Carcass;
        Reading.VisualValid         = true;
        Reading.BodyTransform       = View.Body;
        Reading.WheelTransforms     = View.Wheels;
        Reading.CornerFields        = View.Fields;
        Reading.CornerFieldValid    = View.FieldValid;
        Reading.CornerFitted        = Link.CornerFitted;
        Reading.ContactAngle        = View.ContactAngle;
        Reading.Deflection          = View.Deflection;
        Reading.GaugePressure       = View.GaugePressure;
        Reading.LateralShift        = View.LateralShift;
        Reading.SteeringColumnAngle = -View.Input.Steer * 2.3561945f;
        PublishVehicleReading(Link.Record, Reading);
        for (const RecordToken& Tyre : Link.Tyres) PublishVehicleReading(Tyre, Reading);
    }
    return Written;
}

void PhysicsExtension::End(WorldSpace& World)
{
    Thread.Stop();
    for (const Link& Moved : Links)
    {
        if (Moved.Geometry)
        {
            if (auto* Record = dynamic_cast<GeometryRecord*>(World.FindRecord(Moved.Record)))
            {
                GeometryConfiguration& Geometry = Record->AccessConfiguration();
                for (size_t Axis = 0; Axis < 3; ++Axis)
                {
                    Geometry.Spatial.PositionMetres[Axis]     = Moved.Saved.PositionMetres[Axis];
                    Geometry.Spatial.OrientationDegrees[Axis] = Moved.Saved.OrientationDegrees[Axis];
                    Geometry.Spatial.Scale[Axis]              = Moved.Saved.Scale[Axis];
                }
                Geometry.Dynamic = Moved.SavedDynamic;
                Geometry.AdvanceRevision();
            }
        }
        else if (auto* Record = dynamic_cast<RigidBodyRecord*>(World.FindRecord(Moved.Record)))
        {
            SpatialConfiguration& Spatial = Record->AccessSpatial();
            for (size_t Axis = 0; Axis < 3; ++Axis)
            {
                Spatial.PositionMetres[Axis]     = Moved.Saved.PositionMetres[Axis];
                Spatial.OrientationDegrees[Axis] = Moved.Saved.OrientationDegrees[Axis];
            }
            Spatial.AdvanceRevision();
        }
    }
    for (const VehicleLink& Link : Vehicles)
    {
        if (auto* Record = dynamic_cast<VehicleRecord*>(World.FindRecord(Link.Record)))
        {
            SpatialConfiguration& Spatial = Record->AccessConfiguration().Spatial;
            for (size_t Axis = 0; Axis < 3; ++Axis)
            {
                Spatial.PositionMetres[Axis]     = Link.Saved.PositionMetres[Axis];
                Spatial.OrientationDegrees[Axis] = Link.Saved.OrientationDegrees[Axis];
            }
            Spatial.AdvanceRevision();
        }
        VehicleReading Stopped;
        PublishVehicleReading(Link.Record, Stopped);
        for (const RecordToken& Tyre : Link.Tyres) PublishVehicleReading(Tyre, Stopped);
    }
    Links.clear();
    Vehicles.clear();
    Space.reset();
    Remainder = 0.0;
    Threaded  = false;
    Latest    = {};
}

size_t PhysicsExtension::IntegratePortals(WorldSpace& World)
{
    if (!Space || Threaded || Portals.QueryApertures().empty()) return 0;
    size_t Transits = 0;
    for (Link& Link : Links)
    {
        if (Link.Body == PhysicsNoBody) continue;
        const PhysicsBodyReading Reading = Space->ReadBody(Link.Body);
        if (!Link.PreviousValid)
        {
            Link.PreviousPosition = Reading.PositionMetres;
            Link.PreviousValid    = true;
            continue;
        }
        bool Teleported = false;
        for (const PortalAperture& Aperture : Portals.QueryApertures())
        {
            const PortalAperture* Peer = Portals.QueryPeer(Aperture);
            if (Peer == nullptr) continue;
            const PortalCrossing Crossing = Portals.DetectCrossing(Link.PreviousPosition, Reading.PositionMetres, Aperture);
            if (!Crossing.Crossed || !Crossing.WithinAperture) continue;
            PortalBodyPose Pose;
            Pose.Position        = Reading.PositionMetres;
            Pose.Orientation     = Reading.Orientation;
            Pose.LinearVelocity  = Reading.LinearVelocityMps;
            Pose.AngularVelocity = Reading.AngularVelocityRads;
            const PortalTransit Transit = Portals.ResolveTransit(Pose, Aperture, *Peer);
            PhysicsBodyReading Emerging;
            Emerging.PositionMetres      = Transit.Pose.Position;
            Emerging.Orientation         = Transit.Pose.Orientation;
            Emerging.LinearVelocityMps   = Transit.Pose.LinearVelocity;
            Emerging.AngularVelocityRads = Transit.Pose.AngularVelocity;
            Emerging.Awake               = true;
            Space->PlaceBody(Link.Body, Emerging);
            // 📝 The aperture ratio resizes the body: scale the record so the renderer draws it at the new size.
            if (SceneRecordBase* Record = World.FindRecord(Link.Record))
            {
                auto ScaleSpatial = [&Transit](SpatialConfiguration& Spatial)
                {
                    for (int Axis = 0; Axis < 3; ++Axis) Spatial.Scale[Axis] *= static_cast<float>(Transit.Scale);
                    Spatial.AdvanceRevision();
                };
                if (Link.Geometry)
                {
                    if (auto* Geometry = dynamic_cast<GeometryRecord*>(Record)) ScaleSpatial(Geometry->AccessConfiguration().Spatial);
                }
                else if (auto* Body = dynamic_cast<RigidBodyRecord*>(Record))
                {
                    ScaleSpatial(Body->AccessSpatial());
                }
            }
            Link.PreviousPosition = Transit.Pose.Position;
            Link.PreviousValid    = true;
            Teleported            = true;
            ++Transits;
            break;
        }
        if (!Teleported)
        {
            Link.PreviousPosition = Reading.PositionMetres;
            Link.PreviousValid    = true;
        }
    }
    return Transits;
}

} // namespace Frontier
