/*====================================================================================================================================
                                                    PREDICATEVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Exact predicates vs an independent 256-bit integer reference on near-degenerate inputs (and the naive determinant)

#include "Engine/GeometryProcessing/Precision/ExactPredicates.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Portable 256-bit two's-complement integer (MSVC has no __int128): 8 × 32-bit limbs, wrap-around arithmetic.
    //    The oracle's values stay below 2²¹⁶ in magnitude (in-circle), so 256 bits never wrap.
    struct WideInteger
    {
        std::array<uint32_t, 8> Limbs = {};                                                 // [-] - Little-endian limbs

        WideInteger() = default;
        explicit WideInteger(int64_t Seed)
        {
            const uint64_t Bits = static_cast<uint64_t>(Seed);                               // [-] - Two's-complement bits
            Limbs[0] = static_cast<uint32_t>(Bits);
            Limbs[1] = static_cast<uint32_t>(Bits >> 32);
            for (size_t LimbIndex = 2; LimbIndex < Limbs.size(); ++LimbIndex) Limbs[LimbIndex] = Seed < 0 ? 0xFFFFFFFFu : 0u;
        }

        friend WideInteger operator+(const WideInteger& First, const WideInteger& Second)
        {
            WideInteger Sum;
            uint64_t    Carry = 0;                                                           // [-] - Limb carry
            for (size_t LimbIndex = 0; LimbIndex < Sum.Limbs.size(); ++LimbIndex)
            {
                const uint64_t Partial = static_cast<uint64_t>(First.Limbs[LimbIndex]) + Second.Limbs[LimbIndex] + Carry;
                Sum.Limbs[LimbIndex] = static_cast<uint32_t>(Partial);
                Carry = Partial >> 32;
            }
            return Sum;
        }

        friend WideInteger operator-(const WideInteger& First, const WideInteger& Second)
        {
            WideInteger Negated;
            for (size_t LimbIndex = 0; LimbIndex < Negated.Limbs.size(); ++LimbIndex) Negated.Limbs[LimbIndex] = ~Second.Limbs[LimbIndex];
            return First + Negated + WideInteger(1);
        }

        friend WideInteger operator*(const WideInteger& First, const WideInteger& Second)
        {
            WideInteger Product;
            for (size_t Low = 0; Low < First.Limbs.size(); ++Low)
            {
                uint64_t Carry = 0;                                                          // [-] - Row carry
                for (size_t High = 0; Low + High < Product.Limbs.size(); ++High)
                {
                    const uint64_t Partial = static_cast<uint64_t>(First.Limbs[Low]) * Second.Limbs[High] + Product.Limbs[Low + High] + Carry;
                    Product.Limbs[Low + High] = static_cast<uint32_t>(Partial);
                    Carry = Partial >> 32;
                }
            }
            return Product;
        }

        [[nodiscard]] int Sign() const
        {
            if (Limbs.back() & 0x80000000u) return -1;
            for (uint32_t Limb : Limbs) if (Limb != 0) return 1;
            return 0;
        }
    };

    // 💡 Every double in [1, 2) is an integer multiple of 2⁻⁵², so scaled coordinates are exact integers below 2⁵³ and
    //    the orientation determinant is an exact integer (products below 2¹⁰⁷) — an oracle independent of the
    //    expansion arithmetic under test.
    [[nodiscard]] int ReferenceOrient2D(Vector2 A, Vector2 B, Vector2 C)
    {
        const auto Scale = [](double Coordinate) { return WideInteger(static_cast<int64_t>(std::ldexp(Coordinate, 52))); };
        const WideInteger Determinant = (Scale(A.X) - Scale(C.X)) * (Scale(B.Y) - Scale(C.Y)) - (Scale(A.Y) - Scale(C.Y)) * (Scale(B.X) - Scale(C.X));
        return Determinant.Sign();
    }

    // 📝 3D coordinates are full doubles in [1, 2) (2⁻⁵² grid): differences < 2⁵³, triple products < 2¹⁶¹ — exact in 256 bits.
    [[nodiscard]] int ReferenceOrient3D(Vector3 A, Vector3 B, Vector3 C, Vector3 D)
    {
        const auto Scale = [](double Coordinate) { return WideInteger(static_cast<int64_t>(std::ldexp(Coordinate, 52))); };
        const WideInteger ADx = Scale(A.X) - Scale(D.X), ADy = Scale(A.Y) - Scale(D.Y), ADz = Scale(A.Z) - Scale(D.Z);
        const WideInteger BDx = Scale(B.X) - Scale(D.X), BDy = Scale(B.Y) - Scale(D.Y), BDz = Scale(B.Z) - Scale(D.Z);
        const WideInteger CDx = Scale(C.X) - Scale(D.X), CDy = Scale(C.Y) - Scale(D.Y), CDz = Scale(C.Z) - Scale(D.Z);
        const WideInteger Determinant = ADz * (BDx * CDy - CDx * BDy) + BDz * (CDx * ADy - ADx * CDy) + CDz * (ADx * BDy - BDx * ADy);
        return Determinant.Sign();
    }

    // 📝 In-circle on the 2⁻⁵² grid: differences < 2⁵³, lifts < 2¹⁰⁷, lift × minor < 2²¹⁴ — exact in 256 bits.
    [[nodiscard]] int ReferenceInCircle(Vector2 A, Vector2 B, Vector2 C, Vector2 D)
    {
        const auto Scale = [](double Coordinate) { return WideInteger(static_cast<int64_t>(std::ldexp(Coordinate, 52))); };
        const WideInteger ADx = Scale(A.X) - Scale(D.X), ADy = Scale(A.Y) - Scale(D.Y);
        const WideInteger BDx = Scale(B.X) - Scale(D.X), BDy = Scale(B.Y) - Scale(D.Y);
        const WideInteger CDx = Scale(C.X) - Scale(D.X), CDy = Scale(C.Y) - Scale(D.Y);
        const WideInteger Determinant = (ADx * ADx + ADy * ADy) * (BDx * CDy - CDx * BDy) + (BDx * BDx + BDy * BDy) * (CDx * ADy - ADx * CDy) +
                                        (CDx * CDx + CDy * CDy) * (ADx * BDy - BDx * ADy);
        return Determinant.Sign();
    }

    [[nodiscard]] int SignOf(double Magnitude) { return (Magnitude > 0.0) - (Magnitude < 0.0); }

    struct PredicateTally
    {
        long Cases          = 0;                                                             // [-] - Inputs evaluated
        long ExactWrong     = 0;                                                             // [-] - Exact predicate disagrees with oracle
        long NaiveWrong     = 0;                                                             // [-] - Naive sign disagrees with oracle
        long Degenerate     = 0;                                                             // [-] - Oracle sign 0
        long ExpansionUsed  = 0;                                                             // [-] - Filter fell through
    };

    int GateFailures = 0;                                                                    // [-] - Failed rows

    void ReportTally(const char* Label, const PredicateTally& Tally)
    {
        const bool Passed = Tally.ExactWrong == 0;
        if (!Passed) GateFailures += 1;
        std::printf("| %s | %s | %ld | %ld | %ld | %ld (%.1f%%) | %ld (%.2f%%) |\n", Passed ? "\xF0\x9F\x9F\xA2" : "\xF0\x9F\x94\xB4", Label, Tally.Cases, Tally.Degenerate,
                    Tally.ExactWrong, Tally.NaiveWrong, 100.0 * Tally.NaiveWrong / Tally.Cases, Tally.ExpansionUsed, 100.0 * Tally.ExpansionUsed / Tally.Cases);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::mt19937_64                        Generator(20261020);
    std::uniform_real_distribution<double> Unit(1.0, 2.0);
    std::uniform_real_distribution<double> Fraction(0.0, 1.0);
    constexpr long CaseCount = 200000;                                                       // [-] - Cases per family

    std::printf("# GeometryProcessing — PredicateValidation\n\n");
    std::printf("| | Family | Cases | Oracle zero | Exact wrong | Naive wrong | Expansion fallback |\n|---|---|---|---|---|---|---|\n");

    // ---- Orient2D, C on the rounded segment AB (near-collinear)
    PredicateTally Collinear;
    for (long CaseIndex = 0; CaseIndex < CaseCount; ++CaseIndex)
    {
        const Vector2 A{ Unit(Generator), Unit(Generator) };
        const Vector2 B{ Unit(Generator), Unit(Generator) };
        const double  λ = Fraction(Generator);                                               // [-] - Position along AB
        const Vector2 C{ A.X + λ * (B.X - A.X), A.Y + λ * (B.Y - A.Y) };
        const int     Oracle  = ReferenceOrient2D(A, B, C);
        const auto    Outcome = Orient2D(A, B, C);
        Collinear.Cases         += 1;
        Collinear.Degenerate    += Oracle == 0;
        Collinear.ExactWrong    += Outcome.Sign != Oracle;
        Collinear.NaiveWrong    += SignOf(NaiveOrient2D(A, B, C)) != Oracle;
        Collinear.ExpansionUsed += Outcome.Resolution == PredicateResolution::Expansion;
    }
    ReportTally("Orient2D near-collinear", Collinear);

    // ---- Orient2D, random (well-conditioned) — the filter should decide almost everything
    PredicateTally Random2D;
    for (long CaseIndex = 0; CaseIndex < CaseCount; ++CaseIndex)
    {
        const Vector2 A{ Unit(Generator), Unit(Generator) }, B{ Unit(Generator), Unit(Generator) }, C{ Unit(Generator), Unit(Generator) };
        const int  Oracle  = ReferenceOrient2D(A, B, C);
        const auto Outcome = Orient2D(A, B, C);
        Random2D.Cases += 1; Random2D.Degenerate += Oracle == 0; Random2D.ExactWrong += Outcome.Sign != Oracle;
        Random2D.NaiveWrong += SignOf(NaiveOrient2D(A, B, C)) != Oracle; Random2D.ExpansionUsed += Outcome.Resolution == PredicateResolution::Expansion;
    }
    ReportTally("Orient2D random", Random2D);

    // ---- Orient3D, D on the plane ABC up to rounding (near-coplanar)
    PredicateTally Coplanar;
    for (long CaseIndex = 0; CaseIndex < CaseCount; ++CaseIndex)
    {
        const auto    Random = [&]() { return Vector3{ Unit(Generator), Unit(Generator), Unit(Generator) }; };
        const Vector3 A = Random(), B = Random(), C = Random();
        const double  λ = Fraction(Generator) * 0.5, μ = Fraction(Generator) * 0.5;          // [-] - Barycentric offsets
        const Vector3 D = A + (B - A) * λ + (C - A) * μ;                                     // [-] - Rounded point on the plane
        if (D.X < 1.0 || D.Y < 1.0 || D.Z < 1.0 || D.X >= 2.0 || D.Y >= 2.0 || D.Z >= 2.0) { --CaseIndex; continue; }
        const int  Oracle  = ReferenceOrient3D(A, B, C, D);
        const auto Outcome = Orient3D(A, B, C, D);
        Coplanar.Cases         += 1;
        Coplanar.Degenerate    += Oracle == 0;
        Coplanar.ExactWrong    += Outcome.Sign != Oracle;
        Coplanar.NaiveWrong    += SignOf(NaiveOrient3D(A, B, C, D)) != Oracle;
        Coplanar.ExpansionUsed += Outcome.Resolution == PredicateResolution::Expansion;
    }
    ReportTally("Orient3D near-coplanar", Coplanar);

    // ---- Orient3D, exactly coplanar points on z = (x + y)/2 with 26-bit lattice coordinates — oracle zero by construction
    PredicateTally Lattice;
    std::uniform_int_distribution<int64_t> Lattice26(0, (int64_t(1) << 26) - 1);
    for (long CaseIndex = 0; CaseIndex < CaseCount; ++CaseIndex)
    {
        const auto Lift = [&]()
        {
            const int64_t X = Lattice26(Generator), Y = Lattice26(Generator);               // [-] - Lattice indices
            return Vector3{ 1.0 + std::ldexp(static_cast<double>(X), -26), 1.0 + std::ldexp(static_cast<double>(Y), -26), 1.0 + std::ldexp(static_cast<double>(X + Y), -27) };
        };
        const Vector3 A = Lift(), B = Lift(), C = Lift(), D = Lift();
        const int  Oracle  = ReferenceOrient3D(A, B, C, D);
        const auto Outcome = Orient3D(A, B, C, D);
        Lattice.Cases += 1; Lattice.Degenerate += Oracle == 0; Lattice.ExactWrong += Outcome.Sign != Oracle;
        Lattice.NaiveWrong += SignOf(NaiveOrient3D(A, B, C, D)) != Oracle; Lattice.ExpansionUsed += Outcome.Resolution == PredicateResolution::Expansion;
    }
    ReportTally("Orient3D exact-coplanar lattice", Lattice);

    // ---- InCircle, D on the circle through A, B, C up to rounding (near-cocircular)
    PredicateTally Cocircular;
    for (long CaseIndex = 0; CaseIndex < CaseCount; ++CaseIndex)
    {
        const Vector2 Centre{ 1.5, 1.5 };
        const double  Radius = 0.1 + 0.35 * Fraction(Generator);                             // [-] - Keeps points in [1, 2)
        const auto    OnCircle = [&]() { const double θ = 6.283185307179586 * Fraction(Generator); return Vector2{ Centre.X + Radius * std::cos(θ), Centre.Y + Radius * std::sin(θ) }; };
        const Vector2 A = OnCircle(), B = OnCircle(), C = OnCircle(), D = OnCircle();
        const int  Oracle  = ReferenceInCircle(A, B, C, D);
        const auto Outcome = InCircle(A, B, C, D);
        Cocircular.Cases += 1; Cocircular.Degenerate += Oracle == 0; Cocircular.ExactWrong += Outcome.Sign != Oracle;
        Cocircular.NaiveWrong += SignOf(NaiveInCircle(A, B, C, D)) != Oracle; Cocircular.ExpansionUsed += Outcome.Resolution == PredicateResolution::Expansion;
    }
    ReportTally("InCircle near-cocircular", Cocircular);

    // ---- InCircle, exactly cocircular lattice points on x² + y² = 1105² (1105 = 5·13·17) — oracle zero by construction
    std::vector<Vector2> Circle;
    constexpr int64_t CircleRadius = 1105;                                                   // [-] - Many integer points
    for (int64_t X = -CircleRadius; X <= CircleRadius; ++X)
    {
        const int64_t Square = CircleRadius * CircleRadius - X * X;
        const int64_t Root   = static_cast<int64_t>(std::llround(std::sqrt(static_cast<double>(Square))));
        if (Root * Root != Square) continue;
        for (int64_t Y : { Root, -Root }) { Circle.push_back({ 1.0 + std::ldexp(static_cast<double>(X + CircleRadius), -12), 1.0 + std::ldexp(static_cast<double>(Y + CircleRadius), -12) }); if (Root == 0) break; }
    }
    PredicateTally Exact;
    std::uniform_int_distribution<size_t> Pick(0, Circle.size() - 1);
    for (long CaseIndex = 0; CaseIndex < CaseCount; ++CaseIndex)
    {
        const Vector2 A = Circle[Pick(Generator)], B = Circle[Pick(Generator)], C = Circle[Pick(Generator)], D = Circle[Pick(Generator)];
        const int  Oracle  = ReferenceInCircle(A, B, C, D);
        const auto Outcome = InCircle(A, B, C, D);
        Exact.Cases += 1; Exact.Degenerate += Oracle == 0; Exact.ExactWrong += Outcome.Sign != Oracle;
        Exact.NaiveWrong += SignOf(NaiveInCircle(A, B, C, D)) != Oracle; Exact.ExpansionUsed += Outcome.Resolution == PredicateResolution::Expansion;
    }
    char Label[96];
    std::snprintf(Label, sizeof(Label), "InCircle exact-cocircular lattice (%zu points)", Circle.size());
    ReportTally(Label, Exact);

    std::printf("\n**%s** — %d failing row(s)\n", GateFailures == 0 ? "PASS" : "FAIL", GateFailures);
    return GateFailures == 0 ? 0 : 1;
}
