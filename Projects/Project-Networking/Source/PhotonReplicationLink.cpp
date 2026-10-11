//============================================================================================================================================
//                                                      PHOTONREPLICATIONLINK.CPP
//============================================================================================================================================
// 📦 The Photon Realtime carrier behind the replication link. It owns nothing: the connection, the
// room join and the raise-event call all belong to PhotonTransport, which already exists and is
// already exercised by TransportChecks. This file is the adaptation between the two vocabularies —
// Photon's room slots and master client become the link's slots and arbiter.
//
// It compiles against PhotonTransport.h only, never against a Photon header, so it links the same way
// whether the real SDK or PhotonLinkStub.cpp is in the build. When the stub is linked the carrier
// reports itself unavailable and the router keeps players on the loopback or the EOS path.

#include "PhotonTransport.h"
#include "ReplicationLink.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace Networking
{
namespace
{

class PhotonLink final : public ReplicationLink
{
public:
    ~PhotonLink() override { Close(); }

    ReplicationCarrier Carrier() const noexcept override { return ReplicationCarrier::Photon; }
    bool Available() const noexcept override { return PhotonLinkAvailable(); }

    bool Open(const LinkSpecification& Specification, DiagnosticReception Reception) noexcept override
    {
        Close();
        PhotonCredentials Credentials;
        Credentials.AppId                = Specification.ApplicationId;
        Credentials.AppVersion           = Specification.ApplicationTag;
        Credentials.IdTokenJwt           = Specification.Credential;
        Credentials.OwnershipTokenOrNull = Specification.Ownership;
        Credentials.RoomName             = Specification.Room;

        // The attendant is seated before the connection starts: Photon can deliver a packet inside
        //    the very first pump after the join returns, and a dropped first snapshot would leave a
        //    late peer looking at an empty scene until the next keyframe.
        AttendPhotonPackets(&PhotonLink::Carry, this);
        Opened_ = StartPhotonTransport(Credentials, Reception);
        if (!Opened_) AttendPhotonPackets(nullptr, nullptr);
        return Opened_;
    }

    void Advance() noexcept override
    {
        if (Opened_) TickPhotonTransport();
    }

    void Close() noexcept override
    {
        if (!Opened_) return;
        AttendPhotonPackets(nullptr, nullptr);
        StopPhotonTransport();
        Opened_ = false;
    }

    bool Deliver(ReplicationKind Carriage, const std::uint8_t* Payload, std::size_t Length,
                 bool Reliable) noexcept override
    {
        if (!Opened_) { ++Refusals_; return false; }
        if (!PacketCodec::Encode(Carriage, Payload, Length, Frame_)) { ++Refusals_; return false; }
        if (!SendPhotonPacket(Frame_.data(), Frame_.size(), Reliable)) { ++Refusals_; return false; }
        Written_ += Frame_.size();
        return true;
    }

    LinkReading Inspect() const noexcept override
    {
        const PhotonStatus      Status = InspectPhotonStatus();
        const PhotonRoomReading Room   = InspectPhotonRoom();

        LinkReading Reading;
        Reading.Carrier         = ReplicationCarrier::Photon;
        Reading.Standing        = Translate(Status.State);
        Reading.LocalSlot       = Room.LocalSlot > 0 ? static_cast<std::uint32_t>(Room.LocalSlot) : 0u;
        Reading.Occupancy       = Room.Occupancy > 0 ? static_cast<std::uint32_t>(Room.Occupancy) : 0u;
        Reading.Arbiter         = Room.MasterClient;
        Reading.PacketsSent     = Status.PacketsSent;
        Reading.PacketsReceived = Status.PacketsReceived;
        Reading.BytesSent       = Written_;
        Reading.BytesReceived   = Taken_;
        Reading.Refusals        = Refusals_;
        // Photon's reading is the wider of the two, so it is clipped rather than truncated by
        //    snprintf's own rules; the cause code at its head is what a reader needs.
        std::snprintf(Reading.Reading, sizeof(Reading.Reading), "%.*s",
                      int(sizeof(Reading.Reading) - 1), Status.Reading);
        return Reading;
    }

private:
    // Photon's seven states collapse onto the link's five: everything before a room is Opening,
    //    because nothing above this can usefully act on the difference between a cloud handshake and
    //    a room join, and both recover the same way.
    static LinkStanding Translate(PhotonState State) noexcept
    {
        switch (State)
        {
        case PhotonState::Joined:       return LinkStanding::Open;
        case PhotonState::Connecting:
        case PhotonState::Joining:      return LinkStanding::Opening;
        case PhotonState::Failed:
        case PhotonState::Unlinked:     return LinkStanding::Refused;
        case PhotonState::Disconnected: return LinkStanding::Closed;
        default:                        return LinkStanding::Dormant;
        }
    }

    static void Carry(void* Attendant, int Slot, std::uint8_t Carriage, const std::uint8_t* Payload,
                      std::size_t Length) noexcept
    {
        auto* Link = static_cast<PhotonLink*>(Attendant);
        if (Link == nullptr) return;
        if (!PacketCodec::ValidKind(Carriage)) { ++Link->Refusals_; return; }
        Link->Taken_ += PacketCodec::HeaderLength + Length;

        ArrivedPacket Arrival;
        Arrival.Slot     = Slot > 0 ? static_cast<std::uint32_t>(Slot) : 0u;
        Arrival.Carriage = static_cast<ReplicationKind>(Carriage);
        Arrival.Payload  = Payload;
        Arrival.Length   = Length;
        Link->Announce(Arrival);
    }

    bool                      Opened_ = false;
    std::uint64_t             Written_ = 0, Taken_ = 0;
    std::uint32_t             Refusals_ = 0;
    std::vector<std::uint8_t> Frame_;
};

}   // namespace

std::unique_ptr<ReplicationLink> OpenPhotonReplicationLink() noexcept
{
    return std::make_unique<PhotonLink>();
}

}   // namespace Networking
