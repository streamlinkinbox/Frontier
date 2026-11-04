/*==========================================================================================================================================
                                                          OCCUPANTRELOCATION.CPP
==========================================================================================================================================*/
// 🧩 Editor placements of the scene's occupants, beyond the demo: the level's geometry objects move, hide and re-lay the
//    proving ground's static occupants between refreshes, and every step that reads an occupant follows.

#include "OccupantRelocation.h"

#include "IlluminationHost.h"

#include <algorithm>

namespace Frontier::GlobalIllumination
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    RELOCATION
//------------------------------------------------------------------------------------------------------------------------

uint32_t RelocateOccupants(IlluminationHost& Host, const std::vector<OccupantPlacement>& Placements)
{
    SceneStructure&          Scene = Host.Scene;
    std::vector<WorldBounds> Invalidated;
    std::vector<uint32_t>    Moved;
    std::vector<uint32_t>    Recaptured;
    bool                     ActivityChanged = false;
    bool                     ShadowsChanged  = false;
    bool                     TilesChanged    = false;
    bool                     LampsChanged    = false;
    uint32_t                 Changed         = 0;
    auto MarkRelocated = [&Host](uint32_t Index) {
        if (std::find(Host.RelocatedOccupants.begin(), Host.RelocatedOccupants.end(), Index) == Host.RelocatedOccupants.end())
            Host.RelocatedOccupants.push_back(Index);
    };
    for (const OccupantPlacement& Placement : Placements)
    {
        if (Placement.Occupant >= Scene.Occupants.size()) continue;
        OccupantEntry& Occupant = Scene.Occupants[Placement.Occupant];
        // 📝 A reserve occupant takes its template's shape while it is still hidden, so nothing it was drawn as lingers.
        bool Reassigned = false;
        if (Placement.Template && Placement.Template->FacetSet != Occupant.FacetSet)
        {
            if (!Occupant.Inactive && (Occupant.Participation & OccupantParticipation::Distance))
                Invalidated.push_back(Scene.QueryWorldBounds(Placement.Occupant));
            Reassigned = Host.AssignReserveOccupant(Placement.Occupant, *Placement.Template);
            ActivityChanged = ActivityChanged || Reassigned;
        }
        const bool Inactive        = !Placement.Active;
        const bool Relocated       = Occupant.Current != Placement.Current;
        const bool Participating   = Placement.Participation && *Placement.Participation != Occupant.Participation;
        const bool Resurfaced      = Placement.MaterialOverride && *Placement.MaterialOverride != Occupant.MaterialOverride;
        if (!Reassigned && !Relocated && Occupant.Inactive == Inactive && !Participating && !Resurfaced && !Placement.Recapture) continue;
        ++Changed;
        // 📝 The distance field holds the occupant where it was unioned: both the old and the new bounds are rebuilt.
        const bool Unioned = (Occupant.Participation & OccupantParticipation::Distance) != 0;
        if (Unioned && !Occupant.Inactive) Invalidated.push_back(Scene.QueryWorldBounds(Placement.Occupant));
        if (Relocated)
        {
            const bool Pending = std::find(Host.RelocatedOccupants.begin(), Host.RelocatedOccupants.end(), Placement.Occupant) !=
                                 Host.RelocatedOccupants.end();
            if (!Pending)
            {
                Occupant.Previous = Occupant.Current;
                Host.RelocatedOccupants.push_back(Placement.Occupant);
            }
            Occupant.Current = Placement.Current;
            if (Occupant.UnionTransform) Occupant.UnionTransform = Placement.Current;
            Moved.push_back(Placement.Occupant);
        }
        if (Occupant.Inactive != Inactive)
        {
            Occupant.Inactive = Inactive;
            ActivityChanged   = true;
            MarkRelocated(Placement.Occupant);
            // 📝 An occupant shown again was idle: it arrives where it is placed, without a motion trail.
            if (!Inactive) Occupant.Previous = Occupant.Current;
        }
        if (Participating)
        {
            const uint32_t Toggled = *Placement.Participation ^ Occupant.Participation;
            Occupant.Participation = *Placement.Participation;
            ShadowsChanged = ShadowsChanged || (Toggled & OccupantParticipation::ShadowExempt) != 0;
            TilesChanged   = TilesChanged || (Toggled & OccupantParticipation::Dynamic) != 0;
            MarkRelocated(Placement.Occupant);
        }
        if (Resurfaced)
        {
            Occupant.MaterialOverride = *Placement.MaterialOverride;
            MarkRelocated(Placement.Occupant);
        }
        if ((Resurfaced || Placement.Recapture) && !Reassigned) Recaptured.push_back(Placement.Occupant);
        const bool UnionedNow = (Occupant.Participation & OccupantParticipation::Distance) != 0;
        if (UnionedNow && !Occupant.Inactive) Invalidated.push_back(Scene.QueryWorldBounds(Placement.Occupant));
        LampsChanged = LampsChanged || Occupant.Name.rfind("Light pole head ", 0) == 0 || Occupant.Name.rfind("Point lamp lantern ", 0) == 0;
    }
    if (Changed == 0) return 0;
    if (ActivityChanged || ShadowsChanged) Scene.ConstructDrawRuns();
    if ((ActivityChanged || TilesChanged) && Host.Cards) Host.Cards->RebuildTileLists(Scene);
    if (Host.Distance && !Invalidated.empty()) Host.Distance->InvalidateBounds(Invalidated);
    if (Host.Cards && !Moved.empty()) Host.Cards->WarmOccupants(Scene, Moved, 3);
    if (!Recaptured.empty()) Host.RecaptureOccupants(Recaptured);
    if (LampsChanged) Host.ResolveLampPlacements();
    return Changed;
}

} // namespace Frontier::GlobalIllumination
