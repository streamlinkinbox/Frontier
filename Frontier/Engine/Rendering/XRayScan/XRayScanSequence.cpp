/*==========================================================================================================================================
                                                        XRAYSCANSEQUENCE.CPP
==========================================================================================================================================*/

#include "XRayScanSequence.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace Frontier::XRayScan
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONVERSIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 parseInt(Hex.slice(Start, Start + 2), 16).
int ReadHexPair(std::string_view Hex, size_t Start)
{
    auto Digit = [](char Character) {
        if (Character >= '0' && Character <= '9') return Character - '0';
        if (Character >= 'a' && Character <= 'f') return Character - 'a' + 10;
        return Character - 'A' + 10;
    };
    return Digit(Hex[Start]) * 16 + Digit(Hex[Start + 1]);
}

bool ReadsAsHexColour(std::string_view Hex)
{
    if (Hex.size() != 7 || Hex[0] != '#') return false;
    for (size_t Index = 1; Index < 7; ++Index)
    {
        const char Character = Hex[Index];
        const bool Valid = (Character >= '0' && Character <= '9') || (Character >= 'a' && Character <= 'f') || (Character >= 'A' && Character <= 'F');
        if (!Valid) return false;
    }
    return true;
}

} // namespace

std::array<double, 3> ConvertHexToLinear(std::string_view Hex)
{
    if (!ReadsAsHexColour(Hex))
    {
        throw std::invalid_argument("Expected #RRGGBB");
    }
    std::array<double, 3> Linear{};
    for (size_t Channel = 0; Channel < 3; ++Channel)
    {
        const double Value = ReadHexPair(Hex, 1 + Channel * 2) / 255.0;
        Linear[Channel] = Value <= 0.04045 ? Value / 12.92 : std::pow((Value + 0.055) / 1.055, 2.4);
    }
    return Linear;
}

double ConvertLinearToSrgb(double Value)
{
    return Value <= 0.0031308 ? Value * 12.92 : 1.055 * std::pow(Value, 1.0 / 2.4) - 0.055;
}

std::array<double, 3> ResolveAcesFilmic(const std::array<double, 3>& Linear)
{
    const double InputColumns[3][3]  = {{0.59719, 0.07600, 0.02840}, {0.35458, 0.90834, 0.13383}, {0.04823, 0.01566, 0.83777}};
    const double OutputColumns[3][3] = {{1.60475, -0.10208, -0.00327}, {-0.53108, 1.10813, -0.07276}, {-0.07367, -0.00605, 1.07602}};
    auto Multiply = [](const double (&Columns)[3][3], const std::array<double, 3>& Vector) {
        std::array<double, 3> Product{};
        for (size_t Row = 0; Row < 3; ++Row)
        {
            Product[Row] = Columns[0][Row] * Vector[0] + Columns[1][Row] * Vector[1] + Columns[2][Row] * Vector[2];
        }
        return Product;
    };
    auto Fit = [](double X) { return (X * (X + 0.0245786) - 0.000090537) / (X * (0.983729 * X + 0.4329510) + 0.238081); };
    std::array<double, 3> Prescaled{};
    for (size_t Channel = 0; Channel < 3; ++Channel) Prescaled[Channel] = Linear[Channel] / 0.6;
    std::array<double, 3> Fitted = Multiply(InputColumns, Prescaled);
    for (double& Value : Fitted) Value = Fit(Value);
    std::array<double, 3> Mapped = Multiply(OutputColumns, Fitted);
    for (double& Value : Mapped) Value = std::min(std::max(Value, 0.0), 1.0);
    return Mapped;
}

std::array<double, 3> ResolveDisplayColour(std::string_view Hex)
{
    std::array<double, 3> Display = ResolveAcesFilmic(ConvertHexToLinear(Hex));
    for (double& Value : Display) Value = ConvertLinearToSrgb(Value);
    return Display;
}

double EaseInOutCubic(double X)
{
    return X < 0.5 ? 4.0 * X * X * X : 1.0 - std::pow(-2.0 * X + 2.0, 3.0) / 2.0;
}

double DampValue(double Value, double Target, double Lambda, double Seconds)
{
    return Value + (Target - Value) * (1.0 - std::exp(-Lambda * Seconds));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RUNTIME
//------------------------------------------------------------------------------------------------------------------------

void XRayScanSequence::Assign(bool Enable)
{
    if (Enable && !Enabled_)
    {
        SweepSeconds_ = 0.0;
    }
    Enabled_ = Enable;
}

void XRayScanSequence::Advance(double RawSeconds)
{
    const ScanSpecificationValues& Spec = ScanSpecification;
    const double Seconds = std::min(RawSeconds, Spec.StepLimit);
    Clock_ += RawSeconds;
    const double Target = Enabled_ ? 1.0 : 0.0;
    Ramp_ = DampValue(Ramp_, Target, Spec.RampRate, Seconds);
    if (std::abs(Ramp_ - Target) < Spec.RampSettle)
    {
        Ramp_ = Target;
    }
    double CycleAmount = 1.0;
    bool   Sweeping    = false;
    if (Enabled_)
    {
        SweepSeconds_ += Seconds;
        // 📝 JavaScript's % on numbers is the C fmod.
        const double Phase  = std::fmod(SweepSeconds_, Spec.Cycle);
        const double Travel = Phase < Spec.Sweep ? EaseInOutCubic(Phase / Spec.Sweep) : 1.0;
        Sweeping = Phase < Spec.Sweep;
        Front_   = Spec.FrontStart + (Spec.FrontEnd - Spec.FrontStart) * Travel;
        CycleAmount = Phase < Spec.Sweep + Spec.Hold
                    ? 1.0
                    : std::max(0.0, 1.0 - (Phase - Spec.Sweep - Spec.Hold) / (Spec.Cycle - Spec.Sweep - Spec.Hold));
    }
    CycleAmount_ = CycleAmount;
    Sweeping_    = Sweeping;
    Amount_      = Ramp_ * (Enabled_ ? CycleAmount : 1.0);
    Transparent_ = Ramp_ > Spec.SolidFloor;
}

std::array<double, 2> XRayScanSequence::ResolveCurtain() const
{
    const ScanSpecificationValues& Spec = ScanSpecification;
    const double Visibility = Ramp_ * CycleAmount_ * (Sweeping_ ? 1.0 : Spec.CurtainIdle);
    return {Spec.CurtainOpacity[0] * Visibility, Spec.CurtainOpacity[1] * Visibility};
}

double XRayScanSequence::ResolveEdgeOpacity() const
{
    return ScanSpecification.EdgeOpacity * Amount_;
}

} // namespace Frontier::XRayScan
