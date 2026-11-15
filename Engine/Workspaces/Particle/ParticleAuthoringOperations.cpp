/*============================================================================================================================================
                                                     PARTICLEAUTHORINGOPERATIONS.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleAuthoringOperations encoding lossless .ParticleSpace snapshots into RevisionTracker

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleAuthoringOperations.h"

#include "Engine/Codecs/ParticleCodec.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Frontier::Workspaces
{

bool ParticleAuthoringOperations::RecordDocumentSnapshot(
    RevisionTracker&                       Revision,
    const ParticleSpace::ParticleDocument& Document,
    std::string_view                       OperationTag,
    uint32_t                               TargetRigId,
    uint64_t                               SequenceStamp)
{
    const Codecs::SpaceContainerPayload Payload = Codecs::ParticleEncoder::BuildPayload(Document);
    std::vector<std::byte> BinaryBytes;
    std::string            Diag;
    if (!Codecs::SpaceContainerEncoder::EncodeToBytes(Payload, BinaryBytes, Diag))
    {
        return false;
    }

    ParametricOperationParameters Params{};
    Params.Category      = ParametricOperationCategory::StructuredPayloadDelta;
    Params.TargetChannel = TargetRigId;
    const std::size_t CopyLen = std::min<std::size_t>(OperationTag.size(), sizeof(Params.OperationTag) - 1u);
    if (CopyLen > 0u)
    {
        std::memcpy(Params.OperationTag, OperationTag.data(), CopyLen);
    }

    return Revision.RecordParametricOperation(Params, SequenceStamp, BinaryBytes);
}

bool ParticleAuthoringOperations::ReconstructDocumentAtCursor(
    const RevisionTracker&           Revision,
    ParticleSpace::ParticleDocument& OutDocument)
{
    std::vector<uint32_t> Chain;
    Revision.InspectHistory().ExtractActiveRootToCursorChain(Chain);
    const auto HistoryEntries = Revision.InspectHistory().InspectEntries();
    const auto RecordEntries  = Revision.InspectRecorder().InspectRecordEntries();
    const auto& Region        = Revision.InspectRecorder().InspectContentRegion();

    for (auto It = Chain.rbegin(); It != Chain.rend(); ++It)
    {
        const uint32_t StepIdx = *It;
        if (StepIdx >= HistoryEntries.size())
        {
            continue;
        }
        const uint32_t RecIdx = HistoryEntries[StepIdx].RecordEntryIndex;
        if (RecIdx >= RecordEntries.size())
        {
            continue;
        }
        const ParametricRecordEntry& Rec = RecordEntries[RecIdx];
        if (Rec.Parameters.Category == ParametricOperationCategory::StructuredPayloadDelta &&
            Rec.RegionByteLength > 0u)
        {
            const std::span<const std::byte> Packet = Region.InspectSpan(Rec.RegionByteOffset, Rec.RegionByteLength);
            Codecs::SpaceContainerPayload Payload{};
            std::string Diag;
            if (!Codecs::SpaceContainerDecoder::DecodeFromBytes(Packet, Payload, Diag))
            {
                return false;
            }
            return Codecs::ParticleDecoder::ExtractDocument(Payload, OutDocument, Diag);
        }
    }
    return false;
}

} // namespace Frontier::Workspaces
