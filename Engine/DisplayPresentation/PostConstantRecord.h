//============================================================================================================================================
// 📦 Engine/DisplayPresentation/PostConstantRecord.h — the per-frame post record: stars, lens flare, rainbow
//============================================================================================================================================
// The sky record (binding 21) and the moon record (binding 22) are both full, and the three effects here share
//    nothing but novelty — so they share one small uniform block at binding 24 instead of three. The STAR TABLES
//    ride separately at binding 23 (StarCatalogueIndex wrote them for exactly this: cells then binned stars, no
//    fix-ups) because they are static — uploaded once, never per frame — while everything in this record moves.
//    The layout foresaw exactly this shape (23 pooled as storage, 24 as the UBO hole), so no pool counts change.
//
//    ⚠️ WHAT LIVES WHERE, because the next reader will ask. Per-frame scalars (sidereal time, sun UV, rain state,
//    the pixel spread the star profile floors against) pack here. The 9 100-star catalogue does NOT — it uploads
//    once through UploadStarTables. The rainbow's Descartes angles are NEITHER: they are pure physics, constant
//    forever, so the shader carries them as const arrays (computed by the model itself — see the bow probe note
//    in CheckPostKernel.sh) and no bytes cross.
//
//    ⚠️ THE SUN DIRECTION IS NOT RESTATED. RainbowAlong reads SkySunDirection from binding 21's block — same
//    descriptor set, so no second copy to drift. The packer takes no sun argument for exactly that reason.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Frontier {

// Binding 24, std140. Eight rows; the last row-plus is spare. Floats throughout — the shader compares categories
//    and counts as uint(Post.x), which is exact for the small values packed.
//
//        offset    0   PostStar        LST [deg], latitude [deg], point size [x], brightness [x] (0 = stars off)
//        offset   16   PostFlare       category [0..3], ghost count, intensity [x], halo radius [ndc]
//        offset   32   PostFlare2      chromatic [0..1], streak gain [x], aperture blades, sun visibility [0..1]
//        offset   48   PostFlareUv     sun U [0..1], sun V [0..1], enabled 0/1, unused (0)
//        offset   64   PostBow         intensity [x], width [x], secondary gain [0..1], rain visibility [0..1]
//        offset   80   PostBow2        alexander band 0/1, minimum path [m], enabled 0/1, unused (0)
//        offset   96   PostSpare0      pixel spread [rad], 0, 0, 0 (the star core floors at half of it)
//        offset  112   PostSpare1      reserved (zero)
struct PostConstantRecord
{
    float PostStar[4];     // LST, latitude, point size, brightness
    float PostFlare[4];    // category, ghosts, intensity, halo radius
    float PostFlare2[4];   // chromatic, streak gain, blades, sun visibility
    float PostFlareUv[4];  // sun U, sun V, enabled, 0
    float PostBow[4];      // intensity, width, secondary gain, rain visibility
    float PostBow2[4];     // alexander, min path, enabled, 0
    float PostSpare0[4];   // pixel spread, 0, 0, 0
    float PostSpare1[4];   // reserved
};

static_assert(sizeof(PostConstantRecord) == 128u, "the post record is eight std140 rows");
static_assert(offsetof(PostConstantRecord, PostStar)    == 0u,   "PostStar at row 0");
static_assert(offsetof(PostConstantRecord, PostFlare)   == 16u,  "PostFlare at row 1");
static_assert(offsetof(PostConstantRecord, PostFlare2)  == 32u,  "PostFlare2 at row 2");
static_assert(offsetof(PostConstantRecord, PostFlareUv) == 48u,  "PostFlareUv at row 3");
static_assert(offsetof(PostConstantRecord, PostBow)     == 64u,  "PostBow at row 4");
static_assert(offsetof(PostConstantRecord, PostBow2)    == 80u,  "PostBow2 at row 5");
static_assert(offsetof(PostConstantRecord, PostSpare0)  == 96u,  "PostSpare0 at row 6");
static_assert(offsetof(PostConstantRecord, PostSpare1)  == 112u, "PostSpare1 at row 7");

// The one packer, called by CelestialSequence::PackPostRecord and by the post gate's pack proof. The sun UV and
//    visibility arrive computed — projection needs the camera and occlusion needs the traversal, and neither
//    lives in this header — while the rain visibility is derived by the caller from the precipitation state.
inline PostConstantRecord PackPostConstants(float LocalSiderealDegrees, float LatitudeDegrees,
                                            float StarPointSize, float StarBrightness, float PixelSpreadRadians,
                                            uint32_t FlareCategory, uint32_t FlareGhosts, float FlareIntensity,
                                            float FlareHaloRadius, float FlareChromatic, float FlareStreakGain,
                                            uint32_t FlareBlades, float SunVisibility,
                                            float SunU, float SunV, bool FlareEnabled,
                                            float BowIntensity, float BowWidth, float BowSecondary,
                                            bool BowAlexander, float BowMinimumPath, float RainVisibility,
                                            bool BowEnabled) noexcept
{
    PostConstantRecord R{};
    R.PostStar[0] = LocalSiderealDegrees;
    R.PostStar[1] = LatitudeDegrees;
    R.PostStar[2] = StarPointSize;
    R.PostStar[3] = StarBrightness;
    R.PostFlare[0] = static_cast<float>(FlareCategory);
    R.PostFlare[1] = static_cast<float>(FlareGhosts);
    R.PostFlare[2] = FlareIntensity;
    R.PostFlare[3] = FlareHaloRadius;
    R.PostFlare2[0] = FlareChromatic;
    R.PostFlare2[1] = FlareStreakGain;
    R.PostFlare2[2] = static_cast<float>(FlareBlades);
    R.PostFlare2[3] = SunVisibility;
    R.PostFlareUv[0] = SunU;
    R.PostFlareUv[1] = SunV;
    R.PostFlareUv[2] = FlareEnabled ? 1.0f : 0.0f;
    R.PostBow[0] = BowIntensity;
    R.PostBow[1] = BowWidth;
    R.PostBow[2] = BowSecondary;
    R.PostBow[3] = RainVisibility;
    R.PostBow2[0] = BowAlexander ? 1.0f : 0.0f;
    R.PostBow2[1] = BowMinimumPath;
    R.PostBow2[2] = BowEnabled ? 1.0f : 0.0f;
    R.PostSpare0[0] = PixelSpreadRadians;
    return R;
}

} // namespace Frontier
