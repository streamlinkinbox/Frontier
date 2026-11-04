/*==========================================================================================================================================
                                                           OCCUPANTRELOCATION.H
==========================================================================================================================================*/
// 🧩 Editor placements of the scene's occupants, beyond the demo: the level's geometry objects move, hide and re-lay the
//    proving ground's static occupants between refreshes, and every step that reads an occupant follows.

#pragma once

#include "SceneStructure.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Frontier::GlobalIllumination
{

class IlluminationHost;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One occupant as the editor places it: its world transform, and whether it takes part (drawn, traced, unioned, lit). The
//    optional fields leave the occupant as it is when unset.
struct OccupantPlacement
{
    uint32_t  Occupant = 0;
    Transform Current{};
    bool      Active   = true;
    std::optional<uint32_t>      Participation{};      // [-] OccupantParticipation mask (Dynamic, ShadowExempt, IndirectExempt)
    std::optional<int32_t>       MaterialOverride{};   // [-] −1 (per-facet families), a family, or MaterialBlockBit | block base
    std::optional<OccupantEntry> Template{};           // [-] a reserve occupant first takes this occupant's shape and surface
    bool                         Recapture = false;    // [-] its surface changed: its cards are captured again
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RELOCATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Applies placements between refreshes and returns how many occupants changed. A moved occupant keeps the transform it had
//    at the last refresh as its previous one, so the next refresh carries its motion (the refresh after settles it); a changed
//    activity rebuilds the draw runs and the card tile lists; the distance field is invalidated over the old and the new
//    bounds; the moved occupants' cards relight first; the lamps follow their heads and lanterns. A template reassigns a reserve
//    occupant first; a changed participation rebuilds the shadow runs (ShadowExempt), the tile lists (Dynamic) or the field
//    (IndirectExempt); a changed surface captures the cards again.
uint32_t RelocateOccupants(IlluminationHost& Host, const std::vector<OccupantPlacement>& Placements);

} // namespace Frontier::GlobalIllumination
