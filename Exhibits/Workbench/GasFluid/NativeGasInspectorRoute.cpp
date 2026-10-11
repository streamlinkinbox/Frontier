//==============================================================================================================================================
//                                                     NATIVEGASINSPECTORROUTE.CPP
//==============================================================================================================================================
// 📦 Executed proof that the SHIPPED inspector opens the gas card — not the surface drawn directly, which
//    NativeGasCard.cpp already proves, but Frontier::InspectorPanel::Record() handed a real roster row and
//    a sheet whose Appearance is Gas.
//
// 💡 WHY A SECOND HARNESS RATHER THAN MORE CHECKS IN THE FIRST.
//    The first one asks "does the card draw correctly?". This one asks "does anything open it?" — and that
//    question has been answered wrong before. A surface can be perfect and unreachable: the header existed,
//    was captured, was parity-checked against the browser, and no selection in the editor could reach it.
//    So this drives the panel the editor drives, through the branch the editor takes, and clicks the
//    buttons the user clicks.
//
// 🔴 THE CLICKS ARE REAL CLICKS.
//    The card is a draw list, so InspectorPanel lays invisible buttons over the regions the painter
//    reports. Checking that the regions exist proves nothing; a button at the wrong offset is still a
//    region. So this presses the pointer at the centre of each one through ImGui's own event queue and
//    asserts the domain changed — the toggle flipped, the policy advanced, the one-shot stopped retiring,
//    the expand request was raised for the right instance.

#include "ControlPanel.h"
#include "InspectorPanel.h"
#include "OutlinerMetadata.h"
#include "GasCardSurface.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{
unsigned Checks = 0;
void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition) throw std::runtime_error(Claim);
    std::printf("  PASS  %s\n", Claim);
}
void Banner(const char* Title) { std::printf("\n%s\n", Title); }
}   // namespace

int main()
{
    std::printf("NativeGasInspectorRoute - the shipped inspector, opening the gas card\n");
    std::filesystem::create_directories("Exhibits/Gallery/GasInspectorRoute");

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

    constexpr int Width = 360, Height = 2700;
    IO.DisplaySize = { float(Width), float(Height) };
    IO.DisplayFramebufferScale = { 1, 1 };

    ControlPanel Controls;
    Controls.AssignFonts(Light, Light, Light, Light, Regular, Regular);
    InspectorPanel Inspector;
    Inspector.AssignControls(&Controls);

    // One roster row and one sheet, exactly as a host would hand them over.
    EditorInstance Roster[2] = {};
    std::snprintf(Roster[0].Label, sizeof(Roster[0].Label), "Camp Fire");
    Roster[0].Category = EditorInstanceCategory::Geometry;
    Roster[0].Visible  = true;
    std::snprintf(Roster[1].Label, sizeof(Roster[1].Label), "Flame Source");
    Roster[1].Category  = EditorInstanceCategory::Geometry;
    Roster[1].Component = true;                 // ② a child the outliner will not let you reparent loose
    Roster[1].Depth     = 1u;
    Inspector.AssignRoster(Roster, 2u);

    EditorSheet Sheet = {};
    Sheet.Appearance = EditorSheetAppearance::Gas;

    // One frame of the panel, with the pointer parked and optionally pressed.
    auto Tick = [&](float MouseX, float MouseY, bool Down, uint32_t Picked = 0u)
    {
        IO.AddMousePosEvent(MouseX, MouseY);
        IO.AddMouseButtonEvent(0, Down);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize({ float(Width), float(Height) });
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Light, 14.0f);
        Inspector.Record(&Roster[Picked], Picked, &Sheet, true);
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    // A press and a release over one spot, which is what an InvisibleButton answers to.
    auto Click = [&](ImVec4 Region, uint32_t Picked = 0u)
    {
        const float X = Region.x + Region.z * 0.5f, Y = Region.y + Region.w * 0.5f;
        Tick(X, Y, false, Picked);
        Tick(X, Y, true,  Picked);
        Tick(X, Y, false, Picked);
    };

    //---------------------------------------------------------------------------------------------------------
    Banner("The branch exists and the panel keeps a domain for the row");
    {
        Tick(-1.0f, -1.0f, false);
        Tick(-1.0f, -1.0f, false);
        GasCards::GasCardSubject* Domain = Inspector.QueryGasDomain(0u);
        Check(Domain != nullptr, "recording a Gas sheet makes the panel hold a domain for that instance");
        Check(Inspector.QueryGasDomain(7u) == nullptr, "and only for that instance - a slot is per row, not global");
        Check(!std::strcmp(Domain->Name, "Camp Fire"), "the card takes the row's name rather than carrying its own");
        Check(!Domain->Hidden, "and the row's visibility, so the two cannot disagree");
        Check(!Domain->Coupled, "🔴 two-way coupling starts off, as it does everywhere else in this programme");
        Check(Domain->Retire, "🔴 and a one-shot starts retiring - holding the fields is the deliberate choice");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("🔴 The hit targets are where the paint is - pressed, not inspected");
    {
        // The panel reports where it painted. Guessing the origin instead would be a second copy of the
        //    card's arithmetic, and the ident strip above it is exactly what a guess gets wrong.
        Tick(-1.0f, -1.0f, false);
        const GasCards::GasHitRegions Where = Inspector.QueryGasRegions();
        Check(Where.Tiles[0].z > 0.0f && Where.Preset.z > 0.0f && Where.Policy.z > 0.0f && Where.Open.z > 0.0f,
              "every control the card offers has a region, including ↗");
        Check(Where.Retire.z > 0.0f, "including the retirement toggle, because the domain starts as a one-shot");
        Check(Where.Tiles[0].y < Where.Preset.y && Where.Preset.y < Where.Policy.y && Where.Policy.y < Where.Open.y,
              "and they come down the column in the browser's order: tiles, preset, run policy, ↗ last");

        const ImVec4 All[6] = { Where.Tiles[0], Where.Preset, Where.Policy, Where.Retire, Where.AddEmitter,
                                Where.Open };
        for (int A = 0; A < 6; ++A) for (int B = A + 1; B < 6; ++B)
        {
            const bool Apart = All[A].y + All[A].w <= All[B].y + 0.01f || All[B].y + All[B].w <= All[A].y + 0.01f ||
                               All[A].x + All[A].z <= All[B].x + 0.01f || All[B].x + All[B].z <= All[A].x + 0.01f;
            Check(Apart, "no two hit targets overlap, so no button eats another's clicks");
        }
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Clicking the shipped card changes the shipped domain");
    {
        GasCards::GasHitRegions Where = Inspector.QueryGasRegions();

        const bool CoupledBefore = Inspector.QueryGasDomain(0u)->Coupled;
        Click(Where.Tiles[2]);
        Check(Inspector.QueryGasDomain(0u)->Coupled != CoupledBefore,
              "pressing the Pushes objects tile toggles two-way coupling on the panel's own domain");

        const uint32_t PolicyBefore = Inspector.QueryGasDomain(0u)->Policy;
        Click(Where.Policy);
        Check(Inspector.QueryGasDomain(0u)->Policy == (PolicyBefore + 1u) % 4u,
              "pressing Runs advances the policy - a draw-list select cycles, which is as close as this gets to a dropdown");

        Inspector.QueryGasDomain(0u)->Policy = 1u;   // back to the one-shot, where Retire is drawn
        Tick(-1.0f, -1.0f, false);
        Where = Inspector.QueryGasRegions();
        const bool RetireBefore = Inspector.QueryGasDomain(0u)->Retire;
        Click(Where.Retire);
        Check(Inspector.QueryGasDomain(0u)->Retire != RetireBefore,
              "🔴 and the retirement toggle is reachable, so keeping a one-shot resident is a decision somebody made");

        const uint32_t ChildrenBefore = Inspector.QueryGasDomain(0u)->ChildCount;
        Click(Where.AddEmitter);
        Check(Inspector.QueryGasDomain(0u)->ChildCount == ChildrenBefore + 1u,
              "② Add emitter adds a child to the domain the panel is holding");

        Tick(-1.0f, -1.0f, false);
        Where = Inspector.QueryGasRegions();
        Check(Inspector.ConsumeFluidEditorRequest() == kNoEditorInstance,
              "nothing has asked for the Fluid editor yet");
        Click(Where.Open);
        Check(Inspector.ConsumeFluidEditorRequest() == 0u, "↗ raises the request, naming the instance that asked");
        Check(Inspector.ConsumeFluidEditorRequest() == kNoEditorInstance,
              "and consuming it clears it - a flag that is never consumed fires for the rest of the session");
        Check(Inspector.ConsumeParticleEditorRequest() == kNoEditorInstance,
              "\xe2\x9c\x94\xef\xb8\x8f and a domain's \xe2\x86\x97 did not also ask for the particle editor");
        Check(!Inspector.QueryGasDomain(0u)->Emitter,
              "the domain's card knows it is a domain, which is what its Open band reads from");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("A child emitter takes the same branch");
    {
        Sheet.Appearance = EditorSheetAppearance::GasEmitter;
        Tick(-1.0f, -1.0f, false, 1u);
        Tick(-1.0f, -1.0f, false, 1u);
        Check(Inspector.QueryGasDomain(1u) != nullptr, "an emitter row opens the gas branch and gets a slot of its own");
        Check(!std::strcmp(Inspector.QueryGasDomain(1u)->Name, "Flame Source"),
              "named from its own row, because it is its own entity and not a group on the domain");

        // ③ The emitter's own expanded editor. Same button, same hit region, different page -- and the
        //    fluid request must stay silent, because a host that opened the fluid simulator for an emitter
        //    would be showing the domain's settings over again.
        GasCards::GasHitRegions Where = Inspector.QueryGasRegions();
        Check(Inspector.QueryGasDomain(1u)->Emitter,
              "\xe2\x9c\x94\xef\xb8\x8f an emitter's card knows it is an emitter");
        Click(Where.Open, 1u);
        Check(Inspector.ConsumeParticleEditorRequest() == 1u,
              "\xe2\x86\x97 on an emitter raises the ParticleEditor request, naming the row that asked");
        Check(Inspector.ConsumeFluidEditorRequest() == kNoEditorInstance,
              "\xf0\x9f\x94\xb4 and it does not raise the Fluid one -- one button, one destination, chosen by the row");
        Check(Inspector.ConsumeParticleEditorRequest() == kNoEditorInstance, "consuming clears it");

        Sheet.Appearance = EditorSheetAppearance::Gas;
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The outliner row says what the browser's row says");
    {
        RowReading Reading;
        Reading.Bounds[0] = 1.85f; Reading.Bounds[1] = 2.1f; Reading.Bounds[2] = 1.85f;
        Reading.QualityName = "Near";
        Reading.PolicyName  = "On trigger";

        char Line[96];
        OutlinerMetadata(RowPanel::Gas, Reading, Line, sizeof(Line));
        Check(std::strcmp(Line, "1.9\xc3\x97""2.1 m \xc2\xb7 Near \xc2\xb7 on trigger") == 0,
              "a gas domain reads '1.9x2.1 m - Near - on trigger', policy lowercased as the browser lowercases it");

        Reading.EmissionRate = 1.4f;
        Reading.Temperature  = 3.5f;
        OutlinerMetadata(RowPanel::GasEmitter, Reading, Line, sizeof(Line));
        Check(std::strcmp(Line, "1.40\xc3\x97 \xc2\xb7 3.5 K") == 0, "and an emitter reads '1.40x - 3.5 K'");

        Reading.Emitting = false;
        OutlinerMetadata(RowPanel::GasEmitter, Reading, Line, sizeof(Line));
        Check(std::strcmp(Line, "off \xc2\xb7 3.5 K") == 0,
              "a disabled emitter says 'off' rather than reporting a rate it is not emitting at");

        Check(PanelOfRow(EditorSheetAppearance::Gas, EditorGlyph::Fog, EditorInstanceCategory::Geometry)
              == RowPanel::Gas,
              "the panel comes from the sheet appearance, not the glyph - gas shares the local fog drawing");
        Check(PanelOfRow(EditorSheetAppearance::GasEmitter, EditorGlyph::Fog,
                         EditorInstanceCategory::Geometry) == RowPanel::GasEmitter,
              "and the emitter is its own panel, sharing that glyph with neither of them");
        Check(PanelOfRow(EditorSheetAppearance::LocalFog, EditorGlyph::Fog, EditorInstanceCategory::Geometry)
              == RowPanel::LocalFog, "while a real local fog row is untouched by any of this");
        Check(std::strcmp(CollectionTypeName(RowPanel::Gas), "Gas domains") == 0
              && std::strcmp(CollectionTypeName(RowPanel::GasEmitter), "Gas emitters") == 0,
              "and a folder holding them counts them under names of their own");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Capture");
    {
        Inspector.QueryGasDomain(0u)->Policy = 1u;
        Inspector.QueryGasDomain(0u)->Fired  = true;
        for (int Frame = 0; Frame < 3; ++Frame) Tick(-1.0f, -1.0f, false);

        std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 9);
        for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
            FrontierProof::Draw(List, Pixels.data(), Width, Height, { 0, 0 }, { 1, 1 }, ImTextureID(), {});
        Check(PngWriteCounterpart::WritePng("Exhibits/Gallery/GasInspectorRoute/InspectorRoute.png", Width, Height, 3,
                                            Pixels.data(), Width * 3) != 0,
              "the shipped inspector's own frame is written out, ident strip and all");
    }

    ImGui::DestroyContext();
    std::printf("\nPASS %u\n", Checks);
    return 0;
}
