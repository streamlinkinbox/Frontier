//============================================================================================================================================
//                                                        REPLICATEDPLACEMENT.H
//============================================================================================================================================
// 📦 What a replicated placement looks like on the wire. A description is a run of properties over a
// caller-owned byte run: the replication sequence never allocates the simulation's state, it reads and
// writes the bytes the simulation already keeps, at the offsets the description declares.
//
// This is deliberately carrier-free. Photon, Epic Online Services and PlayFab all carry opaque byte
// arrays, so the description is the same wire shape on every one of them, and the proofs encode and
// decode it with no SDK present at all.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Networking
{

// How a property's bytes are laid out, both in the simulation's run and on the wire. The wire is
//    little-endian and unpadded; a float is the IEEE-754 binary32 the host already holds.
enum class PropertyLayout : std::uint8_t
{
    Boolean = 0,    //  1 byte
    Whole,          //  4 bytes, signed
    Decimal,        //  4 bytes, binary32
    Translation,    // 12 bytes, three binary32 — interpolates linearly
    Orientation,    // 16 bytes, four binary32 xyzw — interpolates along the shorter arc
    Opaque          //  2-byte length then that many bytes; never interpolated
};

/// 📦 The wire width of one property, or 0 when the property carries its own length.
inline std::uint16_t PropertyWidth(PropertyLayout Layout) noexcept
{
    switch (Layout)
    {
    case PropertyLayout::Boolean:     return 1;
    case PropertyLayout::Whole:       return 4;
    case PropertyLayout::Decimal:     return 4;
    case PropertyLayout::Translation: return 12;
    case PropertyLayout::Orientation: return 16;
    default:                          return 0;
    }
}

/// 📦 True when a remote peer should be eased between two samples of this property rather than
///    snapped to the newest one.
inline bool PropertySmoothable(PropertyLayout Layout) noexcept
{
    return Layout == PropertyLayout::Decimal || Layout == PropertyLayout::Translation
        || Layout == PropertyLayout::Orientation;
}

// One replicated property. Quantum is the smallest change worth a packet: a translation that moved
//    less than a millimetre is not news, and at 30 snapshots a second that silence is most of the
//    saving. A zero quantum sends on any change at all, which is what a boolean or a whole wants.
struct ReplicatedProperty
{
    const char*    Name     = "";
    PropertyLayout Layout   = PropertyLayout::Decimal;
    std::uint16_t  Offset   = 0;      // [B]  into the placement's own byte run
    std::uint16_t  Width    = 0;      // [B]  for Opaque only — the run's capacity
    float          Quantum  = 0.0f;
    bool           Smoothed = false;  // eased on remote peers; only meaningful if PropertySmoothable
};

// The shape of one replicated placement. Properties are addressed by index, so their order is part
//    of the wire shape: adding one at the end is safe, reordering is not. Signature catches exactly
//    that mistake — two peers built from different orders refuse each other's snapshots.
struct ReplicatedDescription
{
    const char*               Name          = "";
    const ReplicatedProperty* Properties    = nullptr;
    std::uint16_t             PropertyCount = 0;
    std::uint16_t             Stride        = 0;   // [B] the simulation's byte run for one placement
};

/// 📦 A 16-bit signature over a description's property order, layouts and offsets.
/// note  FNV-1a over the shape only, never the name: renaming a placement is not a wire change.
inline std::uint16_t DescriptionSignature(const ReplicatedDescription& Description) noexcept
{
    std::uint32_t Running = 2166136261u;
    auto Fold = [&Running](std::uint32_t Reading)
    {
        for (int Byte = 0; Byte < 4; ++Byte)
        {
            Running ^= (Reading >> (Byte * 8)) & 0xFFu;
            Running *= 16777619u;
        }
    };
    Fold(Description.PropertyCount);
    for (std::uint16_t At = 0; At < Description.PropertyCount; ++At)
    {
        const ReplicatedProperty& Property = Description.Properties[At];
        Fold(static_cast<std::uint32_t>(Property.Layout));
        Fold(Property.Offset);
        Fold(Property.Width);
    }
    return static_cast<std::uint16_t>((Running >> 16) ^ (Running & 0xFFFFu));
}

/// 📦 True when two readings of the same property differ by enough to be worth sending.
/// in    Property [-] the property being compared
/// in    Fresh    [-] the simulation's current bytes for it
/// in    Shadow   [-] the bytes last sent for it
inline bool PropertyMoved(const ReplicatedProperty& Property, const std::uint8_t* Fresh,
                          const std::uint8_t* Shadow) noexcept
{
    const std::uint16_t Width = PropertyWidth(Property.Layout);
    if (Width == 0) return std::memcmp(Fresh, Shadow, Property.Width) != 0;
    if (Property.Quantum <= 0.0f) return std::memcmp(Fresh, Shadow, Width) != 0;

    const std::uint16_t Decimals = static_cast<std::uint16_t>(Width / 4);
    for (std::uint16_t At = 0; At < Decimals; ++At)
    {
        float Now = 0.0f, Then = 0.0f;
        std::memcpy(&Now,  Fresh  + At * 4, 4);
        std::memcpy(&Then, Shadow + At * 4, 4);
        const float Apart = Now > Then ? Now - Then : Then - Now;
        if (Apart > Property.Quantum) return true;
    }
    return false;
}

}   // namespace Networking
