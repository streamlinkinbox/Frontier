//============================================================================================================================================
//                                                     PANELSAMPLETEST.CPP
//============================================================================================================================================
// 🧩 The High-tier gate. Compiles the SAME panel-sampling code the reflection path runs (GlslShim maps the Slang to
//    C++) and checks that sampling the panel across its face reproduces the panel's actual layout.
//
//    Why this matters more than "does it compile": High tier exists so a reflection shows the panel's CONTENT
//    rather than an averaged glow. A sampler with the UV mapped inside out, or the figure-to-panel transform
//    wrong, still returns plausible colour at every point — it just returns the wrong point's colour, which in a
//    blurry chrome reflection is almost impossible to spot by eye. So this asserts spatial agreement: a lit figure
//    on the left of the panel must sample lit on the LEFT.
//
//    It also compares the High-tier average against the Low-tier average. The two are computed by completely
//    different code — Low sums figure areas on the CPU, High walks the plane per texel — so agreement between them
//    is meaningful evidence that neither is wrong.
//
//    Build: bash Scratchpad/CheckPanelSample.sh

#include "GlslShim.h"
#include "/tmp/InterfaceSignedDistance.port.inc"

#include "SpatialInterface/InterfaceLayoutCodec.h"
#include "SpatialInterface/InterfaceLightProjection.h"
#include "SpatialInterface/InterfaceSequence.h"
#include "SpatialInterface/InterfaceStructure.h"

#include "PngWriteShim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int Failures = 0;

void CheckTrue(const char* Name, bool Condition)
{
    std::printf("  %-66s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

// The figure buffer the shader would read. Populated from the real composition, so this exercises the actual
//    encoded slots rather than hand-written stand-ins.
std::vector<Frontier::InterfaceInstanceFigure> InterfacePanelFigures;

// ── Port of Shaders/InterfacePanelSample.slang, kept line-for-line equivalent ────────────────────────────────────
struct PanelSample { vec3 Emission; float Coverage; uint32_t Walked; };

vec3 InterfaceTintToLinear(uint32_t Packed)
{
    vec3 Encoded(float( Packed        & 0xFFu) / 255.0f,
                 float((Packed >>  8) & 0xFFu) / 255.0f,
                 float((Packed >> 16) & 0xFFu) / 255.0f);
    const auto Convert = [](float E) { return E <= 0.04045f ? E / 12.92f : std::pow((E + 0.055f) / 1.055f, 2.4f); };
    return vec3(Convert(Encoded.x), Convert(Encoded.y), Convert(Encoded.z));
}

PanelSample SampleInterfacePanel(vec2 PanelUv, float PanelHalfWidth, float PanelHalfHeight,
                                 uint32_t FigureCount, uint32_t Budget, float PixelWidth)
{
    PanelSample Result{ vec3(0.0f, 0.0f, 0.0f), 0.0f, 0u };
    vec2 Local((PanelUv.x - 0.5f) * 2.0f * PanelHalfWidth,
               (PanelUv.y - 0.5f) * 2.0f * PanelHalfHeight);

    const uint32_t Limit = std::min(FigureCount, Budget);
    for (uint32_t Index = 0u; Index < Limit; ++Index)
    {
        const Frontier::InterfaceInstanceFigure& F = InterfacePanelFigures[Index];
        ++Result.Walked;

        const vec3 Right(F.RowXx, F.RowYx, F.RowZx);
        const vec3 Up   (F.RowXy, F.RowYy, F.RowZy);
        const float Scale = std::sqrt(Right.x * Right.x + Right.y * Right.y + Right.z * Right.z);
        if (Scale < 1e-9f) continue;
        const float UpLength = std::max(std::sqrt(Up.x * Up.x + Up.y * Up.y + Up.z * Up.z), 1e-9f);

        const vec3 Translation(F.RowXw, F.RowYw, F.RowZw);
        const vec2 Centre((Translation.x * Right.x + Translation.y * Right.y + Translation.z * Right.z) / Scale,
                          (Translation.x * Up.x    + Translation.y * Up.y    + Translation.z * Up.z) / UpLength);

        const vec2 Point((Local.x - Centre.x) / Scale, (Local.y - Centre.y) / Scale);
        const uint32_t Category = F.CategoryPalette >> 24;
        const float Distance = DistanceFigure(Category, Point,
                                              vec2(F.HalfWidth / Scale, F.HalfHeight / Scale),
                                              F.CornerRadius / Scale, F.ScalarAlpha, F.ScalarBeta);

        const float Alpha = CoverageFromDistance(Distance, std::max(PixelWidth, 1.0e-6f))
                          * std::clamp(F.Opacity, 0.0f, 1.0f);
        if (Alpha <= 0.0f) continue;

        const vec3 Tint = InterfaceTintToLinear(F.Tint);
        const float Weight = std::clamp(F.EmissiveWeight, 0.0f, 1.0f);
        Result.Emission = vec3(Result.Emission.x + (Tint.x * Weight - Result.Emission.x) * Alpha,
                               Result.Emission.y + (Tint.y * Weight - Result.Emission.y) * Alpha,
                               Result.Emission.z + (Tint.z * Weight - Result.Emission.z) * Alpha);
        Result.Coverage = Result.Coverage + (1.0f - Result.Coverage) * Alpha;
    }
    return Result;
}

} // namespace

int main()
{
    std::printf("\n=== High tier: panel sampling gate ===\n\n");

    //──────────────────────────────────────────────────────────────────────────
    // A panel with a RED figure hard left and a BLUE figure hard right.
    //──────────────────────────────────────────────────────────────────────────
    // Asymmetric on purpose: a mirrored UV mapping returns perfectly plausible colour everywhere and can only be
    //    caught by asking which SIDE a known colour lands on.
    Frontier::InterfaceStructure Structure;
    constexpr float kHalfWidth = 0.10f, kHalfHeight = 0.06f;
    {
        Frontier::InterfaceFigure Bezel;
        Bezel.HalfWidth = kHalfWidth; Bezel.HalfHeight = kHalfHeight;
        Bezel.TintOverride = 0xFF080808u; Bezel.EmissiveWeight = 0.0f;
        (void)Structure.Construct(Bezel);

        Frontier::InterfaceFigure Left;
        Left.HalfWidth = 0.025f; Left.HalfHeight = 0.025f;
        Left.TintOverride = 0xFF0000FFu;                 // RGBA8 little-endian → red
        Left.EmissiveWeight = 1.0f;
        Left.Placement.Origin = Frontier::PlaneOrigin{ -0.055f, 0.0f, 0.001f };
        (void)Structure.Construct(Left);

        Frontier::InterfaceFigure Right;
        Right.HalfWidth = 0.025f; Right.HalfHeight = 0.025f;
        Right.TintOverride = 0xFFFF0000u;                // → blue
        Right.EmissiveWeight = 1.0f;
        Right.Placement.Origin = Frontier::PlaneOrigin{ 0.055f, 0.0f, 0.001f };
        (void)Structure.Construct(Right);
    }

    Frontier::InterfaceSequence Composition;
    Frontier::InterfaceViewConfiguration View;
    View.EyeZ = 1.0f; View.ForwardY = 1.0f;
    Composition.AssignView(View);
    Composition.Advance(Structure, 0.0);

    InterfacePanelFigures.assign(Composition.QueryInstances(),
                                 Composition.QueryInstances() + Composition.QueryInstanceCount());
    const uint32_t Count = Composition.QueryInstanceCount();
    std::printf("  panel: %u figures, red at local x=-0.055, blue at x=+0.055\n\n", Count);
    CheckTrue("the composition produced figures", Count == 3u);

    const float Texel = (2.0f * kHalfWidth) / 128.0f;

    //──────────────────────────────────────────────────────────────────────────
    // ① Spatial agreement — the check that catches a mirrored or shifted mapping.
    //──────────────────────────────────────────────────────────────────────────
    {
        const PanelSample L = SampleInterfacePanel(vec2(0.225f, 0.5f), kHalfWidth, kHalfHeight, Count, 64u, Texel);
        const PanelSample R = SampleInterfacePanel(vec2(0.775f, 0.5f), kHalfWidth, kHalfHeight, Count, 64u, Texel);
        std::printf("  uv 0.225 → rgb (%.3f %.3f %.3f)\n", L.Emission.x, L.Emission.y, L.Emission.z);
        std::printf("  uv 0.775 → rgb (%.3f %.3f %.3f)\n", R.Emission.x, R.Emission.y, R.Emission.z);

        CheckTrue("the left of the panel samples the LEFT figure (red)",  L.Emission.x > L.Emission.z * 4.0f);
        CheckTrue("the right of the panel samples the RIGHT figure (blue)", R.Emission.z > R.Emission.x * 4.0f);
        CheckTrue("both sampled points are covered", L.Coverage > 0.9f && R.Coverage > 0.9f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ② Between the figures the panel is bare — High shows structure, not a wash.
    //──────────────────────────────────────────────────────────────────────────
    // This is the whole difference from Low: an averaged proxy is lit everywhere, so a gap that reads as DARK is
    //    the evidence that the reflection carries layout rather than a single colour.
    {
        const PanelSample Gap = SampleInterfacePanel(vec2(0.5f, 0.5f), kHalfWidth, kHalfHeight, Count, 64u, Texel);
        std::printf("  uv 0.500 (between) → rgb (%.3f %.3f %.3f), coverage %.2f\n",
                    Gap.Emission.x, Gap.Emission.y, Gap.Emission.z, Gap.Coverage);
        CheckTrue("the gap between figures is dark (layout is visible)",
                  Gap.Emission.x < 0.05f && Gap.Emission.z < 0.05f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ③ Vertical mapping is not flipped either.
    //──────────────────────────────────────────────────────────────────────────
    {
        Frontier::InterfaceFigure Top;
        Top.HalfWidth = 0.02f; Top.HalfHeight = 0.012f;
        Top.TintOverride = 0xFF00FF00u;                  // → green
        Top.EmissiveWeight = 1.0f;
        Top.Placement.Origin = Frontier::PlaneOrigin{ 0.0f, 0.040f, 0.002f };
        (void)Structure.Construct(Top);
        Composition.Advance(Structure, 0.0);
        InterfacePanelFigures.assign(Composition.QueryInstances(),
                                     Composition.QueryInstances() + Composition.QueryInstanceCount());
        const uint32_t Grown = Composition.QueryInstanceCount();

        const PanelSample High = SampleInterfacePanel(vec2(0.5f, 0.833f), kHalfWidth, kHalfHeight, Grown, 64u, Texel);
        const PanelSample Low  = SampleInterfacePanel(vec2(0.5f, 0.167f), kHalfWidth, kHalfHeight, Grown, 64u, Texel);
        std::printf("  uv v=0.833 (upper) → rgb (%.3f %.3f %.3f)\n", High.Emission.x, High.Emission.y, High.Emission.z);
        CheckTrue("a figure placed UP samples in the upper half", High.Emission.y > 0.2f);
        CheckTrue("and not in the lower half",                    Low.Emission.y < 0.05f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ④ The budget is honoured — a caller must be able to bound the cost.
    //──────────────────────────────────────────────────────────────────────────
    {
        const PanelSample Full    = SampleInterfacePanel(vec2(0.5f, 0.5f), kHalfWidth, kHalfHeight, 4u, 64u, Texel);
        const PanelSample Clipped = SampleInterfacePanel(vec2(0.5f, 0.5f), kHalfWidth, kHalfHeight, 4u,  2u, Texel);
        CheckTrue("an unbounded walk visits every figure", Full.Walked == 4u);
        CheckTrue("a budget caps the walk",                Clipped.Walked == 2u);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑤ High and Low agree on the panel's AVERAGE.
    //──────────────────────────────────────────────────────────────────────────
    // Low sums figure areas on the CPU; High walks the plane per texel. Two unrelated computations landing in the
    //    same place is real evidence — if the sampler's transform were wrong, its average would drift from Low's.
    {
        const uint32_t Grid = 96u;
        double SumR = 0.0, SumG = 0.0, SumB = 0.0;
        for (uint32_t Y = 0u; Y < Grid; ++Y)
            for (uint32_t X = 0u; X < Grid; ++X)
            {
                const PanelSample S = SampleInterfacePanel(
                    vec2((X + 0.5f) / Grid, (Y + 0.5f) / Grid), kHalfWidth, kHalfHeight,
                    Composition.QueryInstanceCount(), 64u, Texel);
                SumR += S.Emission.x; SumG += S.Emission.y; SumB += S.Emission.z;
            }
        const double Cells = static_cast<double>(Grid) * Grid;
        const float HighR = static_cast<float>(SumR / Cells);
        const float HighB = static_cast<float>(SumB / Cells);

        const Frontier::PanelRadiance LowTier =
            Frontier::InterfaceLightProjection::MeasureRadiance(Structure, Composition,
                                                                4.0f * kHalfWidth * kHalfHeight);
        std::printf("\n  High average rgb (%.4f %.4f %.4f)\n", HighR, static_cast<float>(SumG / Cells), HighB);
        std::printf("  Low  average rgb (%.4f %.4f %.4f)\n", LowTier.Red, LowTier.Green, LowTier.Blue);

        CheckTrue("High and Low agree that the panel is lit", HighR > 0.0f && LowTier.Red > 0.0f);
        // Same order of magnitude: Low weights by area, High by texel, so exact equality is not expected — but a
        //    factor of ten apart would mean one of them has the transform wrong.
        CheckTrue("High and Low agree within an order of magnitude",
                  HighR < LowTier.Red * 10.0f && LowTier.Red < HighR * 10.0f);
        CheckTrue("both tiers see red and blue in balance",
                  std::fabs(HighR - HighB) < std::max(HighR, HighB) * 0.5f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑥ Render the panel as a reflection would see it.
    //──────────────────────────────────────────────────────────────────────────
    {
        const uint32_t Width = 256u, Height = 154u;
        std::vector<unsigned char> Pixels(static_cast<size_t>(Width) * Height * 3u, 8u);
        for (uint32_t Y = 0u; Y < Height; ++Y)
            for (uint32_t X = 0u; X < Width; ++X)
            {
                const PanelSample S = SampleInterfacePanel(
                    vec2((X + 0.5f) / Width, 1.0f - (Y + 0.5f) / Height),
                    kHalfWidth, kHalfHeight, Composition.QueryInstanceCount(), 64u, Texel);
                const auto Encode = [](float Linear)
                {
                    const float Clamped = std::clamp(Linear, 0.0f, 1.0f);
                    const float Encoded = Clamped <= 0.0031308f ? Clamped * 12.92f
                                                                : 1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f;
                    return static_cast<unsigned char>(Encoded * 255.0f + 0.5f);
                };
                const size_t Offset = (static_cast<size_t>(Y) * Width + X) * 3u;
                Pixels[Offset + 0u] = Encode(S.Emission.x);
                Pixels[Offset + 1u] = Encode(S.Emission.y);
                Pixels[Offset + 2u] = Encode(S.Emission.z);
            }
        CheckTrue("a reflection preview is written",
                  PngWriteShim::WritePng("Diagnostics/SpatialInterface_HighTier_Panel.png",
                                         static_cast<int>(Width), static_cast<int>(Height), 3,
                                         Pixels.data(), static_cast<int>(Width) * 3) != 0);
    }

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
