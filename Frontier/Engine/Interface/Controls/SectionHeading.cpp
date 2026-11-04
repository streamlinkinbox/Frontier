/*==========================================================================================================================================
                                                             SECTIONHEADING.CPP
==========================================================================================================================================*/
// 🧩 Independent section typography transcribed from FogDashboard, WindInstruments, LensOptics and CelestialInfographics.

#include "Engine/Interface/Controls/SectionHeading.h"
#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SECTION HEADING
//------------------------------------------------------------------------------------------------------------------------

void SectionHeading::Construct(ControlColumn &Column, const char *Title, const char *Caption, SectionCaptionPlacement Placement)
{
    const auto &Faces  = Column.QueryFaces();
    const auto &Theme  = Column.QueryTheme();
    const float Scale  = Column.QueryScale();
    const bool  Inline = Placement == SectionCaptionPlacement::Beside;
    const float Size   = Inline ? 13.0f : Placement == SectionCaptionPlacement::Below ? 17.0f : 18.0f;
    auto        Text   = [&](const char *CaptionText, float FontSize, ImVec2 Top, ImU32 Ink, float Tracking)
    {
        (void)GlyphRun::Construct(Column, Faces.Regular, FontSize * Scale,
                                  ImVec2(Top.x, GlyphRun::ResolveBaseline(Top.y, FontSize * Scale, 1.4f)), Ink, CaptionText,
                                  Tracking * Scale);
    };
    if (Placement == SectionCaptionPlacement::Above)
    {
        Text(Caption, 9.0f, Column.Space(12.6f, 0.0f, 9.0f), Theme.Eyebrow, 1.26f);
    }
    const ImVec2 Top = Column.Space(Size * 1.4f, 0.0f, Inline ? 14.0f : Placement == SectionCaptionPlacement::Below ? 0.0f : 19.0f);
    Text(Title, Size, Top, Theme.Ink, Inline ? -0.2f : -0.3f);
    if (Inline)
    {
        const float Advance = GlyphRun::Measure(Column, Faces.Regular, 8.0f * Scale, Caption, 0.7f * Scale);
        Text(Caption, 8.0f, ImVec2(Top.x + Column.QueryWidth() - Advance, Top.y + 5.0f * Scale), Theme.Eyebrow, 0.7f);
    }
    else if (Placement == SectionCaptionPlacement::Below)
    {
        Text(Caption, 9.0f, Column.Space(12.6f, 7.0f, 12.0f), Theme.Eyebrow, 1.26f);
    }
}

} // namespace Frontier
