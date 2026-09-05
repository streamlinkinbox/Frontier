//============================================================================================================================================
//                                                    PALETTECONFIGURATION.H
//============================================================================================================================================
// 🧩 Colour and radius tokens for spatial interface figures. One shared set of slots so a 3D instrument cluster and
//    the 2D Control Centre shade read as the same product — the tokens mirror ThemeStructure's ThemePalette names
//    (MainBackground, CardBackground, TextMain, Accent …) and a host can seed one from the other.
//
// Figures reference a slot by ordinal, not by colour: the instance slot carries a 24-bit palette ordinal, so
//    re-theming the whole interface is a palette upload, not a figure walk. A figure may still override with an
//    explicit tint when a value drives the colour (a redline arc, a warning telltale).

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    COLOUR SLOT
//------------------------------------------------------------------------------------------------------------------------

struct ColourValue
{
    float Red   = 0.0f;    // [0..1] linear
    float Green = 0.0f;    // [0..1]
    float Blue  = 0.0f;    // [0..1]
    float Alpha = 1.0f;    // [0..1]
};

// Packs to the RGBA8 the instance slot carries. Linear values are stored as-is (the scene is linear throughout;
//    the swapchain applies the transfer curve once, at present time).
[[nodiscard]] uint32_t PackColourValue(const ColourValue& Colour) noexcept;
[[nodiscard]] ColourValue UnpackColourValue(uint32_t Packed) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                   PALETTE ORDINAL
//------------------------------------------------------------------------------------------------------------------------
// Mirrors kPalette* in Shaders/InterfaceSignedDistance.slang.

enum class PaletteSlot : uint32_t
{
    Housing     = 0u,   // bezel / outer shell
    Surface     = 1u,   // card face
    SurfaceSunk = 2u,   // nested control bed (bar trough, toggle bed)
    Stroke      = 3u,   // borders and dividers
    Marking     = 4u,   // ticks, digits, primary text
    MarkingMute = 5u,   // secondary marks
    Accent      = 6u,   // the live colour — fills, active knobs, needles
    Caution     = 7u,   // amber
    Warning     = 8u,   // red — redline arcs, fault telltales
    Confirm     = 9u,   // green
    Count       = 10u
};

//------------------------------------------------------------------------------------------------------------------------
//                                                PALETTE CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

class PaletteConfiguration
{
public:
    PaletteConfiguration() noexcept;                            // instrument-dark default (see the .cpp)

    void                        Assign(PaletteSlot Slot, const ColourValue& Colour) noexcept;
    [[nodiscard]] ColourValue   Query(PaletteSlot Slot) const noexcept;
    [[nodiscard]] uint32_t      QueryPacked(PaletteSlot Slot) const noexcept;

    // Corner rounding used when a figure asks for the token radius rather than an explicit one [m].
    void                        AssignTokenRadius(float Metres) noexcept  { TokenRadius = Metres; }
    [[nodiscard]] float         QueryTokenRadius() const noexcept         { return TokenRadius; }

    // Every figure's alpha is multiplied by this — the director (⑤, P3) fades a whole screen with one scalar.
    void                        AssignGroupOpacity(float Opacity) noexcept;
    [[nodiscard]] float         QueryGroupOpacity() const noexcept        { return GroupOpacity; }

    // Bumped on every mutation so InterfaceExchange can skip re-uploading an unchanged palette.
    [[nodiscard]] uint32_t      QueryRevision() const noexcept            { return Revision; }

    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    ColourValue Slots[static_cast<uint32_t>(PaletteSlot::Count)];
    float       TokenRadius  = 0.008f;   // [m]  8 mm — reads as a ~6 px radius on a 0.4 m panel at arm's length
    float       GroupOpacity = 1.0f;     // [-]
    uint32_t    Revision     = 1u;       // [-]
};

template<>
inline uint32_t PaletteConfiguration::Convert<uint32_t>() const noexcept
{
    return Revision;
}

} // namespace Frontier
