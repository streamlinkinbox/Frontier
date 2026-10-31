/*============================================================================================================================================
                                                  PAINTSTAMPENGINEVALIDATION.CPP
============================================================================================================================================*/
// 🧪 Deterministic proof that the C++ port of HarborPaintWorkshop.html's stamp logic (PaintStampEngine)
//    reproduces the physics behaviours the HTML was tuned for, on the same 0.72-unit equirect sphere the
//    HTML painted. Each check drives scripted strokes with a fixed RNG seed and asserts a behavioural
//    inequality (dry-out gradient, tooth speckle, pencil wear widening, marker speed starvation + multiply
//    buildup, spray standoff/flux falloff, splat ballistic sorting, sand planing, medium-aware erase,
//    dust slope selectivity, soot uphill plume). Exit code is non-zero if any check fails.
//    Optional argv[1]: path for a P6 PPM proof sheet painted with every instrument in bands.

#include "UI/TextureStudio/PaintStampEngine.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace Frontier;

namespace
{
int Failures = 0;
void Check(bool Condition, const char *Name, const std::string &Evidence)
{
    std::printf("[%s] %s -- %s\n", Condition ? "PASS" : "FAIL", Name, Evidence.c_str());
    if (!Condition) ++Failures;
}
std::string Num(const char *Label, double Value)
{
    char Buffer[96];
    std::snprintf(Buffer, sizeof(Buffer), "%s=%.3f", Label, Value);
    return Buffer;
}
constexpr float SphereR = 0.72f;

StampHit HitAt(float U, float V, float Pressure = 0.f)
{
    float Point[3];
    PaintStampEngine::SpherePointFromUv(U, V, SphereR, Point);
    return PaintStampEngine::MakeSphereHit(Point, SphereR, Pressure);
}

// Drive a straight stroke in UV space at a fixed cadence; Clock advances by Dt per stamp.
void Stroke(PaintStampEngine &Engine, StampTool Tool, float U0, float V0, float U1, float V1, int Stamps,
    double &Clock, double Dt = 0.016, float Pressure = 0.f)
{
    Engine.BeginStroke(Tool);
    for (int I = 0; I <= Stamps; ++I)
    {
        const float T = Stamps ? float(I) / Stamps : 0.f;
        Engine.Stamp(HitAt(U0 + (U1 - U0) * T, V0 + (V1 - V0) * T, Pressure), Clock);
        Clock += Dt;
    }
    Engine.EndStroke();
}

// Mean "distance from base coat" (0..1 rgb) over a UV disc -- how much visible paint sits there.
double MeanInkDistance(const PaintStampEngine &Engine, float Cu, float Cv, float Radius)
{
    const std::array<float, 3> Base{212.f / 255.f, 207.f / 255.f, 200.f / 255.f};
    double Sum = 0;
    int Count = 0;
    for (int J = -8; J <= 8; ++J)
        for (int I = -8; I <= 8; ++I)
        {
            const float U = Cu + I * Radius / 8.f, V = Cv + J * Radius / 8.f;
            if (std::hypot(U - Cu, V - Cv) > Radius) continue;
            const auto S = Engine.SampleCanvas(U, V);
            Sum += std::hypot(S[0] - Base[0], S[1] - Base[1], S[2] - Base[2]);
            ++Count;
        }
    return Count ? Sum / Count : 0;
}
double MeanCoverage(const PaintStampEngine &Engine, float Cu, float Cv, float Radius)
{
    double Sum = 0;
    int Count = 0;
    for (int J = -8; J <= 8; ++J)
        for (int I = -8; I <= 8; ++I)
        {
            const float U = Cu + I * Radius / 8.f, V = Cv + J * Radius / 8.f;
            if (std::hypot(U - Cu, V - Cv) > Radius) continue;
            Sum += Engine.SampleCanvas(U, V)[3];
            ++Count;
        }
    return Count ? Sum / Count : 0;
}
// Fraction of canvas pixels inside a UV rect whose coverage clears the bar (speckle vs slab test).
double CoveredFraction(const PaintStampEngine &Engine, float U0, float V0, float U1, float V1, float Bar)
{
    const auto &Coverage = Engine.CoveragePixels();
    const int W = Engine.CanvasWidth(), H = Engine.CanvasHeight();
    const int X0 = int(U0 * W), X1 = int(U1 * W), Y0 = int(V0 * H), Y1 = int(V1 * H);
    long Hit = 0, Total = 0;
    for (int Y = Y0; Y < Y1; ++Y)
        for (int X = X0; X < X1; ++X)
        {
            if (Coverage[size_t(Y) * W + X] / 255.f > Bar) ++Hit;
            ++Total;
        }
    return Total ? double(Hit) / Total : 0;
}
double Luminance(const std::array<float, 4> &S) { return 0.299 * S[0] + 0.587 * S[1] + 0.114 * S[2]; }

PaintStampEngine FreshEngine(uint32_t Seed)
{
    PaintStampEngine Engine;
    Engine.SetSeed(Seed);
    Engine.ConfigureNormalLookup([](float U, float V, float Out[3]) noexcept {
        PaintStampEngine::SpherePointFromUv(U, V, 1.f, Out); // unit sphere: position == normal
        return true;
    });
    return Engine;
}
} // namespace

int main(int Argc, char **Argv)
{
    std::printf("PaintStampEngine validation (HarborPaintWorkshop stamp-logic port)\n");

    // ---- 1. Paint brush: deposit + impasto + distance-based ink dry-out gradient ----------------------
    {
        PaintStampEngine Engine = FreshEngine(101);
        auto &P = Engine.Parameters();
        P.Medium = PaintMedium::Oil; P.Shape = BrushShape::Round; P.Radius = 0.026f;
        P.Colour = {196.f, 92.f, 42.f}; P.Load = 0.85f; P.Thick = 0.8f; P.Press = 1.2f;
        Engine.RebuildTuft();
        double Clock = 0;
        Stroke(Engine, StampTool::PaintBrush, 0.18f, 0.5f, 0.82f, 0.5f, 240, Clock);
        const double Start = MeanInkDistance(Engine, 0.24f, 0.5f, 0.012f);
        const double End = MeanInkDistance(Engine, 0.76f, 0.5f, 0.012f);
        const double Film = Engine.SampleHeight(0.3f, 0.5f);
        Check(Start > 0.10, "paint deposit visible", Num("startDist", Start));
        Check(Film > 0.04, "paint impasto film raised", Num("film", Film));
        Check(End < Start * 0.92, "ink dry-out gradient along stroke", Num("start", Start) + " " + Num("end", End));
    }

    // ---- 2. Dry media: tooth speckle (not a slab), and second pass builds the valleys up ----------------
    {
        PaintStampEngine Engine = FreshEngine(202);
        auto &P = Engine.Parameters();
        P.Dry = DryKind::Chalk; P.DryColour = {70.f, 90.f, 200.f}; P.DrySize = 0.03f; P.DryPress = 0.9f; P.DryTooth = 0.8f;
        double Clock = 0;
        Stroke(Engine, StampTool::Dry, 0.3f, 0.5f, 0.7f, 0.5f, 120, Clock);
        const double First = CoveredFraction(Engine, 0.36f, 0.485f, 0.64f, 0.515f, 0.25f);
        Check(First > 0.08 && First < 0.97, "dry tooth catches ridges, skips valleys (speckle)", Num("covered", First));
        Stroke(Engine, StampTool::Dry, 0.3f, 0.5f, 0.7f, 0.5f, 120, Clock);
        Stroke(Engine, StampTool::Dry, 0.3f, 0.5f, 0.7f, 0.5f, 120, Clock);
        const double Third = CoveredFraction(Engine, 0.36f, 0.485f, 0.64f, 0.515f, 0.25f);
        Check(Third > First + 0.03, "dry pigment accumulates with passes", Num("pass1", First) + " " + Num("pass3", Third));
        Check(Engine.DryAt(0.5f, 0.5f) > 0.05, "loose-pigment field records the deposit", Num("dryField", Engine.DryAt(0.5f, 0.5f)));
    }

    // ---- 3. Pencil: point wears -> line widens; idle re-points ------------------------------------------
    {
        PaintStampEngine Engine = FreshEngine(303);
        auto &P = Engine.Parameters();
        P.PencilGrade = 0.5f; P.PencilSize = 0.018f; P.PencilPress = 1.0f; P.PencilSide = 0.08f;
        double Clock = 10; // idle gap beforehand -> fresh point
        Stroke(Engine, StampTool::Pencil, 0.3f, 0.35f, 0.5f, 0.35f, 80, Clock);
        const double Fresh = CoveredFraction(Engine, 0.32f, 0.325f, 0.48f, 0.375f, 0.10f);
        for (int K = 0; K < 4; ++K) // scribble to wear the point, no idle in between
            Stroke(Engine, StampTool::Pencil, 0.55f, 0.55f + K * 0.03f, 0.85f, 0.55f + K * 0.03f, 110, Clock);
        Stroke(Engine, StampTool::Pencil, 0.3f, 0.45f, 0.5f, 0.45f, 80, Clock);
        const double Worn = CoveredFraction(Engine, 0.32f, 0.425f, 0.48f, 0.475f, 0.10f);
        Check(Worn > Fresh * 1.25, "pencil wear widens the line", Num("freshArea", Fresh) + " " + Num("wornArea", Worn));
    }

    // ---- 4. Marker: capillary feed starves at speed; overlap multiplies darker --------------------------
    {
        PaintStampEngine Engine = FreshEngine(404);
        auto &P = Engine.Parameters();
        P.MarkerColour = {30.f, 80.f, 220.f}; P.MarkerSize = 0.02f; P.MarkerPress = 0.9f; P.MarkerLoad = 0.85f;
        double Clock = 0;
        Stroke(Engine, StampTool::Marker, 0.3f, 0.4f, 0.7f, 0.4f, 200, Clock, 0.016); // slow: fed
        const double Slow = Luminance(Engine.SampleCanvas(0.5f, 0.4f));
        Stroke(Engine, StampTool::Marker, 0.3f, 0.6f, 0.7f, 0.6f, 14, Clock, 0.004); // fast: starved
        const double Fast = Luminance(Engine.SampleCanvas(0.5f, 0.6f));
        Check(Slow < Fast - 0.02, "marker feed starves at speed (fast line paler)", Num("slowLum", Slow) + " " + Num("fastLum", Fast));
        Stroke(Engine, StampTool::Marker, 0.3f, 0.4f, 0.7f, 0.4f, 200, Clock, 0.016);
        const double Twice = Luminance(Engine.SampleCanvas(0.5f, 0.4f));
        Check(Twice < Slow - 0.005, "marker dye multiplies on overlap", Num("once", Slow) + " " + Num("twice", Twice));
    }

    // ---- 5. Spray: standoff falloff + time-based flux ---------------------------------------------------
    {
        PaintStampEngine Engine = FreshEngine(505);
        auto &P = Engine.Parameters();
        P.SprayColour = {30.f, 30.f, 30.f}; P.SprayPressure = 0.7f; P.Cone = 0.6f;
        double Clock = 0;
        P.Standoff = 0.12f;
        Stroke(Engine, StampTool::Spray, 0.35f, 0.5f, 0.35f, 0.5f, 40, Clock); // dwell close
        Clock += 2.0; // idle: can repressurises
        P.Standoff = 0.9f;
        Stroke(Engine, StampTool::Spray, 0.65f, 0.5f, 0.65f, 0.5f, 40, Clock); // dwell far
        const double Close = MeanInkDistance(Engine, 0.35f, 0.5f, 0.02f);
        const double Far = MeanInkDistance(Engine, 0.65f, 0.5f, 0.02f);
        Check(Close > Far * 1.25, "spray deposit falls off with standoff", Num("close", Close) + " " + Num("far", Far));
        Clock += 2.0;
        P.Standoff = 0.4f;
        Stroke(Engine, StampTool::Spray, 0.5f, 0.25f, 0.5f, 0.25f, 40, Clock, 0.016); // 0.64 s of valve time
        Clock += 2.0;
        Stroke(Engine, StampTool::Spray, 0.5f, 0.75f, 0.5f, 0.75f, 40, Clock, 0.004); // 0.16 s
        const double Long = MeanInkDistance(Engine, 0.5f, 0.25f, 0.03f);
        const double Short = MeanInkDistance(Engine, 0.5f, 0.75f, 0.03f);
        Check(Long > Short * 1.2, "spray flux is per-second, not per-event", Num("long", Long) + " " + Num("short", Short));
    }

    // ---- 6. Splat: ballistic size sorting (heavy droplets fly farther) ----------------------------------
    {
        PaintStampEngine Engine = FreshEngine(606);
        auto &P = Engine.Parameters();
        P.Liquid = SplatLiquid::Paint; P.SplatVolume = 0.8f; P.SplatForce = 1.1f; P.SplatSatellites = 1.f; P.SplatBody = 0.3f;
        double Clock = 0;
        Stroke(Engine, StampTool::Splat, 0.4f, 0.5f, 0.55f, 0.52f, 20, Clock, 0.008); // flick, bursts on EndStroke
        const auto Stats = Engine.LastSplatStats();
        Check(Stats.Droplets > 15, "splat burst threw satellites", Num("droplets", Stats.Droplets));
        Check(Stats.MeanFlightSmall > 0 && Stats.MeanFlightLarge > Stats.MeanFlightSmall,
            "ballistic sorting: large droplets out-fly small",
            Num("small", Stats.MeanFlightSmall) + " " + Num("large", Stats.MeanFlightLarge));
        Check(MeanCoverage(Engine, 0.42f, 0.5f, 0.02f) > 0.15, "splat core landed", Num("coreCov", MeanCoverage(Engine, 0.42f, 0.5f, 0.02f)));
    }

    // ---- 7. Sand: planes impasto down --------------------------------------------------------------------
    {
        PaintStampEngine Engine = FreshEngine(707);
        auto &P = Engine.Parameters();
        P.Medium = PaintMedium::Oil; P.Colour = {150.f, 60.f, 40.f}; P.Thick = 1.f; P.Press = 1.2f; P.Radius = 0.03f;
        Engine.RebuildTuft();
        double Clock = 0;
        for (int K = 0; K < 3; ++K) Stroke(Engine, StampTool::PaintBrush, 0.35f, 0.5f, 0.65f, 0.5f, 120, Clock);
        const double Before = Engine.SampleHeight(0.5f, 0.5f);
        P.SandGrit = 100.f; P.SandSize = 0.045f; P.SandPress = 1.0f;
        for (int K = 0; K < 3; ++K) Stroke(Engine, StampTool::Sand, 0.35f, 0.5f, 0.65f, 0.5f, 120, Clock);
        const double After = Engine.SampleHeight(0.5f, 0.5f);
        Check(Before > 0.05, "sand test built impasto first", Num("filmBefore", Before));
        Check(After < Before * 0.9, "sand planes the film down", Num("before", Before) + " " + Num("after", After));
    }

    // ---- 8. Eraser: lifts loose dry pigment, cured oil film resists ---------------------------------------
    {
        PaintStampEngine Engine = FreshEngine(808);
        auto &P = Engine.Parameters();
        P.Dry = DryKind::Charcoal; P.DrySize = 0.03f; P.DryPress = 1.1f;
        double Clock = 0;
        Stroke(Engine, StampTool::Dry, 0.3f, 0.35f, 0.7f, 0.35f, 140, Clock);
        P.Medium = PaintMedium::Oil; P.Colour = {40.f, 40.f, 160.f}; P.Thick = 0.9f; P.Press = 1.2f; P.Radius = 0.03f;
        Engine.RebuildTuft();
        Stroke(Engine, StampTool::PaintBrush, 0.3f, 0.65f, 0.7f, 0.65f, 140, Clock);
        const double DryBefore = MeanInkDistance(Engine, 0.5f, 0.35f, 0.015f);
        const double OilBefore = MeanInkDistance(Engine, 0.5f, 0.65f, 0.015f);
        for (int K = 0; K < 900; ++K) Engine.StepFields(0.25); // ~225 s: oil cures (tau 110 s)
        Check(Engine.WetAt(0.5f, 0.65f) < 0.25, "oil film cured before erase test", Num("wet", Engine.WetAt(0.5f, 0.65f)));
        P.EraserSize = 0.03f; P.EraserStrength = 0.9f;
        Clock += 300;
        Stroke(Engine, StampTool::Eraser, 0.3f, 0.35f, 0.7f, 0.35f, 140, Clock);
        Stroke(Engine, StampTool::Eraser, 0.3f, 0.65f, 0.7f, 0.65f, 140, Clock);
        const double DryLift = 1.0 - MeanInkDistance(Engine, 0.5f, 0.35f, 0.015f) / std::max(1e-6, DryBefore);
        const double OilLift = 1.0 - MeanInkDistance(Engine, 0.5f, 0.65f, 0.015f) / std::max(1e-6, OilBefore);
        Check(DryLift > 0.25, "eraser lifts loose dry pigment", Num("dryLift", DryLift));
        Check(DryLift > OilLift + 0.1, "cured film resists the eraser", Num("dryLift", DryLift) + " " + Num("oilLift", OilLift));
    }

    // ---- 9. Dust: settles on up-facing tack, slides off down-facing ----------------------------------------
    {
        PaintStampEngine Engine = FreshEngine(909);
        auto &P = Engine.Parameters();
        P.Particle = DustKind::Dust; P.DustColour = {120.f, 90.f, 40.f}; P.DustSize = 0.05f; P.DustRate = 1.f; P.DustTack = 0.9f;
        double Clock = 0;
        Stroke(Engine, StampTool::Dust, 0.5f, 0.20f, 0.5f, 0.20f, 24, Clock); // near top: up-facing
        Stroke(Engine, StampTool::Dust, 0.5f, 0.80f, 0.5f, 0.80f, 24, Clock); // near bottom: down-facing
        const double Top = MeanInkDistance(Engine, 0.5f, 0.20f, 0.03f);
        const double Bottom = MeanInkDistance(Engine, 0.5f, 0.80f, 0.03f);
        Check(Top > Bottom * 1.4, "dust sticks up-facing, sheds down-facing", Num("top", Top) + " " + Num("bottom", Bottom));
    }

    // ---- 10. Soot: plume climbs uphill ---------------------------------------------------------------------
    {
        PaintStampEngine Engine = FreshEngine(1010);
        auto &P = Engine.Parameters();
        P.SootHeat = 0.8f; P.SootDensity = 0.8f; P.SootSpread = 0.5f;
        double Clock = 0;
        Stroke(Engine, StampTool::Soot, 0.5f, 0.58f, 0.5f, 0.58f, 30, Clock); // dwell below equator
        const double Above = MeanInkDistance(Engine, 0.5f, 0.48f, 0.03f);   // uphill (toward top)
        const double Below = MeanInkDistance(Engine, 0.5f, 0.68f, 0.03f);   // downhill
        Check(Above > Below * 1.3, "soot plume deposits uphill of the flame", Num("above", Above) + " " + Num("below", Below));
    }

    // ---- 11. Digital: exact basic dab ------------------------------------------------------------------------
    {
        PaintStampEngine Engine = FreshEngine(1111);
        auto &P = Engine.Parameters();
        P.DigitalColour = {10.f, 200.f, 60.f}; P.DigitalSize = 0.03f; P.DigitalOpacity = 1.f; P.DigitalHardness = 1.f;
        double Clock = 0;
        Stroke(Engine, StampTool::Digital, 0.4f, 0.5f, 0.6f, 0.5f, 90, Clock, 0.016, 1.f);
        const auto S = Engine.SampleCanvas(0.5f, 0.5f);
        Check(S[3] > 0.8 && std::fabs(S[1] - 200.f / 255.f) < 0.06, "digital dab lays the picked colour",
            Num("cov", S[3]) + " " + Num("g", S[1]));
    }

    // ---- Proof sheet (optional): every instrument in bands ---------------------------------------------------
    if (Argc > 1)
    {
        PaintStampEngine Engine = FreshEngine(4242);
        auto &P = Engine.Parameters();
        double Clock = 0;
        P.Medium = PaintMedium::Oil; P.Colour = {196.f, 92.f, 42.f}; P.Press = 1.2f; P.Radius = 0.026f; P.Thick = 0.8f;
        Engine.RebuildTuft();
        Stroke(Engine, StampTool::PaintBrush, 0.08f, 0.10f, 0.9f, 0.13f, 260, Clock);
        P.Colour = {40.f, 80.f, 180.f}; Engine.Dip(); // wet-into-wet cross stroke picks the orange up
        Stroke(Engine, StampTool::PaintBrush, 0.3f, 0.05f, 0.36f, 0.2f, 120, Clock);
        P.SprayColour = {25.f, 25.f, 25.f}; P.Standoff = 0.35f; P.SprayPressure = 0.7f;
        Stroke(Engine, StampTool::Spray, 0.1f, 0.26f, 0.9f, 0.26f, 160, Clock);
        P.Dry = DryKind::Charcoal; P.DryPress = 1.0f;
        Stroke(Engine, StampTool::Dry, 0.08f, 0.38f, 0.9f, 0.38f, 200, Clock);
        P.PencilPress = 1.0f;
        Stroke(Engine, StampTool::Pencil, 0.08f, 0.47f, 0.9f, 0.47f, 300, Clock);
        P.MarkerColour = {160.f, 30.f, 140.f};
        Stroke(Engine, StampTool::Marker, 0.08f, 0.57f, 0.9f, 0.57f, 220, Clock);
        Stroke(Engine, StampTool::Marker, 0.4f, 0.57f, 0.9f, 0.57f, 160, Clock); // overlap darkens
        P.Liquid = SplatLiquid::Blood; P.SplatVolume = 0.8f;
        Stroke(Engine, StampTool::Splat, 0.25f, 0.70f, 0.38f, 0.72f, 16, Clock, 0.008);
        P.Particle = DustKind::Rust; P.DustRate = 1.f;
        Stroke(Engine, StampTool::Dust, 0.62f, 0.72f, 0.62f, 0.72f, 26, Clock);
        P.SootDensity = 0.8f;
        Stroke(Engine, StampTool::Soot, 0.85f, 0.78f, 0.85f, 0.78f, 24, Clock);
        P.SandGrit = 120.f; P.SandPress = 1.f;
        Stroke(Engine, StampTool::Sand, 0.08f, 0.88f, 0.55f, 0.88f, 180, Clock);
        P.DigitalColour = {20.f, 160.f, 90.f}; P.DigitalOpacity = 0.9f;
        Stroke(Engine, StampTool::Digital, 0.6f, 0.88f, 0.9f, 0.88f, 120, Clock, 0.016, 0.9f);
        for (int K = 0; K < 90; ++K) Engine.StepFields(0.016); // let drips run
        std::FILE *Out = std::fopen(Argv[1], "wb");
        if (Out)
        {
            const int W = Engine.CanvasWidth(), H = Engine.CanvasHeight();
            std::fprintf(Out, "P6\n%d %d\n255\n", W, H);
            for (const auto &Pixel : Engine.ColourPixels()) std::fwrite(Pixel.data(), 1, 3, Out);
            std::fclose(Out);
            std::printf("proof sheet written: %s (%dx%d)\n", Argv[1], W, H);
        }
        else
        {
            std::printf("could not write proof sheet: %s\n", Argv[1]);
            ++Failures;
        }
    }

    std::printf(Failures ? "VALIDATION FAILED (%d)\n" : "VALIDATION PASSED\n", Failures);
    return Failures ? 1 : 0;
}
