//============================================================================================================================================
// 📦 Frontier/PlatformInterchange/VoiceExchange.cpp — Positional Voice Room Routing Implementation
//============================================================================================================================================

#include "VoiceExchange.h"
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

VoiceExchange::VoiceExchange() noexcept
    : LocalPosition{ 0.0f, 0.0f, 0.0f }
    , LocalForward{ 0.0f, 0.0f, -1.0f }
    , LocalMuted(false)
{
    Participants.reserve(32);
}

bool VoiceExchange::JoinRoom(std::string_view RoomNameToken) noexcept
{
    ActiveRoomName = RoomNameToken;
    return true;
}

void VoiceExchange::LeaveRoom() noexcept
{
    ActiveRoomName.clear();
    Participants.clear();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                PARTICIPANT MANAGEMENT
//------------------------------------------------------------------------------------------------------------------------

void VoiceExchange::AssignLocalTransform(const Vector3& Position, const Vector3& Forward) noexcept
{
    LocalPosition = Position;
    LocalForward = Forward.Normalized();
}

void VoiceExchange::AssignMuted(bool Muted) noexcept
{
    LocalMuted = Muted;
}

void VoiceExchange::RegisterPeer(uint64_t AccountId, const Vector3& InitialPosition) noexcept
{
    VoiceParticipant Peer{};
    Peer.AccountIdentifier  = AccountId;
    Peer.SpatialLocation    = InitialPosition;
    Peer.SpeakingCondition  = false;
    Peer.MutedCondition     = false;
    Peer.VolumeMultiplier   = 1.0f;

    Participants.push_back(Peer);
}

void VoiceExchange::UnregisterPeer(uint64_t AccountId) noexcept
{
    auto iterator = std::remove_if(Participants.begin(), Participants.end(),
        [AccountId](const VoiceParticipant& p) { return p.AccountIdentifier == AccountId; });
    Participants.erase(iterator, Participants.end());
}

size_t VoiceExchange::QueryParticipantCount() const noexcept
{
    return Participants.size();
}

} // namespace Frontier
