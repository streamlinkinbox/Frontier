/*==========================================================================================================================================
                                                   SECONDARYRECONNECTIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify diffuse secondary-vertex transport in area and solid-angle measures using production reservoirs.

#include "Engine/Rendering/ReservoirSampling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                      TYPES AND CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr double π = 3.14159265358979323846;
constexpr unsigned CandidateCount = 8, BatchCount = 64, ReservoirCount = 4096;

struct SecondaryPoint
{
    double X = 0, Y = 0;
    bool Present = false;
};

struct ReceiverGeometry
{
    double Cosine = 0, ProjectedArea = 0;
    bool Visible = false;
};

struct EstimateMoments
{
    double Sum = 0, SquaredSum = 0;
    unsigned Count = 0;
    void Accumulate(double Estimate)
    {
        Sum += Estimate;
        SquaredSum += Estimate * Estimate;
        ++Count;
    }
    double Mean() const { return Sum / Count; }
    double StandardError() const
    {
        return std::sqrt(std::max(0., SquaredSum - Sum * Sum / Count) / (Count * (Count - 1.)));
    }
};

void Require(bool Predicate, const char* Explanation)
{
    if (!Predicate) throw std::runtime_error(Explanation);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             GEOMETRY, DENSITIES AND QUADRATURE
//------------------------------------------------------------------------------------------------------------------------

double Unit(std::mt19937_64& Random)
{
    return std::generate_canonical<double, 53>(Random);
}

ReceiverGeometry Geometry(SecondaryPoint Point, double ReceiverX, double Blocker)
{
    const double Δx = Point.X - ReceiverX;
    const double SquaredDistance = 1 + Δx * Δx + Point.Y * Point.Y;
    const double Cosine = 1 / std::sqrt(SquaredDistance);
    const bool Visible = Blocker == 0 || std::abs((Point.X + ReceiverX) * .5) > Blocker;
    return {Cosine, Cosine / SquaredDistance, Visible};
}

double AngularContribution(SecondaryPoint Point, const ReceiverGeometry& Receiver)
{
    const double Radiance = Point.X >= -.1 ? .7 : 0;
    return Receiver.Visible ? .6 * Radiance * Receiver.Cosine / π : 0;
}

SecondaryPoint Proposal(std::mt19937_64& Random, double Mixture, double Blocker)
{
    const double Choice = Unit(Random), U = Unit(Random), V = Unit(Random);
    SecondaryPoint Point;
    if (Choice < Mixture)
    {
        const double Radius = std::sqrt(U / (1 - U));
        Point = {Radius * std::cos(2 * π * V), Radius * std::sin(2 * π * V), true};
        Point.Present = std::abs(Point.X) <= .5 && std::abs(Point.Y) <= .5;
        if (Point.Present && !Geometry(Point, 0, Blocker).Visible) Point.Present = false;
    }
    else Point = {U - .5, V - .5, true};
    return Point;
}

double AreaDensity(SecondaryPoint Point, double Mixture, double Blocker)
{
    const auto Donor = Geometry(Point, 0, Blocker);
    return (1 - Mixture) + (Donor.Visible ? Mixture * Donor.Cosine * Donor.ProjectedArea / π : 0);
}

double AreaIntegral(
    double   ReceiverX,
    double   Blocker,
    unsigned Steps,
    bool     DonorSupportOnly = false)
{
    // 📝 Independently integrate ρ L H²/(π r⁴); clip the strip analytically, not with sampled visibility.
    std::array<double, 6> Boundaries{-.1, .5, -2 * Blocker - ReceiverX,
                                   2 * Blocker - ReceiverX, -2 * Blocker, 2 * Blocker};
    std::sort(Boundaries.begin(), Boundaries.end());
    double Sum = 0;
    for (unsigned Interval = 1; Interval < Boundaries.size(); ++Interval)
    {
        const double Low = std::max(-.1, Boundaries[Interval - 1]);
        const double High = std::min(.5, Boundaries[Interval]);
        if (High <= Low) continue;
        const double Centre = (Low + High) * .5;
        if (Blocker > 0 && std::abs((Centre + ReceiverX) * .5) <= Blocker) continue;
        if (DonorSupportOnly && Blocker > 0 && std::abs(Centre * .5) <= Blocker) continue;
        const double Δx = (High - Low) / Steps, Δy = 1. / Steps;
        for (unsigned X = 0; X < Steps; ++X)
            for (unsigned Y = 0; Y < Steps; ++Y)
            {
                const double Dx = Low + (X + .5) * Δx - ReceiverX;
                const double Dy = -.5 + (Y + .5) * Δy;
                const double SquaredDistance = 1 + Dx * Dx + Dy * Dy;
                Sum += .42 * Δx * Δy / (π * SquaredDistance * SquaredDistance);
            }
    }
    return Sum;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PRODUCTION RESERVOIR REUSE
//------------------------------------------------------------------------------------------------------------------------

struct ReconnectionEstimates
{
    std::array<double, 4> Estimates{};
    unsigned Present = 0;
    double IdentityError = 0;
};

ReconnectionEstimates Reconnect(
    std::mt19937_64& Random,
    double          ReceiverX,
    double          Blocker,
    double          Mixture,
    double          Floor)
{
    Frontier::Reservoir Angular, Area;
    std::array<SecondaryPoint, CandidateCount> Points;
    unsigned Present = 0;
    for (unsigned Index = 0; Index < CandidateCount; ++Index)
    {
        const auto Point = Proposal(Random, Mixture, Blocker);
        Points[Index] = Point;
        double Weight = 0;
        if (Point.Present)
        {
            ++Present;
            const auto Donor = Geometry(Point, 0, Blocker);
            const double Target = AngularContribution(Point, Donor) + Floor;
            Weight = Target * Donor.ProjectedArea / AreaDensity(Point, Mixture, Blocker);
        }
        const double Uniform = Unit(Random);
        const Frontier::ReservoirSample Sample{Index, Point.X, Point.Y};
        Angular.Update(Sample, Weight, Uniform);
        Area.Update(Sample, Weight, Uniform);
    }
    Require(Angular.SampleCount() == CandidateCount && Area.SampleCount() == CandidateCount,
            "Null candidates were removed from M");
    ReconnectionEstimates Result;
    Result.Present = Present;
    if (!Angular.HasSample()) return Result;
    Require(Angular.Sample().Candidate == Area.Sample().Candidate, "Measure encodings selected different points");
    const auto Point = Points[Angular.Sample().Candidate];
    const auto Donor = Geometry(Point, 0, Blocker);
    const auto Receiver = Geometry(Point, ReceiverX, Blocker);
    const double DonorTarget = AngularContribution(Point, Donor) + Floor;
    const double ReceiverTarget = AngularContribution(Point, Receiver) + Floor;
    const double Contribution = AngularContribution(Point, Receiver);
    const double Jacobian = Receiver.ProjectedArea / Donor.ProjectedArea;
    if (ReceiverX == 0) Require(Jacobian == 1, "Self shift changed the solid-angle measure");
    Angular.Finalize(DonorTarget);
    Area.Finalize(DonorTarget * Donor.ProjectedArea);
    for (unsigned Method = 0; Method < 4; ++Method)
    {
        Frontier::Reservoir Shifted;
        const bool AreaMeasure = Method == 1 || Method == 3;
        const double Target = ReceiverTarget * (AreaMeasure ? Receiver.ProjectedArea : 1);
        const double Factor = Method == 0 || Method == 3 ? Jacobian : 1;
        Shifted.Combine(AreaMeasure ? Area : Angular, Target, Factor, .5);
        Shifted.Finalize(Target);
        Result.Estimates[Method] = Shifted.Estimate(Contribution * (AreaMeasure ? Receiver.ProjectedArea : 1));
        Require(Shifted.SampleCount() == CandidateCount, "Combine changed represented candidate count");
        Require(std::isfinite(Result.Estimates[Method]) && Result.Estimates[Method] >= 0,
                "Invalid reconnected radiance");
    }
    Result.IdentityError = std::abs(Result.Estimates[0] - Result.Estimates[1]);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        FIXED EXPERIMENT
//------------------------------------------------------------------------------------------------------------------------

void Validate()
{
    constexpr std::array<const char*, 4> Fixtures{"self", "shift", "partial_support", "dark_donor"};
    constexpr std::array<const char*, 7> Methods{"angular", "area", "missing_j", "double_j",
                                               "no_fallback", "no_floor", "fresh8"};
    for (unsigned Fixture = 0; Fixture < Fixtures.size(); ++Fixture)
    {
        const double ReceiverX = Fixture == 0 ? 0 : 1;
        const double Blocker = Fixture < 2 ? 0 : Fixture == 2 ? .12 : .6;
        const double Reference = AreaIntegral(ReceiverX, Blocker, 2048);
        const double Coarse = AreaIntegral(ReceiverX, Blocker, 1024);
        const double Restricted = AreaIntegral(ReceiverX, Blocker, 2048, true);
        Require(std::abs(Reference - Coarse) < 1e-7, "Independent quadrature has not converged");
        std::printf("RECONNECTION_REFERENCE fixture=%s integral=%.12g refinement=%.12g donor_support=%.12g\n",
                    Fixtures[Fixture], Reference, std::abs(Reference - Coarse), Restricted);
        std::array<EstimateMoments, 7> Moments;
        std::array<double, 7> SquaredError{};
        double IdentityError = 0;
        unsigned long long Present = 0, Considered = 0;
        for (unsigned Batch = 0; Batch < BatchCount; ++Batch)
        {
            const auto Seed = 0x61b94ea3ull + Fixture * 0x1000193ull + Batch * 0x9e3779b97f4a7c15ull;
            std::mt19937_64 CorrectedRandom(Seed), UnsupportedRandom(Seed), ZeroTargetRandom(Seed);
            std::mt19937_64 FreshRandom(Seed ^ 0x7338ab19ull);
            std::array<double, 7> Sum{};
            for (unsigned Sample = 0; Sample < ReservoirCount; ++Sample)
            {
                const auto Corrected = Reconnect(CorrectedRandom, ReceiverX, Blocker, .75, .01);
                const auto Unsupported = Reconnect(UnsupportedRandom, ReceiverX, Blocker, 1, .01);
                const auto ZeroTarget = Reconnect(ZeroTargetRandom, ReceiverX, Blocker, .75, 0);
                std::array<double, 7> Estimates;
                std::copy(Corrected.Estimates.begin(), Corrected.Estimates.end(), Estimates.begin());
                Estimates[4] = Unsupported.Estimates[0];
                Estimates[5] = ZeroTarget.Estimates[0];
                Estimates[6] = 0;
                for (unsigned Draw = 0; Draw < CandidateCount; ++Draw)
                {
                    const auto Offset = Proposal(FreshRandom, 1, 0);
                    SecondaryPoint Point{Offset.X + ReceiverX, Offset.Y, Offset.Present};
                    Point.Present = std::abs(Point.X) <= .5 && std::abs(Point.Y) <= .5;
                    if (Point.Present && Geometry(Point, ReceiverX, Blocker).Visible)
                        Estimates[6] += (Point.X >= -.1 ? .42 : 0) / CandidateCount;
                }
                for (unsigned Method = 0; Method < Methods.size(); ++Method)
                {
                    Sum[Method] += Estimates[Method];
                    const double Error = Estimates[Method] - Reference;
                    SquaredError[Method] += Error * Error;
                }
                IdentityError = std::max(IdentityError, Corrected.IdentityError);
                Present += Corrected.Present;
                Considered += CandidateCount;
            }
            for (unsigned Method = 0; Method < Methods.size(); ++Method)
                Moments[Method].Accumulate(Sum[Method] / ReservoirCount);
        }
        Require(IdentityError < 1e-12, "Angular and area estimates disagree");
        Require(Present > 0 && Present < Considered, "Candidate/null support was not exercised");
        for (unsigned Method = 0; Method < Methods.size(); ++Method)
        {
            const auto& Statistics = Moments[Method];
            const bool Correct = Method < 2 || Method == 6;
            const bool Negative = ((Method == 2 || Method == 3) && Fixture == 1) ||
                                  ((Method == 4 || Method == 5) && Fixture >= 2);
            const double Error = std::abs(Statistics.Mean() - Reference);
            const bool MeanGate = Error < 4 * Statistics.StandardError() + .0001;
            const bool NegativeGate = Error > 6 * Statistics.StandardError() + .005 * Reference;
            std::printf("RECONNECTION_RESULT fixture=%s method=%s mean=%.12g nominal_se=%.12g rmse=%.12g "
                        "mean_gate=%u negative_required=%u negative_gate=%u\n",
                        Fixtures[Fixture], Methods[Method], Statistics.Mean(), Statistics.StandardError(),
                        std::sqrt(SquaredError[Method] / (BatchCount * ReservoirCount)),
                        unsigned(MeanGate), unsigned(Negative), unsigned(NegativeGate));
            Require(!Correct || MeanGate, "Corrected or fresh estimate failed independent reference");
            Require(!Negative || NegativeGate, "Negative control did not expose expected support/measure error");
            if (Method == 4 || Method == 5)
                Require(std::abs(Statistics.Mean() - Restricted) < 4 * Statistics.StandardError() + .0001,
                        "Support-limited estimate disagrees with clipped-domain reference");
        }
        std::printf("RECONNECTION_IDENTITIES fixture=%s maximum_area_angular_error=%.12g present=%llu "
                    "considered=%llu candidates=8\n", Fixtures[Fixture], IdentityError, Present, Considered);
    }
    std::printf("RECONNECTION_DECISION correctness=1 promote=0 gpu_tested=0 production_changed=0\n");
}
} // namespace

int main()
{
    try
    {
        Validate();
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "RECONNECTION_FAILURE %s\n", Error.what());
        return 1;
    }
}
