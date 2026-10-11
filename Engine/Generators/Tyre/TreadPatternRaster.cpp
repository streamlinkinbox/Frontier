//============================================================================================================================================
//                                                         TREADPATTERNRASTER.CPP
//============================================================================================================================================
// 📦 Draws the layer sequence into the unrolled depth field, then reads the four material maps off it.

#include "TreadPatternRaster.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

namespace {

constexpr float π = 3.14159265358979323846f;

//------------------------------------------------------------------------------------------------------------------------
//                                                      VALUE NOISE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 A hash with no state, so the micro texture is the same every time a tyre is re-rasterised.
[[nodiscard]] float Hash2(float X, float Y) noexcept
{
    const float S = std::sin(X * 127.1f + Y * 311.7f) * 43758.5453f;
    return S - std::floor(S);
}

/// 📦 Smoothstep-interpolated value noise on the unit lattice.
[[nodiscard]] float ValueNoise(float X, float Y) noexcept
{
    const float Xi = std::floor(X);
    const float Yi = std::floor(Y);
    const float Xf = X - Xi;
    const float Yf = Y - Yi;
    const float U  = Xf * Xf * (3.0f - 2.0f * Xf);
    const float V  = Yf * Yf * (3.0f - 2.0f * Yf);

    const float A = Hash2(Xi,        Yi);
    const float B = Hash2(Xi + 1.0f, Yi);
    const float C = Hash2(Xi,        Yi + 1.0f);
    const float D = Hash2(Xi + 1.0f, Yi + 1.0f);

    return A + (B - A) * U + (C - A) * V + (A - B - C + D) * U * V;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE GREY CANVAS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 A one-channel canvas holding cut depth rather than brightness, which is the same picture read the other
///    way up: the darken compositing the pattern needs becomes a maximum, and a maximum needs no blend state.
struct DepthCanvas
{
    uint32_t           W = 0u;
    uint32_t           H = 0u;
    std::vector<float> Cut;

    float ScaleX = 1.0f;   // [px/mm] - around
    float ScaleY = 1.0f;   // [px/mm] - across
    float OriginX = 0.0f;  // [px]    - where x = 0 mm lands, shifted per wrap copy
    float OriginY = 0.0f;  // [px]    - where y = 0 mm lands, the centreline

    float ClipLow  = -1e9f;   // [px] - rows outside this band take no ink
    float ClipHigh =  1e9f;   // [px]

    [[nodiscard]] float ToX(float Millimetres) const noexcept { return OriginX + Millimetres * ScaleX; }
    [[nodiscard]] float ToY(float Millimetres) const noexcept { return OriginY + Millimetres * ScaleY; }

    /// 📦 Deepens every texel the shape covers. Coverage is the one-pixel ramp either side of the edge, which
    ///    is what keeps a 1.2 mm sipe from stair-stepping at four texels per millimetre.
    template <typename Distance>
    void Deepen(float MinX, float MinY, float MaxX, float MaxY, float Value, Distance SignedDistance) noexcept
    {
        const int X0 = std::max(0, int(std::floor(MinX - 1.0f)));
        const int Y0 = std::max({ 0, int(std::floor(MinY - 1.0f)), int(std::floor(ClipLow)) });
        const int X1 = std::min(int(W) - 1, int(std::ceil(MaxX + 1.0f)));
        const int Y1 = std::min({ int(H) - 1, int(std::ceil(MaxY + 1.0f)), int(std::ceil(ClipHigh)) });

        for (int Y = Y0; Y <= Y1; ++Y)
        {
            for (int X = X0; X <= X1; ++X)
            {
                const float D = SignedDistance(float(X) + 0.5f, float(Y) + 0.5f);
                if (D >= 0.5f)
                {
                    continue;
                }
                const float Coverage = std::clamp(0.5f - D, 0.0f, 1.0f);
                float& Texel = Cut[size_t(Y) * W + size_t(X)];
                Texel = std::max(Texel, Value * Coverage);
            }
        }
    }

    /// 📦 Replaces every texel the box covers, which is how a wear bar sits in a groove: the bar is not a
    ///    shallower cut layered over a deeper one, it is rubber the mould left standing, so it overwrites.
    void Replace(float MinX, float MinY, float MaxX, float MaxY, float Value) noexcept
    {
        const int X0 = std::max(0, int(std::floor(MinX)));
        const int Y0 = std::max(0, int(std::floor(MinY)));
        const int X1 = std::min(int(W) - 1, int(std::ceil(MaxX)) - 1);
        const int Y1 = std::min(int(H) - 1, int(std::ceil(MaxY)) - 1);

        for (int Y = Y0; Y <= Y1; ++Y)
        {
            for (int X = X0; X <= X1; ++X)
            {
                Cut[size_t(Y) * W + size_t(X)] = Value;
            }
        }
    }
};

/// 📦 Distance from a point to a segment, clamped at both ends, which is the round-joined stroke's field.
[[nodiscard]] float SegmentDistance(float PX, float PY, float AX, float AY, float BX, float BY) noexcept
{
    const float DX = BX - AX;
    const float DY = BY - AY;
    const float LL = DX * DX + DY * DY;
    float T = 0.0f;
    if (LL > 1e-12f)
    {
        T = std::clamp(((PX - AX) * DX + (PY - AY) * DY) / LL, 0.0f, 1.0f);
    }
    const float QX = AX + DX * T - PX;
    const float QY = AY + DY * T - PY;
    return std::sqrt(QX * QX + QY * QY);
}

/// 📦 Strokes a polyline at a given half width. Joins are round because that is what the mould's cutting tool
///    leaves, and a mitred join would put a spike of rubber where a chevron turns.
void StrokePolyline(DepthCanvas& Canvas, const std::vector<float>& Points, float HalfWidth, float Value) noexcept
{
    if (Points.size() < 4u || HalfWidth <= 0.0f)
    {
        return;
    }
    const size_t Count = Points.size() / 2u;
    for (size_t I = 0; I + 1u < Count; ++I)
    {
        const float AX = Points[I * 2u];
        const float AY = Points[I * 2u + 1u];
        const float BX = Points[(I + 1u) * 2u];
        const float BY = Points[(I + 1u) * 2u + 1u];

        Canvas.Deepen(std::min(AX, BX) - HalfWidth, std::min(AY, BY) - HalfWidth,
                      std::max(AX, BX) + HalfWidth, std::max(AY, BY) + HalfWidth, Value,
                      [=](float PX, float PY) noexcept
                      { return SegmentDistance(PX, PY, AX, AY, BX, BY) - HalfWidth; });
    }
}

/// 📦 Fills a disc.
void FillDisc(DepthCanvas& Canvas, float CX, float CY, float Radius, float Value) noexcept
{
    Canvas.Deepen(CX - Radius, CY - Radius, CX + Radius, CY + Radius, Value,
                  [=](float PX, float PY) noexcept
                  { return std::sqrt((PX - CX) * (PX - CX) + (PY - CY) * (PY - CY)) - Radius; });
}

/// 📦 Strokes a circle, which is a dimple layer cutting a ring instead of a disc.
void StrokeCircle(DepthCanvas& Canvas, float CX, float CY, float Radius, float HalfWidth, float Value) noexcept
{
    const float Reach = Radius + HalfWidth;
    Canvas.Deepen(CX - Reach, CY - Reach, CX + Reach, CY + Reach, Value,
                  [=](float PX, float PY) noexcept
                  {
                      const float D = std::sqrt((PX - CX) * (PX - CX) + (PY - CY) * (PY - CY));
                      return std::abs(D - Radius) - HalfWidth;
                  });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE WRAP COPY
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Draws every cutting layer once, at one circumferential offset.
/// note  ⚠️ The offset is not decoration. A lateral groove leaning 30° crosses the seam, and wrapping its
///       coordinate would cut it in half mid-stroke. Drawing the whole sequence at −C, 0 and +C lets the
///       part that leaves one side arrive on the other already joined.
void DrawLayers(DepthCanvas& Canvas, const TreadPatternSpecification& Pattern,
                const TreadSpecification& Specification, const TreadDerivedValues& Derived) noexcept
{
    const float Half = Derived.TreadHalf;
    const float Circ = Derived.Circumference;

    for (const TreadLayerSpecification& Layer : Pattern.Layers)
    {
        if (Layer.Kind == TreadLayerKind::Noise)
        {
            continue;
        }
        const float Value = std::clamp(Layer.DepthFraction, 0.0f, 1.0f);

        switch (Layer.Kind)
        {
            //--------------------------------------------------------------------------------------------------
            // ① a groove running the whole way around, straight or zig-zagged
            //--------------------------------------------------------------------------------------------------
            case TreadLayerKind::Circumferential:
            {
                const float Seat = Layer.Position * Half;
                if (Layer.Zig > 0.0f)
                {
                    const int Periods = std::max(2, int(std::lround(Layer.ZigCount))) * 2;
                    std::vector<float> Points;
                    Points.reserve(size_t(Periods + 1) * 2u);
                    for (int I = 0; I <= Periods; ++I)
                    {
                        const float X = float(I) / float(Periods) * Circ;
                        const float Y = Seat + ((I % 2) ? Layer.Zig : -Layer.Zig);
                        Points.push_back(Canvas.ToX(X));
                        Points.push_back(Canvas.ToY(Y));
                    }
                    StrokePolyline(Canvas, Points, Layer.Width * 0.5f * Canvas.ScaleY, Value);
                }
                else
                {
                    const float Top = Canvas.ToY(Seat - Layer.Width * 0.5f);
                    const float Bottom = Canvas.ToY(Seat + Layer.Width * 0.5f);
                    Canvas.Deepen(Canvas.ToX(-5.0f), Top, Canvas.ToX(Circ + 5.0f), Bottom, Value,
                                  [=](float, float PY) noexcept
                                  { return std::max(Top - PY, PY - Bottom); });
                }
                break;
            }

            //--------------------------------------------------------------------------------------------------
            // ② the lateral family: slots repeated around, optionally mirrored, leaned, bowed and zig-zagged
            //--------------------------------------------------------------------------------------------------
            case TreadLayerKind::Lateral:
            case TreadLayerKind::Chevron:
            case TreadLayerKind::Sipe:
            {
                const float Lean  = std::tan(Layer.Angle * π / 180.0f);
                const int   Count = std::max(1, int(std::lround(Layer.Count)));
                constexpr int StepsAlongSlot = 14;

                for (int I = 0; I < Count; ++I)
                {
                    const float Base = (float(I) + Layer.Phase) / float(Count) * Circ;
                    const int   Sides = Layer.Mirror ? 2 : 1;
                    for (int S = 0; S < Sides; ++S)
                    {
                        const float Sign = (S == 0) ? 1.0f : -1.0f;
                        std::vector<float> Points;
                        Points.reserve(size_t(StepsAlongSlot + 1) * 2u);
                        for (int J = 0; J <= StepsAlongSlot; ++J)
                        {
                            const float Across = Layer.From + (Layer.To - Layer.From) * float(J) / float(StepsAlongSlot);
                            const float Y      = (Layer.Mirror ? Sign * Across : Across) * Half;
                            const float Reach  = Layer.Mirror ? std::abs(Across) : Across;
                            float X = Base + Reach * Half * Lean + Layer.Curve * Reach * Reach * Half * 0.5f;
                            if (Layer.Zig != 0.0f)
                            {
                                X += ((J % 2) ? Layer.Zig : -Layer.Zig) * 0.5f;
                            }
                            Points.push_back(Canvas.ToX(X));
                            Points.push_back(Canvas.ToY(Y));
                        }
                        StrokePolyline(Canvas, Points, Layer.Width * 0.5f * Canvas.ScaleY, Value);
                    }
                }
                break;
            }

            //--------------------------------------------------------------------------------------------------
            // ③ discs or rings in rows, optionally staggered
            //--------------------------------------------------------------------------------------------------
            case TreadLayerKind::Dimple:
            {
                const int Rows  = std::max(1, int(std::lround(Layer.Rows)));
                const int Count = std::max(1, int(std::lround(Layer.Count)));
                for (int R = 0; R < Rows; ++R)
                {
                    const float Across = (Rows == 1) ? (Layer.From + Layer.To) * 0.5f
                                                     : Layer.From + (Layer.To - Layer.From) * float(R) / float(Rows - 1);
                    const float Y     = Canvas.ToY(Across * Half);
                    const float Shift = (Layer.Stagger && (R % 2)) ? 0.5f : 0.0f;
                    for (int I = 0; I < Count; ++I)
                    {
                        const float X = Canvas.ToX((float(I) + Shift) / float(Count) * Circ);
                        if (Layer.Ring)
                        {
                            StrokeCircle(Canvas, X, Y, Layer.Radius * Canvas.ScaleY,
                                         std::max(0.6f, Layer.Radius * 0.4f) * 0.5f * Canvas.ScaleY, Value);
                        }
                        else
                        {
                            FillDisc(Canvas, X, Y, Layer.Radius * Canvas.ScaleY, Value);
                        }
                    }
                }
                break;
            }

            //--------------------------------------------------------------------------------------------------
            // ④ a hexagonal lattice of groove lines, clipped to the band the layer claims
            //--------------------------------------------------------------------------------------------------
            case TreadLayerKind::Hexagon:
            {
                const float Size = std::max(2.0f, Layer.Size);
                const int   NX   = std::max(2, 2 * int(std::lround(Circ / (3.0f * Size))));
                const float SX   = Circ / float(NX);
                const float SY   = std::sqrt(3.0f) * Size;
                const float Y0   = Layer.From * Half;
                const float Y1   = Layer.To * Half;
                const float Low  = Canvas.ToY(std::min(Y0, Y1));
                const float High = Canvas.ToY(std::max(Y0, Y1));
                const float Rot  = Layer.Rotate * π / 180.0f;
                const int   J0   = int(std::floor((std::min(Y0, Y1) - SY) / SY));
                const int   J1   = int(std::ceil((std::max(Y0, Y1) + SY) / SY));
                const float HalfWidth = Layer.Width * 0.5f * Canvas.ScaleY;

                // ⚠️ The band is a clip, not a reject. Half a cell inside the band must draw its half and
                //    stop at the edge, exactly as the generator's canvas clip did; dropping whole cells
                //    would leave a ragged row of hexagons instead of a cleanly cut band.
                Canvas.ClipLow  = Low;
                Canvas.ClipHigh = High;

                for (int I = -1; I <= NX; ++I)
                {
                    for (int J = J0; J <= J1; ++J)
                    {
                        const float CX = float(I) * SX;
                        const float CY = float(J) * SY + ((std::abs(I) % 2) ? SY * 0.5f : 0.0f);
                        std::vector<float> Cell;
                        Cell.reserve(14u);
                        for (int V = 0; V <= 6; ++V)
                        {
                            const float A = Rot + float(V % 6) * π / 3.0f;
                            Cell.push_back(Canvas.ToX(CX + Size * std::cos(A)));
                            Cell.push_back(Canvas.ToY(CY + Size * std::sin(A)));
                        }
                        // The band clip, applied as a bounding reject: a cell wholly outside never draws.
                        float CellLow = Cell[1];
                        float CellHigh = Cell[1];
                        for (size_t K = 1; K < Cell.size(); K += 2u)
                        {
                            CellLow = std::min(CellLow, Cell[K]);
                            CellHigh = std::max(CellHigh, Cell[K]);
                        }
                        if (CellHigh < Low - HalfWidth || CellLow > High + HalfWidth)
                        {
                            continue;
                        }
                        StrokePolyline(Canvas, Cell, HalfWidth, Value);
                    }
                }
                Canvas.ClipLow  = -1e9f;
                Canvas.ClipHigh =  1e9f;
                break;
            }

            case TreadLayerKind::Noise:
            default:
                break;
        }
    }
}

/// 📦 Puts the 1.6 mm legal-limit bars back into every deep straight circumferential groove.
/// note  Six per groove, which is the count a moulded tyre carries, and they overwrite rather than deepen.
void DrawWearBars(DepthCanvas& Canvas, const TreadPatternSpecification& Pattern,
                  const TreadSpecification& Specification, const TreadDerivedValues& Derived) noexcept
{
    if (Specification.TreadDepth <= 1.6f)
    {
        return;
    }
    const float Value = 1.0f - 1.6f / Specification.TreadDepth;
    const float Half  = Derived.TreadHalf;
    const float Circ  = Derived.Circumference;

    for (const TreadLayerSpecification& Layer : Pattern.Layers)
    {
        if (Layer.Kind != TreadLayerKind::Circumferential || Layer.Width < 4.0f
            || Layer.DepthFraction < 0.9f || Layer.Zig != 0.0f)
        {
            continue;
        }
        for (int I = 0; I < 6; ++I)
        {
            const float X = (float(I) + 0.5f) / 6.0f * Circ - 3.0f;
            Canvas.Replace(Canvas.ToX(X), Canvas.ToY(Layer.Position * Half - Layer.Width * 0.5f - 0.2f),
                           Canvas.ToX(X + 6.0f), Canvas.ToY(Layer.Position * Half + Layer.Width * 0.5f + 0.2f),
                           Value);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       COLOUR HELP
//------------------------------------------------------------------------------------------------------------------------

struct Rgb
{
    float R = 0.0f;
    float G = 0.0f;
    float B = 0.0f;
};

[[nodiscard]] Rgb Unpack(uint32_t Argb) noexcept
{
    return Rgb{ float((Argb >> 16) & 0xFFu) / 255.0f,
                float((Argb >> 8) & 0xFFu) / 255.0f,
                float(Argb & 0xFFu) / 255.0f };
}

[[nodiscard]] uint32_t Pack(const Rgb& C) noexcept
{
    const auto Byte = [](float V) noexcept -> uint32_t
    { return uint32_t(std::clamp(V, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return 0xFF000000u | (Byte(C.R) << 16) | (Byte(C.G) << 8) | Byte(C.B);
}

[[nodiscard]] Rgb Mix(const Rgb& A, const Rgb& B, float T) noexcept
{
    const float K = std::clamp(T, 0.0f, 1.0f);
    return Rgb{ A.R + (B.R - A.R) * K, A.G + (B.G - A.G) * K, A.B + (B.B - A.B) * K };
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        WEAR ROW
//------------------------------------------------------------------------------------------------------------------------

float TreadDepthField::WornAt(uint32_t Y, const TreadSpecification& Specification,
                              const TreadDerivedValues& Derived) const noexcept
{
    if (Height == 0u || Derived.TreadHalf <= 0.0f)
    {
        return 0.0f;
    }
    const float Across = (float(Y) / float(Height) * 2.0f - 1.0f) * Derived.AcrossHalf / Derived.TreadHalf;
    const float Shape  = 1.0f - 2.0f * std::min(1.0f, Across * Across);
    return std::clamp(Specification.Wear * (1.0f + 0.6f * Specification.WearBias * Shape), 0.0f, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RASTERISER
//------------------------------------------------------------------------------------------------------------------------

TreadDepthField RasterizeTreadPattern(const TreadPatternSpecification& Pattern,
                                      const TreadSpecification& Specification,
                                      const TreadDerivedValues& Derived,
                                      uint32_t Height, bool SkipNoise)
{
    TreadDepthField Field;
    if (Height == 0u || Derived.AcrossHalf <= 0.0f || Derived.Circumference <= 0.0f)
    {
        return Field;
    }

    // ① one texel is square in millimetres, so the width follows from the tyre rather than from a constant
    const float Pixels = float(Height) / (2.0f * Derived.AcrossHalf);
    const uint32_t Width = uint32_t(std::clamp(std::lround(Derived.Circumference * Pixels), 64L, 4096L));

    DepthCanvas Canvas;
    Canvas.W = Width;
    Canvas.H = Height;
    Canvas.Cut.assign(size_t(Width) * Height, 0.0f);
    Canvas.ScaleX = float(Width) / Derived.Circumference;   // exact, so the seam closes to the texel
    Canvas.ScaleY = Pixels;
    Canvas.OriginY = float(Height) * 0.5f;

    // ② three wrap copies, each one a full pass of cuts followed by its wear bars
    const float Offsets[3] = { -Derived.Circumference, 0.0f, Derived.Circumference };
    for (const float Offset : Offsets)
    {
        Canvas.OriginX = Offset * Canvas.ScaleX;
        DrawLayers(Canvas, Pattern, Specification, Derived);
        DrawWearBars(Canvas, Pattern, Specification, Derived);
    }

    // ③ the micro texture and the wear subtraction, both of which act on the field rather than on a shape
    Field.Width  = Width;
    Field.Height = Height;
    Field.Pixels = Pixels;
    Field.Depth.assign(size_t(Width) * Height, 0.0f);

    std::vector<const TreadLayerSpecification*> NoiseLayers;
    if (!SkipNoise)
    {
        for (const TreadLayerSpecification& Layer : Pattern.Layers)
        {
            if (Layer.Kind == TreadLayerKind::Noise)
            {
                NoiseLayers.push_back(&Layer);
            }
        }
    }

    for (uint32_t Y = 0; Y < Height; ++Y)
    {
        const float Worn = Field.WornAt(Y, Specification, Derived);
        for (uint32_t X = 0; X < Width; ++X)
        {
            const size_t I = size_t(Y) * Width + X;
            float Value = Canvas.Cut[I];
            for (const TreadLayerSpecification* Noise : NoiseLayers)
            {
                const float S = std::max(0.25f, Noise->Scale) * Pixels;
                Value += Noise->Amount * (ValueNoise(float(X) / S, float(Y) / S) - 0.5f)
                       + Noise->Amount * 0.4f * (ValueNoise(float(X) / S * 4.0f, float(Y) / S * 4.0f) - 0.5f);
            }
            Field.Depth[I] = std::max(0.0f, Value - Worn);
        }
    }
    return Field;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      MATERIAL MAPS
//------------------------------------------------------------------------------------------------------------------------

TreadMaterialMap ReadTreadMap(const TreadDepthField& Field, const TreadSpecification& Specification,
                              const TreadDerivedValues& Derived,
                              const TyreAppearanceSpecification& Appearance, TreadMapChannel Channel)
{
    TreadMaterialMap Map;
    if (Field.Width == 0u || Field.Height == 0u)
    {
        return Map;
    }
    Map.Width  = Field.Width;
    Map.Height = Field.Height;
    Map.Texels.assign(size_t(Field.Width) * Field.Height, 0xFF000000u);

    const Rgb Base   = Unpack(Appearance.Rubber);
    const Rgb Worn   = Unpack(0xFF4A4846u);
    const Rgb Groove = Unpack(0xFF0A0A0Au);
    const Rgb Red    = Unpack(Appearance.StripeRed);
    const Rgb Blue   = Unpack(Appearance.StripeBlue);

    const float K            = Field.Pixels;
    const float MillimetresPerPixel = 1.0f / K;
    const float StripeWidth  = std::max(0.5f, Appearance.StripeWidth) * K * 0.5f;
    const float StripeGap    = std::max(1.5f, Appearance.StripeGap) * K * 0.5f;
    const float StripeWave   = std::max(0.0f, Appearance.StripeWave) * K;
    const float StripeOffset = Appearance.StripeOffset * K;
    const float StripePitch  = std::max(4.0f, Appearance.StripePitch) * K;
    const float StripePeriod = std::max(16.0f, 42.0f * K);

    const auto Cover = [&](float Distance) noexcept -> float
    { return std::clamp((StripeWidth + 0.7f - Distance) / 0.7f, 0.0f, 1.0f); };

    const auto PaintAt = [&](float X, float Y, float Centre, float PhaseOffset) noexcept -> float
    {
        const float PX    = X + PhaseOffset;
        const float Cell  = std::floor(PX / StripePitch);
        const float Local = (PX / StripePitch - Cell - 0.5f) * StripePitch;
        switch (Appearance.Pattern)
        {
            case StripePattern::Dotted:
                return Cover(std::sqrt(Local * Local + (Y - Centre) * (Y - Centre)));
            case StripePattern::Dashed:
                return (std::abs(Local) < StripePitch * 0.31f) ? Cover(std::abs(Y - Centre)) : 0.0f;
            case StripePattern::Stitch:
            {
                if (std::abs(Local) >= StripePitch * 0.35f)
                {
                    return 0.0f;
                }
                const float Slope = (int(Cell) % 2) ? 0.65f : -0.65f;
                return Cover(std::abs((Y - Centre) - Local * Slope) / std::sqrt(1.0f + Slope * Slope));
            }
            case StripePattern::ZigZag:
            default:
                return Cover(std::abs(Y - Centre));
        }
    };

    for (uint32_t Y = 0; Y < Field.Height; ++Y)
    {
        const float Wear = Field.WornAt(Y, Specification, Derived);
        for (uint32_t X = 0; X < Field.Width; ++X)
        {
            const size_t I = size_t(Y) * Field.Width + X;
            const float  D = Field.Depth[I];

            if (Channel == TreadMapChannel::Height)
            {
                const uint32_t Q = uint32_t(std::clamp(1.0f - D, 0.0f, 1.0f) * 255.0f + 0.5f);
                Map.Texels[I] = 0xFF000000u | (Q << 16) | (Q << 8) | Q;
                continue;
            }

            if (Channel == TreadMapChannel::Normal)
            {
                const float Left  = Field.Depth[size_t(Y) * Field.Width + (X + Field.Width - 1u) % Field.Width];
                const float Right = Field.Depth[size_t(Y) * Field.Width + (X + 1u) % Field.Width];
                const float Up    = Field.Depth[size_t(Y == 0u ? 0u : Y - 1u) * Field.Width + X];
                const float Down  = Field.Depth[size_t(std::min(Field.Height - 1u, Y + 1u)) * Field.Width + X];
                float NX = (Right - Left) * Specification.TreadDepth / (2.0f * MillimetresPerPixel);
                float NY = (Down - Up) * Specification.TreadDepth / (2.0f * MillimetresPerPixel);
                float NZ = 1.0f;
                const float Length = std::sqrt(NX * NX + NY * NY + NZ * NZ);
                NX /= Length;
                NY /= Length;
                NZ /= Length;
                Map.Texels[I] = Pack(Rgb{ NX * 0.5f + 0.5f, NY * 0.5f + 0.5f, NZ * 0.5f + 0.5f });
                continue;
            }

            const float Grain = ValueNoise(float(X) / 6.0f, float(Y) / 6.0f) * 0.5f
                              + ValueNoise(float(X) / 1.7f, float(Y) / 1.7f) * 0.5f;

            Rgb Colour = Mix(Base, Worn, Wear * 0.75f * (0.7f + 0.6f * Grain));
            Colour = Mix(Colour, Groove, std::min(1.0f, D * 1.2f));
            const float Lift = (Grain - 0.5f) * 0.06f;
            Colour = Rgb{ Colour.R + Lift, Colour.G + Lift, Colour.B + Lift };

            float Paint = 0.0f;
            if (Appearance.FactoryStripes && D < 0.025f && Wear < 0.98f)
            {
                const float Phase = float(X) / StripePeriod - std::floor(float(X) / StripePeriod);
                const float Zig   = (1.0f - 4.0f * std::abs(Phase - 0.5f)) * StripeWave;
                const float Mid   = float(Field.Height) * 0.5f + StripeOffset;
                const float RedPaint  = PaintAt(float(X), float(Y), Mid - StripeGap + Zig, 0.0f);
                const float BluePaint = PaintAt(float(X), float(Y), Mid + StripeGap - Zig, StripePitch * 0.5f);
                const float Fade = 1.0f - Wear * 0.92f;
                if (RedPaint >= BluePaint && RedPaint > 0.0f)
                {
                    Paint = RedPaint * Fade;
                    Colour = Mix(Colour, Red, Paint * 0.92f);
                }
                else if (BluePaint > 0.0f)
                {
                    Paint = BluePaint * Fade;
                    Colour = Mix(Colour, Blue, Paint * 0.92f);
                }
            }

            if (Channel == TreadMapChannel::Roughness)
            {
                const float R = (Paint > 0.0f) ? 0.72f
                              : (D > 0.02f) ? 0.92f
                                            : 0.9f - Wear * 0.42f + (Grain - 0.5f) * 0.15f;
                Map.Texels[I] = Pack(Rgb{ R, R, R });
                continue;
            }
            Map.Texels[I] = Pack(Colour);
        }
    }
    return Map;
}

}   // namespace Frontier
