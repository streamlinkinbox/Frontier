/*==========================================================================================================================================
                                                        ICONDOMAINVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Prove editor icon reuse, workspace catalogue isolation and actual SVG rasterization through the production renderer.

#include "Engine/Interface/IconArt.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        if (Count != 2) throw std::runtime_error("Expected JSON report destination.");
        uint32_t Checks = 0;
        const auto Confirm = [&](bool Condition, const char *Description)
        {
            if (!Condition) throw std::runtime_error(Description);
            ++Checks;
        };
        const auto Editor = IconArt::Catalogue(IconDomain::Editor);
        const auto Texture = IconArt::Catalogue(IconDomain::Texture);
        const auto Fluid = IconArt::Catalogue(IconDomain::Fluid);
        IconArt Artwork("EngineContent/Icons");
        for (auto Symbol : {IconSymbol::EditorSearch, IconSymbol::EditorAdd, IconSymbol::EditorLayers, IconSymbol::EditorSliders,
                            IconSymbol::EditorSave, IconSymbol::EditorReset, IconSymbol::EditorFavourite, IconSymbol::EditorChevronDown})
            for (auto Domain : {IconDomain::Editor, IconDomain::Texture, IconDomain::Fluid})
            {
                Confirm(IconArt::Available(Symbol, Domain), "General editor icon was not reusable across workspaces.");
                const auto Raster = Artwork.Rasterize(Symbol, 24, 24, 1, IconPolicy::Strict, Domain);
                Confirm(Raster->Result == IconResult::Ready && !Raster->Substitute, "Editor SVG did not decode through ThorVG.");
            }
        for (auto Symbol : {IconSymbol::TextureUv, IconSymbol::TexturePaint, IconSymbol::TextureDecal,
                            IconSymbol::TextureFlakes, IconSymbol::TextureClearcoat})
        {
            Confirm(IconArt::Available(Symbol, IconDomain::Texture) && !IconArt::Available(Symbol, IconDomain::Editor) &&
                    !IconArt::Available(Symbol, IconDomain::Fluid), "Texture icon leaked outside Texture.");
            Confirm(std::find(Fluid.begin(), Fluid.end(), Symbol) == Fluid.end() && std::find(Editor.begin(), Editor.end(), Symbol) == Editor.end(),
                "Texture symbol leaked into another catalogue.");
            Confirm(Artwork.Rasterize(Symbol, 24, 24)->Result == IconResult::InvalidRequest, "Default editor access bypassed workspace scope.");
            const auto Raster = Artwork.Rasterize(Symbol, 24, 24, 1, IconPolicy::Strict, IconDomain::Texture);
            Confirm(Raster->Result == IconResult::Ready && !Raster->Substitute, "Texture SVG is not real artwork.");
            size_t Visible = 0;
            for (size_t Pixel = 3; Pixel < Raster->Rgba.size(); Pixel += 4) Visible += Raster->Rgba[Pixel] > 0;
            Confirm(Visible > 12 && Visible < 576, "Texture SVG raster is empty or opaque placeholder artwork.");
            Confirm(Artwork.Rasterize(Symbol, 24, 24, 1, IconPolicy::Strict, IconDomain::Fluid)->Result == IconResult::InvalidRequest,
                "Raster cache reuse bypassed the workspace guard.");
        }
        for (auto Symbol : {IconSymbol::Smoke, IconSymbol::FluidWater, IconSymbol::Fluid, IconSymbol::FolderWater, IconSymbol::FluidGlass})
        {
            Confirm(IconArt::Available(Symbol, IconDomain::Fluid) && !IconArt::Available(Symbol, IconDomain::Editor) &&
                    !IconArt::Available(Symbol, IconDomain::Texture), "Smoke/water icon leaked outside Fluid.");
            Confirm(std::find(Texture.begin(), Texture.end(), Symbol) == Texture.end() && std::find(Editor.begin(), Editor.end(), Symbol) == Editor.end(),
                "Fluid symbol leaked into another catalogue.");
        }
        Confirm(Artwork.Rasterize(IconSymbol::FluidWater, 24, 24, 1, IconPolicy::Strict, IconDomain::Fluid)->Result == IconResult::Ready,
            "Fluid water SVG did not render.");
        Confirm(!IconArt::Available(IconSymbol::EditorSearch, IconDomain(255)) && IconArt::Catalogue(IconDomain(255)).empty(),
            "Invalid workspace admitted icons.");
        std::ofstream(Arguments[1]) << "{\"result\":\"passed\",\"checks\":" << Checks << ",\"editor_symbols\":" << Editor.size() <<
            ",\"texture_symbols\":" << Texture.size() << ",\"fluid_symbols\":" << Fluid.size() <<
            ",\"svg_rasterizations\":" << Artwork.Statistics().Rasterizations << ",\"windows_execution\":false}\n";
        std::cout << "PASS: " << Checks << " icon scope and production SVG rasterization checks\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
