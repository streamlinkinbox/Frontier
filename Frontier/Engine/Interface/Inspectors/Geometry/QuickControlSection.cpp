/*==========================================================================================================================================
                                                          QUICKCONTROLSECTION.CPP
==========================================================================================================================================*/
// 🧩 The geometry inspector's quick controls (the Project Zero editor's capabilities card): Visible, Dynamic, Cast shadows,
//    GI, Physics and Locked.

#include "Engine/Interface/Inspectors/Geometry/GeometrySections.h"

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/Controls/QuickTiles.h"
#include "Engine/Interface/InspectorControls.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    QUICK CONTROLS
//------------------------------------------------------------------------------------------------------------------------

bool GeometryQuickSection::Construct(ControlColumn& Page, GeometryConfiguration& Parameters)
{
    const float S = Page.QueryScale();
    Page.BeginCard(ResolveGenericCardLook(), nullptr, nullptr);
    // 📝 h4 QUICK CONTROLS: 10 px, tracking 1.4 px, #909090.
    const ImVec2 Heading = Page.Space(14.0f, 0.0f, 12.0f);
    (void)GlyphRun::Construct(Page, Page.QueryFaces().Regular, 10.0f * S, ImVec2(Heading.x, GlyphRun::ResolveBaseline(Heading.y, 10.0f * S, 1.4f)),
                              ResolveCssColour(0x909090FF), "QUICK CONTROLS", 1.4f * S);
    const bool Dynamic = Parameters.Dynamic, Physics = Parameters.Physics;
    const QuickTile Tiles[6] = {
        {"Visible", "Eye", &Parameters.Visible, "Drawn, traced and unioned into the distance field; off hides every copy"},
        {"Dynamic", "Move", &Parameters.Dynamic, "Relit every refresh (its surface cards) instead of in turn: for objects that move"},
        {"Cast shadows", "CastShadow", &Parameters.CastShadows, "Off: left out of the shadow depth and of every shadow ray"},
        {"GI", "Sparkles", &Parameters.GlobalIllumination, "Off: left out of the distance field and the traced rays (no bounce light)"},
        {"Physics", "Atom", &Parameters.Physics, "Authored: a rigid body once the physics (Jolt) runs"},
        {"Locked", "Lock", &Parameters.Locked, "Refuses transform and modifier edits"},
    };
    bool Changed = QuickTiles::Construct(Page, "QuickControls", Tiles, 6, Page.QueryContainerWidth() < 300.0f ? 2 : 3);
    // 📝 The reference pairs them: physics needs a dynamic body, and a static body drops its physics.
    if (Parameters.Physics && !Physics) Parameters.Dynamic = true;
    if (!Parameters.Dynamic && Dynamic) Parameters.Physics = false;
    char Note[240];
    std::snprintf(Note, sizeof(Note), "%s object \xC2\xB7 %s. Visible, Dynamic, Cast shadows, GI and Locked act on the viewport now; Physics is "
                                      "authored until the physics (Jolt) arrives.",
                  Parameters.Dynamic ? "Dynamic" : "Static", Parameters.Physics ? "Physics requested" : "Physics off");
    CaptionText::Construct(Page, Note, 0.0f, 0.0f);
    Page.EndCard();
    return Changed;
}

} // namespace Frontier
