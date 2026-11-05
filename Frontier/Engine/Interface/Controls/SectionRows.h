/*==========================================================================================================================================
                                                              SECTIONROWS.H
==========================================================================================================================================*/
// 🧩 The rows a tab-switched inspector section is built from (Render settings, Force Field, Wind, Force source): a section
//    card with its glyph, title and status, sub-headings, switches, counts, amounts, logarithmic distances and bearings.

#pragma once

#include <cstdint>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SECTION ROWS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The page's rows over the shared controls: an Unavailable reason fades the row and becomes its tooltip.
namespace SectionRows
{
void BeginSection(ControlColumn& Page, const char* Title, const char* Glyph, const char* Status, const char* Caption);
void EndSection(ControlColumn& Page);
void SubHeading(ControlColumn& Page, const char* Title);
bool Switch(ControlColumn& Page, const char* Id, const char* Label, bool& Setting, const char* Unavailable = nullptr, const char* Tooltip = nullptr);
bool Count(ControlColumn& Page, const char* Id, const char* Label, int& Setting, int Minimum, int Maximum, const char* Unit,
           const char* Unavailable = nullptr, const char* Tooltip = nullptr);
bool Amount(ControlColumn& Page, const char* Id, const char* Label, float& Setting, float Minimum, float Maximum, float Step, const char* Unit,
            const char* Unavailable = nullptr, const char* Tooltip = nullptr);
// 📝 A double setting on the linear slider (the force records keep doubles).
bool Measure(ControlColumn& Page, const char* Id, const char* Label, double& Setting, double Minimum, double Maximum, double Step,
             const char* Unit, const char* Unavailable = nullptr, const char* Tooltip = nullptr);
// 📝 The same, with captions at the two ends of the track (the prototype's range labels, "Slow motion" and "Fast").
bool MeasureCaptioned(ControlColumn& Page, const char* Id, const char* Label, double& Setting, double Minimum, double Maximum, double Step,
                      const char* Unit, const char* LowCaption, const char* HighCaption, const char* Unavailable = nullptr, const char* Tooltip = nullptr);
// 📝 A distance on a logarithmic track, read in m below a kilometre and in km above (2 m to 40 km on one slider).
bool Distance(ControlColumn& Page, const char* Id, const char* Label, double& Metres, double Minimum, double Maximum, double Step = 0.0,
              const char* Unavailable = nullptr, const char* Tooltip = nullptr);
// 📝 A compass bearing of travel, 0° north and 90° east, with its 16-point name beside the figure.
bool Bearing(ControlColumn& Page, const char* Id, const char* Label, double& Degrees, const char* Unavailable = nullptr);
// 📝 The 16-point compass name of a bearing ("N", "NNE", … "NNW").
[[nodiscard]] const char* ResolveCompassPoint(double Degrees) noexcept;
// 📝 A distance as the rows show it: "850 m", "2.6 km".
void FormatDistance(double Metres, char* Text, uint32_t Capacity) noexcept;
} // namespace SectionRows

} // namespace Frontier
