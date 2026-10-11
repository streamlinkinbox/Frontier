//============================================================================================================================================
//                                                          ENVIRONMENTPROJECTION.H
//============================================================================================================================================
// 📦 Calendar coordinates and explicitly illustrative atmosphere/spectrum studies.

#pragma once

#include "CelestialSolver.h"
#include "SunColourTemperature.h"
#include <algorithm>
#include <cmath>

namespace Frontier::EnvironmentProjection {

inline int CountMonthDays(int Year, int Month)
{
    constexpr int Days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool Leap = Year % 4 == 0 && (Year % 100 != 0 || Year % 400 == 0);
    return Days[std::clamp(Month, 1, 12) - 1] + (Month == 2 && Leap ? 1 : 0);
}

inline int CountYearDays(int Year)
{
    return 337 + CountMonthDays(Year, 2);
}

inline int ProjectDay(const CelestialObservation& Observer)
{
    int DayNumber = std::clamp(Observer.Day, 1, CountMonthDays(Observer.Year, Observer.Month));
    for (int Month = 1; Month < std::clamp(Observer.Month, 1, 12); ++Month)
        DayNumber += CountMonthDays(Observer.Year, Month);
    return DayNumber;
}

inline void ResolveDate(CelestialObservation& Observer, int DayNumber)
{
    DayNumber = std::clamp(DayNumber, 1, CountYearDays(Observer.Year));
    Observer.Month = 1;
    while (DayNumber > CountMonthDays(Observer.Year, Observer.Month))
    {
        DayNumber -= CountMonthDays(Observer.Year, Observer.Month);
        ++Observer.Month;
    }
    Observer.Day = DayNumber;
}

inline float SampleSpectrum(float Wavelength, float Kelvin)
{
    const double Temperature = SunColourTemperature::Clamp(Kelvin);
    const double Peak = std::clamp(2897771.955 / Temperature, 380.0, 780.0);
    const double λ = std::clamp(double(Wavelength), 380.0, 780.0);
    return float(std::pow(Peak / λ, 5) * std::expm1(14387769.0 / (Peak * Temperature)) /
        std::expm1(14387769.0 / (λ * Temperature)));
}

inline float SampleScattering(float Wavelength, float Strength)
{
    return Strength * std::pow(550.0f / std::clamp(Wavelength, 380.0f, 780.0f), 4);
}

inline float SampleAerosolScattering(float Wavelength, float Strength)
{
    return Strength * std::pow(550.0f / std::clamp(Wavelength, 380.0f, 780.0f), 1.3f);
}

inline float SampleHazeTransmission(float Distance, float Strength)
{
    return std::exp(-0.2f * Strength * std::max(Distance, 0.0f));
}

inline float SampleOzoneTransmission(float Wavelength, float Strength)
{
    const float Distance = (Wavelength - 600) / 80;
    return std::exp(-Strength * std::exp(-Distance * Distance));
}

inline float SampleDensity(float Altitude, float ScaleHeight)
{
    return std::exp(-std::max(Altitude, 0.0f) / std::max(ScaleHeight, 1.0f));
}

}
