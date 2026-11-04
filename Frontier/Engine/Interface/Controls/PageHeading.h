/*==========================================================================================================================================
                                                               PAGEHEADING.H
==========================================================================================================================================*/
// 🧩 .co-header: the eyebrow, the h1 title with its title-line pill (Reset ↗ / ON) and the subtitle.

#pragma once

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PAGE HEADING
//------------------------------------------------------------------------------------------------------------------------

class PageHeading
{
  public:
    // 📝 True when the pill was pressed; a null ActionLabel leaves the pill out.
    static bool Construct(ControlColumn &Column,
                          const char    *Id,
                          const char    *Eyebrow,
                          const char    *Title,
                          const char    *ActionLabel,
                          const char    *Subtitle,
                          const char    *ActionTooltip = nullptr);
};

} // namespace Frontier
