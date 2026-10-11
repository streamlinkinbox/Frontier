//============================================================================================================================================
//                                                       FRONTIERRUNTIME.H
//============================================================================================================================================
// 📦 Shared window, device, renderer, editor, camera, input, and celestial execution reached by FrontierHost.

#pragma once

#include "../ProjectInterchange/ProjectInterchange.h"
#include <string>
#include <vector>

namespace Frontier
{

struct ProjectReception
{
    struct DeclaredPanel { std::string StableName, DisplayName; };
    struct ScenePlacement { std::string SubjectName; float Transform[16]; };
    std::vector<DeclaredPanel> DeclaredPanels;
    std::vector<ScenePlacement> SceneMutations;
    std::vector<FrontierProjectCameraRequest> CameraRequests;
    std::vector<FrontierProjectRenderingPreference> RenderingPreferences;
    std::vector<FrontierProjectDiagnostic> Diagnostics;
};

class CodeInterchange;
struct ProjectSpecification;

/// 📦 Runs the sole windowed Frontier experience after the project opening and code image have been verified.
/// in    ArgumentCount          [-]  command-line reading supplied to Frontier.exe
/// in    ArgumentVector         [-]  command-line text supplied to Frontier.exe
/// in    ResolvedSpecification  [-]  validated opening scene and content readings
/// in    ActiveInterchange      [-]  optional project simulation callbacks
/// out   ExitNumber             [-]  process completion reading
int RunFrontierRuntime(
    int ArgumentCount,
    char** ArgumentVector,
    const ProjectSpecification& ResolvedSpecification,
    CodeInterchange& ActiveInterchange,
    ProjectReception& ActiveReception);

} // namespace Frontier
