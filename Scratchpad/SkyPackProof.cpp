// Does the sequence pack the same sky for the kernel that ApplyTo hands the raster?
//
// PackSkyRecord feeds binding 21; ApplyTo feeds the CPU raster. Both must apply the same adjustments — the
// solved direction, the tint and brightness on the radiance, a hidden sun as night — or the GI-on and GI-off
// skies show different suns. This packs records from a live sequence and checks each adjustment behaviourally,
// including the first-frame rule: Light.Direction is a cache the tick fills, so the pack must read the solved
// frame even when the cache holds poison.
#include "Projects/Project-Zero/Source/CelestialSequence.h"
#include <cmath>
#include <cstdio>
using namespace Frontier;
using namespace Frontier::ProjectZero;

namespace {
int Failures=0;
void Expect(bool Ok,const char* What){ if(!Ok)++Failures;
    std::printf("  %-66s %s\n",What,Ok?"PASS":"FAIL"); }

bool Nearly(float A, float B)
{
    const float Scale = 1.0f + std::fabs(A) + std::fabs(B);
    return std::fabs(A - B) <= 1e-5f * Scale;
}
bool Nearly3(const float A[3], const float B[3]) { return Nearly(A[0],B[0]) && Nearly(A[1],B[1]) && Nearly(A[2],B[2]); }
}

static_assert(sizeof(SkyConstantRecord) == 320u, "the packed sky and weather record is the shader's 320-byte block");

int main(){
    std::printf("\nCelestialSequence::PackSkyRecord — the kernel is packed the raster's sky\n");

    CelestialSequence Sky;
    Sky.Prepare();
    // Explicit inputs, so no test passes because a default happened to be zero.
    Sky.Light.Intensity = 3.0f;
    Sky.Light.Colour[0] = 1.0f; Sky.Light.Colour[1] = 0.9f; Sky.Light.Colour[2] = 0.8f;
    Sky.Budget.AtmosphereSamples = 11u;
    Sky.Budget.AtmosphereLightSamples = 5u;

    // ① The first-frame rule: no tick has run, so Light.Direction still holds whatever it was given — poison
    //    it and the pack must still report the solved sun, not the cache.
    Sky.Light.Direction[0] = 0.0f; Sky.Light.Direction[1] = 0.0f; Sky.Light.Direction[2] = 1.0f;
    {
        const SkyConstantRecord R = Sky.PackSkyRecord();
        const float* Solved = Sky.Frame().Sun.Direction;
        Expect(Nearly3(R.SunDirection, Solved), "before the first tick the pack reads the solved frame, not the cache");
        Expect(!(R.SunDirection[0]==0.0f && R.SunDirection[1]==0.0f && R.SunDirection[2]==1.0f),
               "the poisoned cache value does not leak into the record");
        Expect(R.SunDirection[3] == Sky.Frame().Sun.Elevation, "the elevation rides in the direction's spare lane");
    }

    // ② After a tick the cache is filled — poison it again and the pack must STILL read the solved frame,
    //    because the raster does.
    {
        const float Camera[3] = { 0.0f, 0.0f, 2.0f };
        Sky.Tick(0.016f, Camera, 0.0f);
        Sky.Light.Direction[0] = 1.0f; Sky.Light.Direction[1] = 0.0f; Sky.Light.Direction[2] = 0.0f;
        const SkyConstantRecord R = Sky.PackSkyRecord();
        Expect(Nearly3(R.SunDirection, Sky.Frame().Sun.Direction), "after a tick the pack still reads the solved frame");
    }

    // ③ A hidden sun is night, not a black sky: the direction still exists, the radiance does not — while the
    //    record itself stays enabled, exactly as ApplyTo leaves Settings.Enabled untouched.
    Sky.Shown[static_cast<uint32_t>(CelestialEntity::Sun)] = false;
    {
        const SkyConstantRecord R = Sky.PackSkyRecord();
        Expect(R.SunRadiance[0]==0.0f && R.SunRadiance[1]==0.0f && R.SunRadiance[2]==0.0f,
               "a hidden sun removes the radiance");
        Expect(Nearly3(R.SunDirection, Sky.Frame().Sun.Direction), "a hidden sun keeps its direction");
        Expect(R.SunRadiance[3]==1.0f, "a hidden sun does not disable the record");
    }
    Sky.Shown[static_cast<uint32_t>(CelestialEntity::Sun)] = true;

    // ④ The tint and brightness ride on the radiance, not the medium — recomputed here with PackSkyConstants'
    //    own association, so any reordering on either side shows as a number, not a suspicion.
    Sky.SkyTint[0] = 0.5f; Sky.SkyTint[1] = 0.25f; Sky.SkyTint[2] = 2.0f;
    Sky.SkyBrightness = 2.0f;
    {
        const SkyConstantRecord R = Sky.PackSkyRecord();
        const float Gain = Sky.Light.Intensity * Sky.SkyBrightness;
        bool Ok = true;
        for (int C = 0; C < 3; ++C) Ok = Ok && Nearly(R.SunRadiance[C], (Sky.Light.Colour[C] * Sky.SkyTint[C]) * Gain);
        Expect(Ok, "tint and brightness scale the radiance the raster sees");
        bool MediumUntouched = true;
        for (int C = 0; C < 3; ++C)
            MediumUntouched = MediumUntouched && Nearly(R.Rayleigh[C], Sky.Medium.RayleighScattering[C] * Sky.Medium.RayleighStrength);
        Expect(MediumUntouched, "the medium underneath stays physical while the art controls move");
    }
    Sky.SkyTint[0] = Sky.SkyTint[1] = Sky.SkyTint[2] = 1.0f;
    Sky.SkyBrightness = 1.0f;

    // ⑤ The master switch and the tier counts.
    {
        Sky.Enabled = false;
        const SkyConstantRecord Off = Sky.PackSkyRecord();
        Expect(Off.SunRadiance[3]==0.0f && Off.SunRadiance[0]==0.0f, "disabling the sequence disables the record");
        Sky.Enabled = true;
        const SkyConstantRecord On = Sky.PackSkyRecord();
        Expect(On.Control[0]==11u && On.Control[1]==5u, "the tier's atmosphere counts reach the control word");
        Expect(On.SunRadiance[3]==1.0f, "an enabled sky marks the record live");
    }

    // ⑥ Weather uses the same visibility gates as the raster and carries every GPU-facing control row.
    Sky.Cloud.Enabled = true;
    Sky.Cloud.Type = CloudTypeCategory::Cumulonimbus;
    Sky.Cloud.Base = 1700.0f; Sky.Cloud.Thickness = 900.0f;
    Sky.Cloud.Coverage = 0.42f; Sky.Cloud.Density = 1.7f;
    Sky.Cloud.Scale = 0.8f; Sky.Cloud.Anvil = 0.65f; Sky.Cloud.Anisotropy = 0.32f;
    Sky.LocalCloud.Enabled = true;
    Sky.LocalCloud.Centre[0] = -12.0f; Sky.LocalCloud.Centre[1] = 23.0f; Sky.LocalCloud.Centre[2] = 140.0f;
    Sky.LocalCloud.HalfSize[0] = 40.0f; Sky.LocalCloud.HalfSize[1] = 50.0f; Sky.LocalCloud.HalfSize[2] = 30.0f;
    Sky.LocalCloud.Density = 2.1f; Sky.LocalCloud.Coverage = 0.73f; Sky.LocalCloud.Scale = 36.0f;
    Sky.LocalFog.Enabled = true;
    Sky.LocalFog.Centre[0] = 8.0f; Sky.LocalFog.Centre[1] = -7.0f; Sky.LocalFog.Centre[2] = 12.0f;
    Sky.Wind.Speed = 9.0f; Sky.Wind.Bearing = 215.0f; Sky.Wind.Shear = 0.04f; Sky.Wind.Veer = 1.5f;
    Sky.Budget.Volumetrics.CloudSteps = 17u;
    Sky.Budget.Volumetrics.LocalSteps = 13u;
    Sky.Budget.Volumetrics.LightTaps = 5u;
    {
        const SkyConstantRecord R = Sky.PackSkyRecord();
        const uint32_t Media = (1u << 0u) | (1u << 1u) | (1u << 2u);
        Expect((R.Control[2] & Media) == Media, "enabled global and local media reach the sky flags");
        Expect(R.Control[3] == static_cast<uint32_t>(CloudTypeCategory::Cumulonimbus), "the cloud type reaches the control word");
        Expect(Nearly(R.CloudLayer[0], 1700.0f) && Nearly(R.CloudLayer[1], 900.0f)
            && Nearly(R.CloudLayer[2], 0.42f) && Nearly(R.CloudLayer[3], 1.7f), "cloud layer settings reach binding 21");
        Expect(R.CloudControl[0] == 17u && R.CloudControl[1] == 13u && R.CloudControl[2] == 5u,
               "the volumetric tier counts reach the weather rows");
        Expect(Nearly(R.LocalCloudCentre[0], -12.0f) && Nearly(R.LocalCloudHalfSize[2], 30.0f),
               "local volume bounds reach binding 21");
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)] = false;
        const SkyConstantRecord Hidden = Sky.PackSkyRecord();
        Expect((Hidden.Control[2] & (1u << 0u)) == 0u && (Hidden.Control[2] & (1u << 1u)) != 0u,
               "hiding the global cloud leaves an independently shown local cloud live");
        Sky.Shown[static_cast<uint32_t>(CelestialEntity::CloudLayer)] = true;
    }

    // ⑦ The twilight terms pass through untouched.
    Sky.Twilight.GlowIntensity = 0.7f;
    Sky.Twilight.LineIntensity = 0.3f;
    {
        const SkyConstantRecord R = Sky.PackSkyRecord();
        Expect(R.Twilight[0]==0.7f && R.Twilight[1]==0.3f, "the twilight settings arrive as packed");
    }

    std::printf("\n");
    for(int I=0;I<108;++I) std::putchar('=');
    std::printf("\n%s\n\n", Failures==0 ? "  the kernel is packed the raster's sky" : "  THE PACK AND THE RASTER DISAGREE");
    return Failures==0?0:1;
}
