/*==========================================================================================================================================
                                                       WORKSPACEICONEVIDENCE.H
==========================================================================================================================================*/
// 🧩 Capture the production SVG rasters through the same UI injection point as native Vulkan icon presentation.

#pragma once
#include "RasterizeLibraryEvidence.h"
#include "UI/TextureStudio/MaterialLibraryPanel.h"
#include <map>
#include <stdexcept>

struct WorkspaceIconEvidence
{
    Frontier::IconArt Artwork{"EngineContent/Icons"};
    std::map<Frontier::IconSymbol, std::shared_ptr<const Frontier::IconRaster>> Rasters;
    void Configure(Frontier::MaterialLibraryPanel &Panel)
    {
        Panel.PresentIcon = [this](Frontier::IconSymbol Symbol, Frontier::PlanePoint Point, float Size, Frontier::ColorQuad Tint)
        {
            if (!Frontier::IconArt::Available(Symbol, Frontier::IconDomain::Texture)) throw std::runtime_error("Cross-workspace icon leak.");
            auto &Raster = Rasters[Symbol];
            if (!Raster) Raster = Artwork.Rasterize(Symbol, 24, 24, 1, Frontier::IconPolicy::Strict, Frontier::IconDomain::Texture);
            if (Raster->Result != Frontier::IconResult::Ready) throw std::runtime_error(Raster->Diagnostic);
            const auto Colour = ImGui::ColorConvertFloat4ToU32(ImVec4(Tint.Red, Tint.Green, Tint.Blue, Tint.Alpha * ImGui::GetStyle().Alpha));
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(64 + uint64_t(Symbol)), ImVec2(Point.X, Point.Y),
                ImVec2(Point.X + Size, Point.Y + Size), ImVec2(0,0), ImVec2(1,1), Colour);
            return true;
        };
    }
    std::vector<LibraryCaptureTexture> QueryTextures() const
    {
        std::vector<LibraryCaptureTexture> Textures;
        for (const auto &[Symbol, Raster] : Rasters)
            Textures.push_back({64 + uint64_t(Symbol), Raster->Rgba.data(), int(Raster->Width), int(Raster->Height)});
        return Textures;
    }
};
