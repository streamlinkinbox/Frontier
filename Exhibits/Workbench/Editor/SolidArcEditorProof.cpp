//=============================================================================================================================================
//                                                SOLIDARCEDITORPROOF.CPP
//=============================================================================================================================================
// Headless visual proof for the SolidArc authoring editor. It drives the real SolidArcEditorHost through ImGui docking,
// rasterises the resulting draw lists with the same CPU path used by EditorProof.cpp, and writes a PNG in the canonical
// Exhibits/Gallery/Editor proof folder. No browser/SVG fallback is involved.

#ifndef FRONTIER_DEVELOPMENT
#error "the SolidArc proof must define FRONTIER_DEVELOPMENT"
#endif

#include <imgui.h>

#include "Editor/SolidArcEditorHost.h"
#include "PngWriteCounterpart.h"
#include "TypefaceRegistry.h"
#include "../IconArt/CpuDraw.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

static_assert(sizeof(ImDrawIdx) == 2u, "the proof rasteriser walks 16-bit ImGui indices");

namespace {

int gWidth  = 1280;     // the proof window; the per-object shots re-seat it taller
int gHeight = 720;
constexpr unsigned char kGround[3] = { 5u, 5u, 5u };

struct Rgba
{
    float R, G, B, A;
};

Rgba UnpackColour(uint32_t Packed) noexcept
{
    return { static_cast<float>(Packed & 0xFFu) / 255.0f,
             static_cast<float>((Packed >> 8) & 0xFFu) / 255.0f,
             static_cast<float>((Packed >> 16) & 0xFFu) / 255.0f,
             static_cast<float>((Packed >> 24) & 0xFFu) / 255.0f };
}

void OverlayPixel(unsigned char* Pixel, Rgba Over) noexcept
{
    const float Keep = 1.0f - Over.A;
    Pixel[0] = static_cast<unsigned char>(Over.R * 255.0f * Over.A + static_cast<float>(Pixel[0]) * Keep + 0.5f);
    Pixel[1] = static_cast<unsigned char>(Over.G * 255.0f * Over.A + static_cast<float>(Pixel[1]) * Keep + 0.5f);
    Pixel[2] = static_cast<unsigned char>(Over.B * 255.0f * Over.A + static_cast<float>(Pixel[2]) * Keep + 0.5f);
}

Rgba SampleSheet(const unsigned char* Sheet, int SheetWidth, int SheetHeight, float U, float V, bool RgbaSheet) noexcept
{
    int X = static_cast<int>(U * static_cast<float>(SheetWidth));
    int Y = static_cast<int>(V * static_cast<float>(SheetHeight));
    if (X < 0) X = 0; if (X >= SheetWidth) X = SheetWidth - 1;
    if (Y < 0) Y = 0; if (Y >= SheetHeight) Y = SheetHeight - 1;
    const unsigned char* Texel = Sheet + (static_cast<size_t>(Y) * static_cast<size_t>(SheetWidth) + static_cast<size_t>(X)) * 4u;
    return { static_cast<float>(Texel[0]) / 255.0f,
             static_cast<float>(Texel[1]) / 255.0f,
             static_cast<float>(Texel[2]) / 255.0f,
             RgbaSheet ? (static_cast<float>(Texel[3]) / 255.0f) : 1.0f };
}

float EdgeWeight(float Ax, float Ay, float Bx, float By, float Px, float Py) noexcept
{
    return (Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax);
}

const unsigned char* gSolidArcRgba = nullptr;
uint32_t             gSolidArcW    = 0u;
uint32_t             gSolidArcH    = 0u;
bool                 gCtrl         = false;   // the Ctrl the proof holds while it drives the pointer, and the figure last sent
bool                 gCtrlSent     = false;

void RasterizeList(const ImDrawList* List,
                   const unsigned char* GlyphSheet,
                   int GlyphSheetWidth,
                   int GlyphSheetHeight,
                   unsigned char* Pixels,
                   ImVec2 Origin,
                   ImVec2 PixelScale) noexcept
{
    const ImDrawVert* Corners = List->VtxBuffer.Data;
    const ImDrawIdx*  Order   = List->IdxBuffer.Data;
    for (int Command = 0; Command < List->CmdBuffer.Size; ++Command)
    {
        const ImDrawCmd* Cmd = &List->CmdBuffer[Command];
        const ImTextureID CmdTex = Cmd->TexRef._TexData != nullptr ? static_cast<ImTextureID>(0) : Cmd->TexRef._TexID;
        const bool SolidArcTexture = CmdTex != static_cast<ImTextureID>(0) && gSolidArcRgba != nullptr;
        const unsigned char* Sheet = SolidArcTexture ? gSolidArcRgba : GlyphSheet;
        const int SheetW = SolidArcTexture ? static_cast<int>(gSolidArcW) : GlyphSheetWidth;
        const int SheetH = SolidArcTexture ? static_cast<int>(gSolidArcH) : GlyphSheetHeight;
        const bool RgbaSheet = !SolidArcTexture;

        int ScissorLeft   = static_cast<int>((Cmd->ClipRect.x - Origin.x) * PixelScale.x);
        int ScissorTop    = static_cast<int>((Cmd->ClipRect.y - Origin.y) * PixelScale.y);
        int ScissorRight  = static_cast<int>((Cmd->ClipRect.z - Origin.x) * PixelScale.x);
        int ScissorBottom = static_cast<int>((Cmd->ClipRect.w - Origin.y) * PixelScale.y);
        if (ScissorLeft < 0) ScissorLeft = 0;
        if (ScissorRight > gWidth) ScissorRight = gWidth;
        if (ScissorTop < 0) ScissorTop = 0;
        if (ScissorBottom > gHeight) ScissorBottom = gHeight;

        for (unsigned int I = 0u; I < Cmd->ElemCount; I += 3u)
        {
            const ImDrawVert& A = Corners[Order[Cmd->IdxOffset + I] + Cmd->VtxOffset];
            const ImDrawVert& B = Corners[Order[Cmd->IdxOffset + I + 1u] + Cmd->VtxOffset];
            const ImDrawVert& C = Corners[Order[Cmd->IdxOffset + I + 2u] + Cmd->VtxOffset];
            const float SignedArea = EdgeWeight(A.pos.x, A.pos.y, B.pos.x, B.pos.y, C.pos.x, C.pos.y);
            if (SignedArea == 0.0f)
                continue;

            int LoX = static_cast<int>(std::floor(std::fmin(A.pos.x, std::fmin(B.pos.x, C.pos.x))));
            int HiX = static_cast<int>(std::ceil(std::fmax(A.pos.x, std::fmax(B.pos.x, C.pos.x))));
            int LoY = static_cast<int>(std::floor(std::fmin(A.pos.y, std::fmin(B.pos.y, C.pos.y))));
            int HiY = static_cast<int>(std::ceil(std::fmax(A.pos.y, std::fmax(B.pos.y, C.pos.y))));
            if (LoX < ScissorLeft) LoX = ScissorLeft; if (HiX > ScissorRight) HiX = ScissorRight;
            if (LoY < ScissorTop) LoY = ScissorTop; if (HiY > ScissorBottom) HiY = ScissorBottom;

            const Rgba TintedA = UnpackColour(A.col);
            const Rgba TintedB = UnpackColour(B.col);
            const Rgba TintedC = UnpackColour(C.col);
            const float InverseArea = 1.0f / SignedArea;
            for (int Y = LoY; Y < HiY; ++Y)
            {
                for (int X = LoX; X < HiX; ++X)
                {
                    const float Px = static_cast<float>(X) + 0.5f;
                    const float Py = static_cast<float>(Y) + 0.5f;
                    const float W0 = EdgeWeight(B.pos.x, B.pos.y, C.pos.x, C.pos.y, Px, Py) * InverseArea;
                    const float W1 = EdgeWeight(C.pos.x, C.pos.y, A.pos.x, A.pos.y, Px, Py) * InverseArea;
                    const float W2 = EdgeWeight(A.pos.x, A.pos.y, B.pos.x, B.pos.y, Px, Py) * InverseArea;
                    if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
                        continue;

                    const float U = W0 * A.uv.x + W1 * B.uv.x + W2 * C.uv.x;
                    const float V = W0 * A.uv.y + W1 * B.uv.y + W2 * C.uv.y;
                    const Rgba Glyph = SampleSheet(Sheet, SheetW, SheetH, U, V, RgbaSheet);
                    const Rgba Tinted = { (W0 * TintedA.R + W1 * TintedB.R + W2 * TintedC.R) * Glyph.R,
                                          (W0 * TintedA.G + W1 * TintedB.G + W2 * TintedC.G) * Glyph.G,
                                          (W0 * TintedA.B + W1 * TintedB.B + W2 * TintedC.B) * Glyph.B,
                                          (W0 * TintedA.A + W1 * TintedB.A + W2 * TintedC.A) * Glyph.A };
                    OverlayPixel(&Pixels[(static_cast<size_t>(Y) * gWidth + static_cast<size_t>(X)) * 3u], Tinted);
                }
            }
        }
    }
}

bool Run(Frontier::ConsoleHost& Host, const char* Command) noexcept
{
    if (!Host.Execute(Command))
    {
        std::fprintf(stderr, "[SolidArcEditorProof] command failed: %s\n", Command);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.DisplaySize = ImVec2(static_cast<float>(gWidth), static_cast<float>(gHeight));
    IO.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    IO.IniFilename = nullptr;
    IO.LogFilename = nullptr;
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    Frontier::SolidArcEditorHost Editor;
    Editor.ApplyTheme();

    if (IO.Fonts->Fonts.empty())
        IO.Fonts->AddFontDefault();

    static Frontier::TypefaceRegistry Typefaces;
    (void)Typefaces.Load("EngineContent/FontArchives");
    Frontier::TypefaceRegistry::Install(&Typefaces);

    unsigned char* GlyphSheet = nullptr;
    int GlyphSheetWidth = 0, GlyphSheetHeight = 0;
    IO.Fonts->GetTexDataAsRGBA32(&GlyphSheet, &GlyphSheetWidth, &GlyphSheetHeight);

    if (!Editor.SeatShade(gWidth, gHeight))
    {
        std::fprintf(stderr, "[SolidArcEditorProof] [FAIL] the Control Centre notch never seated\n");
        return 1;
    }

    Frontier::ConsoleHost Host("/tmp/solidarc-editor-proof", 520, 340);
    if (!Run(Host, "box (-1.2,-0.5,0) (1.2,0.5,0.8) --name=Body01")) return 2;
    if (!Run(Host, "sphere (0.0,0.0,1.15) 0.35 --sheet --name=CanopySheet")) return 3;
    if (!Run(Host, "line (-1.4,-0.7,0) (1.4,-0.7,0) --name=SketchAxis")) return 4;
    if (!Run(Host, "plane (0,0,-0.02) 3 2 --name=WingPlane")) return 5;
    // One figure of every folder: a profile, a second line to constrain, a dimension and a constraint.
    if (!Run(Host, "circle (0,0,0) 0.4 --name=ProfileDisc")) return 12;
    if (!Run(Host, "line (-1.4,0.7,0) (1.4,0.9,0) --name=SketchAxisB")) return 13;
    if (!Run(Host, "line (-1.4,1.1,0) (1.4,1.1,0) --name=GuideLine --construction")) return 16;
    if (!Run(Host, "dim Body01 --along=X --name=BodyWidth")) return 14;
    if (!Run(Host, "constraint parallel SketchAxis SketchAxisB")) return 15;
    if (!Run(Host, "select none")) return 6;
    Host.Render();
    Frontier::RasterImage Preview = Host.Raster().Readback();
    gSolidArcRgba = Preview.Pixels.empty() ? nullptr : Preview.Pixels.data();
    gSolidArcW = Preview.Width;
    gSolidArcH = Preview.Height;

    std::vector<unsigned char> Pixels(static_cast<size_t>(gWidth) * static_cast<size_t>(gHeight) * 3u);

    auto Tick = [&](float MouseX, float MouseY, bool Down)
    {
        IO.DeltaTime = 1.0f / 60.0f;
        Editor.TickShade(MouseX, MouseY, Down, 0.0f, 1.0f / 60.0f);
        if (Editor.ShadeCoversPointer())
        {
            IO.AddMousePosEvent(-1.0f, -1.0f);
            IO.AddMouseButtonEvent(0, false);
        }
        else
        {
            IO.AddMousePosEvent(MouseX, MouseY);
            IO.AddMouseButtonEvent(0, Down);
        }
        if (gCtrl != gCtrlSent) { IO.AddKeyEvent(ImGuiMod_Ctrl, gCtrl); gCtrlSent = gCtrl; }
        ImGui::NewFrame();
        Editor.Record(Host);
        ImGui::Render();
        FrontierProof::AcknowledgeTextures();
    };
    auto Rest = [&]() { Tick(-1.0f, -1.0f, false); };
    auto Click = [&](float X, float Y)
    {
        Tick(X, Y, false);
        Tick(X, Y, true);
        Tick(X, Y, false);
    };
    auto Rasterise = [&]()
    {
        // The viewport shows what the document draws now, not the frame the proof opened with.
        Preview = Host.Raster().Readback();
        gSolidArcRgba = Preview.Pixels.empty() ? nullptr : Preview.Pixels.data();
        gSolidArcW = Preview.Width;
        gSolidArcH = Preview.Height;
        // The font atlas grows when new glyph sizes appear, so the sheet is read again for every frame.
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&GlyphSheet, &GlyphSheetWidth, &GlyphSheetHeight);
        for (size_t I = 0u; I < Pixels.size(); I += 3u)
        {
            Pixels[I] = kGround[0]; Pixels[I + 1u] = kGround[1]; Pixels[I + 2u] = kGround[2];
        }
        const ImDrawData* Drawings = ImGui::GetDrawData();
        for (int Index = 0; Index < Drawings->CmdListsCount; ++Index)
            RasterizeList(Drawings->CmdLists[Index], GlyphSheet, GlyphSheetWidth, GlyphSheetHeight, Pixels.data(),
                          Drawings->DisplayPos, Drawings->FramebufferScale);
    };
    auto WriteSheet = [&](const char* Sheet, int FailureCode) -> int
    {
        if (stbi_write_png(Sheet, gWidth, gHeight, 3, Pixels.data(), gWidth * 3) == 0)
        {
            std::fprintf(stderr, "[SolidArcEditorProof] [FAIL] could not write %s\n", Sheet);
            return FailureCode;
        }
        return 0;
    };

    for (int I = 0; I < 12; ++I)
        Rest();
    Rasterise();
    const char* FolderSheet = "Exhibits/Gallery/Editor/EditorProof_SolidArc_Folders.png";
    if (const int Write = WriteSheet(FolderSheet, 16); Write != 0)
        return Write;
    // Fold the four upper folders (bottom first so the rows above do not move) to show all seven folder rows.
    for (const float ChevronY : { 512.0f, 434.0f, 356.0f, 234.0f })
        Click(29.0f, ChevronY);
    for (int I = 0; I < 14; ++I)
        Rest();
    Rasterise();
    const char* FoldedSheet = "Exhibits/Gallery/Editor/EditorProof_SolidArc_FoldersFolded.png";
    if (const int Write = WriteSheet(FoldedSheet, 16); Write != 0)
        return Write;
    const std::vector<unsigned char> FoldedPixels = Pixels;
    // Unfold again, top first, so the filter clicks below meet the rows where they always were.
    for (const float ChevronY : { 234.0f, 356.0f, 434.0f, 512.0f })
    {
        Click(29.0f, ChevronY);
        for (int I = 0; I < 14; ++I)
            Rest();
    }
    for (int I = 0; I < 14; ++I)
        Rest();
    Rasterise();
    Click(240.0f, 187.0f); // Filter dropdown in the SolidArc outliner search row.
    for (int I = 0; I < 3; ++I)
        Rest();
    Rasterise();
    const char* MenuSheet = "Exhibits/Gallery/Editor/EditorProof_SolidArc_Menu.png";
    if (const int Write = WriteSheet(MenuSheet, 7); Write != 0)
        return Write;

    Click(244.0f, 301.0f); // Bodies entry; selected filters appear as chips below the search/filter row.
    for (int I = 0; I < 4; ++I)
        Rest();
    Click(78.0f, 306.0f); // Body01 row after the Bodies filter; seats the CAD inspector like the game proof.
    for (int I = 0; I < 8; ++I)
        Rest();
    Rasterise();

    const char* Sheet = "Exhibits/Gallery/Editor/EditorProof_SolidArc.png";
    if (const int Write = WriteSheet(Sheet, 8); Write != 0)
        return Write;

    int Bright = 0;
    for (size_t I = 0u; I < Pixels.size(); I += 3u)
        if (Pixels[I] > 45u || Pixels[I + 1u] > 45u || Pixels[I + 2u] > 45u)
            ++Bright;
    if (Bright < 5000)
    {
        std::fprintf(stderr, "[SolidArcEditorProof] [FAIL] proof image is nearly empty\n");
        return 9;
    }
    std::fprintf(stderr, "[SolidArcEditorProof] wrote %s and %s with %d bright pixels in the filtered sheet\n", MenuSheet, Sheet, Bright);

    // 🔴 Chrome gates: the SolidArc tab sheet and notch must be the game editor's, pixel for pixel.
    bool Failed = false;
    auto At = [&](int X, int Y) -> const unsigned char* { return &Pixels[(static_cast<size_t>(Y) * gWidth + static_cast<size_t>(X)) * 3u]; };
    auto Fail = [&](const char* Why) { std::fprintf(stderr, "[SolidArcEditorProof] [FAIL] %s\n", Why); Failed = true; };

    // Gate 6 - the outliner carries seven CAD folders, in catalogue order, each drawn in its own colour with its
    // name readable. The folded sheet shows every folder row; the first five sit on a 34 px pitch from y 234 and the open Construction folder pushes the last two down by its child row.
    {
        struct FolderExpectation { const char* Name; int Tint[3]; };
        const FolderExpectation Folders[7] =
        {
            { "Lines",        {  79, 216, 224 } }, { "Profiles",     {  52, 199,  89 } }, { "Bodies",       { 255, 180,  84 } },
            { "Surfaces",     {  77, 163, 255 } }, { "Construction", { 180, 140, 255 } }, { "Dimensions",   { 229, 211,  58 } },
            { "Constraints",  { 255, 107, 138 } },
        };
        const int kFoldedRowY[7] = { 234, 268, 302, 336, 370, 448, 526 };
        auto TintHits = [&](int RowY, const int* Tint) -> int
        {
            int Hits = 0;
            for (int Y = RowY - 14; Y <= RowY + 14; ++Y)
                for (int X = 40; X <= 68; ++X)
                {
                    const unsigned char* P = &FoldedPixels[(static_cast<size_t>(Y) * gWidth + static_cast<size_t>(X)) * 3u];
                    if (std::abs(P[0] - Tint[0]) <= 40 && std::abs(P[1] - Tint[1]) <= 40 && std::abs(P[2] - Tint[2]) <= 40)
                        ++Hits;
                }
            return Hits;
        };
        auto NameCells = [&](int RowY) -> int
        {
            int Cells = 0;
            for (int Y = RowY - 8; Y <= RowY + 8; ++Y)
                for (int X = 76; X <= 170; ++X)
                {
                    const unsigned char* P = &FoldedPixels[(static_cast<size_t>(Y) * gWidth + static_cast<size_t>(X)) * 3u];
                    if (P[0] + P[1] + P[2] > 300)
                        ++Cells;
                }
            return Cells;
        };
        for (int Row = 0; Row < 7; ++Row)
        {
            const int RowY = kFoldedRowY[Row];
            const int Hits = TintHits(RowY, Folders[Row].Tint);
            const int Name = NameCells(RowY);
            std::fprintf(stderr, "[SolidArcEditorProof] folder %d %-12s tint cells %d, name cells %d\n", Row, Folders[Row].Name, Hits, Name);
            if (Hits < 15)
                Fail("a folder is not drawn in its own colour");
            if (Name < 40)
                Fail("a folder name is not readable");
        }
        // Neighbouring folders differ: the amber Bodies folder carries no surface blue, the surface folder no amber.
        if (TintHits(kFoldedRowY[2], Folders[3].Tint) >= 15 || TintHits(kFoldedRowY[3], Folders[2].Tint) >= 15)
            Fail("neighbouring folders share a colour");
        // Negative control: a wrong expectation must be refused, or the gate above proves nothing.
        if (TintHits(kFoldedRowY[2], Folders[6].Tint) >= 15)
            Fail("negative control: the Bodies folder matched the Constraints rose");
        else
            std::fprintf(stderr, "[SolidArcEditorProof] negative control: Bodies folder correctly refuses the Constraints rose\n");
    }

    // Gate 7 - the CAD viewport: the rail holds the Construct chip and the nine web icons, no orbit compass sits in the corner, and the
    //    ground grid is a finite pad of crisp one-pixel lines.
    {
        auto Lit = [&](const std::vector<unsigned char>& Sheet, int X, int Y, int Floor) -> bool
        {
            const unsigned char* P = &Sheet[(static_cast<size_t>(Y) * gWidth + static_cast<size_t>(X)) * 3u];
            return static_cast<int>(P[0]) + static_cast<int>(P[1]) + static_cast<int>(P[2]) > Floor;
        };
        auto CountLit = [&](int X0, int Y0, int X1, int Y1, int Floor) -> int
        {
            int Count = 0;
            for (int Y = Y0; Y < Y1; ++Y)
                for (int X = X0; X < X1; ++X)
                    if (Lit(Pixels, X, Y, Floor))
                        ++Count;
            return Count;
        };
        const int RailChip = CountLit(325, 52, 395, 72, 200);
        // Nine icon buttons sit right of Construct: Body Face Edge Vertex, Wireframe Matcap, Move Rotate Scale.
        static const int kIconX[9] = { 419, 454, 488, 524, 648, 682, 729, 764, 799 };
        static const char* const kIconName[9] = { "Body", "Face", "Edge", "Vertex", "Wireframe", "Matcap", "Move", "Rotate", "Scale" };
        int Seen = 0;
        for (int Icon = 0; Icon < 9; ++Icon)
        {
            const int Lit9 = CountLit(kIconX[Icon] - 8, 54, kIconX[Icon] + 8, 70, 200);
            std::fprintf(stderr, "[SolidArcEditorProof] rail icon %-9s %d lit cells\n", kIconName[Icon], Lit9);
            if (Lit9 < 10)
                Fail("a viewport rail icon is missing");
            else
                ++Seen;
        }
        const int RailRest = CountLit(850, 50, 1030, 72, 200);   // empty rail beyond the last capsule
        std::fprintf(stderr, "[SolidArcEditorProof] rail: Construct chip %d lit cells, %d of 9 icons seen, empty rail %d\n", RailChip, Seen, RailRest);
        if (RailChip < 20)
            Fail("negative control: the rail detector cannot see the Construct chip");
        if (RailRest != 0)
            Fail("the viewport rail draws beyond its icon capsules");

        // The icons are live: a click on each moves its capsule's pick, Shift combines selection modes, and Body restores.
        //    Each click is followed by a rest tick, so what is read is the recorded frame, not the click's own.
        {
            auto Settle = [&]() { Rest(); Rest(); };
            Click(764.0f, 62.0f); Settle();
            Click(648.0f, 62.0f); Settle();
            const bool Turned = Editor.QueryRailGizmo() == 1u && Editor.QueryRailShade() == 0u;
            Click(454.0f, 62.0f); Settle();
            const bool OnlyFace = Editor.QueryRailSelectMask() == 2u;
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, true);
            Click(524.0f, 62.0f); Settle();
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);
            const bool Combined = Editor.QueryRailSelectMask() == 10u;
            std::fprintf(stderr, "[SolidArcEditorProof] rail clicks: gizmo %u shade %u mask %u\n", Editor.QueryRailGizmo(), Editor.QueryRailShade(), Editor.QueryRailSelectMask());
            if (!Turned) Fail("the rail's Rotate and Wireframe icons did not take the click");
            if (!OnlyFace) Fail("the rail's Face icon did not become the only selection mode");
            if (!Combined) Fail("Shift-click on Vertex did not combine with Face");
            Click(419.0f, 62.0f); Settle();
            Click(682.0f, 62.0f); Settle();
            Click(729.0f, 62.0f); Settle();
            if (Editor.QueryRailSelectMask() != 1u || Editor.QueryRailShade() != 1u || Editor.QueryRailGizmo() != 0u)
                Fail("the rail did not return to Body, Matcap and Move");
        }

        // The compass is built of saturated filled pads (7 px radius) and a bright ring; the lattice's axis lines are thin.
        //    A saturated 6x6 window in the corner is a pad. The control looks at the Move gizmo, which carries such pads.
        auto Pad = [&](int X0, int Y0, int X1, int Y1) -> int
        {
            auto Saturated = [&](int X, int Y) -> bool
            {
                const unsigned char* P = &Pixels[(static_cast<size_t>(Y) * gWidth + static_cast<size_t>(X)) * 3u];
                return std::max({ P[0], P[1], P[2] }) - std::min({ P[0], P[1], P[2] }) > 70;
            };
            int Pads = 0;
            for (int Y = Y0; Y < Y1 - 6; ++Y)
                for (int X = X0; X < X1 - 6; ++X)
                {
                    bool Full = true;
                    for (int DY = 0; DY < 6 && Full; ++DY)
                        for (int DX = 0; DX < 6 && Full; ++DX)
                            Full = Saturated(X + DX, Y + DY);
                    if (Full)
                        ++Pads;
                }
            return Pads;
        };
        const int Corner = Pad(930, 580, 1022, 668);
        const int GizmoPads = Pad(590, 300, 780, 400);
        std::fprintf(stderr, "[SolidArcEditorProof] bottom-right corner: %d saturated pad windows (gizmo control: %d)\n", Corner, GizmoPads);
        if (GizmoPads == 0)
            Fail("negative control: the compass detector cannot see a saturated pad");
        if (Corner != 0)
            Fail("the viewport still draws a compass in its bottom-right corner");

        // Finite pad: the far field above the grid's fade carries no lattice ink at all.
        const int FarField = CountLit(330, 76, 1020, 112, 90);
        std::fprintf(stderr, "[SolidArcEditorProof] far field above the pad: %d lit cells\n", FarField);
        if (FarField != 0)
            Fail("the ground grid runs on to the horizon instead of ending as a pad");

        // Crisp: a one-pixel line, even crossed by another, almost never fills a 4x4 window of bright pixels. A blurred or fattened line does.
        //    The model and its gizmo, which are legitimately solid, are excluded.
        auto Thick = [&](const std::vector<unsigned char>& Sheet) -> int
        {
            int Blobs = 0;
            for (int Y = 120; Y < 660; ++Y)
                for (int X = 330; X < 1015; ++X)
                {
                    if (X > 560 && X < 800 && Y > 120 && Y < 440)
                        continue;
                    bool Full = true;
                    for (int DY = 0; DY < 4 && Full; ++DY)
                        for (int DX = 0; DX < 4 && Full; ++DX)
                            Full = Lit(Sheet, X + DX, Y + DY, 300);
                    if (Full)
                        ++Blobs;
                }
            return Blobs;
        };
        const int Blobs = Thick(Pixels);
        // Negative control: the same sheet smeared 3 px wider in both axes must read as blurred.
        std::vector<unsigned char> Smeared = Pixels;
        for (int Y = 1; Y < gHeight - 1; ++Y)
            for (int X = 1; X < gWidth - 1; ++X)
                for (int C = 0; C < 3; ++C)
                {
                    unsigned char Peak = 0;
                    for (int DY = -1; DY <= 1; ++DY)
                        for (int DX = -1; DX <= 1; ++DX)
                            Peak = std::max(Peak, Pixels[(static_cast<size_t>(Y + DY) * gWidth + static_cast<size_t>(X + DX)) * 3u + static_cast<size_t>(C)]);
                    Smeared[(static_cast<size_t>(Y) * gWidth + static_cast<size_t>(X)) * 3u + static_cast<size_t>(C)] = Peak;
                }
        const int SmearedBlobs = Thick(Smeared);
        std::fprintf(stderr, "[SolidArcEditorProof] grid 4x4 solid windows (limit 40): %d (smeared control: %d)\n", Blobs, SmearedBlobs);
        // A handful of 4x4 windows survive where two bright major lines cross; a fat or blurred line gives thousands.
        if (Blobs > 40)
            Fail("grid lines are thick or blurred");
        if (SmearedBlobs < 500)
            Fail("negative control: the crispness gate did not catch a smeared grid");
    }

    // Gate 8 - the transform gizmo paints no back-facing triangle (the raster tints those warm red, which showed as pink and
    //    purple patches on the arrows), and no world-origin triad is drawn: the only difference between a selected and an
    //    unselected frame is the gizmo itself.
    {
        Host.Render();
        const Frontier::RasterExchange::Tally With = Host.Raster().QueryTally();
        if (!Run(Host, "select none")) return 17;
        Host.Render();
        const Frontier::RasterExchange::Tally Without = Host.Raster().QueryTally();
        if (!Run(Host, "select Body01")) return 18;
        Host.Render();
        std::fprintf(stderr, "[SolidArcEditorProof] gizmo: triangles %u -> %u, segments %u -> %u, back-facing %u -> %u\n",
                     Without.Triangles, With.Triangles, Without.Segments, With.Segments, Without.BackFacing, With.BackFacing);
        if (With.Triangles <= Without.Triangles + 100u)
            Fail("negative control: the selected frame carries no gizmo triangles");
        if (With.BackFacing != Without.BackFacing)
            Fail("the gizmo paints back-facing triangles (pink and purple winding-fault patches)");
        if (Without.Points != 0u)
            Fail("a world-origin triad is still drawn (its three disc heads are points)");
    }

    // Gate 1 - the seated tab carries the window tint #121212, never ImGui's blue.
    const int TabProbeX[3] = { 60, 470, 1100 };
    for (int Probe = 0; Probe < 3; ++Probe)
    {
        const unsigned char* P = At(TabProbeX[Probe], 26);
        std::fprintf(stderr, "[SolidArcEditorProof] tab %d body at (%d,26): %u %u %u\n", Probe, TabProbeX[Probe], P[0], P[1], P[2]);
        if (std::abs(static_cast<int>(P[0]) - 18) > 1 || std::abs(static_cast<int>(P[1]) - 18) > 1 || std::abs(static_cast<int>(P[2]) - 18) > 1)
            Fail("a selected tab is not the seated #121212");
    }

    // Gate 2 - no blue anywhere in the tab band (stock selected overline and stock Tab* colours are blue).
    int BlueCells = 0;
    for (int Y = 0; Y < 28; ++Y)
        for (int X = 0; X < gWidth; ++X)
        {
            const unsigned char* P = At(X, Y);
            if (P[2] > P[0] + 25u && P[2] > P[1] + 10u)
                ++BlueCells;
        }
    std::fprintf(stderr, "[SolidArcEditorProof] blue cells in the tab band: %d\n", BlueCells);
    if (BlueCells != 0)
        Fail("the tab band still carries blue cells");

    // Gate 3 - the tab is a trapezoid: its right edge sits further left on the top row than on the bottom row.
    auto RightEdge = [&](int Y) -> int
    {
        int Edge = -1;
        for (int X = 0; X < 230; ++X)
        {
            const unsigned char* P = At(X, Y);
            if (P[0] == 18u && P[1] == 18u && P[2] == 18u)
                Edge = X;
        }
        return Edge;
    };
    const int TopEdge = RightEdge(3), FootEdge = RightEdge(27);
    std::fprintf(stderr, "[SolidArcEditorProof] outliner tab right edge: top %d, foot %d\n", TopEdge, FootEdge);
    if (TopEdge < 0 || FootEdge - TopEdge < 8)
        Fail("the tab is not slanted like the game editor's trapezoid");

    // Gate 4 - the notch sits in the band and carries its brand.
    {
        const int PullX = static_cast<int>(Editor.QueryNotchX() + 0.5f);
        const int PullY = static_cast<int>(Editor.QueryNotchY() + 0.5f);
        int Brand = 0;
        for (int Y = PullY - 10; Y < PullY + 10; ++Y)
            for (int X = PullX - 40; X < PullX + 40; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] > 60u || P[1] > 60u || P[2] > 60u)
                    ++Brand;
            }
        std::fprintf(stderr, "[SolidArcEditorProof] notch at (%d,%d), brand %d bright cells\n", PullX, PullY, Brand);
        if (PullY > 30 || Brand < 50)
            Fail("the Control Centre notch is missing from the band");
    }

    // Gate 5 - the notch works: a tap pulls the sheet down, the sheet is the Control Centre, the grip shuts it.
    Click(Editor.QueryNotchX(), Editor.QueryNotchY());
    for (int I = 0; I < 40; ++I)
        Rest();
    std::fprintf(stderr, "[SolidArcEditorProof] shade after notch tap: %s\n", Editor.QueryShadeOpen() ? "open" : "shut");
    if (!Editor.QueryShadeOpen())
        Fail("the notch tap never drew the Control Centre");
    Rasterise();
    const char* ShadeSheet = "Exhibits/Gallery/Editor/EditorProof_SolidArc_Shade.png";
    if (const int Write = WriteSheet(ShadeSheet, 10); Write != 0)
        return Write;
    int Lit = 0;
    for (int Y = 150; Y < 500; ++Y)
        for (int X = 400; X < 880; ++X)
            if (At(X, Y)[2] > At(X, Y)[0] + 60u)
                ++Lit;
    std::fprintf(stderr, "[SolidArcEditorProof] Control Centre tile cells: %d\n", Lit);
    if (Lit < 2000)
        Fail("the pulled sheet does not show the Control Centre tiles");
    Click(Editor.QueryGripX(), Editor.QueryGripY());
    for (int I = 0; I < 40; ++I)
        Rest();
    std::fprintf(stderr, "[SolidArcEditorProof] shade after grip tap: %s\n", Editor.QueryShadeOpen() ? "open" : "shut");
    if (Editor.QueryShadeOpen())
        Fail("the grip tap never shut the Control Centre");
    if (Failed)
        return 11;
    std::fprintf(stderr, "[SolidArcEditorProof] chrome gates GREEN: game-editor tab sheet and Control Centre notch\n");

    // Gate 9 - the inspector is the document-style sheet: head pill, hero, presence cells, tab chips, cards, action tiles. The first
    // shot is the picked body at the proof size; then the window grows tall and every object is picked in turn and shot whole.
    {
        using Role = Frontier::SolidArcOutlinerBinding::Role;
        auto Settle = [&]()
        {
            for (int I = 0; I < 10; ++I)
                Rest();
        };
        Editor.PickRow("Body01", Role::Figure);
        Settle();
        Rasterise();
        if (const int Write = WriteSheet("Exhibits/Gallery/Editor/EditorProof_SolidArc_Inspector.png", 8); Write != 0)
            return Write;

        gWidth  = 1600;
        gHeight = 2100;
        IO.DisplaySize = ImVec2(static_cast<float>(gWidth), static_cast<float>(gHeight));
        Pixels.assign(static_cast<size_t>(gWidth) * static_cast<size_t>(gHeight) * 3u, 0u);
        Editor.ReseatLayout();
        std::filesystem::create_directories("Exhibits/Gallery/Editor/SolidArcObjects");
        Settle();
        Click(82.0f, 228.0f);        // the Bodies chip's close mark: the outliner lists every folder again for the per-object shots
        Settle();
        struct Shot { const char* Label; Role Row; const char* File; };
        const Shot Shots[] =
        {
            { "SketchAxis",  Role::Figure,     "Exhibits/Gallery/Editor/SolidArcObjects/Object_Line.png" },
            { "ProfileDisc", Role::Figure,     "Exhibits/Gallery/Editor/SolidArcObjects/Object_Profile.png" },
            { "Body01",      Role::Figure,     "Exhibits/Gallery/Editor/SolidArcObjects/Object_Body.png" },
            { "CanopySheet", Role::Figure,     "Exhibits/Gallery/Editor/SolidArcObjects/Object_Surface.png" },
            { "GuideLine",   Role::Figure,     "Exhibits/Gallery/Editor/SolidArcObjects/Object_Construction.png" },
            { "WingPlane",   Role::Figure,     "Exhibits/Gallery/Editor/SolidArcObjects/Object_Workplane.png" },
            { nullptr,       Role::Dimension,  "Exhibits/Gallery/Editor/SolidArcObjects/Object_Dimension.png" },
            { nullptr,       Role::Constraint, "Exhibits/Gallery/Editor/SolidArcObjects/Object_Constraint.png" },
        };
        for (const Shot& One : Shots)
        {
            if (!Editor.PickRow(One.Label, One.Row))
            {
                std::fprintf(stderr, "[SolidArcEditorProof] [FAIL] no outliner row for %s\n", One.File);
                Failed = true;
                continue;
            }
            Settle();
            Rasterise();
            if (const int Write = WriteSheet(One.File, 8); Write != 0)
                return Write;
        }
        Editor.ClearPick();
        Settle();
        Rasterise();
        if (const int Write = WriteSheet("Exhibits/Gallery/Editor/SolidArcObjects/Object_Document.png", 8); Write != 0)
            return Write;

        // The live controls on the body's sheet (positions are the 1600x2100 layout: presence cells at y 360, Position X at y 558,
        // action tiles at y 1760). Locked refuses the move, unlocked allows it; Dimensions hides the anchored dimension; the
        // action tiles duplicate, isolate and delete.
        auto FindFigure = [&](const char* Name) -> const Frontier::SceneFigure*
        {
            for (const Frontier::SceneFigure& Figure : Host.AllFigures())
                if (Figure.Name == Name)
                    return &Figure;
            return nullptr;
        };
        auto DragPill = [&](float X0, float X1, float Y)
        {
            Tick(X0, Y, false);
            Tick(X0, Y, true);
            Tick(X1, Y, true);
            Tick(X1, Y, true);
            Tick(X1, Y, false);
            Settle();
        };
        Editor.PickRow("Body01", Role::Figure);
        Settle();
        Click(1367.0f, 360.0f);                                  // Locked
        Settle();
        const Frontier::SceneFigure* Body = FindFigure("Body01");
        if (Body == nullptr || !Body->Locked)
            Fail("the Locked cell did not lock the body");
        const double LockedX = Body != nullptr ? Body->Bounds().Centre().X : 0.0;
        DragPill(1360.0f, 1400.0f, 558.0f);
        Body = FindFigure("Body01");
        if (Body != nullptr && std::fabs(Body->Bounds().Centre().X - LockedX) > 1e-6)
            Fail("a locked body still moved when its Position X was dragged");
        Click(1367.0f, 360.0f);                                  // Locked again: lifted
        Settle();
        Body = FindFigure("Body01");
        if (Body == nullptr || Body->Locked)
            Fail("the Locked cell did not lift");
        DragPill(1360.0f, 1400.0f, 558.0f);
        Body = FindFigure("Body01");
        std::fprintf(stderr, "[SolidArcEditorProof] Position X drag: centre X %.3f -> %.3f\n", LockedX, Body != nullptr ? Body->Bounds().Centre().X : 0.0);
        if (Body == nullptr || Body->Bounds().Centre().X - LockedX < 0.15)
            Fail("dragging the Position X pill did not move the unlocked body");

        Click(1537.0f, 360.0f);                                  // Dimensions
        Settle();
        size_t Hidden = 0u;
        for (const Frontier::ConsoleHost::DimensionEntry& Dimension : Host.AllDimensions())
            if (Dimension.Anchor == (Body != nullptr ? Body->Identity : 0u) && Dimension.Hidden)
                ++Hidden;
        if (Hidden == 0u)
            Fail("the Dimensions cell did not hide the body's dimension");

        const size_t Before = Host.AllFigures().size();
        Click(1296.0f, 1760.0f);                                 // Duplicate
        Settle();
        std::fprintf(stderr, "[SolidArcEditorProof] Duplicate tile: %zu -> %zu figures\n", Before, Host.AllFigures().size());
        if (Host.AllFigures().size() != Before + 1u)
            Fail("the Duplicate tile did not copy the body");
        Editor.PickRow("Body01", Role::Figure);
        Settle();
        Click(1409.0f, 1760.0f);                                 // Isolate
        Settle();
        size_t Shown = 0u;
        for (const Frontier::SceneFigure& Figure : Host.AllFigures())
            if (!Figure.Hidden)
                ++Shown;
        std::fprintf(stderr, "[SolidArcEditorProof] Isolate tile: %zu figure(s) shown\n", Shown);
        if (Shown != 1u)
            Fail("the Isolate tile did not leave exactly the body shown");
        Run(Host, "isolate off");
        Editor.PickRow("Body01", Role::Figure);
        Settle();
        const size_t Total = Host.AllFigures().size();
        Click(1523.0f, 1760.0f);                                 // Delete
        Settle();
        std::fprintf(stderr, "[SolidArcEditorProof] Delete tile: %zu -> %zu figures\n", Total, Host.AllFigures().size());
        if (Host.AllFigures().size() != Total - 1u)
            Fail("the Delete tile did not remove the body");
        if (Failed)
            return 16;
        std::fprintf(stderr, "[SolidArcEditorProof] inspector shots written for every object\n");
    }

    // Gate 10 - the Construct menu opens from its chip, shows its four sections, and every tile it offers places a figure that
    //    draws: the figure count grows by one, the new figure is the picked one, and the raster (with the selection cleared,
    //    so only the figure itself can have changed it) differs from the raster before.
    {
        gWidth  = 1280;
        gHeight = 720;
        IO.DisplaySize = ImVec2(static_cast<float>(gWidth), static_cast<float>(gHeight));
        Pixels.assign(static_cast<size_t>(gWidth) * static_cast<size_t>(gHeight) * 3u, 0u);
        Editor.ReseatLayout();
        auto Settle = [&](int Ticks = 6)
        {
            for (int I = 0; I < Ticks; ++I)
                Rest();
        };
        constexpr float kChipX = 350.0f, kChipY = 62.0f;
        Settle(10);
        Click(kChipX, kChipY);
        Settle();
        if (!Editor.QueryConstructOpen())
            Fail("the Construct chip did not open the menu");
        Rasterise();
        std::filesystem::create_directories("Exhibits/Gallery/Editor/SolidArcConstruct");
        if (const int Write = WriteSheet("Exhibits/Gallery/Editor/SolidArcConstruct/Menu_Reference.png", 8); Write != 0)
            return Write;
        const char* const SectionFile[4] = { "Menu_Reference", "Menu_Sketch", "Menu_Solid", "Menu_Surface" };
        for (uint32_t Section = 0u; Section < 4u; ++Section)
        {
            float Sx = 0.0f, Sy = 0.0f;
            if (!Editor.QueryConstructSectionCentre(Section, &Sx, &Sy))
            {
                Fail("the Construct menu is missing a section");
                continue;
            }
            Click(Sx, Sy);
            Settle();
            Rasterise();
            char File[160];
            std::snprintf(File, sizeof(File), "Exhibits/Gallery/Editor/SolidArcConstruct/%s.png", SectionFile[Section]);
            if (const int Write = WriteSheet(File, 8); Write != 0)
                return Write;
        }
        // Esc shuts it, and the chip opens it again.
        IO.AddKeyEvent(ImGuiKey_Escape, true);
        Settle(2);
        IO.AddKeyEvent(ImGuiKey_Escape, false);
        Settle(2);
        const bool ShutByEscape = !Editor.QueryConstructOpen();
        if (!ShutByEscape)
            Fail("Escape did not close the Construct menu");

        const uint32_t Tiles = Frontier::SolidArcEditorHost::QueryConstructTileCount();
        uint32_t Drawn = 0u;
        for (uint32_t Tile = 0u; Tile < Tiles; ++Tile)
        {
            Run(Host, "select none");
            Host.Render();
            const std::vector<unsigned char> Before = Host.Raster().Readback().Pixels;
            const size_t FiguresBefore = Host.AllFigures().size();

            if (!Editor.QueryConstructOpen())
            {
                Click(kChipX, kChipY);
                Settle();
            }
            bool Seen = false;
            float Tx = 0.0f, Ty = 0.0f;
            for (uint32_t Section = 0u; Section < 4u && !Seen; ++Section)
            {
                float Sx = 0.0f, Sy = 0.0f;
                if (!Editor.QueryConstructSectionCentre(Section, &Sx, &Sy))
                    break;
                Click(Sx, Sy);
                Settle(3);
                Seen = Editor.QueryConstructTileCentre(Tile, &Tx, &Ty);
            }
            if (!Seen)
            {
                std::fprintf(stderr, "[SolidArcEditorProof] construct tile %u: not reachable in the menu\n", Tile);
                Fail("a Construct tile cannot be reached through the menu");
                continue;
            }
            Click(Tx, Ty);
            Settle(4);
            if (Editor.QueryConstructOpen())
                Fail("the menu stayed open after a tile was chosen");
            const size_t FiguresAfter = Host.AllFigures().size();
            if (FiguresAfter != FiguresBefore + 1u)
            {
                std::fprintf(stderr, "[SolidArcEditorProof] construct tile %u: figures %zu -> %zu\n", Tile, FiguresBefore, FiguresAfter);
                Fail("a Construct tile did not place exactly one figure");
                continue;
            }
            const Frontier::SceneFigure& Made = Host.AllFigures().back();
            Run(Host, "select none");
            Host.Render();
            const std::vector<unsigned char> After = Host.Raster().Readback().Pixels;
            size_t Changed = 0u;
            for (size_t I = 0u; I + 3u < Before.size() && I + 3u < After.size(); I += 4u)
                if (std::abs(static_cast<int>(Before[I]) - static_cast<int>(After[I])) + std::abs(static_cast<int>(Before[I + 1u]) - static_cast<int>(After[I + 1u]))
                    + std::abs(static_cast<int>(Before[I + 2u]) - static_cast<int>(After[I + 2u])) > 24)
                    ++Changed;
            std::fprintf(stderr, "[SolidArcEditorProof] construct tile %2u -> %-14s drew %5zu changed pixels\n", Tile, Made.Name.c_str(), Changed);
            if (Changed < 20u)
                Fail("a Construct figure placed but nothing of it is drawn");
            else
                ++Drawn;
        }
        std::fprintf(stderr, "[SolidArcEditorProof] Construct menu: %u of %u tiles placed a figure that draws\n", Drawn, Tiles);
        if (Drawn != Tiles)
            Fail("not every Construct tile draws");
        Run(Host, "view fit");
        Settle(10);
        Rasterise();
        if (const int Write = WriteSheet("Exhibits/Gallery/Editor/SolidArcConstruct/Placed_All.png", 8); Write != 0)
            return Write;
        if (Failed)
            return 17;
    }

    // Gate 11 - the viewport picture is anti-aliased and sized to its view, and the selection is one thing in three places:
    //    taps, Ctrl taps, a Ctrl-dragged box, Ctrl+A, Escape and Delete in the view carry to the document and the outliner,
    //    and the outliner's own multi-pick carries to the document and the view.
    {
        gWidth  = 1280;
        gHeight = 720;
        IO.DisplaySize = ImVec2(static_cast<float>(gWidth), static_cast<float>(gHeight));
        Pixels.assign(static_cast<size_t>(gWidth) * static_cast<size_t>(gHeight) * 3u, 0u);
        Editor.ReseatLayout();
        auto Settle = [&](int Ticks = 6)
        {
            for (int I = 0; I < Ticks; ++I)
                Rest();
        };
        std::filesystem::create_directories("Exhibits/Gallery/Editor/SolidArcSelect");
        // A lone sphere sheet against the black ground: its rim is the cleanest silhouette there is.
        if (!Run(Host, "reset")) return 20;
        if (!Run(Host, "sphere (0,0,0.9) 1.0 --sheet --name=RimSheet")) return 21;
        if (!Run(Host, "view fit")) return 25;
        Settle(12);

        // The picture: the same view drawn with one sample per pixel and with the host's two.
        {
            const uint32_t W = Host.Raster().Width(), H = Host.Raster().Height();
            auto Edges = [&](uint32_t Samples, const char* Name, double* Smooth) -> int
            {
                Host.SeatSurface(W, H, Samples);
                Host.Render();
                const Frontier::RasterImage Image = Host.Raster().Readback();
                int Boundary = 0, Blended = 0;
                for (uint32_t Y = 1u; Y + 1u < H; ++Y)
                    for (uint32_t X = 1u; X + 1u < W; ++X)
                    {
                        const uint32_t Here = Host.Raster().Pick(X, Y);
                        if (Here != 0u) continue;                                      // the ground pixel just outside the figure
                        const uint32_t Around[4] = { Host.Raster().Pick(X - 1u, Y), Host.Raster().Pick(X + 1u, Y), Host.Raster().Pick(X, Y - 1u), Host.Raster().Pick(X, Y + 1u) };
                        if (Around[0] == 0u && Around[1] == 0u && Around[2] == 0u && Around[3] == 0u) continue;
                        const uint8_t* Q = &Image.Pixels[(static_cast<size_t>(Y) * W + X) * 4u];
                        ++Boundary;
                        if (static_cast<int>(Q[0]) + Q[1] + Q[2] > 40) ++Blended;      // ground that took some of the figure's colour: a partly covered pixel
                    }
                *Smooth = Boundary > 0 ? static_cast<double>(Blended) / Boundary : 0.0;
                std::vector<unsigned char> Crop(static_cast<size_t>(W) * H * 3u);
                for (size_t I = 0u; I < static_cast<size_t>(W) * H; ++I)
                    for (int C = 0; C < 3; ++C) Crop[I * 3u + C] = Image.Pixels[I * 4u + C];
                std::string Path = std::string("Exhibits/Gallery/Editor/SolidArcSelect/") + Name;
                if (stbi_write_png(Path.c_str(), static_cast<int>(W), static_cast<int>(H), 3, Crop.data(), static_cast<int>(W) * 3) == 0)
                    return 26;
                return 0;
            };
            double Plain = 0.0, Smooth = 0.0;
            if (const int Write = Edges(1u, "Raster_OneSample.png", &Plain); Write != 0) return Write;
            if (const int Write = Edges(2u, "Raster_TwoSamples.png", &Smooth); Write != 0) return Write;
            std::fprintf(stderr, "[SolidArcEditorProof] silhouette pixels partly covered: one sample %.1f %%, two samples %.1f %%  (view %ux%u)\n", Plain * 100.0, Smooth * 100.0, W, H);
            if (Smooth < 0.25 || Smooth < Plain * 2.0)
                Fail("the silhouette is not anti-aliased");
            Settle(4);
        }

        if (!Run(Host, "reset")) return 20;
        if (!Run(Host, "box (-3.2,-0.8,0) (-1.8,0.8,1.2) --name=BoxA")) return 21;
        if (!Run(Host, "box (-0.7,-0.8,0) (0.7,0.8,1.2) --name=BoxB")) return 22;
        if (!Run(Host, "sphere (2.5,0,0.7) 0.7 --name=BallC")) return 23;
        if (!Run(Host, "circle (3.6,2.6,0) 0.8 --name=RingD")) return 24;
        if (!Run(Host, "view fit")) return 25;
        Settle(12);

        // Screen positions of the figures, found where the raster's own pick plane says they are.
        auto Locate = [&](const char* Name, float* ScreenX, float* ScreenY, float* LowX, float* LowY, float* HighX, float* HighY) -> bool
        {
            const Frontier::SceneFigure* Figure = Host.Document().Find(std::string(Name));
            if (Figure == nullptr) return false;
            const uint32_t W = Host.Raster().Width(), H = Host.Raster().Height();
            double SumX = 0.0, SumY = 0.0; int Count = 0;
            int MinX = static_cast<int>(W), MinY = static_cast<int>(H), MaxX = 0, MaxY = 0;
            for (uint32_t Y = 0u; Y < H; ++Y)
                for (uint32_t X = 0u; X < W; ++X)
                    if (Frontier::SceneDocument::IdentityOf(Host.Raster().Pick(X, Y)) == Figure->Identity)
                    {
                        SumX += X; SumY += Y; ++Count;
                        MinX = std::min(MinX, static_cast<int>(X)); MaxX = std::max(MaxX, static_cast<int>(X));
                        MinY = std::min(MinY, static_cast<int>(Y)); MaxY = std::max(MaxY, static_cast<int>(Y));
                    }
            if (Count < 40) return false;
            // The pixel of the figure nearest its mean, so a curve (which has no interior) is still hit.
            const double MeanX = SumX / Count, MeanY = SumY / Count;
            double Best = 1e18; int PickX = 0, PickY = 0;
            for (int Y = MinY; Y <= MaxY; ++Y)
                for (int X = MinX; X <= MaxX; ++X)
                    if (Frontier::SceneDocument::IdentityOf(Host.Raster().Pick(static_cast<uint32_t>(X), static_cast<uint32_t>(Y))) == Figure->Identity)
                    {
                        const double D = (X - MeanX) * (X - MeanX) + (Y - MeanY) * (Y - MeanY);
                        if (D < Best) { Best = D; PickX = X; PickY = Y; }
                    }
            const float Scale = Editor.QueryViewWidth() / static_cast<float>(W);
            auto ToX = [&](float Px) { return Editor.QueryViewOriginX() + (Px + 0.5f) * Scale; };
            auto ToY = [&](float Py) { return Editor.QueryViewOriginY() + (Py + 0.5f) * Scale; };
            *ScreenX = ToX(static_cast<float>(PickX)); *ScreenY = ToY(static_cast<float>(PickY));
            *LowX = ToX(static_cast<float>(MinX)); *LowY = ToY(static_cast<float>(MinY));
            *HighX = ToX(static_cast<float>(MaxX)); *HighY = ToY(static_cast<float>(MaxY));
            return true;
        };
        struct Spot { float X = 0, Y = 0, Lx = 0, Ly = 0, Hx = 0, Hy = 0; };
        Spot A, B, Ball, Ring;
        if (!Locate("BoxA", &A.X, &A.Y, &A.Lx, &A.Ly, &A.Hx, &A.Hy) || !Locate("BoxB", &B.X, &B.Y, &B.Lx, &B.Ly, &B.Hx, &B.Hy)
            || !Locate("BallC", &Ball.X, &Ball.Y, &Ball.Lx, &Ball.Ly, &Ball.Hx, &Ball.Hy) || !Locate("RingD", &Ring.X, &Ring.Y, &Ring.Lx, &Ring.Ly, &Ring.Hx, &Ring.Hy))
        {
            Fail("a figure of the selection scene is not on screen");
            return 27;
        }
        const uint32_t IdA = Host.Document().Find(std::string("BoxA"))->Identity;
        const uint32_t IdB = Host.Document().Find(std::string("BoxB"))->Identity;
        const uint32_t IdBall = Host.Document().Find(std::string("BallC"))->Identity;
        auto Chosen = [&](uint32_t Identity) { const Frontier::SceneFigure* F = Host.Document().Find(Identity); return F != nullptr && F->Selected; };
        auto Report = [&](const char* What, int Want)
        {
            const int Have = Host.Document().SelectedCount();
            std::fprintf(stderr, "[SolidArcEditorProof] select: %-34s document %d, outliner pick %u (want %d)\n", What, Have, Editor.QueryPickedCount(), Want);
            if (Have != Want) Fail("the document's selection is not what the view did");
            if (static_cast<int>(Editor.QueryPickedCount()) != Want) Fail("the outliner's pick did not follow the view");
        };
        auto Key = [&](ImGuiKey Code)
        {
            IO.AddKeyEvent(Code, true);
            Settle(2);
            IO.AddKeyEvent(Code, false);
            Settle(3);
        };
        auto Shot = [&](const char* Name)
        {
            Rasterise();
            std::string Path = std::string("Exhibits/Gallery/Editor/SolidArcSelect/") + Name;
            return WriteSheet(Path.c_str(), 8);
        };

        // Taps. Empty space clears; a figure picks; Ctrl adds and takes away.
        Click(Ball.X, Ball.Y); Settle();
        Report("tap BallC", 1);
        if (!Chosen(IdBall)) Fail("the tapped figure is not the selected one");
        gCtrl = true;
        Click(B.X, B.Y); Settle();
        Report("Ctrl+tap BoxB adds", 2);
        Click(A.X, A.Y); Settle();
        Report("Ctrl+tap BoxA adds", 3);
        if (const int Write = Shot("Select_Three.png"); Write != 0) return Write;
        Click(Ball.X, Ball.Y); Settle();
        Report("Ctrl+tap BallC takes away", 2);
        if (Chosen(IdBall) || !Chosen(IdA) || !Chosen(IdB)) Fail("Ctrl+tap took the wrong figure away");
        gCtrl = false;
        Click(Ball.X, Ball.Y); Settle();
        Report("tap BallC alone", 1);

        // Keys.
        gCtrl = true; Key(ImGuiKey_A); gCtrl = false;
        Report("Ctrl+A", Host.Document().Figures().size() > 0u ? static_cast<int>(Host.Document().Figures().size()) : 0);
        Key(ImGuiKey_Escape);
        Report("Escape", 0);

        // The box: Ctrl+drag over BoxA and BoxB, short of the ball.
        {
            const float X0 = A.X - 5.0f, Y0 = std::min(A.Y, B.Y) - 5.0f, X1 = B.X + 5.0f, Y1 = std::max(A.Y, B.Y) + 5.0f;
            gCtrl = true;
            Tick(X0, Y0, false); Tick(X0, Y0, true);
            for (int Step = 1; Step <= 6; ++Step)
                Tick(X0 + (X1 - X0) * Step / 6.0f, Y0 + (Y1 - Y0) * Step / 6.0f, true);
            if (const int Write = Shot("Select_BoxDrag.png"); Write != 0) return Write;
            Tick(X1, Y1, false);
            gCtrl = false;
            Settle();
            Report("Ctrl+drag box over BoxA and BoxB", 2);
            if (!Chosen(IdA) || !Chosen(IdB) || Chosen(IdBall)) Fail("the box took the wrong figures");
            if (const int Write = Shot("Select_BoxDone.png"); Write != 0) return Write;
        }

        // The outliner leads the other way: its own Ctrl pick becomes the document's selection.
        Run(Host, "select none"); Settle();
        Report("select none", 0);
        Editor.PickRow("BoxA", Frontier::SolidArcOutlinerBinding::Role::Figure); Settle();
        Editor.ExtendRow("BallC", Frontier::SolidArcOutlinerBinding::Role::Figure); Settle();
        Editor.ExtendRow("RingD", Frontier::SolidArcOutlinerBinding::Role::Figure); Settle();
        Report("outliner pick BoxA + BallC + RingD", 3);
        if (!Chosen(IdA) || !Chosen(IdBall) || Chosen(IdB)) Fail("the outliner's pick did not become the document's selection");
        if (const int Write = Shot("Select_Outliner.png"); Write != 0) return Write;

        // Delete takes the selected figures, locked ones excepted.
        const size_t Before = Host.AllFigures().size();
        Key(ImGuiKey_Delete);
        std::fprintf(stderr, "[SolidArcEditorProof] select: Delete removed %zu of %zu figures\n", Before - Host.AllFigures().size(), Before);
        if (Host.AllFigures().size() != Before - 3u) Fail("Delete did not remove the three selected figures");
        Settle(6);
        if (const int Write = Shot("Select_AfterDelete.png"); Write != 0) return Write;
        if (Failed)
            return 28;
    }
    ImGui::DestroyContext();
    return 0;
}
