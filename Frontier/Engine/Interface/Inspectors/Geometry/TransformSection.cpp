/*==========================================================================================================================================
                                                           TRANSFORMSECTION.CPP
==========================================================================================================================================*/
// 🧩 The geometry inspector's transform card (the Project Zero editor's TransformPanel): position, rotation and scale of the
//    object, with resets to where the scene archive placed it.

#include "Engine/Interface/Inspectors/Geometry/GeometrySections.h"

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/Controls/CardTitleRow.h"
#include "Engine/Interface/Controls/TransformTable.h"
#include "Engine/Interface/InspectorControls.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARD LOOKS
//------------------------------------------------------------------------------------------------------------------------

CardLook ResolveGenericCardLook()
{
    CardLook Look;
    Look.Fill          = ResolveCssColour(0x1A1A1AFF);
    Look.Border        = ResolveCssColour(0xFFFFFF0D);
    Look.Radius        = 22.0f;
    Look.PaddingTop    = 18.0f;
    Look.PaddingSide   = 14.0f;
    Look.PaddingBottom = 18.0f;
    Look.MarginBottom  = 12.0f;
    return Look;
}

ImU32 ResolveCautionInk()
{
    return ResolveCssColour(0xF59E0BFF);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSFORM
//------------------------------------------------------------------------------------------------------------------------

bool GeometryTransformSection::Construct(ControlColumn& Page, GeometryConfiguration& Parameters)
{
    Page.BeginCard(ResolveGenericCardLook(), nullptr, nullptr);
    CardTitleLook Title;
    Title.Caption      = "SCENE SPACE \xC2\xB7 Z UP";
    Title.MarginBottom = 0.0f;
    (void)CardTitleRow::Construct(Page, "TransformTitle", "Transform", Title);
    SpatialConfiguration&   Spatial = Parameters.Spatial;
    const TransformTableRow Rows[3] = {
        {"Position", "m", Spatial.PositionMetres, -100000.0f, 100000.0f, 0.01f, "%.2f",
         {Parameters.ArchiveOriginMetres[0], Parameters.ArchiveOriginMetres[1], Parameters.ArchiveOriginMetres[2]}},
        {"Rotation", "deg", Spatial.OrientationDegrees, -36000.0f, 36000.0f, 0.1f, "%.1f", {0.0f, 0.0f, Parameters.ArchiveYawDegrees}},
        {"Scale", "\xC3\x97", Spatial.Scale, 0.001f, 1000.0f, 0.01f, "%.3f", {1.0f, 1.0f, 1.0f}},
    };
    const bool Changed = TransformTable::Construct(Page, "Transform", Rows, 3, !Parameters.Locked);
    char        Note[240];
    const char* Origin = Parameters.ClaimsArchive && Parameters.ShapeName.empty() ? "where the scene archive placed it" : "where it was constructed";
    if (Parameters.Locked)
        std::snprintf(Note, sizeof(Note), "Object locked. Unlock it in Quick controls to edit.");
    else if (Parameters.Resolved.Shown > 1)
        std::snprintf(Note, sizeof(Note), "Drag to scrub, double-click to type \xC2\xB7 Alt for precision, Shift for speed. All %u copies move with the object; "
                                          "the resets return it to %s. In the viewport: W move, E rotate, R scale.", Parameters.Resolved.Shown, Origin);
    else
        std::snprintf(Note, sizeof(Note), "Drag to scrub, double-click to type \xC2\xB7 Alt for precision, Shift for speed. The resets return the object to "
                                          "%s. In the viewport: W move, E rotate, R scale.", Origin);
    CaptionText::Construct(Page, Note, 0.0f, 0.0f);
    Page.EndCard();
    if (Changed) Spatial.AdvanceRevision();
    return Changed;
}

} // namespace Frontier
