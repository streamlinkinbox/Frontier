//============================================================================================================================================
//                                                       REPLICATIONSEQUENCE.CPP
//============================================================================================================================================
// 📦 The snapshot body, and the four things done with it: gathered from the owned placements,
// delivered, taken apart on arrival, and eased into on the watching peers.
//
// Snapshot body, inside a PacketCodec frame of ReplicationKind::Snapshot:
//
//    'S' | full u8 | moment u32BE | count u16BE
//    then, per placement:  identity u32BE | signature u16BE | moved u32BE | the moved bytes, in order
//
// Every length is checked against what is left of the body before it is read, so a truncated or
// hostile snapshot is counted and dropped rather than read past. At most 32 properties, because the
// moved run is 32 bits — a description with more is refused at enrolment rather than silently clipped.

#include "ReplicationSequence.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Networking
{
namespace
{

constexpr std::uint8_t  SnapshotMarker   = 'S';
constexpr std::size_t   SnapshotHead     = 8;     // marker, full, moment, count
constexpr std::size_t   PlacementHead    = 10;    // identity, signature, moved
constexpr std::uint16_t MostProperties   = 32;

void WriteWhole(std::vector<std::uint8_t>& Body, std::uint32_t Reading) noexcept
{
    Body.push_back(static_cast<std::uint8_t>((Reading >> 24) & 0xFFu));
    Body.push_back(static_cast<std::uint8_t>((Reading >> 16) & 0xFFu));
    Body.push_back(static_cast<std::uint8_t>((Reading >> 8) & 0xFFu));
    Body.push_back(static_cast<std::uint8_t>(Reading & 0xFFu));
}

void WriteShort(std::vector<std::uint8_t>& Body, std::uint16_t Reading) noexcept
{
    Body.push_back(static_cast<std::uint8_t>((Reading >> 8) & 0xFFu));
    Body.push_back(static_cast<std::uint8_t>(Reading & 0xFFu));
}

std::uint32_t ReadWhole(const std::uint8_t* At) noexcept
{
    return (static_cast<std::uint32_t>(At[0]) << 24) | (static_cast<std::uint32_t>(At[1]) << 16)
         | (static_cast<std::uint32_t>(At[2]) << 8)  |  static_cast<std::uint32_t>(At[3]);
}

std::uint16_t ReadShort(const std::uint8_t* At) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(At[0]) << 8) | At[1]);
}

float Between(float Earlier, float Later, float Along) noexcept
{
    return Earlier + (Later - Earlier) * Along;
}

// Four-component shortest-arc interpolation, normalised. A negative dot means the two readings
//    describe the same orientation by opposite routes, so one is flipped before easing.
void BetweenOrientations(const float Earlier[4], const float Later[4], float Along, float Out[4]) noexcept
{
    float Agreement = Earlier[0] * Later[0] + Earlier[1] * Later[1]
                    + Earlier[2] * Later[2] + Earlier[3] * Later[3];
    float Signed[4] = { Later[0], Later[1], Later[2], Later[3] };
    if (Agreement < 0.0f)
    {
        for (int At = 0; At < 4; ++At) Signed[At] = -Signed[At];
        Agreement = -Agreement;
    }
    for (int At = 0; At < 4; ++At) Out[At] = Between(Earlier[At], Signed[At], Along);

    const float Length = std::sqrt(Out[0] * Out[0] + Out[1] * Out[1] + Out[2] * Out[2] + Out[3] * Out[3]);
    if (Length > 1e-6f)
        for (int At = 0; At < 4; ++At) Out[At] /= Length;
    else
        { Out[0] = 0.0f; Out[1] = 0.0f; Out[2] = 0.0f; Out[3] = 1.0f; }
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        SEATING AND ENROLMENT
//------------------------------------------------------------------------------------------------------------------------

ReplicationSequence::~ReplicationSequence()
{
    Detach();
}

void ReplicationSequence::Attend(ReplicationLink& Link, DiagnosticReception Reception) noexcept
{
    Detach();
    Link_ = &Link;
    Reception_ = Reception;
    Link.AttendWith(&ReplicationSequence::Receive, this);
    Elapsed_ = 0.0;
    SinceSend_ = 0.0;
    Renewing_ = true;
}

void ReplicationSequence::Detach() noexcept
{
    if (Link_ != nullptr) Link_->AttendWith(nullptr, nullptr);
    Link_ = nullptr;
    Reception_ = nullptr;
}

ReplicationSequence::Enrolment* ReplicationSequence::Find(NetworkPlacement Placement) noexcept
{
    for (Enrolment& Held : Enrolled_)
        if (Held.Placement == Placement) return &Held;
    return nullptr;
}

bool ReplicationSequence::Enrol(NetworkPlacement Placement, const ReplicatedDescription& Description,
                                void* Reading, bool Owned) noexcept
{
    if (Placement == NoNetworkPlacement || Reading == nullptr) return false;
    if (Description.Properties == nullptr || Description.PropertyCount == 0) return false;
    if (Description.PropertyCount > MostProperties) return false;
    if (Description.Stride == 0) return false;
    if (Find(Placement) != nullptr) return false;

    // Every property must sit inside the stride the description declares, or a snapshot would read
    //    or write past the simulation's own run. Checked once here rather than on every packet.
    for (std::uint16_t At = 0; At < Description.PropertyCount; ++At)
    {
        const ReplicatedProperty& Property = Description.Properties[At];
        const std::uint16_t Width = PropertyWidth(Property.Layout);
        const std::uint16_t Span  = Width != 0 ? Width : Property.Width;
        if (Span == 0 || std::size_t(Property.Offset) + Span > Description.Stride) return false;
    }

    Enrolment Held;
    Held.Placement   = Placement;
    Held.Description = Description;
    Held.Signature   = DescriptionSignature(Description);
    Held.Reading     = static_cast<std::uint8_t*>(Reading);
    Held.Owned       = Owned;
    Held.Shadow.assign(Description.Stride, 0u);
    Held.Earlier.assign(Description.Stride, 0u);
    Held.Later.assign(Description.Stride, 0u);
    Enrolled_.push_back(std::move(Held));
    return true;
}

bool ReplicationSequence::Retire(NetworkPlacement Placement) noexcept
{
    for (std::size_t At = 0; At < Enrolled_.size(); ++At)
        if (Enrolled_[At].Placement == Placement)
        {
            Enrolled_.erase(Enrolled_.begin() + static_cast<std::ptrdiff_t>(At));
            return true;
        }
    return false;
}

bool ReplicationSequence::Adopt(NetworkPlacement Placement, bool Owned) noexcept
{
    Enrolment* Held = Find(Placement);
    if (Held == nullptr) return false;
    if (Held->Owned == Owned) return true;
    Held->Owned = Owned;
    if (Owned)
    {
        // A new owner sends everything once: the room has no way to know which of its properties the
        //    previous owner had already told it about.
        Held->Sampled = false;
        std::memset(Held->Shadow.data(), 0xFFu, Held->Shadow.size());
        Renewing_ = true;
    }
    return true;
}

void ReplicationSequence::AssignSendRate(double Hertz) noexcept
{
    Interval_ = Hertz > 0.0 ? 1.0 / Hertz : 0.0;
}

void ReplicationSequence::AssignSmoothing(double Seconds) noexcept
{
    Smoothing_ = Seconds > 0.0 ? Seconds : 0.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          GATHERING
//------------------------------------------------------------------------------------------------------------------------

bool ReplicationSequence::Gather(std::vector<std::uint8_t>& Body, bool InFull) noexcept
{
    const std::uint32_t Moment = static_cast<std::uint32_t>(Elapsed_ * 1000.0);
    Body.clear();
    Body.push_back(SnapshotMarker);
    Body.push_back(InFull ? 1u : 0u);
    WriteWhole(Body, Moment);
    WriteShort(Body, 0);                     // the count is written back once it is known

    std::uint16_t Counted = 0;
    for (Enrolment& Held : Enrolled_)
    {
        if (!Held.Owned) continue;

        std::uint32_t Moved = 0;
        for (std::uint16_t At = 0; At < Held.Description.PropertyCount; ++At)
        {
            const ReplicatedProperty& Property = Held.Description.Properties[At];
            if (InFull || PropertyMoved(Property, Held.Reading + Property.Offset,
                                        Held.Shadow.data() + Property.Offset))
                Moved |= (1u << At);
        }
        if (Moved == 0) continue;

        // The body is capped at the codec's own payload ceiling. A placement that will not fit is
        //    left for the next cycle rather than half-written — a partial placement is unreadable.
        std::size_t Wanted = PlacementHead;
        for (std::uint16_t At = 0; At < Held.Description.PropertyCount; ++At)
        {
            if ((Moved & (1u << At)) == 0) continue;
            const ReplicatedProperty& Property = Held.Description.Properties[At];
            const std::uint16_t Width = PropertyWidth(Property.Layout);
            Wanted += Width != 0 ? Width : std::size_t(2) + Property.Width;
        }
        if (Body.size() + Wanted > PacketCodec::MaxPayload) break;

        WriteWhole(Body, Held.Placement);
        WriteShort(Body, Held.Signature);
        WriteWhole(Body, Moved);
        for (std::uint16_t At = 0; At < Held.Description.PropertyCount; ++At)
        {
            if ((Moved & (1u << At)) == 0) continue;
            const ReplicatedProperty& Property = Held.Description.Properties[At];
            const std::uint8_t* From = Held.Reading + Property.Offset;
            const std::uint16_t Width = PropertyWidth(Property.Layout);
            if (Width != 0)
                Body.insert(Body.end(), From, From + Width);
            else
            {
                WriteShort(Body, Property.Width);
                Body.insert(Body.end(), From, From + Property.Width);
            }
            std::memcpy(Held.Shadow.data() + Property.Offset, From, Width != 0 ? Width : Property.Width);
            ++PropertiesOut_;
        }
        ++Counted;
    }

    Body[6] = static_cast<std::uint8_t>((Counted >> 8) & 0xFFu);
    Body[7] = static_cast<std::uint8_t>(Counted & 0xFFu);
    return Counted > 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           APPLYING
//------------------------------------------------------------------------------------------------------------------------

bool ReplicationSequence::Apply(std::uint32_t Slot, const std::uint8_t* Body, std::size_t Length) noexcept
{
    if (Body == nullptr || Length < SnapshotHead || Body[0] != SnapshotMarker)
    {
        ++Refused_.Malformed;
        return false;
    }
    const std::uint32_t Moment = ReadWhole(Body + 2);
    const std::uint16_t Counted = ReadShort(Body + 6);

    std::size_t At = SnapshotHead;
    for (std::uint16_t Which = 0; Which < Counted; ++Which)
    {
        if (At + PlacementHead > Length) { ++Refused_.Malformed; return false; }
        const NetworkPlacement Placement = ReadWhole(Body + At);
        const std::uint16_t    Signature = ReadShort(Body + At + 4);
        const std::uint32_t    Moved     = ReadWhole(Body + At + 6);
        At += PlacementHead;

        Enrolment* Held = Find(Placement);

        // An unknown or mismatched placement still has to be stepped over, because the properties
        //    that follow it belong to it and the next identity sits after them. Without the shape
        //    there is no way to know how far that is, so the whole snapshot stops here.
        if (Held == nullptr) { ++Refused_.Unknown; return false; }
        if (Held->Signature != Signature) { ++Refused_.Mismatched; return false; }
        if (Held->Owned) { ++Refused_.Unauthorised; return false; }
        if (Held->Sampled && Moment < Held->LaterAt) { ++Refused_.Stale; return false; }

        // The newest sample becomes the older one, and the arrival is written over the newer.
        if (Held->Sampled)
        {
            Held->Earlier = Held->Later;
            Held->EarlierAt = Held->LaterAt;
        }
        else
        {
            std::memcpy(Held->Later.data(), Held->Reading, Held->Description.Stride);
            Held->Earlier = Held->Later;
            Held->EarlierAt = Moment;
        }

        for (std::uint16_t Index = 0; Index < Held->Description.PropertyCount; ++Index)
        {
            if ((Moved & (1u << Index)) == 0) continue;
            const ReplicatedProperty& Property = Held->Description.Properties[Index];
            const std::uint16_t Width = PropertyWidth(Property.Layout);
            if (Width != 0)
            {
                if (At + Width > Length) { ++Refused_.Malformed; return false; }
                std::memcpy(Held->Later.data() + Property.Offset, Body + At, Width);
                At += Width;
            }
            else
            {
                if (At + 2 > Length) { ++Refused_.Malformed; return false; }
                const std::uint16_t Span = ReadShort(Body + At);
                At += 2;
                if (Span != Property.Width || At + Span > Length) { ++Refused_.Malformed; return false; }
                std::memcpy(Held->Later.data() + Property.Offset, Body + At, Span);
                At += Span;
            }
        }

        // Anything the sender did not move keeps the reading it already had, so a delta snapshot
        //    does not quietly zero the rest of the placement.
        Held->LaterAt = Moment;
        Held->Authority = Slot;
        Held->Sampled = true;

        // Properties that are not eased take effect at once; the eased ones are written by Ease().
        for (std::uint16_t Index = 0; Index < Held->Description.PropertyCount; ++Index)
        {
            const ReplicatedProperty& Property = Held->Description.Properties[Index];
            if (Property.Smoothed && PropertySmoothable(Property.Layout)) continue;
            const std::uint16_t Width = PropertyWidth(Property.Layout);
            const std::uint16_t Span  = Width != 0 ? Width : Property.Width;
            std::memcpy(Held->Reading + Property.Offset, Held->Later.data() + Property.Offset, Span);
        }
    }

    ++Taken_;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                           EASING
//------------------------------------------------------------------------------------------------------------------------

void ReplicationSequence::Ease(Enrolment& Held, std::uint32_t RenderAt) noexcept
{
    if (!Held.Sampled) return;

    const std::uint32_t Span = Held.LaterAt > Held.EarlierAt ? Held.LaterAt - Held.EarlierAt : 0u;
    float Along = 1.0f;
    if (Span > 0)
    {
        if (RenderAt <= Held.EarlierAt) Along = 0.0f;
        else if (RenderAt >= Held.LaterAt) Along = 1.0f;
        else Along = float(RenderAt - Held.EarlierAt) / float(Span);
    }

    for (std::uint16_t At = 0; At < Held.Description.PropertyCount; ++At)
    {
        const ReplicatedProperty& Property = Held.Description.Properties[At];
        if (!Property.Smoothed || !PropertySmoothable(Property.Layout)) continue;

        const std::uint8_t* From = Held.Earlier.data() + Property.Offset;
        const std::uint8_t* To   = Held.Later.data() + Property.Offset;
        std::uint8_t*       Into = Held.Reading + Property.Offset;

        if (Property.Layout == PropertyLayout::Orientation)
        {
            float Was[4], Now[4], Eased[4];
            std::memcpy(Was, From, 16);
            std::memcpy(Now, To, 16);
            BetweenOrientations(Was, Now, Along, Eased);
            std::memcpy(Into, Eased, 16);
            continue;
        }

        const std::uint16_t Decimals = static_cast<std::uint16_t>(PropertyWidth(Property.Layout) / 4);
        for (std::uint16_t Which = 0; Which < Decimals; ++Which)
        {
            float Was = 0.0f, Now = 0.0f;
            std::memcpy(&Was, From + Which * 4, 4);
            std::memcpy(&Now, To + Which * 4, 4);
            const float Eased = Between(Was, Now, Along);
            std::memcpy(Into + Which * 4, &Eased, 4);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          ONE CYCLE
//------------------------------------------------------------------------------------------------------------------------

void ReplicationSequence::Receive(void* Attendant, const ArrivedPacket& Packet) noexcept
{
    auto* Sequence = static_cast<ReplicationSequence*>(Attendant);
    if (Sequence == nullptr) return;
    if (Packet.Carriage != ReplicationKind::Snapshot) return;   // probes and input ride elsewhere
    Sequence->Apply(Packet.Slot, Packet.Payload, Packet.Length);
}

void ReplicationSequence::Advance(double Seconds) noexcept
{
    if (Seconds < 0.0) Seconds = 0.0;
    Elapsed_ += Seconds;
    SinceSend_ += Seconds;

    if (Link_ != nullptr) Link_->Advance();

    // Remote placements are drawn a smoothing delay behind the newest arrival, so there is always a
    //    sample on each side of the moment being drawn and the ease never has to guess ahead.
    const double Behind = Elapsed_ * 1000.0 - Smoothing_ * 1000.0;
    const std::uint32_t RenderAt = Behind > 0.0 ? static_cast<std::uint32_t>(Behind) : 0u;
    for (Enrolment& Held : Enrolled_)
        if (!Held.Owned) Ease(Held, RenderAt);

    if (Link_ == nullptr) return;
    if (Interval_ > 0.0 && SinceSend_ < Interval_) return;
    SinceSend_ = 0.0;

    const bool InFull = Renewing_;
    if (!Gather(Body_, InFull)) { Renewing_ = false; return; }
    if (Link_->Deliver(ReplicationKind::Snapshot, Body_.data(), Body_.size(), InFull))
    {
        ++Delivered_;
        Renewing_ = false;
    }
    else if (Reception_ != nullptr)
    {
        char Line[128]{};
        std::snprintf(Line, sizeof(Line), "replication_snapshot_refused bytes=%u carrier=%s",
                      static_cast<unsigned>(Body_.size()), CarrierName(Link_->Carrier()));
        Reception_(Line);
    }
}

ReplicationReading ReplicationSequence::Inspect() const noexcept
{
    ReplicationReading Reading;
    Reading.Enrolled = static_cast<std::uint32_t>(Enrolled_.size());
    for (const Enrolment& Held : Enrolled_)
        (Held.Owned ? Reading.Owned : Reading.Watched) += 1u;
    Reading.SnapshotsDelivered  = Delivered_;
    Reading.SnapshotsTaken      = Taken_;
    Reading.PropertiesDelivered = PropertiesOut_;
    Reading.Moment   = static_cast<std::uint32_t>(Elapsed_ * 1000.0);
    Reading.Refused  = Refused_;
    return Reading;
}

}   // namespace Networking
