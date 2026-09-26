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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static_assert(sizeof(ImDrawIdx) == 2u, "the proof rasteriser walks 16-bit ImGui indices");

namespace {

constexpr int kWidth  = 1280;
constexpr int kHeight = 720;
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
        if (ScissorRight > kWidth) ScissorRight = kWidth;
        if (ScissorTop < 0) ScissorTop = 0;
        if (ScissorBottom > kHeight) ScissorBottom = kHeight;

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
                    OverlayPixel(&Pixels[(static_cast<size_t>(Y) * kWidth + static_cast<size_t>(X)) * 3u], Tinted);
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
    IO.DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));
    IO.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    IO.IniFilename = nullptr;
    IO.LogFilename = nullptr;
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    Frontier::SolidArcEditorHost Editor;
    Editor.ApplyTheme();

    if (IO.Fonts->Fonts.empty())
        IO.Fonts->AddFontDefault();

    unsigned char* GlyphSheet = nullptr;
    int GlyphSheetWidth = 0, GlyphSheetHeight = 0;
    IO.Fonts->GetTexDataAsRGBA32(&GlyphSheet, &GlyphSheetWidth, &GlyphSheetHeight);

    Frontier::ConsoleHost Host("/tmp/solidarc-editor-proof", 520, 340);
    if (!Run(Host, "box (-1.2,-0.5,0) (1.2,0.5,0.8) --name=Body01")) return 2;
    if (!Run(Host, "sphere (0.0,0.0,1.15) 0.35 --sheet --name=CanopySheet")) return 3;
    if (!Run(Host, "line (-1.4,-0.7,0) (1.4,-0.7,0) --name=SketchAxis")) return 4;
    if (!Run(Host, "plane (0,0,-0.02) 3 2 --name=ConstructionPlane --construction")) return 5;
    if (!Run(Host, "select Body01")) return 6;
    Host.Render();
    Frontier::RasterImage Preview = Host.Raster().Readback();
    gSolidArcRgba = Preview.Pixels.empty() ? nullptr : Preview.Pixels.data();
    gSolidArcW = Preview.Width;
    gSolidArcH = Preview.Height;

    std::vector<unsigned char> Pixels(static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight) * 3u);

    auto Tick = [&](float MouseX, float MouseY, bool Down)
    {
        IO.DeltaTime = 1.0f / 60.0f;
        IO.AddMousePosEvent(MouseX, MouseY);
        IO.AddMouseButtonEvent(0, Down);
        ImGui::NewFrame();
        Editor.Record(Host);
        ImGui::Render();
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
        if (stbi_write_png(Sheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::fprintf(stderr, "[SolidArcEditorProof] [FAIL] could not write %s\n", Sheet);
            return FailureCode;
        }
        return 0;
    };

    for (int I = 0; I < 12; ++I)
        Rest();
    Click(178.0f, 175.0f); // Filter dropdown in the SolidArc outliner search row.
    for (int I = 0; I < 3; ++I)
        Rest();
    Rasterise();
    const char* MenuSheet = "Exhibits/Gallery/Editor/EditorProof_SolidArc_Menu.png";
    if (const int Write = WriteSheet(MenuSheet, 7); Write != 0)
        return Write;

    Click(160.0f, 281.0f); // Bodies entry; selected filters appear as chips below the search/filter row.
    for (int I = 0; I < 4; ++I)
        Rest();
    Click(78.0f, 294.0f); // Body01 row after the Bodies filter; seats the CAD inspector like the game proof.
    for (int I = 0; I < 8; ++I)
        Rest();
    Rasterise();

    const char* Sheet = "Exhibits/Gallery/Editor/EditorProof_SolidArc.png";
    if (const int Write = WriteSheet(Sheet, 8); Write != 0)
        return Write;

    int Bright = 0;
    for (size_t I = 0; I < Pixels.size(); I += 3u)
        if (Pixels[I] > 45u || Pixels[I + 1u] > 45u || Pixels[I + 2u] > 45u)
            ++Bright;
    if (Bright < 5000)
    {
        std::fprintf(stderr, "[SolidArcEditorProof] [FAIL] proof image is nearly empty\n");
        return 9;
    }
    std::fprintf(stderr, "[SolidArcEditorProof] wrote %s and %s with %d bright pixels in the filtered sheet\n", MenuSheet, Sheet, Bright);
    ImGui::DestroyContext();
    return 0;
}
