//============================================================================================================================================
//                                                      EDITORPROOF.CPP
//============================================================================================================================================
// 🧩 Headless visual proof — drives EditorHost through the engine's tick order, rasterises the last tick with a
//    dependency-free CPU rasteriser, and gates the trapezoid sheet. No Vulkan, no GLFW, no window.

#ifndef FRONTIER_DEVELOPMENT
#error "the proof must define FRONTIER_DEVELOPMENT, or the editor records nothing and every gate fails"
#endif
#include <imgui.h>

#include "EditorHost.h"
#include "PngWriteShim.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static_assert(sizeof(ImDrawIdx) == 2u, "the rasteriser below walks 16-bit indices");

namespace {

constexpr int kWidth  = 1280;
constexpr int kHeight = 720;

// Seated-tab tint in bytes: EditorHost::ApplyTheme seats (0.16, 0.17, 0.19).
constexpr unsigned char kSeated[3] = { 41u, 43u, 48u };
constexpr unsigned char kGround[3] = { 5u, 5u, 5u };   // Frontier ground #050505

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

void CompositeOver(unsigned char* Pixel, Rgba Over) noexcept
{
    const float Keep = 1.0f - Over.A;
    Pixel[0] = static_cast<unsigned char>(Over.R * 255.0f * Over.A + static_cast<float>(Pixel[0]) * Keep + 0.5f);
    Pixel[1] = static_cast<unsigned char>(Over.G * 255.0f * Over.A + static_cast<float>(Pixel[1]) * Keep + 0.5f);
    Pixel[2] = static_cast<unsigned char>(Over.B * 255.0f * Over.A + static_cast<float>(Pixel[2]) * Keep + 0.5f);
}

Rgba SampleAtlas(const unsigned char* Atlas, int AtlasWidth, int AtlasHeight, float U, float V) noexcept
{
    int X = static_cast<int>(U * static_cast<float>(AtlasWidth));
    int Y = static_cast<int>(V * static_cast<float>(AtlasHeight));
    if (X < 0) X = 0; if (X >= AtlasWidth) X = AtlasWidth - 1;
    if (Y < 0) Y = 0; if (Y >= AtlasHeight) Y = AtlasHeight - 1;
    const unsigned char* Texel = Atlas + (static_cast<size_t>(Y) * static_cast<size_t>(AtlasWidth) + static_cast<size_t>(X)) * 4u;
    return { static_cast<float>(Texel[0]) / 255.0f, static_cast<float>(Texel[1]) / 255.0f,
             static_cast<float>(Texel[2]) / 255.0f, static_cast<float>(Texel[3]) / 255.0f };
}

float EdgeWeight(float Ax, float Ay, float Bx, float By, float Px, float Py) noexcept
{
    return (Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax);
}

// Rasterises one draw list over the pixels. Textured the way every ImGui backend is: the glyph atlas modulated
//    by the corner colours, composited over what is already there.
void RasterizeList(const ImDrawList* List, const unsigned char* Atlas, int AtlasWidth, int AtlasHeight,
                   unsigned char* Pixels, ImVec2 Origin, ImVec2 PixelScale) noexcept
{
    const ImDrawVert* Corners = List->VtxBuffer.Data;
    const ImDrawIdx*  Order   = List->IdxBuffer.Data;
    for (int Command = 0; Command < List->CmdBuffer.Size; ++Command)
    {
        const ImDrawCmd* Cmd = &List->CmdBuffer[Command];
        int ScissorLeft   = static_cast<int>((Cmd->ClipRect.x - Origin.x) * PixelScale.x);
        int ScissorTop    = static_cast<int>((Cmd->ClipRect.y - Origin.y) * PixelScale.y);
        int ScissorRight  = static_cast<int>((Cmd->ClipRect.z - Origin.x) * PixelScale.x);
        int ScissorBottom = static_cast<int>((Cmd->ClipRect.w - Origin.y) * PixelScale.y);
        if (ScissorLeft < 0)
            ScissorLeft = 0;
        if (ScissorRight > kWidth)
            ScissorRight = kWidth;
        if (ScissorTop < 0)
            ScissorTop = 0;
        if (ScissorBottom > kHeight)
            ScissorBottom = kHeight;

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
                    const Rgba Glyph = SampleAtlas(Atlas, AtlasWidth, AtlasHeight, U, V);
                    const Rgba Tinted = { (W0 * TintedA.R + W1 * TintedB.R + W2 * TintedC.R) * Glyph.R,
                                          (W0 * TintedA.G + W1 * TintedB.G + W2 * TintedC.G) * Glyph.G,
                                          (W0 * TintedA.B + W1 * TintedB.B + W2 * TintedC.B) * Glyph.B,
                                          (W0 * TintedA.A + W1 * TintedB.A + W2 * TintedC.A) * Glyph.A };
                    CompositeOver(&Pixels[(static_cast<size_t>(Y) * kWidth + static_cast<size_t>(X)) * 3u], Tinted);
                }
            }
        }
    }
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
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // the swapchain seats this in the engine; here it is ours
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    unsigned char* Atlas = nullptr;
    int AtlasWidth = 0, AtlasHeight = 0;
    IO.Fonts->GetTexDataAsRGBA32(&Atlas, &AtlasWidth, &AtlasHeight);

    Frontier::EditorHost Editor;
    Editor.ApplyTheme();

    // The engine's tick order (RenderScheduler::Present), minus the Control Centre overlay, which needs Vulkan.
    //    Ten ticks: the built columns settle over the first two, and the gates read the last.
    for (int Tick = 0; Tick < 10; ++Tick)
    {
        IO.DeltaTime = 1.0f / 60.0f;
        IO.AddMousePosEvent(-1.0f, -1.0f);   // nowhere near the strips: no hover tint may pollute the gates
        ImGui::NewFrame();
        Editor.Record();
        ImGui::Render();
    }

    std::vector<unsigned char> Pixels(static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight) * 3u);
    for (size_t I = 0u; I < Pixels.size(); I += 3u)
    {
        Pixels[I] = kGround[0]; Pixels[I + 1u] = kGround[1]; Pixels[I + 2u] = kGround[2];
    }

    const ImDrawData* Drawings = ImGui::GetDrawData();
    for (int Index = 0; Index < Drawings->CmdListsCount; ++Index)
        RasterizeList(Drawings->CmdLists[Index], Atlas, AtlasWidth, AtlasHeight, Pixels.data(),
                      Drawings->DisplayPos, Drawings->FramebufferScale);

    const char* Sheet = "Diagnostics/EditorProof_Tabs.png";
    if (stbi_write_png(Sheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
    {
        std::fprintf(stderr, "[EditorProof] [FAIL] the sheet would not write\n");
        return 1;
    }

    bool Failed = false;
    const auto At = [&](int X, int Y) -> const unsigned char*
    {
        return &Pixels[(static_cast<size_t>(Y) * kWidth + static_cast<size_t>(X)) * 3u];
    };
    const auto IsGround = [&](const unsigned char* P) -> bool
    {
        return P[0] == kGround[0] && P[1] == kGround[1] && P[2] == kGround[2];
    };
    const auto IsSeated = [&](const unsigned char* P) -> bool
    {
        return std::abs(static_cast<int>(P[0]) - 41) <= 6
            && std::abs(static_cast<int>(P[1]) - 43) <= 6
            && std::abs(static_cast<int>(P[2]) - 48) <= 6;
    };

    // Gate 1 — three occupied columns: each third of the sheet must carry panel ink, not bare ground.
    {
        const int LoX[3] = { 10, 400, 1020 };
        const int HiX[3] = { 270, 900, 1270 };
        const char* Name[3] = { "outliner", "viewport", "inspector" };
        for (int Third = 0; Third < 3; ++Third)
        {
            int Ink = 0;
            for (int Y = 100; Y < 700; ++Y)
                for (int X = LoX[Third]; X < HiX[Third]; ++X)
                    if (!IsGround(At(X, Y)))
                        ++Ink;
            const int Cells = (HiX[Third] - LoX[Third]) * 600;
            std::fprintf(stderr, "[EditorProof] %s third: %d ink cells of %d\n", Name[Third], Ink, Cells);
            if (Ink * 100 < Cells * 3)
            {
                std::fprintf(stderr, "[EditorProof] [FAIL] the %s third is bare ground\n", Name[Third]);
                Failed = true;
            }
        }
    }

    // Gate 2 — the trapezoid: both upper corners of the outliner tab must sit inside the lower ones by
    //    the seated slant. The tab spans y 1 … 18; the scanlines sit 1 px off its extremes, and the slant is
    //    linear, so each side must measure between 7 and 17 px against the seated 14. Title glyphs interrupt
    //    the run mid-tab, so the rightward scan steps over glyph-bright cells instead of stopping at them.
    {
        const auto IsGlyph = [&](const unsigned char* P) -> bool
        {
            return P[0] >= 190u && P[1] >= 190u && P[2] >= 190u;
        };
        const auto LeftEdge = [&](int Y) -> int
        {
            for (int X = 0; X < 120; ++X)
                if (IsSeated(At(X, Y)))
                    return X;
            return -1;
        };
        const auto RightEdge = [&](int Y, int FromX) -> int
        {
            int Last = -1;
            for (int X = FromX; X < 400; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (IsSeated(P))
                    Last = X;
                else if (!IsGlyph(P))
                    break;
            }
            return Last;
        };
        const int LoLeft = LeftEdge(17), HiLeft = LeftEdge(2);
        std::fprintf(stderr, "[EditorProof] tab edges: lower-left %d, upper-left %d", LoLeft, HiLeft);
        bool Slanted = LoLeft >= 0 && HiLeft >= 0;
        int LeftInset = 0, RightInset = 0;
        if (Slanted)
        {
            const int LoRight = RightEdge(17, LoLeft), HiRight = RightEdge(2, HiLeft);
            std::fprintf(stderr, ", lower-right %d, upper-right %d\n", LoRight, HiRight);
            Slanted = LoRight > LoLeft && HiRight > HiLeft;
            LeftInset = HiLeft - LoLeft;
            RightInset = LoRight - HiRight;
        }
        else
        {
            std::fprintf(stderr, "\n");
        }
        std::fprintf(stderr, "[EditorProof] slant: left %d px, right %d px (sheet seats 14)\n", LeftInset, RightInset);
        if (!Slanted || LeftInset < 7 || LeftInset > 17 || RightInset < 7 || RightInset > 17)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the tab is not a trapezoid\n");
            Failed = true;
        }
    }

    // Gate 3 — titled strips: the tab band must carry glyph ink (the three titles).
    {
        int Glyphs = 0;
        for (int Y = 0; Y < 32; ++Y)
            for (int X = 0; X < kWidth; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] >= 190u && P[1] >= 190u && P[2] >= 190u)
                    ++Glyphs;
            }
        std::fprintf(stderr, "[EditorProof] %d glyph cells in the tab band\n", Glyphs);
        if (Glyphs < 60)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the strips carry no titles\n");
            Failed = true;
        }
    }

    if (Failed)
    {
        std::fprintf(stderr, "[EditorProof] [FAIL] wrote %s, but the sheet disagrees with its caption\n", Sheet);
        return 1;
    }
    std::fprintf(stderr, "[EditorProof] wrote %s: three columns, trapezoid tabs, titled strips\n", Sheet);
    return 0;
}
