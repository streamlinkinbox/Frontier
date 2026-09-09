//============================================================================================================================================
//                                                    INTERFACELAYOUTCODEC.H
//============================================================================================================================================
// 🧩 Encodes a laid-out figure into the 96-byte instance slot the raster consumes, and decodes it back (the proof
//    harness round-trips every field). This is the one byte-level contract between C++ and Slang for layer ②:
//    Engine/Shaders/InterfaceRecords.slang mirrors InterfaceInstanceFigure exactly — change both sides or neither.
//
// "Layout" here is the verb, matching ConstructControlLayout / ConstructTelemetryLayout in the 2D overlay: the codec
//    encodes the RESULT of laying figures out. It is not a name for a drawable.
//
// Why 96 bytes and no per-figure uniform: 200 figures cost 19 KB per frame into a host-visible ring, which is
//    cheaper than a single descriptor update, and it keeps the draw count at one regardless of figure count.

#pragma once

#include "InterfaceStructure.h"

#include <cstddef>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                    INSTANCE FIGURE  (GPU SSBO — std430, 96 bytes)
//------------------------------------------------------------------------------------------------------------------------
// Transform rows: world = [RowX; RowY; RowZ] · (localX, localY, localZ, 1). Translation lives in the .w of each row.
//    Three rows suffice because interface planes are placed by rotation, uniform scale and translation only —
//    never sheared — so the fourth row is always (0, 0, 0, 1) and is reconstructed in the shader for free.

struct InterfaceInstanceFigure
{
    float    RowXx, RowXy, RowXz, RowXw;      // [-]/[m]  world row 0, translation in w
    float    RowYx, RowYy, RowYz, RowYw;      // [-]/[m]  world row 1
    float    RowZx, RowZy, RowZz, RowZw;      // [-]/[m]  world row 2

    float    HalfWidth;                        // [m]   local half extent along +X
    float    HalfHeight;                       // [m]   local half extent along +Y
    float    CornerRadius;                     // [m]   resolved (token radius already substituted)
    float    Opacity;                          // [-]   figure opacity × group opacity  ⑤

    float    ClipMinimumX, ClipMinimumY;       // [m]   ⑥ figure-local clip rectangle
    float    ClipMaximumX, ClipMaximumY;       // [m]

    uint32_t CategoryPalette;                  // [-]   category in bits 31..24, palette ordinal in bits 23..0
    float    ScalarAlpha;                      // [-]   fill fraction / angle fraction / digit / luminance
    float    ScalarBeta;                       // [-]   thickness / mark count / warn threshold
    uint32_t Tint;                             // [-]   RGBA8, resolved (palette slot already substituted)

    // ⑦ Surface response. Tint is what the figure SHOWS; these two say how it behaves as a physical surface, so the
    //    panel reads as a phone/LCD screen in the room rather than a decal: a dark dielectric housing that catches
    //    the ceiling luminaire and the red wall's bleed, with only the active elements actually emitting.
    uint32_t BaseColour;                       // [-]   RGBA8 albedo of the unlit surface; a = 0 → fall back to Tint
    float    EmissiveWeight;                   // [-]   0 = pure albedo (receives light), 1 = pure emitter ⑦

    // Reserved so the 16-byte std430 tail is named rather than silent. Future channels (roughness, a second
    //    emissive band) land here without moving a single existing offset. Written as 0, unread.
    float    ReserveAlpha;                     // [-]
    float    ReserveBeta;                      // [-]
};

static_assert(sizeof(InterfaceInstanceFigure) == 112u, "InterfaceInstanceFigure must be 112 bytes (std430 mirror of Shaders/InterfaceRecords.slang)");

// Field offsets verified against glslang's std430 reflection of Shaders/InterfaceRecords.slang (topLevelArrayStride
//    112, RowX 0, RowY 16, RowZ 32, HalfExtent 48, CornerRadius 56, Opacity 60, ClipExtent 64, CategoryPalette 80,
//    ScalarAlpha 84, ScalarBeta 88, Tint 92, BaseColour 96, EmissiveWeight 100).
//    Re-run Scratchpad/CompileInterfaceShaders.sh after any field change.
static_assert(offsetof(InterfaceInstanceFigure, RowXx)           ==  0u, "RowX must sit at offset 0");
static_assert(offsetof(InterfaceInstanceFigure, RowYx)           == 16u, "RowY must sit at offset 16");
static_assert(offsetof(InterfaceInstanceFigure, RowZx)           == 32u, "RowZ must sit at offset 32");
static_assert(offsetof(InterfaceInstanceFigure, HalfWidth)       == 48u, "HalfExtent must sit at offset 48");
static_assert(offsetof(InterfaceInstanceFigure, CornerRadius)    == 56u, "CornerRadius must sit at offset 56");
static_assert(offsetof(InterfaceInstanceFigure, Opacity)         == 60u, "Opacity must sit at offset 60");
static_assert(offsetof(InterfaceInstanceFigure, ClipMinimumX)    == 64u, "ClipExtent must sit at offset 64");
static_assert(offsetof(InterfaceInstanceFigure, CategoryPalette) == 80u, "CategoryPalette must sit at offset 80");
static_assert(offsetof(InterfaceInstanceFigure, ScalarAlpha)     == 84u, "ScalarAlpha must sit at offset 84");
static_assert(offsetof(InterfaceInstanceFigure, ScalarBeta)      == 88u, "ScalarBeta must sit at offset 88");
static_assert(offsetof(InterfaceInstanceFigure, Tint)            == 92u, "Tint must sit at offset 92");
static_assert(offsetof(InterfaceInstanceFigure, BaseColour)      == 96u, "BaseColour must sit at offset 96");
static_assert(offsetof(InterfaceInstanceFigure, EmissiveWeight)  ==100u, "EmissiveWeight must sit at offset 100");

//------------------------------------------------------------------------------------------------------------------------
//                                                  WORLD PLACEMENT
//------------------------------------------------------------------------------------------------------------------------
// The composed world transform of one figure, as the three rows the slot stores. Kept as a plain struct rather than
//    the engine's Matrix4x4 so the spatial-interface layer has no dependency on the raster math headers and can be
//    exercised by the headless proof on its own.

struct WorldPlacement
{
    float Row[3][4] = { { 1.0f, 0.0f, 0.0f, 0.0f },
                        { 0.0f, 1.0f, 0.0f, 0.0f },
                        { 0.0f, 0.0f, 1.0f, 0.0f } };
};

// Local placement → rows. Rotation order is Z (in-plane roll), then X (pitch toward the eye), then Y (yaw), which is
//    the order an interface is actually authored in: lay it out flat, tilt it, then swing it into place.
[[nodiscard]] WorldPlacement ComposePlacement(const PlanePlacement& Placement) noexcept;

// Ancestor ∘ local. Uniform scale and rotation compose normally; the ancestor's translation carries through.
[[nodiscard]] WorldPlacement CombinePlacement(const WorldPlacement& Ancestor, const WorldPlacement& Local) noexcept;

// Transforms a point on the figure's local plane into world space.
void TransformPlanePoint(const WorldPlacement& Placement, float LocalX, float LocalY, float LocalZ,
                         float& WorldX, float& WorldY, float& WorldZ) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                 INTERFACE LAYOUT CODEC
//------------------------------------------------------------------------------------------------------------------------

class InterfaceLayoutCodec
{
public:
    // Figure + its composed world placement + the palette → the GPU slot. Resolves the token corner radius, the
    //    palette slot into an explicit tint (unless the figure overrides it), and folds the group opacity in.
    static void Encode(const InterfaceFigure& Figure, const WorldPlacement& Placement,
                       const PaletteConfiguration& Palette, InterfaceInstanceFigure& Slot) noexcept;

    // Slot → figure, for the round-trip proof. The placement is returned separately; the palette ordinal is
    //    recovered but the tint stays explicit (encoding is lossy in exactly that one documented direction).
    static void Decode(const InterfaceInstanceFigure& Slot, InterfaceFigure& Figure, WorldPlacement& Placement) noexcept;

    [[nodiscard]] static uint32_t          ComposeCategoryPalette(InterfaceCategory Category, PaletteSlot Palette) noexcept;
    [[nodiscard]] static InterfaceCategory ExtractCategory(uint32_t CategoryPalette) noexcept;
    [[nodiscard]] static PaletteSlot       ExtractPalette (uint32_t CategoryPalette) noexcept;
};

} // namespace Frontier
