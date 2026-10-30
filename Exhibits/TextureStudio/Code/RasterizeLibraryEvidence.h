/*==========================================================================================================================================
                                                       RASTERIZELIBRARYEVIDENCE.H
==========================================================================================================================================*/
// 🧩 Rasterize actual headless ImGui triangles and its font atlas for review; this is CPU evidence, not native window/GPU execution.

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <vector>
#include <span>

//------------------------------------------------------------------------------------------------------------------------
//                                                     CPU DRAW-COMMAND CAPTURE
//------------------------------------------------------------------------------------------------------------------------

struct LibraryCaptureTexture
{
    uint64_t Token;
    const unsigned char *Rgba;
    int Width, Height;
};

inline void EncodeLibraryDrawCapture(
    const std::filesystem::path &Destination,
    const ImDrawData           &Commands,
    const unsigned char        *Atlas,
    int                         AtlasWidth,
    int                         AtlasHeight,
    std::span<const LibraryCaptureTexture> Textures = {})
{
    const int Width = int(Commands.DisplaySize.x), Height = int(Commands.DisplaySize.y);
    std::vector<unsigned char> Pixels(size_t(Width)*Height*3, 12);
    const auto Edge = [](ImVec2 First, ImVec2 Second, ImVec2 Point)
    {
        return (Second.x-First.x)*(Point.y-First.y)-(Second.y-First.y)*(Point.x-First.x);
    };
    for (const ImDrawList *Drawing : Commands.CmdLists)
        for (const auto &Instruction : Drawing->CmdBuffer)
        {
            if (Instruction.UserCallback) continue;
            const auto Clip = Instruction.ClipRect;
            const unsigned char *Sample = Atlas;
            int SampleWidth = AtlasWidth, SampleHeight = AtlasHeight;
            for (const auto &Texture : Textures)
                if (Texture.Token == Instruction.GetTexID())
                {
                    Sample = Texture.Rgba;
                    SampleWidth = Texture.Width;
                    SampleHeight = Texture.Height;
                }
            for (unsigned Index = 0; Index < Instruction.ElemCount; Index += 3)
            {
                const auto &First = Drawing->VtxBuffer[Drawing->IdxBuffer[Instruction.IdxOffset+Index]+Instruction.VtxOffset];
                const auto &Second = Drawing->VtxBuffer[Drawing->IdxBuffer[Instruction.IdxOffset+Index+1]+Instruction.VtxOffset];
                const auto &Third = Drawing->VtxBuffer[Drawing->IdxBuffer[Instruction.IdxOffset+Index+2]+Instruction.VtxOffset];
                const float Area = Edge(First.pos, Second.pos, Third.pos);
                if (std::abs(Area) < 1e-8f) continue;
                const int MinimumX = int(std::max({0.f, Clip.x, std::floor(std::min({First.pos.x, Second.pos.x, Third.pos.x}))}));
                const int MinimumY = int(std::max({0.f, Clip.y, std::floor(std::min({First.pos.y, Second.pos.y, Third.pos.y}))}));
                const int MaximumX = int(std::min({float(Width), Clip.z, std::ceil(std::max({First.pos.x, Second.pos.x, Third.pos.x}))}));
                const int MaximumY = int(std::min({float(Height), Clip.w, std::ceil(std::max({First.pos.y, Second.pos.y, Third.pos.y}))}));
                for (int Row = MinimumY; Row < MaximumY; ++Row)
                    for (int Column = MinimumX; Column < MaximumX; ++Column)
                    {
                        const ImVec2 Point(Column+.5f, Row+.5f);
                        const float α = Edge(Second.pos, Third.pos, Point)/Area;
                        const float β = Edge(Third.pos, First.pos, Point)/Area;
                        const float γ = 1-α-β;
                        if (std::min({α,β,γ}) < 0) continue;
                        const float Horizontal = α*First.uv.x+β*Second.uv.x+γ*Third.uv.x;
                        const float Vertical = α*First.uv.y+β*Second.uv.y+γ*Third.uv.y;
                        const float TexelX = Horizontal * SampleWidth - .5f, TexelY = Vertical * SampleHeight - .5f;
                        const int ColumnFloor = int(std::floor(TexelX)), RowFloor = int(std::floor(TexelY));
                        const float FractionX = TexelX - ColumnFloor, FractionY = TexelY - RowFloor;
                        std::array<float, 4> Filtered{};
                        for (int Y = 0; Y < 2; ++Y)
                            for (int X = 0; X < 2; ++X)
                            {
                                const size_t Address = (size_t(std::clamp(RowFloor + Y, 0, SampleHeight - 1)) * SampleWidth +
                                    std::clamp(ColumnFloor + X, 0, SampleWidth - 1)) * 4;
                                const float Weight = (X ? FractionX : 1 - FractionX) * (Y ? FractionY : 1 - FractionY);
                                for (size_t Channel = 0; Channel < 4; ++Channel) Filtered[Channel] += Sample[Address + Channel] * Weight;
                            }
                        const float Opacity = (α*(First.col>>24)+β*(Second.col>>24)+γ*(Third.col>>24))/255.f*Filtered[3]/255.f;
                        for (unsigned Channel = 0; Channel < 3; ++Channel)
                        {
                            const unsigned Shift = Channel*8;
                            const float Spectrum = (α*((First.col>>Shift)&255)+β*((Second.col>>Shift)&255)+γ*((Third.col>>Shift)&255))*Filtered[Channel]/255.f;
                            auto &Pixel = Pixels[(size_t(Row)*Width+Column)*3+Channel];
                            Pixel = static_cast<unsigned char>(std::clamp(Spectrum*Opacity+Pixel*(1-Opacity), 0.f, 255.f));
                        }
                    }
            }
        }
    std::ofstream Output(Destination, std::ios::binary);
    Output << "P6\n" << Width << ' ' << Height << "\n255\n";
    Output.write(reinterpret_cast<const char *>(Pixels.data()), std::streamsize(Pixels.size()));
    if (!Output) throw std::runtime_error("Library draw-command capture failed.");
}
