/*==========================================================================================================================================
                                                            FRACTURESECTION.CPP
==========================================================================================================================================*/
// 🧩 The geometry inspector's fracture card (the Project Zero editor's FracturePanel): the per-object fracture switch, its
//    Voronoi settings and the way into the Fracture editor.

#include "Engine/Interface/Inspectors/Geometry/GeometrySections.h"

#include "Engine/Interface/Controls/CardTitleRow.h"
#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRACTURE
//------------------------------------------------------------------------------------------------------------------------

bool GeometryFractureSection::Construct(ControlColumn& Page, GeometryConfiguration& Parameters, bool& OpenEditor)
{
    const auto&            Resolved = Parameters.Resolved;
    FractureConfiguration& Fracture = Parameters.Fracture;
    Page.BeginCard(ResolveGenericCardLook(), nullptr, nullptr);
    CardTitleLook Title;
    Title.TitleSize     = 15.0f;
    Title.Caption       = "PER-OBJECT GEOMETRY";
    Title.CaptionBeside = false;
    Title.ActionGlyph   = "ArrowUpRight";
    Title.ActionTooltip = Fracture.Enabled ? "Open the Fracture editor" : "Enable fracture to open the Fracture editor";
    Title.ActionEnabled = Fracture.Enabled;
    OpenEditor = CardTitleRow::Construct(Page, "FractureTitle", "Fracture", Title);
    bool Changed = false;
    bool Enabled = Fracture.Enabled;
    if (SwitchRow::Construct(Page, "EnableFracture", "Enable fracture", Enabled))
    {
        Fracture.Enabled = Enabled;
        Changed          = true;
    }
    if (Fracture.Enabled)
    {
        Changed |= SliderRow::ConstructInteger(Page, "Cells", "Voronoi cells", Fracture.VoronoiCellCount, 4u, 256u, "cells", !Parameters.Locked);
        Changed |= SliderRow::Construct(Page, "Impulse", "Impulse threshold", Fracture.ImpulseThresholdNs, 1.0f, 500.0f, 0.5f, "N\xC2\xB7s", 1.0f,
                                        !Parameters.Locked);
    }
    char Note[260];
    if (Resolved.Breakable)
        std::snprintf(Note, sizeof(Note), "A smash-alley prop: the scene archive holds a pool of %u fragments for each copy. Breaking it needs the physics "
                                          "(Jolt), which comes next; the settings are saved with the level.", Resolved.FragmentCapacity);
    else if (Resolved.Shape)
        std::snprintf(Note, sizeof(Note), "An engine shape has no fragment pool yet: the fracture settings are kept with the level for when the physics "
                                          "arrives.");
    else
        std::snprintf(Note, sizeof(Note), "Not breakable in the scene archive: the fracture settings are kept with the level for when the physics arrives.");
    CaptionText::Construct(Page, Note, 4.0f, 0.0f);
    Page.EndCard();
    if (Changed) Fracture.AdvanceRevision();
    return Changed;
}

} // namespace Frontier
