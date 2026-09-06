//============================================================================================================================================
//                                                   PALETTECONFIGURATION.CPP
//============================================================================================================================================

#include "PaletteConfiguration.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   COLOUR PACKING
//------------------------------------------------------------------------------------------------------------------------

namespace {

[[nodiscard]] inline uint32_t QuantiseChannel(float Value) noexcept
{
    const float Clamped = std::clamp(Value, 0.0f, 1.0f);
    return static_cast<uint32_t>(Clamped * 255.0f + 0.5f);
}

} // namespace

uint32_t PackColourValue(const ColourValue& Colour) noexcept
{
    return  QuantiseChannel(Colour.Red)
         | (QuantiseChannel(Colour.Green) <<  8)
         | (QuantiseChannel(Colour.Blue)  << 16)
         | (QuantiseChannel(Colour.Alpha) << 24);
}

ColourValue UnpackColourValue(uint32_t Packed) noexcept
{
    constexpr float Inverse = 1.0f / 255.0f;
    return ColourValue{ static_cast<float>( Packed        & 0xFFu) * Inverse,
                        static_cast<float>((Packed >>  8) & 0xFFu) * Inverse,
                        static_cast<float>((Packed >> 16) & 0xFFu) * Inverse,
                        static_cast<float>((Packed >> 24) & 0xFFu) * Inverse };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 INSTRUMENT-DARK DEFAULT
//------------------------------------------------------------------------------------------------------------------------
// Linear Rec.709. The values are the sRGB tokens of the Control Centre theme linearised once here, so a 3D cluster
//    and the 2D shade agree without either side converting at draw time.

PaletteConfiguration::PaletteConfiguration() noexcept
{
    Slots[static_cast<uint32_t>(PaletteSlot::Housing)]     = ColourValue{ 0.0090f, 0.0100f, 0.0120f, 1.00f };
    Slots[static_cast<uint32_t>(PaletteSlot::Surface)]     = ColourValue{ 0.0200f, 0.0220f, 0.0260f, 1.00f };
    Slots[static_cast<uint32_t>(PaletteSlot::SurfaceSunk)] = ColourValue{ 0.0110f, 0.0125f, 0.0150f, 1.00f };
    Slots[static_cast<uint32_t>(PaletteSlot::Stroke)]      = ColourValue{ 0.0600f, 0.0650f, 0.0750f, 1.00f };
    Slots[static_cast<uint32_t>(PaletteSlot::Marking)]     = ColourValue{ 0.8000f, 0.8300f, 0.8800f, 1.00f };
    Slots[static_cast<uint32_t>(PaletteSlot::MarkingMute)] = ColourValue{ 0.1800f, 0.1950f, 0.2200f, 1.00f };
    Slots[static_cast<uint32_t>(PaletteSlot::Accent)]      = ColourValue{ 0.0500f, 0.4200f, 0.9000f, 1.00f };
    Slots[static_cast<uint32_t>(PaletteSlot::Caution)]     = ColourValue{ 0.9000f, 0.4400f, 0.0300f, 1.00f };
    Slots[static_cast<uint32_t>(PaletteSlot::Warning)]     = ColourValue{ 0.8600f, 0.0700f, 0.0900f, 1.00f };
    Slots[static_cast<uint32_t>(PaletteSlot::Confirm)]     = ColourValue{ 0.0800f, 0.6600f, 0.2600f, 1.00f };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      ACCESS
//------------------------------------------------------------------------------------------------------------------------

void PaletteConfiguration::Assign(PaletteSlot Slot, const ColourValue& Colour) noexcept
{
    const uint32_t Ordinal = static_cast<uint32_t>(Slot);
    if (Ordinal >= static_cast<uint32_t>(PaletteSlot::Count)) return;
    Slots[Ordinal] = Colour;
    ++Revision;
}

ColourValue PaletteConfiguration::Query(PaletteSlot Slot) const noexcept
{
    const uint32_t Ordinal = static_cast<uint32_t>(Slot);
    if (Ordinal >= static_cast<uint32_t>(PaletteSlot::Count)) return ColourValue{};
    return Slots[Ordinal];
}

uint32_t PaletteConfiguration::QueryPacked(PaletteSlot Slot) const noexcept
{
    ColourValue Colour = Query(Slot);
    Colour.Alpha      *= GroupOpacity;
    return PackColourValue(Colour);
}

void PaletteConfiguration::AssignGroupOpacity(float Opacity) noexcept
{
    const float Clamped = std::clamp(Opacity, 0.0f, 1.0f);
    if (std::fabs(Clamped - GroupOpacity) < 1.0e-6f) return;
    GroupOpacity = Clamped;
    ++Revision;
}

} // namespace Frontier
