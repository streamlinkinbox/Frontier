//============================================================================================================================================
//                                                      TYREAPPEARANCESPECIFICATION.H
//============================================================================================================================================
// 📦 Everything about a tyre that is paint rather than shape: compound colour, factory tread stripes and the sidewall decal sequence.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     FACTORY STRIPES
//------------------------------------------------------------------------------------------------------------------------

/// 📦 How the two coloured identification stripes a fresh tyre leaves the mould with are laid down.
/// note  The stripes are paint on the raised blocks only. They never enter a groove and they fade with wear,
///       which is why the raster applies them after the depth field rather than as another cutting layer.
/// tag   schema
enum class StripePattern : uint8_t
{
    ZigZag = 0,   // one continuous zig-zagging line
    Dotted = 1,   // a dot per pitch cell
    Dashed = 2,   // a dash per pitch cell
    Stitch = 3    // alternating short diagonals, like a stitch seam
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    SIDEWALL DECALS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Which typeface a sidewall decal is set in. The editor offers these by name; the raster maps them onto
///    whatever faces the host has seated.
/// tag   schema
enum class DecalFace : uint8_t
{
    Outfit    = 0,   // the editor's own face
    Heavy     = 1,   // Impact / Arial Black
    CleanSans = 2,   // Arial / Helvetica
    Serif     = 3,   // Georgia
    Mono      = 4,   // a technical monospace
    Script    = 5    // a cursive face
};

/// 📦 Where a decal's anchor sits along the line it is set on.
/// tag   schema
enum class DecalAlignment : uint8_t
{
    Left   = 0,
    Centre = 1,
    Right  = 2
};

/// 📦 One piece of sidewall lettering, held as an editable layer rather than baked into the sidewall image.
/// note  Text may carry the tokens {brand} {name} {size} {type} {depth} {serial}, which resolve against the
///       document at raster time. That is what lets one decal sequence survive being re-pointed at another
///       preset: the compliance line re-reads its own size and depth instead of going stale.
/// tag   schema
struct SidewallDecalSpecification
{
    std::string     Identity;                              // [-]  - stable key, never shown
    std::string     Name     = "Text decal";               // [-]  - the row label in the editor
    std::string     Text     = "YOUR TEXT";                // [-]  - the copy, tokens included
    DecalFace       Face     = DecalFace::Outfit;
    float           Size     = 42.0f;                      // [px] - cap height in sidewall image pixels
    uint32_t        Colour   = 0xFFE8E8E8u;                // [-]  - ARGB
    float           Across   = 50.0f;                      // [%]  - radial seat, 0 at the bead, 100 at the tread
    float           Along    = 50.0f;                      // [%]  - angular seat around the sidewall
    DecalAlignment  Alignment = DecalAlignment::Centre;
    float           Weight   = 700.0f;                     // [-]  - typographic weight, 100 to 900
    bool            Italic   = false;
    float           Opacity  = 1.0f;                       // [-]  - 0 clear to 1 solid
    bool            Embossed = false;                      // [-]  - raised rubber rather than painted
    bool            Repeat   = true;                       // [-]  - repeat the copy around the sidewall
    bool            Enabled  = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      APPEARANCE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The compound-and-look half of a tyre document.
/// tag   schema
struct TyreAppearanceSpecification
{
    uint32_t      Rubber        = 0xFF151515u;   // [-]   - ARGB, the unworn compound
    std::string   Brand         = "SLATE";       // [-]   - substituted for the {brand} token
    bool          Lettering     = true;          // [-]   - draw the decal sequence at all

    bool          FactoryStripes = true;
    StripePattern Pattern       = StripePattern::ZigZag;
    uint32_t      StripeRed     = 0xFFD63842u;   // [-]   - ARGB
    uint32_t      StripeBlue    = 0xFF3184C7u;   // [-]   - ARGB
    float         StripeWidth   = 1.4f;          // [mm]  - painted line width
    float         StripeGap     = 4.2f;          // [mm]  - lateral separation of the pair
    float         StripeWave    = 1.1f;          // [mm]  - zig amplitude
    float         StripeOffset  = 0.0f;          // [mm]  - lateral seat of the pair, signed
    float         StripePitch   = 12.0f;         // [mm]  - one cell of the dotted, dashed or stitch pattern

    std::vector<SidewallDecalSpecification> Decals;
};

/// 📦 The four decals a new tyre document starts with: brand mark, model name, specification line and the
///    moulded compliance strip.
/// out   TyreAppearanceSpecification::Decals  the default sequence, tokens unresolved
/// tag   api, allocating, nonthrowing
[[nodiscard]] std::vector<SidewallDecalSpecification> DefaultSidewallDecals();

}   // namespace Frontier
