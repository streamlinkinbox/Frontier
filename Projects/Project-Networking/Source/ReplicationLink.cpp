//============================================================================================================================================
//                                                         REPLICATIONLINK.CPP
//============================================================================================================================================
// 📦 The loopback carrier, the two carriers that are seams only, and the factory that chooses between
// them. The Photon carrier lives in PhotonReplicationLink.cpp beside this.
//
// Loopback is a real carrier, not a mock: packets are framed by the same PacketCodec, counted the same
// way, assigned room slots the same way and delivered to every other peer in the same process. That is
// what makes it worth running the proofs on — the replication sequence cannot tell it apart from a
// carrier with a cloud behind it except by the name it reports.

#include "ReplicationLink.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Networking
{
namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE IN-PROCESS ROOM
//------------------------------------------------------------------------------------------------------------------------

class LoopbackLink;

// Every loopback link opened in this process shares one room, exactly as every client of a cloud room
//    shares one room. Slots are handed out 1-based and reused once a peer closes.
struct LoopbackRoom
{
    std::vector<LoopbackLink*> Occupants;
    char                       Name[96]{};

    static LoopbackRoom& Shared() noexcept
    {
        static LoopbackRoom Room;
        return Room;
    }
};

class LoopbackLink final : public ReplicationLink
{
public:
    ~LoopbackLink() override { Close(); }

    ReplicationCarrier Carrier() const noexcept override { return ReplicationCarrier::Loopback; }
    bool Available() const noexcept override { return true; }

    bool Open(const LinkSpecification& Specification, DiagnosticReception Reception) noexcept override
    {
        Close();
        Reception_Log = Reception;
        LoopbackRoom& Room = LoopbackRoom::Shared();
        if (Room.Occupants.empty())
            std::snprintf(Room.Name, sizeof(Room.Name), "%s",
                          Specification.Room != nullptr ? Specification.Room : "frontier-loopback");
        Room.Occupants.push_back(this);
        Slot_ = static_cast<std::uint32_t>(Room.Occupants.size());
        // The first peer in the room arbitrates; later peers only do so if they were asked to and the
        //    room is still without one, which cannot happen here because the first peer never leaves
        //    without handing the room on.
        Arbiter_ = Specification.ArbiterWanted && Slot_ == 1;
        Standing_ = LinkStanding::Open;
        std::snprintf(Note_, sizeof(Note_), "loopback room=%s slot=%u arbiter=%d", Room.Name, Slot_,
                      Arbiter_ ? 1 : 0);
        if (Reception_Log != nullptr) Reception_Log(Note_);
        return true;
    }

    void Advance() noexcept override
    {
        // Arrivals are delivered the moment they are raised, so a pump has nothing left to do beyond
        //    keeping the occupancy reading current.
    }

    void Close() noexcept override
    {
        if (Standing_ != LinkStanding::Open) return;
        LoopbackRoom& Room = LoopbackRoom::Shared();
        Room.Occupants.erase(std::remove(Room.Occupants.begin(), Room.Occupants.end(), this),
                             Room.Occupants.end());
        // Slots close up behind a departing peer, and the room hands arbitration to whoever is first.
        for (std::size_t At = 0; At < Room.Occupants.size(); ++At)
        {
            Room.Occupants[At]->Slot_ = static_cast<std::uint32_t>(At + 1);
            Room.Occupants[At]->Arbiter_ = (At == 0);
        }
        Standing_ = LinkStanding::Closed;
        std::snprintf(Note_, sizeof(Note_), "loopback closed");
    }

    bool Deliver(ReplicationKind Carriage, const std::uint8_t* Payload, std::size_t Length,
                 bool /*Reliable*/) noexcept override
    {
        if (Standing_ != LinkStanding::Open) { ++Refusals_; return false; }
        if (!PacketCodec::Encode(Carriage, Payload, Length, Frame_)) { ++Refusals_; return false; }
        ++Sent_;
        Written_ += Frame_.size();

        ArrivedPacket Arrival;
        Arrival.Slot = Slot_;
        for (LoopbackLink* Peer : LoopbackRoom::Shared().Occupants)
        {
            if (Peer == this) continue;
            ReplicationKind       Decoded = ReplicationKind::Probe;
            const std::uint8_t*   Body    = nullptr;
            std::size_t           Span    = 0;
            if (!PacketCodec::Decode(Frame_.data(), Frame_.size(), Decoded, Body, Span))
            {
                ++Peer->Refusals_;
                continue;
            }
            ++Peer->Read_;
            Peer->Taken_ += Frame_.size();
            Arrival.Carriage = Decoded;
            Arrival.Payload  = Body;
            Arrival.Length   = Span;
            Peer->Announce(Arrival);
        }
        return true;
    }

    LinkReading Inspect() const noexcept override
    {
        LinkReading Reading;
        Reading.Standing        = Standing_;
        Reading.Carrier         = ReplicationCarrier::Loopback;
        Reading.LocalSlot       = Slot_;
        Reading.Occupancy       = static_cast<std::uint32_t>(LoopbackRoom::Shared().Occupants.size());
        Reading.Arbiter         = Arbiter_;
        Reading.PacketsSent     = Sent_;
        Reading.PacketsReceived = Read_;
        Reading.BytesSent       = Written_;
        Reading.BytesReceived   = Taken_;
        Reading.Refusals        = Refusals_;
        std::snprintf(Reading.Reading, sizeof(Reading.Reading), "%s", Note_);
        return Reading;
    }

private:
    DiagnosticReception       Reception_Log = nullptr;
    LinkStanding              Standing_ = LinkStanding::Dormant;
    std::uint32_t             Slot_ = 0;
    bool                      Arbiter_ = false;
    std::uint64_t             Sent_ = 0, Read_ = 0, Written_ = 0, Taken_ = 0;
    std::uint32_t             Refusals_ = 0;
    char                      Note_[192]{};
    std::vector<std::uint8_t> Frame_;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CARRIERS THAT ARE SEAMS
//------------------------------------------------------------------------------------------------------------------------
// Epic Online Services has a P2P interface and PlayFab has Party; neither is linked in this slice.
//    They are written as links rather than left out so the sequence above can be pointed at them the
//    day their SDKs land, and so a build that selects one fails honestly instead of silently going
//    quiet. Refusing to open is the whole implementation, deliberately.

class AbsentLink final : public ReplicationLink
{
public:
    AbsentLink(ReplicationCarrier Carrier, const char* Reason) noexcept
        : Carrier_(Carrier), Reason_(Reason) {}

    ReplicationCarrier Carrier() const noexcept override { return Carrier_; }
    bool Available() const noexcept override { return false; }

    bool Open(const LinkSpecification& /*Specification*/, DiagnosticReception Reception) noexcept override
    {
        Standing_ = LinkStanding::Refused;
        std::snprintf(Note_, sizeof(Note_), "%s=unavailable %s", CarrierName(Carrier_), Reason_);
        if (Reception != nullptr) Reception(Note_);
        return false;
    }

    void Advance() noexcept override {}
    void Close() noexcept override { Standing_ = LinkStanding::Closed; }

    bool Deliver(ReplicationKind, const std::uint8_t*, std::size_t, bool) noexcept override
    {
        ++Refusals_;
        return false;
    }

    LinkReading Inspect() const noexcept override
    {
        LinkReading Reading;
        Reading.Standing = Standing_;
        Reading.Carrier  = Carrier_;
        Reading.Refusals = Refusals_;
        std::snprintf(Reading.Reading, sizeof(Reading.Reading), "%s: %s", CarrierName(Carrier_), Reason_);
        return Reading;
    }

private:
    ReplicationCarrier Carrier_;
    const char*        Reason_;
    LinkStanding       Standing_ = LinkStanding::Dormant;
    std::uint32_t      Refusals_ = 0;
    char               Note_[192]{};
};

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        NAMES AND CHOICE
//------------------------------------------------------------------------------------------------------------------------

const char* CarrierName(ReplicationCarrier Carrier) noexcept
{
    switch (Carrier)
    {
    case ReplicationCarrier::Loopback:   return "loopback";
    case ReplicationCarrier::Photon:     return "photon";
    case ReplicationCarrier::EpicOnline: return "epic-online-services";
    case ReplicationCarrier::PlayFab:    return "playfab-party";
    default:                             return "unknown";
    }
}

const char* StandingName(LinkStanding Standing) noexcept
{
    switch (Standing)
    {
    case LinkStanding::Dormant: return "dormant";
    case LinkStanding::Opening: return "opening";
    case LinkStanding::Open:    return "open";
    case LinkStanding::Refused: return "refused";
    default:                    return "closed";
    }
}

std::unique_ptr<ReplicationLink> OpenPhotonReplicationLink() noexcept;   // PhotonReplicationLink.cpp

std::unique_ptr<ReplicationLink> OpenReplicationLink(ReplicationCarrier Carrier) noexcept
{
    switch (Carrier)
    {
    case ReplicationCarrier::Photon:
        return OpenPhotonReplicationLink();
    case ReplicationCarrier::EpicOnline:
        return std::make_unique<AbsentLink>(ReplicationCarrier::EpicOnline,
                                            "p2p_relay_not_linked=1; lobby and sessions still run on EOS");
    case ReplicationCarrier::PlayFab:
        return std::make_unique<AbsentLink>(ReplicationCarrier::PlayFab,
                                            "party_sdk_not_linked=1; no PlayFab title configured");
    default:
        return std::make_unique<LoopbackLink>();
    }
}

ReplicationCarrier ResolveCarrier(ReplicationCarrier Wanted) noexcept
{
    if (OpenReplicationLink(Wanted)->Available()) return Wanted;
    if (Wanted != ReplicationCarrier::Photon && OpenReplicationLink(ReplicationCarrier::Photon)->Available())
        return ReplicationCarrier::Photon;
    return ReplicationCarrier::Loopback;
}

}   // namespace Networking
