/*============================================================================================================================================
                                                      WORKSPACEZOOMVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Headless CPU-only regression test for the reported "zoom stuck at 800%" bug. Both Pattern's and
//    Stamp's 2D canvases expose +/- zoom buttons and a scroll-wheel zoom gesture; the buttons were raised
//    from an 800% to a 1000% ceiling, but the separate scroll-wheel handler in both panels kept its own,
//    never-updated std::clamp(..., .125f, 8.f) -- so a user who zoomed in with the mouse wheel (the most
//    natural way to zoom a 2D canvas) silently stalled at 800% even though the on-screen readout and the
//    +/- buttons both promised 1000% was reachable. This test drives the shared ApplyWorkspaceZoomStep/
//    ApplyWorkspaceWheelZoom functions both input paths now route through, so a future stale duplicate
//    ceiling in either path is caught here instead of needing another real-machine repro.

#include "UI/TextureStudio/WorkspaceZoom.h"
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
        // 📝 Repeatedly clicking "Zoom in" (the documented 1.25x-per-click button step) must actually
        // reach the documented 1000% ceiling, not stall early.
        float ButtonZoom = 1.f;
        for (int Click = 0; Click < 40; ++Click) ButtonZoom = ApplyWorkspaceZoomStep(ButtonZoom, 1.25f);
        std::cout << "40 \"Zoom in\" clicks -> " << (ButtonZoom * 100) << "%\n";
        Require(ButtonZoom >= 9.999f, "The +/- button zoom path did not reach the documented 1000% ceiling.");

        // 📝 The actual reported repro: scrolling the mouse wheel to zoom in must reach the same ceiling
        // the buttons do -- this is exactly the path that still had a stale, separate 8.f (800%) cap.
        float WheelZoom = 1.f;
        for (int Notch = 0; Notch < 60; ++Notch) WheelZoom = ApplyWorkspaceWheelZoom(WheelZoom, 1.f);
        std::cout << "60 scroll-wheel notches -> " << (WheelZoom * 100) << "%\n";
        Require(WheelZoom >= 9.999f, "The scroll-wheel zoom path stalled below the documented 1000% ceiling "
            "(this is the exact reported bug: it previously clamped at 8.f / 800%).");

        // 📝 Both input paths must agree on the exact same ceiling value, not merely both exceed some
        // lower bound -- otherwise a future edit could raise one path's cap higher than the other's.
        Require(ApplyWorkspaceZoomStep(1000.f, 1.25f) == ApplyWorkspaceWheelZoom(1000.f, 1.f),
            "The button and scroll-wheel zoom paths disagree on the maximum zoom ceiling.");
        Require(ApplyWorkspaceZoomStep(1000.f, 1.25f) == 10.f, "The shared zoom ceiling is no longer 10.0 (1000%).");

        // 📝 Zooming out must still floor at the documented 12.5% minimum on both paths.
        Require(ApplyWorkspaceZoomStep(0.0001f, 1.f / 1.25f) == WorkspaceMinimumZoom, "Button zoom-out floor regressed.");
        Require(ApplyWorkspaceWheelZoom(0.0001f, -1.f) == WorkspaceMinimumZoom, "Wheel zoom-out floor regressed.");
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "WorkspaceZoomValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    std::cout << "WorkspaceZoomValidation passed.\n";
    return 0;
}
