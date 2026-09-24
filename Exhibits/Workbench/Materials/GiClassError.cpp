// GI class error — which class of pixel carries the residual? (roadmap #5, report §14.6)
//
//    The mirror writes a per-pixel GI classification next to the film (`--class-map`), and §14.5's counters say what
//    SHARE of the frame falls in each class. That is not the same question as where the ERROR is: a class can be 69 % of
//    the pixels and carry none of the residual (if its estimator is good), or 5 % of them and carry most of it. This
//    tool answers the second question by masking an RMSE against a converged reference — the two arms it compares are
//    the ReSTIR path and the plain path at the SAME resolve rate, so the difference between the columns is what reuse
//    did, per class.
//
//    Inputs are raw 8-bit dumps (ImageMagick does the PNG decoding: `convert x.png -depth 8 rgb:-`), so this file has no
//    dependencies beyond the C++ standard library and can be compiled anywhere the gates compile.
//
//    The RMSE is reported on the 16-bit scale ImageMagick's `compare -metric RMSE` uses (8-bit value × 257), and the
//    TOTAL line must reproduce that command's number — the driver checks it, so a mismatch in the masking arithmetic
//    cannot pass silently.
//
//    Grey step: the class map is written as class × 32, so 0, 32, 64, ... 224.
//
//    Usage: GiClassError --arm A.rgb --ref R.rgb --class C.gray --width W --height H [--arm2 B.rgb]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace
{
const char* kClassNames[8] = { "none (no geometry)", "bad (degenerate draw)", "escape (BSDF ray saw the sky)",
                               "emitter hit", "unlit receiver", "unusable (glass/SSS)", "VERTEX (the pool's coverage)",
                               "no sample (empty candidate set)" };

std::vector<unsigned char> ReadRaw(const std::string& Path, size_t Expected, const char* What)
{
    FILE* F = std::fopen(Path.c_str(), "rb");
    if (F == nullptr) { std::printf("GiClassError: cannot open %s (%s)\n", Path.c_str(), What); std::exit(2); }
    std::vector<unsigned char> Data(Expected);
    const size_t Got = std::fread(Data.data(), 1, Expected, F);
    std::fclose(F);
    if (Got != Expected)
    {
        std::printf("GiClassError: %s is %zu bytes, expected %zu (%s)\n", Path.c_str(), Got, Expected, What);
        std::exit(2);
    }
    return Data;
}
}   // namespace

int main(int ArgumentCount, char** ArgumentValues)
{
    std::string ArmPath, Arm2Path, RefPath, ClassPath;
    int Width = 0, Height = 0;
    for (int I = 1; I < ArgumentCount; ++I)
    {
        const std::string A = ArgumentValues[I];
        const char* Next = (I + 1 < ArgumentCount) ? ArgumentValues[I + 1] : nullptr;
        auto Need = [&](const char* Name) -> std::string
        {
            if (Next == nullptr) { std::printf("GiClassError: %s needs a value\n", Name); std::exit(2); }
            ++I; return std::string(Next);
        };
        if      (A == "--arm")   ArmPath   = Need("--arm");
        else if (A == "--arm2")  Arm2Path  = Need("--arm2");
        else if (A == "--ref")   RefPath   = Need("--ref");
        else if (A == "--class") ClassPath = Need("--class");
        else if (A == "--width") Width     = std::atoi(Need("--width").c_str());
        else if (A == "--height") Height   = std::atoi(Need("--height").c_str());
        else { std::printf("GiClassError: unknown argument %s\n", A.c_str()); return 2; }
    }
    if (ArmPath.empty() || RefPath.empty() || ClassPath.empty() || Width <= 0 || Height <= 0)
    {
        std::printf("usage: GiClassError --arm A.rgb --ref R.rgb --class C.gray --width W --height H [--arm2 B.rgb]\n");
        return 2;
    }

    const size_t Pixels = static_cast<size_t>(Width) * static_cast<size_t>(Height);
    const std::vector<unsigned char> Arm  = ReadRaw(ArmPath, Pixels * 3u, "arm");
    const std::vector<unsigned char> Ref  = ReadRaw(RefPath, Pixels * 3u, "reference");
    const std::vector<unsigned char> Cls  = ReadRaw(ClassPath, Pixels, "class map");
    const bool HaveArm2 = !Arm2Path.empty();
    const std::vector<unsigned char> Arm2 = HaveArm2 ? ReadRaw(Arm2Path, Pixels * 3u, "arm2")
                                                    : std::vector<unsigned char>();

    // 16-bit scale, the way ImageMagick reports RMSE for an 8-bit image: value × 257 (so a full-scale error is 65535).
    const double Scale = 257.0;
    double SumSq[8] = {0, 0, 0, 0, 0, 0, 0, 0};      // arm 1
    double SumSq2[8] = {0, 0, 0, 0, 0, 0, 0, 0};     // arm 2
    long   Count[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    double TotalSumSq = 0.0, TotalSumSq2 = 0.0;
    long   TotalCount = 0;

    for (size_t P = 0; P < Pixels; ++P)
    {
        const unsigned char C = Cls[P] / 32u;
        const int K = C <= 7u ? static_cast<int>(C) : 0;
        ++Count[K];
        ++TotalCount;
        for (int Ch = 0; Ch < 3; ++Ch)
        {
            const double D = (static_cast<double>(Arm[P * 3u + static_cast<size_t>(Ch)]) -
                              static_cast<double>(Ref[P * 3u + static_cast<size_t>(Ch)])) * Scale;
            SumSq[K] += D * D;
            TotalSumSq += D * D;
            if (HaveArm2)
            {
                const double D2 = (static_cast<double>(Arm2[P * 3u + static_cast<size_t>(Ch)]) -
                                   static_cast<double>(Ref[P * 3u + static_cast<size_t>(Ch)])) * Scale;
                SumSq2[K] += D2 * D2;
                TotalSumSq2 += D2 * D2;
            }
        }
    }

    auto Rmse = [&](double SumSq_, long Count_) -> double
    {
        return Count_ > 0 ? std::sqrt(SumSq_ / (static_cast<double>(Count_) * 3.0)) : 0.0;
    };

    std::printf("%-32s %8s %7s %12s %10s %12s %10s %10s\n", "class", "pixels", "share",
                "ReSTIR RMSE", "MSE share", "plain RMSE", "MSE share", "excess");
    for (int K = 0; K < 8; ++K)
        std::printf("%-32s %8ld %6.1f%% %12.2f %9.1f%% %12.2f %9.1f%% %9.2fx\n", kClassNames[K], Count[K],
                    100.0 * static_cast<double>(Count[K]) / static_cast<double>(TotalCount),
                    Rmse(SumSq[K], Count[K]), 100.0 * SumSq[K] / (TotalSumSq + 1e-30),
                    HaveArm2 ? Rmse(SumSq2[K], Count[K]) : 0.0,
                    HaveArm2 ? 100.0 * SumSq2[K] / (TotalSumSq2 + 1e-30) : 0.0,
                    (HaveArm2 && SumSq2[K] > 0.0) ? std::sqrt(SumSq[K] / SumSq2[K]) : 0.0);
    // A machine-readable tail: the driver checks it against `compare -metric RMSE` on the same PNGs, so a bug in the
    //    masking arithmetic cannot pass as a result.
    std::printf("TOTAL_RMSE %.2f %.2f\n", Rmse(TotalSumSq, TotalCount), HaveArm2 ? Rmse(TotalSumSq2, TotalCount) : 0.0);
    std::printf("%-32s %8ld %6.1f%% %12.2f %9.1f%% %12.2f %9.1f%% %9.2fx\n", "TOTAL (must match compare -metric RMSE)",
                TotalCount, 100.0, Rmse(TotalSumSq, TotalCount), 100.0,
                HaveArm2 ? Rmse(TotalSumSq2, TotalCount) : 0.0, HaveArm2 ? 100.0 : 0.0,
                (HaveArm2 && TotalSumSq2 > 0.0) ? std::sqrt(TotalSumSq / TotalSumSq2) : 0.0);
    return 0;
}
