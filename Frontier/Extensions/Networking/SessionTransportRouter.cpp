/*==========================================================================================================================================
                                                       SESSIONTRANSPORTROUTER.CPP
==========================================================================================================================================*/
// 🧩 The relay selection: the premium rule chooses Photon or Epic, and a provider that cannot carry packets hands the session
//    back to the loopback. The reading records why, so the viewport notice and the log tell the truth about the active relay.

#include "Frontier/Extensions/Networking/SessionTransportRouter.h"

namespace Frontier
{

NetworkTransport& SessionTransportRouter::Select(const SessionRouterSettings& Settings)
{
    Stop();
    // 📝 Project-Networking's rule: premium access and a linked Photon go to Photon; everyone else to the Epic direct path.
    const bool WantPhoton = Settings.PremiumAccess && Photon_.QueryAvailable();
    if (WantPhoton && Photon_.Start(Settings.IdentityToken, Settings.RoomName))
    {
        Active_  = SessionRelay::Photon;
        Reading_ = "relay=photon " + Photon_.QueryReading();
        return Photon_;
    }
    if (!WantPhoton && Epic_.QueryAvailable() && Epic_.Start())
    {
        Active_  = SessionRelay::Epic;
        Reading_ = "relay=epic " + Epic_.QueryReading();
        return Epic_;
    }
    // 📝 The chosen provider cannot carry replication yet (EOS relay unimplemented, Photon unlinked): the loopback keeps the
    //    session alive in-process. The reading names the provider that was wanted and why it fell back.
    Active_  = SessionRelay::Loopback;
    Reading_ = std::string("relay=loopback fallback=") + (WantPhoton ? "photon" : "epic") + " (provider relay not carrying packets yet)";
    return Loopback_;
}

void SessionTransportRouter::Stop()
{
    Photon_.Stop();
    Epic_.Stop();
    Active_  = SessionRelay::Loopback;
    Reading_.clear();
}

} // namespace Frontier
