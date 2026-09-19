//============================================================================================================================================
// 📦 Frontier/PlatformInterchange/OnlineInterchange.h — Epic Online Services Platform, P2P NAT and Anti-Cheat Coordination
//============================================================================================================================================

#pragma once

#include "VoiceExchange.h"
#include <string>
#include <string_view>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                ONLINE CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct OnlineConfiguration
{
    std::string             ProductId;                          // [token] EOS product identifier
    std::string             SandboxId;                          // [token] EOS sandbox identifier
    std::string             DeploymentId;                       // [token] EOS deployment identifier
    std::string             ClientCredentialsId;                // [token] EOS client credentials ID
    std::string             ClientCredentialsSecret;            // [token] EOS client credentials secret
    bool                    EnableAntiCheat;                    // [bool] Easy Anti-Cheat initialization
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  ONLINE INTERCHANGE
//------------------------------------------------------------------------------------------------------------------------

class OnlineInterchange
{
public:
    explicit OnlineInterchange(OnlineConfiguration InitialConfig) noexcept;
    ~OnlineInterchange() noexcept;

    OnlineInterchange(const OnlineInterchange&) = delete;
    OnlineInterchange& operator=(const OnlineInterchange&) = delete;

    [[nodiscard]] bool      InitializePlatform() noexcept;
    void                    TerminatePlatform() noexcept;

    void                    AdvanceOnlineCycle() noexcept;

    [[nodiscard]] bool      AuthenticateLocalUser(std::string_view UserToken) noexcept;
    [[nodiscard]] bool      SendDatagram(uint64_t TargetAccountId, const void* PayloadBytes, size_t PayloadLength) noexcept;

    [[nodiscard]] VoiceExchange& QueryVoiceExchange() noexcept { return Voice; }
    [[nodiscard]] bool      IsAuthenticated() const noexcept { return AuthenticatedCondition; }

    // Single unified conversion operator for authentication status
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    OnlineConfiguration     Config;                             // [config] platform connection parameters
    VoiceExchange           Voice;                              // [voice] 3D positional voice exchange
    bool                    InitializedCondition;               // [bool] platform initialized status
    bool                    AuthenticatedCondition;             // [bool] user authenticated status
};

template<>
inline bool OnlineInterchange::Convert<bool>() const noexcept
{
    return AuthenticatedCondition;
}

} // namespace Frontier
