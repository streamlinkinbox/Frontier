/*==========================================================================================================================================
                                                              NOTCHVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify inherited notch geometry, Android-style swipe release, axis locking, focus loss, and scaled travel.

#include "Engine/Interface/NotchMotion.h"
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

void Settle(Frontier::NotchMotion &Motion)
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
        NotchMotion Motion;
        Motion.ConfigureExtent(1100, 1000, 1);
        const auto Contour = Motion.QueryContour();
        Require(Contour[0].X == 350 && Contour[1].X == 750 && Contour[2].X == 718 && Contour[3].X == 382,
                "Reference trapezoid does not have 8 percent lower-corner insets");
        Require(Contour[2].Y == 36 && !Motion.QueryOpen(), "Notch height or initial closed condition differs");
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
        Require(!Motion.QueryOpen() && Motion.QueryNotchExtent().MinimumX == 450, "Horizontal notch slide failed");

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
        Require(Motion.QueryHeight() == 600 && Motion.QueryNotchExtent().Width() == 500,
                "Resize or interface scale did not update notch travel");
        Motion.ConfigureOpen(false);
        Settle(Motion);
        Motion.ConfigureExtent(200, 300, 1.5f);
        Require(Motion.QueryNotchExtent().Width() == 200, "Narrow viewport allows notch overflow");
        std::cout << "PASS: reference trapezoid; tap; slow upward dismiss; upward/downward flicks; axis lock; "
                     "small-drag retention; focus loss; resize; 125 percent scale; narrow viewport\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
