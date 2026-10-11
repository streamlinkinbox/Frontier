//============================================================================================================================================
//                                             SURFELREFERENCE.CPP
//============================================================================================================================================
#include "SurfelReference.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{
    constexpr float kEpsilon = 1.0e-6f;

    float Dot(const float A[3], const float B[3]) noexcept
    {
        return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
    }

    float Distance(const float A[3], const float B[3]) noexcept
    {
        const float X = A[0] - B[0], Y = A[1] - B[1], Z = A[2] - B[2];
        return std::sqrt(X * X + Y * Y + Z * Z);
    }

    void Normalise(float N[3]) noexcept
    {
        const float Length = std::sqrt(Dot(N, N));
        if (Length <= kEpsilon) { N[0] = 0.0f; N[1] = 1.0f; N[2] = 0.0f; return; }
        N[0] /= Length; N[1] /= Length; N[2] /= Length;
    }
}

SurfelReferenceField::SurfelReferenceField(const SurfelReferenceSettings& InitialSettings) noexcept
    : Settings(InitialSettings)
{
    if (Settings.CellSize <= kEpsilon) Settings.CellSize = 0.25f;
    if (Settings.AgeCap <= 0.0f) Settings.AgeCap = 1.0f;
}

void SurfelReferenceField::Reset() noexcept
{
    Surfels.clear();
    Grid.clear();
    FrameCount = 0u;
}

void SurfelReferenceField::AssignSettings(const SurfelReferenceSettings& NewSettings) noexcept
{
    Settings = NewSettings;
    if (Settings.CellSize <= kEpsilon) Settings.CellSize = 0.25f;
    if (Settings.AgeCap <= 0.0f) Settings.AgeCap = 1.0f;
    RebuildGrid();
}

uint32_t SurfelReferenceField::HashCell(int32_t X, int32_t Y, int32_t Z) const noexcept
{
    // This is intentionally the same integer hash as SurfelGIStage and the compute shaders. The map retains a
    // collision chain, just as the GPU's head/next buffer does; geometric tests below reject unrelated cells.
    return static_cast<uint32_t>(X + 1024) * 73856093u
         ^ static_cast<uint32_t>(Y + 1024) * 19349663u
         ^ static_cast<uint32_t>(Z + 1024) * 83492791u;
}

void SurfelReferenceField::CellOf(const float Position[3], int32_t& X, int32_t& Y, int32_t& Z) const noexcept
{
    const float InverseCell = 1.0f / Settings.CellSize;
    X = static_cast<int32_t>(std::floor((Position[0] - Settings.GridOrigin[0]) * InverseCell));
    Y = static_cast<int32_t>(std::floor((Position[1] - Settings.GridOrigin[1]) * InverseCell));
    Z = static_cast<int32_t>(std::floor((Position[2] - Settings.GridOrigin[2]) * InverseCell));
}

void SurfelReferenceField::RebuildGrid() noexcept
{
    Grid.clear();
    Grid.reserve(Surfels.size());
    for (uint32_t Index = 0u; Index < Surfels.size(); ++Index)
    {
        int32_t X = 0, Y = 0, Z = 0;
        CellOf(Surfels[Index].Sample.Position, X, Y, Z);
        Grid[HashCell(X, Y, Z)].push_back(Index);
    }
}

bool SurfelReferenceField::IsCovered(const SurfelReferenceSample& Candidate) const noexcept
{
    int32_t X = 0, Y = 0, Z = 0;
    CellOf(Candidate.Position, X, Y, Z);
    // A surface can straddle a cell boundary; match the shared gather's 3x3x3 query rather than relying on a
    // single bucket. Hash collisions are harmless because the normal/distance tests remain authoritative.
    for (int32_t OffsetZ = -1; OffsetZ <= 1; ++OffsetZ)
    for (int32_t OffsetY = -1; OffsetY <= 1; ++OffsetY)
    for (int32_t OffsetX = -1; OffsetX <= 1; ++OffsetX)
    {
        const auto It = Grid.find(HashCell(X + OffsetX, Y + OffsetY, Z + OffsetZ));
        if (It == Grid.end()) continue;
        for (uint32_t Index : It->second)
        {
            const Surfel& Existing = Surfels[Index];
            if (Distance(Existing.Sample.Position, Candidate.Position) >= Settings.CellSize) continue;
            if (Dot(Existing.Sample.Normal, Candidate.Normal) > 0.7f) return true;
        }
    }
    return false;
}

void SurfelReferenceField::Seed(const std::vector<SurfelReferenceSample>& Samples) noexcept
{
    bool Changed = false;
    for (const SurfelReferenceSample& Candidate : Samples)
    {
        if (Surfels.size() >= Settings.MaximumSurfels || IsCovered(Candidate)) continue;
        Surfel NewSurfel{};
        NewSurfel.Sample = Candidate;
        Normalise(NewSurfel.Sample.Normal);
        Surfels.push_back(NewSurfel);
        // Make the new sample visible to subsequent candidates in this Seed call. The complete rebuild below keeps
        // cells stable after the batch and avoids any dependence on candidate ordering across calls.
        int32_t X = 0, Y = 0, Z = 0;
        CellOf(NewSurfel.Sample.Position, X, Y, Z);
        Grid[HashCell(X, Y, Z)].push_back(static_cast<uint32_t>(Surfels.size() - 1u));
        Changed = true;
    }
    if (Changed) RebuildGrid();
}

void SurfelReferenceField::Step(SurfelReferenceMeasure Measure, void* UserContext) noexcept
{
    if (Measure == nullptr || Surfels.empty()) return;
    ++FrameCount;
    for (uint32_t Index = 0u; Index < Surfels.size(); ++Index)
    {
        Surfel& Current = Surfels[Index];
        float Measured[3]{};
        Measure(Current.Sample, Index, UserContext, Measured);
        const float Alpha = 1.0f / std::min(Current.Age + 1.0f, Settings.AgeCap);
        for (uint32_t Channel = 0u; Channel < 3u; ++Channel)
            Current.PendingIrradiance[Channel] = Current.Irradiance[Channel]
                                             + (Measured[Channel] - Current.Irradiance[Channel]) * Alpha;
    }
    // Deferred commit is the CPU Jacobi boundary: each Measure above saw exactly the prior field.
    for (Surfel& Current : Surfels)
    {
        Current.Irradiance[0] = Current.PendingIrradiance[0];
        Current.Irradiance[1] = Current.PendingIrradiance[1];
        Current.Irradiance[2] = Current.PendingIrradiance[2];
        Current.Age += 1.0f;
    }
}

void SurfelReferenceField::Gather(const float Position[3], const float Normal[3], float OutIrradiance[3]) const noexcept
{
    OutIrradiance[0] = OutIrradiance[1] = OutIrradiance[2] = 0.0f;
    if (Surfels.empty()) return;

    float QueryNormal[3] = { Normal[0], Normal[1], Normal[2] };
    Normalise(QueryNormal);
    int32_t X = 0, Y = 0, Z = 0;
    CellOf(Position, X, Y, Z);

    float WeightSum = 0.0f;
    for (int32_t OffsetZ = -1; OffsetZ <= 1; ++OffsetZ)
    for (int32_t OffsetY = -1; OffsetY <= 1; ++OffsetY)
    for (int32_t OffsetX = -1; OffsetX <= 1; ++OffsetX)
    {
        const auto It = Grid.find(HashCell(X + OffsetX, Y + OffsetY, Z + OffsetZ));
        if (It == Grid.end()) continue;
        for (uint32_t Index : It->second)
        {
            const Surfel& Source = Surfels[Index];
            const float DistanceToSource = Distance(Position, Source.Sample.Position);
            if (DistanceToSource >= Settings.CellSize) continue;
            const float NormalWeight = Dot(QueryNormal, Source.Sample.Normal);
            if (NormalWeight <= 0.0f) continue;
            const float Delta[3] = { Position[0] - Source.Sample.Position[0], Position[1] - Source.Sample.Position[1], Position[2] - Source.Sample.Position[2] };
            if (std::fabs(Dot(Delta, Source.Sample.Normal)) >= Settings.CellSize * 0.5f) continue;
            float DistanceWeight = 1.0f - DistanceToSource / Settings.CellSize;
            DistanceWeight *= DistanceWeight;
            const float Weight = NormalWeight * DistanceWeight;
            OutIrradiance[0] += Source.Irradiance[0] * Weight;
            OutIrradiance[1] += Source.Irradiance[1] * Weight;
            OutIrradiance[2] += Source.Irradiance[2] * Weight;
            WeightSum += Weight;
        }
    }
    if (WeightSum > kEpsilon)
    {
        OutIrradiance[0] /= WeightSum;
        OutIrradiance[1] /= WeightSum;
        OutIrradiance[2] /= WeightSum;
    }
    else
        OutIrradiance[0] = OutIrradiance[1] = OutIrradiance[2] = 0.0f;
}
} // namespace Frontier
