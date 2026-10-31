/*==========================================================================================================================================
                                                   PROJECTEVERESTINTERCHANGE.CPP
==========================================================================================================================================*/
// 🧩 Project Everest v3 code image: emits dual-tier GPU, vehicle, and telemetry diagnostics into the Log Stream.

#include <ProjectInterchange.h>

#include <chrono>
#include <cstdio>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

using SteadyTime = std::chrono::steady_clock::time_point;

FrontierHostInterchange Host      = {};                  // [-]  - Host callback table received on construction
bool                    HostReady = false;               // [-]  - True while the project code image is active
SteadyTime              LastEmit  = {};                  // [ms] - Timestamp of most recent diagnostic emission
uint32_t                Tick      = 0;                   // [-]  - Monotonic advance counter
uint32_t                Sequence  = 0;                   // [-]  - Diagnostic rotation index
uint32_t                Generator = 0;                   // [-]  - Deterministic LCG state

constexpr std::chrono::milliseconds EmitCadence{2400};   // [ms] - Diagnostic emission period

//------------------------------------------------------------------------------------------------------------------------
//                                                  INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

uint32_t AdvanceSequence() noexcept
{
    Generator = Generator * 1664525u + 1013904223u;
    return (Generator >> 16) & 0x7fffu;
}

void EmitDiagnostic(const char* SubjectName, const char* Explanation)
{
    if (!HostReady || Host.ReceiveDiagnostic == nullptr)
    {
        return;
    }

    FrontierProjectDiagnostic Diagnostic = {};
    Diagnostic.StructureSize             = sizeof(Diagnostic);
    Diagnostic.SubjectName               = SubjectName;
    Diagnostic.Explanation               = Explanation;
    Host.ReceiveDiagnostic(&Diagnostic, Host.ProjectReception);
}

bool FRONTIER_CODE_IMAGE_CALL ConstructProject(
    const FrontierHostInterchange* HostInterchange, void* ProjectReception)
{
    if (HostInterchange == nullptr || HostInterchange->StructureSize < sizeof(*HostInterchange) ||
        HostInterchange->ReceiveDiagnostic == nullptr)
    {
        return false;
    }

    Host      = *HostInterchange;
    HostReady = true;
    Generator = 2166136261u;
    Tick      = 0;
    Sequence  = 0;
    LastEmit  = std::chrono::steady_clock::now() - EmitCadence;
    (void)ProjectReception;

    EmitDiagnostic("Project-Everest/Everest", "Project Everest workspace constructed · dual-tier GPU contract ready");
    return true;
}

void FRONTIER_CODE_IMAGE_CALL AdvanceProject()
{
    ++Tick;

    const SteadyTime Now = std::chrono::steady_clock::now();
    if (Now - LastEmit < EmitCadence)
    {
        return;
    }
    LastEmit = Now;
    ++Sequence;

    char Line[256] = {};
    switch (Sequence % 5)
    {
        case 0:
            std::snprintf(Line, sizeof(Line),
                          "Everest proving grounds · frame %u · outliner and inspector registry synchronised", Tick);
            EmitDiagnostic("Project-Everest/Everest", Line);
            break;

        case 1:
            std::snprintf(Line, sizeof(Line),
                          "ControlVehicle powertrain · gear %u · %u rpm · glazing transmittance active",
                          2 + AdvanceSequence() % 5, 2400 + AdvanceSequence() % 3800);
            EmitDiagnostic("Project-Everest/Powertrain", Line);
            break;

        case 2:
            std::snprintf(Line, sizeof(Line),
                          "RTX tier · PathReplayRayQuery + DielectricBoundary · temporal reuse %u.%u%%",
                          95 + AdvanceSequence() % 5, AdvanceSequence() % 10);
            EmitDiagnostic("Project-Everest/RayTracing", Line);
            break;

        case 3:
            std::snprintf(Line, sizeof(Line),
                          "GTX fallback tier · 4 toroidal SDF cascades + SurfaceCards + OrderedSoftwareBvh (%u BLAS)",
                          18 + AdvanceSequence() % 12);
            EmitDiagnostic("Project-Everest/DistanceField", Line);
            break;

        default:
            std::snprintf(Line, sizeof(Line),
                          "Cabin telemetry · speed %u km/h · battery %u%% · thermal nominal",
                          48 + AdvanceSequence() % 72, 74 + AdvanceSequence() % 24);
            EmitDiagnostic("Project-Everest/Telemetry", Line);
            break;
    }
}

void FRONTIER_CODE_IMAGE_CALL RetireProject()
{
    EmitDiagnostic("Project-Everest/Everest", "Project Everest retired by host");
    HostReady = false;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

extern "C"
{

bool FRONTIER_CODE_IMAGE_CALL ConstructProjectInterchange(
    uint32_t InterchangeNumber, uint64_t InterfaceFingerprint,
    FrontierProjectInterchange* InterchangeDelivery, char* RefusalExplanation,
    size_t RefusalExplanationCapacity)
{
    const auto Refuse = [&](const char* Explanation) -> bool {
        if (RefusalExplanation != nullptr && RefusalExplanationCapacity > 0)
        {
            std::snprintf(RefusalExplanation, RefusalExplanationCapacity, "%s", Explanation);
        }
        return false;
    };

    if (InterchangeDelivery == nullptr ||
        InterchangeDelivery->StructureSize < sizeof(FrontierProjectInterchange))
    {
        return Refuse("Interchange delivery record is missing or undersized.");
    }

    if (InterchangeNumber != FRONTIER_CODE_INTERCHANGE_NUMBER)
    {
        return Refuse("Project-Everest does not implement the requested code interchange number.");
    }

    if (InterfaceFingerprint != FRONTIER_INTERFACE_FINGERPRINT)
    {
        return Refuse("Project-Everest interface fingerprint does not match the host.");
    }

    InterchangeDelivery->ConstructProject = &ConstructProject;
    InterchangeDelivery->AdvanceProject   = &AdvanceProject;
    InterchangeDelivery->RetireProject    = &RetireProject;
    return true;
}

} // extern "C"
