/*==========================================================================================================================================
                                                   BAKINGLAYOUTEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless interaction and rendered evidence for square baking tiles, queue controls, tabs and a single-map bake.

#include "UI/TextureStudio/BakingWorkspacePanel.h"
#include "RasterizeLibraryEvidence.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition) throw std::runtime_error(Description);
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 2, "Expected an evidence output directory.");
        const std::filesystem::path Output(Arguments[1]);
        std::filesystem::create_directories(Output);

        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        // 📝 Stage 8: Baking's docks are now hosted by the shared PanelDockspace (same as Pattern/Stamp),
        // which needs real ImGui docking enabled or its DockBuilder calls crash against a docking context
        // that was never set up.
        IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        IO.DisplaySize = ImVec2(1680, 1000);
        IO.DeltaTime = 1.f / 60;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        IO.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;
        if (std::filesystem::is_regular_file("EngineContent/FontArchives/DMSans/DMSans-Regular.ttf"))
            IO.Fonts->AddFontFromFileTTF("EngineContent/FontArchives/DMSans/DMSans-Regular.ttf", 14);
        else IO.Fonts->AddFontDefault();
        unsigned char *Pixels = nullptr;
        int AtlasWidth = 0, AtlasHeight = 0;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);

        ActivationOwnership PanelOwnership(new BakingWorkspacePanel());
        auto *Panel = static_cast<BakingWorkspacePanel *>(PanelOwnership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Baking panel lifecycle failed.");
        Panel->ConfigureExportDirectory(Output / "ExportedMaps");

        const auto Frame = [&]
        {
            ImGui::NewFrame();
            (void)Panel->Integrate(IO.DeltaTime);
            Panel->ConstructInterface();
            ImGui::Render();
            Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Baking workspace produced no ImGui draw commands.");
        };
        for (int Index = 0; Index < 4; ++Index) Frame();

        int CaptureIndex = 0;
        const auto Capture = [&]
        {
            IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
            char Name[64];
            std::snprintf(Name, sizeof(Name), "%02d-frame.ppm", CaptureIndex++);
            EncodeLibraryDrawCapture(Output / Name, *ImGui::GetDrawData(), Pixels, AtlasWidth, AtlasHeight);
        };

        const auto MoveTo = [&](ImVec2 Position) { IO.AddMousePosEvent(Position.x, Position.y); Frame(); };
        const auto Down = [&] { IO.AddMouseButtonEvent(0, true); Frame(); };
        const auto Up = [&] { IO.AddMouseButtonEvent(0, false); Frame(); Frame(); Frame(); };
        const auto Click = [&](PlaneExtent Extent)
        {
            Require(Extent.Width() > 0 && Extent.Height() > 0, "Target control was never drawn.");
            MoveTo(ImVec2((Extent.MinimumX + Extent.MaximumX) * .5f, (Extent.MinimumY + Extent.MaximumY) * .5f));
            Down();
            Up();
        };

        const PlaneExtent Tile = Panel->QueryControlExtent("Tile normal");
        Require(Tile.Width() > 0 && std::abs(Tile.Width() - Tile.Height()) < 1.f, "Map tile must be square.");
        const auto Before = Panel->QuerySettings().Channels;
        Click(Tile);
        Require(Panel->QuerySettings().Channels == Before, "Selecting a tile must not change the batch queue.");
        Click(Panel->QueryControlExtent("Flip green channel"));
        Require(Panel->QuerySettings().MapOutput.at("normal").FlipGreen, "Per-map green-channel control did not update selected normal map.");
        Click(Panel->QueryControlExtent("Flip green channel"));
        Require(Panel->QuerySettings().MapOutput.find("normal") == Panel->QuerySettings().MapOutput.end(), "Per-map setting did not return to default.");
        Capture();
        Click(Panel->QueryControlExtent("Queue normal"));
        Require(Panel->QuerySettings().Channels.size() + 1 == Before.size(), "Queue toggle did not remove normal.");
        Click(Panel->QueryControlExtent("Queue normal"));
        Require(Panel->QuerySettings().Channels.size() == Before.size(), "Queue toggle did not restore normal.");
        // The map-specific output controls push projection fields down; scroll the inspector before clicking.
        MoveTo(ImVec2(IO.DisplaySize.x - 100.f, IO.DisplaySize.y - 150.f));
        IO.AddMouseWheelEvent(0, -4.f); for (int I=0; I<90; ++I) Frame();
        const PlaneExtent Slider = Panel->QueryControlExtent("Front distance slider");
        Require(Slider.Width() > 0, "Shared slider was not drawn.");
        const float OriginalDistance = Panel->QuerySettings().Front;
        MoveTo(ImVec2(Slider.MinimumX + Slider.Width() * .6f, (Slider.MinimumY + Slider.MaximumY) * .5f));
        Down();
        Up();
        Require(Panel->QuerySettings().Front != OriginalDistance, "Shared slider did not change bake settings.");
        Capture();
        Click(Panel->QueryControlExtent("Tab Outliner"));
        Capture();
        Require(Panel->QuerySelectedSourceCount() == 1, "Expected one high source.");
        Click(Panel->QueryControlExtent("High source Teapot"));
        Require(Panel->QuerySelectedSourceCount() == 0, "High source exclusion failed.");
        Click(Panel->QueryControlExtent("High source Teapot"));
        Require(Panel->QuerySelectedSourceCount() == 1, "High source inclusion failed.");
        Click(Panel->QueryControlExtent("Tab Mesh Maps"));
        Click(Panel->QueryControlExtent("Bake selected map"));
        bool Finished = false;
        for (int Guard = 0; Guard < 4000 && !Finished; ++Guard)
        {
            Frame();
            Finished = Panel->QueryControlExtent("Export").Width() > 0;
            if (!Finished) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        Require(Finished, "Selected map bake did not complete.");
        Click(Panel->QueryControlExtent("Export"));
        for (int Guard=0; Guard<4000 && Panel->QueryLastExportDirectory().empty(); ++Guard)
        { Frame(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        Require(!Panel->QueryLastExportDirectory().empty(), "Export did not complete.");
        size_t Count = 0;
        for (const auto &Entry : std::filesystem::directory_iterator(Panel->QueryLastExportDirectory()))
            if (Entry.path().extension() == ".png") ++Count;
        Require(Count == 1, "Single-map bake must export exactly one map.");
        Capture();
        Click(Panel->QueryControlExtent("Tile volume-sdf"));
        Click(Panel->QueryControlExtent("Bake selected map"));
        for (int Guard = 0; Guard < 6000 && Panel->QueryVolume().Distances.empty(); ++Guard)
        {
            Frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        Require(!Panel->QueryVolume().Distances.empty(), "Volume bake did not finish.");
        Capture();
        Click(Panel->QueryControlExtent("View3D"));
        Capture();
        Click(Panel->QueryControlExtent("Export volume"));
        Require(std::filesystem::is_regular_file(Output / "ExportedMaps/volume-sdf.nrrd"), "Volume export did not write NRRD.");
        Click(Panel->QueryControlExtent("Tab Outliner"));
        const auto Drop = Panel->QueryControlExtent("Import low mesh");
        Require(Drop.Width() > 0, "Low import drop target missing.");
        const auto Obj = Output / "drop.obj";
        { std::ofstream File(Obj); File << "o Test_low\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n"; }
        Require(Panel->ImportDroppedBakeMesh(Obj, (Drop.MinimumX+Drop.MaximumX)*.5f, (Drop.MinimumY+Drop.MaximumY)*.5f), "Low drop was not accepted.");
        Frame();
        Require(Panel->QueryVolume().Distances.empty(), "Changing geometry must invalidate the old volume.");
        Click(Panel->QueryControlExtent("Tab Mesh Maps"));
        Require(!Panel->ImportDroppedBakeMesh(Obj, (Drop.MinimumX+Drop.MaximumX)*.5f, (Drop.MinimumY+Drop.MaximumY)*.5f), "Drop accepted on inactive outliner.");
        std::filesystem::remove(Obj);
        PanelOwnership.reset();
        ImGui::DestroyContext();
        std::cout << "PASS: square tiles, selection, queue, tabs, shared slider and single-map bake/export.\n";
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "BakingWorkspaceEvidence failed: " << Failure.what() << "\n";
        return 1;
    }
    return 0;
}
