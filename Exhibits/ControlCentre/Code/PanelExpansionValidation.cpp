/*==========================================================================================================================================
                                                              PANELEXPANSIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify inherited notch geometry, Android-style swipe release, axis locking, focus loss, and scaled travel.

#include "Engine/Interface/PanelExpansionTab.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition)
        throw std::runtime_error(Description);
}

void Settle(Frontier::PanelExpansionTab &Motion)
{
    for (int Ordinal = 0; Ordinal < 180; ++Ordinal)
        Motion.Integrate(1.0 / 60);
}
} // namespace

int main()
{
    using namespace Frontier;
    try
    {
        PanelExpansionTab Motion;
        Motion.ConfigureExtent(1100, 1000, 1);
        const auto Contour = Motion.QueryContour();
        Require(Contour[0].X == 450 && Contour[1].X == 650 && Contour[2].X == 634 && Contour[3].X == 466,
                "Reference trapezoid does not have 8 percent lower-corner insets");
        Require(Contour[2].Y == 18 && !Motion.QueryOpen(), "Notch height or initial closed condition differs");
        Motion.IntegratePointer(550, 15, true, true, false, 1.0 / 60, false);
        Motion.IntegratePointer(550, 15, false, false, true, 1.0 / 60, false);
        Settle(Motion);
        Require(Motion.QueryOpen() && Motion.QueryHeight() == 1000, "Notch tap failed to open full-height shade");

        Motion.IntegratePointer(550, 500, true, true, false, 1.0 / 60, true);
        Motion.IntegratePointer(550, 380, false, true, false, 1.0 / 60, false);
        Require(Motion.QueryHeight() == 880, "Card did not follow upward pointer carry");
        for (int Ordinal = 0; Ordinal < 90; ++Ordinal)
            Motion.IntegratePointer(550, 380, false, true, false, 1.0 / 60, false);
        Motion.IntegratePointer(550, 380, false, false, true, 1.0 / 60, false);
        Require(!Motion.QueryOpen(), "Slow upward displacement over 50 px failed to dismiss");
        Settle(Motion);
        Require(Motion.QueryHeight() == 0, "Dismissal failed to settle at the top edge");

        // Horizontal motion chooses its axis once and must not turn into a vertical carry on a diagonal.
        Motion.IntegratePointer(550, 15, true, true, false, 1.0 / 60, false);
        Motion.IntegratePointer(650, 16, false, true, false, 1.0 / 60, false);
        Motion.IntegratePointer(650, 200, false, true, false, 1.0 / 60, false);
        Require(Motion.QueryHeight() == 0, "Diagonal motion changed a locked horizontal axis");
        Motion.IntegratePointer(650, 200, false, false, true, 1.0 / 60, false);
        Settle(Motion);
        Require(!Motion.QueryOpen() && Motion.QueryTabExtent().MinimumX == 550, "Horizontal notch slide failed");

        Motion.IntegratePointer(650, 15, true, true, false, 1.0 / 60, false);
        Motion.IntegratePointer(650, 45, false, true, false, 1.0 / 60, false);
        Motion.IntegratePointer(650, 45, false, false, true, 1.0 / 60, false);
        Require(Motion.QueryOpen(), "Downward flick under 50 px did not use velocity threshold");
        Settle(Motion);
        Motion.IntegratePointer(550, 500, true, true, false, 1.0 / 60, true);
        Motion.IntegratePointer(550, 470, false, true, false, 1.0 / 60, false);
        Motion.IntegratePointer(550, 470, false, false, true, 1.0 / 60, false);
        Require(!Motion.QueryOpen(), "Upward flick under 50 px failed to dismiss");
        Settle(Motion);

        Motion.ConfigureOpen(true);
        Settle(Motion);
        Motion.IntegratePointer(550, 500, true, true, false, 1.0 / 60, true);
        Motion.IntegratePointer(550, 470, false, true, false, 1.0 / 60, false);
        for (int Ordinal = 0; Ordinal < 90; ++Ordinal)
            Motion.IntegratePointer(550, 470, false, true, false, 1.0 / 60, false);
        Motion.IntegratePointer(550, 470, false, false, true, 1.0 / 60, false);
        Require(Motion.QueryOpen(), "Small slow displacement incorrectly dismissed an open sheet");
        Settle(Motion);
        const auto Grip = Motion.QueryGripExtent();
        Motion.IntegratePointer(550, Grip.MinimumY + 2, true, true, false, 1.0 / 60, false);
        Motion.IntegratePointer(-std::numeric_limits<float>::max(), 0, false, false, true, 1.0 / 60, false);
        Require(!Motion.QueryDragging() && Motion.QueryOpen(), "Focus loss left pointer capture stuck");

        Motion.ConfigureExtent(800, 600, 1.25f);
        Settle(Motion);
        Require(Motion.QueryHeight() == 600 && Motion.QueryTabExtent().Width() == 250,
                "Resize or interface scale did not update notch travel");
        Motion.ConfigureOpen(false);
        Settle(Motion);
        Motion.ConfigureExtent(200, 300, 1.5f);
        Require(Motion.QueryTabExtent().Width() == 200, "Narrow viewport allows notch overflow");
        PanelExpansionTab Top;
        PanelExpansionTab Bottom(PanelExpansionDirectionCategory::Upward);
        Top.ConfigureExtent(1100, 1000, 1);
        Bottom.ConfigureExtent(1100, 1000, 1);
        Top.ConfigureTravelLimit(600);
        Bottom.ConfigureTravelLimit(600);
        const auto Compare = [&]()
        {
            Require(Top.QueryOpen() == Bottom.QueryOpen() && Top.QueryHeight() == Bottom.QueryHeight() &&
                        Top.QueryProgress() == Bottom.QueryProgress(),
                    "Reflected drawer changed the shared motion contract");
            const auto Upper = Top.QueryContour();
            const auto Lower = Bottom.QueryContour();
            for (size_t Index = 0; Index < Upper.size(); ++Index)
                Require(Upper[Index].X == Lower[3 - Index].X && Upper[Index].Y == 1000 - Lower[3 - Index].Y,
                        "Bottom contour did not reflect geometry and retain winding");
        };
        const auto Carry = [&](float X, float Y, bool Pressed, bool Down, bool Released, bool Card)
        {
            Top.IntegratePointer(X, Y, Pressed, Down, Released, 1.0 / 60, Card);
            Bottom.IntegratePointer(X, 1000 - Y, Pressed, Down, Released, 1.0 / 60, Card);
            Compare();
        };
        Compare();
        Require(Bottom.QueryTabExtent().MinimumY == 982 && Bottom.QueryTabExtent().MaximumY == 1000,
                "Collapsed bottom tab escaped viewport");
        Carry(550, 15, true, true, false, false);
        Carry(550, 15, false, false, true, false);
        Settle(Top);
        Settle(Bottom);
        Compare();
        Require(Bottom.QueryHeight() == 600 && Bottom.QueryProgress() == 1, "Bounded bottom expansion failed");
        Carry(550, 500, true, true, false, true);
        Carry(550, 470, false, true, false, false);
        Carry(550, 470, false, false, true, false);
        Settle(Top);
        Settle(Bottom);
        Compare();
        Require(!Bottom.QueryOpen(), "Bottom downward flick failed to dismiss");
        Carry(550, 15, true, true, false, false);
        Carry(650, 16, false, true, false, false);
        Carry(650, 200, false, true, false, false);
        Carry(650, 200, false, false, true, false);
        Settle(Top);
        Settle(Bottom);
        Compare();
        Require(!Bottom.QueryOpen() && Bottom.QueryTabExtent().MinimumX == 550,
                "Bottom axis lock or lateral carry changed");
        Carry(650, 15, true, true, false, false);
        Carry(650, 45, false, true, false, false);
        Carry(650, 45, false, false, true, false);
        Settle(Top);
        Settle(Bottom);
        Compare();
        const auto BottomGrip = Bottom.QueryGripExtent();
        Bottom.IntegratePointer(550, BottomGrip.MinimumY + 2, true, true, false, 1.0 / 60, false);
        Bottom.IntegratePointer(-1e10f, -1e10f, false, false, true, 1.0 / 60, false);
        Require(Bottom.QueryOpen() && !Bottom.QueryDragging(), "Bottom focus cancellation failed");
        Bottom.ConfigureExtent(375, 700, 1.25f);
        Bottom.ConfigureTravelLimit(550);
        Settle(Bottom);
        Require(Bottom.QueryHeight() == 550 && Bottom.QueryTabExtent().Width() == 250 &&
                    Bottom.QueryTabExtent().MinimumY == 127.5f,
                "Bottom bounded travel/resize/scale failed");
        std::cout << "PASS: reference trapezoid; tap; slow upward dismiss; upward/downward flicks; axis lock; "
                     "mirrored bottom direction and winding; bounded travel; small-drag retention; focus loss; resize; "
                     "125 percent scale; narrow viewport\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
