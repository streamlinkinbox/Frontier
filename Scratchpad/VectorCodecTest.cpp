//============================================================================================================================================
//                                                      VECTORCODECTEST.CPP
//============================================================================================================================================
// 🧩 The P4 gate. An SVG path becomes figures that draw the right shape, in the right place, the right way up.
//
//    The check that earns its keep is the Y flip. SVG's Y axis points down and the interface plane's points up, so
//    a converter that forgets is off by a mirror — and a mirrored icon still passes every count, every bound and
//    every "did it emit figures" test. So this converts an asymmetric path and asserts which end is up.
//
//    It also rasterises a real icon through the SAME distance function the fragment shader runs and writes
//    Diagnostics/SpatialInterface_P4_Vector.png, because "37 figures were emitted" is not "that is a checkmark".
//
//    Build: bash Scratchpad/CheckVectorCodec.sh

#include "GlslShim.h"
#include "/tmp/InterfaceSignedDistance.port.inc"

#include "SpatialInterface/InterfaceVectorCodec.h"
#include "SpatialInterface/InterfaceSequence.h"
#include "SpatialInterface/InterfaceStructure.h"

#include "PngWriteShim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int Failures = 0;

void CheckTrue(const char* Name, bool Condition)
{
    std::printf("  %-66s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

void CheckNear(const char* Name, float Value, float Target, float Tolerance)
{
    const bool Ok = std::fabs(Value - Target) <= Tolerance;
    std::printf("  %-66s %s  (%.4f vs %.4f)\n", Name, Ok ? "PASS" : "FAIL", Value, Target);
    if (!Ok) ++Failures;
}

// lucide 'check' — the canonical stroke icon, and asymmetric enough to catch a flip.
const char* kCheck = "M20 6 L9 17 L4 12";
// A closed square, for the wrap-around segment.
const char* kSquare = "M4 4 L20 4 L20 20 L4 20 Z";

} // namespace

int main()
{
    std::printf("\n=== P4 vector conversion gate ===\n\n");

    Frontier::VectorPlacement Placement;
    Placement.Extent      = 0.040f;
    Placement.StrokeWidth = 0.0030f;

    //──────────────────────────────────────────────────────────────────────────
    // ① It converts, and Measure agrees with Compose.
    //──────────────────────────────────────────────────────────────────────────
    {
        Frontier::InterfaceStructure Structure;
        Frontier::InterfaceFigure Housing;
        Housing.HalfWidth = 0.05f; Housing.HalfHeight = 0.05f;
        const uint32_t Panel = Structure.Construct(Housing);

        const Frontier::VectorConversionMetrics Predicted =
            Frontier::InterfaceVectorCodec::Measure(kCheck, Placement);
        const Frontier::VectorConversionMetrics Actual =
            Frontier::InterfaceVectorCodec::Compose(Structure, Panel, kCheck, Placement);

        std::printf("  'check': %u contours, %u points -> %u figures (%u dropped)\n",
                    Actual.ContourCount, Actual.PointCount, Actual.FigureCount, Actual.DroppedCount);

        CheckTrue("the path converts to figures",        Actual.FigureCount > 0u);
        CheckTrue("Measure agrees with Compose exactly", Predicted.FigureCount == Actual.FigureCount);
        CheckTrue("every figure reached the structure",  Structure.QueryCount() == Actual.FigureCount + 1u);
        // A two-segment path must not explode into hundreds of figures.
        CheckTrue("a simple path stays cheap",           Actual.FigureCount <= 8u);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ② The Y flip. SVG y grows DOWN; the interface plane's y grows UP.
    //──────────────────────────────────────────────────────────────────────────
    // 'check' starts high-right (20 6) and ends mid-left (4 12), passing through its lowest point at (9 17).
    // In SVG coordinates 17 is the LARGEST y, so in interface coordinates it must be the SMALLEST.
    {
        Frontier::InterfaceStructure Structure;
        (void)Frontier::InterfaceVectorCodec::Compose(Structure, Frontier::InterfaceStructure::Detached,
                                                      "M20 6 L9 17", Placement);
        CheckTrue("a single segment emits one figure", Structure.QueryCount() == 1u);

        // The segment runs from SVG (20,6) to (9,17): right-and-down. After the flip that is left-and-down in the
        //    interface plane, so the roll angle must be in the third quadrant (both components negative).
        const Frontier::InterfaceFigure& Segment = Structure.Query(0u);
        const float Angle = Segment.Placement.RotationZ;
        CheckTrue("the segment runs leftward",  std::cos(Angle) < 0.0f);
        CheckTrue("the segment runs downward (Y flipped correctly)", std::sin(Angle) < 0.0f);

        // Its midpoint: SVG (14.5, 11.5) in a 24 box → just right of centre, just above centre after the flip.
        const float Scale = Placement.Extent / 24.0f;
        CheckNear("the midpoint maps right of centre",
                  Segment.Placement.Origin.X, (14.5f - 12.0f) * Scale, 1.0e-5f);
        CheckNear("the midpoint maps above centre (not below)",
                  Segment.Placement.Origin.Y, -(11.5f - 12.0f) * Scale, 1.0e-5f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ③ Capsule geometry: length, thickness and the corner radius that rounds the ends.
    //──────────────────────────────────────────────────────────────────────────
    {
        Frontier::InterfaceStructure Structure;
        // A horizontal segment 12 viewBox units long, so the arithmetic is checkable by hand.
        (void)Frontier::InterfaceVectorCodec::Compose(Structure, Frontier::InterfaceStructure::Detached,
                                                      "M6 12 L18 12", Placement);
        const Frontier::InterfaceFigure& Bar = Structure.Query(0u);
        const float Scale  = Placement.Extent / 24.0f;
        const float Radius = Placement.StrokeWidth * 0.5f;

        CheckNear("half width is half the length plus a radius",
                  Bar.HalfWidth, 12.0f * Scale * 0.5f + Radius, 1.0e-6f);
        CheckNear("half height is the stroke radius", Bar.HalfHeight, Radius, 1.0e-6f);
        CheckNear("the corner radius rounds the ends", Bar.CornerRadius, Radius, 1.0e-6f);
        CheckNear("a horizontal segment has zero roll", Bar.Placement.RotationZ, 0.0f, 1.0e-5f);
        CheckTrue("it is a Surface figure (no new category)",
                  Bar.Category == Frontier::InterfaceCategory::Surface);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ④ A closed contour emits the wrap-around segment.
    //──────────────────────────────────────────────────────────────────────────
    {
        Frontier::InterfaceStructure Open, Closed;
        (void)Frontier::InterfaceVectorCodec::Compose(Open, Frontier::InterfaceStructure::Detached,
                                                      "M4 4 L20 4 L20 20 L4 20", Placement);
        (void)Frontier::InterfaceVectorCodec::Compose(Closed, Frontier::InterfaceStructure::Detached,
                                                      kSquare, Placement);
        std::printf("  open square %u figures, closed square %u figures\n",
                    Open.QueryCount(), Closed.QueryCount());
        CheckTrue("a closed path adds the wrap-around segment", Closed.QueryCount() == Open.QueryCount() + 1u);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑤ Degenerate input is refused quietly rather than crashing a build step.
    //──────────────────────────────────────────────────────────────────────────
    {
        Frontier::InterfaceStructure Structure;
        CheckTrue("an empty path emits nothing",
                  Frontier::InterfaceVectorCodec::Compose(Structure, Frontier::InterfaceStructure::Detached,
                                                          "", Placement).FigureCount == 0u);
        CheckTrue("rubbish emits nothing rather than crashing",
                  Frontier::InterfaceVectorCodec::Compose(Structure, Frontier::InterfaceStructure::Detached,
                                                          "not a path at all", Placement).FigureCount == 0u);
        CheckTrue("nothing was appended for either", Structure.QueryCount() == 0u);

        // Zero-length segments are dropped, not emitted as degenerate capsules.
        const Frontier::VectorConversionMetrics Repeated =
            Frontier::InterfaceVectorCodec::Measure("M10 10 L10 10 L10 10", Placement);
        CheckTrue("repeated points are dropped", Repeated.FigureCount == 0u && Repeated.DroppedCount > 0u);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑥ An icon still costs one draw.
    //──────────────────────────────────────────────────────────────────────────
    {
        Frontier::InterfaceStructure Structure;
        Frontier::InterfaceFigure Housing;
        Housing.HalfWidth = 0.06f; Housing.HalfHeight = 0.06f;
        const uint32_t Panel = Structure.Construct(Housing);
        (void)Frontier::InterfaceVectorCodec::Compose(Structure, Panel, kCheck, Placement);
        (void)Frontier::InterfaceVectorCodec::Compose(Structure, Panel, kSquare, Placement);

        Frontier::InterfaceSequence Composition;
        Frontier::InterfaceViewConfiguration View;
        View.EyeZ = 1.0f; View.ForwardY = 1.0f;
        Composition.AssignView(View);
        Composition.Advance(Structure, 0.0);

        std::printf("  two icons: %u figures -> %u instances, %u draw(s)\n",
                    Structure.QueryCount(), Composition.QueryInstanceCount(),
                    Composition.QueryMetrics().DrawCount);
        CheckTrue("converted icons still cost one draw", Composition.QueryMetrics().DrawCount == 1u);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑦ Rasterise it, so the shape can be looked at.
    //──────────────────────────────────────────────────────────────────────────
    {
        Frontier::InterfaceStructure Structure;
        Frontier::VectorPlacement Icon = Placement;
        Icon.Extent = 0.80f;                    // fill the preview
        Icon.StrokeWidth = 0.060f;
        (void)Frontier::InterfaceVectorCodec::Compose(Structure, Frontier::InterfaceStructure::Detached,
                                                      kCheck, Icon);

        const uint32_t Size = 256u;
        std::vector<unsigned char> Pixels(static_cast<size_t>(Size) * Size * 3u, 18u);
        uint32_t Covered = 0u;

        for (uint32_t Y = 0u; Y < Size; ++Y)
            for (uint32_t X = 0u; X < Size; ++X)
            {
                // Preview plane spans -0.5..+0.5 m, y up.
                const float Px =  ((static_cast<float>(X) + 0.5f) / Size - 0.5f);
                const float Py = -((static_cast<float>(Y) + 0.5f) / Size - 0.5f);

                float Best = 1.0e9f;
                for (uint32_t Ordinal = 0u; Ordinal < Structure.QueryCount(); ++Ordinal)
                {
                    const Frontier::InterfaceFigure& F = Structure.Query(Ordinal);
                    // Undo the figure's roll and translation to get into its own frame.
                    const float Cx = Px - F.Placement.Origin.X;
                    const float Cy = Py - F.Placement.Origin.Y;
                    const float C  = std::cos(-F.Placement.RotationZ);
                    const float S  = std::sin(-F.Placement.RotationZ);
                    const vec2 Local(Cx * C - Cy * S, Cx * S + Cy * C);
                    Best = std::min(Best, DistanceFigure(0u, Local,
                                                         vec2(F.HalfWidth, F.HalfHeight),
                                                         F.CornerRadius, 0.0f, 0.0f));
                }
                const float Coverage = std::clamp(0.5f - Best / (1.0f / Size), 0.0f, 1.0f);
                if (Coverage > 0.5f) ++Covered;
                const unsigned char Value = static_cast<unsigned char>(18.0f + 220.0f * Coverage);
                const size_t Offset = (static_cast<size_t>(Y) * Size + X) * 3u;
                Pixels[Offset + 0u] = Value;
                Pixels[Offset + 1u] = Value;
                Pixels[Offset + 2u] = Value;
            }

        const double Fraction = static_cast<double>(Covered) / static_cast<double>(Size * Size);
        std::printf("  rasterised 'check': %.2f%% of the preview covered\n", Fraction * 100.0);
        CheckTrue("the icon actually draws ink", Fraction > 0.01);
        CheckTrue("and does not flood the preview", Fraction < 0.35);

        CheckTrue("a preview is written for inspection",
                  PngWriteShim::WritePng("Diagnostics/SpatialInterface_P4_Vector.png",
                                         static_cast<int>(Size), static_cast<int>(Size), 3,
                                         Pixels.data(), static_cast<int>(Size) * 3) != 0);
    }

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
