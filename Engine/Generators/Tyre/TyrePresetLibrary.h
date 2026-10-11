//============================================================================================================================================
//                                                          TYREPRESETLIBRARY.H
//============================================================================================================================================
// 📦 The generator's shipped content: the named tread designs, what each layer kind defaults to, and the range and label every control reads.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "TreadPatternSpecification.h"
#include "TreadSpecification.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        PRESETS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 One shipped design: a named pattern together with the carcass it was drawn for.
/// note  The carcass travels with the pattern because a tread is authored against a size. A mud pattern with
///       14 mm blocks on a 30-series carcass is not the same design at a smaller depth, it is a different
///       tyre, so applying a preset seats both halves at once.
/// tag   schema
struct TyrePreset
{
    std::string                  Name;
    std::string                  Kind;         // [-] - the marketing category, one of TyreKinds()
    TreadSpecification           Carcass;
    TreadPatternSpecification    Pattern;
};

/// 📦 The shipped designs, in the order the preset grid lists them.
/// out   span of TyrePreset  fourteen entries
/// tag   api, nonthrowing
[[nodiscard]] const std::vector<TyrePreset>& TyrePresets();

/// 📦 The categories a tread may be filed under.
/// tag   api, nonthrowing
[[nodiscard]] const std::vector<std::string>& TyreKinds();

//------------------------------------------------------------------------------------------------------------------------
//                                                      LAYER CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 What one layer kind is called and what colour marks it in the layer list.
/// tag   schema
struct TreadLayerCatalogueEntry
{
    TreadLayerKind            Kind    = TreadLayerKind::Circumferential;
    const char*               Label   = "";
    uint32_t                  Colour  = 0xFFFFFFFFu;   // [-] - ARGB
    TreadLayerSpecification   Default;
};

/// 📦 Every layer kind the Add control offers, in catalogue order.
/// tag   api, nonthrowing
[[nodiscard]] const std::vector<TreadLayerCatalogueEntry>& TreadLayerCatalogue();

/// 📦 The catalogue entry for one kind.
/// tag   api, nonthrowing
[[nodiscard]] const TreadLayerCatalogueEntry& DescribeLayerKind(TreadLayerKind Kind);

/// 📦 The one-line summary the layer list shows beside the kind: what distinguishes this layer from another
///    of the same kind, which is the only thing the row has room to say.
/// tag   api, allocating, nonthrowing
[[nodiscard]] std::string SummariseLayer(const TreadLayerSpecification& Layer);

//------------------------------------------------------------------------------------------------------------------------
//                                                     PARAMETER TABLE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Which scalar of a layer a control is bound to. The enumeration exists so the property page can be a
///    loop over the fields a kind reads rather than a hand-written form per kind.
/// tag   schema
enum class TreadLayerField : uint8_t
{
    Position = 0, Width, DepthFraction, Zig, ZigCount, Count, Angle, From, To, Phase, Curve,
    Rows, Radius, Size, Rotate, Amount, Scale, Mirror, Stagger, Ring
};

/// 📦 One editable field: its label, its range, and whether it is a scalar or a flag.
/// tag   schema
struct TreadLayerFieldDescriptor
{
    TreadLayerField Field   = TreadLayerField::Position;
    const char*     Label   = "";
    float           Minimum = 0.0f;
    float           Maximum = 1.0f;
    float           Step    = 0.01f;
    bool            Flag    = false;   // [-] - a toggle rather than a slider
};

/// 📦 The fields one layer kind exposes, in the order the property page lists them.
/// note  Order is the order the generator's own property page used, which is the order the fields were
///       authored in rather than anything alphabetical — a reader looking for Count finds it where it has
///       always been.
/// tag   api, allocating, nonthrowing
[[nodiscard]] std::vector<TreadLayerFieldDescriptor> DescribeLayerFields(TreadLayerKind Kind);

/// 📦 Reads one field off a layer.
/// tag   api, nonthrowing
[[nodiscard]] float ReadLayerField(const TreadLayerSpecification& Layer, TreadLayerField Field) noexcept;

/// 📦 Writes one field back to a layer.
/// tag   api, nonthrowing
void WriteLayerField(TreadLayerSpecification& Layer, TreadLayerField Field, float Value) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                     GENERATED DESIGNS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 A plausible tyre model name.
/// in    Seed  [-]  any value; the same seed always yields the same name
/// tag   api, allocating, nonthrowing
[[nodiscard]] std::string RandomTyreName(uint32_t Seed);

/// 📦 Invents a whole design: a category, a carcass sized for it, and a layer sequence built the way that
///    category's treads are built.
/// in    Seed      [-]  any value; the same seed always yields the same tyre
/// out   Carcass   [-]  seated from the category's canonical size, jittered
/// out   Pattern   [-]  the invented layer sequence
/// out   Kind      [-]  the category chosen
/// out   Name      [-]  a name to match
/// note  The grooves come first and the siping last, because that is the order a tread is actually designed
///       in: the water channels decide the block layout, and the sipes are cut into whatever blocks remain.
/// tag   api, allocating, nonthrowing
void RandomiseTyreDesign(TreadSpecification& Carcass, TreadPatternSpecification& Pattern,
                         std::string& Kind, std::string& Name, uint32_t Seed);

/// 📦 Mirrors a layer about the centreline, which is the one edit that is a whole button rather than a field.
/// note  Mirroring is not negating Position alone: the lateral span reverses end for end, the lean flips
///       unless the layer is already mirrored about the centre itself, and the phase moves half a repeat so
///       the copy interleaves with the original instead of landing on top of it.
/// tag   api, nonthrowing
[[nodiscard]] TreadLayerSpecification MirrorLayer(const TreadLayerSpecification& Layer) noexcept;

}   // namespace Frontier
