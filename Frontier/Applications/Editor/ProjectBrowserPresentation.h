/*====================================================================================================================================
                                                PROJECTBROWSERPRESENTATION.H
====================================================================================================================================*/
// 🧩 Native application selection and .frontier project selection for the Frontier editor host.

#pragma once

#include "ProjectBrowserHost.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Frontier
{

enum class ProjectBrowserPage : uint8_t
{
    Applications,
    Projects
};

struct ProjectBrowserButtonBounds
{
    float Left = 0.0f;
    float Top = 0.0f;
    float Right = 0.0f;
    float Bottom = 0.0f;
    bool Valid = false;
};

struct ProjectBrowserPresentation
{
    ProjectBrowserEnumeration          Listing;
    ProjectBrowserPage                 Page = ProjectBrowserPage::Applications;
    std::filesystem::path              ChosenDescriptor;
    ProjectBrowserButtonBounds         ApplicationButton;
    std::vector<ProjectBrowserButtonBounds> ProjectOpenButtons;
};

// Constructs the current native browser page. Selecting a project stores its descriptor path for the host to open.
void ConstructProjectBrowserInterface(ProjectBrowserPresentation& Presentation);

} // namespace Frontier
