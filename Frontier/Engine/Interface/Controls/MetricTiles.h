/*==========================================================================================================================================
                                                               METRICTILES.H
==========================================================================================================================================*/
// 🧩 .co-kpis: equal metric tiles (label, figure, caption), the last one lit.

#pragma once

#include <string>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    METRIC TILES
//------------------------------------------------------------------------------------------------------------------------

struct MetricTile
{
    std::string Label{};
    std::string Figure{};
    std::string Unit{};
};

class MetricTiles
{
  public:
    static constexpr int LastLead = -1;        // [-] the last tile lit (.co-kpis)
    static constexpr int NoLead   = 1 << 30;   // [-] no tile lit
    // 📝 Lead: the lit tile's index, LastLead (the default) or NoLead.
    static void Construct(ControlColumn& Column, const MetricTile* Metrics, int Count, int Lead = LastLead);
};

} // namespace Frontier
