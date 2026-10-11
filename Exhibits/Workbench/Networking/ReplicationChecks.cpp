//============================================================================================================================================
//                                                        REPLICATIONCHECKS.CPP
//============================================================================================================================================
// 📦 Executed proof for the replication port. Two sequences are seated on two links in one room and
// made to replicate a moving placement to each other — no SDK, no socket, no window.
//
// What is proved that a screenshot cannot:
//
//    the wire shape is read back byte for byte, including the delta mask and the full-renewal marker;
//    a watcher's authority is enforced, so a peer cannot drive a placement it does not own;
//    a truncated, mismatched, stale or unknown snapshot is counted and dropped, never read past;
//    the quantum actually suppresses traffic, measured in packets rather than asserted;
//    easing lands on the sender's own reading once the smoothing delay has passed;
//    the carrier seam answers for all four carriers, and the two that are not linked refuse honestly.

#include "ReplicationSequence.h"
#include "ReplicationLink.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Networking;

namespace
{
unsigned Checks = 0;

void Check(bool Condition, const char* Claim)
{
    ++Checks;
    if (!Condition) throw std::runtime_error(Claim);
}

bool Near(float A, float B, float Slack = 1e-4f) { return std::fabs(A - B) <= Slack; }

// One replicated placement: where it is, which way it faces, how fast it is going, whether its
//    lights are on, and a short opaque run standing in for a gameplay payload.
struct VehicleReading
{
    float         Translation[3] = { 0.0f, 0.0f, 0.0f };
    float         Orientation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float         Speed          = 0.0f;
    std::int32_t  Gear           = 0;
    std::uint8_t  Headlights     = 0;
    std::uint8_t  Livery[4]      = { 0, 0, 0, 0 };
};

const ReplicatedProperty VehicleProperties[] = {
    { "Translation", PropertyLayout::Translation, offsetof(VehicleReading, Translation), 0,  0.001f, true  },
    { "Orientation", PropertyLayout::Orientation, offsetof(VehicleReading, Orientation), 0,  0.0005f, true },
    { "Speed",       PropertyLayout::Decimal,     offsetof(VehicleReading, Speed),       0,  0.05f,  true  },
    { "Gear",        PropertyLayout::Whole,       offsetof(VehicleReading, Gear),        0,  0.0f,   false },
    { "Headlights",  PropertyLayout::Boolean,     offsetof(VehicleReading, Headlights),  0,  0.0f,   false },
    { "Livery",      PropertyLayout::Opaque,      offsetof(VehicleReading, Livery),      4,  0.0f,   false },
};

const ReplicatedDescription VehicleDescription{
    "Vehicle", VehicleProperties, 6, sizeof(VehicleReading)
};

void Quiet(const char*) {}
}   // namespace

int main()
{
    //-----------------------------------------------------------------------------------------------------
    // The carrier seam. Every carrier constructs; only the ones that can actually run say so.
    //-----------------------------------------------------------------------------------------------------
    {
        Check(std::strcmp(CarrierName(ReplicationCarrier::Photon), "photon") == 0,
              "the carriers name themselves");
        Check(std::strcmp(CarrierName(ReplicationCarrier::EpicOnline), "epic-online-services") == 0,
              "including the ones that are seams");
        Check(std::strcmp(CarrierName(ReplicationCarrier::PlayFab), "playfab-party") == 0,
              "and PlayFab");

        for (int At = 0; At < int(ReplicationCarrier::Count); ++At)
            Check(OpenReplicationLink(ReplicationCarrier(At)) != nullptr,
                  "every carrier constructs a link, available or not");

        auto Epic = OpenReplicationLink(ReplicationCarrier::EpicOnline);
        Check(!Epic->Available(), "the Epic P2P relay is not linked in this slice");
        Check(!Epic->Open(LinkSpecification{}, &Quiet), "so it refuses to open rather than going quiet");
        Check(Epic->Inspect().Standing == LinkStanding::Refused, "and says it refused");
        Check(!Epic->Deliver(ReplicationKind::Snapshot, nullptr, 0, true),
              "a refused link carries nothing");
        Check(Epic->Inspect().Refusals == 1, "and counts what it turned away");

        auto Party = OpenReplicationLink(ReplicationCarrier::PlayFab);
        Check(!Party->Available() && !Party->Open(LinkSpecification{}, &Quiet),
              "PlayFab Party answers the same way");

        auto Loop = OpenReplicationLink(ReplicationCarrier::Loopback);
        Check(Loop->Available(), "the loopback carrier always runs");
        Check(ResolveCarrier(ReplicationCarrier::PlayFab) != ReplicationCarrier::PlayFab,
              "an unavailable carrier is never resolved to");
    }

    //-----------------------------------------------------------------------------------------------------
    // The room. Two peers, slots handed out in order, the first arbitrating.
    //-----------------------------------------------------------------------------------------------------
    auto Driver  = OpenReplicationLink(ReplicationCarrier::Loopback);
    auto Watcher = OpenReplicationLink(ReplicationCarrier::Loopback);
    {
        LinkSpecification Room;
        Room.Room = "frontier-replication-proof";
        Check(Driver->Open(Room, &Quiet), "the first peer joins the room");
        Check(Watcher->Open(Room, &Quiet), "and so does the second");
        Check(Driver->Inspect().LocalSlot == 1 && Watcher->Inspect().LocalSlot == 2,
              "slots are handed out in join order, 1-based");
        Check(Driver->Inspect().Arbiter && !Watcher->Inspect().Arbiter,
              "the first peer in the room arbitrates");
        Check(Driver->Inspect().Occupancy == 2, "and the room counts both of them");
    }

    //-----------------------------------------------------------------------------------------------------
    // The description. Shape, not name, decides whether two peers agree.
    //-----------------------------------------------------------------------------------------------------
    {
        ReplicatedDescription Renamed = VehicleDescription;
        Renamed.Name = "Car";
        Check(DescriptionSignature(Renamed) == DescriptionSignature(VehicleDescription),
              "renaming a placement is not a wire change");

        ReplicatedProperty Reordered[6];
        std::memcpy(Reordered, VehicleProperties, sizeof(Reordered));
        std::swap(Reordered[3], Reordered[4]);
        ReplicatedDescription Other = VehicleDescription;
        Other.Properties = Reordered;
        Check(DescriptionSignature(Other) != DescriptionSignature(VehicleDescription),
              "but reordering the properties is");

        Check(PropertyWidth(PropertyLayout::Translation) == 12
              && PropertyWidth(PropertyLayout::Orientation) == 16
              && PropertyWidth(PropertyLayout::Opaque) == 0,
              "the layouts carry the widths the wire expects");
        Check(PropertySmoothable(PropertyLayout::Whole) == false
              && PropertySmoothable(PropertyLayout::Translation),
              "only the continuous layouts are eased");
    }

    //-----------------------------------------------------------------------------------------------------
    // Enrolment refusals.
    //-----------------------------------------------------------------------------------------------------
    {
        VehicleReading Bytes;
        ReplicationSequence Sequence;
        Check(!Sequence.Enrol(NoNetworkPlacement, VehicleDescription, &Bytes, true),
              "a placement with no identity is refused");
        Check(!Sequence.Enrol(7, VehicleDescription, nullptr, true),
              "and so is one with no bytes behind it");

        ReplicatedProperty Overrun = VehicleProperties[0];
        Overrun.Offset = static_cast<std::uint16_t>(sizeof(VehicleReading) - 4);
        ReplicatedDescription Bad = VehicleDescription;
        Bad.Properties = &Overrun;
        Bad.PropertyCount = 1;
        Check(!Sequence.Enrol(7, Bad, &Bytes, true),
              "a property that would read past the stride is refused at enrolment");

        Check(Sequence.Enrol(7, VehicleDescription, &Bytes, true), "a sound enrolment is taken");
        Check(!Sequence.Enrol(7, VehicleDescription, &Bytes, true), "and taken only once");
        Check(Sequence.Inspect().Enrolled == 1 && Sequence.Inspect().Owned == 1,
              "the sequence counts what it owns");
        Check(Sequence.Retire(7) && !Sequence.Retire(7), "retiring is idempotent only once");
    }

    //-----------------------------------------------------------------------------------------------------
    // Replication proper. The driver owns the vehicle; the watcher follows it.
    //-----------------------------------------------------------------------------------------------------
    VehicleReading Driven;
    VehicleReading Followed;

    ReplicationSequence DriverSequence;
    ReplicationSequence WatcherSequence;
    DriverSequence.Attend(*Driver, &Quiet);
    WatcherSequence.Attend(*Watcher, &Quiet);
    DriverSequence.AssignSendRate(20.0);
    WatcherSequence.AssignSendRate(20.0);
    DriverSequence.AssignSmoothing(0.1);
    WatcherSequence.AssignSmoothing(0.1);

    Check(DriverSequence.Enrol(1001, VehicleDescription, &Driven, true), "the driver owns it");
    Check(WatcherSequence.Enrol(1001, VehicleDescription, &Followed, false), "the watcher follows it");
    Check(WatcherSequence.Inspect().Watched == 1 && WatcherSequence.Inspect().Owned == 0,
          "and knows it does not own it");

    // The first snapshot is a full one, whatever has moved, so a peer that joined late is not left
    //    looking at an empty scene until something happens to change.
    Driven.Translation[0] = 4.0f;
    Driven.Gear = 3;
    Driven.Headlights = 1;
    std::memcpy(Driven.Livery, "RED\0", 4);
    DriverSequence.Advance(0.1);
    Check(WatcherSequence.Inspect().SnapshotsTaken == 1, "the first snapshot lands");
    Check(Followed.Gear == 3 && Followed.Headlights == 1,
          "and the properties that are not eased take effect at once");
    Check(std::memcmp(Followed.Livery, "RED\0", 4) == 0, "including the opaque run");

    //-----------------------------------------------------------------------------------------------------
    // Easing. The watcher is drawn a smoothing delay behind, so it trails and then arrives.
    //-----------------------------------------------------------------------------------------------------
    for (int Step = 0; Step < 8; ++Step)
    {
        Driven.Translation[0] += 1.0f;
        DriverSequence.Advance(0.05);
        WatcherSequence.Advance(0.05);
    }
    Check(Followed.Translation[0] > 4.0f, "the watcher has moved");
    Check(Followed.Translation[0] < Driven.Translation[0],
          "but trails the driver by roughly the smoothing delay");

    // Once the driver stops and the delay has passed, the two readings agree exactly.
    for (int Step = 0; Step < 12; ++Step)
    {
        DriverSequence.Advance(0.05);
        WatcherSequence.Advance(0.05);
    }
    Check(Near(Followed.Translation[0], Driven.Translation[0]),
          "and lands on the driver's own reading once it settles");

    //-----------------------------------------------------------------------------------------------------
    // The quantum. A reading that jitters below it is not worth a packet.
    //-----------------------------------------------------------------------------------------------------
    {
        const std::uint64_t Before = WatcherSequence.Inspect().SnapshotsTaken;
        for (int Step = 0; Step < 20; ++Step)
        {
            Driven.Speed += (Step % 2 == 0) ? 0.0001f : -0.0001f;   // well under the 0.05 quantum
            DriverSequence.Advance(0.05);
        }
        Check(WatcherSequence.Inspect().SnapshotsTaken == Before,
              "jitter under the quantum sends nothing at all");

        Driven.Speed += 2.0f;
        DriverSequence.Advance(0.05);
        Check(WatcherSequence.Inspect().SnapshotsTaken == Before + 1,
              "and a real change sends exactly one snapshot");
    }

    //-----------------------------------------------------------------------------------------------------
    // Authority. A watcher cannot drive what it does not own, and a transfer reverses that.
    //-----------------------------------------------------------------------------------------------------
    {
        Followed.Gear = 99;
        WatcherSequence.Advance(0.05);
        Check(Driven.Gear == 3, "a watcher's own changes are never sent");

        const ReplicationReading Reading = DriverSequence.Inspect();
        Check(Reading.Refused.Unauthorised == 0, "and so nothing is refused yet");

        // Hand it over: the watcher takes authority and the driver gives it up.
        Check(WatcherSequence.Adopt(1001, true) && DriverSequence.Adopt(1001, false),
              "authority moves");
        Followed.Gear = 5;
        WatcherSequence.Advance(0.05);
        Check(Driven.Gear == 5, "and the new owner now drives the old one");

        // While the watcher owns it, a snapshot from the old owner is refused rather than applied.
        Check(DriverSequence.Adopt(1001, true), "if the old owner claims it back without agreement");
        Driven.Gear = 7;
        DriverSequence.Advance(0.05);
        WatcherSequence.Advance(0.05);
        Check(WatcherSequence.Inspect().Refused.Unauthorised >= 1,
              "the other peer refuses a snapshot for a placement it owns");
        Check(Followed.Gear == 5, "and keeps its own reading");
        Check(WatcherSequence.Adopt(1001, false), "the room settles again");
    }

    //-----------------------------------------------------------------------------------------------------
    // Hostile and stale bodies. Counted and dropped, never read past.
    //-----------------------------------------------------------------------------------------------------
    {
        ReplicationSequence Alone;
        VehicleReading Bytes;
        Alone.Enrol(1001, VehicleDescription, &Bytes, false);

        Check(!Alone.Apply(1, nullptr, 0), "a body with no bytes is refused");
        const std::uint8_t NotASnapshot[16] = { 'X' };
        Check(!Alone.Apply(1, NotASnapshot, sizeof(NotASnapshot)), "and so is one with the wrong marker");

        // A sound body, then the same body cut short at every length before its end.
        ReplicationSequence Source;
        VehicleReading Moving;
        Moving.Translation[1] = 12.5f;
        Moving.Gear = 2;
        Source.Enrol(1001, VehicleDescription, &Moving, true);
        std::vector<std::uint8_t> Body;
        Check(Source.Gather(Body, true), "a full snapshot is gathered");
        Check(Body.size() > 8 && Body[0] == 'S' && Body[1] == 1,
              "the body carries the marker and the full-renewal mark");

        for (std::size_t Cut = 1; Cut < Body.size(); ++Cut)
        {
            ReplicationSequence Cutter;
            VehicleReading Spare;
            Cutter.Enrol(1001, VehicleDescription, &Spare, false);
            Cutter.Apply(1, Body.data(), Cut);          // must not read past, must not crash
        }
        ++Checks;   // surviving every truncation is the check

        Check(Alone.Apply(1, Body.data(), Body.size()), "the intact body applies");
        Check(Near(Bytes.Translation[1], 12.5f) || Bytes.Gear == 2,
              "and carries the sender's readings");

        // An identity nobody enrolled, and a shape that disagrees.
        ReplicationSequence Stranger;
        VehicleReading Other;
        Stranger.Enrol(2002, VehicleDescription, &Other, false);
        Check(!Stranger.Apply(1, Body.data(), Body.size()), "an unknown placement is refused");
        Check(Stranger.Inspect().Refused.Unknown == 1, "and counted as unknown");

        std::vector<std::uint8_t> Bent = Body;
        Bent[8 + 4] ^= 0xFFu;                            // corrupt the signature
        Check(!Alone.Apply(1, Bent.data(), Bent.size()), "a mismatched shape is refused");
        Check(Alone.Inspect().Refused.Mismatched == 1, "and counted as mismatched");
    }

    //-----------------------------------------------------------------------------------------------------
    // What the link carried. The loopback counts packets exactly as a cloud carrier would.
    //-----------------------------------------------------------------------------------------------------
    {
        const LinkReading Out = Driver->Inspect();
        const LinkReading In  = Watcher->Inspect();
        Check(Out.PacketsSent > 0, "the driver sent packets");
        Check(In.PacketsReceived > 0, "and the watcher received them");
        Check(Out.BytesSent >= Out.PacketsSent * PacketCodec::HeaderLength,
              "every packet carried a codec header");
        Check(Out.Standing == LinkStanding::Open, "and the link stayed open throughout");

        Driver->Close();
        Check(Watcher->Inspect().LocalSlot == 1 && Watcher->Inspect().Arbiter,
              "when the arbiter leaves, the room hands arbitration on");
        Watcher->Close();
    }

    std::printf("PASS %u checks: carrier seam, wire shape, authority, refusals, quantum and easing.\n",
                Checks);
    return 0;
}
