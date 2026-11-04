/*==========================================================================================================================================
                                                        CLOUDDENSITYREFERENCE.CPP
==========================================================================================================================================*/
// 🧩 The demo's seeded 64³ field, continuous local density, vertical envelope and eight-sample diagnostic projection.

#include "Engine/Interface/Inspectors/CloudDensityReference.h"
#include "Engine/Interface/Inspectors/EnvironmentReadings.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DENSITY EQUATIONS
//------------------------------------------------------------------------------------------------------------------------

double Clamp(double Quantity)
{
    return std::clamp(Quantity, 0.0, 1.0);
}
double Fraction(double Quantity)
{
    return Quantity - std::floor(Quantity);
}
double Ease(double Quantity)
{
    return Quantity * Quantity * (3.0 - 2.0 * Quantity);
}
double Smooth(double Minimum, double Maximum, double Quantity)
{
    return Ease(Clamp((Quantity - Minimum) / (Maximum - Minimum)));
}

const std::vector<uint8_t> &ResolveShapeField()
{
    static const std::vector<uint8_t> Field = []
    {
        uint32_t                          Seed = 0x31a59e27;
        std::array<std::vector<float>, 3> Grids;
        for (int Octave = 0; Octave < 3; ++Octave)
        {
            const int Edge = 8 << Octave;
            Grids[Octave].resize(Edge * Edge * Edge);
            for (float &Sample : Grids[Octave])
            {
                Seed ^= Seed << 13;
                Seed ^= Seed >> 17;
                Seed ^= Seed << 5;
                Sample = static_cast<float>(static_cast<double>(Seed) / 4294967296.0);
            }
        }
        std::vector<uint8_t> Result(64 * 64 * 64);
        for (int Depth = 0; Depth < 64; ++Depth)
            for (int Row = 0; Row < 64; ++Row)
                for (int Column = 0; Column < 64; ++Column)
                {
                    double Sum = 0.0, Weights = 0.0;
                    for (int Octave = 0; Octave < 3; ++Octave)
                    {
                        const int    Edge    = 8 << Octave;
                        const double Scale   = Edge / 64.0;
                        const int    LowerX  = static_cast<int>(std::floor(Column * Scale));
                        const int    LowerY  = static_cast<int>(std::floor(Row * Scale));
                        const int    LowerZ  = static_cast<int>(std::floor(Depth * Scale));
                        const double BlendX  = Ease(Column * Scale - LowerX);
                        const double BlendY  = Ease(Row * Scale - LowerY);
                        const double BlendZ  = Ease(Depth * Scale - LowerZ);
                        double       Density = 0.0;
                        for (int OffsetZ = 0; OffsetZ < 2; ++OffsetZ)
                            for (int OffsetY = 0; OffsetY < 2; ++OffsetY)
                                for (int OffsetX = 0; OffsetX < 2; ++OffsetX)
                                    Density += Grids[Octave][(((LowerZ + OffsetZ) % Edge) * Edge + (LowerY + OffsetY) % Edge) * Edge +
                                                             (LowerX + OffsetX) % Edge] *
                                               (OffsetX ? BlendX : 1.0 - BlendX) * (OffsetY ? BlendY : 1.0 - BlendY) *
                                               (OffsetZ ? BlendZ : 1.0 - BlendZ);
                        const double Weight = 1.0 / (1 << Octave);
                        Sum += Density * Weight;
                        Weights += Weight;
                    }
                    Result[(Depth * 64 + Row) * 64 + Column] = static_cast<uint8_t>(std::round(Sum / Weights * 255.0));
                }
        return Result;
    }();
    return Field;
}

double SampleShape(const std::array<double, 3> &Position)
{
    const auto           &Field = ResolveShapeField();
    std::array<int, 3>    Lower{};
    std::array<double, 3> Blend{};
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        const double Coordinate = std::fmod(Position[Axis], 1.0) * 64.0 - 0.5;
        Lower[Axis]             = static_cast<int>(std::floor(Coordinate));
        Blend[Axis]             = Coordinate - Lower[Axis];
    }
    const auto Wrap = [](int Index) { return (Index % 64 + 64) % 64; };
    double     Sum  = 0.0;
    for (int Depth = 0; Depth < 2; ++Depth)
        for (int Row = 0; Row < 2; ++Row)
            for (int Column = 0; Column < 2; ++Column)
                Sum += Field[(Wrap(Lower[2] + Depth) * 64 + Wrap(Lower[1] + Row)) * 64 + Wrap(Lower[0] + Column)] / 255.0 *
                       (Column ? Blend[0] : 1.0 - Blend[0]) * (Row ? Blend[1] : 1.0 - Blend[1]) * (Depth ? Blend[2] : 1.0 - Blend[2]);
    return Sum;
}

double LocalNoise(const std::array<double, 3> &Position)
{
    std::array<double, 3> Lower{}, Blend{};
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        Lower[Axis] = std::floor(Position[Axis]);
        Blend[Axis] = Ease(Position[Axis] - Lower[Axis]);
    }
    double Sum = 0.0;
    for (int Depth = 0; Depth < 2; ++Depth)
        for (int Row = 0; Row < 2; ++Row)
            for (int Column = 0; Column < 2; ++Column)
            {
                double       AlongX = Fraction((Lower[0] + Column) * 0.1031);
                double       AlongY = Fraction((Lower[1] + Row) * 0.1031);
                double       AlongZ = Fraction((Lower[2] + Depth) * 0.1031);
                const double Dot    = AlongX * (AlongZ + 31.32) + AlongY * (AlongY + 31.32) + AlongZ * (AlongX + 31.32);
                AlongX += Dot;
                AlongY += Dot;
                AlongZ += Dot;
                Sum += Fraction((AlongX + AlongY) * AlongZ) * (Column ? Blend[0] : 1.0 - Blend[0]) * (Row ? Blend[1] : 1.0 - Blend[1]) *
                       (Depth ? Blend[2] : 1.0 - Blend[2]);
            }
    return Sum;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    REFERENCE EVALUATION
//------------------------------------------------------------------------------------------------------------------------

double CloudDensityReference::Evaluate(const CloudConfiguration &Cloud, const std::array<double, 3> &Position)
{
    for (double Coordinate : Position)
        if (!std::isfinite(Coordinate)) return 0;
    if (!Cloud.Enabled) return 0.0;
    if (Cloud.LocalVolume)
    {
        std::array<double, 3> Normal{};
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            const double Half = Cloud.BoundsShape == 2 || (Cloud.BoundsShape == 3 && Axis != 2) ?
                Cloud.HalfExtentsMetres[0] : Cloud.HalfExtentsMetres[Axis];
            Normal[Axis] = (Position[Axis] - Cloud.CentreMetres[Axis]) / std::max(1.0,Half);
        }
        double Margin = 1.0 - (Cloud.BoundsShape == 1 ? std::max({std::abs(Normal[0]), std::abs(Normal[1]), std::abs(Normal[2])}) :
            std::hypot(Normal[0],Normal[1],Normal[2]));
        if (Cloud.BoundsShape == 3) Margin = std::min(Margin,Normal[2]);
        const double Mask   = Smooth(0.0, std::max(0.05, static_cast<double>(Cloud.EdgeSoftness)), Margin);
        if (Mask <= 0.0) return 0.0;
        const double Height  = Clamp(Cloud.BoundsShape == 3 ? Normal[2] : Normal[2] * 0.5 + 0.5);
        double       Profile = Smooth(0.0, 0.1, Height) * (1.0 - Smooth(0.75, 1.0, Height));
        if (Cloud.LocalFormation == 1) Profile = Smooth(0.0, 0.08, Height) * (1.0 - Smooth(0.4, 1.0, Height)) * 1.15;
        if (Cloud.LocalFormation == 2) Profile = Smooth(0.0, 0.3, Height) * (1.0 - Smooth(0.6, 1.0, Height)) * 0.6;
        const double                Size          = std::max(1.0f, Cloud.LocalFeatureMetres);
        const std::array<double, 3> Coordinate    = {Position[0] / Size, Position[2] / Size, Position[1] / Size};
        constexpr double            Scales[]      = {1.0, 2.02, 4.1, 8.3};
        constexpr double            Offsets[4][3] = {{0, 0, 0}, {3.1, 1.7, 9.2}, {7.7, 2.2, 1.1}, {1.3, 8.8, 4.4}};
        double                      Shape         = 0.0;
        for (int Octave = 0; Octave < 4; ++Octave)
            Shape += LocalNoise({Coordinate[0] * Scales[Octave] + Offsets[Octave][0], Coordinate[1] * Scales[Octave] + Offsets[Octave][1],
                                 Coordinate[2] * Scales[Octave] + Offsets[Octave][2]}) /
                     (2 << Octave);
        const double Density =
            Clamp((Shape / 0.9375 - (1.0 - Cloud.Coverage)) / std::max(0.001, static_cast<double>(Cloud.Coverage))) * Profile * Mask;
        if (Density <= 0.0) return 0.0;
        const double Detail = LocalNoise({Coordinate[0] * 9, Coordinate[1] * 9, Coordinate[2] * 9}) * 0.6 +
                              LocalNoise({Coordinate[0] * 19 + 5, Coordinate[1] * 19 + 5, Coordinate[2] * 19 + 5}) * 0.4;
        const double Erosion = (Detail + (1.0 - 2.0 * Detail) * Clamp(Height * 3.0)) * Cloud.EdgeErosion * 0.45;
        return Clamp(Density - Erosion * (1.0 - Density)) * Cloud.ExtinctionDensity;
    }
    const double Top    = std::min<double>(Cloud.CeilingMetres, Cloud.BaseAltitudeMetres + Cloud.LayerThicknessMetres);
    const double Height = (Position[2] - Cloud.BaseAltitudeMetres) / std::max(1.0, Top - Cloud.BaseAltitudeMetres);
    if (Top <= Cloud.BaseAltitudeMetres || Height <= 0.0 || Height >= 1.0) return 0.0;
    const double                Size       = std::max(1.0, 7200.0 * Cloud.FeatureScale);
    const std::array<double, 3> Coordinate = {Position[0] / Size, Position[1] / Size, Position[2] / Size};
    const double                Shape      = SampleShape(Coordinate) -
                         Cloud.EdgeErosion *
                             (1.0 - SampleShape({Coordinate[0] * 4.1 + 0.21, Coordinate[1] * 4.1 + 0.47, Coordinate[2] * 4.1 + 0.13})) *
                             0.22;
    const double Density = Clamp((Shape - (1.0 - Cloud.Coverage)) / std::max(0.08, Cloud.Coverage * 0.65));
    return Ease(Density) * EnvironmentReadings::CloudProfile(Height, Cloud.Formation, Cloud.AnvilShaping) * Cloud.ExtinctionDensity;
}

void CloudDensityReference::ConstructImages(const CloudConfiguration &Cloud,
                                            double                    Aspect,
                                            std::vector<uint8_t>     &Top,
                                            std::vector<uint8_t>     &Side)
{
    Top.resize(Width * Height * 4);
    Side.resize(Width * Height * 4);
    const bool   Local = Cloud.LocalVolume;
    const double HalfZ = Cloud.BoundsShape == 2 ? Cloud.HalfExtentsMetres[0] : Cloud.HalfExtentsMetres[2];
    const double Low   = Local ? Cloud.CentreMetres[2] - (Cloud.BoundsShape == 3 ? 0 : HalfZ) : Cloud.BaseAltitudeMetres;
    const double High  = Local ? Cloud.CentreMetres[2] + HalfZ
                               : std::min<double>(Cloud.CeilingMetres, Cloud.BaseAltitudeMetres + Cloud.LayerThicknessMetres);
    Aspect             = std::max(0.1, Aspect);
    for (int Row = 0; Row < Height; ++Row)
        for (int Column = 0; Column < Width; ++Column)
        {
            const double Along = (Column + 0.5) / Width, Across = (Row + 0.5) / Height;
            const double Half      = Local ? std::max<double>(Cloud.HalfExtentsMetres[0], (Cloud.BoundsShape >= 2 ? Cloud.HalfExtentsMetres[0] : Cloud.HalfExtentsMetres[1]) * Aspect) : 3000.0;
            const double PositionX = Local ? Cloud.CentreMetres[0] + (Along * 2 - 1) * Half : (Along - 0.5) * 6000;
            const double PositionY = Local ? Cloud.CentreMetres[1] + (1 - Across * 2) * Half / Aspect : (0.5 - Across) * 6000 / Aspect;
            double       Average   = 0.0;
            for (int Sample = 0; Sample < 8; ++Sample)
                Average += Evaluate(Cloud, {PositionX, PositionY, Low + (Sample + 0.5) / 8.0 * (High - Low)});
            const double Section = Evaluate(
                Cloud, {Local ? Cloud.CentreMetres[0] + (Along * 2 - 1) * Cloud.HalfExtentsMetres[0] : PositionX,
                        Local ? Cloud.CentreMetres[1] : 0.0,
                        (Local ? High : Cloud.CeilingMetres) - Across * ((Local ? High : Cloud.CeilingMetres) - (Local ? Low : 0.0))});
            const size_t Offset = static_cast<size_t>(Row * Width + Column) * 4;
            auto         Write  = [&](std::vector<uint8_t> &Pixels, double Density)
            {
                const double Opacity = 1.0 - std::exp(-std::max(0.0, Density) * 3.0);
                Pixels[Offset]       = static_cast<uint8_t>(std::nearbyint(26.0 + Opacity * 180.0));
                Pixels[Offset + 1]   = static_cast<uint8_t>(std::nearbyint(35.0 + Opacity * 181.0));
                Pixels[Offset + 2]   = static_cast<uint8_t>(std::nearbyint(47.0 + Opacity * 182.0));
                Pixels[Offset + 3]   = 255;
            };
            Write(Top, Average / 8.0);
            Write(Side, Section);
        }
}

} // namespace Frontier
