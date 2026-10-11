//============================================================================================================================================
//                                                          TREADPATTERNRASTER.H
//============================================================================================================================================
// 📦 The tread pattern rasterised into the unrolled depth field, and the colour, height, normal and roughness maps read off it.

#pragma once

#include <cstdint>
#include <vector>

#include "TreadPatternSpecification.h"
#include "TreadSpecification.h"
#include "TyreAppearanceSpecification.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                       DEPTH FIELD
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The pattern as a depth field over the unrolled tread: 0 is the moulded surface, 1 is a full-depth cut.
/// note  The field wraps in the circumferential axis and does not wrap across. Width is chosen so that one
///       texel is square in millimetres, which is why the aspect of the map changes with the tyre and is not
///       a constant the caller picks.
/// tag   schema
struct TreadDepthField
{
    uint32_t           Width  = 0u;       // [px] - around the circumference
    uint32_t           Height = 0u;       // [px] - across the moulded surface
    float              Pixels = 1.0f;     // [px/mm] - uniform, by construction
    std::vector<float> Depth;             // [-]  - Width × Height, row-major, 0 to 1

    [[nodiscard]] float At(uint32_t X, uint32_t Y) const noexcept
    {
        return Depth[static_cast<size_t>(Y) * Width + X];
    }

    /// 📦 How much of this row has already been worn away, which the raster subtracts before anything else.
    [[nodiscard]] float WornAt(uint32_t Y, const TreadSpecification& Specification,
                               const TreadDerivedValues& Derived) const noexcept;
};

/// 📦 Rasterises the layer sequence into a depth field, exactly as the generator's own renderer does.
/// in    Pattern        [-]   the ordered layer sequence; Noise layers perturb the field after the cuts
/// in    Specification  [-]   carcass parameters, read for TreadDepth, Wear and WearBias
/// in    Derived        [-]   values from DeriveTreadValues for the same specification
/// in    Height         [px]  rows across the moulded surface; width follows from the aspect
/// in    SkipNoise      [-]   leave the micro texture off, which the preset thumbnails want
/// out   TreadDepthField      the field, already wear-subtracted
/// note  📐 Layers composite by darken, not by painting over: a shallow layer crossing a deep one must not
///       fill the deep groove back in. Each cut is drawn at grey 1 − DepthFraction and the darker of the two
///       wins, so the deepest layer touching a texel owns it however late the shallow one was authored.
/// note  ⚠️ Every layer is drawn three times, at −circumference, 0 and +circumference. A groove leaning at
///       an angle leaves the domain on one side and must re-enter on the other with its ends matching to the
///       texel, and wrapping the coordinate instead would break every stroke that straddles the seam.
/// cost  🚩 O(Width × Height) plus the stroke work; a 384-row field on a 2.6 m tyre is about 1.1 Mpx.
/// tag   api, allocating, nonthrowing
[[nodiscard]] TreadDepthField RasterizeTreadPattern(const TreadPatternSpecification& Pattern,
                                                    const TreadSpecification& Specification,
                                                    const TreadDerivedValues& Derived,
                                                    uint32_t Height, bool SkipNoise = false);

//------------------------------------------------------------------------------------------------------------------------
//                                                      MATERIAL MAPS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Which of the four maps a view is asking for.
/// tag   schema
enum class TreadMapChannel : uint8_t
{
    Colour    = 0,
    Height    = 1,
    Normal    = 2,
    Roughness = 3
};

/// 📦 One rasterised map, ready to be handed to a texture or drawn straight into a panel.
/// tag   schema
struct TreadMaterialMap
{
    uint32_t              Width  = 0u;
    uint32_t              Height = 0u;
    std::vector<uint32_t> Texels;        // [-] - ARGB, row-major
};

/// 📦 Reads one material map off a depth field.
/// in    Field          [-]   the rasterised pattern
/// in    Specification  [-]   read for TreadDepth and Wear
/// in    Derived        [-]   values from DeriveTreadValues for the same specification
/// in    Appearance     [-]   compound colour and the factory stripe controls
/// in    Channel        [-]   which map to produce
/// out   TreadMaterialMap     the same dimensions as the field
/// note  The contact surface greys as it wears while the groove floors stay dark, and the factory stripes are
///       painted only where the depth field says rubber still stands proud — paint in a groove would read as
///       a manufacturing fault rather than as a marking.
/// cost  🚩 O(Width × Height)
/// tag   api, allocating, nonthrowing
[[nodiscard]] TreadMaterialMap ReadTreadMap(const TreadDepthField& Field,
                                            const TreadSpecification& Specification,
                                            const TreadDerivedValues& Derived,
                                            const TyreAppearanceSpecification& Appearance,
                                            TreadMapChannel Channel);

}   // namespace Frontier
