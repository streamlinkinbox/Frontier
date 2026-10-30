/*============================================================================================================================================
                                           VECTORCOVERAGEZOOMRESOLUTIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Headless CPU-only regression test for the reported "pixelation at high zoom" bug. Drives the real,
//    pure, stateless VectorCoverageRasterization::SelectBakeExtent used by both Pattern and Stamp canvases
//    to pick the pooled bake-texture resolution for the live, zoom-aware canvas. Both workspace panels
//    (UI/TextureStudio/PatternWorkspacePanel.cpp and StampWorkspacePanel.cpp) compute the on-screen,
//    device-pixel size of the board quad and clamp it to a maximum of 4096 before asking for a bake at
//    that size -- and VectorCoverageRasterization::Initialize's own validation explicitly accepts images up
//    to 4096x4096 -- so 4096 is the documented, intended ceiling. SelectBakeExtent's pooled tier list
//    stopped at 2048 though: once the on-screen quad exceeds 2048 device pixels (an ordinary ~4x zoom on a
//    512-unit canvas; the reported repro was 800% zoom, i.e. ~4096 device pixels), the baked texture stays
//    frozen at 2048 while the displayed quad keeps growing -- a fixed-resolution raster stretched past its
//    own resolution is visible pixelation/blur, exactly the reported symptom, and the opposite of the
//    "true resolution independence at any zoom" requirement this rasterizer was supposed to deliver.

#include "Engine/Render/VectorCoverageRasterization.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool Condition, const std::string &Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
}

int main()
{
    using namespace Frontier;
    try
    {
        // 📝 A plain, simple document (no shapes/segments) zoomed to the two panels' own documented
        // maximum on-screen size (4096 device pixels -- an ~800% zoom on a 512-unit canvas at 1x DPI,
        // the reported repro). The bake extent must track the display up to that same ceiling; a
        // single-channel canvas bake has no content-complexity excuse to fall short of it here.
        const uint32_t Baked = VectorCoverageRasterization::SelectBakeExtent(0, 1, 4096);
        std::cout << "DisplayExtent=4096 (simulated 800% zoom) -> baked extent=" << Baked
            << " (magnification if under-resolved: " << (4096.0 / Baked) << "x)\n";
        Require(Baked >= 4096, "Bake resolution did not track the canvas's own documented 4096 zoom ceiling "
            "-- the displayed board is a stretched, under-resolved raster past this point (visible pixelation at high zoom).");

        // 📝 Sanity: resolution should still scale up monotonically with zoom all the way to the ceiling,
        // not just suddenly jump at the very top value.
        uint32_t Previous = 0;
        for (uint32_t DisplayExtent : {128u, 256u, 512u, 1024u, 2048u, 3000u, 4096u})
        {
            const uint32_t Extent = VectorCoverageRasterization::SelectBakeExtent(0, 1, DisplayExtent);
            Require(Extent >= Previous, "Bake extent regressed while DisplayExtent increased.");
            Require(Extent >= DisplayExtent, "Bake extent fell below the requested on-screen device-pixel size at DisplayExtent=" +
                std::to_string(DisplayExtent) + " (baked=" + std::to_string(Extent) + ") -- stretched/pixelated at this zoom.");
            Previous = Extent;
        }
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "VectorCoverageZoomResolutionValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    std::cout << "VectorCoverageZoomResolutionValidation passed.\n";
    return 0;
}
