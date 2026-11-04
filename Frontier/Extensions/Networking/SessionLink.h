/*==========================================================================================================================================
                                                              SESSIONLINK.H
==========================================================================================================================================*/
// 🧩 The session link a workspace toggles: it owns the session's NetworkingSpace, the replication extension and the transport
//    router, and runs the host/client loop. A host replicates its changed WorldSpace records and exchanges them; a client
//    exchanges and absorbs. Start greets the peers, Integrate steps the loop at a fixed rate, Stop farewells. The Quick Controls
//    "Session link" toggle drives it; the relay (Loopback / Epic / Photon) is chosen by the router and reported honestly.

#pragma once

#include "Frontier/Extensions/Networking/NetworkingExtension.h"
#include "Frontier/Extensions/Networking/SessionTransportRouter.h"

#include <memory>
#include <string>

namespace Frontier
{

class WorldSpace;

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

struct SessionLinkStatus
{
    bool        Linked        = false;
    SessionRelay Relay        = SessionRelay::Loopback;
    std::string Reading{};                  // [-] the router's relay reading
    size_t      Peers         = 0;          // [-] admitted peers (the local peer included)
    uint64_t    MessagesSent  = 0;
    uint64_t    MessagesReceived = 0;
    size_t      Posted        = 0;          // [-] record messages the last host replicate posted
    size_t      Applied       = 0;          // [-] messages the last client absorb applied
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       SESSION LINK
//------------------------------------------------------------------------------------------------------------------------

class SessionLink
{
  public:
    SessionLink();
    ~SessionLink();

    // 📝 Opens the session on the chosen relay and greets the peers. Returns false when the link is already open.
    bool Start(WorldSpace& World, const SessionRouterSettings& Router, NetworkRole Role, const std::string& PeerName = "Frontier");
    // 📝 Steps the loop: a host replicates and exchanges, a client exchanges and absorbs. Rate-limited to ReplicateHz.
    void Integrate(WorldSpace& World, double DeltaSeconds);
    // 📝 Farewells the peers and closes the relay.
    void Stop(WorldSpace& World);

    [[nodiscard]] bool                 QueryLinked() const noexcept { return Extension_ != nullptr; }
    [[nodiscard]] const SessionLinkStatus& QueryStatus() const noexcept { return Status_; }
    [[nodiscard]] SessionTransportRouter&  AccessRouter() noexcept { return Router_; }
    [[nodiscard]] NetworkingSpace&         AccessSpace() noexcept { return Space_; }

  private:
    NetworkingSpace                       Space_{};
    SessionTransportRouter                Router_{};
    std::unique_ptr<NetworkingExtension>  Extension_{};
    SessionLinkStatus                     Status_{};
    double                                ReplicateHz   = 20.0;   // [Hz] the host replicate / exchange rate
    double                                Accumulator_  = 0.0;    // [s] time since the last exchange
};

} // namespace Frontier
