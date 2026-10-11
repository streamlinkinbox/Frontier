//==============================================================================================================================================
//                                                            NATIVEWINDCARDS.CPP
//==============================================================================================================================================
// 📦 Executed proof: records the native wind binding card and its composite flow field from real ImGui draw commands.

#include "WindPanelSurface.h"
#include "CpuDraw.h"
#include "PngWriteCounterpart.h"
#include <imgui_internal.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Frontier;
using namespace Frontier::WindCards;

namespace
{

unsigned Checks = 0;

void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition)
    {
        throw std::runtime_error(Claim);
    }
}

float Speed(const WindComposite& Field, float X, float Z, float Time)
{
    float Velocity[2];
    Evaluate(Field, X, Z, Time, Velocity);
    return std::sqrt(Velocity[0] * Velocity[0] + Velocity[1] * Velocity[1]);
}

WindComposite Single(WindKind Kind, float Strength, float Bearing, float Radius)
{
    WindComposite Field;
    WindPart Part;
    std::snprintf(Part.Name, sizeof(Part.Name), "%s", KindName(Kind));
    Part.Kind     = Kind;
    Part.Strength = Strength;
    Part.Bearing  = Bearing;
    Part.Radius   = Radius;
    Adopt(Field, Part);
    return Field;
}

}   // namespace

int main()
{
    std::filesystem::create_directories("Exhibits/Gallery/WindNative");
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.IniFilename = nullptr;
    IO.DeltaTime = 1.0f / 60;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    ImFontConfig FaceConfig;
    std::snprintf(FaceConfig.Name, sizeof(FaceConfig.Name), "Sun reference / regular");
    ImFont* Face = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Regular.ttf", 14,
                                                &FaceConfig);
    ImGui::StyleColorsDark();

    int   Width = 420, Height = 1400;
    float Measured = 0;

    //  What the frame paints this pass; the lambdas below swap it between captures -----------------------
    enum class Subject { Binding, Flow };
    Subject        Shown = Subject::Binding;
    BindingValues  Values;
    WindComposite  Field = Resolve(7.0f, 250.0f, 0.25f);
    WindMotes      Motes;
    CanvasSettings Look;
    BindingHits    Hits;

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::BeginChild("##wind", { 0, float(Height) }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Face, 14.0f);

        ImDrawList* Draw = ImGui::GetWindowDrawList();
        const ImVec2 Origin{ 10.0f, 10.0f };
        const float  Card = float(Width) - 20.0f;
        float Tall = 0.0f;
        if (Shown == Subject::Binding)
        {
            Tall = BindingHeight(Face, Card, Values);
            Hits = PaintBinding(Draw, Face, Origin, Card, Values, Field, Motes, 0.0f);
        }
        else
        {
            Tall = VisualHeight(260.0f);
            Draw->AddRectFilled({ 0, 0 }, { float(Width), Origin.y + Tall + 20.0f }, CardTop);
            PaintVisual(Draw, Face, Origin, Card, 260.0f, Field, Look, Motes, 0.0f, CardTop);
        }

        ImGui::PopFont();
        ImGui::SetCursorScreenPos({ 0, 0 });
        ImGui::Dummy({ float(Width), Tall + 20.0f });
        Measured = ImGui::GetCursorPosY();
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    // Captures are primed off the clock so the flow strokes land in the same place on every run.
    auto Prime = [&](int Frames)
    {
        Seed(Motes);
        const bool Active = Shown == Subject::Binding ? (Values.Following && !Values.FieldHidden) : Look.Active;
        for (int Frame = 0; Frame < Frames; ++Frame)
        {
            Advance(Field, float(Width) - 68.0f, 180.0f, Motes, 1.0f / 30, Active, true);
        }
    };

    auto Capture = [&](const char* Name)
    {
        Height = 2600;
        Tick();
        Height = static_cast<int>(Measured) + 2;
        for (int Frame = 0; Frame < 3; ++Frame)
        {
            Tick();
        }
        std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 5);
        for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
        {
            FrontierProof::Draw(List, Pixels.data(), Width, Height, { 0, 0 }, { 1, 1 }, ImTextureID(), {});
        }
        const std::string Path = "Exhibits/Gallery/WindNative/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0, "capture written");
    };

    //-----------------------------------------------------------------------------------------------------
    // The authoring model — ResolveWind's defaults, clamps and fallback pair.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(Field.Wide == 1000.0f && Field.Deep == 1000.0f, "an unauthored slice is 1000 m square");
        Check(Field.Count == 2, "the fallback pair is a prevailing wind and a passing gust");
        Check(std::strcmp(Field.Parts[0].Name, "Prevailing wind") == 0, "the first fallback is the prevailing wind");
        Check(Field.Parts[0].Kind == WindKind::Directional, "the prevailing wind is directional");
        Check(Field.Parts[0].Strength == 7.0f && Field.Parts[0].Bearing == 250.0f, "it carries Speed and Bearing");
        Check(std::strcmp(Field.Parts[1].Name, "Passing gust") == 0, "the second fallback is the passing gust");
        Check(Field.Parts[1].Kind == WindKind::Gust, "the passing gust is a gust");
        Check(std::fabs(Field.Parts[1].Strength - 1.75f) < 1e-6f, "the gust is Speed times Gust, 7 x .25");
        Check(Field.Parts[1].Radius == 450.0f, "the gust reaches 450 m");
        Check(Field.Parts[1].Frequency == 0.3f, "the gust pulses at .3 Hz");

        WindComposite Clamps;
        WindPart Wild;
        Wild.X = -9000.0f; Wild.Z = 9000.0f; Wild.Radius = 1.0f;
        Wild.Strength = 500.0f; Wild.Bearing = 940.0f; Wild.Frequency = 40.0f;
        Adopt(Clamps, Wild);
        Check(Clamps.Parts[0].X == -500.0f && Clamps.Parts[0].Z == 500.0f, "placement clamps to the slice half-size");
        Check(Clamps.Parts[0].Radius == 10.0f, "radius clamps up to 10 m");
        Check(Clamps.Parts[0].Strength == 100.0f, "strength clamps to 100 m/s");
        Check(Clamps.Parts[0].Bearing == 360.0f, "bearing clamps to 360 deg");
        Check(Clamps.Parts[0].Frequency == 5.0f, "frequency clamps to 5 Hz");
    }

    //-----------------------------------------------------------------------------------------------------
    // EvaluateWind — one branch at a time, read against the reference arithmetic.
    //-----------------------------------------------------------------------------------------------------
    {
        const WindComposite East = Single(WindKind::Directional, 7.0f, 90.0f, 260.0f);
        float Velocity[2];
        Evaluate(East, 0.0f, 0.0f, 0.0f, Velocity);
        Check(std::fabs(Velocity[0] - 7.0f) < 1e-5f, "a 90 deg directional blows along +X at full strength");
        Check(std::fabs(Velocity[1]) < 1e-5f, "a 90 deg directional has no Z component");
        Evaluate(East, 4000.0f, -3000.0f, 0.0f, Velocity);
        Check(std::fabs(Velocity[0] - 7.0f) < 1e-5f, "a directional ignores radius and reaches the whole slice");

        const WindComposite Prevailing = Single(WindKind::Directional, 7.0f, 250.0f, 260.0f);
        Evaluate(Prevailing, 0.0f, 0.0f, 0.0f, Velocity);
        Check(std::fabs(Velocity[0] + 6.5778f) < 1e-3f, "250 deg resolves to -6.58 m/s on X");
        Check(std::fabs(Velocity[1] - 2.3941f) < 1e-3f, "250 deg resolves to +2.39 m/s on Z");
        Check(std::fabs(Speed(Prevailing, 0.0f, 0.0f, 0.0f) - 7.0f) < 1e-4f, "bearing only rotates, never scales");
    }

    {
        const WindComposite Gust = Single(WindKind::Gust, 10.0f, 90.0f, 400.0f);
        Check(std::fabs(Speed(Gust, 0.0f, 0.0f, 0.0f) - 6.5f) < 1e-4f, "a gust sits at .65 of strength at phase 0");
        const float Crest = 1.0f / (4.0f * 0.3f);
        Check(std::fabs(Speed(Gust, 0.0f, 0.0f, Crest) - 10.0f) < 1e-3f, "the gust crests at full strength");
        const float Trough = 3.0f / (4.0f * 0.3f);
        Check(std::fabs(Speed(Gust, 0.0f, 0.0f, Trough) - 3.0f) < 1e-3f, "the gust troughs at .30 of strength");
        Check(Speed(Gust, 400.0f, 0.0f, Crest) < 1e-5f, "the gust is spent at its radius");
        Check(std::fabs(Speed(Gust, 200.0f, 0.0f, Crest) - 5.625f) < 1e-3f, "half a radius out leaves .5625 falloff");
    }

    {
        const WindComposite Vortex = Single(WindKind::Tornado, 22.0f, 90.0f, 300.0f);
        float Velocity[2];
        Evaluate(Vortex, 150.0f, 0.0f, 0.0f, Velocity);
        Check(Velocity[1] > 0.0f && std::fabs(Velocity[0]) < Velocity[1], "a vortex is mostly tangential");
        Check(std::fabs(Velocity[0] / Velocity[1] + 0.18f) < 1e-5f, "the inward lean is a fixed .18 of the tangent");
        Check(Speed(Vortex, 0.0f, 0.0f, 0.0f) < 1e-9f, "the vortex eye is still");
        Check(std::fabs(Speed(Vortex, 50.0f, 0.0f, 0.0f) - Speed(Vortex, 50.0f, 0.0f, 9.0f)) < 1e-6f,
              "a vortex does not pulse with the clock");

        const WindComposite Burst = Single(WindKind::Radial, 12.0f, 0.0f, 300.0f);
        Evaluate(Burst, 150.0f, 0.0f, 0.0f, Velocity);
        Check(Velocity[0] > 0.0f && std::fabs(Velocity[1]) < 1e-6f, "a radial burst blows straight outward");
        Evaluate(Burst, -150.0f, 0.0f, 0.0f, Velocity);
        Check(Velocity[0] < 0.0f, "the opposite side blows the opposite way");
        const float Core = Speed(Burst, 50.0f, 0.0f, 0.0f), Mid = Speed(Burst, 150.0f, 0.0f, 0.0f);
        Check(Core < Mid, "the core ramp softens the centre of a radial");
    }

    {
        WindComposite Sum;
        WindPart A; A.Kind = WindKind::Directional; A.Strength = 5.0f; A.Bearing = 90.0f;
        WindPart B; B.Kind = WindKind::Directional; B.Strength = 3.0f; B.Bearing = 90.0f;
        Adopt(Sum, A);
        Adopt(Sum, B);
        Check(std::fabs(Speed(Sum, 0.0f, 0.0f, 0.0f) - 8.0f) < 1e-5f, "enabled components sum linearly");
        Sum.Parts[1].Enabled = false;
        Check(std::fabs(Speed(Sum, 0.0f, 0.0f, 0.0f) - 5.0f) < 1e-5f, "a disabled component contributes nothing");
        Sum.Parts[1].Enabled = true;
        Sum.Parts[1].Strength = 0.0f;
        Check(std::fabs(Speed(Sum, 0.0f, 0.0f, 0.0f) - 5.0f) < 1e-5f, "a zero-strength component is skipped");
    }

    //-----------------------------------------------------------------------------------------------------
    // The speed palette: hsl(190 -> 40, 48 %, 32 % -> 60 %) across nought to thirty metres a second.
    //-----------------------------------------------------------------------------------------------------
    {
        const ImU32 Still = SpeedInk(0.0f, 1.0f), Full = SpeedInk(30.0f, 1.0f), Over = SpeedInk(90.0f, 1.0f);
        Check(Still == IM_COL32(42, 108, 121, 255), "still air is the teal end, hsl(190,48%,32%)");
        Check(Full == IM_COL32(202, 169, 104, 255), "thirty metres a second is the gold end, hsl(40,48%,60%)");
        Check(Full == Over, "the palette saturates past thirty rather than wrapping the hue");
        Check(Blend(Still, 0.5f) == IM_COL32(42, 108, 121, 128), "the heat layer carries the canvas .5 alpha");
        Check(SpeedInk(15.0f, 1.0f) == IM_COL32(70, 174, 61, 255), "the midpoint is hsl(115,48%,46%), the green belt");
    }

    //-----------------------------------------------------------------------------------------------------
    // Card geometry, read off Editor.css and WindPanel.css.
    //-----------------------------------------------------------------------------------------------------
    {
        BindingValues Bound;
        Bound.FieldName = "Coastal wind";
        Bound.Assigned  = true;
        const float Tall = BindingHeight(Face, 400.0f, Bound);
        const float Stack = 23 + 16 + 28 + std::round(13.0f * 1.2f) + 10 + 32 + 18
                          + WideButtonHeight(13.0f) + 14 + VisualHeight(180.0f) + 22;
        // DM Sans sets `normal` line-height to 1.302 em, and the note wraps after "preview;" in a 352 px
        //    column, so the two note lines are 2 x 11 x 1.302 px tall.
        Check(std::fabs(Stacked(Face, 11.0f, 352.0f, "x") - 11.0f * 1.302f) < 0.01f,
              "one 11 px line box is 1.302 em tall");
        Check(std::fabs(Tall - (Stack + 2.0f * 11.0f * 1.302f)) < 0.51f,
              "the bound card stacks padding, title, select, button, visual and two note lines");
        Check(VisualHeight(180.0f) == 180 + 12 + 10 + 6 + 10, "the visual is canvas, legend and caption");
        Check(WideButtonHeight(13.0f) == 40.0f, "a 12 px padded wide button is 40 px tall");

        BindingValues Empty;
        Check(BindingHeight(Face, 400.0f, Empty) < Tall - 200.0f, "an unbound card drops the preview entirely");
        Check(std::strcmp(SelectCaption(Empty), "None \xc2\xb7 still air") == 0, "an unbound select reads still air");
        BindingValues Lost;
        Lost.Assigned = true;
        Check(std::strcmp(SelectCaption(Lost), "Missing field \xc2\xb7 choose another") == 0,
              "a dangling id offers another field");
        Check(std::strcmp(SelectCaption(Bound), "Coastal wind") == 0, "a resolved field shows its name");
    }

    //-----------------------------------------------------------------------------------------------------
    // Captures.
    //-----------------------------------------------------------------------------------------------------
    Shown = Subject::Binding;
    Field = Resolve(7.0f, 250.0f, 0.25f);

    Values = BindingValues{};
    Values.FieldName = "Coastal wind";
    Values.Assigned  = true;
    Values.Following = true;
    Prime(90);
    Capture("BindingBound");
    {
        Check(Hits.Select.z - Hits.Select.x == 400.0f - 48.0f, "the select spans the card's inner width");
        Check(std::fabs((Hits.Select.w - Hits.Select.y) - 32.0f) < 0.01f, "the select is 32 px tall");
        Check(Hits.Edit.z > Hits.Edit.x, "a bound card mounts the editor shortcut");
        Check(std::fabs((Hits.Edit.w - Hits.Edit.y) - 40.0f) < 0.01f, "the editor shortcut is 40 px tall");
        Check(Hits.Edit.y > Hits.Select.w, "the shortcut sits below the select");
    }

    Values.Following = false;
    Prime(90);
    Capture("BindingUnfollowed");
    {
        Check(Hits.Edit.z > Hits.Edit.x, "the shortcut survives Follow Wind being switched off");
    }

    // What the fog and clouds panels actually mount: Wind Source entry 0 is the shared Global wind, so the
    //    native select always names a field and the still-air blank never arises from that sheet.
    Values = BindingValues{};
    Values.FieldName = "Global wind";
    Values.Assigned  = true;
    Values.Following = true;
    Prime(90);
    Capture("BindingGlobal");
    {
        char Note[224];
        BindingNote(Values, Note, sizeof(Note));
        Check(std::strstr(Note, "Using all components of Global wind.") != nullptr,
              "the note names the bound field rather than a placeholder");
        Check(Hits.Edit.z > Hits.Edit.x, "the global binding still offers the editor shortcut");
    }

    Values = BindingValues{};
    Values.Assigned = true;
    Prime(0);
    Capture("BindingMissing");
    {
        Check(Hits.Edit.z == 0.0f, "a dangling binding mounts no editor shortcut");
    }

    Values = BindingValues{};
    Prime(0);
    Capture("BindingNone");

    Shown = Subject::Flow;
    Look = CanvasSettings{};

    Field = Resolve(7.0f, 250.0f, 0.25f);
    Prime(90);
    Capture("FlowPrevailing");

    Look.Vectors = true;
    Prime(90);
    Capture("FlowVectors");
    Look.Vectors = false;

    Field = Single(WindKind::Tornado, 22.0f, 90.0f, 320.0f);
    Prime(120);
    Capture("FlowTornado");

    Field = Single(WindKind::Radial, 14.0f, 0.0f, 340.0f);
    Prime(120);
    Capture("FlowRadial");

    Field = Single(WindKind::Gust, 18.0f, 135.0f, 420.0f);
    Prime(60);
    Capture("FlowGust");

    Field = Resolve(7.0f, 250.0f, 0.25f);
    Look.Active = false;
    Prime(0);
    Capture("FlowDisabled");
    Look.Active = true;

    //-----------------------------------------------------------------------------------------------------
    // The motes drift with the field and wrap at the slice edges rather than piling up.
    //-----------------------------------------------------------------------------------------------------
    {
        WindMotes Walk;
        Seed(Walk);
        Check(Walk.X[0] == 0.0f && Walk.Y[0] == 0.0f, "the mote seed is deterministic, not random");
        Check(std::fabs(Walk.X[1] - 73.0f / 151.0f) < 1e-6f, "the X stride is 73 over 151");
        Check(std::fabs(Walk.Y[1] - 43.0f / 149.0f) < 1e-6f, "the Y stride is 43 over 149");

        const WindComposite East = Single(WindKind::Directional, 10.0f, 90.0f, 260.0f);
        const float Before = Walk.X[5];
        Advance(East, 352.0f, 180.0f, Walk, 1.0f / 30, true, true);
        Check(Walk.X[5] > Before, "an eastward field carries the motes along +X");
        Check(std::fabs(Walk.X[5] - Before - 10.0f * (1.0f / 30) * 8.0f / 1000.0f) < 1e-6f,
              "the drift is eight times real time over the slice width");

        Walk.X[5] = 0.999f;
        for (int Frame = 0; Frame < 20; ++Frame) Advance(East, 352.0f, 180.0f, Walk, 1.0f / 30, true, true);
        Check(Walk.X[5] >= 0.0f && Walk.X[5] < 1.0f, "motes wrap round the slice instead of leaving it");

        const float Held = Walk.X[5];
        Advance(East, 352.0f, 180.0f, Walk, 1.0f / 30, false, true);
        Check(Walk.X[5] == Held, "a disabled field freezes the motes");
        Advance(East, 352.0f, 180.0f, Walk, 1.0f / 30, true, false);
        Check(Walk.X[5] == Held, "a paused preview freezes the motes");

        const float Clock = Walk.Time;
        Advance(East, 352.0f, 180.0f, Walk, 4.0f, true, true);
        Check(std::fabs(Walk.Time - Clock - 0.05f) < 1e-6f, "a long frame is capped at fifty milliseconds");
    }

    ImGui::DestroyContext();
    std::printf("PASS %u checks: native wind binding card, composite flow field and four component kinds.\n",
                Checks);
    return 0;
}
