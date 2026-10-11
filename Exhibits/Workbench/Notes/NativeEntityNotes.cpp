//==============================================================================================================================================
//                                                         NATIVEENTITYNOTES.CPP
//==============================================================================================================================================
// 📦 Executed proof: records EntityNotes from real ImGui draw commands, against Inspectors.jsx:409 and the
//    .entity-notes rules inlined in Experimental/ProjectZeroEditor/index.html.
//
//    This one is a correction, not an addition. The native RecordNotes drew a chevron disclosure in a
//    rounded 18 px inset card with a caps header and a 64 px field; none of that shape is in the bundle.

#include "EntityNotesSurface.h"
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
using namespace Frontier::EntityNotes;

namespace
{
unsigned Checks = 0;
void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition) throw std::runtime_error(Claim);
}
}   // namespace

int main()
{
    std::filesystem::create_directories("Exhibits/Gallery/EntityNotesNative");
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

    constexpr int Margin = 20;
    constexpr float Column = 300.0f;
    const int Width = int(Column) + Margin * 2;
    int Height = 300;
    int Scene = 0;
    const char* Body = "";
    bool Hot = false;

    auto Tick = [&]()
    {
        IO.DisplaySize = { float(Width), float(Height) };
        IO.DisplayFramebufferScale = { 1, 1 };
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize(IO.DisplaySize);
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushFont(Light, 14.0f);
        ImDrawList* Draw = ImGui::GetWindowDrawList();
        Draw->AddRectFilled({ 0, 0 }, { float(Width), float(Height) }, IM_COL32(9, 9, 9, 255));
        if (Scene == 0)
            PaintAdd(Draw, Light, { float(Margin) + Column - AddWidth(Light), float(Margin) }, Hot);
        else
            PaintPanel(Draw, Light, Regular, { float(Margin), float(Margin) }, Column, Body);
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };

    auto Capture = [&](const char* Name)
    {
        Height = int(std::ceil(Scene == 0 ? AddTall : PanelHeight())) + Margin * 2;
        for (int Frame = 0; Frame < 3; ++Frame) Tick();
        constexpr int Over = 3;
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
        const std::string Path = "Exhibits/Gallery/EntityNotesNative/" + std::string(Name) + ".png";
        Check(stbi_write_png(Path.c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0, "capture written");
    };

    //-----------------------------------------------------------------------------------------------------
    // The disclosure. useState(Boolean(Value)): a subject that already has a note opens showing it.
    //-----------------------------------------------------------------------------------------------------
    Check(!OpensItself(""), "a subject with no note starts closed");
    Check(!OpensItself(nullptr), "and so does one with no note field at all");
    Check(OpensItself("Owned by lighting"), "a subject that already has one starts open");

    //-----------------------------------------------------------------------------------------------------
    // The closed state: an inline-flex button sized by its own content.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(AddTall == 30.0f, "the add button is 30 px tall");
        const float Wide = AddWidth(Light);
        const float Label = Kit::Measured(Light, AddSize, "Add notes");
        Check(std::fabs(Wide - (9.0f + 13.0f + 6.0f + Label + 9.0f + 2.0f)) < 1e-4f,
              "and as wide as padding, a 13 px plus, the gap, the label and padding");
        Check(Wide > 60.0f && Wide < 120.0f, "which lands between 60 and 120 px at 9 px type");
    }

    //-----------------------------------------------------------------------------------------------------
    // The open state. Every number here is a CSS box, not a guess.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(std::fabs(RowHeight() - 22.0f) < 1e-4f,
              "the header row is the 22 px Hide button, taller than either piece of type beside it");
        Check(std::fabs(PanelHeight() - 128.0f) < 1e-4f,
              "so the panel is 1 + 10 + 22 + 8 + 76 + 10 + 1 = 128 px");
        Check(std::fabs(HeadingHeight(false) - 110.0f) < 1e-4f, "a heading without a note is 110 px");
        Check(std::fabs(HeadingHeight(true) - 224.0f) < 1e-4f, "and 224 with one");
        // A genuine property of the reference worth pinning: the panel is absolutely positioned at 105,
        //    so it overhangs the heading it is reserved space in.
        Check(std::fabs(NotesTop + PanelHeight() - 233.0f) < 1e-4f,
              "placed at top 105 it reaches 233, overhanging the 224 px heading by nine pixels");
    }

    //-----------------------------------------------------------------------------------------------------
    // Captures.
    //-----------------------------------------------------------------------------------------------------
    Scene = 0; Hot = false; Capture("AddNotes");
    Scene = 0; Hot = true;  Capture("AddNotesHover");
    Scene = 1; Body = "";   Capture("PanelEmpty");
    Scene = 1;
    Body = "Owned by the lighting team. Review before the next bake; the rig was retargeted in March and "
           "the key light still reads a stop hot against the reference plate.";
    Capture("PanelFilled");

    ImGui::DestroyContext();
    std::printf("PASS %u checks: EntityNotes, both disclosure states, against the bundle's own boxes.\n", Checks);
    return 0;
}
