/*==========================================================================================================================================
                                                   PLUMECOMPOSITEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The plume picture's composite (Frontier/Extensions/Plume/PlumeComposite.h), checked four ways:
//      ① the kept backdrop gives the picture a backdrop resolved afresh for every call gives: over a run of calls that repeats the camera, changes each of
//        the eight numbers the backdrop depends on in turn, returns to the first camera and changes the picture's size, with a march and without one;
//      ② the backdrop really is kept: a value written into it is read again while the camera and the size are the last ones', and gone the moment any one
//        of them changes;
//      ③ the march's texels: a value that is not finite is dark and opaque, an alpha beyond [0, 1] is held to it, premultiplied radiance lands as radiance
//        times the exposure through the sRGB encoding, and each picture pixel takes the texel nearest it, edges included;
//      ④ the backdrop itself: a sky that is blue above the horizon, a grey checker below it.
//
//        PlumeCompositeValidation
//
// 📝 Needs no device and no files: the composite is CPU code. What a composite costs is printed for the record, never asserted.

#include "Frontier/Engine/Rendering/GlobalIllumination/ShadowProjection.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/TransformProjection.h"
#include "Frontier/Extensions/Plume/PlumeComposite.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::GlobalIllumination;

namespace
{

int Assertions = 0;
int Failures   = 0;

void Verify(bool Condition, const std::string& Caption)
{
    ++Assertions;
    std::printf("%s\t%s\n", Condition ? "PASS" : "FAIL", Caption.c_str());
    if (!Condition)
    {
        ++Failures;
        std::cerr << "FAIL: " << Caption << '\n';
    }
}

using Size = std::array<uint32_t, 2>;

constexpr uint32_t TraceScale = 2;   // [-] picture pixels to a trace texel, as the extension's smallest

Size ResolveTrace(Size Picture)
{
    return { (Picture[0] + TraceScale - 1) / TraceScale, (Picture[1] + TraceScale - 1) / TraceScale };
}

// 📝 A camera on the stage, looking at a point: the numbers the composite reads, set the way the extension sets them.
CameraPose ConstructPose(Vector3 Position, Vector3 Target, double VerticalFieldOfView, double Aspect)
{
    CameraPose Pose;
    Pose.Position            = Position;
    Pose.Target              = Target;
    Pose.Forward             = NormalizeVector({ Target[0] - Position[0], Target[1] - Position[1], Target[2] - Position[2] });
    Pose.VerticalFieldOfView = VerticalFieldOfView;
    Pose.Aspect              = Aspect;
    return Pose;
}

// 📝 The half-float patterns this test writes, each named by its bits.
constexpr uint16_t HalfZero      = 0x0000;
constexpr uint16_t HalfOne       = 0x3C00;
constexpr uint16_t HalfTwo       = 0x4000;
constexpr uint16_t HalfMinusOne  = 0xBC00;
constexpr uint16_t HalfNaN       = 0x7E00;
constexpr uint16_t HalfInfinity  = 0x7C00;
constexpr uint16_t HalfDenormal  = 0x0001;   // [-] 2^-24, the smallest positive half

std::vector<uint8_t> ConstructRadiance(Size Trace, std::array<uint16_t, 4> Texel)
{
    std::vector<uint8_t> Radiance(static_cast<size_t>(Trace[0]) * Trace[1] * 8u);
    for (size_t Index = 0; Index < static_cast<size_t>(Trace[0]) * Trace[1]; ++Index)
        for (size_t Channel = 0; Channel < 4; ++Channel) std::memcpy(&Radiance[(Index * 4u + Channel) * 2u], &Texel[Channel], 2);
    return Radiance;
}

// 📝 Random texels: a third of them any bit pattern at all (NaN, infinity, denormals, negatives, huge values), a tenth zero, the rest between 2^-8 and 2.
std::vector<uint8_t> ConstructRandomRadiance(Size Trace, std::mt19937& Random)
{
    std::vector<uint8_t> Radiance(static_cast<size_t>(Trace[0]) * Trace[1] * 8u);
    for (size_t Index = 0; Index + 1 < Radiance.size(); Index += 2)
    {
        const uint32_t Kind = Random() % 10u;
        uint16_t       Half;
        if (Kind < 3) Half = static_cast<uint16_t>(Random());
        else if (Kind < 4) Half = 0;
        else Half = static_cast<uint16_t>(0x2000u + (Random() & 0x1FFFu));
        std::memcpy(&Radiance[Index], &Half, 2);
    }
    return Radiance;
}

// 📝 The composite with nothing kept: a new backdrop picture for every call.
std::vector<uint8_t> ResolveAfresh(const std::vector<uint8_t>& Radiance, bool Idle, const CameraPose& Camera, Size Picture)
{
    PlumeBackdropPicture Fresh;
    std::vector<uint8_t> Pixels;
    ResolvePlumePicture(Radiance, Idle, Camera, Picture, ResolveTrace(Picture), TraceScale, Fresh, Pixels);
    return Pixels;
}

std::vector<uint8_t> ResolveKept(const std::vector<uint8_t>& Radiance, bool Idle, const CameraPose& Camera, Size Picture, PlumeBackdropPicture& Kept)
{
    std::vector<uint8_t> Pixels;
    ResolvePlumePicture(Radiance, Idle, Camera, Picture, ResolveTrace(Picture), TraceScale, Kept, Pixels);
    return Pixels;
}

struct Alteration
{
    std::string Caption;
    CameraPose  Pose;
    Size        Picture;
};

// 📝 The first camera, then the eight numbers the backdrop depends on changed one at a time (each from the first camera), the first camera again and a new size.
std::vector<Alteration> ConstructAlterations(Size Picture)
{
    const CameraPose First = ConstructPose({ 8.0, 0.5, 3.0 }, { 3.0, 0.0, 1.8 }, 0.78, static_cast<double>(Picture[0]) / Picture[1]);
    std::vector<Alteration> AlterationSequence;
    AlterationSequence.push_back({ "the first camera", First, Picture });
    AlterationSequence.push_back({ "the same camera again", First, Picture });
    for (int Number = 0; Number < 8; ++Number)
    {
        CameraPose Pose = First;
        std::string Caption;
        switch (Number)
        {
        case 0: Pose.Position[0] += 0.5; Caption = "Position x"; break;
        case 1: Pose.Position[1] += 0.5; Caption = "Position y"; break;
        case 2: Pose.Position[2] += 0.5; Caption = "Position z"; break;
        case 3: Pose.Forward[0] = -Pose.Forward[0]; Caption = "Forward x"; break;   // 📝 a sign flip: one component alone changes, the direction stays a unit one
        case 4: Pose.Forward[1] = -Pose.Forward[1]; Caption = "Forward y"; break;
        case 5: Pose.Forward[2] = -Pose.Forward[2]; Caption = "Forward z"; break;
        case 6: Pose.VerticalFieldOfView += 0.1; Caption = "VerticalFieldOfView"; break;
        default: Pose.Aspect += 0.1; Caption = "Aspect"; break;
        }
        AlterationSequence.push_back({ Caption + " changed", Pose, Picture });
    }
    AlterationSequence.push_back({ "back to the first camera", First, Picture });
    const Size Larger = { Picture[0] + 3u, Picture[1] + 5u };
    AlterationSequence.push_back({ "the picture's size changed", First, Larger });
    AlterationSequence.push_back({ "the larger picture again", First, Larger });
    return AlterationSequence;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       ① THE KEPT BACKDROP AGAINST ONE RESOLVED AFRESH
//------------------------------------------------------------------------------------------------------------------------

void ValidateKeptAgainstAfresh()
{
    std::mt19937 Random(11);
    for (const Size Picture : { Size{ 97, 61 }, Size{ 330, 200 }, Size{ 554, 438 } })
    {
        PlumeBackdropPicture Kept;
        int                  Different = 0, Compared = 0;
        for (const Alteration& Step : ConstructAlterations(Picture))
        {
            const std::vector<uint8_t> Radiance = ConstructRandomRadiance(ResolveTrace(Step.Picture), Random);
            for (const bool Idle : { false, true })
            {
                const bool Same = ResolveKept(Radiance, Idle, Step.Pose, Step.Picture, Kept) == ResolveAfresh(Radiance, Idle, Step.Pose, Step.Picture);
                ++Compared;
                if (!Same)
                {
                    ++Different;
                    Verify(false, std::to_string(Picture[0]) + " x " + std::to_string(Picture[1]) + ": " + Step.Caption + (Idle ? ", no march" : ", with a march")
                                      + " differs from a composite with nothing kept");
                }
            }
        }
        Verify(Different == 0, std::to_string(Picture[0]) + " x " + std::to_string(Picture[1]) + ": " + std::to_string(Compared)
                                   + " composites over the run (repeat, eight numbers changed, return, new size; with and without a march) equal one with nothing kept");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                ② THE BACKDROP IS REALLY KEPT
//------------------------------------------------------------------------------------------------------------------------

void ValidateKept()
{
    const Size                   Picture = { 97, 61 };
    const std::vector<Alteration> AlterationSequence = ConstructAlterations(Picture);
    const std::vector<uint8_t>   NoMarch;   // [-] unused when Idle
    const auto                   Contaminate = [](PlumeBackdropPicture& Kept) { std::fill(Kept.Colours.begin(), Kept.Colours.end(), 9.0f); };
    const auto                   AllWhite  = [](const std::vector<uint8_t>& Pixels) {
        for (const uint8_t Byte : Pixels) if (Byte != 255) return false;
        return !Pixels.empty();
    };

    PlumeBackdropPicture Kept;
    (void)ResolveKept(NoMarch, true, AlterationSequence[0].Pose, Picture, Kept);
    Verify(Kept.Resolved && Kept.Colours.size() == static_cast<size_t>(Picture[0]) * Picture[1] * 3u, "the first call resolves the backdrop: three floats a pixel");
    Contaminate(Kept);
    Verify(AllWhite(ResolveKept(NoMarch, true, AlterationSequence[0].Pose, Picture, Kept)), "the same camera and size: the backdrop is read as it was kept (a poisoned 9.0 everywhere comes out white)");
    Verify(AllWhite(ResolveKept(NoMarch, true, AlterationSequence[1].Pose, Picture, Kept)), "the same camera again, a third time: still the kept backdrop");

    // 📝 Every change drops the poison and gives what a composite with nothing kept gives.
    for (const Alteration& Step : AlterationSequence)
    {
        if (Step.Caption.find("changed") == std::string::npos) continue;   // 📝 the repeats and the return are no change from the first camera
        PlumeBackdropPicture Probe;
        (void)ResolveKept(NoMarch, true, AlterationSequence[0].Pose, Picture, Probe);   // [-] kept for the first camera at the first size
        Contaminate(Probe);
        const std::vector<uint8_t> Changed = ResolveKept(NoMarch, true, Step.Pose, Step.Picture, Probe);
        Verify(!AllWhite(Changed) && Changed == ResolveAfresh(NoMarch, true, Step.Pose, Step.Picture), Step.Caption + ": the kept backdrop is dropped and resolved again");
    }

    PlumeBackdropPicture Larger;
    (void)ResolveKept(NoMarch, true, AlterationSequence.back().Pose, AlterationSequence.back().Picture, Larger);
    Contaminate(Larger);
    Verify(AllWhite(ResolveKept(NoMarch, true, AlterationSequence.back().Pose, AlterationSequence.back().Picture, Larger)), "the larger picture again with the camera unchanged: the kept backdrop is read");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ③ THE MARCH'S TEXELS
//------------------------------------------------------------------------------------------------------------------------

void ValidateTexels()
{
    const CameraPose Pose = ConstructPose({ 8.0, 0.5, 3.0 }, { 3.0, 0.0, 1.8 }, 0.78, 1.0);
    const Size       Picture = { 4, 4 };
    const Size       Trace   = ResolveTrace(Picture);
    PlumeBackdropPicture Kept;

    const std::vector<uint8_t> Backdrop = ResolveKept({}, true, Pose, Picture, Kept);   // [-] the backdrop alone
    const auto ResolveWith = [&](std::array<uint16_t, 4> Texel) { return ResolveKept(ConstructRadiance(Trace, Texel), false, Pose, Picture, Kept); };

    Verify(ResolveWith({ HalfZero, HalfZero, HalfZero, HalfOne }) == Backdrop, "no radiance and an opaque alpha: the picture is the backdrop alone, bit for bit");
    Verify(ResolveWith({ HalfNaN, HalfNaN, HalfNaN, HalfNaN }) == Backdrop, "a NaN in every channel: dark and opaque, the picture is the backdrop alone");
    Verify(ResolveWith({ HalfInfinity, HalfInfinity, HalfInfinity, HalfInfinity }) == Backdrop, "an infinity in every channel: dark and opaque, the picture is the backdrop alone");
    Verify(ResolveWith({ HalfZero, HalfZero, HalfZero, HalfTwo }) == Backdrop, "an alpha of 2 is held to 1: the picture is the backdrop alone");

    const std::vector<uint8_t> Black = ResolveWith({ HalfZero, HalfZero, HalfZero, HalfMinusOne });
    bool                       AllBlack = true;
    for (size_t Index = 0; Index < Black.size(); ++Index) AllBlack = AllBlack && (Index % 4u == 3u ? Black[Index] == 255 : Black[Index] == 0);
    Verify(AllBlack, "an alpha of -1 is held to 0: no backdrop, no radiance, black and opaque");

    // 📝 sRGB of 0.42 (one times the exposure), worked out here with the exact curve: the picture's table may differ from it by one level.
    const double Linear   = static_cast<double>(PlumeExposure);
    const double Expected = 255.0 * (1.055 * std::pow(Linear, 1.0 / 2.4) - 0.055);
    const std::vector<uint8_t> Radiant = ResolveWith({ HalfOne, HalfOne, HalfOne, HalfZero });
    bool                       Lands = true;
    for (size_t Index = 0; Index < Radiant.size(); ++Index) Lands = Lands && (Index % 4u == 3u ? Radiant[Index] == 255 : std::fabs(Radiant[Index] - Expected) <= 1.0);
    Verify(Lands, "premultiplied radiance of 1 with an alpha of 0: " + std::to_string(Radiant[0]) + " in every channel, sRGB of the exposure " + std::to_string(Expected) + " (within one level)");

    const std::vector<uint8_t> Negative = ResolveWith({ HalfMinusOne, HalfMinusOne, HalfMinusOne, HalfZero });
    bool                       NegativeBlack = true;
    for (size_t Index = 0; Index < Negative.size(); ++Index) NegativeBlack = NegativeBlack && (Index % 4u == 3u ? Negative[Index] == 255 : Negative[Index] == 0);
    Verify(NegativeBlack, "a negative radiance encodes as black, never wraps");

    const std::vector<uint8_t> Denormal = ResolveWith({ HalfDenormal, HalfDenormal, HalfDenormal, HalfZero });
    bool                       DenormalBlack = true;
    for (size_t Index = 0; Index < Denormal.size(); ++Index) DenormalBlack = DenormalBlack && (Index % 4u == 3u ? Denormal[Index] == 255 : Denormal[Index] == 0);
    Verify(DenormalBlack, "the smallest half (2^-24) is a number, not a fault: it encodes as black");

    // 📝 The nearest texel: texel (0, 0) bright, the others black, on a 4 x 4 picture of 2 x 2 texels: exactly the top-left 2 x 2 pixels are bright.
    {
        std::vector<uint8_t> Radiance = ConstructRadiance(Trace, { HalfZero, HalfZero, HalfZero, HalfZero });
        for (size_t Channel = 0; Channel < 3; ++Channel) std::memcpy(&Radiance[Channel * 2u], &HalfOne, 2);
        const std::vector<uint8_t> Pixels = ResolveKept(Radiance, false, Pose, Picture, Kept);
        bool                       Placed = true;
        for (uint32_t Y = 0; Y < Picture[1]; ++Y)
            for (uint32_t X = 0; X < Picture[0]; ++X)
            {
                const bool Bright = Pixels[(static_cast<size_t>(Y) * Picture[0] + X) * 4u] > 100;
                Placed            = Placed && (Bright == (X < 2 && Y < 2));
            }
        Verify(Placed, "texel (0, 0) bright and the others black: the top-left 2 x 2 pixels are bright, no others");
    }

    // 📝 A picture that is not a whole number of texels: 5 x 3 pixels, 3 x 2 texels; the last texel (2, 1) reaches only pixel (4, 2).
    {
        const Size Odd      = { 5, 3 };
        const Size OddTrace = ResolveTrace(Odd);
        std::vector<uint8_t> Radiance = ConstructRadiance(OddTrace, { HalfZero, HalfZero, HalfZero, HalfZero });
        const size_t Last = (static_cast<size_t>(1) * OddTrace[0] + 2u) * 4u;
        for (size_t Channel = 0; Channel < 3; ++Channel) std::memcpy(&Radiance[(Last + Channel) * 2u], &HalfOne, 2);
        const std::vector<uint8_t> Pixels = ResolveKept(Radiance, false, Pose, Odd, Kept);
        bool                       Placed = true;
        for (uint32_t Y = 0; Y < Odd[1]; ++Y)
            for (uint32_t X = 0; X < Odd[0]; ++X)
            {
                const bool Bright = Pixels[(static_cast<size_t>(Y) * Odd[0] + X) * 4u] > 100;
                Placed            = Placed && (Bright == (X == 4 && Y == 2));
            }
        Verify(Placed && Pixels.size() == static_cast<size_t>(Odd[0]) * Odd[1] * 4u, "a 5 x 3 picture of 3 x 2 texels: only pixel (4, 2) takes the last texel, and the picture is sized to the pixels");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ④ THE BACKDROP ITSELF
//------------------------------------------------------------------------------------------------------------------------

void ValidateBackdrop()
{
    // 📝 Level with the horizon, 1.5 m up, 0.9 rad of vertical field of view: the top rows look at the sky, the bottom rows at the ground a few metres away.
    const CameraPose Pose = ConstructPose({ 0.0, 0.0, 1.5 }, { 10.0, 0.0, 1.5 }, 0.9, 1.5);
    const Size       Picture = { 96, 64 };
    PlumeBackdropPicture Kept;
    const std::vector<uint8_t> Pixels = ResolveKept({}, true, Pose, Picture, Kept);
    const auto RetrievePixel = [&](uint32_t X, uint32_t Y) {
        const size_t At = (static_cast<size_t>(Y) * Picture[0] + X) * 4u;
        return std::array<int, 4>{ Pixels[At], Pixels[At + 1], Pixels[At + 2], Pixels[At + 3] };
    };
    const auto Sky    = RetrievePixel(48, 2);
    const auto Ground = RetrievePixel(48, 62);
    Verify(Sky[2] > Sky[1] && Sky[1] > Sky[0] && Sky[2] - Sky[0] > 60, "the top row looks at the sky: blue above green above red, a strong blue");
    Verify(std::abs(Ground[2] - Ground[0]) < 30, "the bottom row looks at the ground: grey, no blue cast (red " + std::to_string(Ground[0]) + ", blue " + std::to_string(Ground[2]) + ")");

    int Jumps = 0, SmallSteps = 0;
    for (uint32_t X = 1; X < Picture[0]; ++X)
    {
        const int Step = std::abs(RetrievePixel(X, 62)[1] - RetrievePixel(X - 1, 62)[1]);
        if (Step >= 8) ++Jumps;
        else if (Step > 0) ++SmallSteps;
    }
    Verify(Jumps >= 2, "along the bottom row the ground flips between two greys where the two-metre checker changes (" + std::to_string(Jumps) + " jumps of eight levels or more; the fade between them moves "
                           + std::to_string(SmallSteps) + " smaller steps)");

    bool Opaque = true;
    for (size_t Index = 3; Index < Pixels.size(); Index += 4) Opaque = Opaque && Pixels[Index] == 255;
    Verify(Opaque && Pixels.size() == static_cast<size_t>(Picture[0]) * Picture[1] * 4u, "every pixel is opaque and the picture is sized to the pixels");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      WHAT A COMPOSITE COSTS
//------------------------------------------------------------------------------------------------------------------------

void EstimateCost()
{
    std::mt19937 Random(7);
    for (const Size Picture : { Size{ 554, 438 }, Size{ 1108, 718 } })
    {
        const Size                 Trace    = ResolveTrace(Picture);
        const std::vector<uint8_t> Radiance = ConstructRandomRadiance(Trace, Random);
        CameraPose                 Pose     = ConstructPose({ 8.0, 0.0, 3.0 }, { 3.0, 0.0, 1.8 }, 0.78, static_cast<double>(Picture[0]) / Picture[1]);
        PlumeBackdropPicture       Kept;
        std::vector<uint8_t>       Pixels;
        ResolvePlumePicture(Radiance, false, Pose, Picture, Trace, TraceScale, Kept, Pixels);
        constexpr int Rounds = 3;
        auto          Begin  = std::chrono::steady_clock::now();
        for (int Round = 0; Round < Rounds; ++Round) ResolvePlumePicture(Radiance, false, Pose, Picture, Trace, TraceScale, Kept, Pixels);
        const double AtRest = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begin).count() / Rounds;
        Begin               = std::chrono::steady_clock::now();
        for (int Round = 0; Round < Rounds; ++Round)
        {
            Pose.Forward = NormalizeVector({ Pose.Forward[0], Pose.Forward[1] + 0.01 * (Round + 1), Pose.Forward[2] });
            ResolvePlumePicture(Radiance, false, Pose, Picture, Trace, TraceScale, Kept, Pixels);
        }
        const double Turning = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begin).count() / Rounds;
        std::printf("INFO\t%u x %u picture: composite %.2f ms with the camera at rest, %.2f ms while it turns (this build, this CPU: a record, not a performance claim)\n", Picture[0],
                    Picture[1], AtRest, Turning);
    }
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    ValidateKeptAgainstAfresh();
    ValidateKept();
    ValidateTexels();
    ValidateBackdrop();
    EstimateCost();
    std::printf("[PlumeCompositeValidation] %d assertions, %d failed\n", Assertions, Failures);
    return Failures == 0 ? 0 : 1;
}
