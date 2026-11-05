/*==========================================================================================================================================
                                                                SLIDERROW.H
==========================================================================================================================================*/
// 🧩 .co-slider: label, editable figure and unit over the accent track (ObservatorySlider.js, SliderControl.css).
//    Quantity is the stored setting; the figure shows Quantity × DisplayScale. Disabled rows are pending a native
//    field and show Tooltip.

#pragma once

#include <cstdint>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SLIDER ROW
//------------------------------------------------------------------------------------------------------------------------

class SliderRow
{
  public:
    static bool Construct(ControlColumn& Column, const char* Id, const char* Label, float& Quantity, float Minimum, float Maximum, float Step,
                          const char* Unit, float DisplayScale = 1.0f, bool Enabled = true, const char* Tooltip = nullptr);
    // 📝 The slider with .range-labels under its track: two 9 px captions, one at each end ("Still" and "Gale"). Null captions leave the block out.
    static bool ConstructCaptioned(ControlColumn& Column, const char* Id, const char* Label, float& Quantity, float Minimum, float Maximum, float Step,
                                   const char* Unit, const char* LowCaption, const char* HighCaption, float DisplayScale = 1.0f, bool Enabled = true,
                                   const char* Tooltip = nullptr);
    static bool ConstructInteger(ControlColumn& Column, const char* Id, const char* Label, uint32_t& Quantity, uint32_t Minimum,
                                 uint32_t Maximum, const char* Unit, bool Enabled = true, const char* Tooltip = nullptr);
    // 📝 A distance on a logarithmic track (InspectorControls.js Scale: 'Logarithmic'): equal travel per decade, the figure
    //    in m below a kilometre and km above, typed in the unit shown. Step 0 snaps to three significant figures.
    static bool ConstructDistance(ControlColumn& Column, const char* Id, const char* Label, double& Metres, double Minimum, double Maximum,
                                  double Step = 0.0, bool Enabled = true, const char* Tooltip = nullptr);
    static void FormatDistance(double Metres, char* Text, uint32_t Capacity) noexcept;
};

} // namespace Frontier
