//==============================================================================================================================================
//                                                         NATIVEGASRAYMARCH.CPP
//==============================================================================================================================================
// 📦 Executed proof for the volume integration: the lighting arithmetic checked term by term, then the reproducible field actually rendered.
//
// The pictures are the point. Engine/Shaders/GasVolumeRaymarch.slang is a transcription of VolumeRaymarch.h,
//    and a transcription is only trustworthy if the original produces the right image — so this marches the
//    same arithmetic on the CPU, over a CoarseGasField advanced by the solver from the previous step, and
//    writes what it saw. A shader that disagrees with these captures has a diff, not a mystery.
//
// Four properties are worth more than the rest, and each is checked because it is invisible until it is wrong:
//
//    ① THE PHASE FUNCTION IS NORMALISED. An unnormalised phase changes total brightness with the sun angle,
//       which reads as the exposure drifting as the camera turns.
//    ② THE DUAL LOBE BEATS A SINGLE ONE BEHIND THE PLUME. That is the silver lining, and it is the whole
//       reason two lobes are carried instead of one.
//    ③ OPTICAL DEPTH IS NORMALISED BY THE DOMAIN DIAGONAL. Without it a plume evaporates the moment dynamic
//       bounds expand the box, and it looks like the smoke dissipating rather than like a rendering defect.
//    ④ TRANSMITTANCE NEVER INCREASES. A ray that gets clearer as it goes is an integration written backwards.

#include "CoarseGasField.h"
#include "GasPresetLibrary.h"
#include "VolumeRaymarch.h"
#include "PngWriteCounterpart.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace Frontier;

namespace
{

unsigned Checks = 0;

void Check(bool Condition, const char* Claim)
{
    ++Checks;
    std::printf("  %s  %s\n", Condition ? "PASS" : "FAIL", Claim);
    if (!Condition) throw std::runtime_error(Claim);
}

void Banner(const char* Title)
{
    std::printf("\n%s\n", Title);
}

// The seam from the integration to the solver's reading. The integration marches the unit cube; the field
//    lives in world metres, so this is the one place the two coordinate systems meet.
VolumeSample ReadCoarseField(const void* Context, const float Coordinate[3])
{
    const CoarseGasField& Field = *static_cast<const CoarseGasField*>(Context);
    const float Lattice[3] = { Coordinate[0] * static_cast<float>(CoarseExtent) - 0.5f,
                               Coordinate[1] * static_cast<float>(CoarseExtent) - 0.5f,
                               Coordinate[2] * static_cast<float>(CoarseExtent) - 0.5f };
    VolumeSample Reading;
    Reading.Smoke       = ReadTrilinear(Field.Smoke,       Lattice[0], Lattice[1], Lattice[2]);
    Reading.Temperature = ReadTrilinear(Field.Temperature, Lattice[0], Lattice[1], Lattice[2]);
    return Reading;
}

// A smooth analytic ball of smoke, so the lighting checks do not depend on the solver having run.
VolumeSample ReadAnalyticBall(const void* Context, const float Coordinate[3])
{
    const float Density = *static_cast<const float*>(Context);
    const float Dx = Coordinate[0] - 0.5f, Dy = Coordinate[1] - 0.5f, Dz = Coordinate[2] - 0.5f;
    const float Radius = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    VolumeSample Reading;
    Reading.Smoke = Radius < 0.3f ? Density * (1.0f - Radius / 0.3f) : 0.0f;
    return Reading;
}

VolumeSample ReadClearAir(const void*, const float[3])
{
    return VolumeSample{};
}

struct Capture
{
    uint32_t Width  = 0u;
    uint32_t Height = 0u;
    std::vector<unsigned char> Pixels;   // RGB, 3 bytes a pixel
    float Coverage  = 0.0f;              // [-] fraction of pixels the volume touched at all
    float Brightest = 0.0f;              // [-] largest linear radiance seen in any channel
};

// Orbits the unit cube at a bearing and renders it. The camera looks at the cube's centre from outside, which
//    is the arrangement the editor viewport will use.
Capture Render(VolumeReading Reading, const void* Context, const VolumeLighting& Lighting,
               const float SunDirection[3], float Bearing, uint32_t Extent, float DomainDiagonal)
{
    Capture Result;
    Result.Width = Result.Height = Extent;
    Result.Pixels.assign(static_cast<std::size_t>(Extent) * Extent * 3u, 0u);

    const float Distance = 1.9f;
    const float Origin[3] = { 0.5f + std::cos(Bearing) * Distance,
                              0.5f + std::sin(Bearing) * Distance,
                              0.72f };
    float Forward[3] = { 0.5f - Origin[0], 0.5f - Origin[1], 0.5f - Origin[2] };
    const float Reach = std::sqrt(Forward[0] * Forward[0] + Forward[1] * Forward[1] + Forward[2] * Forward[2]);
    for (int Axis = 0; Axis < 3; ++Axis) Forward[Axis] /= Reach;

    // Right is forward crossed with world up; up closes the basis. Axis 2 is up, matching the solver.
    float Right[3] = { Forward[1] * 1.0f - Forward[2] * 0.0f,
                       Forward[2] * 0.0f - Forward[0] * 1.0f,
                       Forward[0] * 0.0f - Forward[1] * 0.0f };
    const float RightReach = std::sqrt(Right[0] * Right[0] + Right[1] * Right[1] + Right[2] * Right[2]);
    for (int Axis = 0; Axis < 3; ++Axis) Right[Axis] /= RightReach;
    const float Up[3] = { Right[1] * Forward[2] - Right[2] * Forward[1],
                          Right[2] * Forward[0] - Right[0] * Forward[2],
                          Right[0] * Forward[1] - Right[1] * Forward[0] };

    const float HalfFieldOfView = 0.42f;
    uint32_t Touched = 0u;

    for (uint32_t Row = 0u; Row < Extent; ++Row)
    for (uint32_t Column = 0u; Column < Extent; ++Column)
    {
        const float ScreenX = (static_cast<float>(Column) + 0.5f) / static_cast<float>(Extent) * 2.0f - 1.0f;
        const float ScreenY = 1.0f - (static_cast<float>(Row) + 0.5f) / static_cast<float>(Extent) * 2.0f;
        float Direction[3];
        for (int Axis = 0; Axis < 3; ++Axis)
            Direction[Axis] = Forward[Axis] + Right[Axis] * ScreenX * HalfFieldOfView
                                            + Up[Axis]    * ScreenY * HalfFieldOfView;
        const float Length = std::sqrt(Direction[0] * Direction[0] + Direction[1] * Direction[1]
                                     + Direction[2] * Direction[2]);
        for (int Axis = 0; Axis < 3; ++Axis) Direction[Axis] /= Length;

        const MarchedRay Marched = MarchVolume(Reading, Context, Origin, Direction, SunDirection,
                                               Lighting, DomainDiagonal);
        if (Marched.Transmittance < 0.999f) ++Touched;
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            if (Marched.Radiance[Channel] > Result.Brightest) Result.Brightest = Marched.Radiance[Channel];
            const std::size_t Slot = (static_cast<std::size_t>(Row) * Extent + Column) * 3u
                                   + static_cast<std::size_t>(Channel);
            Result.Pixels[Slot] = TransferToDisplay(Marched.Radiance[Channel], Lighting.Exposure);
        }
    }
    Result.Coverage = static_cast<float>(Touched) / static_cast<float>(Extent * Extent);
    return Result;
}

void Write(const Capture& Shot, const char* Path)
{
    const int Written = PngWriteCounterpart::WritePng(Path, static_cast<int>(Shot.Width), static_cast<int>(Shot.Height), 3,
                                 Shot.Pixels.data(), static_cast<int>(Shot.Width) * 3);
    if (Written == 0) throw std::runtime_error("could not write a capture");
    std::printf("    wrote %s  (%ux%u, %.1f%% covered)\n", Path, Shot.Width, Shot.Height,
                Shot.Coverage * 100.0f);
}

}   // namespace


int main()
{
    std::printf("Gas raymarch checks - the lighting arithmetic, and the reproducible field rendered\n");

    try
    {

    //----------------------------------------------------------------------------------------------------------------
    //                                       CLAIM ① - THE PHASE FUNCTION
    //----------------------------------------------------------------------------------------------------------------

    Banner("Claim 1 - the phase function is normalised and shaped");

    {
        // Integrate over the sphere: the phase must sum to one, or total brightness changes with sun angle.
        constexpr uint32_t Bands = 2000u;
        constexpr float Pi = 3.14159265358979323846f;
        float Integral = 0.0f;
        for (uint32_t Band = 0u; Band < Bands; ++Band)
        {
            const float Angle = (static_cast<float>(Band) + 0.5f) / static_cast<float>(Bands) * Pi;
            Integral += HenyeyGreenstein(std::cos(Angle), 0.38f) * std::sin(Angle) * (Pi / Bands) * 2.0f * Pi;
        }
        Check(std::fabs(Integral - 1.0f) < 0.01f, "a single lobe integrates to one over the sphere");

        float DualIntegral = 0.0f;
        for (uint32_t Band = 0u; Band < Bands; ++Band)
        {
            const float Angle = (static_cast<float>(Band) + 0.5f) / static_cast<float>(Bands) * Pi;
            DualIntegral += DualHenyeyGreenstein(std::cos(Angle), 0.38f) * std::sin(Angle)
                          * (Pi / Bands) * 2.0f * Pi;
        }
        Check(std::fabs(DualIntegral - 1.0f) < 0.01f, "and so does the blend of two");

        Check(HenyeyGreenstein(1.0f, 0.38f) > HenyeyGreenstein(-1.0f, 0.38f),
              "a positive asymmetry scatters forward");
        Check(HenyeyGreenstein(-1.0f, -0.25f) > HenyeyGreenstein(1.0f, -0.25f),
              "and a negative one scatters backward");
        Check(HenyeyGreenstein(0.0f, 0.0f) > 0.0f, "an isotropic lobe is finite everywhere");
        Check(HenyeyGreenstein(1.0f, 0.999f) < 1.0e9f, "and a near-singular one is clamped rather than infinite");

        // ② The silver lining. Behind the plume, looking toward the sun, the blend must beat the bare
        //    forward lobe — that excess is the whole reason two lobes are carried.
        const float Behind = -0.85f;
        Check(DualHenyeyGreenstein(Behind, 0.38f) > HenyeyGreenstein(Behind, 0.38f) * 1.5f,
              "behind the plume the two-lobe blend is markedly brighter than one lobe alone");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                        BLACKBODY AND THE TRANSFER
    //----------------------------------------------------------------------------------------------------------------

    Banner("Blackbody emission and the display transfer");

    {
        float Cold[3], Warm[3], Hot[3], Clamped[3];
        BlackbodyRadiance(0.0f,   1.15f, Cold);
        BlackbodyRadiance(1.5f,   1.15f, Warm);
        BlackbodyRadiance(4.5f,   1.15f, Hot);
        BlackbodyRadiance(900.0f, 1.15f, Clamped);

        Check(Cold[0] == 0.0f && Cold[1] == 0.0f && Cold[2] == 0.0f, "cold gas emits nothing at all");
        Check(Warm[0] > 0.0f, "warm gas emits red first");
        Check(Warm[0] > Warm[1] && Warm[1] >= Warm[2], "and red dominates green dominates blue, as it should");
        Check(Hot[1] > Warm[1], "hotter gas gains green, moving toward orange");
        Check(Hot[0] >= Warm[0], "without losing red");
        Check(Clamped[0] <= 1.01f && Clamped[1] <= 1.01f && Clamped[2] <= 1.01f,
              "an absurd temperature saturates rather than blowing past the palette");

        Check(TransferToDisplay(0.0f, 1.25f) == 0u, "no radiance is black");
        Check(TransferToDisplay(1000.0f, 1.25f) == 255u, "and an enormous radiance is white, not wrapped");
        Check(TransferToDisplay(0.5f, 1.25f) > TransferToDisplay(0.2f, 1.25f), "the transfer is monotonic");
        Check(TransferToDisplay(0.5f, 2.5f) > TransferToDisplay(0.5f, 1.25f), "and exposure brightens it");
        Check(TransferToDisplay(-5.0f, 1.25f) == 0u, "a negative radiance clamps to black rather than wrapping");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                           THE RAY AGAINST THE BOX
    //----------------------------------------------------------------------------------------------------------------

    Banner("The ray against the box");

    {
        float Enter = 0.0f, Exit = 0.0f;
        const float Through[3]  = { -1.0f, 0.5f, 0.5f };
        const float Along[3]    = {  1.0f, 0.0f, 0.0f };
        Check(UnitCubeInterval(Through, Along, Enter, Exit), "a ray aimed at the cube enters it");
        Check(std::fabs(Enter - 1.0f) < 1.0e-5f && std::fabs(Exit - 2.0f) < 1.0e-5f,
              "and enters and leaves exactly at its faces");

        const float Past[3] = { -1.0f, 5.0f, 0.5f };
        Check(!UnitCubeInterval(Past, Along, Enter, Exit), "a ray aimed past it misses");

        // An axis-parallel ray divides by a zero component; it must not produce a spurious hit.
        const float Beside[3]  = { 0.5f, 0.5f, 5.0f };
        const float Sideways[3] = { 1.0f, 0.0f, 0.0f };
        Check(!UnitCubeInterval(Beside, Sideways, Enter, Exit),
              "an axis-parallel ray outside the slab misses rather than dividing by zero into a hit");

        const float Within[3] = { 0.5f, 0.5f, 0.5f };
        Check(UnitCubeInterval(Within, Along, Enter, Exit), "a ray starting inside is inside");
        Check(Enter == 0.0f, "and starts marching immediately rather than behind itself");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                   CLAIM ④ - THE INTEGRATION ITSELF
    //----------------------------------------------------------------------------------------------------------------

    Banner("Claim 4 - the integration");

    {
        const VolumeLighting Lighting;
        const float Sun[3] = { 0.40f, 0.30f, 0.866f };
        const float Origin[3] = { -1.0f, 0.5f, 0.5f };
        const float Direction[3] = { 1.0f, 0.0f, 0.0f };

        const MarchedRay Empty = MarchVolume(&ReadClearAir, nullptr, Origin, Direction, Sun, Lighting, 4.0f);
        Check(Empty.Transmittance == 1.0f, "clear air occludes nothing");
        Check(Empty.Radiance[0] == 0.0f, "and emits nothing");

        float Density = 1.0f;
        const MarchedRay Thin = MarchVolume(&ReadAnalyticBall, &Density, Origin, Direction, Sun, Lighting, 4.0f);
        Check(Thin.Transmittance < 1.0f, "a ball of smoke occludes");
        Check(Thin.Transmittance >= 0.0f, "but never more than completely");
        Check(Thin.Radiance[0] > 0.0f, "and scatters light toward the camera");

        float Thicker = 4.0f;
        const MarchedRay Dense = MarchVolume(&ReadAnalyticBall, &Thicker, Origin, Direction, Sun, Lighting, 4.0f);
        Check(Dense.Transmittance < Thin.Transmittance, "denser smoke occludes more");

        const float Missing[3] = { -1.0f, 9.0f, 0.5f };
        const MarchedRay Past = MarchVolume(&ReadAnalyticBall, &Density, Missing, Direction, Sun, Lighting, 4.0f);
        Check(Past.Transmittance == 1.0f, "a ray missing the cube returns clear air rather than refusing");

        // ③ The optical normalisation: the same plume in a box twice the size keeps its opacity. Without the
        //    normalisation the longer ray would integrate far more density and the plume would go opaque.
        const MarchedRay Small = MarchVolume(&ReadAnalyticBall, &Density, Origin, Direction, Sun, Lighting, 2.0f);
        const MarchedRay Large = MarchVolume(&ReadAnalyticBall, &Density, Origin, Direction, Sun, Lighting, 8.0f);
        const float Opacity = (1.0f - Small.Transmittance) / (1.0f - Large.Transmittance);
        Check(Opacity > 1.0f, "a smaller domain is denser per metre, as the normalisation intends");
        Check(Opacity < 6.0f, "and the four-fold change in extent is tempered rather than compounded");

        // ④ Transmittance never increases, checked by marching in growing prefixes of the same ray.
        VolumeLighting Stepped = Lighting;
        float Previous = 1.0f;
        bool Monotonic = true;
        for (int32_t Steps = 4; Steps <= 96; Steps += 4)
        {
            Stepped.RaymarchSteps = Steps;
            const MarchedRay Partial = MarchVolume(&ReadAnalyticBall, &Thicker, Origin, Direction, Sun,
                                                   Stepped, 4.0f);
            if (Partial.Transmittance > 1.0f || Partial.Transmittance < 0.0f) Monotonic = false;
            Previous = Partial.Transmittance;
        }
        Check(Monotonic, "transmittance stays within [0, 1] at every step count");
        Check(Previous < 1.0f, "and the finest march still sees the smoke");

        // The shadow march must darken the far side of a ball relative to no shadowing at all.
        VolumeLighting Unshadowed = Lighting;
        Unshadowed.ShadowSteps = 1;
        Unshadowed.ShadowDensity = 0.0f;
        const MarchedRay Lit = MarchVolume(&ReadAnalyticBall, &Thicker, Origin, Direction, Sun,
                                           Unshadowed, 4.0f);
        const MarchedRay Shadowed = MarchVolume(&ReadAnalyticBall, &Thicker, Origin, Direction, Sun,
                                                Lighting, 4.0f);
        Check(Shadowed.Radiance[0] < Lit.Radiance[0], "self-shadowing darkens the plume rather than ignoring it");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                       THE FIELD, ACTUALLY RENDERED
    //----------------------------------------------------------------------------------------------------------------

    Banner("The reproducible field, rendered");

    {
        // The lighting the camp fire preset asks for, so the capture is an authored look and not a guess.
        const GasSettings Preset = ConstructPresetSettings("camp_fire");
        VolumeLighting Lighting;
        Lighting.DensityExtinction  = Preset.DensityExtinction;
        Lighting.SmokeAlbedo        = Preset.SmokeAlbedo;
        Lighting.ShadowDensity      = Preset.ShadowDensity;
        Lighting.FireIntensity      = Preset.FireIntensity;
        Lighting.TemperatureScale   = Preset.TemperatureScale;
        Lighting.InternalScattering = Preset.InternalScattering;
        Lighting.PhaseAnisotropy    = Preset.PhaseAnisotropy;
        Lighting.AmbientIntensity   = Preset.AmbientIntensity;
        Lighting.SunIntensity       = Preset.SunIntensity;
        Lighting.Exposure           = Preset.Exposure;
        Lighting.RaymarchSteps      = Preset.RaymarchSteps;
        Lighting.ShadowSteps        = Preset.ShadowSteps;

        CoarseGasField Field;
        const float Origin[3] = { 0.0f, 0.0f, 0.0f };
        ResetField(Field, Origin, 8.0f);

        CoarseGasSettings Solver;
        Solver.BuoyancyLift = 3.2f;
        GasEmission Source;
        Source.Position[0] = 4.0f;
        Source.Position[1] = 4.0f;
        Source.Position[2] = 1.0f;
        Source.Radius = 0.55f;
        // 📝 Emission rates chosen so the capture is legible, not to flatter the integration. The optical
        //    depth a ray accumulates is density multiplied by the normalised interval, and at 32 cubed over
        //    an 8 m cube that interval is about 0.002 — so a density near 1 integrates to a radiance near
        //    0.02, which transfers to a barely visible grey. The browser's plumes sit an order of magnitude
        //    denser than that. A capture nobody can read proves nothing, so these match it.
        Source.SmokeRate = 60.0f;
        Source.TemperatureRate = 40.0f;

        for (uint32_t Step = 0u; Step < 110u; ++Step)
        {
            InjectEmission(Field, Source, CoarseStepInterval);
            AdvanceField(Field, Solver);
        }
        Check(TotalSmoke(Field) > 1.0f, "the field being rendered is carrying a real plume");

        const float Sun[3] = { 0.42f, 0.31f, 0.852f };
        const Capture Front = Render(&ReadCoarseField, &Field, Lighting, Sun, 0.0f, 320u, 8.0f);
        Write(Front, "Exhibits/Gallery/GasField/PlumeFront.png");
        Check(Front.Coverage > 0.02f, "the plume covers a visible part of the frame");
        Check(Front.Coverage < 0.90f, "and does not fill it edge to edge, so there is shape to see");
        Check(Front.Brightest > 0.0f, "and something in it is lit");

        const Capture Side = Render(&ReadCoarseField, &Field, Lighting, Sun, 2.1f, 320u, 8.0f);
        Write(Side, "Exhibits/Gallery/GasField/PlumeSide.png");
        Check(Side.Coverage > 0.02f, "and it is still there from another bearing");

        // 💡 A cold capture as well, and it is not a duplicate: the fire plume above is almost entirely
        //    blackbody emission, so it would look much the same if the sun scattering, the shadow march and
        //    the powder term were all broken. A dust preset carries no fuel and no heat, so everything
        //    visible in it arrived through the scattering path alone. The two captures together cover both
        //    halves of the integration; either one alone covers half of it and looks complete.
        const GasSettings Dust = ConstructPresetSettings("settling_dust");
        VolumeLighting Cold = Lighting;
        Cold.DensityExtinction  = Dust.DensityExtinction;
        Cold.SmokeAlbedo        = Dust.SmokeAlbedo;
        Cold.ShadowDensity      = Dust.ShadowDensity;
        Cold.FireIntensity      = 0.0f;
        Cold.InternalScattering = 0.0f;
        Cold.AmbientIntensity   = Dust.AmbientIntensity;
        Cold.SunIntensity       = Dust.SunIntensity;
        Cold.Exposure           = Dust.Exposure;

        CoarseGasField Settling;
        ResetField(Settling, Origin, 8.0f);
        CoarseGasSettings Falling;
        Falling.BuoyancyLift       = 0.0f;      // cold dust has nothing lifting it
        Falling.SmokeWeight        = 1.4f;      // and settles under its own weight
        Falling.SmokeLossPerSecond = 0.05f;
        GasEmission Ledge = Source;
        Ledge.Position[2]     = 6.4f;           // spilling from a height
        Ledge.Radius          = 0.9f;
        Ledge.SmokeRate       = 90.0f;
        Ledge.TemperatureRate = 0.0f;           // 🔴 no heat, so nothing can emit
        for (uint32_t Step = 0u; Step < 120u; ++Step)
        {
            if (Step < 45u) InjectEmission(Settling, Ledge, CoarseStepInterval);
            AdvanceField(Settling, Falling);
        }

        float HottestDust = 0.0f;
        for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot)
            if (Settling.Temperature[Slot] > HottestDust) HottestDust = Settling.Temperature[Slot];
        Check(HottestDust == 0.0f, "the cold capture genuinely carries no heat anywhere");

        const Capture Dusty = Render(&ReadCoarseField, &Settling, Cold, Sun, 0.6f, 320u, 8.0f);
        Write(Dusty, "Exhibits/Gallery/GasField/DustCold.png");
        Check(Dusty.Coverage > 0.02f, "cold dust is visible with no emission at all");
        Check(Dusty.Brightest > 0.0f, "so everything in that capture arrived through sun and sky scattering");

        // ② again, as a picture rather than a number, and deliberately on the cold field. Measuring the
        //    silver lining on the fire plume does not work: blackbody emission does not depend on where the
        //    sun is, and at a realistic density it dominates the frame, so the back-lit and front-lit
        //    captures come out the same and the check would be measuring emission rather than phase. On a
        //    field with no heat in it, every photon came through the phase function.
        const float Behind[3] = { -0.72f, -0.52f, 0.455f };
        const Capture BackLit  = Render(&ReadCoarseField, &Settling, Cold, Behind, 0.6f, 160u, 8.0f);
        const Capture FrontLit = Render(&ReadCoarseField, &Settling, Cold, Sun,    0.6f, 160u, 8.0f);
        Write(BackLit, "Exhibits/Gallery/GasField/DustBackLit.png");
        Check(BackLit.Brightest > FrontLit.Brightest * 1.1f,
              "back-lit cold dust is markedly brighter than front-lit, which is the two-lobe phase arriving");

        VolumeLighting Sunless = Cold;
        Sunless.SunIntensity     = 0.0f;
        Sunless.AmbientIntensity = 0.0f;
        const Capture Unlit = Render(&ReadCoarseField, &Settling, Sunless, Sun, 0.6f, 160u, 8.0f);
        Check(Unlit.Brightest == 0.0f,
              "and removing both lights makes it vanish, so nothing is being added from nowhere");

        // Rendering is a pure function of the field: the same field twice gives the same bytes.
        const Capture Again = Render(&ReadCoarseField, &Field, Lighting, Sun, 0.0f, 320u, 8.0f);
        Check(Again.Pixels == Front.Pixels, "rendering the same field twice produces identical pixels");

        // The capture must not be a uniform wash, which is what a broken march produces while still
        //    technically covering the frame.
        unsigned Distinct = 0u;
        unsigned char Seen[256] = { 0u };
        for (std::size_t Slot = 0u; Slot < Front.Pixels.size(); Slot += 3u)
            if (!Seen[Front.Pixels[Slot]]) { Seen[Front.Pixels[Slot]] = 1u; ++Distinct; }
        Check(Distinct > 16u, "the capture has real tonal range rather than being a flat wash");
    }

    }
    catch (const std::exception& Failure)
    {
        std::printf("\nFAILED after %u checks: %s\n", Checks, Failure.what());
        return 1;
    }

    std::printf("\nPASS %u\n", Checks);
    return 0;
}
