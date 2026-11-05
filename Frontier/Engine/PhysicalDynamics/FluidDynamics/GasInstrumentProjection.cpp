/*==========================================================================================================================================
                                                    GASINSTRUMENTPROJECTION.CPP
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor's decay and blackbody instruments: DrawDecay, DrawBlackbody and ResolveHelland of InspectorPages.js, expression for
//    expression (the same operations in the same order, on doubles).

#include "GasInstrumentProjection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::FluidDynamics
{

namespace
{

constexpr InstrumentColour GridInk    = { 0x2c, 0x2c, 0x2c };   // [-] "#2c2c2c"
constexpr InstrumentColour DensityInk = { 0xd8, 0xd8, 0xd8 };   // [-] "#d8d8d8"
constexpr InstrumentColour HeatInk    = { 0xe8, 0x83, 0x3a };   // [-] "#e8833a"
constexpr InstrumentColour LabelInk   = { 0x7d, 0x7d, 0x7d };   // [-] "#7d7d7d"

// 📝 Number.prototype.toFixed(0): the nearest integer, a tie taken upward.
std::string FormatWhole(double Value)
{
    char Text[48];
    std::snprintf(Text, sizeof(Text), "%.0f", std::floor(Value + 0.5));
    return Text;
}

} // namespace

std::array<double, 3> ResolveHellandColour(double Kelvin) noexcept
{
    const double Hecto = std::max(1000.0, std::min(6600.0, Kelvin)) / 100.0;
    const double Green = std::min(1.0, std::max(0.0, (99.4708025861 * std::log(Hecto) - 161.1195681661) / 255.0));
    const double Blue  = Hecto <= 19.0
                             ? 0.0
                             : std::min(1.0, std::max(0.0, (138.5177312231 * std::log(std::max(Hecto - 10.0, 1.0)) - 305.0447927307) / 255.0));
    return { 1.0, std::pow(Green, 2.2), std::pow(Blue, 2.2) };
}

DecayInstrument ConstructDecayInstrument(const GasParameters& Gas, double Width, double Height)
{
    DecayInstrument Instrument;
    const double    Left = 18.0, Right = Width - 18.0, Top = 18.0, Bottom = Height - 26.0;
    for (int Line = 0; Line <= 4; ++Line)
    {
        const double Y = Top + (Bottom - Top) * Line / 4;
        Instrument.Strokes.push_back({ GridInk, 1.0, { { Left, Y }, { Right, Y } } });
    }
    const auto Curve = [&](double Loss, const InstrumentColour& Colour) {
        InstrumentPolyline Stroke{ Colour, 3.0, {} };
        for (int Step = 0; Step <= 60; ++Step)
        {
            const double Seconds  = static_cast<double>(Step) / 60 * 12.0;
            const double Survival = std::pow(std::max(0.0, 1.0 - Loss), Seconds);
            const double X        = Left + (Right - Left) * Step / 60;
            const double Y        = Bottom - (Bottom - Top) * Survival;
            Stroke.Points.push_back({ X, Y });
        }
        Instrument.Strokes.push_back(std::move(Stroke));
    };
    Curve(Gas.Dissipation, DensityInk);
    Curve(Gas.Cooling, HeatInk);
    Instrument.Labels.push_back({ "density", Left, Height - 4, LabelInk });
    Instrument.Labels.push_back({ "heat", Left + 80, Height - 4, HeatInk });
    Instrument.Labels.push_back({ "12 s", Right - 40, Height - 4, LabelInk });
    return Instrument;
}

BlackbodyInstrument ConstructBlackbodyInstrument(const GasParameters& Gas, double Width, double Height)
{
    BlackbodyInstrument Instrument;
    const double        Left = 18.0, Right = Width - 18.0;
    const double        Tip = Gas.FlameTip * Gas.FlameTemperature, Core = Gas.FlameCore * Gas.FlameTemperature;
    for (double X = Left; X < Right; X += 2)
    {
        const double Mapped     = (X - Left) / (Right - Left);
        const double Kelvin     = Tip + (Core - Tip) * Mapped;
        const auto   Colour     = ResolveHellandColour(Kelvin);
        const double Brightness = std::min(1.6, std::pow(Kelvin / 1500.0, 4.0));
        InstrumentColour Tone{};
        for (size_t Channel = 0; Channel < 3; ++Channel)
        {
            const double Lit     = Colour[Channel] * Brightness / (1.0 + Colour[Channel] * Brightness);
            const double Encoded = std::floor(255 * std::pow(std::min(1.0, Lit) * 1.6, 1 / 2.2) + 0.5);   // Math.round
            Tone[Channel]        = static_cast<uint8_t>(std::min(255.0, Encoded));
        }
        Instrument.Bands.push_back({ X, 14.0, 2.0, Height - 50, Tone });
    }
    Instrument.Labels.push_back({ "tip " + FormatWhole(Tip) + " K", Left, Height - 8, LabelInk });
    Instrument.Labels.push_back({ "core " + FormatWhole(Core) + " K", Right - 100, Height - 8, LabelInk });
    return Instrument;
}

} // namespace Frontier::FluidDynamics
