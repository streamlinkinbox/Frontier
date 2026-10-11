//============================================================================================================================================
//                                                        REPLICATIONSEQUENCE.H
//============================================================================================================================================
// 📦 The replication runtime: what is replicated, who owns it, what changed, and what a remote peer
// sees while the next snapshot is in flight. It speaks only to ReplicationLink, so the same sequence
// runs over Photon, over the in-process loopback, and over Epic Online Services or PlayFab the day
// those links open.
//
// The shape of one cycle of Advance():
//
//    1. pump the link, which hands back arrivals
//    2. apply arrivals to placements this peer does not own, holding two samples of each
//    3. ease those placements toward the newer sample, a smoothing delay behind the newest arrival
//    4. once the send interval is up, gather the owned placements whose properties moved past their
//       quantum and deliver one snapshot
//
// Authority is per placement, not per peer: a peer may own some placements in a room and watch the
// rest. A snapshot for a placement this peer owns is refused, so two peers cannot fight over one.

#pragma once

#include "ReplicatedPlacement.h"
#include "ReplicationLink.h"
#include <cstdint>
#include <vector>

namespace Networking
{

// A placement's identity across the room. The simulation assigns it and it must agree on every peer;
//    a scene record's own stable id is the usual source.
using NetworkPlacement = std::uint32_t;

constexpr NetworkPlacement NoNetworkPlacement = 0u;

// Why a snapshot was turned away. Counted rather than thrown: a hostile or stale peer must not be
//    able to interrupt the room, and the counts are what the diagnostics panel reads.
struct ReplicationRefusals
{
    std::uint32_t Malformed   = 0;  // truncated, or a length that does not match the body
    std::uint32_t Unknown     = 0;  // a placement this peer has not enrolled
    std::uint32_t Mismatched  = 0;  // enrolled, but built from a different property order
    std::uint32_t Unauthorised = 0; // a peer sending for a placement it does not own
    std::uint32_t Stale       = 0;  // older than a sample already applied
};

struct ReplicationReading
{
    std::uint32_t Enrolled   = 0;
    std::uint32_t Owned      = 0;
    std::uint32_t Watched    = 0;
    std::uint64_t SnapshotsDelivered = 0;
    std::uint64_t SnapshotsTaken     = 0;
    std::uint64_t PropertiesDelivered = 0;
    std::uint32_t Moment     = 0;   // [ms] since the sequence was attended
    ReplicationRefusals Refused;
};

class ReplicationSequence
{
public:
    ReplicationSequence() = default;
    ~ReplicationSequence();

    ReplicationSequence(const ReplicationSequence&) = delete;
    ReplicationSequence& operator=(const ReplicationSequence&) = delete;

    /// 📦 Seats this sequence on a link. The link must already be open, or opened before the first
    ///    Advance(); the sequence does not open it, because who pays for the room is not its business.
    void Attend(ReplicationLink& Link, DiagnosticReception Reception = nullptr) noexcept;
    void Detach() noexcept;

    /// 📦 Enrols one placement.
    /// in    Placement   [-] the room-wide identity; must be the same number on every peer
    /// in    Description [-] the property run; must be the same shape on every peer
    /// in    Reading     [-] the simulation's own bytes — not copied, not owned, must outlive the enrolment
    /// in    Owned       [-] true when this peer has authority over it
    /// out   bool        [-] false when the identity is already enrolled or the arguments do not agree
    bool Enrol(NetworkPlacement Placement, const ReplicatedDescription& Description, void* Reading,
               bool Owned) noexcept;

    bool Retire(NetworkPlacement Placement) noexcept;

    /// 📦 Moves authority. A peer that takes authority stops easing and starts sending; a peer that
    ///    gives it up keeps its last reading until the new owner's first snapshot lands.
    bool Adopt(NetworkPlacement Placement, bool Owned) noexcept;

    /// 📦 One cycle. Seconds is the simulation's own step, not wall clock, so a paused host does not
    ///    drift its remote placements forward.
    void Advance(double Seconds) noexcept;

    /// 📦 Sends every owned placement in full on the next Advance, whatever moved. A peer that has
    ///    just joined asks the room for this, and the arbiter answers it.
    void RenewInFull() noexcept { Renewing_ = true; }

    void AssignSendRate(double Hertz) noexcept;        // default 30
    void AssignSmoothing(double Seconds) noexcept;     // default 0.1
    double SendRate() const noexcept { return Interval_ > 0.0 ? 1.0 / Interval_ : 0.0; }
    double Smoothing() const noexcept { return Smoothing_; }

    ReplicationReading Inspect() const noexcept;

    /// 📦 Encodes the owned placements into one snapshot body, without a link. Exposed so the proofs
    ///    can read the wire shape directly, and so a recorded session can be written to a file.
    bool Gather(std::vector<std::uint8_t>& Body, bool InFull) noexcept;

    /// 📦 Applies one snapshot body as if it had arrived from Slot. Exposed for the same reason.
    bool Apply(std::uint32_t Slot, const std::uint8_t* Body, std::size_t Length) noexcept;

private:
    struct Enrolment
    {
        NetworkPlacement      Placement = NoNetworkPlacement;
        ReplicatedDescription Description;
        std::uint16_t         Signature = 0;
        std::uint8_t*         Reading   = nullptr;   // the simulation's bytes, borrowed
        bool                  Owned     = false;
        std::uint32_t         Authority = 0;         // the slot last seen sending for it

        std::vector<std::uint8_t> Shadow;            // what was last delivered, for change detection
        std::vector<std::uint8_t> Earlier, Later;    // the two samples a watcher eases between
        std::uint32_t             EarlierAt = 0, LaterAt = 0;   // [ms]
        bool                      Sampled   = false;
    };

    Enrolment* Find(NetworkPlacement Placement) noexcept;
    void Ease(Enrolment& Held, std::uint32_t RenderAt) noexcept;
    static void Receive(void* Attendant, const ArrivedPacket& Packet) noexcept;

    ReplicationLink*       Link_ = nullptr;
    DiagnosticReception    Reception_ = nullptr;
    std::vector<Enrolment> Enrolled_;
    std::vector<std::uint8_t> Body_;

    double        Elapsed_   = 0.0;     // [s] since attended
    double        SinceSend_ = 0.0;     // [s]
    double        Interval_  = 1.0 / 30.0;
    double        Smoothing_ = 0.1;
    bool          Renewing_  = true;    // the first snapshot after attending is always a full one

    std::uint64_t Delivered_ = 0, Taken_ = 0, PropertiesOut_ = 0;
    ReplicationRefusals Refused_;
};

}   // namespace Networking
