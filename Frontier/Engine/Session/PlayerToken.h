#pragma once
/*==========================================================================================================================================
                                                             PLAYERTOKEN.H
==========================================================================================================================================*/
// 🧩 A player's identity inside a session: a small value token, issued by SessionSpace, never reused while
//    the session lives. Profiles, Agents and drivers all refer to players by this and nothing else.

#include <cstdint>

namespace Frontier
{

struct PlayerToken
{
    uint64_t Value = 0;   // 0 = nobody

    [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != 0; }
    constexpr bool operator==(const PlayerToken& Other) const noexcept { return Value == Other.Value; }
    constexpr bool operator!=(const PlayerToken& Other) const noexcept { return Value != Other.Value; }
};

} // namespace Frontier
