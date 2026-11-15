/*============================================================================================================================================
                                                      PARTICLEAUTHORINGOPERATIONS.H
============================================================================================================================================*/
// 🧩 Parametric reversible authoring operations recorded into RevisionTracker for ParticleWorkspace undo/redo

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/Workspaces/Revision/RevisionTracker.h"

#include <cstdint>
#include <string_view>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ParticleAuthoringOperations
{
public:
    static bool RecordDocumentSnapshot(
        RevisionTracker&                      Revision,
        const ParticleSpace::ParticleDocument& Document,
        std::string_view                      OperationTag,
        uint32_t                              TargetRigId = 0u,
        uint64_t                              SequenceStamp = 0ull);

    [[nodiscard]] static bool ReconstructDocumentAtCursor(
        const RevisionTracker&                Revision,
        ParticleSpace::ParticleDocument&      OutDocument);
};

} // namespace Frontier::Workspaces
