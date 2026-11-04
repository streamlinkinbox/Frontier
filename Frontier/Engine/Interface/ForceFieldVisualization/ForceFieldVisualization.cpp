/*==========================================================================================================================================
                                                       FORCEFIELDVISUALIZATION.CPP
==========================================================================================================================================*/
// 🧩 The prototype's slice renderer on ImDrawList: a vertex-coloured heatmap, eos arrows, tracers, source outlines, a legend, a
//    scale bar and a minimap, with the lattice sampled off the interface thread.

#include "Engine/Interface/ForceFieldVisualization/ForceFieldVisualization.h"

#include "Engine/Interface/Controls/DiagramPen.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

namespace
{

constexpr double π = 3.14159265358979323846;

// 📝 The eos Force Field Debugger's colour stops, interpolated into a 256-entry table.
constexpr uint8_t ColourStops[5][3] = {{5, 18, 81}, {0, 112, 241}, {0, 224, 210}, {235, 239, 39}, {239, 60, 18}};

struct ColourTable
{
    uint8_t Entries[256][3]{};

    ColourTable() noexcept
    {
        for (int Index = 0; Index < 256; ++Index)
        {
            const double Share = Index / 255.0 * 4.0;
            const int    Lower = std::min(3, static_cast<int>(std::floor(Share)));
            const double Blend = Share - Lower;
            for (int Channel = 0; Channel < 3; ++Channel)
                Entries[Index][Channel] = static_cast<uint8_t>(std::clamp(
                    std::lround(ColourStops[Lower][Channel] + (ColourStops[Lower + 1][Channel] - ColourStops[Lower][Channel]) * Blend), 0L, 255L));
        }
    }
};

const ColourTable Colours{};

ImU32 ContributionColour(ForceContribution Contribution, float Alpha) noexcept
{
    const int Value = Contribution == ForceContribution::Accelerate ? 0xE8C27A : Contribution == ForceContribution::Damp ? 0xB9A3E8 : 0x8FD3C4;
    return IM_COL32((Value >> 16) & 255, (Value >> 8) & 255, Value & 255, static_cast<int>(std::clamp(Alpha, 0.0f, 1.0f) * 255.0f));
}

ImU32 WithAlpha(ImU32 Colour, float Alpha) noexcept
{
    const uint32_t Base = (Colour >> IM_COL32_A_SHIFT) & 255u;
    return (Colour & ~IM_COL32_A_MASK) | (static_cast<uint32_t>(std::clamp(Base * Alpha, 0.0f, 255.0f)) << IM_COL32_A_SHIFT);
}

void RoundedPanel(ImDrawList* Ink, ImVec2 Minimum, ImVec2 Size, float Scale)
{
    Ink->AddRectFilled(Minimum, ImVec2(Minimum.x + Size.x, Minimum.y + Size.y), IM_COL32(10, 10, 10, 184), 10.0f * Scale);
    Ink->AddRect(Minimum, ImVec2(Minimum.x + Size.x, Minimum.y + Size.y), IM_COL32(255, 255, 255, 18), 10.0f * Scale, 0, Scale);
}

void Text(ImDrawList* Ink, ImFont* Face, float Size, ImVec2 Position, ImU32 Colour, const char* Caption)
{
    Ink->AddText(Face, Size, Position, Colour, Caption);
}

float Measure(ImFont* Face, float Size, const char* Caption)
{
    if (Face == nullptr) return ImGui::CalcTextSize(Caption).x * Size / std::max(1.0f, ImGui::GetFontSize());
    return Face->CalcTextSizeA(Size, 1.0e6f, 0.0f, Caption).x;
}

double Wrap360(double Degrees) noexcept
{
    return std::fmod(std::fmod(Degrees, 360.0) + 360.0, 360.0);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHARED HELPERS
//------------------------------------------------------------------------------------------------------------------------

ImU32 ForceFieldVisualization::ResolveColour(double Share, float Alpha) noexcept
{
    const int Index = std::clamp(static_cast<int>(std::lround(Share * 255.0)), 0, 255);
    return IM_COL32(Colours.Entries[Index][0], Colours.Entries[Index][1], Colours.Entries[Index][2], static_cast<int>(std::clamp(Alpha, 0.0f, 1.0f) * 255.0f));
}

const char* ForceFieldVisualization::ResolveUnit(ForceContribution Contribution, ForceFieldQuantity Quantity) noexcept
{
    if (Contribution == ForceContribution::Damp) return "1/s";
    if (Quantity == ForceFieldQuantity::Vorticity || Quantity == ForceFieldQuantity::Divergence)
        return Contribution == ForceContribution::Flow ? "1/s" : "1/s\xC2\xB2";
    return Contribution == ForceContribution::Accelerate ? "m/s\xC2\xB2" : "m/s";
}

const char* ForceFieldVisualization::ResolveQuantityLabel(ForceFieldQuantity Quantity) noexcept
{
    switch (Quantity)
    {
    case ForceFieldQuantity::AlongX: return "East \xC2\xB7 X";
    case ForceFieldQuantity::AlongZ: return "South \xC2\xB7 Z";
    case ForceFieldQuantity::Vertical: return "Vertical \xC2\xB7 Y";
    case ForceFieldQuantity::Vorticity: return "Vorticity";
    case ForceFieldQuantity::Divergence: return "Divergence";
    default: return "Horizontal speed";
    }
}

bool ForceFieldVisualization::QuerySigned(ForceFieldQuantity Quantity) noexcept
{
    return Quantity != ForceFieldQuantity::Speed;
}

void ForceFieldVisualization::FormatDistance(double Metres, char* Text, size_t Capacity) noexcept
{
    const double Magnitude = std::abs(Metres);
    if (Magnitude < 1e-6)
    {
        std::snprintf(Text, Capacity, "0 m");
        return;
    }
    if (Magnitude >= 1000.0)
    {
        std::snprintf(Text, Capacity, Magnitude >= 10000.0 ? "%.0f km" : Magnitude >= 2000.0 ? "%.1f km" : "%.2f km", Metres / 1000.0);
        // 📝 "2.0 km" reads as "2 km", as the prototype's replace(/\.0+$/).
        if (char* Point = std::strchr(Text, '.'))
        {
            char* Unit = std::strchr(Point, ' ');
            char* Last = Unit - 1;
            while (Last > Point && *Last == '0') --Last;
            if (Last == Point) --Last;
            std::memmove(Last + 1, Unit, std::strlen(Unit) + 1);
        }
        return;
    }
    if (Magnitude >= 10.0) std::snprintf(Text, Capacity, "%.0f m", Metres);
    else if (Magnitude >= 1.0)
    {
        std::snprintf(Text, Capacity, "%.1f m", Metres);
        if (char* Point = std::strstr(Text, ".0 ")) std::memmove(Point, Point + 2, std::strlen(Point + 2) + 1);
    }
    else std::snprintf(Text, Capacity, "%.0f cm", Metres * 100.0);
}

void ForceFieldVisualization::FormatReading(double Reading, char* Text, size_t Capacity) noexcept
{
    const double Magnitude = std::abs(Reading);
    if (Magnitude == 0.0) std::snprintf(Text, Capacity, "0");
    else if (Magnitude >= 100.0) std::snprintf(Text, Capacity, "%.0f", Reading);
    else if (Magnitude >= 10.0) std::snprintf(Text, Capacity, "%.1f", Reading);
    else if (Magnitude >= 1.0) std::snprintf(Text, Capacity, "%.2f", Reading);
    else if (Magnitude >= 0.01) std::snprintf(Text, Capacity, "%.3f", Reading);
    else std::snprintf(Text, Capacity, "%.1e", Reading);
}

// 📝 The largest 1 · 2 · 5 × 10ᵏ that does not exceed the target (scale bars, ground grid).
double ForceFieldVisualization::ResolveNiceLength(double Target) noexcept
{
    const double Decade = std::pow(10.0, std::floor(std::log10(std::max(1e-9, Target))));
    for (const double Step : {5.0, 2.0, 1.0})
        if (Step * Decade <= Target) return Step * Decade;
    return Decade;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIFETIME AND FIELD
//------------------------------------------------------------------------------------------------------------------------

ForceFieldVisualization::ForceFieldVisualization() = default;

ForceFieldVisualization::~ForceFieldVisualization()
{
    if (Pending_.valid()) Pending_.wait();
}

void ForceFieldVisualization::ConfigureField(const ForceFieldSpace& Field, uint64_t Revision)
{
    // 📝 The copy is taken every call (the timeline moves the sources); the revision only decides whether to re-sample now.
    Field_ = std::make_shared<const ForceFieldSpace>(Field);
    if (Revision != FieldRevision_)
    {
        FieldRevision_ = Revision;
        Dirty_         = true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROJECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Map and Oblique are affine on the slice plane: Screen = Linear · (X − CentreX, Z − CentreZ) + (W/2, H/2), with heights
//    above the slice lifted by cos(pitch) in Oblique.
ForceFieldVisualization::Affine ForceFieldVisualization::ResolveAffine() const noexcept
{
    const double PixelsPerMetre = Width_ / std::max(1e-6, View.Span);
    if (View.Mode == FieldViewMode::Map) return {PixelsPerMetre, 0.0, 0.0, PixelsPerMetre, 0.0, PixelsPerMetre};
    const double Cosine = std::cos(View.Yaw), Sine = std::sin(View.Yaw), Tilt = std::sin(View.Pitch);
    return {PixelsPerMetre * Cosine, -PixelsPerMetre * Sine, PixelsPerMetre * Sine * Tilt, PixelsPerMetre * Cosine * Tilt,
            PixelsPerMetre * std::cos(View.Pitch), PixelsPerMetre};
}

bool ForceFieldVisualization::Project(double X, double Y, double Z, ImVec2& OutScreen) const
{
    if (View.Mode == FieldViewMode::Camera)
    {
        // 📝 Field axes to the editor's z-up world: (X, −Z, Y).
        const FieldCamera& Camera   = View.Camera;
        const double       World[3] = {X - Camera.Eye[0], -Z - Camera.Eye[1], Y - Camera.Eye[2]};
        double             Depth = 0, Across = 0, Vertical = 0;
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            Depth += World[Axis] * Camera.Forward[Axis];
            Across += World[Axis] * Camera.Right[Axis];
            Vertical += World[Axis] * Camera.Up[Axis];
        }
        if (!std::isfinite(Depth) || Depth < 0.1) return false;
        const double Tangent = std::tan(std::clamp(Camera.VerticalFieldOfView, 0.05, 3.0) * 0.5);
        const double Aspect  = Width_ / std::max(1.0f, Height_);
        const double NdcX    = Across / (Depth * Tangent * Aspect);
        const double NdcY    = Vertical / (Depth * Tangent);
        if (!std::isfinite(NdcX) || !std::isfinite(NdcY) || std::abs(NdcX) > 12.0 || std::abs(NdcY) > 12.0) return false;
        OutScreen = ImVec2(Origin_.x + static_cast<float>((0.5 + NdcX * 0.5) * Width_), Origin_.y + static_cast<float>((0.5 - NdcY * 0.5) * Height_));
        return true;
    }
    const Affine Linear = ResolveAffine();
    const double DeltaX = X - View.CentreX, DeltaZ = Z - View.CentreZ;
    OutScreen = ImVec2(Origin_.x + static_cast<float>(Width_ / 2.0 + Linear.A * DeltaX + Linear.B * DeltaZ),
                       Origin_.y + static_cast<float>(Height_ / 2.0 + Linear.C * DeltaX + Linear.D * DeltaZ - Linear.Lift * (Y - Options.SliceHeight)));
    return true;
}

// 📝 Screen → slice plane. Camera mode casts the pixel's ray from the eye and meets the slice height.
bool ForceFieldVisualization::ScreenToPlane(ImVec2 Screen, double& OutX, double& OutZ) const
{
    const double LocalX = Screen.x - Origin_.x, LocalY = Screen.y - Origin_.y;
    if (View.Mode == FieldViewMode::Camera)
    {
        const FieldCamera& Camera  = View.Camera;
        const double       Tangent = std::tan(std::clamp(Camera.VerticalFieldOfView, 0.05, 3.0) * 0.5);
        const double       Aspect  = Width_ / std::max(1.0f, Height_);
        const double       NdcX = LocalX / Width_ * 2.0 - 1.0, NdcY = 1.0 - LocalY / Height_ * 2.0;
        double             Ray[3];
        for (size_t Axis = 0; Axis < 3; ++Axis)
            Ray[Axis] = Camera.Forward[Axis] + Camera.Right[Axis] * NdcX * Tangent * Aspect + Camera.Up[Axis] * NdcY * Tangent;
        if (std::abs(Ray[2]) < 1e-9) return false;
        const double Parameter = (Options.SliceHeight - Camera.Eye[2]) / Ray[2];
        if (Parameter < 0.0) return false;
        OutX = Camera.Eye[0] + Ray[0] * Parameter;
        OutZ = -(Camera.Eye[1] + Ray[1] * Parameter);
        return true;
    }
    const Affine Linear      = ResolveAffine();
    const double Determinant = Linear.A * Linear.D - Linear.B * Linear.C;
    if (std::abs(Determinant) < 1e-12) return false;
    const double OffsetX = LocalX - Width_ / 2.0, OffsetY = LocalY - Height_ / 2.0;
    OutX = View.CentreX + (Linear.D * OffsetX - Linear.B * OffsetY) / Determinant;
    OutZ = View.CentreZ + (-Linear.C * OffsetX + Linear.A * OffsetY) / Determinant;
    return true;
}

// 📝 The world rectangle the grid must cover: the view (Map), the inverse of the screen corners (Oblique, a plate of 0.84
//    span a side) or, for the camera, the slice under the screen out to a distance that follows the eye's height.
ForceFieldVisualization::Coverage ForceFieldVisualization::ResolveCoverage() const
{
    Coverage Area{1e300, 1e300, -1e300, -1e300};
    if (View.Mode == FieldViewMode::Camera)
    {
        if (Options.HasExtent && Options.ClipToExtent) return {Options.Extent[0], Options.Extent[1], Options.Extent[2], Options.Extent[3]};
        const FieldCamera& Camera = View.Camera;
        const double       Above  = std::max(2.0, std::abs(Camera.Eye[2] - Options.SliceHeight));
        const double       Reach  = std::clamp(Above * 14.0, 120.0, 30000.0);
        const double       EyeX = Camera.Eye[0], EyeZ = -Camera.Eye[1];
        const double       Tangent = std::tan(std::clamp(Camera.VerticalFieldOfView, 0.05, 3.0) * 0.5);
        const double       Aspect  = Width_ / std::max(1.0f, Height_);
        for (int Row = 0; Row <= 4; ++Row)
            for (int Column = 0; Column <= 4; ++Column)
            {
                const ImVec2 Screen(Origin_.x + Width_ * Column / 4.0f, Origin_.y + Height_ * Row / 4.0f);
                double       X = 0, Z = 0;
                if (!ScreenToPlane(Screen, X, Z))
                {
                    // 📝 Above the horizon: the ray's ground direction at the reach.
                    const double NdcX   = Column / 2.0 - 1.0;
                    const double RayX   = Camera.Forward[0] + Camera.Right[0] * NdcX * Tangent * Aspect;
                    const double RayY   = Camera.Forward[1] + Camera.Right[1] * NdcX * Tangent * Aspect;
                    const double Length = std::max(1e-9, std::hypot(RayX, RayY));
                    X = EyeX + RayX / Length * Reach;
                    Z = EyeZ - RayY / Length * Reach;
                }
                const double DeltaX = std::clamp(X - EyeX, -Reach, Reach), DeltaZ = std::clamp(Z - EyeZ, -Reach, Reach);
                Area.MinimumX = std::min(Area.MinimumX, EyeX + DeltaX);
                Area.MaximumX = std::max(Area.MaximumX, EyeX + DeltaX);
                Area.MinimumZ = std::min(Area.MinimumZ, EyeZ + DeltaZ);
                Area.MaximumZ = std::max(Area.MaximumZ, EyeZ + DeltaZ);
            }
        if (Area.MaximumX <= Area.MinimumX || Area.MaximumZ <= Area.MinimumZ) return {EyeX - 50, EyeZ - 50, EyeX + 50, EyeZ + 50};
        return Area;
    }
    const ImVec2 Corners[4] = {Origin_, ImVec2(Origin_.x + Width_, Origin_.y), ImVec2(Origin_.x, Origin_.y + Height_),
                               ImVec2(Origin_.x + Width_, Origin_.y + Height_)};
    for (const ImVec2& Corner : Corners)
    {
        double X = 0, Z = 0;
        if (!ScreenToPlane(Corner, X, Z)) continue;
        Area.MinimumX = std::min(Area.MinimumX, X);
        Area.MaximumX = std::max(Area.MaximumX, X);
        Area.MinimumZ = std::min(Area.MinimumZ, Z);
        Area.MaximumZ = std::max(Area.MaximumZ, Z);
    }
    const double Limit = View.Mode == FieldViewMode::Oblique ? View.Span * 0.42 : View.Span * 3.0;
    Area.MinimumX      = std::max(Area.MinimumX, View.CentreX - Limit);
    Area.MaximumX      = std::min(Area.MaximumX, View.CentreX + Limit);
    Area.MinimumZ      = std::max(Area.MinimumZ, View.CentreZ - Limit);
    Area.MaximumZ      = std::min(Area.MaximumZ, View.CentreZ + Limit);
    if (Options.ClipToExtent && Options.HasExtent)
    {
        Area.MinimumX = std::max(Area.MinimumX, Options.Extent[0]);
        Area.MaximumX = std::min(Area.MaximumX, Options.Extent[2]);
        Area.MinimumZ = std::max(Area.MinimumZ, Options.Extent[1]);
        Area.MaximumZ = std::min(Area.MaximumZ, Options.Extent[3]);
    }
    if (Area.MaximumX <= Area.MinimumX || Area.MaximumZ <= Area.MinimumZ)
        return {View.CentreX - View.Span / 2, View.CentreZ - View.Span / 2, View.CentreX + View.Span / 2, View.CentreZ + View.Span / 2};
    return Area;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SAMPLING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Level of detail: the cell span is the power of two (in metres) nearest CellPixels on screen, so the grid snaps to the
//    world and panning never shimmers; each halving of zoom is one level, like a clipmap cascade.
double ForceFieldVisualization::ResolveCellSpan() const
{
    if (View.Mode == FieldViewMode::Camera)
    {
        const Coverage Area = ResolveCoverage();
        return std::max(Area.MaximumX - Area.MinimumX, Area.MaximumZ - Area.MinimumZ) / std::max(4u, Options.CameraCells);
    }
    const double Wanted = View.Span / std::max(1.0f, Width_) * Options.CellPixels;
    return std::pow(2.0, std::round(std::log2(std::max(1e-4, Wanted))));
}

std::string ForceFieldVisualization::ResolveStamp() const
{
    char               Stamp[512];
    const FieldCamera& Camera = View.Camera;
    const bool         Lens   = View.Mode == FieldViewMode::Camera;
    std::snprintf(Stamp, sizeof(Stamp), "%d|%.6f|%.6f|%.6f|%.5f|%.5f|%.0f|%.0f|%d|%.4f|%.2f|%u|%d|%d|%.3f,%.3f,%.3f|%.4f,%.4f,%.4f",
                  static_cast<int>(View.Mode), View.CentreX, View.CentreZ, View.Span, View.Yaw, View.Pitch, Width_, Height_,
                  static_cast<int>(Options.Contribution), Options.SliceHeight, Options.CellPixels, Options.CameraCells, Options.ClipToExtent ? 1 : 0,
                  Options.Solo, Lens ? Camera.Eye[0] : 0.0, Lens ? Camera.Eye[1] : 0.0, Lens ? Camera.Eye[2] : 0.0, Lens ? Camera.Forward[0] : 0.0,
                  Lens ? Camera.Forward[1] : 0.0, Lens ? Camera.Forward[2] : 0.0);
    return Stamp;
}

std::shared_ptr<ForceFieldVisualization::Lattice> ForceFieldVisualization::SampleLattice(std::shared_ptr<const ForceFieldSpace> Field,
                                                                                         LatticeRequest                         Request)
{
    const auto Started = std::chrono::steady_clock::now();
    auto       Result  = std::make_shared<Lattice>();
    Result->Columns  = Request.Columns;
    Result->Rows     = Request.Rows;
    Result->OriginX  = Request.OriginX;
    Result->OriginZ  = Request.OriginZ;
    Result->CellSpan = Request.CellSpan;
    Result->Time     = Request.Time;
    const size_t Count = static_cast<size_t>(Request.Columns) * Request.Rows;
    Result->Vectors.assign(Count * 3, 0.0f);
    Result->Inside.assign(Count, 1);
    if (Field)
        for (uint32_t Row = 0; Row < Request.Rows; ++Row)
        {
            const double Z = Request.OriginZ + Row * Request.CellSpan;
            for (uint32_t Column = 0; Column < Request.Columns; ++Column)
            {
                const double        X     = Request.OriginX + Column * Request.CellSpan;
                const SpatialVector Value = Field->EvaluateContribution(Request.Contribution, {X, Request.SliceY, Z}, Request.CellSpan, true, Request.Solo);
                const size_t        Index = static_cast<size_t>(Row) * Request.Columns + Column;
                Result->Vectors[Index * 3]     = static_cast<float>(Value.X);
                Result->Vectors[Index * 3 + 1] = static_cast<float>(Value.Y);
                Result->Vectors[Index * 3 + 2] = static_cast<float>(Value.Z);
            }
        }
    Result->Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
    return Result;
}

void ForceFieldVisualization::AcceptLattice(std::shared_ptr<Lattice> Finished)
{
    DeriveQuantity(*Finished);
    Lattice_                 = std::move(Finished);
    LatticeStamp_            = PendingStamp_;
    Statistics_.Samples      = Lattice_->Columns * Lattice_->Rows;
    Statistics_.Milliseconds = Lattice_->Milliseconds;
    Statistics_.CellSpan     = Lattice_->CellSpan;
    Statistics_.Columns      = Lattice_->Columns;
    Statistics_.Rows         = Lattice_->Rows;
    ++Statistics_.Refreshes;
}

void ForceFieldVisualization::DeriveQuantity(Lattice& Target)
{
    const uint32_t Columns = Target.Columns, Rows = Target.Rows;
    const size_t   Count   = static_cast<size_t>(Columns) * Rows;
    Target.Quantity.assign(Count, 0.0f);
    const bool                Damp     = Options.Contribution == ForceContribution::Damp;
    const ForceFieldQuantity  Mode     = Damp ? ForceFieldQuantity::Speed : Options.Quantity;
    const double              Inverse2 = 1.0 / (2.0 * Target.CellSpan);
    const std::vector<float>& Vectors  = Target.Vectors;
    double                    Peak     = 0.0;
    for (uint32_t Row = 0; Row < Rows; ++Row)
        for (uint32_t Column = 0; Column < Columns; ++Column)
        {
            const size_t Index = static_cast<size_t>(Row) * Columns + Column;
            const double VX = Vectors[Index * 3], VY = Vectors[Index * 3 + 1], VZ = Vectors[Index * 3 + 2];
            double       Reading = 0.0;
            if (Damp) Reading = VX;
            else if (Mode == ForceFieldQuantity::Speed) Reading = std::sqrt(VX * VX + VZ * VZ);
            else if (Mode == ForceFieldQuantity::AlongX) Reading = VX;
            else if (Mode == ForceFieldQuantity::AlongZ) Reading = VZ;
            else if (Mode == ForceFieldQuantity::Vertical) Reading = VY;
            else
            {
                const size_t Left = Column > 0 ? Index - 1 : Index, Right = Column + 1 < Columns ? Index + 1 : Index;
                const size_t Up = Row > 0 ? Index - Columns : Index, Down = Row + 1 < Rows ? Index + Columns : Index;
                const double SpanX  = Right > Left ? static_cast<double>(Right - Left) : 1.0;
                const double SpanZ  = (Down - Up) / Columns > 0 ? static_cast<double>((Down - Up) / Columns) : 1.0;
                const double ScaleX = 2.0 / SpanX * Inverse2, ScaleZ = 2.0 / SpanZ * Inverse2;
                const double DvxDz  = (Vectors[Down * 3] - Vectors[Up * 3]) * ScaleZ;
                const double DvzDx  = (Vectors[Right * 3 + 2] - Vectors[Left * 3 + 2]) * ScaleX;
                if (Mode == ForceFieldQuantity::Vorticity) Reading = DvxDz - DvzDx;
                else Reading = (Vectors[Right * 3] - Vectors[Left * 3]) * ScaleX + (Vectors[Down * 3 + 2] - Vectors[Up * 3 + 2]) * ScaleZ;
            }
            Target.Quantity[Index] = static_cast<float>(Reading);
            if (Target.Inside[Index]) Peak = std::max(Peak, std::abs(Reading));
        }
    Statistics_.Peak = Peak;
    Unit_            = ResolveUnit(Options.Contribution, Mode);

    // 📝 The rising / sinking markers compare each cell with the view's median vertical reading, so a uniform pull (gravity
    //    everywhere) marks nothing while a lift column or a downdraft still stands out.
    std::vector<float> Vertical;
    const size_t       Stride = std::max<size_t>(1, Count / 400);
    for (size_t Index = 0; Index < Count; Index += Stride)
        if (Target.Inside[Index]) Vertical.push_back(Vectors[Index * 3 + 1]);
    std::sort(Vertical.begin(), Vertical.end());
    Target.MedianVertical = Vertical.empty() ? 0.0 : Vertical[Vertical.size() >> 1];

    // 📝 Automatic scale: the 98th percentile of |reading| (a 256-bin histogram), eased between refreshes and snapped when the
    //    view jumps by more than ×4, so the legend does not flicker while the field animates.
    double Target98 = Options.FixedScale;
    if (Options.AutomaticScale)
    {
        uint32_t Bins[256]{};
        uint32_t Counted = 0;
        for (size_t Index = 0; Index < Count; ++Index)
        {
            if (!Target.Inside[Index]) continue;
            ++Bins[std::min<size_t>(255, static_cast<size_t>(std::floor(std::abs(Target.Quantity[Index]) / (Peak > 0 ? Peak : 1.0) * 255.0)))];
            ++Counted;
        }
        uint32_t Running = 0, Bin = 255;
        for (uint32_t Index = 0; Index < 256; ++Index)
        {
            Running += Bins[Index];
            if (Running >= Counted * 0.98)
            {
                Bin = Index;
                break;
            }
        }
        Target98 = std::max({Peak * (Bin + 1) / 256.0, Peak * 0.25, 1e-6});
        if (Peak <= 1e-9) Target98 = ScaleReading_ > 0 ? ScaleReading_ : 1.0;
    }
    if (ScaleReading_ <= 0 || !Options.AutomaticScale || Target98 > ScaleReading_ * 4 || Target98 < ScaleReading_ / 4) ScaleReading_ = Target98;
    else ScaleReading_ += (Target98 - ScaleReading_) * 0.3;
    Statistics_.Scale = ScaleReading_;

    // 📝 The colours: signed quantities centre on the middle of the scale.
    const bool   Signed       = !Damp && QuerySigned(Options.Quantity);
    const double InverseScale = 1.0 / (ScaleReading_ > 0 ? ScaleReading_ : 1.0);
    const float  Alpha        = std::clamp(Options.Opacity, 0.0f, 1.0f);
    Target.Colours.resize(Count);
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const double Share = Signed ? 0.5 + 0.5 * Target.Quantity[Index] * InverseScale : Target.Quantity[Index] * InverseScale;
        Target.Colours[Index] = ResolveColour(Share, Target.Inside[Index] ? Alpha : 0.0f);
    }
}

// 📝 Bilinear read of the sampled lattice: arrows and tracers use the grid the heatmap shows, so the three agree.
bool ForceFieldVisualization::Interpolate(double X, double Z, double Out[3]) const
{
    if (!Lattice_ || Lattice_->Columns < 2 || Lattice_->Rows < 2) return false;
    const Lattice& Grid = *Lattice_;
    const double   U = (X - Grid.OriginX) / Grid.CellSpan, V = (Z - Grid.OriginZ) / Grid.CellSpan;
    if (U < 0 || V < 0 || U > Grid.Columns - 1 || V > Grid.Rows - 1) return false;
    const uint32_t Column = std::min(Grid.Columns - 2, static_cast<uint32_t>(std::floor(U)));
    const uint32_t Row    = std::min(Grid.Rows - 2, static_cast<uint32_t>(std::floor(V)));
    const double   S = U - Column, T = V - Row;
    const size_t   I00 = static_cast<size_t>(Row) * Grid.Columns + Column, I10 = I00 + 1, I01 = I00 + Grid.Columns, I11 = I01 + 1;
    if (!(Grid.Inside[I00] | Grid.Inside[I10] | Grid.Inside[I01] | Grid.Inside[I11])) return false;
    for (size_t Channel = 0; Channel < 3; ++Channel)
        Out[Channel] = (Grid.Vectors[I00 * 3 + Channel] * (1 - S) + Grid.Vectors[I10 * 3 + Channel] * S) * (1 - T) +
                       (Grid.Vectors[I01 * 3 + Channel] * (1 - S) + Grid.Vectors[I11 * 3 + Channel] * S) * T;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HEATMAP
//------------------------------------------------------------------------------------------------------------------------

void ForceFieldVisualization::RenderHeatmap(ImDrawList* Ink) const
{
    if (!Options.Heatmap || !Lattice_ || Lattice_->Colours.empty()) return;
    const Lattice& Grid  = *Lattice_;
    const ImVec2   White = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    if (View.Mode != FieldViewMode::Camera && Options.SmoothHeatmap)
    {
        // 📝 Smooth: a vertex per lattice point, colours blended across each cell (the prototype's smoothed image).
        std::vector<ImVec2> Upper(Grid.Columns), Lower(Grid.Columns);
        for (uint32_t Column = 0; Column < Grid.Columns; ++Column)
            (void)Project(Grid.OriginX + Column * Grid.CellSpan, Options.SliceHeight, Grid.OriginZ, Upper[Column]);
        for (uint32_t Row = 0; Row + 1 < Grid.Rows; ++Row)
        {
            for (uint32_t Column = 0; Column < Grid.Columns; ++Column)
                (void)Project(Grid.OriginX + Column * Grid.CellSpan, Options.SliceHeight, Grid.OriginZ + (Row + 1) * Grid.CellSpan, Lower[Column]);
            Ink->PrimReserve(static_cast<int>(6 * (Grid.Columns - 1)), static_cast<int>(2 * Grid.Columns));
            const ImDrawIdx Base = static_cast<ImDrawIdx>(Ink->_VtxCurrentIdx);
            for (uint32_t Column = 0; Column < Grid.Columns; ++Column)
            {
                Ink->PrimWriteVtx(Upper[Column], White, Grid.Colours[static_cast<size_t>(Row) * Grid.Columns + Column]);
                Ink->PrimWriteVtx(Lower[Column], White, Grid.Colours[static_cast<size_t>(Row + 1) * Grid.Columns + Column]);
            }
            for (uint32_t Column = 0; Column + 1 < Grid.Columns; ++Column)
            {
                const ImDrawIdx A = static_cast<ImDrawIdx>(Base + 2 * Column), B = static_cast<ImDrawIdx>(A + 1), C = static_cast<ImDrawIdx>(A + 2),
                                D = static_cast<ImDrawIdx>(A + 3);
                Ink->PrimWriteIdx(A);
                Ink->PrimWriteIdx(C);
                Ink->PrimWriteIdx(D);
                Ink->PrimWriteIdx(A);
                Ink->PrimWriteIdx(D);
                Ink->PrimWriteIdx(B);
            }
            std::swap(Upper, Lower);
        }
        return;
    }
    // 📝 Cells (eos) and the camera: each lattice point fills its projected cell with one colour.
    const double Half = Grid.CellSpan / 2.0;
    for (uint32_t Row = 0; Row < Grid.Rows; ++Row)
        for (uint32_t Column = 0; Column < Grid.Columns; ++Column)
        {
            const size_t Index = static_cast<size_t>(Row) * Grid.Columns + Column;
            if (((Grid.Colours[Index] >> IM_COL32_A_SHIFT) & 255u) == 0u) continue;
            const double X = Grid.OriginX + Column * Grid.CellSpan, Z = Grid.OriginZ + Row * Grid.CellSpan;
            ImVec2       Corner[4];
            if (!Project(X - Half, Options.SliceHeight, Z - Half, Corner[0]) || !Project(X + Half, Options.SliceHeight, Z - Half, Corner[1]) ||
                !Project(X + Half, Options.SliceHeight, Z + Half, Corner[2]) || !Project(X - Half, Options.SliceHeight, Z + Half, Corner[3]))
                continue;
            Ink->PrimReserve(6, 4);
            const ImDrawIdx Base = static_cast<ImDrawIdx>(Ink->_VtxCurrentIdx);
            for (const ImVec2& Point : Corner) Ink->PrimWriteVtx(Point, White, Grid.Colours[Index]);
            Ink->PrimWriteIdx(Base);
            Ink->PrimWriteIdx(static_cast<ImDrawIdx>(Base + 1));
            Ink->PrimWriteIdx(static_cast<ImDrawIdx>(Base + 2));
            Ink->PrimWriteIdx(Base);
            Ink->PrimWriteIdx(static_cast<ImDrawIdx>(Base + 2));
            Ink->PrimWriteIdx(static_cast<ImDrawIdx>(Base + 3));
        }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GROUND AND CASCADES
//------------------------------------------------------------------------------------------------------------------------

void ForceFieldVisualization::StrokeWorldPath(ImDrawList* Ink, const std::vector<std::array<double, 3>>& Points, bool Close, ImU32 Colour,
                                              float Width, float DashOn, float DashOff) const
{
    std::vector<ImVec2> Screen;
    auto Flush = [&]() {
        if (Screen.size() >= 2)
        {
            if (DashOn > 0.0f && DashOff > 0.0f)
                for (size_t Index = 0; Index + 1 < Screen.size(); ++Index)
                    DiagramPen::RasterizeDashedSegment(Ink, Screen[Index], Screen[Index + 1], Colour, Width, DashOn, DashOff);
            else
                Ink->AddPolyline(Screen.data(), static_cast<int>(Screen.size()), Colour, 0, Width);
        }
        Screen.clear();
    };
    bool Complete = true;
    for (const auto& Point : Points)
    {
        ImVec2 Projected;
        if (!Project(Point[0], Point[1], Point[2], Projected))
        {
            Complete = false;
            Flush();
            continue;
        }
        Screen.push_back(Projected);
    }
    if (Close && Complete && !Screen.empty()) Screen.push_back(Screen.front());
    Flush();
}

void ForceFieldVisualization::RenderGroundGrid(ImDrawList* Ink) const
{
    if (!Options.ShowGroundGrid || View.Mode == FieldViewMode::Camera) return;
    const Coverage Area   = ResolveCoverage();
    const double   Step   = ResolveNiceLength(View.Span / 6.0);
    const double   SliceY = Options.SliceHeight;
    const ImU32    Faint  = IM_COL32(255, 255, 255, 13);
    for (double X = std::ceil(Area.MinimumX / Step) * Step; X <= Area.MaximumX; X += Step)
        StrokeWorldPath(Ink, {{X, SliceY, Area.MinimumZ}, {X, SliceY, Area.MaximumZ}}, false, Faint, 1.0f);
    for (double Z = std::ceil(Area.MinimumZ / Step) * Step; Z <= Area.MaximumZ; Z += Step)
        StrokeWorldPath(Ink, {{Area.MinimumX, SliceY, Z}, {Area.MaximumX, SliceY, Z}}, false, Faint, 1.0f);
    if (Options.HasExtent)
    {
        const auto& E = Options.Extent;
        StrokeWorldPath(Ink, {{E[0], SliceY, E[1]}, {E[2], SliceY, E[1]}, {E[2], SliceY, E[3]}, {E[0], SliceY, E[3]}}, true, IM_COL32(159, 180, 214, 89), 1.0f,
                        6.0f, 6.0f);
    }
    if (View.Mode == FieldViewMode::Oblique)
        StrokeWorldPath(Ink, {{Area.MinimumX, SliceY, Area.MinimumZ}, {Area.MaximumX, SliceY, Area.MinimumZ}, {Area.MaximumX, SliceY, Area.MaximumZ},
                              {Area.MinimumX, SliceY, Area.MaximumZ}},
                        true, IM_COL32(223, 231, 243, 140), 1.2f);
}

void ForceFieldVisualization::RenderCascades(ImDrawList* Ink, const EditorTypefaces& Faces, float Scale) const
{
    if (!Options.ShowCascades || Options.Cascades.empty() || View.Mode == FieldViewMode::Camera) return;
    const double FocusX = Options.CascadeFocus[0], FocusZ = Options.CascadeFocus[1], SliceY = Options.SliceHeight;
    const double PixelsPerMetre = ResolveAffine().PixelsPerMetre;
    for (size_t Position = 0; Position < Options.Cascades.size(); ++Position)
    {
        const ForceCascadeLevel& Cascade  = Options.Cascades[Position];
        const double             Half     = Cascade.ExtentMetres / 2.0;
        const float              Strength = std::max(0.25f, 0.75f - static_cast<float>(Position) * 0.12f);
        const ImU32              Colour   = IM_COL32(223, 231, 243, static_cast<int>(Strength * 255));
        StrokeWorldPath(Ink, {{FocusX - Half, SliceY, FocusZ - Half}, {FocusX + Half, SliceY, FocusZ - Half}, {FocusX + Half, SliceY, FocusZ + Half},
                              {FocusX - Half, SliceY, FocusZ + Half}},
                        true, Colour, 1.2f * Scale, Position == 0 ? 0.0f : 7.0f * Scale, 5.0f * Scale);
        ImVec2 Corner;
        if (!Project(FocusX - Half, SliceY, FocusZ - Half, Corner)) continue;
        const double Across = Cascade.ExtentMetres * PixelsPerMetre;
        if (Across < 90 * Scale || Corner.x < Origin_.x - 200 || Corner.y < Origin_.y - 40 || Corner.x > Origin_.x + Width_ || Corner.y > Origin_.y + Height_)
            continue;
        char Extent[32], Cell[32], Label[96];
        FormatDistance(Cascade.ExtentMetres, Extent, sizeof(Extent));
        FormatDistance(Cascade.CellMetres, Cell, sizeof(Cell));
        std::snprintf(Label, sizeof(Label), "C%zu \xC2\xB7 %s \xC2\xB7 %s cells", Position, Extent, Cell);
        const float  Size = 10.5f * Scale, Wide = Measure(Faces.Medium, Size, Label);
        const ImVec2 At(std::max(Origin_.x + 8 * Scale, Corner.x + 6 * Scale), std::max(Origin_.y + 16 * Scale, Corner.y + 15 * Scale));
        Ink->AddRectFilled(ImVec2(At.x - 5 * Scale, At.y - 11 * Scale), ImVec2(At.x + Wide + 5 * Scale, At.y + 5 * Scale), IM_COL32(8, 10, 14, 199));
        Text(Ink, Faces.Medium, Size, ImVec2(At.x, At.y - Size * 0.95f), IM_COL32(223, 231, 243, 255), Label);
    }
    ImVec2 Focus;
    if (Project(FocusX, SliceY, FocusZ, Focus)) Ink->AddCircle(Focus, 4 * Scale, IM_COL32_WHITE, 16, Scale);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ARROWS
//------------------------------------------------------------------------------------------------------------------------

double ForceFieldVisualization::ResolveArrowSpan() const
{
    if (View.Mode == FieldViewMode::Camera)
    {
        const Coverage Area   = ResolveCoverage();
        const double   Across = std::max(Area.MaximumX - Area.MinimumX, Area.MaximumZ - Area.MinimumZ);
        return Across / std::max(4.0, std::round(Options.CameraCells / 3.0));
    }
    const double Wanted = View.Span / std::max(1.0f, Width_) * Options.ArrowSpacing;
    const double Cell   = Lattice_ ? Lattice_->CellSpan : 1.0;
    return Cell * std::pow(2.0, std::max(0.0, std::ceil(std::log2(Wanted / Cell))));
}

// 📝 eos arrows: white, 1 px, 5 px heads at ±0.55 rad. A cell whose vertical flow dominates shows ⊙ (rising) or ⊗ (sinking).
void ForceFieldVisualization::RenderArrows(ImDrawList* Ink, float Scale)
{
    if (!Options.Arrows || Options.Contribution == ForceContribution::Damp || !Lattice_) return;
    const double ArrowSpan = ResolveArrowSpan();
    Statistics_.ArrowSpan  = ArrowSpan;
    const Coverage Area    = ResolveCoverage();
    const double   SliceY  = Options.SliceHeight;
    const double   Reading = ScaleReading_ > 0 ? ScaleReading_ : 1.0;
    const double   StartX  = View.Mode == FieldViewMode::Camera ? Area.MinimumX + ArrowSpan / 2 : std::floor(Area.MinimumX / ArrowSpan) * ArrowSpan + ArrowSpan / 2;
    const double   StartZ  = View.Mode == FieldViewMode::Camera ? Area.MinimumZ + ArrowSpan / 2 : std::floor(Area.MinimumZ / ArrowSpan) * ArrowSpan + ArrowSpan / 2;
    struct Stroke { ImVec2 Tail, Tip, Left, Right; };
    struct Marker { ImVec2 Centre; bool Rising; };
    std::vector<Stroke> Strokes;
    std::vector<Marker> Markers;
    int                 Drawn = 0;
    for (double Z = StartZ; Z <= Area.MaximumZ && Drawn < 4000; Z += ArrowSpan)
        for (double X = StartX; X <= Area.MaximumX && Drawn < 4000; X += ArrowSpan)
        {
            double Vector[3];
            if (!Interpolate(X, Z, Vector)) continue;
            const double Horizontal = std::hypot(Vector[0], Vector[2]);
            const double Lifted     = Vector[1] - Lattice_->MedianVertical;
            const double Magnitude  = std::hypot(Horizontal, Lifted);
            if (Magnitude < Reading * 0.02) continue;
            ImVec2 Tail;
            if (!Project(X, SliceY, Z, Tail)) continue;
            if (Tail.x < Origin_.x - 40 || Tail.y < Origin_.y - 40 || Tail.x > Origin_.x + Width_ + 40 || Tail.y > Origin_.y + Height_ + 40) continue;
            if (std::abs(Lifted) > Horizontal * 1.4 && std::abs(Lifted) > Reading * 0.12)
            {
                Markers.push_back({Tail, Lifted > 0});
                ++Drawn;
                continue;
            }
            if (Horizontal < 1e-9) continue;
            const double Length = std::min(1.0, Horizontal / Reading) * ArrowSpan * 0.88;
            ImVec2       Tip;
            if (!Project(X + Vector[0] / Horizontal * Length, SliceY, Z + Vector[2] / Horizontal * Length, Tip)) continue;
            const float Angle  = std::atan2(Tip.y - Tail.y, Tip.x - Tail.x);
            const float Pixels = std::hypot(Tip.x - Tail.x, Tip.y - Tail.y);
            if (Pixels < 1.5f * Scale) continue;
            const float Head = std::min(5.0f * Scale, Pixels * 0.45f);
            Strokes.push_back({Tail, Tip, ImVec2(Tip.x - std::cos(Angle - 0.55f) * Head, Tip.y - std::sin(Angle - 0.55f) * Head),
                               ImVec2(Tip.x - std::cos(Angle + 0.55f) * Head, Tip.y - std::sin(Angle + 0.55f) * Head)});
            ++Drawn;
        }
    // 📝 A dark 2.6 px under-stroke, then the 1.1 px light stroke (the prototype's two passes over one path).
    const ImU32 Shadow = IM_COL32(0, 0, 0, 89), Light = IM_COL32(239, 255, 255, 199);
    for (const ImU32 Colour : {Shadow, Light})
    {
        const float Width = (Colour == Shadow ? 2.6f : 1.1f) * Scale;
        for (const Stroke& Arrow : Strokes)
        {
            Ink->AddLine(Arrow.Tail, Arrow.Tip, Colour, Width);
            const ImVec2 Head[3] = {Arrow.Left, Arrow.Tip, Arrow.Right};
            Ink->AddPolyline(Head, 3, Colour, 0, Width);
        }
    }
    for (const Marker& Point : Markers)
    {
        Ink->AddCircle(Point.Centre, 4 * Scale, Light, 16, 1.2f * Scale);
        if (Point.Rising) Ink->AddCircleFilled(Point.Centre, 1.3f * Scale, IM_COL32(239, 255, 255, 230), 8);
        else
        {
            const float C = 2.4f * Scale;
            Ink->AddLine(ImVec2(Point.Centre.x - C, Point.Centre.y - C), ImVec2(Point.Centre.x + C, Point.Centre.y + C), IM_COL32(239, 255, 255, 230), Scale);
            Ink->AddLine(ImVec2(Point.Centre.x + C, Point.Centre.y - C), ImVec2(Point.Centre.x - C, Point.Centre.y + C), IM_COL32(239, 255, 255, 230), Scale);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRACERS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Tracers ride the sampled lattice at a display rate (a full-scale reading crosses 7 % of the view per second), so they read
//    the same at 100 m and at 100 km; they show direction and structure, not real-time travel.
constexpr uint32_t TrailLength = 10;

void ForceFieldVisualization::ReseedTracer(Tracer& Target, const Coverage& Area, bool Scattered)
{
    auto Random = [this]() {
        RandomSeed_ = RandomSeed_ * 1664525u + 1013904223u;
        return static_cast<double>(RandomSeed_ >> 8) / 16777216.0;
    };
    Target.X     = Area.MinimumX + Random() * (Area.MaximumX - Area.MinimumX);
    Target.Z     = Area.MinimumZ + Random() * (Area.MaximumZ - Area.MinimumZ);
    Target.Age   = Scattered ? Random() * 2.5 : 0.0;
    Target.Life  = 2.2 + Random() * 2.4;
    Target.Count = 0;
    Target.Head  = 0;
}

void ForceFieldVisualization::AdvanceTracers(double Delta)
{
    if (!Options.Tracers || Options.Contribution == ForceContribution::Damp || !Lattice_) return;
    const Coverage Area = ResolveCoverage();
    while (Tracers_.size() < Options.TracerCount)
    {
        Tracers_.emplace_back();
        ReseedTracer(Tracers_.back(), Area, true);
    }
    if (Tracers_.size() > Options.TracerCount) Tracers_.resize(Options.TracerCount);
    const double Across  = std::max(Area.MaximumX - Area.MinimumX, Area.MaximumZ - Area.MinimumZ);
    const double Reading = ScaleReading_ > 0 ? ScaleReading_ : 1.0;
    const double Rate    = (View.Mode == FieldViewMode::Camera ? Across : View.Span) * 0.07 / Reading;
    for (Tracer& Mote : Tracers_)
    {
        Mote.Age += Delta;
        const bool Outside = Mote.X < Area.MinimumX || Mote.X > Area.MaximumX || Mote.Z < Area.MinimumZ || Mote.Z > Area.MaximumZ;
        double     Vector[3];
        if (Mote.Age > Mote.Life || Outside || !Interpolate(Mote.X, Mote.Z, Vector))
        {
            ReseedTracer(Mote, Area, false);
            continue;
        }
        const double Horizontal = std::hypot(Vector[0], Vector[2]);
        if (Horizontal < Reading * 0.01 && Mote.Age > 0.6)
        {
            ReseedTracer(Mote, Area, false);
            continue;
        }
        Mote.X += Vector[0] * Rate * Delta;
        Mote.Z += Vector[2] * Rate * Delta;
        Mote.Trail[Mote.Head * 2]     = Mote.X;
        Mote.Trail[Mote.Head * 2 + 1] = Mote.Z;
        Mote.Head                     = (Mote.Head + 1) % TrailLength;
        Mote.Count                    = std::min(TrailLength, Mote.Count + 1);
    }
}

void ForceFieldVisualization::RenderTracers(ImDrawList* Ink, float Scale) const
{
    if (!Options.Tracers || Options.Contribution == ForceContribution::Damp) return;
    const double SliceY = Options.SliceHeight;
    for (uint32_t Segment = 1; Segment < TrailLength; ++Segment)
    {
        const float Fade   = 1.0f - static_cast<float>(Segment) / TrailLength;
        const ImU32 Colour = IM_COL32(255, 255, 255, static_cast<int>(0.55f * Fade * 255));
        const float Width  = 1.5f * (1.0f - Segment / (TrailLength * 1.4f)) * Scale;
        for (const Tracer& Mote : Tracers_)
        {
            if (Mote.Count <= Segment) continue;
            const uint32_t Newer = (Mote.Head + TrailLength - Segment) % TrailLength;
            const uint32_t Older = (Newer + TrailLength - 1) % TrailLength;
            ImVec2         From, To;
            if (!Project(Mote.Trail[Older * 2], SliceY, Mote.Trail[Older * 2 + 1], From) || !Project(Mote.Trail[Newer * 2], SliceY, Mote.Trail[Newer * 2 + 1], To))
                continue;
            if (std::abs(From.x - To.x) + std::abs(From.y - To.y) > 80 * Scale) continue;
            Ink->AddLine(From, To, Colour, Width);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOURCES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

std::vector<std::array<double, 3>> CircleOutline(double X, double Y, double Z, double Radius, int Segments)
{
    std::vector<std::array<double, 3>> Points;
    Points.reserve(static_cast<size_t>(Segments) + 1);
    for (int Index = 0; Index <= Segments; ++Index)
    {
        const double Angle = Index / static_cast<double>(Segments) * π * 2.0;
        Points.push_back({X + std::cos(Angle) * Radius, Y, Z + std::sin(Angle) * Radius});
    }
    return Points;
}

std::vector<std::array<double, 3>> BoxOutline(const FieldSourceOutline& Source, double Y)
{
    const double Turn = Source.RotationDegrees * π / 180.0, Cosine = std::cos(Turn), Sine = std::sin(Turn);
    const double CentreX = Source.OutlineOffset ? Source.OutlineX : Source.X, CentreZ = Source.OutlineOffset ? Source.OutlineZ : Source.Z;
    std::vector<std::array<double, 3>> Points;
    for (const auto& [U, V] : {std::pair{-1.0, -1.0}, std::pair{1.0, -1.0}, std::pair{1.0, 1.0}, std::pair{-1.0, 1.0}})
    {
        const double LocalX = U * Source.HalfX, LocalZ = V * Source.HalfZ;
        Points.push_back({CentreX + LocalX * Cosine - LocalZ * Sine, Y, CentreZ + LocalX * Sine + LocalZ * Cosine});
    }
    return Points;
}

} // namespace

void ForceFieldVisualization::RenderSources(ImDrawList* Ink, const EditorTypefaces& Faces, float Scale) const
{
    if (!Options.ShowSources) return;
    const double SliceY         = Options.SliceHeight;
    const double PixelsPerMetre = View.Mode == FieldViewMode::Camera ? 0.0 : ResolveAffine().PixelsPerMetre;
    struct Label { const FieldSourceOutline* Source; ImVec2 Centre; };
    std::vector<Label> Labels;
    for (const FieldSourceOutline& Source : Options.Sources)
    {
        const float Alpha = static_cast<float>((0.45 + 0.55 * std::min(1.0, Source.Active + (Source.Selected ? 0.4 : 0.0))) *
                                               (Source.Dim && !Source.Selected ? 0.4 : 1.0));
        const ImU32 Colour = ContributionColour(Source.Contribution, Alpha);
        const float Width  = (Source.Selected ? 2.0f : 1.2f) * Scale;
        const float DashOn = Source.Active > 0.01 ? 0.0f : 5.0f * Scale;
        if (Source.Track.size() > 1)
        {
            std::vector<std::array<double, 3>> Track;
            for (const auto& Point : Source.Track) Track.push_back({Point[0], SliceY, Point[1]});
            StrokeWorldPath(Ink, Track, false, Colour, Scale, 2.0f * Scale, 5.0f * Scale);
        }
        if (Source.Shape == FieldOutlineShape::Box) StrokeWorldPath(Ink, BoxOutline(Source, SliceY), true, Colour, Width, DashOn, DashOn);
        else if (Source.Shape == FieldOutlineShape::Circle)
        {
            const double OnScreen = PixelsPerMetre > 0 ? Source.Radius * PixelsPerMetre : 99.0;
            if (OnScreen >= 3.0) StrokeWorldPath(Ink, CircleOutline(Source.X, SliceY, Source.Z, Source.Radius, 72), true, Colour, Width, DashOn, DashOn);
            if (Source.InnerRadius > 0 && (PixelsPerMetre <= 0 || Source.InnerRadius * PixelsPerMetre >= 3.0))
                StrokeWorldPath(Ink, CircleOutline(Source.X, SliceY, Source.Z, Source.InnerRadius, 48), true, WithAlpha(Colour, 0.6f), Width, 2.0f * Scale,
                                3.0f * Scale);
        }
        if (View.Mode == FieldViewMode::Oblique && Source.Height > 0)
        {
            StrokeWorldPath(Ink, {{Source.X, Source.Y, Source.Z}, {Source.X, Source.Y + Source.Height, Source.Z}}, false, Colour, 1.4f * Scale);
            if (Source.Radius * PixelsPerMetre >= 3.0)
                StrokeWorldPath(Ink, CircleOutline(Source.X, Source.Y + Source.Height, Source.Z, std::max(Source.InnerRadius, Source.Radius * 0.3), 48), true,
                                WithAlpha(Colour, 0.5f), 1.4f * Scale);
        }
        ImVec2 Centre;
        if (!Project(Source.X, SliceY, Source.Z, Centre)) continue;
        if (Source.ShowBearing)
        {
            const double Turn  = Source.BearingDegrees * π / 180.0;
            const double Reach = PixelsPerMetre > 0 ? 46.0 * Scale / PixelsPerMetre : std::max(0.6, Source.Radius * 0.5);
            ImVec2       Tip;
            if (Project(Source.X + std::sin(Turn) * Reach, SliceY, Source.Z - std::cos(Turn) * Reach, Tip))
            {
                const float Angle = std::atan2(Tip.y - Centre.y, Tip.x - Centre.x);
                Ink->AddLine(Centre, Tip, Colour, 1.8f * Scale);
                const ImVec2 Head[3] = {ImVec2(Tip.x - std::cos(Angle - 0.5f) * 8 * Scale, Tip.y - std::sin(Angle - 0.5f) * 8 * Scale), Tip,
                                        ImVec2(Tip.x - std::cos(Angle + 0.5f) * 8 * Scale, Tip.y - std::sin(Angle + 0.5f) * 8 * Scale)};
                Ink->AddPolyline(Head, 3, Colour, 0, 1.8f * Scale);
            }
        }
        Ink->AddCircleFilled(Centre, (Source.Selected ? 5.5f : 4.2f) * Scale, Colour, 16);
        if (Source.Selected) Ink->AddCircle(Centre, 9 * Scale, IM_COL32_WHITE, 24, 1.6f * Scale);
        // 📝 At large spans a source smaller than a few pixels keeps its dot but not its label, so a 100 km view stays legible.
        const bool Tiny = PixelsPerMetre > 0 && Source.Shape != FieldOutlineShape::Everywhere &&
                          std::max({Source.Radius, Source.HalfX, Source.HalfZ}) * PixelsPerMetre < 6.0 * Scale;
        if (Options.ShowLabels && !Source.Label.empty() && (!Tiny || Source.Selected) && (!Source.Dim || Source.Selected)) Labels.push_back({&Source, Centre});
    }
    const float         Size = 11.0f * Scale;
    std::vector<ImVec4> Placed;
    for (const Label& Entry : Labels)
    {
        const char* Caption = Entry.Source->Label.c_str();
        const float Wide    = Measure(Faces.Medium, Size, Caption);
        const float X       = Entry.Centre.x + 10 * Scale;
        float       Y       = Entry.Centre.y - 9 * Scale;
        // 📝 Labels step down until they no longer overlap one already placed (crowded large-scale views).
        for (int Attempt = 0; Attempt < 6; ++Attempt)
        {
            const bool Overlaps = std::any_of(Placed.begin(), Placed.end(), [&](const ImVec4& Box) {
                return X < Box.z && X + Wide + 10 * Scale > Box.x && Y - 12 * Scale < Box.w && Y + 5 * Scale > Box.y;
            });
            if (!Overlaps) break;
            Y += 17 * Scale;
        }
        Placed.push_back(ImVec4(X - 5 * Scale, Y - 12 * Scale, X + Wide + 5 * Scale, Y + 5 * Scale));
        Ink->AddRectFilled(ImVec2(X - 6 * Scale, Y - 12 * Scale), ImVec2(X + Wide + 6 * Scale, Y + 5 * Scale),
                           Entry.Source->Selected ? IM_COL32(30, 34, 40, 235) : IM_COL32(8, 10, 14, 184), 8 * Scale);
        const ImU32 Colour = Entry.Source->Selected ? IM_COL32_WHITE : Entry.Source->Active > 0.01 ? IM_COL32(230, 230, 230, 255) : IM_COL32(138, 138, 138, 255);
        Text(Ink, Faces.Medium, Size, ImVec2(X, Y - Size * 0.9f), Colour, Caption);
    }
}

std::vector<FieldSourceOutline> ForceFieldVisualization::DescribeSources(const ForceFieldSpace& Field, const std::vector<std::string>& Names,
                                                                         int32_t Selected, ForceContribution Shown, double TimelineDurationSeconds)
{
    std::vector<FieldSourceOutline> Outlines;
    const auto&                     Prepared = Field.AuthoredSources();
    for (size_t Index = 0; Index < Prepared.size(); ++Index)
    {
        const PreparedForceSource&      Entry    = Prepared[Index];
        const ForceSourceConfiguration& Source   = Entry.Source;
        const SpatialVector             Authored = ConvertSceneToFieldAxes(Source.CentreMetres);
        FieldSourceOutline              Outline;
        Outline.Index        = static_cast<int32_t>(Index);
        Outline.X            = Entry.CentreMetres.X;
        Outline.Y            = Entry.CentreMetres.Y;
        Outline.Z            = Entry.CentreMetres.Z;
        Outline.Contribution = Entry.Contribution;
        Outline.Label        = Index < Names.size() ? Names[Index] : std::string(ResolveForceSourceDescriptor(Source.Category).Name);
        Outline.Active       = Entry.Activation.Envelope;
        Outline.Selected     = static_cast<int32_t>(Index) == Selected;
        Outline.Dim          = Entry.Contribution != Shown;
        Outline.Radius       = Source.ReachMetres;
        if (Source.Category == ForceSourceCategory::GustFront)
        {
            const double Turn = Source.BearingDegrees * π / 180.0;
            Outline.Shape           = FieldOutlineShape::Box;
            Outline.HalfX           = Source.HalfWidthMetres;
            Outline.HalfZ           = Source.CrestDepthMetres * 1.3;
            Outline.RotationDegrees = Source.BearingDegrees;
            Outline.ShowBearing     = true;
            Outline.BearingDegrees  = Source.BearingDegrees;
            Outline.OutlineOffset   = true;
            Outline.OutlineX        = Entry.CentreMetres.X - std::sin(Turn) * Source.CrestDepthMetres * 1.05;
            Outline.OutlineZ        = Entry.CentreMetres.Z + std::cos(Turn) * Source.CrestDepthMetres * 1.05;
        }
        else if (Source.Reach == ForceReach::Box)
        {
            // 📝 The box the reach mask uses (ForceSourceCatalogue's box extents): a jet's corridor, a wake's trail.
            double HalfX = std::max(0.5, Source.HalfExtentsMetres[0]), HalfZ = std::max(0.5, Source.HalfExtentsMetres[1]), Offset = 0.0;
            if (Source.Category == ForceSourceCategory::JetCorridor)
            {
                HalfX  = Source.InnerRadiusMetres * 3.2;
                HalfZ  = Source.LengthMetres * 0.75;
                Offset = Source.LengthMetres * 0.5;
            }
            else if (Source.Category == ForceSourceCategory::VehicleWake)
            {
                HalfX  = Source.HalfWidthMetres * 4.0;
                HalfZ  = Source.LengthMetres * 1.2;
                Offset = -Source.LengthMetres;
            }
            const double Turn = Source.BearingDegrees * π / 180.0;
            Outline.Shape           = FieldOutlineShape::Box;
            Outline.HalfX           = HalfX;
            Outline.HalfZ           = HalfZ;
            Outline.RotationDegrees = Source.BearingDegrees;
            Outline.OutlineOffset   = true;
            Outline.OutlineX        = Entry.CentreMetres.X + std::sin(Turn) * Offset;
            Outline.OutlineZ        = Entry.CentreMetres.Z - std::cos(Turn) * Offset;
            Outline.ShowBearing     = Source.Category != ForceSourceCategory::DragVolume;
            Outline.BearingDegrees  = Source.BearingDegrees;
        }
        else if (Source.Reach == ForceReach::Everywhere) Outline.Shape = FieldOutlineShape::Everywhere;
        if (Source.Category == ForceSourceCategory::BlastWave && Entry.Activation.Envelope > 0.001)
            Outline.Radius = std::min(Source.ReachMetres, Source.InitialRadiusMetres + Source.ExpansionMetresPerSecond * Entry.Activation.AgeSeconds);
        if (Source.Category == ForceSourceCategory::TornadoColumn || Source.Category == ForceSourceCategory::ThermalUpdraft ||
            Source.Category == ForceSourceCategory::Microburst)
        {
            Outline.InnerRadius = Source.InnerRadiusMetres;
            Outline.Height      = Source.HeightMetres;
        }
        if (Source.Category == ForceSourceCategory::GustPuff)
        {
            Outline.ShowBearing    = true;
            Outline.BearingDegrees = Source.BearingDegrees;
        }
        if (Source.Category == ForceSourceCategory::TornadoColumn && Source.TrackSpeedMetresPerSecond > 0)
        {
            const double Turn = Source.TrackBearingDegrees * π / 180.0, Travel = Source.TrackSpeedMetresPerSecond * TimelineDurationSeconds;
            Outline.Track = {{Authored.X, Authored.Z}, {Authored.X + std::sin(Turn) * Travel, Authored.Z - std::cos(Turn) * Travel}};
        }
        if (Source.Category == ForceSourceCategory::GustPuff || Source.Category == ForceSourceCategory::GustFront)
            Outline.Track = {{Authored.X, Authored.Z}, {Entry.CentreMetres.X, Entry.CentreMetres.Z}};
        Outlines.push_back(std::move(Outline));
    }
    return Outlines;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCALE BAR, LEGEND, PROBE
//------------------------------------------------------------------------------------------------------------------------

void ForceFieldVisualization::RenderScaleBar(ImDrawList* Ink, const EditorTypefaces& Faces, float Scale) const
{
    if (!Options.ShowScaleBar || View.Mode == FieldViewMode::Camera) return;
    const double PixelsPerMetre = ResolveAffine().PixelsPerMetre;
    const double Metres         = ResolveNiceLength(View.Span * 0.18);
    const float  Pixels         = static_cast<float>(Metres * PixelsPerMetre);
    const float  X = Origin_.x + 14 * Scale, Y = Origin_.y + Height_ - 14 * Scale;
    RoundedPanel(Ink, ImVec2(X - 2 * Scale, Y - 32 * Scale), ImVec2(Pixels + 66 * Scale, 38 * Scale), Scale);
    const ImVec2 Bar[4] = {ImVec2(X + 10 * Scale, Y - 13 * Scale), ImVec2(X + 10 * Scale, Y - 7 * Scale), ImVec2(X + 10 * Scale + Pixels, Y - 7 * Scale),
                           ImVec2(X + 10 * Scale + Pixels, Y - 13 * Scale)};
    Ink->AddPolyline(Bar, 4, IM_COL32_WHITE, 0, 1.5f * Scale);
    Ink->AddLine(ImVec2(X + 10 * Scale + Pixels / 2, Y - 10 * Scale), ImVec2(X + 10 * Scale + Pixels / 2, Y - 7 * Scale), IM_COL32_WHITE, 1.5f * Scale);
    char Length[32], Span[32];
    FormatDistance(Metres, Length, sizeof(Length));
    FormatDistance(View.Span, Span, sizeof(Span));
    const float Size = 10.5f * Scale;
    Text(Ink, Faces.Regular, Size, ImVec2(X + 10 * Scale, Y - 18 * Scale - Size * 0.9f), IM_COL32(216, 216, 216, 255), Length);
    Text(Ink, Faces.Regular, Size, ImVec2(X + 18 * Scale + Pixels, Y - 4 * Scale - Size * 0.9f), IM_COL32(120, 120, 120, 255),
         View.Mode == FieldViewMode::Oblique ? "across" : Span);
}

void ForceFieldVisualization::RenderLegend(ImDrawList* Ink, const EditorTypefaces& Faces, float Scale) const
{
    if (!Options.ShowLegend) return;
    const bool   Signed   = Options.Contribution != ForceContribution::Damp && QuerySigned(Options.Quantity);
    const double Reading  = ScaleReading_ > 0 ? ScaleReading_ : 1.0;
    const float  BarWidth = 168 * Scale;
    const float  X        = Origin_.x + 14 * Scale;
    const float  Y        = Origin_.y + Height_ - ((Options.ShowScaleBar && View.Mode != FieldViewMode::Camera) ? 64 : 16) * Scale - 44 * Scale;
    RoundedPanel(Ink, ImVec2(X - 2 * Scale, Y), ImVec2(BarWidth + 22 * Scale, 48 * Scale), Scale);
    const char* Title = !Options.LegendTitle.empty() ? Options.LegendTitle.c_str()
                        : Options.Contribution == ForceContribution::Damp ? "Damping rate"
                                                                           : ResolveQuantityLabel(Options.Quantity);
    const float Size = 10.5f * Scale;
    Text(Ink, Faces.Medium, Size, ImVec2(X + 9 * Scale, Y + 15 * Scale - Size * 0.9f), IM_COL32(216, 216, 216, 255), Title);
    char UnitText[48];
    std::snprintf(UnitText, sizeof(UnitText), "%s%s", Unit_, Options.AutomaticScale ? " \xC2\xB7 auto" : "");
    Text(Ink, Faces.Regular, Size, ImVec2(X + BarWidth + 11 * Scale - Measure(Faces.Regular, Size, UnitText), Y + 15 * Scale - Size * 0.9f),
         IM_COL32(120, 120, 120, 255), UnitText);
    const float Segment = BarWidth / 4.0f;
    for (int Stop = 0; Stop < 4; ++Stop)
    {
        const ImU32 Left  = IM_COL32(ColourStops[Stop][0], ColourStops[Stop][1], ColourStops[Stop][2], 255);
        const ImU32 Right = IM_COL32(ColourStops[Stop + 1][0], ColourStops[Stop + 1][1], ColourStops[Stop + 1][2], 255);
        Ink->AddRectFilledMultiColor(ImVec2(X + 9 * Scale + Segment * Stop, Y + 21 * Scale), ImVec2(X + 9 * Scale + Segment * (Stop + 1), Y + 28 * Scale), Left,
                                     Right, Right, Left);
    }
    char         Low[24], Middle[24], High[24];
    const double Values[3] = {Signed ? -Reading : 0.0, Signed ? 0.0 : Reading / 2, Reading};
    FormatReading(Values[0], Low, sizeof(Low));
    FormatReading(Values[1], Middle, sizeof(Middle));
    FormatReading(Values[2], High, sizeof(High));
    char HighText[28];
    std::snprintf(HighText, sizeof(HighText), Signed ? "+%s" : "%s", High);
    const char* Ticks[3]  = {Low, Middle, HighText};
    const float Shares[3] = {0.0f, 0.5f, 1.0f};
    const float Small     = 9.5f * Scale;
    for (int Index = 0; Index < 3; ++Index)
    {
        const float Wide = Measure(Faces.Regular, Small, Ticks[Index]);
        const float At   = X + 9 * Scale + Shares[Index] * BarWidth - (Index == 0 ? 0.0f : Index == 2 ? Wide : Wide / 2);
        Text(Ink, Faces.Regular, Small, ImVec2(At, Y + 41 * Scale - Small * 0.9f), IM_COL32(154, 154, 154, 255), Ticks[Index]);
    }
}

void ForceFieldVisualization::RenderProbe(ImDrawList* Ink, float Scale) const
{
    if (!Options.HasProbe) return;
    ImVec2 Point;
    if (!Project(Options.ProbePoint[0], Options.SliceHeight, Options.ProbePoint[1], Point)) return;
    const float Near = 4 * Scale, Far = 11 * Scale, Width = 1.4f * Scale;
    Ink->AddLine(ImVec2(Point.x - Far, Point.y), ImVec2(Point.x - Near, Point.y), IM_COL32_WHITE, Width);
    Ink->AddLine(ImVec2(Point.x + Near, Point.y), ImVec2(Point.x + Far, Point.y), IM_COL32_WHITE, Width);
    Ink->AddLine(ImVec2(Point.x, Point.y - Far), ImVec2(Point.x, Point.y - Near), IM_COL32_WHITE, Width);
    Ink->AddLine(ImVec2(Point.x, Point.y + Near), ImVec2(Point.x, Point.y + Far), IM_COL32_WHITE, Width);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER
//------------------------------------------------------------------------------------------------------------------------

void ForceFieldVisualization::Render(ImDrawList* Ink, const EditorTypefaces& Faces, ImVec2 Minimum, ImVec2 Maximum, double Time, float Scale)
{
    const auto   Now   = std::chrono::steady_clock::now();
    const double Delta = LastRender_.time_since_epoch().count() == 0 ? 0.0 : std::min(0.1, std::chrono::duration<double>(Now - LastRender_).count());
    LastRender_ = Now;
    Origin_     = Minimum;
    Width_      = std::max(1.0f, Maximum.x - Minimum.x);
    Height_     = std::max(1.0f, Maximum.y - Minimum.y);
    Time_       = Time;

    // 📝 A finished lattice replaces the shown one; a new one is asked for when the view moved, the field changed, or the time
    //    advanced by a refresh interval. One sampling at a time runs on a worker thread.
    if (Pending_.valid() && Pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) AcceptLattice(Pending_.get());

    // 📝 What only colours the lattice (quantity, scale, opacity) re-derives it without sampling again.
    char Derivation[96];
    std::snprintf(Derivation, sizeof(Derivation), "%d|%d|%.4f|%.3f|%d", static_cast<int>(Options.Quantity), Options.AutomaticScale ? 1 : 0,
                  Options.FixedScale, Options.Opacity, static_cast<int>(Options.Contribution));
    if (Lattice_ && Derivation != DerivationStamp_)
    {
        if (DerivationStamp_.empty() || DerivationStamp_[0] != Derivation[0]) ScaleReading_ = 0.0;
        DerivationStamp_ = Derivation;
        DeriveQuantity(*Lattice_);
    }
    const std::string Stamp     = ResolveStamp();
    const bool        TimeMoved = Options.Animated && Lattice_ && Time != Lattice_->Time &&
                           std::chrono::duration<double>(Now - RefreshedAt_).count() >= Options.RefreshSeconds;
    if (!Pending_.valid() && (Dirty_ || Stamp != LatticeStamp_ || TimeMoved || !Lattice_))
    {
        const Coverage Area = ResolveCoverage();
        double         Cell = ResolveCellSpan();
        LatticeRequest Request;
        for (;;)
        {
            if (View.Mode == FieldViewMode::Camera)
            {
                Request.OriginX = Area.MinimumX + Cell / 2;
                Request.OriginZ = Area.MinimumZ + Cell / 2;
                Request.Columns = std::max<uint32_t>(2, static_cast<uint32_t>(std::lround((Area.MaximumX - Area.MinimumX) / Cell)));
                Request.Rows    = std::max<uint32_t>(2, static_cast<uint32_t>(std::lround((Area.MaximumZ - Area.MinimumZ) / Cell)));
            }
            else
            {
                Request.OriginX = std::floor(Area.MinimumX / Cell) * Cell;
                Request.OriginZ = std::floor(Area.MinimumZ / Cell) * Cell;
                Request.Columns = std::max<uint32_t>(2, static_cast<uint32_t>(std::ceil((Area.MaximumX - Request.OriginX) / Cell)) + 2);
                Request.Rows    = std::max<uint32_t>(2, static_cast<uint32_t>(std::ceil((Area.MaximumZ - Request.OriginZ) / Cell)) + 2);
            }
            if (static_cast<uint64_t>(Request.Columns) * Request.Rows <= 48000 || View.Mode == FieldViewMode::Camera) break;
            Cell *= 2.0;
        }
        Request.CellSpan     = Cell;
        Request.SliceY       = Options.SliceHeight;
        Request.Time         = Time;
        Request.Contribution = Options.Contribution;
        Request.Solo         = Options.Solo;
        PendingStamp_        = Stamp;
        RefreshedAt_         = Now;
        Dirty_               = false;
        Pending_             = std::async(std::launch::async, &ForceFieldVisualization::SampleLattice, Field_, Request);
        // 📝 The first lattice is waited for, so a view never opens empty.
        if (!Lattice_) AcceptLattice(Pending_.get());
        DerivationStamp_ = Derivation;
    }
    AdvanceTracers(Delta);

    Ink->PushClipRect(Minimum, Maximum, true);
    if (View.Mode != FieldViewMode::Camera) Ink->AddRectFilled(Minimum, Maximum, Options.Background);
    RenderHeatmap(Ink);
    RenderGroundGrid(Ink);
    RenderCascades(Ink, Faces, Scale);
    RenderArrows(Ink, Scale);
    RenderTracers(Ink, Scale);
    RenderSources(Ink, Faces, Scale);
    RenderProbe(Ink, Scale);
    RenderLegend(Ink, Faces, Scale);
    RenderScaleBar(Ink, Faces, Scale);
    Ink->PopClipRect();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    READINGS AND NAVIGATION
//------------------------------------------------------------------------------------------------------------------------

void ForceFieldVisualization::SettleSampling()
{
    if (Pending_.valid()) AcceptLattice(Pending_.get());
}

FieldProbeReading ForceFieldVisualization::ProbeWorld(double X, double Z) const
{
    FieldProbeReading Reading;
    if (!Field_) return Reading;
    const double Cell = Lattice_ ? Lattice_->CellSpan : 1.0, Half = Cell / 2.0, SliceY = Options.SliceHeight;
    auto Read = [&](double SampleX, double SampleZ) {
        return Field_->EvaluateContribution(Options.Contribution, {SampleX, SliceY, SampleZ}, Cell, true, Options.Solo);
    };
    const SpatialVector Centre = Read(X, Z), East = Read(X + Half, Z), West = Read(X - Half, Z), South = Read(X, Z + Half), North = Read(X, Z - Half);
    Reading.Valid      = true;
    Reading.X          = X;
    Reading.Y          = SliceY;
    Reading.Z          = Z;
    Reading.Vector     = Centre;
    Reading.Speed      = std::sqrt(Centre.X * Centre.X + Centre.Y * Centre.Y + Centre.Z * Centre.Z);
    Reading.Horizontal = std::hypot(Centre.X, Centre.Z);
    Reading.Bearing    = Wrap360(std::atan2(Centre.X, -Centre.Z) * 180.0 / π);
    Reading.Vorticity  = (South.X - North.X) / Cell - (East.Z - West.Z) / Cell;
    Reading.Divergence = (East.X - West.X) / Cell + (South.Z - North.Z) / Cell;
    Reading.CellSpan   = Cell;
    Reading.Unit       = ResolveUnit(Options.Contribution, ForceFieldQuantity::Speed);
    return Reading;
}

FieldProbeReading ForceFieldVisualization::ProbeScreen(ImVec2 Screen) const
{
    double X = 0, Z = 0;
    if (!ScreenToPlane(Screen, X, Z)) return {};
    return ProbeWorld(X, Z);
}

void ForceFieldVisualization::Zoom(double Factor, ImVec2 About)
{
    double     BeforeX = 0, BeforeZ = 0, AfterX = 0, AfterZ = 0;
    const bool Before = ScreenToPlane(About, BeforeX, BeforeZ);
    View.Span         = std::clamp(View.Span * Factor, Options.MinimumSpan, Options.MaximumSpan);
    if (Before && ScreenToPlane(About, AfterX, AfterZ))
    {
        View.CentreX += BeforeX - AfterX;
        View.CentreZ += BeforeZ - AfterZ;
    }
}

void ForceFieldVisualization::JumpToSpan(double Span)
{
    View.Span = std::clamp(Span, Options.MinimumSpan, Options.MaximumSpan);
}

int32_t ForceFieldVisualization::HitSource(ImVec2 Screen) const
{
    int32_t Best         = -1;
    float   BestDistance = 12.0f;
    for (const FieldSourceOutline& Source : Options.Sources)
    {
        ImVec2 Point;
        if (!Project(Source.X, Options.SliceHeight, Source.Z, Point)) continue;
        const float Distance = std::hypot(Point.x - Screen.x, Point.y - Screen.y);
        if (Distance < BestDistance)
        {
            Best         = Source.Index;
            BestDistance = Distance;
        }
    }
    return Best;
}

FieldInteraction ForceFieldVisualization::ConstructNavigation(const char* Id, ImVec2 Minimum, ImVec2 Maximum)
{
    FieldInteraction Outcome;
    if (View.Mode == FieldViewMode::Camera) return Outcome;
    Origin_ = Minimum;
    Width_  = std::max(1.0f, Maximum.x - Minimum.x);
    Height_ = std::max(1.0f, Maximum.y - Minimum.y);
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton(Id, ImVec2(Width_, Height_), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImGuiIO& Io      = ImGui::GetIO();
    const ImVec2   Pointer = Io.MousePos;
    if (ImGui::IsItemHovered())
    {
        double X = 0, Z = 0;
        if (ScreenToPlane(Pointer, X, Z))
        {
            Outcome.Hovering   = true;
            Outcome.HoverPoint = {X, Z};
        }
        if (Io.MouseWheel != 0.0f)
        {
            Zoom(std::exp(-Io.MouseWheel * 120.0 * 0.0012), Pointer);
            Outcome.ViewChanged = true;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ScreenToPlane(Pointer, X, Z))
        {
            View.CentreX        = X;
            View.CentreZ        = Z;
            Outcome.ViewChanged = true;
        }
        if (Drag_ == DragMode::None && HitSource(Pointer) >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (ImGui::IsItemActivated())
    {
        const bool    Turning = ImGui::IsMouseDown(ImGuiMouseButton_Right) || Io.KeyAlt;
        const int32_t Hit     = !Turning ? HitSource(Pointer) : -1;
        double        X = 0, Z = 0;
        const bool    OnPlane = ScreenToPlane(Pointer, X, Z);
        if (Hit >= 0)
        {
            Outcome.PickedSource = Hit;
            Drag_                = DragMode::Source;
            DragSource_          = Hit;
            for (const FieldSourceOutline& Source : Options.Sources)
                if (Source.Index == Hit) DragOffset_ = {Source.X - (OnPlane ? X : Source.X), Source.Z - (OnPlane ? Z : Source.Z)};
        }
        else if (Turning) Drag_ = DragMode::Turn;
        else
        {
            Drag_       = DragMode::Pan;
            DragPlane_  = {X, Z};
            DragCentre_ = {View.CentreX, View.CentreZ};
        }
        DragScreen_ = Pointer;
    }
    if (ImGui::IsItemActive() && Drag_ != DragMode::None)
    {
        if (Drag_ == DragMode::Source)
        {
            double X = 0, Z = 0;
            if ((Pointer.x != DragScreen_.x || Pointer.y != DragScreen_.y) && ScreenToPlane(Pointer, X, Z))
            {
                Outcome.MovedSource = DragSource_;
                Outcome.MovedTo     = {X + DragOffset_[0], Z + DragOffset_[1]};
            }
        }
        else if (Drag_ == DragMode::Turn)
        {
            if (View.Mode == FieldViewMode::Oblique)
            {
                View.Yaw += (Pointer.x - DragScreen_.x) * 0.008;
                View.Pitch          = std::clamp(View.Pitch + (Pointer.y - DragScreen_.y) * 0.006, 0.18, 1.55);
                Outcome.ViewChanged = true;
            }
            DragScreen_ = Pointer;
        }
        else
        {
            View.CentreX = DragCentre_[0];
            View.CentreZ = DragCentre_[1];
            double X = 0, Z = 0;
            if (ScreenToPlane(Pointer, X, Z))
            {
                View.CentreX = DragCentre_[0] + DragPlane_[0] - X;
                View.CentreZ = DragCentre_[1] + DragPlane_[1] - Z;
            }
            Outcome.ViewChanged = true;
        }
    }
    if (!ImGui::IsItemActive()) Drag_ = DragMode::None;
    return Outcome;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MINIMAP
//------------------------------------------------------------------------------------------------------------------------

// 📝 The whole extent at 72 × 72 samples (refreshed at most every 400 ms), the sources as dots and the view outline: the visible
//    rectangle (Map) or the inverse-projected screen (Oblique).
bool ForceFieldVisualization::ConstructMinimap(const char* Id, ImDrawList* Ink, ImVec2 Minimum, ImVec2 Maximum, double, float Scale)
{
    if (!Options.HasExtent || !Field_) return false;
    constexpr int Size  = 72;
    const auto&   E     = Options.Extent;
    const double  SpanX = E[2] - E[0], SpanZ = E[3] - E[1];
    const auto    Now   = std::chrono::steady_clock::now();
    char          Stamp[96];
    std::snprintf(Stamp, sizeof(Stamp), "%d|%.3f|%llu|%d", static_cast<int>(Options.Contribution), Options.SliceHeight,
                  static_cast<unsigned long long>(FieldRevision_), Options.Solo);
    if (MinimapColours_.empty() || std::chrono::duration<double>(Now - MinimapAt_).count() > 0.4 || Stamp != MinimapStamp_)
    {
        MinimapAt_    = Now;
        MinimapStamp_ = Stamp;
        std::vector<double> Readings(Size * Size);
        double              Peak = 0.0;
        const double        Cell = std::max(SpanX, SpanZ) / Size;
        for (int Row = 0; Row < Size; ++Row)
            for (int Column = 0; Column < Size; ++Column)
            {
                const double        X = E[0] + (Column + 0.5) / Size * SpanX, Z = E[1] + (Row + 0.5) / Size * SpanZ;
                const SpatialVector V = Field_->EvaluateContribution(Options.Contribution, {X, Options.SliceHeight, Z}, Cell, true, Options.Solo);
                const double        Reading = Options.Contribution == ForceContribution::Damp ? V.X : std::hypot(V.X, V.Z);
                Readings[static_cast<size_t>(Row) * Size + Column] = Reading;
                Peak = std::max(Peak, Reading);
            }
        MinimapColours_.resize(Size * Size);
        for (size_t Index = 0; Index < Readings.size(); ++Index) MinimapColours_[Index] = ResolveColour(Readings[Index] / (Peak > 0 ? Peak : 1.0), 0.9f);
    }
    const float Wide = Maximum.x - Minimum.x, Tall = Maximum.y - Minimum.y;
    Ink->AddRectFilled(Minimum, Maximum, IM_COL32(5, 11, 18, 255), 8 * Scale);
    const ImVec2 White = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
    for (int Row = 0; Row + 1 < Size; ++Row)
    {
        Ink->PrimReserve(6 * (Size - 1), 2 * Size);
        const ImDrawIdx Base = static_cast<ImDrawIdx>(Ink->_VtxCurrentIdx);
        for (int Column = 0; Column < Size; ++Column)
        {
            const float X = Minimum.x + (Column + 0.5f) / Size * Wide;
            Ink->PrimWriteVtx(ImVec2(X, Minimum.y + (Row + 0.5f) / Size * Tall), White, MinimapColours_[static_cast<size_t>(Row) * Size + Column]);
            Ink->PrimWriteVtx(ImVec2(X, Minimum.y + (Row + 1.5f) / Size * Tall), White, MinimapColours_[static_cast<size_t>(Row + 1) * Size + Column]);
        }
        for (int Column = 0; Column + 1 < Size; ++Column)
        {
            const ImDrawIdx A = static_cast<ImDrawIdx>(Base + 2 * Column);
            Ink->PrimWriteIdx(A);
            Ink->PrimWriteIdx(static_cast<ImDrawIdx>(A + 2));
            Ink->PrimWriteIdx(static_cast<ImDrawIdx>(A + 3));
            Ink->PrimWriteIdx(A);
            Ink->PrimWriteIdx(static_cast<ImDrawIdx>(A + 3));
            Ink->PrimWriteIdx(static_cast<ImDrawIdx>(A + 1));
        }
    }
    auto ToMap = [&](double X, double Z) {
        return ImVec2(Minimum.x + static_cast<float>((X - E[0]) / SpanX) * Wide, Minimum.y + static_cast<float>((Z - E[1]) / SpanZ) * Tall);
    };
    for (const FieldSourceOutline& Source : Options.Sources)
        Ink->AddCircleFilled(ToMap(Source.X, Source.Z), (Source.Selected ? 3.2f : 2.2f) * Scale,
                             Source.Selected ? IM_COL32_WHITE : ContributionColour(Source.Contribution, 1.0f), 10);
    ImVec2       Outline[4];
    bool         Complete   = true;
    const ImVec2 Corners[4] = {Origin_, ImVec2(Origin_.x + Width_, Origin_.y), ImVec2(Origin_.x + Width_, Origin_.y + Height_),
                               ImVec2(Origin_.x, Origin_.y + Height_)};
    for (int Index = 0; Index < 4; ++Index)
    {
        double X = 0, Z = 0;
        Complete &= ScreenToPlane(Corners[Index], X, Z);
        Outline[Index] = ToMap(X, Z);
    }
    Ink->PushClipRect(Minimum, Maximum, true);
    if (Complete)
    {
        Ink->AddConvexPolyFilled(Outline, 4, IM_COL32(255, 255, 255, 20));
        Ink->AddPolyline(Outline, 4, IM_COL32_WHITE, ImDrawFlags_Closed, 1.4f * Scale);
    }
    Ink->PopClipRect();
    Ink->AddRect(Minimum, Maximum, IM_COL32(255, 255, 255, 30), 8 * Scale, 0, Scale);

    // 📝 A click or drag centres the view on the picked point.
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::InvisibleButton(Id, ImVec2(std::max(1.0f, Wide), std::max(1.0f, Tall)));
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const ImVec2 Pointer = ImGui::GetIO().MousePos;
        View.CentreX         = E[0] + std::clamp((Pointer.x - Minimum.x) / Wide, 0.0f, 1.0f) * SpanX;
        View.CentreZ         = E[1] + std::clamp((Pointer.y - Minimum.y) / Tall, 0.0f, 1.0f) * SpanZ;
        return true;
    }
    return false;
}

} // namespace Frontier
