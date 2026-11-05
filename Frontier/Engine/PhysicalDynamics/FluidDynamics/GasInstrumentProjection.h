/*==========================================================================================================================================
                                                    GASINSTRUMENTPROJECTION.H
==========================================================================================================================================*/
// 🧩 The two instruments of the Smoke & fire editor as the strokes, bands and labels they draw, in canvas pixels: the survival of density and
//    heat under the gas's losses (Simulation card) and the flame's blackbody palette from tip to core (Fire card). A port of the HMI
//    prototype's InspectorPages.js DrawDecay, DrawBlackbody and ResolveHelland, expression for expression; the editor lays them on its cards,
//    and PlumeSimulationValidation holds them to what the prototype's own drawing code strokes and fills on a recording canvas.
//
// 📝 The prototype's canvas is InstrumentCanvasWidth wide and twice its CSS height tall, shown at the card's width and the CSS height: the
//    editor maps canvas x by (card width / InstrumentCanvasWidth) and canvas y by 0.5, as the browser does.

#pragma once

#include "GasParameterCatalogue.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::FluidDynamics
{

inline constexpr double InstrumentCanvasWidth = 640.0;   // [px] Instrument(): canvas.width

using InstrumentColour = std::array<uint8_t, 3>;

// 📝 One canvas path: beginPath, moveTo, lineTo ×(n − 1), stroke, in Colour at Width.
struct InstrumentPolyline
{
    InstrumentColour                  Colour{};
    double                            Width = 1.0;   // [px]
    std::vector<std::array<double, 2>> Points;      // [px]
};

// 📝 One fillRect.
struct InstrumentBand
{
    double           X      = 0.0;   // [px]
    double           Y      = 0.0;   // [px]
    double           Width  = 0.0;   // [px]
    double           Height = 0.0;   // [px]
    InstrumentColour Colour{};
};

// 📝 One fillText at a baseline position, in the prototype's 18 px canvas font.
struct InstrumentLabel
{
    std::string      Text;
    double           X = 0.0;   // [px]
    double           Y = 0.0;   // [px]
    InstrumentColour Colour{};
};

struct DecayInstrument
{
    std::vector<InstrumentPolyline> Strokes;   // the five grid lines, then the density and heat curves
    std::vector<InstrumentLabel>    Labels;    // "density", "heat", "12 s"
};

struct BlackbodyInstrument
{
    std::vector<InstrumentBand>  Bands;    // one 2 px band per two canvas pixels from tip to core
    std::vector<InstrumentLabel> Labels;   // "tip … K", "core … K"
};

// 📐 DrawDecay: survival after emission, (1 − loss)^t over 12 s, for the density's Dissipation and the heat's Cooling.
[[nodiscard]] DecayInstrument ConstructDecayInstrument(const GasParameters& Gas, double Width, double Height);

// 📐 DrawBlackbody: the Helland fit, tone-mapped as the march maps it, from FlameTip to FlameCore (each × FlameTemperature).
[[nodiscard]] BlackbodyInstrument ConstructBlackbodyInstrument(const GasParameters& Gas, double Width, double Height);

// 📐 ResolveHelland: Helland's blackbody fit (the march's ResolveBlackbodyColour), linear. Kelvin is held to 1000 K … 6600 K.
[[nodiscard]] std::array<double, 3> ResolveHellandColour(double Kelvin) noexcept;

} // namespace Frontier::FluidDynamics
