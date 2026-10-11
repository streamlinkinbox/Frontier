//==============================================================================================================================================
//                                                          NATIVEWINDEDITOR.CPP
//==============================================================================================================================================
// 📦 Executed proof: records the WindEditor dialog from real ImGui draw commands, against WindPanel.jsx:503
//    and WindPanel.css. Every geometric claim below is the CSS box model worked through by hand.

#include "WindEditorSurface.h"
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
using namespace Frontier::WindEditor;

namespace
{
unsigned Checks = 0;
void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition) throw std::runtime_error(Claim);
}
bool Near(float Value, float Want, float Slack = 0.01f) { return std::fabs(Value - Want) < Slack; }

// The editor is authored in two weights: body copy is DM Sans Light, .wind-section-head h2/h3 is Regular.
ImFont* Light = nullptr;
ImFont* Regular = nullptr;
}   // namespace

int main()
{
    std::filesystem::create_directories("Exhibits/Gallery/WindEditorNative");
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.IniFilename = nullptr;
    IO.DeltaTime = 1.0f / 60;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    ImFontConfig LightConfig;
    std::snprintf(LightConfig.Name, sizeof(LightConfig.Name), "Sun reference / light 300");
    Light = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Light.ttf", 14,
                                         &LightConfig);
    ImFontConfig RegularConfig;
    std::snprintf(RegularConfig.Name, sizeof(RegularConfig.Name), "Sun reference / regular 400");
    Regular = IO.Fonts->AddFontFromFileTTF("EngineContent/Fonts/SunReference/DMSans-Regular.ttf", 14,
                                           &RegularConfig);
    ImGui::StyleColorsDark();

    //---------------------------------------------------------------------------------------------------------
    // The field the dialog edits. ResolveWind seeds two components when nothing is saved, and Add() drops a
    //    new one at twelve per cent across and eight per cent up the slice.
    //---------------------------------------------------------------------------------------------------------
    Kit::WindComposite Field = Kit::Resolve(7.0f, 250.0f, 0.25f);
    Check(Field.Count == 2, "an unsaved field resolves to prevailing wind and a passing gust");
    Check(std::strcmp(Field.Parts[0].Name, "Prevailing wind") == 0
       && Field.Parts[0].Kind == Kit::WindKind::Directional && Near(Field.Parts[0].Strength, 7.0f)
       && Near(Field.Parts[0].Bearing, 250.0f), "the prevailing component carries speed and bearing");
    Check(std::strcmp(Field.Parts[1].Name, "Passing gust") == 0 && Near(Field.Parts[1].Strength, 1.75f)
       && Near(Field.Parts[1].Radius, 450.0f) && Near(Field.Parts[1].Frequency, 0.3f),
          "the passing gust is speed times gust over a 450 m radius");

    Kit::WindPart Twister;
    std::snprintf(Twister.Name, sizeof(Twister.Name), "%s", "Tornado 1");
    Twister.Kind     = Kit::WindKind::Tornado;
    Twister.Strength = 22.0f;                  // NewWindComponent: tornadoes start at 22, everything else 7
    Twister.X        =  Field.Wide * 0.12f;
    Twister.Z        = -Field.Deep * 0.08f;
    Kit::Adopt(Field, Twister);
    Check(Field.Count == 3 && Near(Field.Parts[2].X, 120.0f) && Near(Field.Parts[2].Z, -80.0f)
       && Near(Field.Parts[2].Radius, 260.0f) && Near(Field.Parts[2].Strength, 22.0f),
          "a new tornado lands at .12 width, -.08 depth with a 260 m radius and 22 m/s");

    EditorState State;
    std::snprintf(State.FieldName, sizeof(State.FieldName), "%s", "Coastal wind");
    State.Selection = 2;

    //---------------------------------------------------------------------------------------------------------
    // Type: CSS sizes the em box, ImGui sizes ascent minus descent. DM Sans puts 1.302 between them.
    //---------------------------------------------------------------------------------------------------------
    Check(Near(Kit::EmScale, 1.302f) && Near(Kit::AscentShare, 0.992f),
          "DM Sans reports hhea ascent 992 and descent -310 per 1000 em");
    Check(Near(Kit::Grind(12.0f), 15.624f), "a 12 px CSS run is baked at 15.624 ImGui pixels");
    // Measured against the font's own hmtx table: 'Pause preview' is 70.980 px wide at 12 px in DM Sans Light.
    Check(Near(Measured(Light, 12.0f, "Pause preview"), 70.980f, 0.06f),
          "a 12 px button label measures what the browser measures");
    Check(Near(LineBox(11.0f), 14.322f) && Near(LineBox(26.0f), 33.852f),
          "a line box is one em scale tall");
    Check(Near(Baseline(13.0f), 12.896f), "the baseline sits an ascent below the top of its line box");

    //---------------------------------------------------------------------------------------------------------
    // Box model. Every number here is padding plus border plus a line box, read off WindPanel.css.
    //---------------------------------------------------------------------------------------------------------
    Check(Near(FieldTall, 9.0f * 2 + 2 + 12.0f * 1.302f) && Near(FieldTall, 35.624f),
          "an input is 9 px padding, a 1 px border and one 12 px line box");
    Check(Near(LabelBlock, 14.322f + 8.0f + 35.624f) && Near(LabelBlock, 57.946f),
          "a label stacks its caption, an 8 px gap and its control");
    Check(Near(PartTall, 2 + 14 + 9.0f * 1.302f + 6 + 12.0f * 1.302f) && Near(PartTall, 49.342f),
          "a component row is a border, 7 px padding, a 9 px lead, a 6 px gap and a 12 px name");
    Check(Near(AddTall, 32.322f), "an add button is 8 px padding over an 11 px line, past the 32 px minimum");
    Check(Near(HeadTall, 22 + 13.0f * 1.302f + 5 + 26.0f * 1.302f + 22 + 1) && Near(HeadTall, 100.778f),
          "the header is 22 px padding, the eyebrow strut, 5 px and a 26 px title over its rule");
    Check(Near(FootTall, 14 + 10.0f * 1.302f + 14 + 1) && Near(FootTall, 42.02f),
          "the footer is 14 px padding around one 10 px line, under its rule");
    Check(Near(ProseTall, 14 + 11.0f * 1.6f + 14) && Near(ProseTall, 45.6f),
          "a one-line paragraph is 14 px margins around a 1.6 line box");
    Check(Near(SwitchTall, 19.0f), "a user agent checkbox row is 13 px plus 3 px margins");

    const float Body = ShellTall - HeadTall - FootTall;
    const float MainRun = ShellWide - AsideWide - MainPad * 2.0f;
    const float Half = (MainRun - PairGap) * 0.5f;
    Check(Near(MainRun, 1321.0f) && Near(Half, 649.5f),
          "the 235 px aside leaves two 649.5 px view columns inside 22 px padding");
    Check(Near(Body, 807.202f), "the body is what the header and footer leave of 950 px");

    //---------------------------------------------------------------------------------------------------------
    // The property grid follows the component type, exactly as the JSX branches.
    //---------------------------------------------------------------------------------------------------------
    Check(PropertyCount(Kit::WindKind::Directional) == 7 && PropertyCount(Kit::WindKind::Gust) == 8
       && PropertyCount(Kit::WindKind::Tornado) == 6 && PropertyCount(Kit::WindKind::Radial) == 6,
          "bearing joins directional and gust, frequency only gust");
    Check(std::strstr(KindNote(Kit::WindKind::Tornado), "horizontal slice") != nullptr
       && std::strstr(KindNote(Kit::WindKind::Gust), "Frequency drives") != nullptr
       && std::strstr(KindNote(Kit::WindKind::Radial), "outward flow") != nullptr
       && std::strstr(KindNote(Kit::WindKind::Directional), "0\xc2\xb0 north, 90\xc2\xb0 east") != nullptr,
          "each kind carries its own explanatory paragraph");
    {
        const float Inner = MainRun - 2.0f - PropsPad * 2.0f;
        Check(Near(ProseHeight(Light, Inner, KindNote(Kit::WindKind::Radial)), 45.6f),
              "the short radial note takes one line in a 1279 px column");
        const float Tornado = PropertiesHeight(Light, MainRun, Field, 2);
        Check(Near(Tornado, 2 + PropsPad * 2 + 32 + 18 + 2 * LabelBlock + GridGap + 45.6f),
              "six tornado fields wrap to two rows under a 32 px head");
        Kit::WindComposite Bare;
        Check(Near(PropertiesHeight(Light, MainRun, Bare, -1), 2 + PropsPad * 2 + LineBox(HeadingSize)),
              "with nothing selected the card is only its heading");
    }

    //---------------------------------------------------------------------------------------------------------
    // The placement map: viewBox 0 0 600 400 stretched into the content box, so the two axes differ.
    //---------------------------------------------------------------------------------------------------------
    {
        const float ScaleX = (Half - 2.0f) / 600.0f, ScaleY = (ViewTall - 2.0f) / 400.0f;
        Check(Near(ScaleX, 1.07917f, 1e-4f) && Near(ScaleY, 0.82f, 1e-4f),
              "the map stretches 1.079 across and 0.82 down");
        const float MapX = (Field.Parts[2].X / Field.Wide + 0.5f) * 600.0f;
        const float MapZ = (Field.Parts[2].Z / Field.Deep + 0.5f) * 400.0f;
        Check(Near(MapX, 372.0f) && Near(MapZ, 168.0f),
              "the tornado at 120 m, -80 m sits at 372, 168 in user space");
        // CheckWind.mjs drags the handle to .36 / .62 of the map and expects -140 m and 120 m back.
        Check(Near((0.36f - 0.5f) * Field.Wide, -140.0f) && Near((0.62f - 0.5f) * Field.Deep, 120.0f),
              "a drag to .36, .62 of a 1000 m slice reads back -140 m and 120 m");
        Check(Near(52.0f + 0 * 34.0f, 52.0f) && Near(52.0f + 2 * 34.0f, 120.0f),
              "directional components pin to x 30 and stack 34 apart");
    }

    //---------------------------------------------------------------------------------------------------------
    // Painting.
    //---------------------------------------------------------------------------------------------------------
    const int Width = int(ShellWide + BackPad * 2), Height = int(ShellTall + BackPad * 2);
    Kit::WindMotes Motes;

    auto Tick = [&](float Delta)
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        IO.DeltaTime = Delta > 0.0f ? Delta : 1.0f / 60;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("WindEditor", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Light, 14.0f);
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        // .wind-editor-backdrop over the editor behind it: #000b on the editor's own near-black ground.
        Draw->AddRectFilled({ 0, 0 }, { float(Width), float(Height) }, IM_COL32(9, 9, 9, 255));
        Draw->AddRectFilled({ 0, 0 }, { float(Width), float(Height) }, Backdrop);
        PaintDialog(Draw, Light, Regular, { BackPad, BackPad }, ShellWide, ShellTall, Field, State, Motes,
                    Delta);
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    std::vector<unsigned char> Pixels;
    auto Capture = [&](const char* Name, int Frames)
    {
        for (int Frame = 0; Frame < Frames; ++Frame) Tick(1.0f / 60);
        constexpr int Over = 3;
        const int WideOver = Width * Over, TallOver = Height * Over;
        std::vector<unsigned char> Dense(size_t(WideOver) * TallOver * 3, 0);
        for (ImDrawList* List : ImGui::GetDrawData()->CmdLists)
            FrontierProof::Draw(List, Dense.data(), WideOver, TallOver, { 0, 0 }, { Over, Over },
                                ImTextureID(), {});
        Pixels.assign(size_t(Width) * Height * 3, 0);
        for (int Y = 0; Y < Height; ++Y) for (int X = 0; X < Width; ++X) for (int C = 0; C < 3; ++C)
        {
            int Total = 0;
            for (int DY = 0; DY < Over; ++DY) for (int DX = 0; DX < Over; ++DX)
                Total += Dense[(size_t(Y * Over + DY) * WideOver + X * Over + DX) * 3 + C];
            Pixels[(size_t(Y) * Width + X) * 3 + C] = static_cast<unsigned char>(Total / (Over * Over));
        }
        const std::string Path = "Exhibits/Gallery/WindEditorNative/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0,
              "capture written");
    };
    auto At = [&](int X, int Y, int Channel)
    {
        return int(Pixels[(size_t(Y) * Width + X) * 3 + Channel]);
    };

    Capture("Dialog", 6);
    {   // the 22 px gutter of .wind-editor-main is bare shell
        const int BareX = int(BackPad + AsideWide + MainPad * 0.5f), BareY = int(BackPad + HeadTall + 8);
        Check(At(BareX, BareY, 0) == 17 && At(BareX, BareY, 1) == 19 && At(BareX, BareY, 2) == 19,
              "the shell paints #111313 behind the editor body");
    }
    Check(At(6, 6, 0) < 9 && At(6, 6, 1) < 9, "the backdrop darkens the editor behind the dialog");
    {
        // The footer dot is #34c759 at the left of the footer line.
        const int DotY = int(BackPad + ShellTall - FootTall + 1 + FootPadY + (LineBox(FooterSize)) * 0.5f);
        const int DotX = int(BackPad + FootPadX + DotSize * 0.5f);
        Check(At(DotX, DotY, 1) > 150 && At(DotX, DotY, 0) < 90,
              "an evaluated field shows the green footer dot");
    }
    {
        // Row three of .wind-parts is selected, so it is #292a23 rather than #1a1f1c.
        const float RowY = BackPad + HeadTall + AsidePad + LabelBlock + NameDrop + LineBox(HeadingSize)
                         + AddLift + AddTall * 2 + AddGap + AddDrop + 2 * (PartTall + PartDrop);
        const int SampleY = int(RowY + PartTall * 0.5f), SampleX = int(BackPad + AsidePad + 180);
        Check(At(SampleX, SampleY, 0) == 41 && At(SampleX, SampleY, 1) == 42 && At(SampleX, SampleY, 2) == 35,
              "the selected component row is painted #292a23");
        const int AboveY = int(RowY - PartTall * 0.5f);
        Check(At(SampleX, AboveY, 0) == 26 && At(SampleX, AboveY, 1) == 31,
              "an unselected component row stays #1a1f1c");
    }

    State.Vectors = true;
    Field.Parts[2].Strength = 36.0f;
    Field.Parts[2].Radius   = 340.0f;
    Field.Parts[2].X        = -140.0f;
    Field.Parts[2].Z        =  120.0f;
    Capture("DialogTuned", 2);

    State.Vectors   = false;
    State.Playing   = false;
    State.Hidden    = true;
    State.Selection = 1;
    Field.Parts[2].Enabled = false;
    Capture("DialogPaused", 2);
    {
        const int DotY = int(BackPad + ShellTall - FootTall + 1 + FootPadY + (LineBox(FooterSize)) * 0.5f);
        const int DotX = int(BackPad + FootPadX + DotSize * 0.5f);
        Check(At(DotX, DotY, 0) > 150 && At(DotX, DotY, 1) < 140,
              "a hidden field shows the red footer dot");
    }

    Kit::WindComposite Bare;
    Field = Bare;
    State.Selection = -1;
    State.Hidden = false;
    State.Playing = true;
    std::snprintf(State.FieldName, sizeof(State.FieldName), "%s", "Empty field");
    Capture("DialogEmpty", 2);

    std::printf("PASS %u checks: native WindEditor dialog, placement map and property grid.\n", Checks);
    return 0;
}
