/*==========================================================================================================================================
                                                          PROVIDERTRANSPORT.CPP
==========================================================================================================================================*/
// 🧩 The Epic and Photon adapters over the Project-Networking facades: encode a message to the wire form, hand it to the relay,
//    and decode whatever the receive hook returns. The facades report their own honest status; nothing here invents a connection.

#include "Frontier/Extensions/Networking/ProviderTransport.h"

#include "EosTransport.h"
#include "PhotonTransport.h"

#include <cstring>

namespace Frontier
{

const char* ResolveSessionRelayName(SessionRelay Relay) noexcept
{
    switch (Relay)
    {
        case SessionRelay::Loopback: return "Loopback";
        case SessionRelay::Epic:     return "Epic";
        case SessionRelay::Photon:   return "Photon";
    }
    return "Loopback";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EPIC TRANSPORT
//------------------------------------------------------------------------------------------------------------------------

bool EosTransportAdapter::Start()
{
    Started_ = Networking::StartEosTransport(nullptr);
    return Started_;
}

void EosTransportAdapter::Stop()
{
    if (Started_) Networking::StopEosTransport();
    Started_ = false;
}

void EosTransportAdapter::Tick()
{
    if (Started_) Networking::TickEosTransport();
}

bool EosTransportAdapter::Send(const NetworkMessage& Message)
{
    if (!Started_) return false;
    const std::string Bytes = EncodeNetworkMessage(Message);
    return Networking::SendEosPacket(reinterpret_cast<const unsigned char*>(Bytes.data()), static_cast<unsigned>(Bytes.size()), true);
}

std::vector<NetworkMessage> EosTransportAdapter::Receive()
{
    std::vector<NetworkMessage> Messages;
    if (!Reception_) return Messages;
    for (const std::string& Frame : Reception_())
    {
        NetworkMessage Message;
        if (DecodeNetworkMessage(Frame, Message)) Messages.push_back(std::move(Message));
    }
    return Messages;
}

bool EosTransportAdapter::QueryAvailable() const noexcept
{
    // 📝 The EOS packet relay is not implemented in this slice (SendEosPacket always refuses); the adapter is wired but cannot
    //    carry replication yet, so the router treats it as unavailable and keeps the session on a working relay.
    return false;
}

std::string EosTransportAdapter::QueryReading() const
{
    const Networking::EosStatus Status = Networking::InspectEosStatus();
    return std::string(Status.Reading);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHOTON TRANSPORT
//------------------------------------------------------------------------------------------------------------------------

bool PhotonTransportAdapter::Start(const std::string& IdentityToken, const std::string& RoomName)
{
    Networking::PhotonCredentials Credentials;
    Credentials.IdTokenJwt = IdentityToken.empty() ? nullptr : IdentityToken.c_str();
    Credentials.RoomName   = RoomName.empty() ? nullptr : RoomName.c_str();
    Started_ = Networking::StartPhotonTransport(Credentials, nullptr);
    return Started_;
}

void PhotonTransportAdapter::Stop()
{
    if (Started_) Networking::StopPhotonTransport();
    Started_ = false;
}

void PhotonTransportAdapter::Tick()
{
    if (Started_) Networking::TickPhotonTransport();
}

bool PhotonTransportAdapter::Send(const NetworkMessage& Message)
{
    if (!Started_) return false;
    const std::string Bytes = EncodeNetworkMessage(Message);
    return Networking::SendPhotonPacket(reinterpret_cast<const std::uint8_t*>(Bytes.data()), Bytes.size(), true);
}

std::vector<NetworkMessage> PhotonTransportAdapter::Receive()
{
    std::vector<NetworkMessage> Messages;
    if (!Reception_) return Messages;
    for (const std::string& Frame : Reception_())
    {
        NetworkMessage Message;
        if (DecodeNetworkMessage(Frame, Message)) Messages.push_back(std::move(Message));
    }
    return Messages;
}

bool PhotonTransportAdapter::QueryAvailable() const noexcept
{
    return Networking::PhotonLinkAvailable();
}

std::string PhotonTransportAdapter::QueryReading() const
{
    const Networking::PhotonStatus Status = Networking::InspectPhotonStatus();
    return std::string(Status.Reading);
}

} // namespace Frontier
