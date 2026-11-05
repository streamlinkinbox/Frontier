/*==========================================================================================================================================
                                                         XRAYSCANSEQUENCE.H
==========================================================================================================================================*/
// 🧩 The X-ray scan's clock: a front travels from the nose to the tail, holds, fades and repeats; everything the front has passed turns from
//    solid shell into ghost, wireframe and particles. A port of ScanSequence.js statement for statement.

#pragma once

#include "XRayScanSpecification.h"

#include <array>

namespace Frontier::XRayScan
{

class XRayScanSequence
{
public:
    // 📝 useEffect(() => { if (scan) sweepT.current = 0 }, [scan]): turning the scan on restarts the sweep; turning it off keeps the front where
    //    it was.
    void Assign(bool Enable);

    // 📝 useFrame: one refresh of RawSeconds. The ramp and the sweep step by at most StepLimit, the clock by the whole.
    void Advance(double RawSeconds);

    // 📝 The curtain's two plane opacities: (0.075, 0.30) × ramp × cycle × (sweeping ? 1 : 0.25).
    [[nodiscard]] std::array<double, 2> ResolveCurtain() const;

    [[nodiscard]] double ResolveEdgeOpacity() const;

    // 📝 Anything of the scan is drawn at all: the solid fade and the ghost need Amount, the curtain needs the ramp.
    [[nodiscard]] bool QueryVisible() const noexcept { return Amount_ > 0.0 || Ramp_ > 0.0; }

    [[nodiscard]] bool   QueryEnabled() const noexcept { return Enabled_; }
    [[nodiscard]] double QueryRamp() const noexcept { return Ramp_; }
    [[nodiscard]] double QuerySweepSeconds() const noexcept { return SweepSeconds_; }
    [[nodiscard]] double QueryFront() const noexcept { return Front_; }             // [m]  uScanX
    [[nodiscard]] double QueryAmount() const noexcept { return Amount_; }           // [-]  uHoloAmt
    [[nodiscard]] double QueryCycleAmount() const noexcept { return CycleAmount_; }
    [[nodiscard]] bool   QuerySweeping() const noexcept { return Sweeping_; }
    [[nodiscard]] bool   QueryTransparent() const noexcept { return Transparent_; }
    [[nodiscard]] double QueryClock() const noexcept { return Clock_; }             // [s]

private:
    bool   Enabled_      = false;
    double Ramp_         = 0.0;
    double SweepSeconds_ = 0.0;   // [s]
    double Front_        = ScanSpecification.FrontIdle;
    double Amount_       = 0.0;
    double CycleAmount_  = 1.0;
    bool   Sweeping_     = false;
    bool   Transparent_  = false;
    double Clock_        = 0.0;   // [s]
};

} // namespace Frontier::XRayScan
