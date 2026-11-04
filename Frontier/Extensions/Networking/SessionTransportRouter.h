/*==========================================================================================================================================
                                                        SESSIONTRANSPORTROUTER.H
==========================================================================================================================================*/
// 🧩 Picks the relay a session rides, reusing Project-Networking's selection rule (premium access and a linked Photon go to
//    Photon, everyone else to the Epic direct path) and falling back to the in-process loopback whenever the chosen provider
//    cannot carry packets both ways. The loopback always works, so a session link is never dead; the provider relays activate
//    the moment their SDK receive path is wired. One router serves every workspace.

#pragma once

#include "Frontier/Extensions/Networking/ProviderTransport.h"

#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

struct SessionRouterSettings
{
    bool        PremiumAccess = false;   // [-] premium multiplayer routes to Photon when it is linked
    std::string IdentityToken{};         // [-] the EOS identity token Photon authenticates with; never logged
    std::string RoomName{};              // [-] the Photon room; empty selects the facade's dev room
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    SESSION TRANSPORT ROUTER
//------------------------------------------------------------------------------------------------------------------------

class SessionTransportRouter
{
  public:
    // 📝 Starts the chosen relay and returns it. The reference stays valid for the session (the router owns all three relays).
    NetworkTransport& Select(const SessionRouterSettings& Settings);
    void              Stop();

    [[nodiscard]] SessionRelay        QueryRelay() const noexcept { return Active_; }
    [[nodiscard]] const std::string&  QueryReading() const noexcept { return Reading_; }
    [[nodiscard]] EosTransportAdapter&    AccessEpic() noexcept { return Epic_; }
    [[nodiscard]] PhotonTransportAdapter& AccessPhoton() noexcept { return Photon_; }
    [[nodiscard]] LoopbackTransport&      AccessLoopback() noexcept { return Loopback_; }

  private:
    LoopbackTransport      Loopback_{};
    EosTransportAdapter    Epic_{};
    PhotonTransportAdapter Photon_{};
    SessionRelay           Active_ = SessionRelay::Loopback;
    std::string            Reading_{};
};

} // namespace Frontier
