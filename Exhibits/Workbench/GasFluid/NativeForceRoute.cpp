//==============================================================================================================================================
//                                                        NATIVEFORCEROUTE.CPP
//==============================================================================================================================================
// 📦 Executed proof that the SHIPPED inspector opens the force field panel — not the surface drawn
//    directly, which NativeForceCard.cpp already proves, but Frontier::InspectorPanel::Record() handed a
//    real roster row and a sheet whose Appearance is ForceFields.
//
// 💡 WHY A SECOND HARNESS RATHER THAN MORE CHECKS IN THE FIRST.
//    The first asks "does the panel draw correctly?". This asks "does anything open it?" — a question this
//    repository has answered wrong before: a surface can be perfect, captured, parity-checked, and
//    unreachable from any selection in the editor.
//
// 🔴 THE CLICKS ARE REAL CLICKS, AND THE LIST THEY EDIT IS ONE LIST.
//    The panel is a draw list with invisible buttons laid over the regions the painter reports. Pressing
//    "+ Gravity" must add a field that reaches everywhere; pressing Remove must delete the card that was
//    pressed and not the one that shifted into its place. Both have a wrong answer that still looks right
//    in a screenshot, which is the whole reason this file exists.

#include "ControlPanel.h"
#include "InspectorPanel.h"
#include "OutlinerMetadata.h"
#include "ForceFieldCardSurface.h"
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
    if (!Condition)
    {
        std::fprintf(stderr, "  FAIL  %s\n", Claim);
        throw std::runtime_error(Claim);
    }
    std::printf("  PASS  %s\n", Claim);
}
void Banner(const char* Title) { std::printf("\n%s\n", Title); }
}   // namespace

int main()
{
    std::printf("NativeForceRoute - the shipped inspector, opening the force field panel\n");
    std::filesystem::create_directories("Exhibits/Gallery/ForceFieldRoute");

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

    // The panel grows a card per field, so the harness window is tall — a region painted past the bottom
    //    is not clickable, and a proof that cannot reach its own buttons proves nothing.
    constexpr int Width = 360, Height = 4200;
    IO.DisplaySize = { float(Width), float(Height) };
    IO.DisplayFramebufferScale = { 1, 1 };

    ControlPanel Controls;
    Controls.AssignFonts(Light, Light, Light, Light, Regular, Regular);
    InspectorPanel Inspector;
    Inspector.AssignControls(&Controls);

    EditorInstance Roster[2] = {};
    std::snprintf(Roster[0].Label, sizeof(Roster[0].Label), "Force fields");
    Roster[0].Category = EditorInstanceCategory::Geometry;
    Roster[0].Visible  = true;
    std::snprintf(Roster[1].Label, sizeof(Roster[1].Label), "Camp Fire");
    Roster[1].Category = EditorInstanceCategory::Geometry;
    Roster[1].Visible  = true;
    Inspector.AssignRoster(Roster, 2u);

    EditorSheet Sheet = {};
    Sheet.Appearance = EditorSheetAppearance::ForceFields;

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

    auto Click = [&](ImVec4 Region, uint32_t Picked = 0u)
    {
        const float X = Region.x + Region.z * 0.5f, Y = Region.y + Region.w * 0.5f;
        Tick(X, Y, false, Picked);
        Tick(X, Y, true,  Picked);
        Tick(X, Y, false, Picked);
    };

    //---------------------------------------------------------------------------------------------------------
    Banner("The branch exists, and the panel holds one set of fields");
    {
        Tick(-1.0f, -1.0f, false);
        Tick(-1.0f, -1.0f, false);
        Check(Inspector.QueryForceFields().Count == 0u,
              "a fresh scene has no fields - the panel invents nothing to look busy");
        const ForceCards::ForceHitRegions& Where = Inspector.QueryForceRegions();
        for (uint32_t At = 0u; At < uint32_t(ForceFieldKind::Count); ++At)
            Check(Where.Add[At].z > 0.0f,
                  (std::string("every kind can be added: ") + FactsOf(ForceFieldKind(At)).Name).c_str());
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Adding a field takes the kind's own facts, not a blank default");
    {
        ForceCards::ForceHitRegions Where = Inspector.QueryForceRegions();
        Click(Where.Add[uint32_t(ForceFieldKind::Gravity)]);
        Check(Inspector.QueryForceFields().Count == 1u, "pressing + Gravity adds exactly one field");
        const ForceField& Fresh = Inspector.QueryForceFields().Fields[0];
        Check(Fresh.Kind == ForceFieldKind::Gravity, "and it is a gravity field");
        // 🔴 This is the one that was wrong in the browser first. An 8 m gravity ball is the bug the
        //    taxonomy's Reaches exists to prevent, and the add button is where it would come back.
        Check(Fresh.Reaches == ForceReach::Everywhere,
              "🔴 it arrives reaching everywhere, not as an 8 m ball somebody has to notice");
        Check(!std::strcmp(Fresh.Name, "Gravity"), "named from the kind, so the card is not titled nothing");
        Check(Fresh.Enabled, "and awake, because a control you must then switch on is a control you missed");

        Tick(-1.0f, -1.0f, false);
        Where = Inspector.QueryForceRegions();
        Click(Where.Add[uint32_t(ForceFieldKind::Prevailing)]);
        Check(Inspector.QueryForceFields().Count == 2u, "a second kind adds a second field");
        Check(Inspector.QueryForceFields().Fields[1].Reaches == ForceReach::Everywhere,
              "prevailing wind reaches everywhere too, for its own reason");
        Tick(-1.0f, -1.0f, false);
        Where = Inspector.QueryForceRegions();
        Click(Where.Add[uint32_t(ForceFieldKind::Gust)]);
        Check(Inspector.QueryForceFields().Fields[2].Reaches == ForceReach::Sphere,
              "a gust front arrives bounded, which is what gives it a centre and a radius");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("🔴 The hit targets are where the paint is - pressed, not inspected");
    {
        Tick(-1.0f, -1.0f, false);
        const ForceCards::ForceHitRegions& Where = Inspector.QueryForceRegions();
        const uint32_t Count = Inspector.QueryForceFields().Count;
        for (uint32_t At = 0u; At < Count; ++At)
        {
            Check(Where.Enabled[At].z > 0.0f && Where.Remove[At].z > 0.0f && Where.Strength[At].z > 0.0f,
                  "every card offers its toggle, its strength track and its remove button");
            Check(Where.Enabled[At].y < Where.Strength[At].y && Where.Strength[At].y < Where.Remove[At].y,
                  "down the card in the browser's order: enabled, strength, remove last");
        }
        for (uint32_t A = 0u; A < Count; ++A) for (uint32_t B = A + 1u; B < Count; ++B)
        {
            const ImVec4& One = Where.Remove[A];
            const ImVec4& Two = Where.Remove[B];
            Check(One.y + One.w <= Two.y + 0.01f || Two.y + Two.w <= One.y + 0.01f,
                  "no two cards' remove buttons overlap, so no card eats another's clicks");
        }
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Clicking the shipped panel changes the shipped fields");
    {
        Tick(-1.0f, -1.0f, false);
        ForceCards::ForceHitRegions Where = Inspector.QueryForceRegions();
        const bool Before = Inspector.QueryForceFields().Fields[0].Enabled;
        Click(Where.Enabled[0]);
        Check(Inspector.QueryForceFields().Fields[0].Enabled != Before,
              "pressing a card's toggle puts that field to sleep");
        Click(Where.Enabled[0]);
        Check(Inspector.QueryForceFields().Fields[0].Enabled == Before, "and wakes it again");

        // Dragging the far right of the strength track takes it to the ceiling. A draw-list slider has no
        //    thumb to grab, so the track is the control.
        const ImVec4 Track = Where.Strength[0];
        Tick(Track.x + Track.z - 1.0f, Track.y + Track.w * 0.5f, false);
        Tick(Track.x + Track.z - 1.0f, Track.y + Track.w * 0.5f, true);
        Tick(Track.x + Track.z - 1.0f, Track.y + Track.w * 0.5f, true);
        Check(Inspector.QueryForceFields().Fields[0].Strength > 19.0f,
              "dragging the strength track to its right end reads the ceiling, 20");
        Tick(Track.x + 1.0f, Track.y + Track.w * 0.5f, true);
        Check(Inspector.QueryForceFields().Fields[0].Strength < -19.0f,
              "🔴 and to its left end reads -20 - strength is signed, which is how a repulsor is an attractor");
        Tick(-1.0f, -1.0f, false);
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("🔴 Remove deletes the card that was pressed");
    {
        Tick(-1.0f, -1.0f, false);
        ForceCards::ForceHitRegions Where = Inspector.QueryForceRegions();
        Check(Inspector.QueryForceFields().Count == 3u, "three fields are on the page");

        // The cards are grouped by contribution, so index 1 in the list is not the second card down. The
        //    proof reads which field the pressed region belongs to rather than assuming.
        const ForceFieldKind Doomed = Inspector.QueryForceFields().Fields[1].Kind;
        const ForceFieldKind Kept0  = Inspector.QueryForceFields().Fields[0].Kind;
        const ForceFieldKind Kept2  = Inspector.QueryForceFields().Fields[2].Kind;
        Click(Where.Remove[1]);
        Check(Inspector.QueryForceFields().Count == 2u, "pressing Remove deletes one field, not two");
        Check(Inspector.QueryForceFields().Fields[0].Kind == Kept0, "the field above it is untouched");
        Check(Inspector.QueryForceFields().Fields[1].Kind == Kept2,
              "the field below it closes the gap, in order");
        Check(Inspector.QueryForceFields().Fields[0].Kind != Doomed &&
              Inspector.QueryForceFields().Fields[1].Kind != Doomed,
              "and the pressed field is the one that is gone");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The fields belong to the scene, not to the row that opened them");
    {
        // 🔴 Gas domains get a slot per instance, because a domain IS the instance. A force field is not:
        //    it is a thing in the world. Selecting a different row must show the same list.
        const uint32_t Count = Inspector.QueryForceFields().Count;
        Tick(-1.0f, -1.0f, false, 1u);
        Tick(-1.0f, -1.0f, false, 1u);
        Check(Inspector.QueryForceFields().Count == Count,
              "🔴 a second row showing the force field page shows the same fields, not a fresh set");
        Tick(-1.0f, -1.0f, false, 0u);
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("The outliner row says what the browser's row says");
    {
        Check(PanelOfRow(EditorSheetAppearance::ForceFields, EditorGlyph::Wind,
                         EditorInstanceCategory::Geometry) == RowPanel::ForceField,
              "the panel comes from the sheet appearance - force fields share the wind glyph, not its page");
        Check(PanelOfRow(EditorSheetAppearance::Wind, EditorGlyph::Wind, EditorInstanceCategory::Geometry)
              == RowPanel::Wind,
              "🔴 and a real wind row is untouched by any of this - Wind was not renamed, it was placed under");
        Check(std::strcmp(CollectionTypeName(RowPanel::ForceField), "Force fields") == 0,
              "a folder holding them counts them under a name of their own");

        RowReading Reading;
        Reading.FlowFields = 3u; Reading.AccelerateFields = 2u;
        char Line[96];
        OutlinerMetadata(RowPanel::ForceField, Reading, Line, sizeof(Line));
        Check(std::strcmp(Line, "3 flow \xc2\xb7 2 accel") == 0,
              "a scene with no damping reads '3 flow - 2 accel'");

        Reading.DampFields = 1u;
        OutlinerMetadata(RowPanel::ForceField, Reading, Line, sizeof(Line));
        Check(std::strcmp(Line, "3 flow \xc2\xb7 2 accel \xc2\xb7 1 damp") == 0,
              "and the damp term appears only when there is one to report");

        Reading.OwnedFields = 2u;
        OutlinerMetadata(RowPanel::ForceField, Reading, Line, sizeof(Line));
        Check(std::strcmp(Line, "3 flow \xc2\xb7 2 accel \xc2\xb7 1 damp \xc2\xb7 2 black hole") == 0,
              "\xe2\x9c\x94\xef\xb8\x8f per-system attractors are counted separately, because the panel does not own them");

        // 🔴 The row counts by what a field CONTRIBUTES. Six fields is not a useful figure; these three
        //    numbers are, because they cost differently and behave differently.
        Reading = RowReading{};
        OutlinerMetadata(RowPanel::ForceField, Reading, Line, sizeof(Line));
        Check(std::strcmp(Line, "0 flow \xc2\xb7 0 accel") == 0,
              "an empty scene still reads its two numbers rather than going blank");
    }

    //---------------------------------------------------------------------------------------------------------
    Banner("Capture");
    {
        ForceCards::ForceCardSubject& Fields = Inspector.QueryForceFields();
        Fields = ForceCards::ForceCardSubject{};
        const ForceFieldKind Shown[4] = { ForceFieldKind::Prevailing, ForceFieldKind::Gust,
                                          ForceFieldKind::Gravity, ForceFieldKind::Drag };
        for (ForceFieldKind Kind : Shown)
        {
            ForceField& One = Fields.Fields[Fields.Count++];
            One = ForceField{};
            One.Kind    = Kind;
            One.Reaches = FactsOf(Kind).Reaches;
            std::snprintf(One.Name, sizeof(One.Name), "%s", FactsOf(Kind).Name);
        }
        for (int Frame = 0; Frame < 3; ++Frame) Tick(-1.0f, -1.0f, false);

        std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 9);
        for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
            FrontierProof::Draw(List, Pixels.data(), Width, Height, { 0, 0 }, { 1, 1 }, ImTextureID(), {});
        Check(PngWriteCounterpart::WritePng("Exhibits/Gallery/ForceFieldRoute/InspectorRoute.png", Width,
                                            Height, 3, Pixels.data(), Width * 3) != 0,
              "the shipped inspector's own frame is written out, ident strip and all");
    }

    ImGui::DestroyContext();
    std::printf("\nPASS %u\n", Checks);
    return 0;
}
