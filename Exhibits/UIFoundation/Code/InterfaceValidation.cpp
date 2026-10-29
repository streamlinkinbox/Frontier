/*==========================================================================================================================================
                                                      INTERFACEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify SVG rasters, cache ownership, and actual ImGui commands without pretending to run a GPU.

#include "Engine/Interface/IconArt.h"
#include "Engine/Interface/IconPresentation.h"
#include "UI/Editor/FoundationPanel.h"
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    VALIDATION SUPPORT
//------------------------------------------------------------------------------------------------------------------------

void Require(bool Condition, const char* Description)
{
    if (!Condition) throw std::runtime_error(Description);
}

// CPU-only acknowledgement for texture protocol checks. This is not an upload or a GPU backend.
void AcknowledgeTextures()
{
    for (auto* Texture : ImGui::GetPlatformIO().Textures)
    {
        if (Texture->Status == ImTextureStatus_WantCreate || Texture->Status == ImTextureStatus_WantUpdates)
        {
            Texture->SetTexID(static_cast<ImTextureID>(1));
            Texture->SetStatus(ImTextureStatus_OK);
        }
        if (Texture->Status == ImTextureStatus_WantDestroy)
        {
            Texture->SetTexID(ImTextureID_Invalid);
            Texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      VERIFICATION
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 3, "Expected icon root and evidence directory");
        const std::filesystem::path Output(Arguments[2]);
        std::filesystem::create_directories(Output);
        IconArt Artwork(Arguments[1]);
        constexpr unsigned TileSize = 64;
        constexpr unsigned Columns = 12;
        const unsigned Count = static_cast<unsigned>(IconSymbol::Count);
        const unsigned Width = Columns * TileSize;
        const unsigned Height = ((Count + Columns - 1) / Columns) * TileSize;
        std::vector<unsigned char> Image(Width * Height * 3, 24);
        std::ofstream Inventory(Output / "Icons.csv");
        Inventory << "Symbol,Status,Width,Height,Substitute\n";
        for (unsigned SymbolIndex = 0; SymbolIndex < Count; ++SymbolIndex)
        {
            const auto Symbol = static_cast<IconSymbol>(SymbolIndex);
            // 📝 Scoped catalogues deny Texture/Fluid symbols to the default Editor domain by design
            // (see IconDomainValidation); exercise every symbol inside its owning workspace instead.
            auto Raster = Artwork.Rasterize(Symbol, 48, 48, 1.0f, IconPolicy::Strict, IconArt::Domain(Symbol));
            Inventory << IconArt::Name(Symbol) << ',' << IconArt::ResultName(Raster->Result) << ','
                      << Raster->Width << ',' << Raster->Height << ',' << Raster->Substitute << '\n';
            Require(Raster->Result == IconResult::Ready && !Raster->Substitute, "SVG artwork not ready");
            Require(Raster->Rgba.size() == 48 * 48 * 4, "Unexpected raster dimensions");
            auto Reused = Artwork.Rasterize(Symbol, 48, 48, 1.0f, IconPolicy::Strict, IconArt::Domain(Symbol));
            Require(Raster == Reused, "Identical raster request did not reuse cache");
            bool Visible = false;
            for (unsigned Vertical = 0; Vertical < 48; ++Vertical)
            {
                for (unsigned Horizontal = 0; Horizontal < 48; ++Horizontal)
                {
                    const unsigned Source = (Vertical * 48 + Horizontal) * 4;
                    const unsigned Alpha = Raster->Rgba[Source + 3];
                    Visible = Visible || Alpha != 0;
                    const unsigned PixelX = (SymbolIndex % Columns) * TileSize + 8 + Horizontal;
                    const unsigned PixelY = (SymbolIndex / Columns) * TileSize + 8 + Vertical;
                    for (unsigned Channel = 0; Channel < 3; ++Channel)
                    {
                        Image[(PixelY * Width + PixelX) * 3 + Channel] = static_cast<unsigned char>(
                            (Raster->Rgba[Source + Channel] * Alpha + 24 * (255 - Alpha)) / 255);
                    }
                }
            }
            Require(Visible, "SVG produced entirely transparent artwork");
        }
        std::ofstream Pixels(Output / "Icons.ppm", std::ios::binary);
        Pixels << "P6\n" << Width << ' ' << Height << "\n255\n";
        Pixels.write(reinterpret_cast<const char*>(Image.data()), static_cast<std::streamsize>(Image.size()));
        Pixels.close();
        Require(Artwork.Statistics().Reuses >= Count, "Reuse statistics differ");
        Require(Artwork.Rasterize(IconSymbol::Count, 24, 24)->Result == IconResult::InvalidRequest,
                "Invalid symbol was accepted");
        Require(Artwork.Rasterize(IconSymbol::Sun, -1, 24)->Result == IconResult::InvalidRequest,
                "Negative dimensions were accepted");
        Require(Artwork.Rasterize(IconSymbol::Sun, std::numeric_limits<float>::infinity(), 24)->Result
                == IconResult::InvalidRequest, "Infinite dimensions were accepted");
        auto Retained = Artwork.Rasterize(IconSymbol::Sun, 24, 24);
        Artwork.Clear();
        Require(Retained->Rgba.size() == 24 * 24 * 4, "Clear invalidated retained immutable raster");
        IconArt Missing(Output / "missing-icons");
        Require(Missing.Rasterize(IconSymbol::Sun, 24, 24)->Result == IconResult::Missing,
                "Missing artwork did not report missing");
        IconArt Limited(Arguments[1], 24 * 24 * 4);
        auto Evicted = Limited.Rasterize(IconSymbol::Sun, 24, 24);
        (void)Limited.Rasterize(IconSymbol::Moon, 24, 24);
        Require(Limited.Statistics().Evictions == 1 && !Evicted->Rgba.empty(), "Eviction ownership differs");

        ImGui::CreateContext();
        auto& IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DisplaySize = ImVec2(1000, 800);
        IO.DeltaTime = 1.0f / 60.0f;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        ImGui::GetStyle().TabSlant = 10.0f;
        ImGui::GetStyle().TabOverlap = 12.0f;
        IconPresentation::Attach(Arguments[1]);
        ActivationOwnership Panel(new FoundationPanel());
        Require(Panel->Initialize() && Panel->Activate(), "Panel activation failed");
        for (int IntegrationIndex = 0; IntegrationIndex < 4; ++IntegrationIndex)
        {
            IO.DisplayFramebufferScale = ImVec2(1.0f + IntegrationIndex, 1.0f + IntegrationIndex);
            ImGui::NewFrame();
            static_cast<FoundationPanel*>(Panel.get())->ConstructInterface();
            ImGui::Render();
            AcknowledgeTextures();
            Require(ImGui::GetDrawData()->TotalVtxCount > 0, "UI produced no ImGui geometry");
            Require(IconPresentation::Result(IconSymbol::Sun) == IconResult::Ready, "Atlas icon not ready");
            Require(ImGui::GetPlatformIO().Textures.Size <= 4, "Atlas retirement is unbounded");
        }
        Panel.reset();
        ImGui::DestroyContext();
        std::cout << "PASS: " << Count << " SVGs ready and visible; cache, invalid input, eviction, "
                  << "missing assets, ImGui commands, patched tabs, DPI atlas retirement\n"
                  << "CPU evidence only: no GPU upload, Windows execution, or scene renderer tested\n";
        return 0;
    }
    catch (const std::exception& Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        return 1;
    }
}
