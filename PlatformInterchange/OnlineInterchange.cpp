//============================================================================================================================================
// 📦 Frontier/PlatformInterchange/OnlineInterchange.cpp — EOS Platform Interchange Implementation
//============================================================================================================================================

#include "OnlineInterchange.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

OnlineInterchange::OnlineInterchange(OnlineConfiguration InitialConfig) noexcept
    : Config(std::move(InitialConfig))
    , InitializedCondition(false)
    , AuthenticatedCondition(false)
{
}

OnlineInterchange::~OnlineInterchange() noexcept
{
    TerminatePlatform();
}

bool OnlineInterchange::InitializePlatform() noexcept
{
    InitializedCondition = true;
    return true;
}

void OnlineInterchange::TerminatePlatform() noexcept
{
    if (InitializedCondition)
    {
        Voice.LeaveRoom();
        AuthenticatedCondition = false;
        InitializedCondition = false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                CYCLE ADVANCEMENT
//------------------------------------------------------------------------------------------------------------------------

void OnlineInterchange::AdvanceOnlineCycle() noexcept
{
    if (!InitializedCondition)
    {
        return;
    }

    // Corresponds to EOS_Platform_Tick()
}

bool OnlineInterchange::AuthenticateLocalUser(std::string_view UserToken) noexcept
{
    if (!InitializedCondition || UserToken.empty())
    {
        return false;
    }

    AuthenticatedCondition = true;
    return true;
}

bool OnlineInterchange::SendDatagram(uint64_t TargetAccountId, const void* PayloadBytes, size_t PayloadLength) noexcept
{
    (void)TargetAccountId;
    (void)PayloadBytes;
    (void)PayloadLength;
    return AuthenticatedCondition;
}

} // namespace Frontier
