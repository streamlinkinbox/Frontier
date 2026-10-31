/*==========================================================================================================================================
                                               INDIRECT ILLUMINATION VALIDATION (EXHIBIT)
                                     ------------------------------------------------------------------
                 Distance-field GI + surface-card radiosity against analytic oracles (Phase 5, port order 2).
==========================================================================================================================================*/

// Oracles, all analytic or algebraic:
//   - exact SDF values, sphere tracing vs closed-form ray/sphere and ray/box intersections;
//   - indirect irradiance: hemisphere-filling emitter (exact pi*L) and a finite sphere (pi*L*sin^2);
//   - baked record grid: exact at nodes, error strictly improving with resolution;
//   - card form factors: reciprocity is an identity of the kernel; closed-room sums approach 1;
//   - radiosity: texel solver vs an independent 6x6 wall-aggregated direct solve (Gaussian
//     elimination), emitted-vs-absorbed power audit, and distance-field occlusion zeroing factors;
//   - the indirect prelude resolves ahead of DirectRadiance in the declared illumination frame.
// Usage: IndirectIlluminationValidation [ppm-prefix]   (writes <prefix>distance-room.ppm)

#include "Engine/Foundation/DeterministicRandom.h"
#include "Engine/Rendering/BarrierDerivation.h"
#include "Engine/Rendering/DistanceField.h"
#include "Engine/Rendering/IlluminationUnits.h"
#include "Engine/Rendering/SurfaceCards.h"
#include "Engine/Rendering/TransientTargetAtlas.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

int Failures = 0;
int Total    = 0;
constexpr double Pi = 3.14159265358979323846;

void Check(bool Condition, const char* Label)
{
    ++Total;
    if (!Condition)
    {
        ++Failures;
        std::printf("FAIL  %s\n", Label);
    }
}

void Print(const char* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    std::vprintf(Format, Args);
    va_end(Args);
}

bool Near(double A, double B, double Tol) { return std::abs(A - B) <= Tol; }

DistancePrimitive Sphere(SpatialVector C, double R, double Emissive = 0.0)
{
    return {DistancePrimitiveKind::Sphere, C, {R, R, R}, Emissive};
}
DistancePrimitive Box(SpatialVector C, SpatialVector Half, double Emissive = 0.0)
{
    return {DistancePrimitiveKind::Box, C, Half, Emissive};
}
DistancePrimitive HalfSpace(SpatialVector PointOnPlane, SpatialVector Normal, double Emissive = 0.0)
{
    return {DistancePrimitiveKind::HalfSpace, PointOnPlane, Normal, Emissive};
}

std::vector<SurfaceCard> CubeRoom(uint32_t Texels)
{
    const auto Card = [&](SpatialVector O, SpatialVector U, SpatialVector V, double Albedo, double Emit) {
        return SurfaceCard{O, U, V, Texels, Texels, Albedo, Emit};
    };
    return {
        Card({0, 0, 0}, {0, 0, 4}, {4, 0, 0}, 0.60, 0.0),   // floor   (+y)
        Card({0, 4, 0}, {4, 0, 0}, {0, 0, 4}, 0.30, 3.0),   // ceiling (-y, emissive)
        Card({0, 0, 0}, {0, 4, 0}, {0, 0, 4}, 0.75, 0.0),   // x=0     (+x)
        Card({4, 0, 0}, {0, 0, 4}, {0, 4, 0}, 0.50, 0.0),   // x=4     (-x)
        Card({0, 0, 0}, {4, 0, 0}, {0, 4, 0}, 0.65, 0.0),   // z=0     (+z)
        Card({0, 0, 4}, {0, 4, 0}, {4, 0, 0}, 0.45, 0.0),   // z=4     (-z)
    };
}

uint32_t CentreTexel(const CardRadiosity& Solver, uint32_t Card, uint32_t TexelsPerCard)
{
    // Texels are emitted card-major in build order; pick nearest to the card centre by position instead.
    uint32_t Best = UINT32_MAX;
    double   BestD2 = 1e30;
    (void)TexelsPerCard;
    SpatialVector Centre{2, 2, 2};
    for (uint32_t T = 0; T < Solver.TexelCount(); ++T)
    {
        if (Solver.TexelCard(T) != Card) continue;
        const SpatialVector C = Solver.TexelCentre(T);
        // Distance to the card's own centre: project room centre onto the card plane coordinate-wise.
        SpatialVector Target = Centre;
        if (Solver.TexelNormal(T).Y != 0) Target.Y = C.Y;
        if (Solver.TexelNormal(T).X != 0) Target.X = C.X;
        if (Solver.TexelNormal(T).Z != 0) Target.Z = C.Z;
        const double D2 = (C.X - Target.X) * (C.X - Target.X) + (C.Y - Target.Y) * (C.Y - Target.Y) +
                          (C.Z - Target.Z) * (C.Z - Target.Z);
        if (D2 < BestD2) { BestD2 = D2; Best = T; }
    }
    return Best;
}

} // namespace

int main(int Argc, char** Argv)
{
    const std::string PpmPrefix = Argc > 1 ? Argv[1] : "";

    // ------------------------------------------------------------------ exact SDF values
    {
        DistanceFieldScene Scene;
        Scene.AddPrimitive(Sphere({0, 0, 0}, 2.0));
        Check(Near(Scene.Evaluate({3, 0, 0}).Distance, 1.0, 1e-12), "SDF sphere: outside");
        Check(Near(Scene.Evaluate({0, 0, 0}).Distance, -2.0, 1e-12), "SDF sphere: centre is -r");
        Check(Near(Scene.Evaluate({2, 0, 0}).Distance, 0.0, 1e-12), "SDF sphere: surface is 0");

        Scene.Clear();
        Scene.AddPrimitive(Box({0, 0, 0}, {1, 2, 3}));
        Check(Near(Scene.Evaluate({2, 0, 0}).Distance, 1.0, 1e-12), "SDF box: face axis");
        Check(Near(Scene.Evaluate({0, 0, 0}).Distance, -1.0, 1e-12), "SDF box: interior");
        Check(Near(Scene.Evaluate({2, 3, 0}).Distance, std::sqrt(2.0), 1e-12), "SDF box: edge diagonal");

        Scene.Clear();
        Scene.AddPrimitive(HalfSpace({0, 0, 0}, {0, 1, 0}));
        Check(Near(Scene.Evaluate({0, 5, 0}).Distance, 5.0, 1e-12), "SDF halfspace: above");
        Check(Near(Scene.Evaluate({7, -2, 9}).Distance, -2.0, 1e-12), "SDF halfspace: below");

        Scene.Clear();
        Scene.AddPrimitive(Sphere({-5, 0, 0}, 1.0));
        Scene.AddPrimitive(Sphere({5, 0, 0}, 1.0));
        const DistanceFieldSample NearB = Scene.Evaluate({3.5, 0, 0});
        Check(NearB.Primitive == 1 && Near(NearB.Distance, 0.5, 1e-12), "SDF union: min + owner");

        Scene.Clear();
        const DistanceFieldSample Empty = Scene.Evaluate({0, 0, 0});
        Check(Empty.Primitive == UINT32_MAX && Empty.Distance > 1e29, "SDF empty scene is far + unowned");
        Check(!Scene.Trace({0, 0, 0}, {1, 0, 0}, 100.0).Hit, "trace: empty scene never hits");
    }

    // ------------------------------------------------------------------ tracing vs closed form
    {
        DistanceFieldScene Scene;
        Scene.AddPrimitive(Sphere({0, 0, 0}, 2.0));
        const double Analytic = 5.0 - std::sqrt(4.0 - 0.09);   // ray at height 0.3 from x=-5
        const DistanceTraceResult Hit = Scene.Trace({-5, 0.3, 0}, {1, 0, 0}, 100.0);
        Check(Hit.Hit && Near(Hit.Distance, Analytic, 2e-3), "trace: ray/sphere matches closed form");
        Print("trace sphere: %.5f vs analytic %.5f (%u steps)\n", Hit.Distance, Analytic, Hit.StepsUsed);
        Check(!Scene.Trace({-5, 3.0, 0}, {1, 0, 0}, 100.0).Hit, "trace: offset ray misses");

        const SpatialVector N = Scene.NormalAt({2, 0, 0});
        Check(Near(N.X, 1.0, 2e-3) && Near(N.Y, 0.0, 2e-3) && Near(N.Z, 0.0, 2e-3), "normal: sphere +x pole");

        Scene.Clear();
        Scene.AddPrimitive(Box({0, 0, 0}, {1, 1, 1}));
        const DistanceTraceResult BoxHit = Scene.Trace({-5, 0.2, -0.4}, {1, 0, 0}, 100.0);
        Check(BoxHit.Hit && Near(BoxHit.Distance, 4.0, 2e-3), "trace: ray/box face distance");
        const SpatialVector BN = Scene.NormalAt({0, 1, 0});
        Check(Near(BN.Y, 1.0, 2e-3), "normal: box top face");

        // Segment visibility, incl. resting-on-surface endpoints.
        Scene.Clear();
        Scene.AddPrimitive(Sphere({0, 0, 0}, 1.0));
        Check(Scene.SegmentVisible({-3, 2, 0}, {3, 2, 0}), "segment: clear line visible");
        Check(!Scene.SegmentVisible({-3, 0, 0}, {3, 0, 0}), "segment: through sphere occluded");
        Check(Scene.SegmentVisible({1, 0, 0}, {4, 1, 0}), "segment: endpoint on surface not self-occluded");
    }

    // ------------------------------------------------------------------ AO behaviour
    {
        DistanceFieldScene Open;
        Open.AddPrimitive(HalfSpace({0, 0, 0}, {0, 1, 0}));
        Check(Near(Open.AmbientOcclusion({0, 0.01, 0}, {0, 1, 0}, 0.5), 1.0, 1e-12), "AO: open ground is 1");

        const auto CorridorAO = [](double HalfWidth) {
            DistanceFieldScene Scene;
            Scene.AddPrimitive(HalfSpace({0, 0, 0}, {0, 1, 0}));
            Scene.AddPrimitive(Box({-(HalfWidth + 1.0), 1, 0}, {1, 1, 1}));
            Scene.AddPrimitive(Box({(HalfWidth + 1.0), 1, 0}, {1, 1, 1}));
            return Scene.AmbientOcclusion({0, 0.05, 0}, {0, 1, 0}, 0.5);
        };
        const double Wide = CorridorAO(0.30), Narrow = CorridorAO(0.15);
        Print("AO: open 1.0, corridor 0.30 -> %.3f, corridor 0.15 -> %.3f\n", Wide, Narrow);
        Check(Wide < 1.0 && Narrow < Wide, "AO: narrower corridor occludes more (monotone)");

        DistanceFieldScene Ceiling;
        Ceiling.AddPrimitive(HalfSpace({0, 0, 0}, {0, 1, 0}));
        Ceiling.AddPrimitive(Box({0, 0.8, 0}, {1, 0.5, 1}));
        Check(Ceiling.AmbientOcclusion({0, 0.02, 0}, {0, 1, 0}, 0.5) < 0.7, "AO: low ceiling occludes");
    }

    // ------------------------------------------------------------------ indirect irradiance oracles
    {
        DistanceFieldScene Sky;
        Sky.AddPrimitive(HalfSpace({0, 0, 0}, {0, 1, 0}, 0.0));
        Sky.AddPrimitive(HalfSpace({0, 3, 0}, {0, -1, 0}, 2.0));   // solid emissive above y=3
        DeterministicRandom R1(11, 12);
        const double Full = Sky.IndirectIrradiance({0, 0.01, 0}, {0, 1, 0}, 256, R1);
        Print("irradiance full hemisphere: %.5f vs pi*L %.5f\n", Full, Pi * 2.0);
        Check(Near(Full, Pi * 2.0, 0.02 * Pi * 2.0), "irradiance: hemisphere-filling emitter is pi*L");

        DistanceFieldScene Ball;
        Ball.AddPrimitive(HalfSpace({0, 0, 0}, {0, 1, 0}, 0.0));
        Ball.AddPrimitive(Sphere({0, 4, 0}, 1.0, 3.0));
        DeterministicRandom R2(21, 22);
        const double Estimate = Ball.IndirectIrradiance({0, 0.01, 0}, {0, 1, 0}, 4096, R2);
        const double Sin2 = (1.0 / 4.0) * (1.0 / 4.0);
        const double Truth = Pi * 3.0 * Sin2;
        Print("irradiance sphere: %.5f vs analytic %.5f (%.1f%% off)\n", Estimate, Truth,
              100.0 * std::abs(Estimate - Truth) / Truth);
        Check(Near(Estimate, Truth, 0.25 * Truth), "irradiance: finite sphere matches projected solid angle");

        DeterministicRandom R3(31, 32);
        Check(Ball.IndirectIrradiance({0, 0.01, 0}, {0, 1, 0}, 0, R3) == 0.0, "irradiance: zero samples is 0");
    }

    // ------------------------------------------------------------------ baked records
    {
        DistanceFieldScene Scene;
        Scene.AddPrimitive(Sphere({0.3, -0.2, 0.1}, 1.4));
        Scene.AddPrimitive(Box({-1.2, 0.8, -0.5}, {0.5, 0.4, 0.6}));
        BakedDistanceField Coarse, Fine;
        Check(Coarse.Bake(Scene, {-3, -3, -3}, {3, 3, 3}, 16, 16, 16), "bake: 16^3 builds");
        Check(Fine.Bake(Scene, {-3, -3, -3}, {3, 3, 3}, 32, 32, 32), "bake: 32^3 builds");
        Check(!Coarse.Bake(Scene, {-3, -3, -3}, {3, 3, 3}, 1, 16, 16), "bake: resolution < 2 refused");
        Check(Coarse.Bake(Scene, {-3, -3, -3}, {3, 3, 3}, 16, 16, 16), "bake: rebuild after refusal works");

        // Exact at grid nodes.
        const double NodeExact = std::abs(Fine.Sample({-3, -3, -3}) - Scene.Evaluate({-3, -3, -3}).Distance);
        Check(NodeExact < 1e-12, "bake: node values are exact");

        DeterministicRandom R(41, 42);
        double ErrorCoarse = 0.0, ErrorFine = 0.0;
        const int Probes = 400;
        for (int I = 0; I < Probes; ++I)
        {
            const SpatialVector P{-2.8 + 5.6 * R.NextUnit(), -2.8 + 5.6 * R.NextUnit(), -2.8 + 5.6 * R.NextUnit()};
            const double Exact = Scene.Evaluate(P).Distance;
            ErrorCoarse += std::abs(Coarse.Sample(P) - Exact);
            ErrorFine += std::abs(Fine.Sample(P) - Exact);
        }
        ErrorCoarse /= Probes; ErrorFine /= Probes;
        Print("bake mean |error|: 16^3 %.4f m, 32^3 %.4f m\n", ErrorCoarse, ErrorFine);
        Check(ErrorFine < ErrorCoarse * 0.8, "bake: error improves with resolution");
        Check(ErrorFine < 0.05, "bake: 32^3 mean error under 5 cm in a 6 m volume");

        BakedDistanceField Unbaked;
        Check(Unbaked.Sample({0, 0, 0}) > 1e29, "bake: unbaked sample is far (defined)");
    }

    // ------------------------------------------------------------------ card form factors
    {
        CardRadiosity Broken;
        Check(!Broken.Build({SurfaceCard{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, 4, 4, 0.5, 0.0}}, nullptr),
              "cards: zero-area card refused");
        Check(!Broken.Build({SurfaceCard{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 4, 4, 1.0, 0.0}}, nullptr),
              "cards: albedo 1 refused (non-convergent system)");

        // Two cards facing away exchange nothing.
        CardRadiosity Backs;
        const SurfaceCard A{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 2, 2, 0.5, 1.0};    // normal +z
        const SurfaceCard B{{0, 0, 2}, {1, 0, 0}, {0, 1, 0}, 2, 2, 0.5, 1.0};    // normal +z too: backs together
        Check(Backs.Build({A, B}, nullptr), "cards: two-card build");
        Check(Backs.FormFactorSum(0) == 0.0 && Backs.FormFactorSum(4) == 0.0, "cards: back-facing factors are 0");
        Backs.Solve();
        Check(Near(Backs.Radiosity(0), 1.0, 1e-12), "cards: isolated emitter radiosity equals its emission");

        // Closed room: reciprocity identity + form-factor sums + convergence with refinement.
        CardRadiosity Room4, Room8;
        Check(Room4.Build(CubeRoom(4), nullptr), "room: 4x4 texels build");
        Check(Room8.Build(CubeRoom(8), nullptr), "room: 8x8 texels build");
        bool Reciprocal = true;
        DeterministicRandom PairRandom(55, 66);
        for (int Trial = 0; Trial < 200; ++Trial)
        {
            const uint32_t I = PairRandom.NextBelow(Room8.TexelCount());
            const uint32_t J = PairRandom.NextBelow(Room8.TexelCount());
            Reciprocal &= Near(Room8.TexelArea(I) * Room8.FormFactor(I, J),
                               Room8.TexelArea(J) * Room8.FormFactor(J, I), 1e-12);
        }
        Check(Reciprocal, "room: form-factor reciprocity A_i F_ij = A_j F_ji");

        const uint32_t FloorCentre8 = CentreTexel(Room8, 0, 64);
        const uint32_t FloorCentre4 = CentreTexel(Room4, 0, 16);
        const double Sum8 = Room8.FormFactorSum(FloorCentre8);
        const double Sum4 = Room4.FormFactorSum(FloorCentre4);
        Print("room floor-centre form-factor sum: 4x4 %.4f, 8x8 %.4f (closed room -> 1)\n", Sum4, Sum8);
        Check(Sum8 > 0.85 && Sum8 <= 1.0 + 1e-9, "room: closed-enclosure sum approaches 1, never exceeds");
        Check(Sum8 >= Sum4 - 1e-9, "room: sum converges upward (from below) with refinement");

        bool SumsBounded = true;
        for (uint32_t T = 0; T < Room8.TexelCount(); ++T) SumsBounded &= Room8.FormFactorSum(T) <= 1.0 + 1e-9;
        Check(SumsBounded, "room: every texel sum <= 1");
    }

    // ------------------------------------------------------------------ radiosity solve + energy + reference
    {
        const std::vector<SurfaceCard> Cards = CubeRoom(6);
        CardRadiosity Room;
        Check(Room.Build(Cards, nullptr), "radiosity: room builds");
        const double Residual = Room.Solve(400, 1e-10);
        Print("radiosity residual %.2e after solve\n", Residual);
        Check(Residual < 1e-8, "radiosity: Jacobi iteration converges");

        const double Emitted = Room.EmittedPower(), Absorbed = Room.AbsorbedPower();
        Print("power (6x6): emitted %.4f, absorbed %.4f (%.1f%% accounted)\n", Emitted, Absorbed,
              100.0 * Absorbed / Emitted);
        // With regularised factors the discrete system leaks (sums < 1) instead of creating energy;
        // the leak is a discretisation error and must shrink under texel refinement. Audit that.
        Check(Emitted > 0.0 && Absorbed / Emitted > 0.80 && Absorbed / Emitted <= 1.0 + 1e-9,
              "radiosity: no energy created; >=80% accounted at 6x6 texels");
        {
            CardRadiosity FineRoom;
            Check(FineRoom.Build(CubeRoom(12), nullptr), "radiosity: 12x12 room builds");
            FineRoom.Solve(400, 1e-9);
            const double FineRatio = FineRoom.AbsorbedPower() / FineRoom.EmittedPower();
            Print("power (12x12): %.1f%% accounted\n", 100.0 * FineRatio);
            Check(FineRatio > Absorbed / Emitted && FineRatio > 0.90,
                  "radiosity: energy accounting improves under refinement (>90% at 12x12)");
        }

        // Independent reference: aggregate form factors onto 2x2-texel patches (54 unknowns),
        // solve that system directly (generic Gaussian elimination), compare patch means.
        // (A 1-patch-per-wall reference is a different Galerkin basis: its wall means legitimately
        // differ from the fine solution by the within-wall covariance, ~8% on the floor here.)
        const uint32_t TexelsPerCard = 36, PatchStride = 2;   // 6x6 texels -> 3x3 patches per card
        const auto PatchOf = [&](uint32_t Texel) {
            const uint32_t Local = Texel % TexelsPerCard;
            const uint32_t U = Local % 6, V = Local / 6;
            return Room.TexelCard(Texel) * 9u + (V / PatchStride) * 3u + (U / PatchStride);
        };
        const uint32_t Patches = 6 * 9;
        std::vector<double> PatchCount(Patches, 0.0);
        for (uint32_t T = 0; T < Room.TexelCount(); ++T) PatchCount[PatchOf(T)] += 1.0;
        std::vector<std::vector<double>> System(Patches, std::vector<double>(Patches + 1, 0.0));
        for (uint32_t I = 0; I < Room.TexelCount(); ++I)
            for (uint32_t J = 0; J < Room.TexelCount(); ++J)
                System[PatchOf(I)][PatchOf(J)] += Room.FormFactor(I, J) / PatchCount[PatchOf(I)];
        for (uint32_t P = 0; P < Patches; ++P)
        {
            const uint32_t Card = P / 9;
            for (uint32_t K = 0; K < Patches; ++K)
                System[P][K] = (P == K ? 1.0 : 0.0) - Cards[Card].Albedo * System[P][K];
            System[P][Patches] = Cards[Card].EmittedExitance;
        }
        bool ReferenceOk = true;
        for (uint32_t C = 0; C < Patches && ReferenceOk; ++C)
        {
            uint32_t Pivot = C;
            for (uint32_t R2 = C + 1; R2 < Patches; ++R2)
                if (std::abs(System[R2][C]) > std::abs(System[Pivot][C])) Pivot = R2;
            ReferenceOk &= std::abs(System[Pivot][C]) > 1e-14;
            std::swap(System[C], System[Pivot]);
            for (uint32_t R2 = 0; R2 < Patches; ++R2)
            {
                if (R2 == C) continue;
                const double F = System[R2][C] / System[C][C];
                for (uint32_t K = C; K <= Patches; ++K) System[R2][K] -= F * System[C][K];
            }
        }
        Check(ReferenceOk, "radiosity: patch reference system solvable");
        std::vector<double> PatchMeans(Patches, 0.0);
        for (uint32_t T = 0; T < Room.TexelCount(); ++T)
            PatchMeans[PatchOf(T)] += Room.Radiosity(T) / PatchCount[PatchOf(T)];
        double MaxPatchError = 0.0;
        for (uint32_t P = 0; P < Patches; ++P)
        {
            const double Ref = System[P][Patches] / System[P][P];
            MaxPatchError = std::max(MaxPatchError, std::abs(PatchMeans[P] - Ref) / std::max(Ref, 1e-9));
        }
        Print("radiosity vs patch direct solve: max relative patch error %.2f%%\n", 100.0 * MaxPatchError);
        Check(MaxPatchError < 0.02, "radiosity: texel solver matches independent patch direct solve (2%)");

        // Distance-field occlusion: a sphere between the emissive ceiling and floor centre.
        DistanceFieldScene Occluders;
        Occluders.AddPrimitive(Sphere({2, 2, 2}, 0.9));
        CardRadiosity Shadowed;
        Check(Shadowed.Build(Cards, &Occluders), "occlusion: room builds with DF occluders");
        const uint32_t FloorC = CentreTexel(Shadowed, 0, 36);
        const uint32_t CeilC  = CentreTexel(Shadowed, 1, 36);
        Check(Shadowed.FormFactor(FloorC, CeilC) == 0.0, "occlusion: blocked texel pair factor is exactly 0");
        Shadowed.Solve(400, 1e-10);
        const uint32_t FloorCRoom = CentreTexel(Room, 0, 36);
        Print("floor centre radiosity: %.4f open vs %.4f occluded\n", Room.Radiosity(FloorCRoom),
              Shadowed.Radiosity(FloorC));
        Check(Shadowed.Radiosity(FloorC) < 0.9 * Room.Radiosity(FloorCRoom),
              "occlusion: shadowed texel is decisively darker");

        // No emission anywhere -> zero field.
        std::vector<SurfaceCard> DarkCards = CubeRoom(2);
        for (SurfaceCard& C : DarkCards) C.EmittedExitance = 0.0;
        CardRadiosity Dark;
        Dark.Build(DarkCards, nullptr);
        Dark.Solve();
        bool AllZero = true;
        for (uint32_t T = 0; T < Dark.TexelCount(); ++T) AllZero &= Dark.Radiosity(T) == 0.0;
        Check(AllZero, "radiosity: dark room stays dark (no energy invented)");
    }

    // ------------------------------------------------------------------ indirect declaration wiring
    {
        const IlluminationFrameDeclaration Frame = DeclareIlluminationFrame(1920, 1080);
        ResolvedSubmissionOrder Order;
        std::string Diagnostic;
        Check(ResolveSubmissionOrder(Frame.Units, Order, Diagnostic), "indirect: merged frame resolves");
        const auto PositionOf = [&](const char* Name) {
            for (uint32_t Pos = 0; Pos < Order.Order.size(); ++Pos)
                if (Frame.Units[Order.Order[Pos]].Name == Name) return static_cast<int>(Pos);
            return -1;
        };
        const bool Ordered =
            PositionOf("DistanceRecordsSubmission") < PositionOf("DistanceTraversalSubmission") &&
            PositionOf("DistanceTraversalSubmission") < PositionOf("DistanceTransferSubmission") &&
            PositionOf("CardCaptureSubmission") < PositionOf("CardDirectSubmission") &&
            PositionOf("CardDirectSubmission") < PositionOf("CardRadiositySubmission") &&
            PositionOf("CardRadiositySubmission") < PositionOf("CardFinalSubmission") &&
            PositionOf("CardFinalSubmission") < PositionOf("DistanceTransferSubmission") &&
            PositionOf("DistanceTransferSubmission") < PositionOf("DirectRadianceSubmission");
        Check(Ordered, "indirect: DF + card prelude resolves ahead of DirectRadiance");

        DerivedBarriers Barriers;
        Check(DeriveBarriers(Frame.Units, Order, Frame.Targets, Barriers, Diagnostic),
              "indirect: barriers derive over the merged frame");
        if (!Diagnostic.empty()) Print("      diagnostic: %s\n", Diagnostic.c_str());

        ResolvedTransientAtlas Atlas;
        Check(ResolveTransientTargetAtlas(Frame.Units, Order, Frame.Targets, Atlas, Diagnostic),
              "indirect: atlas resolves over the merged frame");
        bool CardsOffAtlas = true;
        for (const AtlasPlacement& Placement : Atlas.Placements)
            if (Placement.Name == "CardAtlas" || Placement.Name == "CardLighting")
                CardsOffAtlas &= (Placement.Bin == UINT32_MAX);
        Check(CardsOffAtlas, "indirect: persistent card cache stays off the transient atlas");
    }

    // ------------------------------------------------------------------ evidence render
    if (!PpmPrefix.empty())
    {
        // A small sphere-traced room lit only by distance-field indirect irradiance.
        DistanceFieldScene Room;
        Room.AddPrimitive(HalfSpace({0, 0, 0}, {0, 1, 0}));              // floor
        Room.AddPrimitive(HalfSpace({0, 4, 0}, {0, -1, 0}));             // ceiling
        Room.AddPrimitive(HalfSpace({-3, 0, 0}, {1, 0, 0}));             // walls
        Room.AddPrimitive(HalfSpace({3, 0, 0}, {-1, 0, 0}));
        Room.AddPrimitive(HalfSpace({0, 0, -3}, {0, 0, 1}));
        Room.AddPrimitive(HalfSpace({0, 0, 5}, {0, 0, -1}));
        Room.AddPrimitive(Sphere({-1.2, 2.7, -1.0}, 0.55, 5.0));         // emissive lamp ball
        Room.AddPrimitive(Sphere({0.6, 0.8, -0.6}, 0.8));                // occluder ball
        Room.AddPrimitive(Box({1.5, 0.5, -1.6}, {0.5, 0.5, 0.5}));       // crate

        const uint32_t W = 128, H = 128;
        const std::string Path = PpmPrefix + "distance-room.ppm";
        FILE* File = std::fopen(Path.c_str(), "wb");
        if (File != nullptr)
        {
            std::fprintf(File, "P6\n%u %u\n255\n", W, H);
            const SpatialVector Eye{0, 1.8, 4.2};
            DeterministicRandom PixelRandom(7, 7);
            for (uint32_t Y = 0; Y < H; ++Y)
            {
                for (uint32_t X = 0; X < W; ++X)
                {
                    const double U = (static_cast<double>(X) / W) * 2.0 - 1.0;
                    const double V = 1.0 - (static_cast<double>(Y) / H) * 2.0;
                    SpatialVector Dir{U * 0.9, V * 0.9, -1.6};
                    const double Len = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y + Dir.Z * Dir.Z);
                    Dir = {Dir.X / Len, Dir.Y / Len, Dir.Z / Len};
                    const DistanceTraceResult Hit = Room.Trace(Eye, Dir, 100.0);
                    double Value = 0.0;
                    if (Hit.Hit)
                    {
                        const SpatialVector At{ Eye.X + Dir.X * Hit.Distance, Eye.Y + Dir.Y * Hit.Distance,
                                                Eye.Z + Dir.Z * Hit.Distance };
                        const double Emissive = Room.Primitives()[Hit.Primitive].EmissiveRadiance;
                        if (Emissive > 0.0)
                        {
                            Value = Emissive * 0.35;   // lamp seen directly (display-scaled)
                        }
                        else
                        {
                            const SpatialVector N = Room.NormalAt(At);
                            const double Irr = Room.IndirectIrradiance(At, N, 48, PixelRandom);
                            const double AO = Room.AmbientOcclusion(At, N, 0.6);
                            Value = Irr * AO * 0.55;
                        }
                    }
                    const double Mapped = std::pow(std::clamp(Value, 0.0, 1.0), 1.0 / 1.8);
                    const unsigned char B = static_cast<unsigned char>(Mapped * 255.0);
                    const unsigned char RGB[3] = {B, static_cast<unsigned char>(B * 0.96), static_cast<unsigned char>(B * 0.90)};
                    std::fwrite(RGB, 1, 3, File);
                }
            }
            std::fclose(File);
            Print("evidence image -> %s\n", Path.c_str());
        }
    }

    Print("Indirect illumination validation: %d/%d checks passed\n", Total - Failures, Total);
    return Failures == 0 ? 0 : 1;
}
