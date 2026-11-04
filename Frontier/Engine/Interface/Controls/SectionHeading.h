/*==========================================================================================================================================
                                                              SECTIONHEADING.H
==========================================================================================================================================*/
// 🧩 The demo’s overline/title, title/caption and inline celestial section headings.

#pragma once

namespace Frontier
{

class ControlColumn;

enum class SectionCaptionPlacement
{
    Above,
    Below,
    Beside
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    SECTION HEADING
//------------------------------------------------------------------------------------------------------------------------

class SectionHeading
{
  public:
    static void Construct(ControlColumn          &Column,
                          const char             *Title,
                          const char             *Caption,
                          SectionCaptionPlacement Placement = SectionCaptionPlacement::Above);
};

} // namespace Frontier
