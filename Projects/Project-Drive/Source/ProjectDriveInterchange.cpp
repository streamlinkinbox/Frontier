//============================================================================================================================================
//                                               PROJECTDRIVEINTERCHANGE.CPP
//============================================================================================================================================
// 📦 Project-Drive's optional code image; vehicle semantics cross Frontier.exe only through C-layout records.

#include "../../../Engine/ProjectInterchange/ProjectInterchange.h"

#include "VehicleInstanceSequence.h"
#include "TyreSequence.h"
#include "../../../Engine/ProjectInterchange/GeometryInterchange.h"
#include "ChaseCameraSolver.h"
#include "../../../Engine/ProjectInterchange/DeploymentCodec.h"
#include <fstream>
#include <filesystem>
#include <memory>
#include "../../../Engine/PhysicalDynamics/Vehicle/DriverInputIntegrator.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <cstdio>
#include <cstring>

namespace
{

struct DriveSequence
{
    FrontierProjectHostInterchange Reception{};
    Frontier::Vehicle::VehicleGeometry Geometry;
    std::unique_ptr<Frontier::Drive::VehicleInstanceSequence> Vehicle;
    Frontier::DeploymentPoint DeploymentTemplate;
    Frontier::DeploymentSequence Deployments;
    std::optional<Frontier::DeploymentReading> ActivePlayer;
    uint64_t DeploymentIdentifier = 0u;
    uint64_t SpawnNumber = 0u;
    Frontier::Drive::ChaseCameraSolver Camera;
    Frontier::Vehicle::DriverInputIntegrator Input;
    std::vector<Frontier::InstanceRecord> Poses{5u};
    uint32_t PreviousTransport = 0u;
    bool PreviousReset = false;
    uint64_t GeometryRevision = 0u;
    std::array<Frontier::Drive::TyreSequence, 4> Surfaces;

    void CaptureSurfaces()
    {
        const auto& Tyres = Vehicle->Tyres();
        const auto& Wheels = Vehicle->Telemetry().Wheels;
        for (size_t Index = 0u; Index < Surfaces.size() && Index < Tyres.size(); ++Index)
        {
            const auto Up = Vehicle->Chassis().Orientation.Rotate({0.0f, 0.0f, 1.0f});
            const auto Steering = Frontier::Vehicle::Quat::AxisAngle(Up, Wheels[Index].SteerAngleRad);
            const auto HubRotation = Frontier::Vehicle::QuatNormalize(
                Frontier::Vehicle::QuatMul(Steering, Vehicle->Chassis().Orientation));
            Surfaces[Index].Capture(Tyres[Index], Wheels[Index].HubPosition, HubRotation);
        }
        ++GeometryRevision;
    }

    void RestoreDeployment()
    {
        Deployments.Retire(DeploymentIdentifier);
        DeploymentIdentifier = Deployments.Register(DeploymentTemplate);
        Vehicle.reset();
        ActivePlayer.reset();
        Input = Frontier::Vehicle::DriverInputIntegrator{};
    }

    bool DeployVehicle()
    {
        try
        {
            auto Spawned = Deployments.Deploy(DeploymentIdentifier, [&](const Frontier::DeploymentPoint& Point) -> uint64_t
            {
                if (Point.Archetype != "ControlVehicle") return 0u;
                auto Candidate = std::make_unique<Frontier::Drive::VehicleInstanceSequence>();
                Frontier::Drive::VehicleInstanceConfiguration Placement;
                Placement.SpawnLocation = {Point.Position[0], Point.Position[1], Point.Position[2]};
                Placement.SpawnHeight = Point.Position[2];
                Placement.SpawnRotation = {Point.Rotation[0], Point.Rotation[1], Point.Rotation[2], Point.Rotation[3]};
                if (!Candidate->Construct(Geometry, Placement)) return 0u;
                Vehicle = std::move(Candidate); // hooks retain their allocated object's address
                return ++SpawnNumber;
            });
            if (!Spawned) return false;
            ActivePlayer = std::move(Spawned); // player data outlives a consumed deployment actor
            Input = Frontier::Vehicle::DriverInputIntegrator{};
            return true;
        }
        catch (...) { return false; }
    }

    void DeliverPoses()
    {
        if (Reception.ReceiveSceneMutation == nullptr) return;
        static const char* Subjects[] = { "ControlVehicle — paint / glass / plastic", "XPBD Tyre FL", "XPBD Tyre FR", "XPBD Tyre RL", "XPBD Tyre RR" };
        for (uint32_t Slot = 0u; Slot < Poses.size(); ++Slot)
        {
            FrontierProjectSceneMutation Mutation{};
            Mutation.StructureSize = sizeof(Mutation);
            Mutation.MutationNumber = 2u;
            Mutation.SubjectName = Subjects[Slot];
            std::copy_n(Poses[Slot].World, 16u, Mutation.Transform);
            Frontier::Vehicle::Vec3 Origin{0.0f, 0.0f, Geometry.CoMHeight + 0.02f};
            if (Slot != 0u) Origin += Geometry.AxleMountLocal(Slot - 1u);
            // The opening glTF is already placed at rest. Send T(simulated) R T(-rest),
            // not an absolute pose applied a second time to that geometry.
            for (uint32_t Row = 0u; Row < 3u; ++Row)
                Mutation.Transform[12u + Row] -= Mutation.Transform[Row] * Origin.x
                                              + Mutation.Transform[4u + Row] * Origin.y
                                              + Mutation.Transform[8u + Row] * Origin.z;
            Reception.ReceiveSceneMutation(&Mutation, Reception.ProjectReception);
        }
    }

    void DeliverCamera(float Seconds, bool Snap)
    {
        const auto& Chassis = Vehicle->Chassis();
        const auto Forward = Chassis.Orientation.Rotate({1.0f, 0.0f, 0.0f});
        const Frontier::Vector3 Position{Chassis.Position.x, Chassis.Position.y, Chassis.Position.z};
        const Frontier::Vector3 Direction{Forward.x, Forward.y, Forward.z};
        if (Snap) Camera.SnapTo(Position, Direction);
        else Camera.AdvanceChase(Position, Direction, std::sqrt(Frontier::Vehicle::Dot(Chassis.LinearVelocity, Chassis.LinearVelocity)), Seconds);
        if (Reception.ReceiveCameraRequest == nullptr) return;
        FrontierProjectCameraRequest Request{};
        Request.StructureSize = sizeof(Request);
        const auto& Eye = Camera.QuerySpatialLocation();
        const auto& Sight = Camera.QueryForwardVector();
        Request.Eye[0] = Eye.x; Request.Eye[1] = Eye.y; Request.Eye[2] = Eye.z;
        Request.Forward[0] = Sight.x; Request.Forward[1] = Sight.y; Request.Forward[2] = Sight.z;
        Request.VerticalFieldOfView = Camera.QueryFieldOfViewRadians();
        Reception.ReceiveCameraRequest(&Request, Reception.ProjectReception);
    }
};

void WriteRefusal(FrontierProjectRefusal* Refusal, FrontierProjectRefusalNumber Number, const char* Explanation)
{
    if (Refusal == nullptr)
        return;

    Refusal->Number = static_cast<uint32_t>(Number);
    std::snprintf(Refusal->Explanation, sizeof(Refusal->Explanation), "%s", Explanation);
}

uint32_t FRONTIER_CODE_IMAGE_CALL ConstructProject(
    const FrontierProjectLaunch* ActiveLaunch,
    const FrontierProjectHostInterchange* HostInterchange,
    void** ProjectRecord,
    FrontierProjectRefusal* Refusal)
{
    if (ActiveLaunch == nullptr || ActiveLaunch->StructureSize < sizeof(FrontierProjectLaunch) ||
        HostInterchange == nullptr || HostInterchange->StructureSize < sizeof(FrontierProjectHostInterchange) ||
        ProjectRecord == nullptr)
    {
        WriteRefusal(Refusal, FrontierProjectRefusalStructure, "ProjectDrive received an incomplete Frontier host record");
        return 0u;
    }

    // These declarations let the shared Frontier editor own the UI/render/input implementation while Project-Drive
    // owns the vehicle semantics.  They are deliberately granular: selecting ControlVehicle reveals the inspector,
    // while each XPBD tyre is independently addressable in the project outliner instead of hiding all of the car in
    // a generic "Vehicle Dynamics" placeholder.
    if (HostInterchange->ReceivePanel != nullptr)
    {
        static const FrontierProjectPanel Panels[] =
        {
            { sizeof(FrontierProjectPanel), "DriveOutliner",       "Project-Drive Outliner" },
            { sizeof(FrontierProjectPanel), "ControlVehicle",     "ControlVehicle Inspector" },
            { sizeof(FrontierProjectPanel), "XPBDTyres",          "XPBD Tyres" },
            { sizeof(FrontierProjectPanel), "VehicleDynamics",    "Vehicle Dynamics" },
            { sizeof(FrontierProjectPanel), "DriveRenderModes",   "Surfel GI / ReSTIR" },
        };
        for (const FrontierProjectPanel& Panel : Panels)
            HostInterchange->ReceivePanel(&Panel, HostInterchange->ProjectReception);
    }

    if (HostInterchange->ReceiveDiagnostic != nullptr)
    {
        FrontierProjectDiagnostic Diagnostic{};
        Diagnostic.StructureSize = sizeof(FrontierProjectDiagnostic);
        Diagnostic.SeverityNumber = 0u;
        Diagnostic.SubjectName = "ControlVehicle";
        Diagnostic.Explanation = "Vehicle inspector declares chassis, Pacejka and XPBD tyre rows; project code stays C-ABI-only.";
        HostInterchange->ReceiveDiagnostic(&Diagnostic, HostInterchange->ProjectReception);
    }

    auto* Sequence = new (std::nothrow) DriveSequence;
    if (Sequence == nullptr)
    {
        WriteRefusal(Refusal, FrontierProjectRefusalConstruction, "ProjectDrive could not allocate its vehicle simulation");
        return 0u;
    }
    Sequence->Reception = *HostInterchange;
    Sequence->DeploymentTemplate.Position[2] = Sequence->Geometry.CoMHeight + 0.02f;
    if (ActiveLaunch->SpecificationLocation && ActiveLaunch->SpecificationLocation[0])
    {
        std::ifstream Stream(std::filesystem::u8path(ActiveLaunch->SpecificationLocation));
        std::string Explanation;
        if (!Stream || !Frontier::DecodeDeploymentPoint(Stream, Sequence->DeploymentTemplate, Explanation))
        {
            delete Sequence;
            WriteRefusal(Refusal, FrontierProjectRefusalConstruction, Explanation.empty() ? "Cannot read deployment specification" : Explanation.c_str());
            return 0u;
        }
    }
    Sequence->RestoreDeployment();
    *ProjectRecord = Sequence;
    return 1u;
}

uint32_t FRONTIER_CODE_IMAGE_CALL AdvanceProject(
    void* ProjectRecord,
    const FrontierProjectCycle* ActiveCycle,
    FrontierProjectRefusal* Refusal)
{
    if (ProjectRecord == nullptr || ActiveCycle == nullptr || ActiveCycle->StructureSize < sizeof(FrontierProjectCycle))
    {
        WriteRefusal(Refusal, FrontierProjectRefusalStructure, "ProjectDrive received an incomplete display-cycle record");
        return 0u;
    }

    auto& Sequence = *static_cast<DriveSequence*>(ProjectRecord);
    const auto* Reading = ActiveCycle->InputReading;
    if (Reading == nullptr || Reading->StructureSize < sizeof(FrontierProjectInputReading))
    {
        WriteRefusal(Refusal, FrontierProjectRefusalStructure, "ProjectDrive requires revision-3 input readings");
        return 0u;
    }
    const uint32_t Transport = Reading->TransportNumber;
    const bool Started = Transport != 0u && Sequence.PreviousTransport == 0u;
    const bool Reset = Reading->ResetPressed != 0u && !Sequence.PreviousReset && Reading->KeyboardCaptured == 0u;
    // Stop/new Play is a new editor session. Reset inside Play is a respawn, never a resurrection of a consumed point.
    if (Transport == 0u && Sequence.PreviousTransport != 0u) Sequence.RestoreDeployment();
    bool Deployed = false;
    if (Transport != 0u && (Started || Reset))
    {
        Deployed = Sequence.DeployVehicle();
        if (!Deployed && Sequence.Reception.ReceiveDiagnostic)
        {
            FrontierProjectDiagnostic Diagnostic{};
            Diagnostic.StructureSize = sizeof(Diagnostic);
            Diagnostic.SeverityNumber = 1u;
            Diagnostic.SubjectName = "DeploymentPoint";
            Diagnostic.Explanation = "Deployment refused: point disabled/consumed, unsupported archetype, or missing terrain support. Existing player preserved.";
            Sequence.Reception.ReceiveDiagnostic(&Diagnostic, Sequence.Reception.ProjectReception);
        }
    }
    Sequence.PreviousTransport = Transport;
    Sequence.PreviousReset = Reading->ResetPressed != 0u;
    if (Transport == 0u || !Sequence.Vehicle) return 1u;

    const float Seconds = Reading->Paused ? (Reading->SimulationStep ? 1.0f / 60.0f : 0.0f)
                                         : std::clamp(ActiveCycle->CycleSeconds, 0.0f, 0.1f);
    const bool Driving = Transport == 1u && Reading->KeyboardCaptured == 0u;
    Sequence.Input.ForwardThrottleKey(Driving && Reading->MoveAxisY > 0.0f);
    Sequence.Input.ForwardBrakeKey(Driving && Reading->MoveAxisY < 0.0f);
    Sequence.Input.ForwardSteerLeftKey(Driving && Reading->MoveAxisX < 0.0f);
    Sequence.Input.ForwardSteerRightKey(Driving && Reading->MoveAxisX > 0.0f);
    Sequence.Input.ForwardHandbrakeKey(Driving && Reading->HandbrakePressed != 0u);
    const auto Command = Sequence.Input.Advance(Seconds);
    if (Seconds > 0.0f || Deployed)
    {
        Sequence.Vehicle->AdvanceVehicle(Command.Drive, Sequence.Poses, Seconds);
        Sequence.CaptureSurfaces();
        Sequence.DeliverPoses();
    }
    if (Transport == 1u) Sequence.DeliverCamera(Seconds, Deployed);
    return 1u;
}

void FRONTIER_CODE_IMAGE_CALL RetireProject(void* ProjectRecord)
{
    delete static_cast<DriveSequence*>(ProjectRecord);
}

} // namespace

extern "C" FRONTIER_CODE_IMAGE_EXPORT uint32_t FRONTIER_CODE_IMAGE_CALL ConstructProjectInterchange(
    uint32_t RequestedInterchangeNumber,
    uint64_t RequestedFingerprint,
    FrontierProjectInterchange* DeliveredInterchange,
    FrontierProjectRefusal* Refusal)
{
    if (DeliveredInterchange == nullptr || RequestedInterchangeNumber != FrontierCodeInterchangeNumber ||
        RequestedFingerprint != FrontierCodeInterchangeFingerprint)
    {
        WriteRefusal(Refusal, FrontierProjectRefusalInterchange, "ProjectDrive cannot deliver the requested code interchange");
        return 0u;
    }

    std::memset(DeliveredInterchange, 0, sizeof(*DeliveredInterchange));
    DeliveredInterchange->StructureSize = sizeof(FrontierProjectInterchange);
    DeliveredInterchange->CodeInterchangeNumber = FrontierCodeInterchangeNumber;
    DeliveredInterchange->InterfaceFingerprint = FrontierCodeInterchangeFingerprint;
    DeliveredInterchange->ConstructProject = &ConstructProject;
    DeliveredInterchange->AdvanceProject = &AdvanceProject;
    DeliveredInterchange->RetireProject = &RetireProject;
    return 1u;
}

extern "C" FRONTIER_CODE_IMAGE_EXPORT uint32_t FRONTIER_CODE_IMAGE_CALL ProjectGeometryInterchange(
    void* ProjectRecord, FrontierProjectGeometryReading* Reading)
{
    if (!Reading || Reading->StructureSize != sizeof(*Reading) || Reading->InterchangeNumber != 1u ||
        !Reading->SubjectName) return 2u;
    static const char* Subjects[] = { "XPBD Tyre FL", "XPBD Tyre FR", "XPBD Tyre RL", "XPBD Tyre RR" };
    size_t Slot = 0u;
    while (Slot < 4u && std::strcmp(Subjects[Slot], Reading->SubjectName) != 0) ++Slot;
    if (Slot == 4u) return 0u;
    auto* Sequence = static_cast<DriveSequence*>(ProjectRecord);
    Reading->Revision = Sequence && Sequence->Vehicle ? Sequence->GeometryRevision : 0u;
    if (Reading->VertexCount == 0u) return 1u;
    if (!Sequence || !Sequence->Vehicle || Reading->Revision == 0u) return 0u;
    if (!Reading->RestPositions || !Reading->CurrentPositions) return 2u;
    auto Origin = Sequence->Geometry.AxleMountLocal(static_cast<uint32_t>(Slot));
    Origin.z += Sequence->Geometry.CoMHeight + 0.02f;
    for (uint32_t Index = 0u; Index < Reading->VertexCount; ++Index)
    {
        const float* Rest = Reading->RestPositions + Index * 3u;
        if (!std::isfinite(Rest[0]) || !std::isfinite(Rest[1]) || !std::isfinite(Rest[2])) return 2u;
        const bool Rubber = Reading->MaterialName == nullptr || std::strncmp(Reading->MaterialName, "StandardRubber", 14u) == 0;
        const auto World = Sequence->Surfaces[Slot].Project({Rest[0] - Origin.x, Rest[1] - Origin.y, Rest[2] - Origin.z}, Rubber);
        float* Current = Reading->CurrentPositions + Index * 3u;
        Current[0] = World.x; Current[1] = World.y; Current[2] = World.z;
    }
    return 1u;
}
