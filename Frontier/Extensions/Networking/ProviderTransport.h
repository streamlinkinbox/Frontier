/*==========================================================================================================================================
                                                           PROVIDERTRANSPORT.H
==========================================================================================================================================*/
// 🧩 The provider transports behind NetworkTransport: Epic Online Services and Photon Realtime, each wrapping the
//    Project-Networking facade (EosTransport / PhotonTransport) so the engine's replication rides the same relay the login app
//    uses. The facades are SDK-free at this boundary; the real SDK implementations replace the stubs at link time on Windows.
//    A receive hook lets the host hand the SDK's incoming packets to the space; until a provider carries packets both ways the
//    SessionTransportRouter falls back to the loopback, so a session link always works.

#pragma once

#include "Frontier/Extensions/Networking/NetworkingExtension.h"

#include <functional>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which relay a session rides. Loopback is the in-process pair (always available); Epic and Photon are the provider relays.
enum class SessionRelay : uint8_t
{
    Loopback,
    Epic,
    Photon,
};

[[nodiscard]] const char* ResolveSessionRelayName(SessionRelay Relay) noexcept;

// 📝 The incoming-packet hook: the host drains the provider SDK's receive queue into wire-form frames. Empty until the SDK's
//    receive path is wired; the adapter then reports no incoming messages.
using PacketReception = std::function<std::vector<std::string>()>;

//------------------------------------------------------------------------------------------------------------------------
//                                                    EPIC TRANSPORT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Epic Online Services direct path. The facade's packet relay is not implemented in this slice (SendEosPacket refuses), so the
//    adapter reports unavailable and the router keeps the session on a relay that carries packets. The wiring stands ready for
//    the EOS P2P relay the lobby plan names.
class EosTransportAdapter final : public NetworkTransport
{
  public:
    bool Start();
    void Stop();
    void Tick();
    void ConfigureReception(PacketReception Reception) noexcept { Reception_ = std::move(Reception); }

    bool                                      Send(const NetworkMessage& Message) override;
    [[nodiscard]] std::vector<NetworkMessage> Receive() override;
    [[nodiscard]] const char*                 QueryName() const noexcept override { return "Epic"; }
    [[nodiscard]] bool                        QueryAvailable() const noexcept;
    [[nodiscard]] std::string                 QueryReading() const;

  private:
    PacketReception Reception_{};
    bool            Started_ = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHOTON TRANSPORT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Photon Realtime relay. Available only when the official SDK libs are linked (PhotonLinkAvailable); the stub reports
//    unlinked and the router falls back. Credentials carry the EOS identity token; no token is logged.
class PhotonTransportAdapter final : public NetworkTransport
{
  public:
    bool Start(const std::string& IdentityToken = {}, const std::string& RoomName = {});
    void Stop();
    void Tick();
    void ConfigureReception(PacketReception Reception) noexcept { Reception_ = std::move(Reception); }

    bool                                      Send(const NetworkMessage& Message) override;
    [[nodiscard]] std::vector<NetworkMessage> Receive() override;
    [[nodiscard]] const char*                 QueryName() const noexcept override { return "Photon"; }
    [[nodiscard]] bool                        QueryAvailable() const noexcept;
    [[nodiscard]] std::string                 QueryReading() const;

  private:
    PacketReception Reception_{};
    bool            Started_ = false;
};

} // namespace Frontier
