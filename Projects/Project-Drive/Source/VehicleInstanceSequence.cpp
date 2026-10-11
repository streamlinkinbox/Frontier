//============================================================================================================================================
// 📦 Projects/Project-Drive/Source/VehicleInstanceSequence.cpp — engine⇄project seam: VehicleSolver → renderer instances
//============================================================================================================================================
#include "VehicleInstanceSequence.h"
#include "DriveCourse.h"
#include "../../../Engine/PhysicalDynamics/Vehicle/VehicleSpawnSolver.h"
#include <cstdio>

#include <algorithm>
#include <cmath>

namespace Frontier {
namespace Drive {

using Frontier::Vehicle::Vec3;
using Frontier::Vehicle::Quat;
using Frontier::Vehicle::QuatMul;
using Frontier::Vehicle::QuatNormalize;
using Frontier::Vehicle::VehicleSolver;

//------------------------------------------------------------------------------------------------------------------------ chassis body (identical integrator to DriveTelemetry / FieldDemo)
Quat VehicleChassisBody::Conjugate() const noexcept
{ return {-Orientation.x, -Orientation.y, -Orientation.z, Orientation.w}; }

Vec3 VehicleChassisBody::WorldAngularAccel(const Vec3& torqueWorld) const noexcept
{
    const Vec3 tb = Conjugate().Rotate(torqueWorld);
    const Vec3 ab{tb.x*InvInertiaDiag.x, tb.y*InvInertiaDiag.y, tb.z*InvInertiaDiag.z};
    return Orientation.Rotate(ab);
}
void VehicleChassisBody::ApplyForceAtPoint(const Vec3& f, const Vec3& p) noexcept
{ ForceAccum += f; TorqueAccum += Frontier::Vehicle::Cross(p - Position, f); }
void VehicleChassisBody::ApplyTorque(const Vec3& t) noexcept { TorqueAccum += t; }

void VehicleChassisBody::Integrate(float dt) noexcept
{
    ForceAccum += Gravity*Mass;
    LinearVelocity += ForceAccum*(dt/Mass);
    if (LinearDamping > 0.0f) LinearVelocity = LinearVelocity*std::exp(-LinearDamping*dt);
    Position += LinearVelocity*dt;
    AngularVelocity += WorldAngularAccel(TorqueAccum)*dt;
    AngularVelocity = AngularVelocity*std::exp(-AngularDamping*dt);
    const Quat wq{AngularVelocity.x, AngularVelocity.y, AngularVelocity.z, 0.0f};
    const Quat dq = QuatMul(wq, Orientation);
    Orientation = QuatNormalize(Quat{Orientation.x + 0.5f*dt*dq.x, Orientation.y + 0.5f*dt*dq.y,
                                     Orientation.z + 0.5f*dt*dq.z, Orientation.w + 0.5f*dt*dq.w});
    ForceAccum = Vec3{}; TorqueAccum = Vec3{};
}
Frontier::Vehicle::ChassisState VehicleChassisBody::State() const noexcept
{ return {Position, Orientation, LinearVelocity, AngularVelocity}; }

//------------------------------------------------------------------------------------------------------------------------ construction
bool VehicleInstanceSequence::Construct(const Frontier::Vehicle::VehicleGeometry& Geometry,
                                        const VehicleInstanceConfiguration& Configuration,
                                        Frontier::Vehicle::XPBDSoftTyre::GroundQuery Ground) noexcept
{
    Built = false;
    Instancing = Configuration;
    ActiveConfiguration = Frontier::Vehicle::VehicleSolverConfiguration{};
    ActiveConfiguration.ActiveScheme = Frontier::Vehicle::DrivingScheme::PacejkaDrivetrain;
    Frontier::Vehicle::ApplyGeometry(ActiveConfiguration, Geometry);
    ActiveConfiguration.Aero.Enabled = true;

    SpawnPosition = Instancing.SpawnLocation;
    SpawnPosition.z = Instancing.SpawnHeight;
    SpawnOrientation = Instancing.SpawnRotation;
    Body = VehicleChassisBody{};
    Body.Position       = SpawnPosition;
    Body.Orientation    = SpawnOrientation;
    Body.Mass           = ActiveConfiguration.ChassisMass;
    Body.InvInertiaDiag = Geometry.InvInertia();

    VehicleSolver::Hooks h;
    h.ReadChassis       = [this]{ return Body.State(); };
    h.ApplyForceAtPoint = [this](const Vec3& f, const Vec3& p){ Body.ApplyForceAtPoint(f, p); };
    h.ApplyTorque       = [this](const Vec3& t){ Body.ApplyTorque(t); };
    GroundSurface = Ground ? std::move(Ground) : Frontier::Vehicle::XPBDSoftTyre::GroundQuery([](const Vec3& p, Vec3& s, Vec3& n)
    {
        // The surface query, not a height: CourseSurface answers with the nearest point ON the course and
        //    its outward normal, so a kerb's vertical face can stop a tyre sideways instead of lifting it.
        float Sx, Sy, Sz, Nx, Ny, Nz;
        CourseSurface(p.x, p.y, p.z, Sx, Sy, Sz, Nx, Ny, Nz);
        s = Vec3{Sx, Sy, Sz};
        n = Vec3{Nx, Ny, Nz};
        return true;
    });
    h.Ground = GroundSurface;
    if (Instancing.FitTerrain)
    {
        const auto Settled = Frontier::Vehicle::ResolveVehicleSpawn(ActiveConfiguration, Body.State(), GroundSurface);
        if (Settled.Supported)
        {
            SpawnPosition = Settled.Pose.Position;
            SpawnOrientation = Settled.Pose.Orientation;
            Body.Position = SpawnPosition;
            Body.Orientation = SpawnOrientation;
        }
        else { std::fputs("[Drive] Spawn terrain is incomplete; deployment refused.\n", stderr); return false; }
    }
    ActiveVehicleSolver.Build(ActiveConfiguration, h, Body.State());

    WheelSpin.assign(Instancing.WheelCount, 0.0f);
    ChassisState_ = Body.State();
    Accumulator = 0.0f;
    RestCaptured = false;
    Built = true;
    return true;
}

void VehicleInstanceSequence::Reconfigure(const Frontier::Vehicle::VehicleSolverConfiguration& Edited) noexcept
{
    ActiveConfiguration = Edited;
    // Rebuild so a curve / geometry / tyre edit rebinds the soft tyres at the current chassis pose.
    VehicleSolver::Hooks h;
    h.ReadChassis       = [this]{ return Body.State(); };
    h.ApplyForceAtPoint = [this](const Vec3& f, const Vec3& p){ Body.ApplyForceAtPoint(f, p); };
    h.ApplyTorque       = [this](const Vec3& t){ Body.ApplyTorque(t); };
    h.Ground = GroundSurface;
    ActiveVehicleSolver.Build(ActiveConfiguration, h, Body.State());
}

void VehicleInstanceSequence::ResetToSpawn() noexcept
{
    Accumulator          = 0.0f;
    Body.Position        = SpawnPosition;
    Body.Orientation     = SpawnOrientation;
    Body.LinearVelocity  = Vec3{};
    Body.AngularVelocity = Vec3{};
    Body.ForceAccum      = Vec3{};
    Body.TorqueAccum     = Vec3{};
    std::fill(WheelSpin.begin(), WheelSpin.end(), 0.0f);
    Reconfigure(ActiveConfiguration);          // rebinds the tyres at the spawn pose using the current config
    ChassisState_ = Body.State();
}

//------------------------------------------------------------------------------------------------------------------------ per-frame advance
void VehicleInstanceSequence::AdvanceVehicle(const Frontier::Vehicle::DriverInput& Input,
                                             std::vector<InstanceRecord>& Rows, float DeltaSeconds) noexcept
{
    if (!Built) return;

    ActiveVehicleSolver.AssignInput(Input);
    Accumulator += std::min(std::max(DeltaSeconds, 0.0f), Instancing.MaxFrameSeconds);
    const float h = Instancing.SubStepSeconds;
    const auto& tel = ActiveVehicleSolver.Telemetry();
    while (Accumulator >= h)
    {
        ActiveVehicleSolver.Step(h);   // reads chassis, steps tyres, applies wheel forces
        Body.Integrate(h);    // integrates the chassis with those forces (no body collision)
        for (uint32_t w = 0; w < Instancing.WheelCount && w < tel.Wheels.size(); ++w)
            WheelSpin[w] += tel.Wheels[w].WheelOmega * h;   // visual spin only
        Accumulator -= h;
    }
    ChassisState_ = Body.State();

    WriteBodyRow(Rows);
    WriteWheelRows(Rows);
}

//------------------------------------------------------------------------------------------------------------------------ matrix write
void VehicleInstanceSequence::ComposeTRS(const Vec3& T, const Quat& R, float Out[16]) noexcept
{
    const Vec3 cx = R.Rotate(Vec3{1,0,0});
    const Vec3 cy = R.Rotate(Vec3{0,1,0});
    const Vec3 cz = R.Rotate(Vec3{0,0,1});
    Out[0]=cx.x;  Out[1]=cx.y;  Out[2]=cx.z;  Out[3]=0.0f;   // column 0 (local X → world)
    Out[4]=cy.x;  Out[5]=cy.y;  Out[6]=cy.z;  Out[7]=0.0f;   // column 1 (local Y → world)
    Out[8]=cz.x;  Out[9]=cz.y;  Out[10]=cz.z; Out[11]=0.0f;  // column 2 (local Z → world)
    Out[12]=T.x;  Out[13]=T.y;  Out[14]=T.z;  Out[15]=1.0f;  // column 3 (translation)
}

void VehicleInstanceSequence::WriteBodyRow(std::vector<InstanceRecord>& Rows) noexcept
{
    if (Instancing.BodyInstance >= Rows.size()) return;
    InstanceRecord& R = Rows[Instancing.BodyInstance];
    std::copy(std::begin(R.World), std::end(R.World), std::begin(R.PreviousWorld));  // roll for motion vectors
    ComposeTRS(Body.Position, Body.Orientation, R.World);
}

void VehicleInstanceSequence::WriteWheelRows(std::vector<InstanceRecord>& Rows) noexcept
{
    const auto& tel = ActiveVehicleSolver.Telemetry();
    for (uint32_t w = 0; w < Instancing.WheelCount; ++w)
    {
        const uint32_t idx = Instancing.FirstWheel + w;
        if (idx >= Rows.size() || w >= tel.Wheels.size()) break;
        const auto& wt = tel.Wheels[w];

        // wheel = chassis · steer(about local Z) · spin(about local axle Y)   — axle authored along local Y
        const Quat steer = Quat::AxisAngle(Vec3{0,0,1}, wt.SteerAngleRad);
        const Quat spin  = Quat::AxisAngle(Vec3{0,1,0}, WheelSpin[w]);
        const Quat rot   = QuatMul(Body.Orientation, QuatMul(steer, spin));

        InstanceRecord& R = Rows[idx];
        std::copy(std::begin(R.World), std::end(R.World), std::begin(R.PreviousWorld));
        ComposeTRS(wt.HubPosition, rot, R.World);
    }
}

//------------------------------------------------------------------------------------------------------------------------ flat-triangle refit (mirrors PhysicsInstanceSequence::RefreshBodyFacets)
void VehicleInstanceSequence::RefreshBodyFacets(std::vector<TriangleIndex>& Facets,
                                                const std::vector<InstanceRecord>& Instances) noexcept
{
    const uint32_t bodyCount = 1u + Instancing.WheelCount;

    if (!RestCaptured)
    {
        FacetFirst.assign(bodyCount, 0u);
        FacetCount.assign(bodyCount, 0u);
        RestFacets.clear();
        auto capture = [&](uint32_t slot, uint32_t instance)
        {
            if (instance >= Instances.size()) return;
            const InstanceRecord& I = Instances[instance];
            FacetFirst[slot] = I.FlatTriangleOffset;
            FacetCount[slot] = I.TriangleCount;
            for (uint32_t t = 0; t < I.TriangleCount; ++t)
            {
                const uint32_t f = I.FlatTriangleOffset + t;
                if (f < Facets.size()) RestFacets.push_back(Facets[f]);   // baked object-local at rest
            }
        };
        capture(0u, Instancing.BodyInstance);
        for (uint32_t w = 0; w < Instancing.WheelCount; ++w) capture(1u + w, Instancing.FirstWheel + w);
        RestCaptured = true;
    }

    // Re-transform each body/wheel's captured local triangles by its current World and overwrite the flat list.
    auto retransform = [&](uint32_t slot, uint32_t instance)
    {
        if (instance >= Instances.size()) return;
        const float* M = Instances[instance].World;
        uint32_t src = 0;
        for (uint32_t s = 0; s < slot; ++s) src += FacetCount[s];
        for (uint32_t t = 0; t < FacetCount[slot]; ++t)
        {
            const uint32_t f = FacetFirst[slot] + t;
            if (f >= Facets.size() || (src + t) >= RestFacets.size()) break;
            const TriangleIndex& r = RestFacets[src + t];
            TriangleIndex out = r;
            auto xf = [&](float x, float y, float z, float& ox, float& oy, float& oz)
            {
                ox = M[0]*x + M[4]*y + M[8]*z  + M[12];
                oy = M[1]*x + M[5]*y + M[9]*z  + M[13];
                oz = M[2]*x + M[6]*y + M[10]*z + M[14];
            };
            xf(r.VertexAlphaX, r.VertexAlphaY, r.VertexAlphaZ, out.VertexAlphaX, out.VertexAlphaY, out.VertexAlphaZ);
            xf(r.VertexBetaX,  r.VertexBetaY,  r.VertexBetaZ,  out.VertexBetaX,  out.VertexBetaY,  out.VertexBetaZ);
            xf(r.VertexGammaX, r.VertexGammaY, r.VertexGammaZ, out.VertexGammaX, out.VertexGammaY, out.VertexGammaZ);
            Facets[f] = out;
        }
    };
    retransform(0u, Instancing.BodyInstance);
    for (uint32_t w = 0; w < Instancing.WheelCount; ++w) retransform(1u + w, Instancing.FirstWheel + w);
}

} // namespace Drive
} // namespace Frontier
