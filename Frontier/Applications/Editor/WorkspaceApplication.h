/*==========================================================================================================================================
                                                        WORKSPACE APPLICATION
                                     ------------------------------------------------------------------
                 Editor host lifecycle (Initialize -> Loop -> Finalize) without banned entry-point vocabulary.
==========================================================================================================================================*/

// 🧩 Editor host lifecycle (Initialize -> Loop -> Finalize) transitioning Project Browser <-> Workspace in one loop.

#pragma once

#include "CommandLine.h"
#include "Engine/Interface/ProjectDescriptor.h"

#include <filesystem>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  WORKSPACE APPLICATION
//------------------------------------------------------------------------------------------------------------------------

class WorkspaceApplication
{
  public:
    WorkspaceApplication()                                       = default;
    WorkspaceApplication(const WorkspaceApplication&)            = delete;
    WorkspaceApplication& operator=(const WorkspaceApplication&) = delete;

    [[nodiscard]] bool Initialize(int ArgumentCount, const char* const* Arguments);
    [[nodiscard]] int  Loop();
    void               Finalize();

  private:
    LaunchRequest         Request;
    std::filesystem::path ExecutableFolder;
    ProjectDescriptor     InitialProject;
    bool                  InitialProjectPrepared = false;
    int                   ImmediateExitCode      = 0;
    bool                  ImmediateExit          = false;
};

[[nodiscard]] int ExecuteWorkspaceApplication(int ArgumentCount, const char* const* Arguments);

} // namespace Frontier
