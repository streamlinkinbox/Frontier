/*============================================================================================================================================
                                                      PATTERNLIBRARYGALLERYVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Prove the Alloy gallery port matches the web library: collection counts, search behaviour, palette
// variants, and crash-free tessellation of every shipped family into finite card geometry.

#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include <imgui_internal.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>

using namespace Frontier;

namespace
{
uint32_t Assertions = 0;

void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Assertions;
}
}

int main()
{
    try
    {
        PatternWorkspacePanel Workspace;
        Confirm(Workspace.ConfigurePresetDirectory("EngineContent/PatternArchives/TextureStudio"), "Pattern library directory rejected.");
        Confirm(Workspace.QueryLibraryFamilies().size() == 103, "Pattern library family count changed.");
        Confirm(Workspace.QueryLibraryMatches().size() == 103, "Unfiltered gallery match count changed.");
        for (const auto &[Group, Count] : {std::pair<const char *, size_t>{"Geometric constructions", 30}, {"Geometric designs", 16},
                 {"Fabric weaves", 10}, {"Stitch patterns", 10}, {"Color patterns", 10}, {"Islamic geometry", 10},
                 {"African traditional", 10}, {"Originals", 7}})
        {
            Workspace.ConfigureLibraryFilter(Group, "", true);
            Confirm(Workspace.QueryLibraryMatches().size() == Count, "Gallery collection count changed.");
        }
        Workspace.ConfigureLibraryFilter("All patterns", "", true);
        for (const auto &[Query, Count] : {std::pair<const char *, size_t>{"kente", 2}, {"weave", 11}, {"xyz654", 0}, {"star", 6}})
        {
            Workspace.ConfigureLibrarySearch(Query);
            Confirm(Workspace.QueryLibraryMatches().size() == Count, "Gallery search count changed.");
        }
        Workspace.ConfigureLibraryFilter("All patterns", "", true);
        size_t Tessellated = 0, Triangles = 0;
        auto Budget = std::chrono::steady_clock::now();
        for (const auto &Family : Workspace.QueryLibraryFamilies())
        {
            for (const auto &[Palette, File] : Family.Files)
            {
                (void)File;
                Workspace.ConfigureLibraryFilter("All patterns", Palette, false);
                const auto Started = std::chrono::steady_clock::now();
                const auto &Entry = Workspace.QueryPresetEntry(Family);
                const auto Elapsed = std::chrono::steady_clock::now() - Started;
                Confirm(!Entry.Failed, "Shipped preset failed gallery decode.");
                Confirm(!Entry.Shapes.empty(), "Shipped preset tessellated to no card shapes.");
                Confirm(Elapsed < std::chrono::seconds(10), "Preset tessellation exceeded its hang budget.");
                for (const auto &Shape : Entry.Shapes)
                {
                    Confirm(Shape.Triangles.size() % 3 == 0, "Card fill is not whole triangles.");
                    Confirm(Shape.StrokeWidth >= 0, "Card stroke width is negative.");
                    for (const auto &Point : Shape.Triangles)
                        Confirm(std::isfinite(Point[0]) && std::isfinite(Point[1]) && std::abs(Point[0]) < 10000 && std::abs(Point[1]) < 10000,
                            "Card triangle escaped sane tile bounds.");
                    for (const auto &Point : Shape.Outline)
                        Confirm(std::isfinite(Point[0]) && std::isfinite(Point[1]) && std::abs(Point[0]) < 10000 && std::abs(Point[1]) < 10000,
                            "Card outline escaped sane tile bounds.");
                    Triangles += Shape.Triangles.size() / 3;
                }
                ++Tessellated;
            }
        }
        Confirm(Tessellated == 178, "Gallery palette variant count changed.");
        // 🧩 Headless grid regression: construct full interface frames and
        // prove the 24 page-zero cards land in pairwise-disjoint boxes. The
        // wrapped card name used to leave the cursor mid-card, shearing the
        // rows into a diagonal cascade that data-level checks could not see.
        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        if (std::filesystem::is_regular_file("EngineContent/FontArchives/Inter/Inter-Regular.otf"))
            IO.Fonts->AddFontFromFileTTF("EngineContent/FontArchives/Inter/Inter-Regular.otf", 14);
        else IO.Fonts->AddFontDefault();
        unsigned char *Pixels = nullptr;
        int AtlasWidth = 0, AtlasHeight = 0;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
        PatternWorkspacePanel Grid;
        Confirm(Grid.ConfigurePresetDirectory("EngineContent/PatternArchives/TextureStudio"), "Grid panel rejected the pattern library.");
        Grid.ConfigureTypeface(IO.Fonts->Fonts[0]);
        Confirm(Grid.Initialize() && Grid.Activate(), "Grid panel lifecycle failed.");
        Confirm(Grid.QueryPanels().ConfigureStackedPanels("pattern-library", "pattern-layers", 800.f),
            "Grid panel refused the expanded gallery split.");
        IO.DisplaySize = ImVec2(1280, 900);
        IO.DeltaTime = 1.f / 60;
        for (int Frame = 0; Frame < 6; ++Frame)
        {
            ImGui::NewFrame();
            Grid.ConstructInterface();
            ImGui::Render();
        }
        Confirm(ImGui::FindWindowByName("Pattern library###FrontierPanel-pattern-library") != nullptr,
            "Pattern library dock panel is absent.");
        const auto GridFamilies = Grid.QueryLibraryFamilies();
        const auto GridMatches = Grid.QueryLibraryMatches();
        Confirm(GridMatches.size() == 103, "Grid panel match count changed.");
        for (size_t First = 0; First < 24; ++First)
        {
            const auto A = Grid.QueryControlExtent(GridFamilies[GridMatches[First]].Name);
            Confirm(A.Width() > 0 && A.Height() > 0, "Gallery card recorded no extent.");
            for (size_t Second = First + 1; Second < 24; ++Second)
            {
                const auto B = Grid.QueryControlExtent(GridFamilies[GridMatches[Second]].Name);
                const bool Overlaps = A.MinimumX < B.MaximumX - 1 && B.MinimumX < A.MaximumX - 1 &&
                    A.MinimumY < B.MaximumY - 1 && B.MinimumY < A.MaximumY - 1;
                Confirm(!Overlaps, "Gallery cards overlap.");
            }
        }
        ImGui::DestroyContext();
        std::cout << "{\"result\":\"passed\",\"checks\":" << Assertions << ",\"variants\":" << Tessellated
                  << ",\"triangles\":" << Triangles << ",\"seconds\":"
                  << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - Budget).count() << "}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cout << "{\"result\":\"failed\",\"error\":\"" << Failure.what() << "\"}\n";
        return 1;
    }
}
