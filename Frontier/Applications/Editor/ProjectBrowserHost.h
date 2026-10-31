/*==========================================================================================================================================
                                                    PROJECT ENUMERATION FOR THE BROWSER
                                     ------------------------------------------------------------------
                                   Every immediate project folder's descriptor, decoded and name-sorted.
==========================================================================================================================================*/

// 🧩 Project enumeration for the browser: every immediate project folder's descriptor, decoded and name-sorted.

#pragma once

#include "Engine/Interface/ProjectDescriptor.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ProjectBrowserEntry
{
    ProjectDescriptor Descriptor;   // [-] - Decoded descriptor (name, code image, categories, paths)
};

struct ProjectBrowserEnumeration
{
    std::vector<ProjectBrowserEntry> Entries;      // [projects] - Decoded projects, sorted by name
    std::vector<std::string>         Diagnostics;  // [lines] - One line per descriptor that failed to decode
};

#if defined(FRONTIER_WITH_INTERFACE_SDK)
struct ProjectBrowserButtonBounds
{
    float Left = 0.0f;
    float Top = 0.0f;
    float Right = 0.0f;
    float Bottom = 0.0f;
    bool Valid = false;
};
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Scans ProjectsDirectory's immediate subfolders for .frontier files (the repository's project layout).
// Unreadable descriptors are reported in Diagnostics instead of failing the whole enumeration.
[[nodiscard]] ProjectBrowserEnumeration EnumerateProjects(const std::filesystem::path& ProjectsDirectory);

// Registers a descriptor's declared [[LogCategories]] with the log registry (descriptor colours applied).
// Returns how many categories were newly registered by this call.
uint32_t RegisterDescriptorLogCategories(const ProjectDescriptor& Descriptor);

#if defined(FRONTIER_WITH_INTERFACE_SDK)
// Draws the existing .frontier project list from FrontierMain::RunBrowserWindow. The optional bounds are
// capture instrumentation only; the native browser model and visible controls remain the existing ones.
void ConstructProjectBrowserInterface(const ProjectBrowserEnumeration& Listing,
                                      std::filesystem::path& ChosenDescriptor,
                                      std::vector<ProjectBrowserButtonBounds>* ProjectOpenButtons = nullptr);
#endif

} // namespace Frontier
