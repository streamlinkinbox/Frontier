//============================================================================================================================================
//                                                       TYREGENERATORPROOF.CPP
//============================================================================================================================================
// 📦 Headless visual proof of the tyre's editor surfaces. Drives the REAL EditorHost over a roster built by the
//    REAL TyreGeneratorSequence, draws the REAL TyreInspectorPanel and Tyre Generator window, and rasterises the
//    resulting ImDrawData. Nothing here draws a widget; it drives the editor and photographs what came out.

#include "EditorHost.h"
#include "ControlPanel.h"
#include "EditorInstance.h"
#include "../../../Engine/Generators/Tyre/TyreGeneratorWindow.h"
#include "../../../Engine/Generators/Tyre/TyrePresetLibrary.h"
#include "TyreInspectorPanel.h"
#include "../../../Projects/Project-Drive/Source/TyreGeneratorSequence.h"

#include "../../../Engine/DisplayPresentation/TypefaceRegistry.h"

#include "imgui.h"
#include "imgui_internal.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RASTERISER
//------------------------------------------------------------------------------------------------------------------------
// The same job every ImGui backend does: scissor, barycentric-interpolate the vertex colour, modulate by the
//    glyph sheet, composite. Software, so the proof needs no device.

struct Surface
{
    int Width = 0, Height = 0;
    std::vector<unsigned char> Pixels;

    void Seat(int W, int H) noexcept
    {
        Width = W;
        Height = H;
        Pixels.assign(size_t(W) * size_t(H) * 4u, 0u);
        for (size_t I = 0; I < Pixels.size(); I += 4u)
        {
            Pixels[I + 0] = 9u;   Pixels[I + 1] = 9u;   Pixels[I + 2] = 11u;  Pixels[I + 3] = 255u;
        }
    }
};

void Blend(unsigned char* Pixel, float R, float G, float B, float A) noexcept
{
    if (A <= 0.0f)
    {
        return;
    }
    const float Inv = 1.0f - A;
    Pixel[0] = (unsigned char)(R * 255.0f * A + float(Pixel[0]) * Inv);
    Pixel[1] = (unsigned char)(G * 255.0f * A + float(Pixel[1]) * Inv);
    Pixel[2] = (unsigned char)(B * 255.0f * A + float(Pixel[2]) * Inv);
    Pixel[3] = 255u;
}

void RasterizeList(const ImDrawList* List, const unsigned char* Sheet, int SheetW, int SheetH, Surface& Target) noexcept
{
    size_t Offset = 0;
    for (int C = 0; C < List->CmdBuffer.Size; ++C)
    {
        const ImDrawCmd& Cmd = List->CmdBuffer[C];
        if (Cmd.UserCallback != nullptr)
        {
            Offset += Cmd.ElemCount;
            continue;
        }
        const int ClipX0 = int(Cmd.ClipRect.x < 0.0f ? 0.0f : Cmd.ClipRect.x);
        const int ClipY0 = int(Cmd.ClipRect.y < 0.0f ? 0.0f : Cmd.ClipRect.y);
        const int ClipX1 = int(Cmd.ClipRect.z > float(Target.Width)  ? float(Target.Width)  : Cmd.ClipRect.z);
        const int ClipY1 = int(Cmd.ClipRect.w > float(Target.Height) ? float(Target.Height) : Cmd.ClipRect.w);

        for (unsigned int E = 0; E < Cmd.ElemCount; E += 3u)
        {
            // ⚠️ VtxOffset is not optional. The backend advertises RendererHasVtxOffset, so once a list
            //    passes 65 536 vertices ImGui starts a fresh command whose indices are relative to this
            //    offset. Ignoring it does not drop the overflow — it silently reads the WRONG vertices,
            //    which is why a long preset grid used to go blank partway down instead of failing loudly.
            const unsigned int Base = Cmd.VtxOffset;
            const ImDrawVert& A = List->VtxBuffer[Base + List->IdxBuffer[Offset + E + 0u]];
            const ImDrawVert& B = List->VtxBuffer[Base + List->IdxBuffer[Offset + E + 1u]];
            const ImDrawVert& D = List->VtxBuffer[Base + List->IdxBuffer[Offset + E + 2u]];

            float MinX = A.pos.x, MaxX = A.pos.x, MinY = A.pos.y, MaxY = A.pos.y;
            MinX = B.pos.x < MinX ? B.pos.x : MinX;  MaxX = B.pos.x > MaxX ? B.pos.x : MaxX;
            MinY = B.pos.y < MinY ? B.pos.y : MinY;  MaxY = B.pos.y > MaxY ? B.pos.y : MaxY;
            MinX = D.pos.x < MinX ? D.pos.x : MinX;  MaxX = D.pos.x > MaxX ? D.pos.x : MaxX;
            MinY = D.pos.y < MinY ? D.pos.y : MinY;  MaxY = D.pos.y > MaxY ? D.pos.y : MaxY;

            int X0 = int(MinX), X1 = int(MaxX) + 1, Y0 = int(MinY), Y1 = int(MaxY) + 1;
            if (X0 < ClipX0) X0 = ClipX0;  if (X1 > ClipX1) X1 = ClipX1;
            if (Y0 < ClipY0) Y0 = ClipY0;  if (Y1 > ClipY1) Y1 = ClipY1;

            const float Area = (B.pos.x - A.pos.x) * (D.pos.y - A.pos.y)
                             - (B.pos.y - A.pos.y) * (D.pos.x - A.pos.x);
            if (Area == 0.0f)
            {
                continue;
            }
            const float InvArea = 1.0f / Area;

            for (int Y = Y0; Y < Y1; ++Y)
            {
                for (int X = X0; X < X1; ++X)
                {
                    const float Px = float(X) + 0.5f, Py = float(Y) + 0.5f;
                    float W0 = ((B.pos.x - Px) * (D.pos.y - Py) - (B.pos.y - Py) * (D.pos.x - Px)) * InvArea;
                    float W1 = ((D.pos.x - Px) * (A.pos.y - Py) - (D.pos.y - Py) * (A.pos.x - Px)) * InvArea;
                    float W2 = 1.0f - W0 - W1;
                    if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
                    {
                        continue;
                    }
                    const float U = W0 * A.uv.x + W1 * B.uv.x + W2 * D.uv.x;
                    const float V = W0 * A.uv.y + W1 * B.uv.y + W2 * D.uv.y;

                    auto Chan = [](ImU32 Colour, int Shift) noexcept -> float
                    { return float((Colour >> Shift) & 0xFFu) / 255.0f; };
                    const float R = W0 * Chan(A.col, 0)  + W1 * Chan(B.col, 0)  + W2 * Chan(D.col, 0);
                    const float G = W0 * Chan(A.col, 8)  + W1 * Chan(B.col, 8)  + W2 * Chan(D.col, 8);
                    const float Bl= W0 * Chan(A.col, 16) + W1 * Chan(B.col, 16) + W2 * Chan(D.col, 16);
                    float Al      = W0 * Chan(A.col, 24) + W1 * Chan(B.col, 24) + W2 * Chan(D.col, 24);

                    int Sx = int(U * float(SheetW)), Sy = int(V * float(SheetH));
                    if (Sx < 0) Sx = 0;  if (Sx >= SheetW) Sx = SheetW - 1;
                    if (Sy < 0) Sy = 0;  if (Sy >= SheetH) Sy = SheetH - 1;
                    const unsigned char* Texel = Sheet + (size_t(Sy) * size_t(SheetW) + size_t(Sx)) * 4u;
                    Al *= float(Texel[3]) / 255.0f;

                    Blend(&Target.Pixels[(size_t(Y) * size_t(Target.Width) + size_t(X)) * 4u], R, G, Bl, Al);
                }
            }
        }
        Offset += Cmd.ElemCount;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        GATES
//------------------------------------------------------------------------------------------------------------------------

int gChecks = 0, gFailures = 0;

void Gate(const char* Name, bool Pass, const char* Detail = "") noexcept
{
    ++gChecks;
    if (!Pass)
    {
        ++gFailures;
    }
    std::printf("[TyreGeneratorProof] %s  %s%s%s\n", Pass ? "PASS" : "[FAIL]", Name,
                Detail[0] ? "  -- " : "", Detail);
}

/// 📦 How much of the surface is not the background — a sheet that drew nothing is not a proof.
[[nodiscard]] double InkFraction(const Surface& S) noexcept
{
    size_t Lit = 0;
    for (size_t I = 0; I < S.Pixels.size(); I += 4u)
    {
        if (S.Pixels[I] != 9u || S.Pixels[I + 1] != 9u || S.Pixels[I + 2] != 11u)
        {
            ++Lit;
        }
    }
    return double(Lit) / double(S.Pixels.size() / 4u);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.DisplaySize = ImVec2(1600.0f, 900.0f);
    IO.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    IO.DeltaTime   = 1.0f / 60.0f;
    IO.IniFilename = nullptr;
    IO.LogFilename = nullptr;
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Tyre Generator is dockable because the host docks
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    Frontier::EditorHost Editor;
    Editor.ApplyTheme();                                // seats the faces before the glyph sheet is taken

    static Frontier::TypefaceRegistry Typefaces;
    Typefaces.Load("EngineContent/FontArchives");
    Frontier::TypefaceRegistry::Install(&Typefaces);

    unsigned char* Sheet = nullptr;
    int SheetW = 0, SheetH = 0;
    IO.Fonts->GetTexDataAsRGBA32(&Sheet, &SheetW, &SheetH);

    if (!Editor.SeatShade(1600u, 900u))
    {
        std::fprintf(stderr, "[TyreGeneratorProof] [FAIL] the shade never seated\n");
        return 1;
    }

    Frontier::EditorReadout Readout = {};
    Readout.Fps = 60.0f;
    std::snprintf(Readout.Quality, sizeof(Readout.Quality), "Standard");
    std::snprintf(Readout.Scene, sizeof(Readout.Scene), "Drive");
    Editor.AssignReadout(&Readout);

    // ① the real sequence, seated with the same preset the tread mesh gate runs
    Frontier::Drive::TyreGeneratorSequence Generator;
    Frontier::Drive::SeatGrizzlyMagnum(Generator.Document);

    Frontier::EditorInstance Rows[Frontier::kMaxEditorInstances] = {};
    const uint32_t RowCount = Generator.FillRoster(Rows, 0u, Frontier::kMaxEditorInstances, 0u);

    Gate("roster carries the tyre and its parts", RowCount == 12u,
         [&]{ static char T[48]; std::snprintf(T, sizeof(T), "%u rows", unsigned(RowCount)); return T; }());
    Gate("the tyre node names the pattern", std::strcmp(Rows[0].Label, "Tyre") == 0
         && std::strstr(Rows[0].Meta, "Grizzly") != nullptr, Rows[0].Meta);
    Gate("the seven layers are rows of their own",
         RowCount >= 10u && std::strcmp(Rows[3].Label, "Circumferential") == 0
         && std::strcmp(Rows[6].Label, "Chevron") == 0, Rows[6].Label);
    Gate("decals stand warned rather than absent",
         Rows[RowCount - 2u].Standing == Frontier::EditorStanding::Warn, Rows[RowCount - 2u].StandingNote);

    // ② the sheets the inspector will draw
    struct Phase { const char* Name; uint64_t Key; int Page; };   // Page -1 keeps the generator closed
    const Phase Phases[] = {
        { "Tyre",      Frontier::Drive::TyreInspectorKey(Frontier::Drive::TyreSection::Tyre),       -1 },
        { "Carcass",   Frontier::Drive::TyreInspectorKey(Frontier::Drive::TyreSection::Carcass),    -1 },
        { "Lattice",   Frontier::Drive::TyreInspectorKey(Frontier::Drive::TyreSection::Lattice),    -1 },
        { "Generator", Frontier::Drive::TyreInspectorKey(Frontier::Drive::TyreSection::Layer, 3u),   0 },
        { "Rim",       Frontier::Drive::TyreInspectorKey(Frontier::Drive::TyreSection::Layer, 3u),   1 },
        { "Look",      Frontier::Drive::TyreInspectorKey(Frontier::Drive::TyreSection::Layer, 3u),   2 },
        { "Export",    Frontier::Drive::TyreInspectorKey(Frontier::Drive::TyreSection::Layer, 3u),   3 },
    };

    // The application's own state, seated from the preset the project document carries.
    Frontier::TyreGeneratorState App;
    App.Document.Appearance.Decals = Frontier::DefaultSidewallDecals();
    for (int I = 0; I < int(Frontier::TyrePresets().size()); ++I)
    {
        if (Frontier::TyrePresets()[size_t(I)].Name == "Grizzly Magnum")
        {
            Frontier::ApplyTyrePreset(App, I);
        }
    }
    App.PickedLayer = 3;
    App.PickedDecal = 1;

    for (const Phase& P : Phases)
    {
        Frontier::EditorSheet Built{};
        const bool Ok = Generator.BuildSheet(P.Key, &Built);
        char Detail[64];
        std::snprintf(Detail, sizeof(Detail), "%u cards", unsigned(Built.GroupCount));
        char Name[64];
        std::snprintf(Name, sizeof(Name), "%s sheet builds", P.Name);
        Gate(Name, Ok && Built.GroupCount > 0u, Detail);
    }

    // ③ the write-back actually writes
    {
        Frontier::EditorSheet Built{};
        const uint64_t Key = Frontier::Drive::TyreInspectorKey(Frontier::Drive::TyreSection::Tyre);
        (void)Generator.BuildSheet(Key, &Built);
        Built.Groups[0].Properties[0].Figure = 310.0f;
        const bool Changed = Generator.ApplySheet(Key, Built);
        Gate("an edited sheet reaches the document",
             Changed && std::fabs(Generator.Document.Carcass.InflationPressure - 310.0f) < 0.01f,
             "pressure 240 -> 310 kPa");
        const float Hoop = Generator.Document.Carcass.HoopCompliance();
        Gate("compliance follows pressure", Hoop < 1.0e-7f,
             [&]{ static char T[48]; std::snprintf(T, sizeof(T), "hoop %.3e m/N", double(Hoop)); return T; }());
        Generator.Document.Carcass.InflationPressure = 240.0f;
    }

    // ④ drive the real editor and photograph each phase
    Surface Target;
    for (const Phase& P : Phases)
    {
        Frontier::EditorSheet Built{};
        if (!Generator.BuildSheet(P.Key, &Built))
        {
            continue;
        }

        // The sheet alone is not a selection: the outliner owns the pick, and the inspector draws what IS picked.
        uint32_t Picked = Frontier::kNoEditorInstance;
        for (uint32_t I = 0; I < RowCount; ++I)
        {
            if (Rows[I].InspectorKey == P.Key)
            {
                Picked = I;
                break;
            }
        }
        Editor.PickInstance(Picked);

        char PickName[72];
        std::snprintf(PickName, sizeof(PickName), "%s row is picked in the outliner", P.Name);
        Gate(PickName, Picked != Frontier::kNoEditorInstance && Editor.QueryPickedInstance() == Picked,
             Picked != Frontier::kNoEditorInstance ? Rows[Picked].Label : "not found");

        const bool GeneratorOpen = P.Page >= 0;
        if (GeneratorOpen)
        {
            App.Tab = Frontier::TyreGeneratorTab(P.Page);
        }

        // ten ticks so every easing in the panels has settled before the shutter
        for (int Tick = 0; Tick < 10; ++Tick)
        {
            ImGui::NewFrame();
            Editor.Record(Rows, RowCount, &Built);
            if (GeneratorOpen)
            {
                bool Open = true;
                Frontier::RecordTyreGeneratorWindow(Editor.QueryControls(), App, &Open);
            }
            ImGui::Render();
        }

        const ImDrawData* Data = ImGui::GetDrawData();
        Target.Seat(int(IO.DisplaySize.x), int(IO.DisplaySize.y));
        for (int L = 0; L < Data->CmdListsCount; ++L)
        {
            RasterizeList(Data->CmdLists[L], Sheet, SheetW, SheetH, Target);
        }

        char Path[160];
        std::snprintf(Path, sizeof(Path), "Exhibits/Gallery/Editor/TyreGeneratorProof_%s.png", P.Name);
        stbi_write_png(Path, Target.Width, Target.Height, 4, Target.Pixels.data(), Target.Width * 4);

        const double Ink = InkFraction(Target);
        char Name[72], Detail[64];
        std::snprintf(Name, sizeof(Name), "%s sheet rasterises", P.Name);
        std::snprintf(Detail, sizeof(Detail), "%.1f%% of the surface drawn", Ink * 100.0);
        Gate(Name, Ink > 0.25, Detail);
        std::printf("[TyreGeneratorProof] wrote %s\n", Path);
    }

    std::printf("[TyreGeneratorProof] %d/%d gates passed\n", gChecks - gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
