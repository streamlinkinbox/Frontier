//============================================================================================================================================
//                                                          REPLICATIONLINK.H
//============================================================================================================================================
// 📦 One carrier-agnostic seam for replication traffic. The replication sequence above it never names
// Photon, Epic Online Services or PlayFab: it opens a link, delivers codec packets into it, and is
// handed arrivals back. Each carrier implements this and nothing else, so adding one is a new .cpp
// rather than a change anywhere else.
//
// Photon Realtime is the carrier in use. The Epic and PlayFab links are present and honest: they
// report themselves unavailable and refuse to open rather than pretending to carry traffic. The
// loopback link carries traffic entirely in process and is what the proofs run on — no SDK, no socket.
//
// Nothing here logs a credential. A specification's token is passed to the carrier and forgotten.

#pragma once

#include "EpicExchange.h"
#include "TransportCodec.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Networking
{

// The four carriers the seam is written against. The order is the preference order the router walks
//    when a specification does not name one outright.
enum class ReplicationCarrier : std::uint8_t
{
    Loopback = 0,   // in-process; no SDK, no socket, used by the proofs and by single-player hosting
    Photon,         // Photon Realtime — the carrier this slice ships on
    EpicOnline,     // Epic Online Services P2P — seam only, refuses to open
    PlayFab,        // PlayFab Party — seam only, refuses to open
    Count
};

const char* CarrierName(ReplicationCarrier Carrier) noexcept;

// How far a link has got. A refused link states why in its reading and never retries by itself.
enum class LinkStanding : std::uint8_t
{
    Dormant = 0,    // constructed, never opened
    Opening,        // handshake or room join in flight
    Open,           // packets may flow
    Refused,        // the last open failed; the reading says why, redacted
    Closed          // was open, then closed or dropped
};

const char* StandingName(LinkStanding Standing) noexcept;

// Everything a link will say about itself. Counters are cumulative across one open.
struct LinkReading
{
    LinkStanding       Standing   = LinkStanding::Dormant;
    ReplicationCarrier Carrier    = ReplicationCarrier::Loopback;
    std::uint32_t      LocalSlot  = 0;      // [-] this peer's room slot, 1-based; 0 while unassigned
    std::uint32_t      Occupancy  = 0;      // [-] peers in the room, this one included
    bool               Arbiter    = false;  // this peer owns every placement nobody else claims
    std::uint64_t      PacketsSent = 0, PacketsReceived = 0;
    std::uint64_t      BytesSent   = 0, BytesReceived   = 0;
    std::uint32_t      Refusals    = 0;     // [-] packets the carrier or the codec turned away
    char               Reading[192]{};
};

// What a link needs to open. Null selects the carrier's own built-in, which is what the dev room uses.
struct LinkSpecification
{
    const char* Room            = nullptr;  // room or session name
    const char* Credential      = nullptr;  // EOS id token, PlayFab entity token — never logged
    const char* Ownership       = nullptr;  // entitlement token, or null while dormant
    const char* ApplicationId   = nullptr;
    const char* ApplicationTag  = nullptr;  // the carrier's app version string
    bool        ArbiterWanted   = true;     // claim arbitration when the room has no arbiter yet
};

// One packet that arrived. Payload points into the carrier's own bytes and is valid only for the
//    duration of the reception call — anything kept must be copied.
struct ArrivedPacket
{
    std::uint32_t       Slot    = 0;        // [-] the sender's room slot
    ReplicationKind     Carriage = ReplicationKind::Probe;
    const std::uint8_t* Payload = nullptr;
    std::size_t         Length  = 0;
};

using PacketReception = void (*)(void* Attendant, const ArrivedPacket&);

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE LINK ITSELF
//------------------------------------------------------------------------------------------------------------------------

class ReplicationLink
{
public:
    virtual ~ReplicationLink() = default;

    virtual ReplicationCarrier Carrier() const noexcept = 0;

    /// 📦 True when this carrier can actually run here — its SDK is linked and its room is reachable.
    /// note  An unavailable carrier still constructs, so the router can read its refusal rather than
    ///       branch on a compile-time switch.
    virtual bool Available() const noexcept = 0;

    virtual bool Open(const LinkSpecification& Specification, DiagnosticReception Reception) noexcept = 0;

    /// 📦 Pumps the carrier once. Arrivals are passed to the attending reception before this returns.
    virtual void Advance() noexcept = 0;

    virtual void Close() noexcept = 0;

    /// 📦 Frames one payload through PacketCodec and hands it to the carrier.
    /// out   bool [-] false when the link is not open, the payload is oversized, or the carrier refused
    virtual bool Deliver(ReplicationKind Carriage, const std::uint8_t* Payload, std::size_t Length,
                         bool Reliable) noexcept = 0;

    virtual LinkReading Inspect() const noexcept = 0;

    /// 📦 Who hears arrivals. One attendant at a time; passing nullptr detaches.
    void AttendWith(PacketReception Reception, void* Attendant) noexcept
    {
        Reception_ = Reception;
        Attendant_ = Attendant;
    }

protected:
    void Announce(const ArrivedPacket& Packet) noexcept
    {
        if (Reception_ != nullptr) Reception_(Attendant_, Packet);
    }

    PacketReception Reception_ = nullptr;
    void*           Attendant_ = nullptr;
};

/// 📦 Constructs the link for one carrier. Never null — an unknown carrier answers with the loopback.
std::unique_ptr<ReplicationLink> OpenReplicationLink(ReplicationCarrier Carrier) noexcept;

/// 📦 The first carrier in preference order that is actually available here.
/// in    Wanted [-] the carrier the caller would rather have
/// out   ReplicationCarrier [-] Wanted when it is available, otherwise Photon, otherwise Loopback
ReplicationCarrier ResolveCarrier(ReplicationCarrier Wanted) noexcept;

}   // namespace Networking
