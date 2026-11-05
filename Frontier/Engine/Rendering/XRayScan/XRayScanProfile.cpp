/*==========================================================================================================================================
                                                        XRAYSCANPROFILE.CPP
==========================================================================================================================================*/

#include "XRayScanProfile.h"

#include <cmath>

namespace Frontier::XRayScan
{

namespace
{

// 📝 Float32Array.prototype.set of four doubles at a float offset.
void StoreVector(std::array<float, ScanProfileBytes / 4>& Values, size_t Offset, double X, double Y, double Z, double W)
{
    Values[Offset]     = static_cast<float>(X);
    Values[Offset + 1] = static_cast<float>(Y);
    Values[Offset + 2] = static_cast<float>(Z);
    Values[Offset + 3] = static_cast<float>(W);
}

} // namespace

std::array<float, ScanProfileBytes / 4> EncodeScanProfile(const XRayScanSequence& Scan, const ScanStage& Stage, uint32_t SparkleCount, double HudLineTop)
{
    const ScanSpecificationValues& Specification = ScanSpecification;
    const std::array<double, 2> Curtain = Scan.ResolveCurtain();
    // 📝 The truck group bobs about its ground by 12 mm at 0.8 rad/s; its world-height lines ride with it.
    const double Bob = 0.012 * std::sin(0.8 * Scan.QueryClock());
    std::array<float, ScanProfileBytes / 4> Values{};
    const std::array<double, 3> Ghost  = ConvertHexToLinear(Specification.GhostColour);
    const std::array<double, 3> Accent = ConvertHexToLinear(Specification.Accent);
    const std::array<double, 3> Edge   = ResolveDisplayColour(Specification.EdgeColour);
    const std::array<double, 3> Plane  = ResolveDisplayColour(Specification.Accent);
    const std::array<double, 3> Sparkle = ResolveDisplayColour(Specification.ParticleColour);
    StoreVector(Values, 0, Ghost[0], Ghost[1], Ghost[2], 1.0);
    StoreVector(Values, 4, Accent[0], Accent[1], Accent[2], 1.0);
    StoreVector(Values, 8, Edge[0], Edge[1], Edge[2], Scan.ResolveEdgeOpacity());
    StoreVector(Values, 12, Plane[0], Plane[1], Plane[2], 1.0);
    StoreVector(Values, 16, Curtain[0], Curtain[1], 0.0, 0.0);
    StoreVector(Values, 20, Sparkle[0], Sparkle[1], Sparkle[2], Specification.ParticleOpacity);
    StoreVector(Values, 24, Stage.Origin[0], Stage.Origin[1], Stage.Origin[2], -0.62 + Bob);
    StoreVector(Values, 28, Stage.Forward[0], Stage.Forward[1], Stage.Forward[2], 0.0);
    StoreVector(Values, 32, Stage.Right[0], Stage.Right[1], Stage.Right[2], 0.0);
    StoreVector(Values, 36, Stage.Up[0], Stage.Up[1], Stage.Up[2], 0.0);
    StoreVector(Values, 40, Scan.QueryFront(), Scan.QueryClock(), 1.0, HudLineTop);
    StoreVector(Values, 44, Specification.ParticleSize, Specification.ParticleSpeed, SparkleCount, Scan.QueryEnabled() ? 1.0 : 0.0);
    return Values;
}

std::array<uint32_t, 4> EncodeOccupantMask(std::span<const uint32_t> Indices)
{
    std::array<uint32_t, 4> Words{};
    for (uint32_t Index : Indices)
    {
        if (Index < 128u)
        {
            Words[Index >> 5] |= 1u << (Index & 31u);
        }
    }
    return Words;
}

XRayScanView EncodeScanView(const XRayScanSequence& Scan, const ScanStage& Stage, std::span<const uint32_t> ScannedOccupants, double CameraNear)
{
    XRayScanView View;
    const double Offset = -(Stage.Origin[0] * Stage.Forward[0] + Stage.Origin[1] * Stage.Forward[1] + Stage.Origin[2] * Stage.Forward[2]) - Scan.QueryFront();
    View.ScanPlane        = {static_cast<float>(Stage.Forward[0]), static_cast<float>(Stage.Forward[1]), static_cast<float>(Stage.Forward[2]),
                             static_cast<float>(Offset)};
    View.ScanState        = {static_cast<float>(Scan.QueryAmount()), 0.0f, 0.0f, 0.0f};
    View.ScannedOccupants = EncodeOccupantMask(ScannedOccupants);
    View.SparedFamilies   = {HeadlightFamily, TaillightFamily, 0u, 0u};
    View.Camera           = {static_cast<float>(CameraNear), 0.0f, 0.0f, 0.0f};
    return View;
}

} // namespace Frontier::XRayScan
