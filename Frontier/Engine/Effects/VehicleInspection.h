#pragma once
/*==========================================================================================================================================
                                                          VEHICLEINSPECTION.H
==========================================================================================================================================*/
// 🧩 Visual effects only: how a vehicle is *looked at*, never how it behaves. The inspection views mirror
//    the HTML HMI's — Solid, Ghost (faces at 45 %), Wireframe, and the X-ray Scan, a bright band that
//    sweeps the car's length while hidden construction shows through. Physics, session rules and the
//    packet contract never read this; only presentations do. That is why it lives under Effects/, not
//    with the unit, the cabin fittings, or the session.

#include <cstdint>

namespace Frontier
{

enum class InspectionView : uint8_t
{
    Solid = 0,      // as authored
    Ghost = 1,      // faces ghosted to 45 %, hidden edges stay visible (the editor's X-ray)
    Wireframe = 2,  // edges only
    Scan = 3        // X-ray with the sweep band travelling the length of the car
};

struct VehicleInspection
{
    InspectionView View = InspectionView::Solid;
    float ScanSeconds = 0.0f;      // [s] clock driving the sweep
    float ScanPeriodSeconds = 3.2f; // [s] one full sweep of the car's length

    void Cycle() noexcept
    {
        View = static_cast<InspectionView>((static_cast<uint8_t>(View) + 1) % 4);
        ScanSeconds = 0.0f;
    }
    void Advance(float StepSeconds) noexcept
    {
        if (View == InspectionView::Scan)
            ScanSeconds += StepSeconds;
    }

    // 📝 Face opacity the presentation should use (the editor's X-ray ghosts at 45 %).
    [[nodiscard]] float ResolveFaceOpacity() const noexcept
    {
        switch (View)
        {
            case InspectionView::Ghost: return 0.45f;
            case InspectionView::Scan: return 0.45f;
            case InspectionView::Wireframe: return 0.0f;
            default: return 1.0f;
        }
    }
    // 📝 Whether hidden edges show through the faces (true for every X-ray family view).
    [[nodiscard]] bool ShowsHiddenEdges() const noexcept { return View != InspectionView::Solid; }

    // 📝 The sweep band's centre, as a fraction of the car's length (0 at the nose, 1 at the tail).
    [[nodiscard]] float ResolveScanFraction() const noexcept
    {
        if (ScanPeriodSeconds <= 0.0f)
            return 0.0f;
        const float Phase = ScanSeconds / ScanPeriodSeconds;
        return Phase - static_cast<float>(static_cast<int>(Phase));
    }
};

} // namespace Frontier
