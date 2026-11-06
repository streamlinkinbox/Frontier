#pragma once
/*==========================================================================================================================================
                                                       HALFFLOATCONVERSION.H
==========================================================================================================================================*/
// 🧩 The one conversion from a 32-bit float to the 16-bit float that the RGBA16F targets hold. Truncating, as it always was: the mantissa is cut, not
//    rounded, and the exponent saturates to zero or to infinity. It lives in a header so that the upload of float texels and the optimized path that
//    converts only the texels that changed use the same code, which is what keeps their results the same bit for bit.

#include <cstdint>
#include <cstring>

namespace Frontier
{

[[nodiscard]] inline uint16_t ConvertFloat32ToHalf16(float InputScalar) noexcept
{
    uint32_t Bits = 0u;
    std::memcpy(&Bits, &InputScalar, sizeof(uint32_t));
    const uint32_t Sign = (Bits >> 16u) & 0x8000u;
    const int32_t  Exp  = static_cast<int32_t>((Bits >> 23u) & 0xFFu) - 127 + 15;
    const uint32_t Mantissa = Bits & 0x007FFFFFu;
    if (Exp <= 0)
        return static_cast<uint16_t>(Sign);
    if (Exp >= 31)
        return static_cast<uint16_t>(Sign | 0x7C00u);
    return static_cast<uint16_t>(Sign | (static_cast<uint32_t>(Exp) << 10u) | (Mantissa >> 13u));
}

} // namespace Frontier
