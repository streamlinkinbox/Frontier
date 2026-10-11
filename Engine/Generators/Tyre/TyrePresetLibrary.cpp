//============================================================================================================================================
//                                                         TYREPRESETLIBRARY.CPP
//============================================================================================================================================
// 📦 The shipped tread designs and the parameter catalogue the property page is generated from.

#include "TyrePresetLibrary.h"

#include <cmath>
#include <cstdio>

namespace Frontier {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                     LAYER DEFAULTS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] TreadLayerSpecification DefaultCircumferential() noexcept
{
    TreadLayerSpecification L;
    L.Kind = TreadLayerKind::Circumferential;
    L.Position = 0.0f;
    L.Width = 8.0f;
    L.DepthFraction = 1.0f;
    L.Zig = 0.0f;
    L.ZigCount = 60.0f;
    return L;
}

[[nodiscard]] TreadLayerSpecification DefaultLateral() noexcept
{
    TreadLayerSpecification L;
    L.Kind = TreadLayerKind::Lateral;
    L.Count = 48.0f;
    L.Angle = 20.0f;
    L.Width = 6.0f;
    L.DepthFraction = 1.0f;
    L.From = -1.0f;
    L.To = 1.0f;
    L.Phase = 0.0f;
    L.Mirror = false;
    L.Curve = 0.0f;
    L.Zig = 0.0f;
    return L;
}

[[nodiscard]] TreadLayerSpecification DefaultChevron() noexcept
{
    TreadLayerSpecification L;
    L.Kind = TreadLayerKind::Chevron;
    L.Count = 40.0f;
    L.Angle = 55.0f;
    L.Width = 7.0f;
    L.DepthFraction = 1.0f;
    L.From = 0.08f;
    L.To = 1.3f;
    L.Phase = 0.0f;
    L.Mirror = true;
    L.Curve = 0.3f;
    L.Zig = 0.0f;
    return L;
}

[[nodiscard]] TreadLayerSpecification DefaultSipe() noexcept
{
    TreadLayerSpecification L;
    L.Kind = TreadLayerKind::Sipe;
    L.Count = 140.0f;
    L.Angle = 0.0f;
    L.Width = 1.2f;
    L.DepthFraction = 0.55f;
    L.From = -1.0f;
    L.To = 1.0f;
    L.Phase = 0.5f;
    L.Mirror = false;
    L.Curve = 0.0f;
    L.Zig = 1.5f;
    return L;
}

[[nodiscard]] TreadLayerSpecification DefaultDimple() noexcept
{
    TreadLayerSpecification L;
    L.Kind = TreadLayerKind::Dimple;
    L.Rows = 3.0f;
    L.Count = 60.0f;
    L.Radius = 3.0f;
    L.DepthFraction = 0.5f;
    L.From = -0.7f;
    L.To = 0.7f;
    L.Stagger = true;
    L.Ring = false;
    return L;
}

[[nodiscard]] TreadLayerSpecification DefaultHexagon() noexcept
{
    TreadLayerSpecification L;
    L.Kind = TreadLayerKind::Hexagon;
    L.Size = 14.0f;
    L.Width = 3.0f;
    L.DepthFraction = 0.8f;
    L.From = -1.0f;
    L.To = 1.0f;
    L.Rotate = 0.0f;
    return L;
}

[[nodiscard]] TreadLayerSpecification DefaultNoise() noexcept
{
    TreadLayerSpecification L;
    L.Kind = TreadLayerKind::Noise;
    L.Amount = 0.06f;
    L.Scale = 3.0f;
    return L;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PRESET CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

/// 📦 A carcass seeded from the generator's defaults and then overridden field by field, which is how the
///    original expressed a preset: a size is a diff against the running tyre, not a whole new document.
struct CarcassEdit
{
    float Width = 245.0f;
    float Aspect = 40.0f;
    float Rim = 18.0f;
    float TreadDepth = 8.0f;
    float TreadFraction = 0.88f;
    float Crown = 2.0f;
    float Shoulder = 18.0f;
    float RimWidthFraction = 0.78f;
};

[[nodiscard]] TreadSpecification Carcass(const CarcassEdit& E) noexcept
{
    TreadSpecification S;
    S.Width = E.Width;
    S.Aspect = E.Aspect;
    S.Rim = E.Rim;
    S.TreadDepth = E.TreadDepth;
    S.TreadFraction = E.TreadFraction;
    S.Crown = E.Crown;
    S.Shoulder = E.Shoulder;
    S.RimWidthFraction = E.RimWidthFraction;
    return S;
}

using Layer = TreadLayerSpecification;

[[nodiscard]] Layer Circ(float Position, float Width, float Zig = 0.0f, float ZigCount = 60.0f,
                         float Depth = 1.0f) noexcept
{
    Layer L = DefaultCircumferential();
    L.Position = Position;
    L.Width = Width;
    L.Zig = Zig;
    L.ZigCount = ZigCount;
    L.DepthFraction = Depth;
    return L;
}

[[nodiscard]] Layer Lat(float Count, float Angle, float Width, float From, float To, float Phase,
                        float Curve = 0.0f) noexcept
{
    Layer L = DefaultLateral();
    L.Count = Count;
    L.Angle = Angle;
    L.Width = Width;
    L.From = From;
    L.To = To;
    L.Phase = Phase;
    L.Curve = Curve;
    return L;
}

[[nodiscard]] Layer Chev(float Count, float Angle, float Width, float From, float To, float Curve,
                         float Phase = 0.0f, float Depth = 1.0f) noexcept
{
    Layer L = DefaultChevron();
    L.Count = Count;
    L.Angle = Angle;
    L.Width = Width;
    L.From = From;
    L.To = To;
    L.Curve = Curve;
    L.Phase = Phase;
    L.DepthFraction = Depth;
    return L;
}

[[nodiscard]] Layer Sipe(float Count, float Angle, float Width, float Depth, float From, float To,
                         bool Mirror = false, float Zig = 1.5f, float Phase = 0.5f) noexcept
{
    Layer L = DefaultSipe();
    L.Count = Count;
    L.Angle = Angle;
    L.Width = Width;
    L.DepthFraction = Depth;
    L.From = From;
    L.To = To;
    L.Mirror = Mirror;
    L.Zig = Zig;
    L.Phase = Phase;
    return L;
}

[[nodiscard]] Layer Dimple(float Rows, float Count, float Radius, float Depth, float From, float To,
                           bool Stagger) noexcept
{
    Layer L = DefaultDimple();
    L.Rows = Rows;
    L.Count = Count;
    L.Radius = Radius;
    L.DepthFraction = Depth;
    L.From = From;
    L.To = To;
    L.Stagger = Stagger;
    return L;
}

[[nodiscard]] Layer Hex(float Size, float Width, float Depth, float From, float To) noexcept
{
    Layer L = DefaultHexagon();
    L.Size = Size;
    L.Width = Width;
    L.DepthFraction = Depth;
    L.From = From;
    L.To = To;
    return L;
}

[[nodiscard]] Layer Noise(float Amount, float Scale) noexcept
{
    Layer L = DefaultNoise();
    L.Amount = Amount;
    L.Scale = Scale;
    return L;
}

[[nodiscard]] TyrePreset Make(const char* Name, const char* Kind, const CarcassEdit& Edit,
                              std::vector<Layer> Layers)
{
    TyrePreset Preset;
    Preset.Name = Name;
    Preset.Kind = Kind;
    Preset.Carcass = Carcass(Edit);
    Preset.Pattern.Name = Name;
    Preset.Pattern.Layers = std::move(Layers);
    return Preset;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE LIBRARY
//------------------------------------------------------------------------------------------------------------------------

const std::vector<TyrePreset>& TyrePresets()
{
    static const std::vector<TyrePreset> Library = []
    {
        std::vector<TyrePreset> P;
        P.reserve(14u);

        P.push_back(Make("GZero", "Slick",
            { 305.0f, 30.0f, 19.0f, 4.0f, 0.93f, 1.2f, 26.0f, 0.78f },
            { Dimple(2, 4, 2.5f, 0.7f, -0.55f, 0.55f, false), Noise(0.04f, 2.0f) }));

        P.push_back(Make("Grizzly Magnum", "Off-road",
            { 285.0f, 70.0f, 17.0f, 15.0f, 0.92f, 3.0f, 14.0f, 0.78f },
            { Circ(0.0f, 12.0f, 6.0f, 28.0f), Circ(0.55f, 11.0f, 5.0f, 28.0f), Circ(-0.55f, 11.0f, 5.0f, 28.0f),
              Lat(28, 18.0f, 12.0f, 0.3f, 1.3f, 0.0f), Lat(28, -18.0f, 12.0f, -1.3f, -0.3f, 0.5f),
              Lat(28, 0.0f, 9.0f, -0.3f, 0.3f, 0.25f),
              Sipe(56, 12.0f, 1.5f, 0.4f, -1.3f, 1.3f, false, 2.0f, 0.5f) }));

        P.push_back(Make("Vortex R1", "Grip / UHP",
            { 255.0f, 35.0f, 19.0f, 7.0f, 0.88f, 1.8f, 16.0f, 0.78f },
            { Circ(0.22f, 7.0f), Circ(-0.22f, 7.0f), Chev(44, 58.0f, 6.0f, 0.3f, 1.3f, 0.35f),
              Sipe(88, 60.0f, 1.2f, 0.5f, 0.45f, 1.25f, true, 1.5f, 0.5f) }));

        P.push_back(Make("Smokestack D", "Drift",
            { 235.0f, 40.0f, 18.0f, 6.0f, 0.88f, 1.5f, 20.0f, 0.78f },
            { Circ(0.35f, 6.0f), Circ(-0.35f, 6.0f), Lat(24, 35.0f, 5.0f, 0.5f, 1.3f, 0.0f),
              Lat(24, -35.0f, 5.0f, -1.3f, -0.5f, 0.5f) }));

        P.push_back(Make("Urban Pulse", "Street",
            { 205.0f, 55.0f, 16.0f, 8.0f, 0.88f, 2.2f, 15.0f, 0.78f },
            { Circ(0.6f, 8.0f), Circ(0.2f, 7.0f), Circ(-0.2f, 7.0f), Circ(-0.6f, 8.0f),
              Lat(64, 10.0f, 4.0f, -1.3f, 1.3f, 0.0f),
              Sipe(128, 10.0f, 1.0f, 0.6f, -1.3f, 1.3f, false, 1.2f, 0.5f) }));

        P.push_back(Make("Monsoon Cut", "Wet",
            { 245.0f, 45.0f, 18.0f, 10.0f, 0.88f, 2.5f, 15.0f, 0.78f },
            { Circ(0.0f, 12.0f), Circ(0.5f, 8.0f), Circ(-0.5f, 8.0f),
              Chev(36, 50.0f, 9.0f, 0.05f, 1.3f, 0.5f),
              Chev(36, 50.0f, 3.0f, 0.05f, 1.3f, 0.5f, 0.5f, 0.6f) }));

        P.push_back(Make("Shadow Line", "Grip / UHP",
            { 265.0f, 35.0f, 20.0f, 7.0f, 0.88f, 1.6f, 17.0f, 0.78f },
            { Circ(-0.15f, 9.0f), Circ(-0.55f, 8.0f), Circ(0.4f, 6.0f),
              Lat(56, 22.0f, 4.0f, -1.3f, -0.15f, 0.0f), Lat(22, -25.0f, 6.0f, 0.4f, 1.3f, 0.3f, 0.3f),
              Sipe(112, 22.0f, 1.0f, 0.5f, -1.3f, -0.15f, false, 0.0f, 0.5f) }));

        P.push_back(Make("HexaGrip Nova", "Street",
            { 225.0f, 45.0f, 17.0f, 8.0f, 0.88f, 2.0f, 16.0f, 0.78f },
            { Hex(13.0f, 3.5f, 0.9f, -1.3f, 1.3f), Circ(0.0f, 7.0f) }));

        P.push_back(Make("Dimple Rave", "Semi-slick",
            { 245.0f, 40.0f, 18.0f, 5.0f, 0.88f, 1.5f, 20.0f, 0.78f },
            { Dimple(7, 90, 3.2f, 0.7f, -1.1f, 1.1f, true), Circ(0.0f, 5.0f, 0.0f, 60.0f, 0.9f) }));

        P.push_back(Make("Frostbite Sipe", "Winter",
            { 215.0f, 60.0f, 16.0f, 10.0f, 0.88f, 2.5f, 14.0f, 0.78f },
            { Circ(0.45f, 7.0f, 2.0f, 80.0f), Circ(-0.45f, 7.0f, 2.0f, 80.0f),
              Chev(40, 35.0f, 6.0f, 0.05f, 1.3f, 0.1f),
              Sipe(200, 35.0f, 1.1f, 0.7f, 0.05f, 1.3f, true, 1.6f, 0.33f),
              Sipe(200, 35.0f, 1.1f, 0.7f, 0.05f, 1.3f, true, 1.6f, 0.66f) }));

        P.push_back(Make("Kestrel TC", "Semi-slick",
            { 275.0f, 35.0f, 18.0f, 5.0f, 0.88f, 1.2f, 22.0f, 0.78f },
            { Circ(0.3f, 9.0f), Circ(-0.3f, 9.0f), Lat(18, 40.0f, 5.0f, 0.65f, 1.3f, 0.0f),
              Lat(18, -40.0f, 5.0f, -1.3f, -0.65f, 0.5f), Noise(0.03f, 3.0f) }));

        P.push_back(Make("Ravager Mud", "Rally",
            { 235.0f, 75.0f, 15.0f, 14.0f, 0.90f, 4.0f, 12.0f, 0.78f },
            { Lat(22, 30.0f, 14.0f, 0.1f, 1.3f, 0.0f), Lat(22, -30.0f, 14.0f, -1.3f, -0.1f, 0.5f),
              Circ(0.0f, 9.0f, 8.0f, 22.0f), Lat(22, 0.0f, 10.0f, -0.1f, 0.1f, 0.25f) }));

        P.push_back(Make("Comet Drag", "Drag",
            { 375.0f, 50.0f, 15.0f, 4.0f, 0.95f, 1.0f, 30.0f, 0.85f },
            { Dimple(2, 3, 3.0f, 0.8f, -0.7f, 0.7f, true), Noise(0.05f, 2.0f) }));

        P.push_back(Make("Talon Trail", "Off-road",
            { 265.0f, 65.0f, 17.0f, 13.0f, 0.88f, 3.0f, 13.0f, 0.78f },
            { Hex(22.0f, 8.0f, 1.0f, -1.3f, 1.3f), Circ(0.0f, 9.0f, 4.0f, 30.0f) }));

        return P;
    }();
    return Library;
}

const std::vector<std::string>& TyreKinds()
{
    static const std::vector<std::string> Kinds = {
        "Slick", "Semi-slick", "Drift", "Street", "Grip / UHP",
        "Wet", "Winter", "Rally", "Off-road", "Drag"
    };
    return Kinds;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      LAYER CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

const std::vector<TreadLayerCatalogueEntry>& TreadLayerCatalogue()
{
    static const std::vector<TreadLayerCatalogueEntry> Catalogue = {
        { TreadLayerKind::Circumferential, "Circumferential groove", 0xFF38C46Du, DefaultCircumferential() },
        { TreadLayerKind::Lateral,         "Lateral groove",         0xFFD6C93Au, DefaultLateral() },
        { TreadLayerKind::Chevron,         "Chevron / V groove",     0xFFF0A04Bu, DefaultChevron() },
        { TreadLayerKind::Sipe,            "Sipes",                  0xFF9AA4FFu, DefaultSipe() },
        { TreadLayerKind::Dimple,          "Circular pattern",       0xFF4FC3F7u, DefaultDimple() },
        { TreadLayerKind::Hexagon,         "Hexagon cells",          0xFFE5423Fu, DefaultHexagon() },
        { TreadLayerKind::Noise,           "Micro texture",          0xFF8A8D94u, DefaultNoise() }
    };
    return Catalogue;
}

const TreadLayerCatalogueEntry& DescribeLayerKind(TreadLayerKind Kind)
{
    const std::vector<TreadLayerCatalogueEntry>& Catalogue = TreadLayerCatalogue();
    for (const TreadLayerCatalogueEntry& Entry : Catalogue)
    {
        if (Entry.Kind == Kind)
        {
            return Entry;
        }
    }
    return Catalogue.front();
}

std::string SummariseLayer(const TreadLayerSpecification& Layer)
{
    char Text[64] = {};
    switch (Layer.Kind)
    {
        case TreadLayerKind::Circumferential:
            std::snprintf(Text, sizeof(Text), "@%g · %gmm", double(Layer.Position), double(Layer.Width));
            break;
        case TreadLayerKind::Hexagon:
            std::snprintf(Text, sizeof(Text), "%gmm cells", double(Layer.Size));
            break;
        case TreadLayerKind::Dimple:
            std::snprintf(Text, sizeof(Text), "%g×%g", double(Layer.Rows), double(Layer.Count));
            break;
        case TreadLayerKind::Noise:
            std::snprintf(Text, sizeof(Text), "%g", double(Layer.Amount));
            break;
        default:
            std::snprintf(Text, sizeof(Text), "×%g · %g°", double(Layer.Count), double(Layer.Angle));
            break;
    }
    return std::string(Text);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PARAMETER TABLE
//------------------------------------------------------------------------------------------------------------------------

namespace {

using Field = TreadLayerField;
using Descriptor = TreadLayerFieldDescriptor;

const Descriptor PositionField { Field::Position,      "Across position",  -1.3f,  1.3f, 0.01f };
const Descriptor WidthField    { Field::Width,         "Width (mm)",        0.5f, 30.0f, 0.1f  };
const Descriptor DepthField    { Field::DepthFraction, "Depth (×tread)",    0.05f, 1.0f, 0.01f };
const Descriptor ZigField      { Field::Zig,           "Zig-zag (mm)",      0.0f, 12.0f, 0.1f  };
const Descriptor ZigCountField { Field::ZigCount,      "Zig-zag count",     4.0f, 240.0f, 1.0f };
const Descriptor CountField    { Field::Count,         "Count around",      2.0f, 300.0f, 1.0f };
const Descriptor AngleField    { Field::Angle,         "Angle (°)",       -80.0f, 80.0f, 1.0f  };
const Descriptor FromField     { Field::From,          "From (across)",    -1.3f,  1.3f, 0.01f };
const Descriptor ToField       { Field::To,            "To (across)",      -1.3f,  1.3f, 0.01f };
const Descriptor PhaseField    { Field::Phase,         "Phase",             0.0f,  1.0f, 0.01f };
const Descriptor CurveField    { Field::Curve,         "Curvature",        -1.5f,  1.5f, 0.01f };
const Descriptor RowsField     { Field::Rows,          "Rows",              1.0f, 14.0f, 1.0f  };
const Descriptor RadiusField   { Field::Radius,        "Radius (mm)",       0.5f, 15.0f, 0.1f  };
const Descriptor SizeField     { Field::Size,          "Cell size (mm)",    4.0f, 40.0f, 0.5f  };
const Descriptor RotateField   { Field::Rotate,        "Cell rotation (°)", 0.0f, 60.0f, 1.0f  };
const Descriptor AmountField   { Field::Amount,        "Amount",            0.0f,  0.5f, 0.01f };
const Descriptor ScaleField    { Field::Scale,         "Scale",             1.0f, 12.0f, 0.5f  };
const Descriptor MirrorField   { Field::Mirror,        "Mirror (V)",        0.0f,  1.0f, 1.0f, true };
const Descriptor StaggerField  { Field::Stagger,       "Stagger rows",      0.0f,  1.0f, 1.0f, true };
const Descriptor RingField     { Field::Ring,          "Rings instead of dots", 0.0f, 1.0f, 1.0f, true };

}   // namespace

std::vector<TreadLayerFieldDescriptor> DescribeLayerFields(TreadLayerKind Kind)
{
    switch (Kind)
    {
        case TreadLayerKind::Circumferential:
            return { PositionField, WidthField, DepthField, ZigField, ZigCountField };
        case TreadLayerKind::Lateral:
        case TreadLayerKind::Sipe:
            return { CountField, AngleField, WidthField, DepthField, FromField, ToField, PhaseField, MirrorField, CurveField, ZigField };
        case TreadLayerKind::Chevron:
            return { CountField, AngleField, WidthField, DepthField, FromField, ToField, PhaseField, MirrorField, CurveField, ZigField };
        case TreadLayerKind::Dimple:
            return { RowsField, CountField, RadiusField, DepthField, FromField, ToField, StaggerField, RingField };
        case TreadLayerKind::Hexagon:
            return { SizeField, WidthField, DepthField, FromField, ToField, RotateField };
        case TreadLayerKind::Noise:
            return { AmountField, ScaleField };
        default:
            return {};
    }
}

float ReadLayerField(const TreadLayerSpecification& Layer, TreadLayerField Field) noexcept
{
    switch (Field)
    {
        case TreadLayerField::Position:      return Layer.Position;
        case TreadLayerField::Width:         return Layer.Width;
        case TreadLayerField::DepthFraction: return Layer.DepthFraction;
        case TreadLayerField::Zig:           return Layer.Zig;
        case TreadLayerField::ZigCount:      return Layer.ZigCount;
        case TreadLayerField::Count:         return Layer.Count;
        case TreadLayerField::Angle:         return Layer.Angle;
        case TreadLayerField::From:          return Layer.From;
        case TreadLayerField::To:            return Layer.To;
        case TreadLayerField::Phase:         return Layer.Phase;
        case TreadLayerField::Curve:         return Layer.Curve;
        case TreadLayerField::Rows:          return Layer.Rows;
        case TreadLayerField::Radius:        return Layer.Radius;
        case TreadLayerField::Size:          return Layer.Size;
        case TreadLayerField::Rotate:        return Layer.Rotate;
        case TreadLayerField::Amount:        return Layer.Amount;
        case TreadLayerField::Scale:         return Layer.Scale;
        case TreadLayerField::Mirror:        return Layer.Mirror ? 1.0f : 0.0f;
        case TreadLayerField::Stagger:       return Layer.Stagger ? 1.0f : 0.0f;
        case TreadLayerField::Ring:          return Layer.Ring ? 1.0f : 0.0f;
        default:                             return 0.0f;
    }
}

void WriteLayerField(TreadLayerSpecification& Layer, TreadLayerField Field, float Value) noexcept
{
    switch (Field)
    {
        case TreadLayerField::Position:      Layer.Position = Value; break;
        case TreadLayerField::Width:         Layer.Width = Value; break;
        case TreadLayerField::DepthFraction: Layer.DepthFraction = Value; break;
        case TreadLayerField::Zig:           Layer.Zig = Value; break;
        case TreadLayerField::ZigCount:      Layer.ZigCount = Value; break;
        case TreadLayerField::Count:         Layer.Count = Value; break;
        case TreadLayerField::Angle:         Layer.Angle = Value; break;
        case TreadLayerField::From:          Layer.From = Value; break;
        case TreadLayerField::To:            Layer.To = Value; break;
        case TreadLayerField::Phase:         Layer.Phase = Value; break;
        case TreadLayerField::Curve:         Layer.Curve = Value; break;
        case TreadLayerField::Rows:          Layer.Rows = Value; break;
        case TreadLayerField::Radius:        Layer.Radius = Value; break;
        case TreadLayerField::Size:          Layer.Size = Value; break;
        case TreadLayerField::Rotate:        Layer.Rotate = Value; break;
        case TreadLayerField::Amount:        Layer.Amount = Value; break;
        case TreadLayerField::Scale:         Layer.Scale = Value; break;
        case TreadLayerField::Mirror:        Layer.Mirror = Value != 0.0f; break;
        case TreadLayerField::Stagger:       Layer.Stagger = Value != 0.0f; break;
        case TreadLayerField::Ring:          Layer.Ring = Value != 0.0f; break;
        default:                             break;
    }
}

TreadLayerSpecification MirrorLayer(const TreadLayerSpecification& Layer) noexcept
{
    TreadLayerSpecification Copy = Layer;
    Copy.Position = -Copy.Position;

    const float From = -Copy.To;
    const float To   = -Copy.From;
    Copy.From = From;
    Copy.To   = To;

    if (!Copy.Mirror)
    {
        Copy.Angle = -Copy.Angle;
    }
    Copy.Phase = std::fmod(Copy.Phase + 0.5f, 1.0f);
    return Copy;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                     GENERATED DESIGNS
//------------------------------------------------------------------------------------------------------------------------

namespace {

/// 📦 A small deterministic generator. Deterministic matters: a design the user liked has to be reachable
///    again from the same seed, and a clock-seeded generator makes that impossible.
class Roll
{
public:
    explicit Roll(uint32_t Seed) noexcept : State_(Seed * 2654435761u + 1u) {}

    [[nodiscard]] float Unit() noexcept
    {
        State_ ^= State_ << 13;
        State_ ^= State_ >> 17;
        State_ ^= State_ << 5;
        return float(State_ & 0x00FFFFFFu) / float(0x01000000u);
    }

    [[nodiscard]] float Between(float Low, float High) noexcept { return Low + Unit() * (High - Low); }

    [[nodiscard]] int Whole(int Low, int High) noexcept
    {
        return Low + int(Unit() * float(High - Low + 1)) % std::max(1, High - Low + 1);
    }

    template <typename T>
    [[nodiscard]] const T& Pick(const std::vector<T>& From) noexcept
    {
        return From[size_t(Whole(0, int(From.size()) - 1))];
    }

private:
    uint32_t State_ = 1u;
};

const std::vector<std::string> NamePrefixes = {
    "Grizzly", "Vortex", "Phantom", "Raptor", "Kestrel", "Nomad", "Viper", "Comet", "Ghost", "Ember",
    "Frost", "Monsoon", "Talon", "Rogue", "Sabre", "Nitro", "Onyx", "Havoc", "Quasar", "Slate",
    "Cobra", "Tempest", "Falcon"
};
const std::vector<std::string> NameSuffixes = {
    "Magnum", "R1", "GT", "Zero", "Cut", "Sport", "X", "Drift", "Evo", "Nova",
    "S3", "Grip", "Blade", "Trail", "RS", "TC", "Rain", "Ice"
};

}   // namespace

std::string RandomTyreName(uint32_t Seed)
{
    Roll R(Seed);
    const float Which = R.Unit();
    if (Which < 0.2f)
    {
        static const std::vector<std::string> Letters = { "G", "Z", "K", "V", "T", "X", "R", "Q" };
        static const std::vector<std::string> Tails   = { "Zero", "One", "Force", "Edge", "Line" };
        return R.Pick(Letters) + R.Pick(Tails);
    }
    if (Which < 0.35f)
    {
        return R.Pick(NamePrefixes) + " " + R.Pick(NameSuffixes) + " " + std::to_string(R.Whole(2, 9));
    }
    return R.Pick(NamePrefixes) + " " + R.Pick(NameSuffixes);
}

void RandomiseTyreDesign(TreadSpecification& Carcass, TreadPatternSpecification& Pattern,
                         std::string& Kind, std::string& Name, uint32_t Seed)
{
    Roll R(Seed);
    Kind = R.Pick(TyreKinds());

    // ① the canonical size for the category, jittered by one step either way
    struct Canonical { const char* Kind; float Width; float Aspect; float Rim; float Depth; };
    static const Canonical CanonicalSizes[10] = {
        { "Slick",      305.0f, 30.0f, 19.0f,  4.0f }, { "Semi-slick", 265.0f, 35.0f, 18.0f,  5.0f },
        { "Drift",      235.0f, 40.0f, 18.0f,  6.0f }, { "Street",     205.0f, 55.0f, 16.0f,  8.0f },
        { "Grip / UHP", 255.0f, 35.0f, 19.0f,  7.0f }, { "Wet",        245.0f, 45.0f, 18.0f, 10.0f },
        { "Winter",     215.0f, 60.0f, 16.0f, 10.0f }, { "Rally",      235.0f, 70.0f, 15.0f, 14.0f },
        { "Off-road",   285.0f, 70.0f, 17.0f, 15.0f }, { "Drag",       375.0f, 50.0f, 15.0f,  4.0f }
    };
    const Canonical* Size = &CanonicalSizes[0];
    for (const Canonical& Entry : CanonicalSizes)
    {
        if (Kind == Entry.Kind)
        {
            Size = &Entry;
        }
    }
    Carcass.Width      = Size->Width + float(R.Whole(-2, 2)) * 10.0f;
    Carcass.Aspect     = Size->Aspect + float(R.Whole(-1, 1)) * 5.0f;
    Carcass.Rim        = Size->Rim;
    Carcass.TreadDepth = Size->Depth;
    Carcass.Crown      = R.Between(1.0f, 3.0f);
    Carcass.Shoulder   = R.Between(12.0f, 26.0f);
    Carcass.Wear       = 0.0f;

    // ② the water channels, which decide where the blocks can be
    Pattern.Name = Name;
    Pattern.Layers.clear();
    const bool Smooth = (Kind == "Slick" || Kind == "Drag");
    const int Grooves = Smooth ? 0
                      : (Kind == "Semi-slick" || Kind == "Drift") ? R.Whole(1, 2)
                                                                  : R.Whole(2, 4);
    const std::vector<std::vector<float>> GrooveSeats = {
        {}, { 0.0f }, { -0.3f, 0.3f }, { -0.5f, 0.0f, 0.5f }, { -0.6f, -0.2f, 0.2f, 0.6f }
    };
    const float Zig = (Kind == "Off-road" || Kind == "Rally") ? R.Between(3.0f, 7.0f)
                    : (Kind == "Winter")                      ? R.Between(0.0f, 2.0f)
                                                              : 0.0f;
    for (const float Seat : GrooveSeats[size_t(Grooves)])
    {
        Pattern.Layers.push_back(Circ(Seat, R.Between(5.0f, 11.0f), Zig, float(R.Whole(22, 80))));
    }

    // ③ the blocks, in one of five families
    static const std::vector<std::string> BlockStyles = { "sym", "dir", "asym", "hex", "dot" };
    const std::string Style = R.Pick(BlockStyles);
    if (Smooth)
    {
        Pattern.Layers.push_back(Dimple(2, float(R.Whole(3, 6)), 2.5f, 0.7f, -0.6f, 0.6f, false));
        Pattern.Layers.push_back(Noise(0.04f, 2.0f));
    }
    else if (Style == "dir")
    {
        Pattern.Layers.push_back(Chev(float(R.Whole(30, 50)), float(R.Whole(35, 65)),
                                      R.Between(4.0f, 9.0f), R.Between(0.05f, 0.3f), 1.3f,
                                      R.Between(0.0f, 0.6f)));
    }
    else if (Style == "asym")
    {
        Pattern.Layers.push_back(Lat(float(R.Whole(40, 64)), float(R.Whole(10, 30)), R.Between(3.0f, 5.0f),
                                     -1.3f, R.Between(-0.3f, -0.1f), 0.0f));
        Pattern.Layers.push_back(Lat(float(R.Whole(16, 26)), -float(R.Whole(15, 40)), R.Between(4.0f, 7.0f),
                                     R.Between(0.2f, 0.5f), 1.3f, 0.0f, 0.3f));
    }
    else if (Style == "hex")
    {
        Pattern.Layers.push_back(Hex(R.Between(10.0f, 24.0f), R.Between(3.0f, 7.0f),
                                     R.Between(0.7f, 1.0f), -1.3f, 1.3f));
    }
    else if (Style == "dot")
    {
        Layer Dots = Dimple(float(R.Whole(3, 8)), float(R.Whole(40, 100)), R.Between(2.0f, 5.0f),
                            R.Between(0.4f, 0.9f), -1.1f, 1.1f, true);
        Dots.Ring = R.Unit() < 0.3f;
        Pattern.Layers.push_back(Dots);
    }
    else
    {
        const float Angle = float(R.Whole(-30, 30));
        const float Count = float(R.Whole(20, 64));
        Pattern.Layers.push_back(Lat(Count,  Angle, R.Between(4.0f, 10.0f),  0.2f,  1.3f, 0.0f));
        Pattern.Layers.push_back(Lat(Count, -Angle, R.Between(4.0f, 10.0f), -1.3f, -0.2f, 0.5f));
    }

    // ④ the siping, cut into whatever blocks the first three steps left standing
    if (Kind == "Winter" || (Kind == "Street" && R.Unit() < 0.7f))
    {
        Pattern.Layers.push_back(Sipe(float(R.Whole(100, 220)), float(R.Whole(-30, 30)), 1.1f,
                                      R.Between(0.4f, 0.7f), -1.3f, 1.3f, false,
                                      R.Between(0.0f, 2.0f), 0.5f));
    }

    Name = RandomTyreName(Seed * 2246822519u + 7u);
    Pattern.Name = Name;
}

}   // namespace Frontier
