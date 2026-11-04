/*==========================================================================================================================================
                                                             SESSIONLINK.CPP
==========================================================================================================================================*/
// 🧩 The session loop: start selects the relay and greets, integrate replicates/exchanges/absorbs at a fixed rate, stop farewells.
//    The status carries the relay reading and the tallies so the workspace can show what the link is doing.

#include "Frontier/Extensions/Networking/SessionLink.h"

#include "Frontier/Engine/Scene/WorldSpace.h"

namespace Frontier
{

SessionLink::SessionLink() = default;

SessionLink::~SessionLink()
{
    if (Extension_) Router_.Stop();
}

bool SessionLink::Start(WorldSpace& World, const SessionRouterSettings& Router, NetworkRole Role, const std::string& PeerName)
{
    if (Extension_) return false;
    Space_ = NetworkingSpace{};
    Space_.ConfigureLocalPeer(PeerName, Role);
    NetworkTransport& Transport = Router_.Select(Router);
    Extension_ = std::make_unique<NetworkingExtension>(Space_, Transport);
    Extension_->Greet();
    Extension_->Exchange();
    Accumulator_ = 0.0;
    Status_      = {};
    Status_.Linked  = true;
    Status_.Relay   = Router_.QueryRelay();
    Status_.Reading = Router_.QueryReading();
    Status_.Peers   = Space_.QueryPeers().size();
    return true;
}

void SessionLink::Integrate(WorldSpace& World, double DeltaSeconds)
{
    if (!Extension_) return;
    Accumulator_ += DeltaSeconds;
    const double Interval = 1.0 / ReplicateHz;
    if (Accumulator_ < Interval) return;
    Accumulator_ = 0.0;
    if (Space_.QueryHosting())
    {
        Status_.Posted = Extension_->Replicate(World);
        Extension_->Exchange();
    }
    else
    {
        Extension_->Exchange();
        Status_.Applied = Extension_->Absorb(World);
    }
    const NetworkTally& Tally = Space_.QueryTally();
    Status_.MessagesSent     = Tally.MessagesSent;
    Status_.MessagesReceived = Tally.MessagesReceived;
    Status_.Peers            = Space_.QueryPeers().size();
    Status_.Relay            = Router_.QueryRelay();
    Status_.Reading          = Router_.QueryReading();
}

void SessionLink::Stop(WorldSpace& World)
{
    if (!Extension_) return;
    Space_.Post(NetworkTopic::Farewell, {});
    Extension_->Exchange();
    Extension_.reset();
    Router_.Stop();
    Status_         = {};
    Status_.Linked  = false;
}

} // namespace Frontier
