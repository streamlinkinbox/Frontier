/*==========================================================================================================================================
                                            ENUMERATION TOLERATES INDIVIDUAL BROKEN DESCRIPTORS
                                     ------------------------------------------------------------------
                                            The browser lists what decoded and reports the rest.
==========================================================================================================================================*/

// 🧩 Enumeration tolerates individual broken descriptors: the browser lists what decoded and reports the rest.

#include "ProjectBrowserHost.h"

#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Interface/ProjectContent.h"

#include <algorithm>
#include <system_error>

#if defined(FRONTIER_WITH_INTERFACE_SDK)
#include <imgui.h>
#endif

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ProjectBrowserEnumeration EnumerateProjects(const std::filesystem::path& ProjectsDirectory)
{
    ProjectBrowserEnumeration Result;

    std::error_code Error;
    if (!std::filesystem::is_directory(ProjectsDirectory, Error))
    {
        Result.Diagnostics.push_back("Projects directory not found: " + EncodeUtf8Path(ProjectsDirectory));
        return Result;
    }

    for (const std::filesystem::directory_entry& ProjectFolder :
         std::filesystem::directory_iterator(ProjectsDirectory, Error))
    {
        if (!ProjectFolder.is_directory(Error))
        {
            continue;
        }
        for (const std::filesystem::directory_entry& Candidate :
             std::filesystem::directory_iterator(ProjectFolder.path(), Error))
        {
            if (!Candidate.is_regular_file(Error) || Candidate.path().extension() != ".frontier")
            {
                continue;
            }

            ProjectDescriptor Descriptor;
            std::string       Diagnostic;
            if (DecodeProjectDescriptor(Candidate.path(), Descriptor, Diagnostic))
            {
                Result.Entries.push_back(ProjectBrowserEntry{Descriptor});
            }
            else
            {
                Result.Diagnostics.push_back(EncodeUtf8Path(Candidate.path()) + ": " + Diagnostic);
            }
            break;   // [descriptors] - One descriptor per project folder
        }
    }

    std::sort(Result.Entries.begin(), Result.Entries.end(),
              [](const ProjectBrowserEntry& Left, const ProjectBrowserEntry& Right)
              {
                  return Left.Descriptor.Name < Right.Descriptor.Name;
              });
    return Result;
}

uint32_t RegisterDescriptorLogCategories(const ProjectDescriptor& Descriptor)
{
    uint32_t Registered = 0;
    for (const ProjectLogCategory& Declared : Descriptor.LogCategories)
    {
        const bool AlreadyKnown = LogCategories().QueryByName(Declared.Name) != nullptr;

        LogColour Colour = LogColour::Neutral;
        if (const std::optional<LogColour> Parsed = ParseLogColour(Declared.Colour))
        {
            Colour = *Parsed;
        }
        const LogCategory Added = RegisterLogCategory(Declared.Name, Colour);
        Registered += (!AlreadyKnown && Added.Token != 0) ? 1 : 0;
    }
    return Registered;
}

#if defined(FRONTIER_WITH_INTERFACE_SDK)
namespace
{
ProjectBrowserButtonBounds CurrentBrowserButtonBounds() noexcept
{
    const ImVec2 Minimum = ImGui::GetItemRectMin();
    const ImVec2 Maximum = ImGui::GetItemRectMax();
    return {Minimum.x, Minimum.y, Maximum.x, Maximum.y, true};
}
}

void ConstructProjectBrowserInterface(const ProjectBrowserEnumeration& Listing,
                                      std::filesystem::path& ChosenDescriptor,
                                      std::vector<ProjectBrowserButtonBounds>* ProjectOpenButtons)
{
    if (ProjectOpenButtons != nullptr)
    {
        ProjectOpenButtons->assign(Listing.Entries.size(), {});
    }

    const ImVec2 Display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(Display);
    if (ImGui::Begin("Project Browser", nullptr, ImGuiWindowFlags_NoDecoration))
    {
        ImGui::TextUnformatted("Open a project");
        ImGui::Separator();
        for (size_t Index = 0; Index < Listing.Entries.size(); ++Index)
        {
            const ProjectBrowserEntry& Entry = Listing.Entries[Index];
            ImGui::PushID(static_cast<int>(Index));
            if (ImGui::Button(Entry.Descriptor.Name.c_str(), ImVec2(420.0f, 0.0f)))
            {
                ChosenDescriptor = Entry.Descriptor.DescriptorPath;
            }
            if (ProjectOpenButtons != nullptr)
            {
                (*ProjectOpenButtons)[Index] = CurrentBrowserButtonBounds();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", EncodeUtf8Path(Entry.Descriptor.ProjectDirectory).c_str());
            ImGui::PopID();
        }
        for (const std::string& Diagnostic : Listing.Diagnostics)
        {
            ImGui::TextDisabled("skipped: %s", Diagnostic.c_str());
        }
    }
    ImGui::End();
}
#endif // FRONTIER_WITH_INTERFACE_SDK

} // namespace Frontier
