//============================================================================================================================================
//                                                   INSTANCEMOTIONSEQUENCE.CPP
//============================================================================================================================================

#include "InstanceMotionSequence.h"

#include <cmath>

namespace Frontier {
namespace ProjectZero {

namespace {

constexpr float kTau = 6.28318530717958647692f;

// InstanceRecord::World is column-major, Columns[c][r] flattened to c*4 + r, so the translation column is 12..14.
constexpr uint32_t kTranslateX = 12u;
constexpr uint32_t kTranslateY = 13u;
constexpr uint32_t kTranslateZ = 14u;

} // namespace

void InstanceMotionSequence::Construct(const std::vector<InstanceRecord>& Instances,
                                       const InstanceMotionConfiguration& Configuration) noexcept
{
    Config = Configuration;
    RestHeights.clear();

    // Clamp the span to what actually exists — a project asking for more instances than the scene has is a
    //    configuration error, not a reason to read past the end.
    const uint32_t Total = static_cast<uint32_t>(Instances.size());
    if (Config.FirstInstance >= Total) { Config.InstanceCount = 0u; return; }
    const uint32_t Available = Total - Config.FirstInstance;
    if (Config.InstanceCount > Available) Config.InstanceCount = Available;

    RestHeights.reserve(Config.InstanceCount);
    for (uint32_t I = 0u; I < Config.InstanceCount; ++I)
        RestHeights.push_back(Instances[Config.FirstInstance + I].World[kTranslateZ]);
}

float InstanceMotionSequence::QueryExpectedHeight(uint32_t Ordinal, double Elapsed) const noexcept
{
    if (Ordinal >= RestHeights.size()) return 0.0f;
    const float Phase = static_cast<float>(Ordinal) * Config.PhaseStride;
    const float Angle = kTau * Config.BobRate * static_cast<float>(Elapsed) + Phase;
    return RestHeights[Ordinal] + Config.BobAmplitude * std::sin(Angle);
}

void InstanceMotionSequence::AdvanceMotion(std::vector<InstanceRecord>& Rows, double Elapsed) const noexcept
{
    const uint32_t Driven = static_cast<uint32_t>(RestHeights.size());
    for (uint32_t I = 0u; I < Driven; ++I)
    {
        const uint32_t Slot = Config.FirstInstance + I;
        if (Slot >= Rows.size()) break;
        InstanceRecord& Row = Rows[Slot];

        // Motion vectors and ReSTIR reprojection read PreviousWorld, so it must carry LAST frame's transform.
        //    Rolling it here (rather than in the caller) keeps the two in step by construction.
        for (uint32_t E = 0u; E < 16u; ++E) Row.PreviousWorld[E] = Row.World[E];

        const float Phase = static_cast<float>(I) * Config.PhaseStride;
        const float Yaw   = Config.YawRate * static_cast<float>(Elapsed) + Phase;
        const float C = std::cos(Yaw), S = std::sin(Yaw);

        // Rotation about +Z, written into the upper 3×3 of the column-major matrix. The translation column is
        //    preserved except for Z, which follows the bob.
        Row.World[0] =  C;   Row.World[1] = S;   Row.World[2]  = 0.0f;
        Row.World[4] = -S;   Row.World[5] = C;   Row.World[6]  = 0.0f;
        Row.World[8] = 0.0f; Row.World[9] = 0.0f; Row.World[10] = 1.0f;
        Row.World[3] = Row.World[7] = Row.World[11] = 0.0f;
        Row.World[15] = 1.0f;
        Row.World[kTranslateZ] = QueryExpectedHeight(I, Elapsed);
        (void)kTranslateX; (void)kTranslateY;   // X and Y keep whatever the scene placed them at
    }
}

} // namespace ProjectZero
} // namespace Frontier
