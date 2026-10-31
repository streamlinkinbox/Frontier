// VehicleHMI Outliner glyph renderer: native Lucide path tessellation plus ThorVG-backed custom SVG atlas draws.
// Lucide path data below is from lucide-static 1.57.0 (https://lucide.dev), used under the ISC License:
// Copyright (c) 2026 Lucide Icons and Contributors. Permission to use, copy, modify, and/or distribute this software
// for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission
// notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO
// THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.

#include "VehicleHmiGlyphs.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/IconPresentation.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace Frontier
{
namespace
{

constexpr double Pi = 3.14159265358979323846;

struct SvgPoint
{
    double X = 0.0;
    double Y = 0.0;
};

SvgPoint operator-(SvgPoint A, SvgPoint B) noexcept { return {A.X - B.X, A.Y - B.Y}; }
SvgPoint operator*(SvgPoint A, double S) noexcept { return {A.X * S, A.Y * S}; }

class SvgPathReader final
{
  public:
    explicit SvgPathReader(std::string_view Source) : Source_(Source) {}

    bool AtEnd() const noexcept { return Position_ >= Source_.size(); }

    void SkipSeparators() noexcept
    {
        while (Position_ < Source_.size() &&
               (std::isspace(static_cast<unsigned char>(Source_[Position_])) || Source_[Position_] == ','))
        {
            ++Position_;
        }
    }

    bool HasNumber() noexcept
    {
        SkipSeparators();
        return Position_ < Source_.size() &&
               !std::isalpha(static_cast<unsigned char>(Source_[Position_]));
    }

    char ReadCommand() noexcept
    {
        SkipSeparators();
        if (Position_ >= Source_.size() || !std::isalpha(static_cast<unsigned char>(Source_[Position_])))
        {
            return '\0';
        }
        return Source_[Position_++];
    }

    bool ReadNumber(double& Value) noexcept
    {
        SkipSeparators();
        if (Position_ >= Source_.size())
        {
            return false;
        }
        const char* Begin = Source_.data() + Position_;
        const char* End   = Source_.data() + Source_.size();
        const auto  Read  = std::from_chars(Begin, End, Value, std::chars_format::general);
        if (Read.ec != std::errc{} || Read.ptr == Begin)
        {
            return false;
        }
        Position_ = static_cast<size_t>(Read.ptr - Source_.data());
        return std::isfinite(Value);
    }

  private:
    std::string_view Source_;
    size_t Position_ = 0;
};

ImVec2 TransformPoint(SvgPoint Point, ImVec2 Centre, float Size, double ViewExtent, float RotationDegrees) noexcept
{
    const double Scale = static_cast<double>(Size) / ViewExtent;
    double       X     = (Point.X - ViewExtent * 0.5) * Scale;
    double       Y     = (Point.Y - ViewExtent * 0.5) * Scale;
    if (RotationDegrees != 0.0f)
    {
        const double Angle = static_cast<double>(RotationDegrees) * Pi / 180.0;
        const double CosA  = std::cos(Angle);
        const double SinA  = std::sin(Angle);
        const double RotX  = X * CosA - Y * SinA;
        Y                  = X * SinA + Y * CosA;
        X                  = RotX;
    }
    return ImVec2(Centre.x + static_cast<float>(X), Centre.y + static_cast<float>(Y));
}

void DrawStrokePolyline(ImDrawList* Ink, const std::vector<SvgPoint>& Points, bool Closed, ImVec2 Centre, float Size,
                        double ViewExtent, ImU32 Colour, float Thickness, float RotationDegrees)
{
    if (Ink == nullptr || Points.size() < 2 || Thickness <= 0.0f)
    {
        return;
    }
    std::vector<ImVec2> Screen;
    Screen.reserve(Points.size());
    for (const SvgPoint Point : Points)
    {
        Screen.push_back(TransformPoint(Point, Centre, Size, ViewExtent, RotationDegrees));
    }
    Ink->AddPolyline(Screen.data(), static_cast<int>(Screen.size()), Colour,
                     Closed ? ImDrawFlags_Closed : ImDrawFlags_None, Thickness);

    // Lucide specifies round joins and caps. ImDrawList polylines are butt-capped, so round the
    // joints/endpoints with the same half-stroke radius as the original SVG stroke.
    for (const ImVec2 Point : Screen)
    {
        Ink->AddCircleFilled(Point, Thickness * 0.5f, Colour, 8);
    }
}

void AppendCubic(std::vector<SvgPoint>& Points, SvgPoint From, SvgPoint ControlA, SvgPoint ControlB, SvgPoint To)
{
    constexpr int Steps = 8;
    for (int Step = 1; Step <= Steps; ++Step)
    {
        const double T  = static_cast<double>(Step) / Steps;
        const double U  = 1.0 - T;
        const double W0 = U * U * U;
        const double W1 = 3.0 * U * U * T;
        const double W2 = 3.0 * U * T * T;
        const double W3 = T * T * T;
        Points.push_back({W0 * From.X + W1 * ControlA.X + W2 * ControlB.X + W3 * To.X,
                          W0 * From.Y + W1 * ControlA.Y + W2 * ControlB.Y + W3 * To.Y});
    }
}

void AppendQuadratic(std::vector<SvgPoint>& Points, SvgPoint From, SvgPoint Control, SvgPoint To)
{
    constexpr int Steps = 6;
    for (int Step = 1; Step <= Steps; ++Step)
    {
        const double T = static_cast<double>(Step) / Steps;
        const double U = 1.0 - T;
        Points.push_back({U * U * From.X + 2.0 * U * T * Control.X + T * T * To.X,
                          U * U * From.Y + 2.0 * U * T * Control.Y + T * T * To.Y});
    }
}

void AppendArc(std::vector<SvgPoint>& Points, SvgPoint From, double RadiusX, double RadiusY,
               double RotationDegrees, bool LargeArc, bool Sweep, SvgPoint To)
{
    if (RadiusX <= 1e-9 || RadiusY <= 1e-9 ||
        (std::abs(From.X - To.X) < 1e-9 && std::abs(From.Y - To.Y) < 1e-9))
    {
        if (std::abs(From.X - To.X) > 1e-9 || std::abs(From.Y - To.Y) > 1e-9)
        {
            Points.push_back(To);
        }
        return;
    }

    double Rx  = std::abs(RadiusX);
    double Ry  = std::abs(RadiusY);
    const double Phi = RotationDegrees * Pi / 180.0;
    const double Cos = std::cos(Phi);
    const double Sin = std::sin(Phi);
    const double Dx  = (From.X - To.X) * 0.5;
    const double Dy  = (From.Y - To.Y) * 0.5;
    const double Xp  = Cos * Dx + Sin * Dy;
    const double Yp  = -Sin * Dx + Cos * Dy;
    const double Lambda = (Xp * Xp) / (Rx * Rx) + (Yp * Yp) / (Ry * Ry);
    if (Lambda > 1.0)
    {
        const double Factor = std::sqrt(Lambda);
        Rx *= Factor;
        Ry *= Factor;
    }

    const double Numerator = std::max(0.0, Rx * Rx * Ry * Ry - Rx * Rx * Yp * Yp - Ry * Ry * Xp * Xp);
    const double Denominator = Rx * Rx * Yp * Yp + Ry * Ry * Xp * Xp;
    const double Direction = (LargeArc == Sweep) ? -1.0 : 1.0;
    const double Factor = Denominator > 1e-12 ? Direction * std::sqrt(Numerator / Denominator) : 0.0;
    const double Cxp = Factor * (Rx * Yp / Ry);
    const double Cyp = Factor * (-Ry * Xp / Rx);
    const double Cx  = Cos * Cxp - Sin * Cyp + (From.X + To.X) * 0.5;
    const double Cy  = Sin * Cxp + Cos * Cyp + (From.Y + To.Y) * 0.5;

    const double Ux = (Xp - Cxp) / Rx;
    const double Uy = (Yp - Cyp) / Ry;
    const double Vx = (-Xp - Cxp) / Rx;
    const double Vy = (-Yp - Cyp) / Ry;
    const double Start = std::atan2(Uy, Ux);
    double Delta = std::atan2(Ux * Vy - Uy * Vx, Ux * Vx + Uy * Vy);
    if (!Sweep && Delta > 0.0)
    {
        Delta -= 2.0 * Pi;
    }
    else if (Sweep && Delta < 0.0)
    {
        Delta += 2.0 * Pi;
    }
    const int Steps = std::clamp(static_cast<int>(std::ceil(std::abs(Delta) / (Pi / 16.0))), 2, 32);
    for (int Step = 1; Step <= Steps; ++Step)
    {
        const double Angle = Start + Delta * static_cast<double>(Step) / Steps;
        const double X     = Rx * std::cos(Angle);
        const double Y     = Ry * std::sin(Angle);
        Points.push_back({Cx + Cos * X - Sin * Y, Cy + Sin * X + Cos * Y});
    }
    Points.back() = To; // retain the authored endpoint exactly after arc flattening
}

void DrawSvgPath(ImDrawList* Ink, std::string_view Data, ImVec2 Centre, float Size, double ViewExtent,
                 ImU32 Colour, float StrokeWidth, float RotationDegrees)
{
    SvgPathReader Reader(Data);
    std::vector<SvgPoint> Points;
    Points.reserve(40);
    SvgPoint Current{};
    SvgPoint SubpathStart{};
    SvgPoint CubicControl{};
    SvgPoint QuadraticControl{};
    char     Command = '\0';
    char     PreviousCommand = '\0';
    bool     Closed = false;
    const float Thickness = StrokeWidth * Size / static_cast<float>(ViewExtent);

    const auto Flush = [&]() {
        DrawStrokePolyline(Ink, Points, Closed, Centre, Size, ViewExtent, Colour, Thickness, RotationDegrees);
        Points.clear();
        Closed = false;
    };
    const auto ReadPair = [&](SvgPoint& Point, bool Relative) -> bool {
        double X = 0.0;
        double Y = 0.0;
        if (!Reader.ReadNumber(X) || !Reader.ReadNumber(Y))
        {
            return false;
        }
        Point = Relative ? SvgPoint{Current.X + X, Current.Y + Y} : SvgPoint{X, Y};
        return true;
    };

    while (!Reader.AtEnd())
    {
        const char NewCommand = Reader.ReadCommand();
        if (NewCommand != '\0')
        {
            Command = NewCommand;
            const char Op = static_cast<char>(std::toupper(static_cast<unsigned char>(Command)));
            if (Op == 'Z')
            {
                if (!Points.empty())
                {
                    if (std::abs(Points.back().X - SubpathStart.X) > 1e-8 ||
                        std::abs(Points.back().Y - SubpathStart.Y) > 1e-8)
                    {
                        Points.push_back(SubpathStart);
                    }
                    Closed = true;
                    Current = SubpathStart;
                    Flush();
                }
                PreviousCommand = 'Z';
                Command = '\0';
                continue;
            }
        }
        else if (!Reader.HasNumber())
        {
            break;
        }
        if (Command == '\0')
        {
            break;
        }

        const bool Relative = std::islower(static_cast<unsigned char>(Command)) != 0;
        const char Op = static_cast<char>(std::toupper(static_cast<unsigned char>(Command)));
        SvgPoint End{};
        switch (Op)
        {
            case 'M':
                if (!ReadPair(End, Relative))
                {
                    return;
                }
                if (!Points.empty())
                {
                    Flush();
                }
                Current = SubpathStart = End;
                Points.push_back(Current);
                Command = Relative ? 'l' : 'L'; // subsequent move-pairs are line segments by SVG rule
                PreviousCommand = 'M';
                break;
            case 'L':
                if (!ReadPair(End, Relative)) return;
                Current = End;
                Points.push_back(Current);
                PreviousCommand = 'L';
                break;
            case 'H':
            {
                double X = 0.0;
                if (!Reader.ReadNumber(X)) return;
                Current.X = Relative ? Current.X + X : X;
                Points.push_back(Current);
                PreviousCommand = 'H';
                break;
            }
            case 'V':
            {
                double Y = 0.0;
                if (!Reader.ReadNumber(Y)) return;
                Current.Y = Relative ? Current.Y + Y : Y;
                Points.push_back(Current);
                PreviousCommand = 'V';
                break;
            }
            case 'C':
            {
                SvgPoint A{}, B{};
                if (!ReadPair(A, Relative) || !ReadPair(B, Relative) || !ReadPair(End, Relative)) return;
                AppendCubic(Points, Current, A, B, End);
                Current = End;
                CubicControl = B;
                PreviousCommand = 'C';
                break;
            }
            case 'S':
            {
                SvgPoint A = (PreviousCommand == 'C' || PreviousCommand == 'S')
                                 ? Current * 2.0 - CubicControl : Current;
                SvgPoint B{};
                if (!ReadPair(B, Relative) || !ReadPair(End, Relative)) return;
                AppendCubic(Points, Current, A, B, End);
                Current = End;
                CubicControl = B;
                PreviousCommand = 'S';
                break;
            }
            case 'Q':
            {
                SvgPoint Control{};
                if (!ReadPair(Control, Relative) || !ReadPair(End, Relative)) return;
                AppendQuadratic(Points, Current, Control, End);
                Current = End;
                QuadraticControl = Control;
                PreviousCommand = 'Q';
                break;
            }
            case 'T':
            {
                const SvgPoint Control = (PreviousCommand == 'Q' || PreviousCommand == 'T')
                                            ? Current * 2.0 - QuadraticControl : Current;
                if (!ReadPair(End, Relative)) return;
                AppendQuadratic(Points, Current, Control, End);
                Current = End;
                QuadraticControl = Control;
                PreviousCommand = 'T';
                break;
            }
            case 'A':
            {
                double Rx = 0.0, Ry = 0.0, Rotation = 0.0, Large = 0.0, Sweep = 0.0;
                if (!Reader.ReadNumber(Rx) || !Reader.ReadNumber(Ry) || !Reader.ReadNumber(Rotation) ||
                    !Reader.ReadNumber(Large) || !Reader.ReadNumber(Sweep) || !ReadPair(End, Relative)) return;
                AppendArc(Points, Current, Rx, Ry, Rotation, Large != 0.0, Sweep != 0.0, End);
                Current = End;
                PreviousCommand = 'A';
                break;
            }
            default:
                return; // Unknown SVG command: fail closed rather than draw a misleading glyph.
        }
    }
    if (!Points.empty())
    {
        Flush();
    }
}

void DrawCircle(ImDrawList* Ink, ImVec2 Centre, float Radius, ImU32 Colour, float Thickness, bool Filled = false)
{
    if (Ink == nullptr || Radius <= 0.0f)
    {
        return;
    }
    if (Filled)
    {
        Ink->AddCircleFilled(Centre, Radius, Colour, 24);
    }
    else
    {
        Ink->AddCircle(Centre, Radius, Colour, 24, Thickness);
    }
}

void DrawLineRound(ImDrawList* Ink, ImVec2 A, ImVec2 B, ImU32 Colour, float Thickness)
{
    Ink->AddLine(A, B, Colour, Thickness);
    const float R = Thickness * 0.5f;
    Ink->AddCircleFilled(A, R, Colour, 8);
    Ink->AddCircleFilled(B, R, Colour, 8);
}

void DrawRect(ImDrawList* Ink, ImVec2 Centre, float Size, double X, double Y, double Width, double Height,
              double ViewExtent, ImU32 Colour, float Thickness, float Radius = 0.0f)
{
    const ImVec2 Min(Centre.x + static_cast<float>((X - ViewExtent * 0.5) * Size / ViewExtent),
                     Centre.y + static_cast<float>((Y - ViewExtent * 0.5) * Size / ViewExtent));
    const ImVec2 Max(Centre.x + static_cast<float>((X + Width - ViewExtent * 0.5) * Size / ViewExtent),
                     Centre.y + static_cast<float>((Y + Height - ViewExtent * 0.5) * Size / ViewExtent));
    Ink->AddRect(Min, Max, Colour, Radius * Size / static_cast<float>(ViewExtent), ImDrawFlags_None,
                 Thickness * Size / static_cast<float>(ViewExtent));
}

void DrawLine(ImDrawList* Ink, ImVec2 Centre, float Size, double X1, double Y1, double X2, double Y2,
              double ViewExtent, ImU32 Colour, float Thickness, float RotationDegrees = 0.0f)
{
    const ImVec2 A = TransformPoint({X1, Y1}, Centre, Size, ViewExtent, RotationDegrees);
    const ImVec2 B = TransformPoint({X2, Y2}, Centre, Size, ViewExtent, RotationDegrees);
    DrawLineRound(Ink, A, B, Colour, Thickness * Size / static_cast<float>(ViewExtent));
}

ImU32 WithOpacity(ImU32 Colour, float Opacity) noexcept
{
    const int Alpha = static_cast<int>((Colour >> IM_COL32_A_SHIFT) & 0xff);
    const int NewAlpha = static_cast<int>(Alpha * std::clamp(Opacity, 0.0f, 1.0f) + 0.5f);
    return (Colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(NewAlpha) << IM_COL32_A_SHIFT);
}

bool DrawVehicleHmiAssetGlyph(ImDrawList* Ink, std::string_view Name, ImVec2 Centre, float Size, float Opacity)
{
    IconSymbol Symbol = IconSymbol::Count;
    if (Name == "NativeEnvironment") Symbol = IconSymbol::FolderEnvironment;
    else if (Name == "NativeSun") Symbol = IconSymbol::Sun;
    else if (Name == "NativeAtmosphere") Symbol = IconSymbol::SkyScattering;
    else if (Name == "NativeClouds") Symbol = IconSymbol::Clouds;
    else if (Name == "NativeLocalCloud") Symbol = IconSymbol::LocalCloud;
    else if (Name == "NativeFog") Symbol = IconSymbol::Fog;
    else if (Name == "NativeLocalFog") Symbol = IconSymbol::LocalFog;
    else if (Name == "NativeStars") Symbol = IconSymbol::OutlinerStars;
    else if (Name == "NativeMoon") Symbol = IconSymbol::Moon;
    else if (Name == "NativeFlare") Symbol = IconSymbol::LensFlare;
    else return false;

    const ImVec2 Minimum(Centre.x - Size * 0.5f, Centre.y - Size * 0.5f);
    return IconPresentation::Draw(Ink, Symbol, Minimum, Size, Opacity);
}

void DrawEllipse(ImDrawList* Ink, ImVec2 Centre, float RadiusX, float RadiusY, ImU32 Colour, bool Filled)
{
    std::array<ImVec2, 32> Points{};
    for (size_t Index = 0; Index < Points.size(); ++Index)
    {
        const double Angle = 2.0 * Pi * static_cast<double>(Index) / static_cast<double>(Points.size());
        Points[Index] = ImVec2(Centre.x + RadiusX * static_cast<float>(std::cos(Angle)),
                               Centre.y + RadiusY * static_cast<float>(std::sin(Angle)));
    }
    if (Filled)
    {
        Ink->AddConvexPolyFilled(Points.data(), static_cast<int>(Points.size()), Colour);
    }
    else
    {
        Ink->AddPolyline(Points.data(), static_cast<int>(Points.size()), Colour, ImDrawFlags_Closed, 1.0f);
    }
}

ImVec2 EnvironmentPoint(ImVec2 Centre, float Size, double X, double Y) noexcept
{
    const float Scale = Size / 256.0f;
    return ImVec2(Centre.x + (static_cast<float>(X) - 128.0f) * Scale,
                  Centre.y + (static_cast<float>(Y) - 128.0f) * Scale);
}

void DrawNativeSun(ImDrawList* Ink, ImVec2 Centre, float Size, float Opacity)
{
    const float Scale = Size / 256.0f;
    Ink->AddCircleFilled(Centre, 115.0f * Scale, WithOpacity(IM_COL32(243, 78, 4, 70), Opacity), 32);
    Ink->AddCircleFilled(Centre, 82.0f * Scale, WithOpacity(IM_COL32(255, 147, 28, 180), Opacity), 32);
    Ink->AddCircleFilled(Centre, 76.0f * Scale, WithOpacity(IM_COL32(255, 203, 66, 255), Opacity), 32);
    Ink->AddCircleFilled(ImVec2(Centre.x - 18.0f * Scale, Centre.y - 24.0f * Scale), 31.0f * Scale,
                         WithOpacity(IM_COL32(255, 245, 161, 180), Opacity), 24);
}

void DrawNativeAtmosphere(ImDrawList* Ink, ImVec2 Centre, float Size, float Opacity)
{
    const float Scale = Size / 256.0f;
    Ink->AddCircleFilled(Centre, 105.0f * Scale, WithOpacity(IM_COL32(71, 123, 185, 255), Opacity), 40);
    Ink->AddCircleFilled(Centre, 84.0f * Scale, WithOpacity(IM_COL32(109, 137, 178, 175), Opacity), 40);
    Ink->AddCircleFilled(Centre, 65.0f * Scale, WithOpacity(IM_COL32(41, 73, 99, 255), Opacity), 40);
    Ink->AddCircleFilled(ImVec2(Centre.x - 14.0f * Scale, Centre.y - 17.0f * Scale), 33.0f * Scale,
                         WithOpacity(IM_COL32(86, 133, 153, 125), Opacity), 32);
}

void DrawNativeCloud(ImDrawList* Ink, ImVec2 Centre, float Size, float Opacity, bool Local)
{
    const float Scale = Size / 256.0f;
    const ImU32 Main = WithOpacity(Local ? IM_COL32(199, 221, 241, 255) : IM_COL32(217, 224, 228, 255), Opacity);
    const ImU32 Shade = WithOpacity(IM_COL32(66, 86, 116, Local ? 120 : 150), Opacity);
    const auto P = [&](double X, double Y) { return EnvironmentPoint(Centre, Size, X, Y); };
    Ink->AddCircleFilled(P(66, 149), 39.0f * Scale, Main, 28);
    Ink->AddCircleFilled(P(101, 116), 48.0f * Scale, Main, 30);
    Ink->AddCircleFilled(P(143, 112), 49.0f * Scale, Main, 30);
    Ink->AddCircleFilled(P(184, 143), 38.0f * Scale, Main, 28);
    Ink->AddRectFilled(P(62, 132), P(195, 180), Main, 15.0f * Scale);
    DrawEllipse(Ink, P(128, 164), 65.0f * Scale, 12.0f * Scale, Shade, true);
    if (Local)
    {
        Ink->AddCircleFilled(P(172, 86), 17.0f * Scale, WithOpacity(IM_COL32(255, 210, 117, 230), Opacity), 20);
    }
}

void DrawNativeFog(ImDrawList* Ink, ImVec2 Centre, float Size, float Opacity, bool Local)
{
    const float Scale = Size / 256.0f;
    const ImU32 Mist = WithOpacity(IM_COL32(194, 208, 214, Local ? 150 : 120), Opacity);
    DrawEllipse(Ink, EnvironmentPoint(Centre, Size, 125, 153), 103.0f * Scale, 40.0f * Scale, Mist, true);
    DrawEllipse(Ink, EnvironmentPoint(Centre, Size, 74, 145), 52.0f * Scale, 31.0f * Scale,
                WithOpacity(IM_COL32(226, 232, 232, 130), Opacity), true);
    DrawEllipse(Ink, EnvironmentPoint(Centre, Size, 161, 145), 67.0f * Scale, 34.0f * Scale,
                WithOpacity(IM_COL32(145, 167, 184, 100), Opacity), true);
}

void DrawNativeEnvironment(ImDrawList* Ink, ImVec2 Centre, float Size, float Opacity)
{
    const float Scale = Size / 256.0f;
    const auto P = [&](double X, double Y) { return EnvironmentPoint(Centre, Size, X, Y); };
    const ImU32 Rear = WithOpacity(IM_COL32(48, 48, 48, 255), Opacity);
    const ImU32 Front = WithOpacity(IM_COL32(60, 60, 60, 255), Opacity);
    const ImU32 Edge = WithOpacity(IM_COL32(85, 85, 85, 255), Opacity);
    const float R = 18.0f * Scale;
    Ink->AddRectFilled(P(27, 48), P(229, 214), Rear, R);
    Ink->AddRect(P(27, 48), P(229, 214), Edge, R, ImDrawFlags_None, 3.0f * Scale);
    Ink->AddRectFilled(P(27, 99), P(229, 214), Front, R);
    Ink->AddRect(P(27, 99), P(229, 214), Edge, R, ImDrawFlags_None, 3.0f * Scale);
    Ink->AddCircleFilled(P(97, 139), 24.0f * Scale, WithOpacity(IM_COL32(224, 190, 127, 255), Opacity), 24);
    const ImU32 Cloud = WithOpacity(IM_COL32(169, 198, 221, 255), Opacity);
    Ink->AddCircleFilled(P(118, 156), 20.0f * Scale, Cloud, 20);
    Ink->AddCircleFilled(P(145, 143), 27.0f * Scale, Cloud, 22);
    Ink->AddCircleFilled(P(172, 157), 19.0f * Scale, Cloud, 20);
    Ink->AddRectFilled(P(111, 153), P(190, 178), Cloud, 10.0f * Scale);
}

void DrawNativeStars(ImDrawList* Ink, ImVec2 Centre, float Size, float Opacity)
{
    const ImU32 Star = WithOpacity(IM_COL32(213, 223, 234, 245), Opacity);
    const float Scale = Size / 24.0f;
    const auto Ray = [&](float X, float Y, float R) {
        Ink->AddLine(ImVec2(Centre.x + (X - 12) * Scale, Centre.y + (Y - R - 12) * Scale),
                     ImVec2(Centre.x + (X - 12) * Scale, Centre.y + (Y + R - 12) * Scale), Star, 1.0f * Scale);
        Ink->AddLine(ImVec2(Centre.x + (X - R - 12) * Scale, Centre.y + (Y - 12) * Scale),
                     ImVec2(Centre.x + (X + R - 12) * Scale, Centre.y + (Y - 12) * Scale), Star, 1.0f * Scale);
    };
    Ray(6.0f, 7.0f, 2.1f);
    Ray(17.0f, 6.0f, 1.4f);
    Ray(14.0f, 16.0f, 2.3f);
    Ink->AddCircleFilled(ImVec2(Centre.x - 5.0f * Scale, Centre.y + 6.0f * Scale), 0.75f * Scale, Star, 8);
}

void DrawNativeFlare(ImDrawList* Ink, ImVec2 Centre, float Size, float Opacity)
{
    const float Scale = Size / 24.0f;
    const ImU32 Main = WithOpacity(IM_COL32(231, 204, 157, 230), Opacity);
    DrawCircle(Ink, Centre, 4.0f * Scale, Main, 1.0f * Scale);
    DrawCircle(Ink, ImVec2(Centre.x + 6.0f * Scale, Centre.y - 5.0f * Scale), 1.4f * Scale, Main, 0.8f * Scale);
    DrawLine(Ink, Centre, Size, 12, 2, 12, 22, 24, Main, 0.75f);
    DrawLine(Ink, Centre, Size, 2, 12, 22, 12, 24, Main, 0.75f);
}

void DrawNativeMoon(ImDrawList* Ink, ImVec2 Centre, float Size, float Opacity)
{
    const float Scale = Size / 24.0f;
    const ImU32 Moon = WithOpacity(IM_COL32(196, 206, 221, 245), Opacity);
    Ink->AddCircle(Centre, 8.5f * Scale, Moon, 24, 1.5f * Scale);
    Ink->AddCircle(ImVec2(Centre.x + 5.0f * Scale, Centre.y - 4.0f * Scale), 7.1f * Scale,
                   WithOpacity(IM_COL32(16, 16, 16, 255), Opacity), 24, 2.2f * Scale);
}

void Paths(ImDrawList* Ink, ImVec2 Centre, float Size, ImU32 Colour, float StrokeWidth, float Opacity,
           float RotationDegrees, std::initializer_list<std::string_view> Data)
{
    const ImU32 Toned = WithOpacity(Colour, Opacity);
    for (const std::string_view Path : Data)
    {
        DrawSvgPath(Ink, Path, Centre, Size, 24.0, Toned, StrokeWidth, RotationDegrees);
    }
}

} // namespace

void DrawVehicleHmiGlyph(ImDrawList* Ink, std::string_view Name, ImVec2 Centre, float Size, ImU32 Colour,
                         float StrokeWidth, float Opacity, float RotationDegrees)
{
    if (Ink == nullptr || Size <= 0.0f)
    {
        return;
    }
    if (DrawVehicleHmiAssetGlyph(Ink, Name, Centre, Size, Opacity))
    {
        return;
    }
    if (Name == "NativeEnvironment") { DrawNativeEnvironment(Ink, Centre, Size, Opacity); return; }
    if (Name == "NativeSun") { DrawNativeSun(Ink, Centre, Size, Opacity); return; }
    if (Name == "NativeAtmosphere") { DrawNativeAtmosphere(Ink, Centre, Size, Opacity); return; }
    if (Name == "NativeClouds") { DrawNativeCloud(Ink, Centre, Size, Opacity, false); return; }
    if (Name == "NativeLocalCloud") { DrawNativeCloud(Ink, Centre, Size, Opacity, true); return; }
    if (Name == "NativeFog") { DrawNativeFog(Ink, Centre, Size, Opacity, false); return; }
    if (Name == "NativeLocalFog") { DrawNativeFog(Ink, Centre, Size, Opacity, true); return; }
    if (Name == "NativeStars") { DrawNativeStars(Ink, Centre, Size, Opacity); return; }
    if (Name == "NativeMoon") { DrawNativeMoon(Ink, Centre, Size, Opacity); return; }
    if (Name == "NativeFlare") { DrawNativeFlare(Ink, Centre, Size, Opacity); return; }

    if (Name == "Layers")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83z",
               "M2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12",
               "M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17"});
        return;
    }
    if (Name == "Box")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z",
               "m3.3 7 8.7 5 8.7-5", "M12 22V12"});
        return;
    }
    if (Name == "Sun")
    {
        DrawCircle(Ink, Centre, Size * 4.0f / 24.0f, WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f);
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M12 2v2", "M12 20v2", "m4.93 4.93 1.41 1.41", "m17.66 17.66 1.41 1.41",
               "M2 12h2", "M20 12h2", "m6.34 17.66-1.41 1.41", "m19.07 4.93-1.41-1.41"});
        return;
    }
    if (Name == "Camera")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M13.997 4a2 2 0 0 1 1.76 1.05l.486.9A2 2 0 0 0 18.003 7H20a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2h1.997a2 2 0 0 0 1.759-1.048l.489-.904A2 2 0 0 1 10.004 4z"});
        // Camera lens is offset down one SVG unit.
        DrawCircle(Ink, ImVec2(Centre.x, Centre.y + Size / 24.0f), Size * 3.0f / 24.0f,
                   WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f);
        return;
    }
    if (Name == "Car")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M19 17h2c.6 0 1-.4 1-1v-3c0-.9-.7-1.7-1.5-1.9C18.7 10.6 16 10 16 10s-1.3-1.4-2.2-2.3c-.5-.4-1.1-.7-1.8-.7H5c-.6 0-1.1.4-1.4.9l-1.4 2.9A3.7 3.7 0 0 0 2 12v4c0 .6.4 1 1 1h2",
               "M9 17h6"});
        DrawCircle(Ink, ImVec2(Centre.x + (7.0f - 12.0f) * Size / 24.0f, Centre.y + (17.0f - 12.0f) * Size / 24.0f),
                   2.0f * Size / 24.0f, WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f);
        DrawCircle(Ink, ImVec2(Centre.x + (17.0f - 12.0f) * Size / 24.0f, Centre.y + (17.0f - 12.0f) * Size / 24.0f),
                   2.0f * Size / 24.0f, WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f);
        return;
    }
    if (Name == "Flame")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M12 3q1 4 4 6.5t3 5.5a1 1 0 0 1-14 0 5 5 0 0 1 1-3 1 1 0 0 0 5 0c0-2-1.5-3-1.5-5q0-2 2.5-4"});
        return;
    }
    if (Name == "Wind")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M12.8 19.6A2 2 0 1 0 14 16H2", "M17.5 8a2.5 2.5 0 1 1 2 4H2", "M9.8 4.4A2 2 0 1 1 11 8H2"});
        return;
    }
    if (Name == "Sparkles")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M11.017 2.814a1 1 0 0 1 1.966 0l1.051 5.558a2 2 0 0 0 1.594 1.594l5.558 1.051a1 1 0 0 1 0 1.966l-5.558 1.051a2 2 0 0 0-1.594 1.594l-1.051 5.558a1 1 0 0 1-1.966 0l-1.051-5.558a2 2 0 0 0-1.594-1.594l-5.558-1.051a1 1 0 0 1 0-1.966l5.558-1.051a2 2 0 0 0 1.594-1.594z",
               "M20 2v4", "M22 4h-4"});
        DrawCircle(Ink, ImVec2(Centre.x + (4.0f - 12.0f) * Size / 24.0f, Centre.y + (20.0f - 12.0f) * Size / 24.0f),
                   2.0f * Size / 24.0f, WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f);
        return;
    }
    if (Name == "Hammer")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"m15 12-9.373 9.373a1 1 0 0 1-3.001-3L12 9",
               "m18 15 4-4",
               "m21.5 11.5-1.914-1.914A2 2 0 0 1 19 8.172v-.344a2 2 0 0 0-.586-1.414l-1.657-1.657A6 6 0 0 0 12.516 3H9l1.243 1.243A6 6 0 0 1 12 8.485V10l2 2h1.172a2 2 0 0 1 1.414.586L18.5 14.5"});
        return;
    }
    if (Name == "Gauge")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"m12 14 4-4", "M3.34 19a10 10 0 1 1 17.32 0"});
        return;
    }
    if (Name == "Eye")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.747 10.747 0 0 1-19.876 0"});
        DrawCircle(Ink, Centre, Size * 3.0f / 24.0f, WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f);
        return;
    }
    if (Name == "EyeOff")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M10.733 5.076a10.744 10.744 0 0 1 11.205 6.575 1 1 0 0 1 0 .696 10.747 10.747 0 0 1-1.444 2.49",
               "M14.084 14.158a3 3 0 0 1-4.242-4.242",
               "M17.479 17.499a10.75 10.75 0 0 1-15.417-5.151 1 1 0 0 1 0-.696 10.75 10.75 0 0 1 4.446-5.143",
               "m2 2 20 20"});
        return;
    }
    if (Name == "ChevronDown")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees, {"m6 9 6 6 6-6"});
        return;
    }
    if (Name == "ChevronRight")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees, {"m9 18 6-6-6-6"});
        return;
    }
    if (Name == "Search")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees, {"m21 21-4.34-4.34"});
        DrawCircle(Ink, ImVec2(Centre.x - Size / 24.0f, Centre.y - Size / 24.0f),
                   8.0f * Size / 24.0f, WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f);
        return;
    }
    if (Name == "Monitor")
    {
        DrawRect(Ink, Centre, Size, 2, 3, 20, 14, 24, WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f, 2);
        DrawLine(Ink, Centre, Size, 8, 21, 16, 21, 24, WithOpacity(Colour, Opacity), StrokeWidth);
        DrawLine(Ink, Centre, Size, 12, 17, 12, 21, 24, WithOpacity(Colour, Opacity), StrokeWidth);
        return;
    }
    if (Name == "GridLattice")
    {
        DrawRect(Ink, Centre, Size, 3, 3, 18, 18, 24, WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f, 2);
        for (double Axis : {9.0, 15.0})
        {
            DrawLine(Ink, Centre, Size, 3, Axis, 21, Axis, 24, WithOpacity(Colour, Opacity), StrokeWidth);
            DrawLine(Ink, Centre, Size, Axis, 3, Axis, 21, 24, WithOpacity(Colour, Opacity), StrokeWidth);
        }
        return;
    }
    if (Name == "LayoutGrid")
    {
        for (const auto& Rect : {std::array<double, 4>{3, 3, 7, 7}, {14, 3, 7, 7}, {14, 14, 7, 7}, {3, 14, 7, 7}})
        {
            DrawRect(Ink, Centre, Size, Rect[0], Rect[1], Rect[2], Rect[3], 24,
                     WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f, 1);
        }
        return;
    }
    if (Name == "Map")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M14.106 5.553a2 2 0 0 0 1.788 0l3.659-1.83A1 1 0 0 1 21 4.619v12.764a1 1 0 0 1-.553.894l-4.553 2.277a2 2 0 0 1-1.788 0l-4.212-2.106a2 2 0 0 0-1.788 0l-3.659 1.83A1 1 0 0 1 3 19.381V6.618a1 1 0 0 1 .553-.894l4.553-2.277a2 2 0 0 1 1.788 0z",
               "M15 5.764v15", "M9 3.236v15"});
        return;
    }
    if (Name == "Boxes")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M2.97 12.92A2 2 0 0 0 2 14.63v3.24a2 2 0 0 0 .97 1.71l3 1.8a2 2 0 0 0 2.06 0L12 19v-5.5l-5-3-4.03 2.42Z",
               "m7 16.5-4.74-2.85", "m7 16.5 5-3", "M7 16.5v5.17",
               "M12 13.5V19l3.97 2.38a2 2 0 0 0 2.06 0l3-1.8a2 2 0 0 0 .97-1.71v-3.24a2 2 0 0 0-.97-1.71L17 10.5l-5 3Z",
               "m17 16.5-5-3", "m17 16.5 4.74-2.85", "M17 16.5v5.17",
               "M7.97 4.42A2 2 0 0 0 7 6.13v4.37l5 3 5-3V6.13a2 2 0 0 0-.97-1.71l-3-1.8a2 2 0 0 0-2.06 0l-3 1.8Z",
               "M12 8 7.26 5.15", "m12 8 4.74-2.85", "M12 13.5V8"});
        return;
    }
    if (Name == "Activity")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M22 12h-2.48a2 2 0 0 0-1.93 1.46l-2.35 8.36a.25.25 0 0 1-.48 0L9.24 2.18a.25.25 0 0 0-.48 0l-2.35 8.36A2 2 0 0 1 4.49 12H2"});
        return;
    }
    if (Name == "CircleDot")
    {
        DrawCircle(Ink, Centre, Size * 10.0f / 24.0f, WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f);
        DrawCircle(Ink, Centre, Size / 24.0f, WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f);
        return;
    }
    if (Name == "Gem")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M10.5 3 8 9l4 13 4-13-2.5-6", "M17 3a2 2 0 0 1 1.6.8l3 4a2 2 0 0 1 .013 2.382l-7.99 10.986a2 2 0 0 1-3.247 0l-7.99-10.986A2 2 0 0 1 2.4 7.8l2.998-3.997A2 2 0 0 1 7 3z", "M2 9h20"});
        return;
    }
    if (Name == "TrafficCone")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M16.05 10.966a5 2.5 0 0 1-8.1 0",
               "m16.923 14.049 4.48 2.04a1 1 0 0 1 .001 1.831l-8.574 3.9a2 2 0 0 1-1.66 0l-8.574-3.91a1 1 0 0 1 0-1.83l4.484-2.04",
               "M16.949 14.14a5 2.5 0 1 1-9.9 0L10.063 3.5a2 2 0 0 1 3.874 0z",
               "M9.194 6.57a5 2.5 0 0 0 5.61 0"});
        return;
    }
    if (Name == "Flag")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"M4 15s1-1 4-1 5 2 8 2 4-1 4-1V3s-1 1-4 1-5-2-8-2-4 1-4 1z", "M4 22V15"});
        return;
    }
    if (Name == "Check")
    {
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees, {"M20 6 9 17l-5-5"});
        return;
    }
    if (Name == "Aperture")
    {
        DrawCircle(Ink, Centre, Size * 10.0f / 24.0f, WithOpacity(Colour, Opacity), StrokeWidth * Size / 24.0f);
        Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
              {"m14.31 8 5.74 9.94", "M9.69 8h11.48", "m7.38 12 5.74-9.94",
               "M9.69 16 3.95 6.06", "M14.31 16H2.83", "m16.62 12-5.74 9.94"});
        return;
    }

    // Unknown editor-authored glyph name: retain a meaningful Lucide object icon, never a generic kit drawing.
    Paths(Ink, Centre, Size, Colour, StrokeWidth, Opacity, RotationDegrees,
          {"M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z",
           "m3.3 7 8.7 5 8.7-5", "M12 22V12"});
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
