//============================================================================================================================================
//                                             SURFELREFERENCE.H
//============================================================================================================================================
// 📦 Deterministic, Vulkan-free reference for the shared Surfel-GI field.
//
// This is engine code, rather than a per-project executable, so every project can use the same CPU oracle
// when validating the GPU SurfelGIStage. The owner supplies scene-specific ray/lighting evaluation through the small
// callback; this class owns the renderer-independent parts that must match on every project: persistent placement,
// the spatial hash, Jacobi-style running mean, and irradiance gathering.
//============================================================================================================================================

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Frontier
{
    struct SurfelReferenceSample
    {
        float Position[3]{};
        float Normal[3]{};
        float Albedo[3]{};
    };

    struct SurfelReferenceSettings
    {
        float    GridOrigin[3] = { 0.0f, 0.0f, 0.0f };
        float    CellSize      = 0.25f;
        float    AgeCap        = 64.0f;
        uint32_t MaximumSurfels = 262144u;
    };

    // The measurement is irradiance at the surfel, before its temporal running mean. A project may implement it with
    // its deterministic CPU CWBVH/BVH, direct-light oracle, and previous-field Gather call. It is deliberately a
    // C-layout callback rather than a project type or std::function so the shared reference has no project ownership.
    using SurfelReferenceMeasure = void (*)(const SurfelReferenceSample& Sample, uint32_t Index,
                                            void* UserContext, float OutIrradiance[3]) noexcept;

    class SurfelReferenceField
    {
    public:
        explicit SurfelReferenceField(const SurfelReferenceSettings& InitialSettings = {}) noexcept;

        void Reset() noexcept;
        void AssignSettings(const SurfelReferenceSettings& NewSettings) noexcept;

        // Adds persistent samples only where the existing world-space field does not already cover the same surface.
        // Calling this once per frame with the same visible samples is intentionally idempotent.
        void Seed(const std::vector<SurfelReferenceSample>& Samples) noexcept;

        // All measurements observe the previous irradiance state; the commit happens only after every callback
        // returned. This is the CPU equivalent of SurfelIrradianceUpdate + SurfelCommit's Jacobi boundary.
        void Step(SurfelReferenceMeasure Measure, void* UserContext = nullptr) noexcept;

        // Irradiance gathered from the current persistent field with the same 3x3x3 hash neighbourhood, normal and
        // tangent-plane gates that the GPU resolve uses. Returns black when no matching surfel covers the query.
        void Gather(const float Position[3], const float Normal[3], float OutIrradiance[3]) const noexcept;

        [[nodiscard]] const SurfelReferenceSettings& QuerySettings() const noexcept { return Settings; }
        [[nodiscard]] uint32_t QuerySurfelCount() const noexcept { return static_cast<uint32_t>(Surfels.size()); }
        [[nodiscard]] uint64_t QueryFrameCount() const noexcept { return FrameCount; }

    private:
        struct Surfel
        {
            SurfelReferenceSample Sample{};
            float Irradiance[3]{};
            float PendingIrradiance[3]{};
            float Age = 0.0f;
        };

        [[nodiscard]] uint32_t HashCell(int32_t X, int32_t Y, int32_t Z) const noexcept;
        void CellOf(const float Position[3], int32_t& X, int32_t& Y, int32_t& Z) const noexcept;
        void RebuildGrid() noexcept;
        [[nodiscard]] bool IsCovered(const SurfelReferenceSample& Candidate) const noexcept;

        SurfelReferenceSettings Settings{};
        std::vector<Surfel> Surfels;
        std::unordered_map<uint32_t, std::vector<uint32_t>> Grid;
        uint64_t FrameCount = 0u;
    };
}
