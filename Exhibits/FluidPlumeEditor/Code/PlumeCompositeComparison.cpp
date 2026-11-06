/*==========================================================================================================================================
                                                     PLUMECOMPOSITECOMPARISON.CPP
==========================================================================================================================================*/
// 🧩 The plume picture's composite as it was before its backdrop was kept, against Frontier::ResolvePlumePicture: first the bytes of every picture
//    compared, then the time of each. The earlier composite is OldPlumeComposite.inc, which RunPlumeCompositeComparison.sh cuts out of an earlier
//    commit's PlumeExtension.cpp byte for byte, so nothing of it is rewritten here. No device is needed. The script builds this at -O2: the times are
//    of optimised code on whatever CPU runs them (a record of that CPU, not a figure for the Windows machine).

#include "Frontier/Extensions/Plume/PlumeExtension.h"

#include "Frontier/Engine/Rendering/GlobalIllumination/IlluminationConstants.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/ShaderInterchange.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

namespace Frontier
{

using namespace GlobalIllumination;
using namespace PlumeSimulation;

namespace Before
{
#include "OldPlumeComposite.inc"
} // namespace Before

namespace
{

// 📝 Random half floats for a march of Trace texels. A wild march holds any bit pattern a third of the time (NaN, infinity, denormals, negatives, huge
//    numbers), a tame one only radiance of about the plume's size.
std::vector<uint8_t> ConstructRadiance(std::mt19937& Random, std::array<uint32_t, 2> Trace, bool Wild)
{
    std::vector<uint8_t> Radiance(static_cast<size_t>(Trace[0]) * Trace[1] * 4u * 2u);
    for (size_t Index = 0; Index + 1 < Radiance.size(); Index += 2)
    {
        uint16_t       Half;
        const uint32_t Kind = Random() % 10u;
        if (!Wild) Half = static_cast<uint16_t>(0x3000u + (Random() & 0x0FFFu));
        else if (Kind < 3) Half = static_cast<uint16_t>(Random());
        else if (Kind < 4) Half = 0;
        else Half = static_cast<uint16_t>(0x2000u + (Random() & 0x1FFFu));
        std::memcpy(&Radiance[Index], &Half, 2);
    }
    return Radiance;
}

// 📝 A camera at (X, Y, Z) looking at the emitter's height, with the field of view and the picture's aspect.
CameraPose ConstructPose(const std::array<double, 4>& Placement, std::array<uint32_t, 2> Picture)
{
    CameraPose Camera{};
    Camera.Position            = { Placement[0], Placement[1], Placement[2] };
    Camera.Forward             = NormalizeVector(Vector3{ -Placement[0], -Placement[1], 1.8 - Placement[2] });
    Camera.VerticalFieldOfView = Placement[3];
    Camera.Aspect              = static_cast<double>(Picture[0]) / Picture[1];
    return Camera;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ① THE BYTES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Six picture sizes, and for each six camera placements (the first twice, then a new position, a new field of view, a new direction and the first
//    again) with a march and without one, so the backdrop is resolved, kept and dropped. Returns how many pictures differ from the earlier composite's.
int VerifyAgainstBefore(int& Total)
{
    std::mt19937 Random(11);

    const std::array<std::array<uint32_t, 2>, 6> Pictures   = { { { 554, 438 }, { 1108, 718 }, { 641, 397 }, { 1840, 960 }, { 97, 61 }, { 5, 3 } } };
    const std::array<std::array<double, 4>, 6>   Placements = { { { 8.0, 0.0, 3.0, 0.78 }, { 8.0, 0.0, 3.0, 0.78 }, { -4.0, 6.0, 1.5, 0.78 }, { 8.0, 0.0, 3.0, 0.6 }, { 0.0, -9.0, 0.4, 1.2 }, { 8.0, 0.0, 3.0, 0.78 } } };

    int Differing = 0;
    for (const auto& Picture : Pictures)
    {
        const uint32_t                Scale = Before::ResolveTraceScale(Picture);
        const std::array<uint32_t, 2> Trace = { (Picture[0] + Scale - 1u) / Scale, (Picture[1] + Scale - 1u) / Scale };
        const std::vector<uint8_t>    Radiance = ConstructRadiance(Random, Trace, true);

        PlumeBackdropPicture Kept;
        int                  Differs = 0, Count = 0;
        for (const auto& Placement : Placements)
        {
            for (const bool Idle : { false, true })
            {
                const CameraPose     Camera = ConstructPose(Placement, Picture);
                std::vector<uint8_t> Earlier, Later;
                Before::CompositePicture(Radiance, Idle, Camera, Picture, Trace, Scale, Earlier);
                ResolvePlumePicture(Radiance, Idle, Camera, Picture, Trace, Scale, Kept, Later);
                ++Count;
                if (Earlier != Later) ++Differs;
            }
        }
        std::printf("%s\t%u x %u picture: %d of %d pictures differ from the earlier composite's\n", Differs == 0 ? "PASS" : "FAIL", Picture[0], Picture[1], Differs, Count);
        Total += Count;
        Differing += Differs;
    }
    return Differing;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ② THE TIME
//------------------------------------------------------------------------------------------------------------------------

// 📝 Three stage sizes: the earlier composite, the new one with the camera at rest (the backdrop kept), and the new one while the camera turns (the
//    backdrop resolved again each time). Milliseconds a picture, the mean of twelve after one to warm up.
void EstimateCost()
{
    std::mt19937 Random(7);

    const std::array<std::array<uint32_t, 2>, 3> Pictures = { { { 554, 438 }, { 1108, 718 }, { 1840, 960 } } };
    for (const auto& Picture : Pictures)
    {
        const uint32_t                Scale    = Before::ResolveTraceScale(Picture);
        const std::array<uint32_t, 2> Trace    = { (Picture[0] + Scale - 1u) / Scale, (Picture[1] + Scale - 1u) / Scale };
        const std::vector<uint8_t>    Radiance = ConstructRadiance(Random, Trace, false);

        CameraPose           Camera = ConstructPose({ 8.0, 0.0, 3.0, 0.78 }, Picture);
        std::vector<uint8_t> Pixels;
        PlumeBackdropPicture Kept;
        constexpr int        Rounds = 12;

        const auto Measure = [&](auto&& Body) {
            Body();
            const auto Begin = std::chrono::steady_clock::now();
            for (int Round = 0; Round < Rounds; ++Round) Body();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begin).count() / Rounds;
        };
        const double EarlierMs = Measure([&] { Before::CompositePicture(Radiance, false, Camera, Picture, Trace, Scale, Pixels); });
        const double AtRestMs  = Measure([&] { ResolvePlumePicture(Radiance, false, Camera, Picture, Trace, Scale, Kept, Pixels); });

        double Yaw = 0.0;
        const double TurningMs = Measure([&] {
            Yaw += 0.01;
            Camera.Forward = NormalizeVector(Vector3{ -5.0, Yaw, -1.2 });
            ResolvePlumePicture(Radiance, false, Camera, Picture, Trace, Scale, Kept, Pixels);
        });
        std::printf("INFO\t%u x %u picture: earlier composite %.2f ms; now, camera at rest %.2f ms; now, camera turning %.2f ms (this CPU, -O2: a record, not a performance claim)\n",
                    Picture[0], Picture[1], EarlierMs, AtRestMs, TurningMs);
    }
}

} // namespace

} // namespace Frontier

int main()
{
    int       Total     = 0;
    const int Differing = Frontier::VerifyAgainstBefore(Total);
    Frontier::EstimateCost();
    std::printf("[PlumeCompositeComparison] %d comparisons, %d differ\n", Total, Differing);
    return Differing == 0 ? 0 : 1;
}
