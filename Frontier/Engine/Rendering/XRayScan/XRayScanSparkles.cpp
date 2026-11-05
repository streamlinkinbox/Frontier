/*==========================================================================================================================================
                                                        XRAYSCANSPARKLES.CPP
==========================================================================================================================================*/

#include "XRayScanSparkles.h"

namespace Frontier::XRayScan
{

double SparkleGenerator::Next()
{
    // 📝 Every operation is the JavaScript's 32-bit one (>>> 0, Math.imul, the bitwise operators): unsigned arithmetic wraps the same way.
    State_ += 0x6d2b79f5u;
    uint32_t T = State_;
    T = (T ^ (T >> 15)) * (T | 1u);
    T ^= T + (T ^ (T >> 7)) * (T | 61u);
    return static_cast<double>(T ^ (T >> 14)) / 4294967296.0;
}

std::vector<float> ConstructSparkleCloud(uint32_t Count, const std::array<double, 3>& Box, double Centre)
{
    SparkleGenerator Generator;
    std::vector<float> Records(static_cast<size_t>(Count) * 4, 0.0f);
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        Records[Index * 4]     = static_cast<float>(Box[0] * (0.5 - Generator.Next()));
        Records[Index * 4 + 1] = static_cast<float>(Centre + Box[1] * (0.5 - Generator.Next()));
        Records[Index * 4 + 2] = static_cast<float>(Box[2] * (0.5 - Generator.Next()));
    }
    return Records;
}

} // namespace Frontier::XRayScan
