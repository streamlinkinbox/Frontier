/*==========================================================================================================================================
                                                          FORCEFIELDSECTIONS.H
==========================================================================================================================================*/
// 🧩 The Force Field page's seven sections (View, Range, Forces, Receivers, Timeline, Vectors, Budget) and the Wind page's five
//    (Flow, Gusts, Altitude, Eddies, Mesoscale), one class per file, after the approved prototype (Experimental/ForceFieldEditor);
//    their rows: Controls/SectionRows.h, their charts: ForceFieldPieces.h.

#pragma once

#include "Engine/Interface/Controls/SectionRows.h"
#include "Engine/Interface/EnvironmentEditAccess.h"
#include "Engine/PhysicalDynamics/ForceSourceCatalogue.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

class ControlColumn;
class ForceFieldVisualization;

//------------------------------------------------------------------------------------------------------------------------
//                                                    READINGS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the Force Field sections read, resolved once per page refresh: the level's forces (copies, in the Outliner's order),
//    their budget, the count of each contribution and the Field view's readings. Without a level (the page captures) the
//    list is empty and the readings unmeasured.
struct ForceFieldPageReading
{
    const EnvironmentEditAccess*          Access = nullptr;
    std::vector<ForceSourceConfiguration> Sources;
    std::vector<std::string>              Names;
    std::vector<uint64_t>                 Tokens;
    ForceFieldBudget                      Budget{};
    std::array<uint32_t, 3>               Counts{};   // [-] flow, accelerate, damp
    double                                TimelineSeconds = 0.0;
};

[[nodiscard]] ForceFieldPageReading ResolveForceFieldPageReading(const ForceFieldConfiguration& Field);

// 📝 What the Wind sections read: the master field (its eddies switch and timeline) and the eddy preview the page keeps.
struct WindPageReading
{
    ForceFieldConfiguration* Forces          = nullptr;
    double                   TimelineSeconds = 0.0;
    ForceFieldVisualization* EddyPreview     = nullptr;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    FORCE FIELD SECTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each Construct draws its section and returns true when a Force Field setting changed (edits to other records advance
//    their own revisions).
class FieldViewSection
{
  public:
    static bool Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading& Reading);
};

class FieldRangeSection
{
  public:
    static bool Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading& Reading);
};

class FieldForcesSection
{
  public:
    static bool Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading& Reading);
};

class FieldReceiversSection
{
  public:
    static bool Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading& Reading);
};

class FieldTimelineSection
{
  public:
    static bool Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading& Reading);
};

class FieldVectorsSection
{
  public:
    static bool Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading& Reading);
};

class FieldBudgetSection
{
  public:
    static bool Construct(ControlColumn& Page, ForceFieldConfiguration& Field, const ForceFieldPageReading& Reading);
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    WIND SECTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each Construct draws its section and returns true when a Wind setting changed.
class WindFlowSection
{
  public:
    static bool Construct(ControlColumn& Page, WindConfiguration& Wind, const WindPageReading& Reading);
};

class WindGustSection
{
  public:
    static bool Construct(ControlColumn& Page, WindConfiguration& Wind, const WindPageReading& Reading);
};

class WindAltitudeSection
{
  public:
    static bool Construct(ControlColumn& Page, WindConfiguration& Wind, const WindPageReading& Reading);
};

class WindEddySection
{
  public:
    static bool Construct(ControlColumn& Page, WindConfiguration& Wind, const WindPageReading& Reading);
};

class WindMesoscaleSection
{
  public:
    static bool Construct(ControlColumn& Page, WindConfiguration& Wind, const WindPageReading& Reading);
};

} // namespace Frontier
