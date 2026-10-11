//============================================================================================================================================
//                                                       FRONTIERHOST.CPP
//============================================================================================================================================
// 📦 Project-opening orchestration before the shared Frontier windowed runtime receives control.

#include "FrontierHost.h"
#include "ProjectOpeningSequence.h"

#include "FrontierRuntime.h"
#include "../ProjectInterchange/CodeInterchange.h"
#include "../ProjectInterchange/ProjectSpecification.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

namespace
{

using Frontier::ProjectReception;

void ReceiveSceneMutation(const FrontierProjectSceneMutation* ActiveMutation, void* ReceptionAddress)
{
    if (ActiveMutation == nullptr || ActiveMutation->StructureSize < sizeof(FrontierProjectSceneMutation) ||
        ReceptionAddress == nullptr)
        return;

    if (ActiveMutation->MutationNumber != 2u || ActiveMutation->SubjectName == nullptr) return;
    ProjectReception::ScenePlacement Placement;
    Placement.SubjectName = ActiveMutation->SubjectName;
    std::copy_n(ActiveMutation->Transform, 16u, Placement.Transform);
    static_cast<ProjectReception*>(ReceptionAddress)->SceneMutations.push_back(std::move(Placement));
}

void ReceiveCameraRequest(const FrontierProjectCameraRequest* ActiveRequest, void* ReceptionAddress)
{
    if (ActiveRequest == nullptr || ActiveRequest->StructureSize < sizeof(FrontierProjectCameraRequest) ||
        ReceptionAddress == nullptr)
        return;

    static_cast<ProjectReception*>(ReceptionAddress)->CameraRequests.push_back(*ActiveRequest);
}

void ReceiveRenderingPreference(const FrontierProjectRenderingPreference* ActivePreference, void* ReceptionAddress)
{
    if (ActivePreference == nullptr || ActivePreference->StructureSize < sizeof(FrontierProjectRenderingPreference) ||
        ReceptionAddress == nullptr)
        return;

    static_cast<ProjectReception*>(ReceptionAddress)->RenderingPreferences.push_back(*ActivePreference);
}

void ReceiveProjectPanel(const FrontierProjectPanel* ActivePanel, void* ReceptionAddress)
{
    if (ActivePanel == nullptr || ActivePanel->StructureSize < sizeof(FrontierProjectPanel) ||
        ActivePanel->StableName == nullptr || ActivePanel->DisplayName == nullptr || ReceptionAddress == nullptr)
        return;

    static_cast<ProjectReception*>(ReceptionAddress)->DeclaredPanels.push_back(
        { ActivePanel->StableName, ActivePanel->DisplayName });
}

void ReceiveDiagnostic(const FrontierProjectDiagnostic* ActiveDiagnostic, void* ReceptionAddress)
{
    if (ActiveDiagnostic == nullptr || ActiveDiagnostic->StructureSize < sizeof(FrontierProjectDiagnostic) ||
        ReceptionAddress == nullptr)
        return;

    static_cast<ProjectReception*>(ReceptionAddress)->Diagnostics.push_back(*ActiveDiagnostic);
}

const char* QuerySpecificationArgument(int ArgumentCount, char** ArgumentVector)
{
    for (int ArgumentIndex = 1; ArgumentIndex < ArgumentCount; ++ArgumentIndex)
    {
        const std::string ActiveArgument(ArgumentVector[ArgumentIndex]);
        if (ActiveArgument.size() >= 9u && ActiveArgument.substr(ActiveArgument.size() - 9u) == ".frontier")
            return ArgumentVector[ArgumentIndex];
    }

    return nullptr;
}

} // namespace

namespace Frontier
{

int RunFrontierHost(int ArgumentCount, char** ArgumentVector)
{
    const char* SpecificationArgument = QuerySpecificationArgument(ArgumentCount, ArgumentVector);
    if (SpecificationArgument == nullptr)
    {
        for (int Index = 1; Index + 1 < ArgumentCount; ++Index)
            if (std::strcmp(ArgumentVector[Index], "--verify-project-browser") == 0)
                return RunProjectBrowser(ArgumentVector[Index + 1]);
        return RunProjectBrowser();
    }

    ProjectSpecification ResolvedSpecification;
    std::string Refusal;
    if (!DecodeProjectSpecification(SpecificationArgument, ResolvedSpecification, Refusal))
    {
        std::fprintf(stderr, "Frontier.exe refused project opening: %s\n", Refusal.c_str());
        return 65;
    }

    CodeInterchange ActiveInterchange;
    if (!ActiveInterchange.Construct(ResolvedSpecification, Refusal))
    {
        std::fprintf(stderr, "Frontier.exe refused project code image: %s\n", Refusal.c_str());
        return 66;
    }

    ProjectReception ActiveReception;
    FrontierProjectInputReading InitialInput{};
    InitialInput.StructureSize = sizeof(FrontierProjectInputReading);

    FrontierProjectHostInterchange HostInterchange{};
    HostInterchange.StructureSize = sizeof(FrontierProjectHostInterchange);
    HostInterchange.InputReading = &InitialInput;
    HostInterchange.ReceiveSceneMutation = &ReceiveSceneMutation;
    HostInterchange.ReceiveCameraRequest = &ReceiveCameraRequest;
    HostInterchange.ReceiveRenderingPreference = &ReceiveRenderingPreference;
    HostInterchange.ReceivePanel = &ReceiveProjectPanel;
    HostInterchange.ReceiveDiagnostic = &ReceiveDiagnostic;
    HostInterchange.ProjectReception = &ActiveReception;
    if (!ActiveInterchange.ConstructProject(ResolvedSpecification, HostInterchange, Refusal))
    {
        std::fprintf(stderr, "Frontier.exe refused project construction: %s\n", Refusal.c_str());
        return 67;
    }

    return RunFrontierRuntime(ArgumentCount, ArgumentVector, ResolvedSpecification, ActiveInterchange, ActiveReception);
}

} // namespace Frontier
