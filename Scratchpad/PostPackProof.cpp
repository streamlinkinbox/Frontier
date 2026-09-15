//============================================================================================================================================
// 📦 Scratchpad/PostPackProof.cpp — CelestialSequence::PackPostRecord packs the kernel's post params
//============================================================================================================================================
// The post record (binding 24) carries three effects' per-frame scalars: the star field's time and knobs, the
//    flare's settings plus the projected, occlusion-gated sun, and the rainbow's settings plus the rain column.
//    This pins the pack: solved values land in solved lanes, panel values echo, projections centre, gates gate.

#include "Projects/Project-Zero/Source/CelestialSequence.h"

#include <cmath>
#include <cstdio>

namespace {

int Failures = 0;

void Expect(bool Ok, const char* What)
{
    if (!Ok) ++Failures;
    std::printf("  %-66s %s\n", What, Ok ? "PASS" : "FAIL");
}

bool Nearly(float A, float B, float Eps = 1e-5f) { return std::fabs(A - B) <= Eps; }

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::ProjectZero;

    std::printf("\nCelestialSequence::PackPostRecord — the kernel is packed the post params\n");

    CelestialSequence Sky;
    Sky.Prepare();

    const float TanHalf = 0.5f, Aspect = 16.0f / 9.0f;
    const uint32_t Height = 900u;
    const float Cam0[3] = { 0.0f, 0.0f, 0.0f };

    // A camera staring straight at the solved sun: forward = sun, right/up an arbitrary perpendicular pair.
    float F[3] = { Sky.Frame().Sun.Direction[0], Sky.Frame().Sun.Direction[1], Sky.Frame().Sun.Direction[2] };
    float R[3] = { -F[1], F[0], 0.0f };
    {
        const float L = std::sqrt(R[0] * R[0] + R[1] * R[1] + R[2] * R[2]);
        R[0] /= L; R[1] /= L; R[2] /= L;
    }
    const float U[3] = { R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0] };

    // ① Solved lanes read the solved frame; the spread is the shader's PixelSpreadAngle in plain C++.
    {
        const PostConstantRecord P = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(Nearly(P.PostStar[0], Sky.Frame().LocalSiderealTime), "LST rides the star row");
        Expect(Nearly(P.PostStar[1], Sky.Observation.Latitude), "latitude rides the star row");
        Expect(Nearly(P.PostStar[2], Sky.StarSize), "panel point size rides the star row");
        const float WantBright = Sky.Stars().Empty() ? 0.0f : Sky.StarBrightness;
        Expect(Nearly(P.PostStar[3], WantBright), "brightness rides — or zero when the catalogue is missing");
        Expect(Nearly(P.PostSpare0[0], 2.0f * TanHalf / static_cast<float>(Height)),
               "the pixel spread is 2·tan/h, the shader's own formula");
    }

    // ② The sun projects to the frame centre when stared at, and parks off-frame when behind the camera.
    {
        const PostConstantRecord P = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(Nearly(P.PostFlareUv[0], 0.5f, 1e-6f) && Nearly(P.PostFlareUv[1], 0.5f, 1e-6f),
               "a faced sun projects to the frame centre");
        const float B[3] = { -F[0], -F[1], -F[2] };
        const PostConstantRecord Q = Sky.PackPostRecord(B, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(Q.PostFlareUv[0] == -10.0f && Q.PostFlareUv[1] == -10.0f,
               "a sun behind the camera parks outside the edge fade");
    }

    // ③ Flare settings echo; the entity eye and the switch gate the enabled lane.
    {
        const PostConstantRecord P = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 0.75f);
        Expect(P.PostFlare[0] == 0.0f && P.PostFlare[1] == 6.0f && Nearly(P.PostFlare[2], 1.0f)
               && Nearly(P.PostFlare[3], 0.28f), "category, ghosts, intensity and halo echo the panel");
        Expect(Nearly(P.PostFlare2[0], 0.6f) && Nearly(P.PostFlare2[1], 1.0f) && P.PostFlare2[2] == 8.0f,
               "chromatic, streak and blades echo the panel");
        Expect(Nearly(P.PostFlareUv[2], 1.0f), "the flare ships enabled");
        Sky.Flare.GhostCount = 3u;
        const PostConstantRecord Q = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(Q.PostFlare[1] == 3.0f, "a slider edit lands in the record");
        Sky.Flare.GhostCount = 6u;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::LensFlare)] = false;
        const PostConstantRecord H = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(H.PostFlareUv[2] == 0.0f, "hiding the entity disables the flare lane");
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::LensFlare)] = true;
    }

    // ④ Occlusion passes through by day and the horizon fades it at night.
    {
        const PostConstantRecord P = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 0.75f);
        Expect(Nearly(P.PostFlare2[3], 0.75f), "daylight passes the traced visibility through");
        Sky.Observation.LocalHours = 22.0f;
        Sky.Tick(0.0f, Cam0, 0.0f);
        const PostConstantRecord Q = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(Q.PostFlare2[3] == 0.0f, "a sun deep below the horizon flares nothing");
        Sky.Observation.LocalHours = 15.5f;
        Sky.Tick(0.0f, Cam0, 0.0f);
    }

    // ⑤ Rain visibility rises with the rate for rain, halves for drizzle, and snow earns nothing.
    {
        const PostConstantRecord P = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(P.PostBow[3] == 0.0f, "precipitation ships off, so no rain column");
        Expect(Nearly(P.PostBow[0], 1.0f) && Nearly(P.PostBow[1], 1.0f) && Nearly(P.PostBow[2], 1.0f),
               "bow intensity, width and secondary echo the panel");
        Expect(P.PostBow2[0] == 1.0f && Nearly(P.PostBow2[1], 250.0f),
               "Alexander's band and the minimum path echo the panel");
        Sky.Precip.Enabled = true;
        Sky.Precip.Category = PrecipitationCategory::Rain;
        Sky.Precip.RateMillimetresPerHour = 20.0f;
        const PostConstantRecord Q = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(Nearly(Q.PostBow[3], 1.0f), "heavy rain saturates the column");
        Sky.Precip.RateMillimetresPerHour = 5.0f;
        const PostConstantRecord Rr = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(Nearly(Rr.PostBow[3], 0.5f), "light rain half-fills it");
        Sky.Precip.Category = PrecipitationCategory::Drizzle;
        Sky.Precip.RateMillimetresPerHour = 20.0f;
        const PostConstantRecord D = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(Nearly(D.PostBow[3], 0.5f), "drizzle earns half a column at best");
        Sky.Precip.Category = PrecipitationCategory::Snow;
        Sky.Precip.RateMillimetresPerHour = 50.0f;
        const PostConstantRecord S = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(S.PostBow[3] == 0.0f, "snow earns nothing — ice makes halos, not bows");
        Sky.Precip.Enabled = false;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Rainbow)] = false;
        const PostConstantRecord H = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(H.PostBow2[2] == 0.0f, "hiding the entity disables the bow lane");
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Rainbow)] = true;
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = false;
        const PostConstantRecord St = Sky.PackPostRecord(F, R, U, TanHalf, Aspect, Height, 1.0f);
        Expect(St.PostStar[3] == 0.0f, "hiding the entity zeroes the star brightness");
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::Stars)] = true;
    }

    std::printf("\n%s\n\n", Failures == 0 ? "  the kernel is packed the post params" : "  POST PACK MISMATCH");
    return Failures == 0 ? 0 : 1;
}
