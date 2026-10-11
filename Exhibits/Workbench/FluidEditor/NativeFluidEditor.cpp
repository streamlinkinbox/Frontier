//==============================================================================================================================================
//                                                           NATIVEFLUIDEDITOR.CPP
//==============================================================================================================================================
// 📦 Executed proof: the whole Fluid editor page, recorded from real ImGui draw commands against
//    Experimental/Fluid — index.html, src/FluidPanel.css and src/FluidPanel.js, the browser app the eight
//    sample scenes were authored in.
//
//    Five things are proved that a screenshot cannot. The four bands and the three columns resolve to the
//    stylesheet's own figures at every breakpoint it declares. The inspector's groups are the browser's
//    groups — same order, same badges, same ones closed, and the collider and the sun each promoting
//    their own group the way ConstructInspector() promotes it. Every key those groups name is a key the
//    generated control table carries, and every one of them resolves to a member of GasSettings, so no
//    row can be drawn with a blank reading. The derived strings — the timecode, the voxel count, the
//    bounds line, the lattice reading, the stage caption, the source track — are assembled the way
//    Refresh() assembles them. And the preset rail answers its filters over all eighteen cards.
//
//    Then it paints the page at three sizes and writes them out.

#include "FluidEditorSurface.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Frontier;
using namespace Frontier::FluidEditor;

namespace
{
unsigned Checks = 0;
void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition)
    {
        std::fprintf(stderr, "  FAIL  %s\n", Claim);
        throw std::runtime_error(Claim);
    }
    std::printf("  PASS  %s\n", Claim);
}
void Banner(const char* Title) { std::printf("\n%s\n", Title); }
bool Near(float A, float B, float Slack = 1e-3f) { return std::fabs(A - B) <= Slack; }

Subject ExampleScene()
{
    Subject Scene;
    Scene.DocumentName   = "Pyro plume";
    Scene.Unsaved        = true;
    Scene.PresetIdentity = "camp_fire";
    Scene.Filter         = PresetFilter::All;
    Scene.Rows[0] = { "Gas Domain",   ObjectKind::Domain,   true,  "48\xc2\xb3" };
    Scene.Rows[1] = { "Flame Source", ObjectKind::Emitter,  true,  "" };
    Scene.Rows[2] = { "Boulder",      ObjectKind::Collider, true,  "" };
    Scene.Rows[3] = { "Key light",    ObjectKind::Sun,      false, "" };
    Scene.RowCount = 4u;
    Scene.Chosen   = 1u;
    Scene.Settings = ConstructPresetSettings("camp_fire");
    Scene.Open     = InspectorTab::Source;
    Scene.Running  = true;
    Scene.Elapsed  = 3.25f;
    Scene.Advances = 195u;
    Scene.Hertz    = 60.0f;
    Scene.Readiness = "Ready";
    return Scene;
}
}   // namespace

int main()
{
    std::filesystem::create_directories("Exhibits/Gallery/FluidEditorNative");
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.IniFilename = nullptr;
    IO.DeltaTime = 1.0f / 60;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    ImFontConfig LightConfig;
    std::snprintf(LightConfig.Name, sizeof(LightConfig.Name), "Sun reference / light");
    ImFont* Light = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Light.ttf", 14,
                                                 &LightConfig);
    ImFontConfig RegularConfig;
    std::snprintf(RegularConfig.Name, sizeof(RegularConfig.Name), "Sun reference / regular");
    ImFont* Regular = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Regular.ttf", 14,
                                                   &RegularConfig);
    ImGui::StyleColorsDark();

    //---------------------------------------------------------------------------------------------------------
    Banner("The four bands and the three columns");
    {
        const Layout Page = Measure(1440.0f, 900.0f);
        Check(Near(Page.DocTop, 54.0f), "the header is 54 px, as .app-header is");
        Check(Near(Page.WorkTop, 90.0f), "and the document bar another 36");
        Check(Near(Page.StatusTop, 874.0f), "the status bar is 26 px off the bottom");
        Check(Near(Page.WorkTall, 784.0f), "so the workspace absorbs 784");
        Check(Near(Page.LeftW, 236.0f) && Near(Page.RightW, 310.0f),
              "the outliner is 236 wide and the inspector 310, the stylesheet's own columns");
        Check(Near(Page.CentreX, 236.0f) && Near(Page.CentreW, 894.0f),
              "and the viewport takes the 894 between them");
        Check(Near(Page.MoveTop, 738.0f) && Near(Page.TimeTop, 786.0f),
              "the transport is 48 tall and the timeline 88, stacked onto the status bar");
        Check(Near(Page.StageTop, 131.0f) && Near(Page.StageTall, 607.0f),
              "which leaves the viewport itself 607 px under its 41 px bar");

        const Layout Big = Measure(1680.0f, 1050.0f);
        Check(Near(Big.LeftW, 258.0f) && Near(Big.RightW, 330.0f),
              "at 1600 and up both panes widen, to 258 and 330");
        Check(Near(Big.RowSpacing, 15.0f) && Near(Big.ContentFoot, 18.0f) && Near(Big.CardPad, 10.0f),
              "and the same breakpoint loosens the property rows, the group foot and the preset cards");

        const Layout Snug = Measure(1100.0f, 800.0f);
        Check(Near(Snug.LeftW, 205.0f) && Near(Snug.RightW, 277.0f), "at 1200 and under they narrow to 205 and 277");

        const Layout Tight = Measure(900.0f, 700.0f);
        Check(Near(Tight.LeftW, 175.0f) && Near(Tight.RightW, 245.0f), "at 960 and under, to 175 and 245");
        Check(!Tight.ShowTag, "and the gas tag leaves the header, which is what that breakpoint hides");

        const Layout Bare = Measure(720.0f, 640.0f);
        Check(!Bare.ShowOutliner && Near(Bare.LeftW, 0.0f),
              "under 760 the outliner is dropped entirely rather than squeezed");
        Check(Near(Bare.CentreX, 0.0f) && Near(Bare.RightW, 230.0f),
              "the viewport slides to the left edge and the inspector holds 230");

        const Layout Cramped = Measure(400.0f, 600.0f);
        Check(Near(Cramped.CentreW, 260.0f),
              "the centre never goes under its minmax floor, however narrow the page gets");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The control table is the browser's, and it is complete");
    {
        Check(FluidControlCount == 82u, "all 82 controls came across");
        Check(FluidPresetCardCount == 18u, "and all 18 preset cards");
        Check(DebugChannelCount == 9u, "with the nine debug channels DEBUG_CHANNELS lists");

        const ControlRow* Bounds = ReadControl("boundsWidth");
        Check(Bounds != nullptr, "boundsWidth is in the table");
        Check(Near(Bounds->Low, 1.0f) && Near(Bounds->High, 4.0f) && Near(Bounds->Step, 0.05f),
              "carrying 1 to 4 m in steps of 0.05, which is what SceneSpecification.js declares");
        Check(std::strcmp(Bounds->Unit, "m") == 0, "and the metre unit cell RenderControl() gives it");
        Check(std::strcmp(Bounds->Label, "Width / depth") == 0
              && std::strcmp(Bounds->Heading, "Base Bounds Width X / Z") == 0,
              "the short label and the long heading are both kept, as the browser keeps both");

        const ControlRow* Lattice = ReadControl("gridResolution");
        Check(Lattice && Lattice->Kind == ControlKind::Choice && Lattice->ChoiceCount == 7u,
              "the resolution is a select of seven");
        Check(Lattice->Choices[0].Ordinal == 16 && Lattice->Choices[6].Ordinal == 128,
              "running 16 to 128, in the order the option list is written");

        const ControlRow* Enabled = ReadControl("emitterEnabled");
        Check(Enabled && Enabled->Kind == ControlKind::Toggle, "and emission is a switch, not a slider");
        Check(ReadControl("nothingAtAll") == nullptr, "a key nothing carries reads as absent, not as zero");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Every key the inspector names can actually be drawn");
    {
        const GasSettings Settings = ConstructPresetSettings("camp_fire");
        unsigned Named = 0u, Missing = 0u, Unreadable = 0u;
        const ObjectKind Kinds[4] = { ObjectKind::Domain, ObjectKind::Emitter, ObjectKind::Collider,
                                      ObjectKind::Sun };
        for (uint32_t Tab = 0u; Tab < 3u; ++Tab)
            for (const ObjectKind Kind : Kinds)
            {
                const GroupListing Listing = InspectorGroups(InspectorTab(Tab), Kind);
                for (uint32_t Index = 0u; Index < Listing.Count; ++Index)
                    for (uint32_t Slot = 0u; Slot < Listing.Groups[Index].KeyCount; ++Slot)
                    {
                        const char* Key = Listing.Groups[Index].Keys[Slot];
                        ++Named;
                        if (!ReadControl(Key)) ++Missing;
                        float Value = 0.0f;
                        if (!ReadSetting(Settings, Key, Value)) ++Unreadable;
                    }
            }
        Check(Named == 283u, "the twelve tab-and-object combinations name 283 rows between them");
        Check(Missing == 0u, "not one of them names a key the control table does not carry");
        Check(Unreadable == 0u, "and not one names a key GasSettings cannot answer, so no row paints blank");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The inspector's groups are ConstructInspector()'s groups");
    {
        const GroupListing Source = InspectorGroups(InspectorTab::Source, ObjectKind::Emitter);
        Check(Source.Count == 5u, "an emitter's Source tab has five groups");
        Check(std::strcmp(Source.Groups[0].Title, "Emission") == 0
              && std::strcmp(Source.Groups[0].Badge, "SOURCE") == 0 && Source.Groups[0].Opened,
              "Emission leads, badged SOURCE and open");
        Check(std::strcmp(Source.Groups[3].Title, "Burst generator") == 0 && !Source.Groups[3].Opened,
              "the burst generator is fourth and closed");
        Check(std::strcmp(Source.Groups[4].Title, "Collider") == 0 && !Source.Groups[4].Opened,
              "and the collider trails it, also closed");

        const GroupListing Chosen = InspectorGroups(InspectorTab::Source, ObjectKind::Collider);
        Check(Chosen.Count == 2u && std::strcmp(Chosen.Groups[0].Title, "Collider") == 0
              && Chosen.Groups[0].Opened,
              "select the collider itself and that group comes to the top, open, with the emission groups gone");
        Check(Chosen.Groups[0].Hint[0] != 0,
              "and it keeps its hint about normalised position, which is the only place that is explained");

        const GroupListing Simulation = InspectorGroups(InspectorTab::Simulation, ObjectKind::Domain);
        Check(Simulation.Count == 4u && std::strcmp(Simulation.Groups[1].Title, "Voxel lattice") == 0,
              "the Simulation tab is Domain, Voxel lattice, Forces and Combustion");
        Check(Simulation.Groups[0].Opened && Simulation.Groups[1].Opened && !Simulation.Groups[2].Opened
              && !Simulation.Groups[3].Opened, "with the first two open and the last two closed");

        const GroupListing Rendering = InspectorGroups(InspectorTab::Rendering, ObjectKind::Domain);
        Check(Rendering.Count == 5u && std::strcmp(Rendering.Groups[0].Title, "Volume shading") == 0,
              "on Rendering, volume shading leads when a domain is selected");
        const GroupListing Lit = InspectorGroups(InspectorTab::Rendering, ObjectKind::Sun);
        Check(Lit.Count == 5u && std::strcmp(Lit.Groups[0].Title, "Directional light") == 0
              && Lit.Groups[0].Opened,
              "select the sun and the light group is promoted instead, exactly as the collider is");
        Check(std::strcmp(Lit.Groups[1].Title, "Volume shading") == 0,
              "and volume shading drops to second rather than being removed");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The rail answers its filters");
    {
        const PresetCard* Rail = FluidPresetCards();
        unsigned Fire = 0u, Smoke = 0u, Blast = 0u, All = 0u;
        for (uint32_t Index = 0u; Index < FluidPresetCardCount; ++Index)
        {
            All   += Shows(Rail[Index], PresetFilter::All)   ? 1u : 0u;
            Fire  += Shows(Rail[Index], PresetFilter::Fire)  ? 1u : 0u;
            Smoke += Shows(Rail[Index], PresetFilter::Smoke) ? 1u : 0u;
            Blast += Shows(Rail[Index], PresetFilter::Blast) ? 1u : 0u;
        }
        Check(All == 18u, "All shows all eighteen");
        Check(Fire == 7u && Smoke == 7u && Blast == 4u, "and fire, smoke and blast hold 7, 7 and 4 of them");
        Check(Fire + Smoke + Blast == All,
              "which is every card accounted for exactly once -- no effect is unreachable behind a filter, "
              "and none is reachable behind two");
        Check(std::strcmp(Rail[0].Identity, "ue5_pyro_default") == 0
              && std::strcmp(Rail[0].Name, "Pyro plume") == 0,
              "the rail opens on the pyro plume, under the short name the card shows rather than the long "
              "one the preset library stores");
        Check(std::strcmp(Rail[FluidPresetCardCount - 1u].Identity, "low_gpu_performance") == 0,
              "and ends on the lightweight plume");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The derived readings");
    {
        char Line[96];
        Timecode(0.0f, Line, sizeof(Line));
        Check(std::strcmp(Line, "00:00.000") == 0, "an idle transport reads 00:00.000");
        Timecode(3.25f, Line, sizeof(Line));
        Check(std::strcmp(Line, "00:03.250") == 0, "3.25 s reads 00:03.250");
        Timecode(125.5f, Line, sizeof(Line));
        Check(std::strcmp(Line, "02:05.500") == 0, "and past two minutes it carries into the minute field");

        VoxelCount(48, Line, sizeof(Line));
        Check(std::strcmp(Line, "110,592 voxels") == 0,
              "a 48 lattice is 110,592 voxels, grouped as toLocaleString groups it");
        VoxelCount(16, Line, sizeof(Line));
        Check(std::strcmp(Line, "4,096 voxels") == 0, "and a 16 one needs only the one separator");

        GasSettings Settings;
        Settings.BoundsWidth = 1.85f; Settings.BoundsHeight = 2.1f;
        BoundsReading(Settings, Line, sizeof(Line));
        Check(std::strcmp(Line, "1.9 \xc3\x97 2.1 \xc3\x97 1.9 m") == 0,
              "the bounds line prints width, height, width -- X and Z are one setting, which is why it "
              "cannot read three different numbers");

        LatticeReading(96, Line, sizeof(Line));
        Check(std::strcmp(Line, "96\xc2\xb3 LATTICE") == 0, "the HUD reads 96 cubed");

        Subject Scene = ExampleScene();
        StageCaption(Scene, Line, sizeof(Line));
        Check(std::strcmp(Line, "Eulerian volume \xc2\xb7 physically shaded") == 0,
              "on the lit channel the caption says physically shaded");
        Scene.Channel = 6;
        StageCaption(Scene, Line, sizeof(Line));
        Check(std::strcmp(Line, "Eulerian volume \xc2\xb7 Vorticity") == 0,
              "and on any other it names that channel instead");
        Scene.Channel = 99;
        StageCaption(Scene, Line, sizeof(Line));
        Check(std::strcmp(Line, "Eulerian volume \xc2\xb7 physically shaded") == 0,
              "a channel out of range falls back to the lit one rather than reading off the end");

        Check(std::strcmp(SourceReading(Scene), "Continuous emission \xc2\xb7 burst armed") == 0,
              "the camp fire emits continuously with a burst armed");
        Scene.Settings.ShrapnelEnabled = false;
        Check(std::strcmp(SourceReading(Scene), "Continuous emission") == 0, "disarm it and the track just emits");
        Scene.Settings.EmitterEnabled = false;
        Check(std::strcmp(SourceReading(Scene), "No continuous emission") == 0,
              "and with the source off the track says so rather than going blank");

        Reading(1.85f, 0.05f, Line, sizeof(Line));
        Check(std::strcmp(Line, "1.85") == 0,
              "a step of 0.05 still prints two decimals -- FormatNumber brackets on a hundredth, it does "
              "not count the step's own places");
        Reading(0.125f, 0.005f, Line, sizeof(Line));
        Check(std::strcmp(Line, "0.125") == 0, "finer than a hundredth and it prints three");
        Reading(72.0f, 1.0f, Line, sizeof(Line));
        Check(std::strcmp(Line, "72") == 0, "and a whole step prints a whole number");

        Check(std::strcmp(KindName(ObjectKind::Emitter), "CONTINUOUS SOURCE") == 0
              && std::strcmp(KindName(ObjectKind::Collider), "SIGNED DISTANCE COLLIDER") == 0,
              "the eyebrow under a name is the browser's own word for that kind of object");
        Check(EnabledCount(Scene) == 3u, "and the outliner counts three of the four rows enabled");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The inspector fits the pane it is given");
    {
        Subject Scene = ExampleScene();
        const Layout Page = Measure(1440.0f, 900.0f);
        const float Needed = InspectorContentHeight(Light, Page.RightW, Scene, Page.RowSpacing,
                                                    Page.ContentFoot);
        Check(Needed > 0.0f, "the open tab has a measurable height");
        Check(Needed < Page.WorkTall * 3.0f,
              "which is within three screens -- the pane scrolls in the browser, so this only has to be sane");
        Scene.Open = InspectorTab::Rendering;
        const float Rendering = InspectorContentHeight(Light, Page.RightW, Scene, Page.RowSpacing,
                                                       Page.ContentFoot);
        Check(Rendering > 0.0f && Rendering != Needed,
              "and switching tabs changes it, which is the only way to know the measure reads the tab at all");

        const PropertyGroup& Emission = InspectorGroups(InspectorTab::Source, ObjectKind::Emitter).Groups[0];
        const float Open = GroupHeight(Light, Page.RightW, Emission, Page.RowSpacing, Page.ContentFoot);
        PropertyGroup Shut = Emission;
        Shut.Opened = false;
        Check(Near(GroupHeight(Light, Page.RightW, Shut, Page.RowSpacing, Page.ContentFoot),
                   GroupSummaryHeight()),
              "a closed group is exactly its summary, with no content left behind under it");
        Check(Open > GroupSummaryHeight() + 100.0f, "and an open one is its summary plus four rows and a hint");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Capture");

    int Width = 1440, Height = 900;
    Subject Scene = ExampleScene();

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Fluid", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Light, 14.0f);
        Paint(ImGui::GetWindowDrawList(), Light, Regular, Measure(float(Width), float(Height)), Scene);
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        for (int Frame = 0; Frame < 3; ++Frame) Tick();
        constexpr int Over = 2;
        const int WideOver = Width * Over, TallOver = Height * Over;
        std::vector<unsigned char> Dense(size_t(WideOver) * TallOver * 3, 9);
        for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
            FrontierProof::Draw(List, Dense.data(), WideOver, TallOver, { 0, 0 }, { Over, Over },
                                ImTextureID(), {});
        std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 9);
        for (int Y = 0; Y < Height; ++Y) for (int X = 0; X < Width; ++X) for (int C = 0; C < 3; ++C)
        {
            int Total = 0;
            for (int DY = 0; DY < Over; ++DY) for (int DX = 0; DX < Over; ++DX)
                Total += Dense[(size_t(Y * Over + DY) * WideOver + X * Over + DX) * 3 + C];
            Pixels[(size_t(Y) * Width + X) * 3 + C] = static_cast<unsigned char>(Total / (Over * Over));
        }
        const std::string Path = "Exhibits/Gallery/FluidEditorNative/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0,
              (std::string("the page is written out as ") + Name + ".png").c_str());
    };

    Capture("SourceTab");

    Scene.Open = InspectorTab::Simulation;
    Scene.Chosen = 0u;
    Scene.LatticeShown = true;
    Scene.Channel = 1;
    Capture("SimulationTab");

    Scene.Open = InspectorTab::Rendering;
    Scene.Chosen = 3u;
    Scene.LatticeShown = false;
    Scene.Channel = 0;
    Scene.Running = false;
    Scene.Filter = PresetFilter::Fire;
    Capture("RenderingTabWithSunSelected");

    Width = 1680; Height = 1050;
    Scene = ExampleScene();
    Scene.Chosen = 2u;
    Scene.Open = InspectorTab::Source;
    Capture("WideBreakpoint");

    Width = 900; Height = 700;
    Capture("NarrowBreakpoint");

    std::printf("\nPASS %u\n", Checks);
    return 0;
}
