//============================================================================================================================================
//                                                          RELAYQUEUE.H
//============================================================================================================================================
// 🧩 Wait-free single-producer / single-consumer relay of the latest value of T across a thread seam (main → realtime).
//    Depth 1 with two spare slots — the classic triple-slot mailbox: the producer always owns a free slot to write into,
//    the consumer always owns the slot it last took, and one slot sits in the mailbox. Neither side ever spins, retries,
//    or allocates; stale values are dropped (latest wins), which is exactly right for demand like rpm / throttle.
//
//    Why not a seqlock: Scratchpad/AudioTransportTest [5] showed a seqlock reader giving up on 63 % of reads under a
//    saturating writer (0 torn records, but the realtime side then runs on last block's demand). The triple slot is
//    wait-free on both sides at the price of two extra copies of T.
//
//    T must be trivially copyable. One producer thread, one consumer thread — anything else is a contract violation.

#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace Frontier {

template<typename T>
class RelayQueue
{
    static_assert(std::is_trivially_copyable_v<T>, "RelayQueue relays trivially copyable records only");

public:
    RelayQueue() noexcept = default;
    explicit RelayQueue(const T& Initial) noexcept : Slots{ Initial, Initial, Initial } { }

    RelayQueue(const RelayQueue&)            = delete;
    RelayQueue& operator=(const RelayQueue&) = delete;

    // Producer: publish Value. Overwrites anything the consumer has not yet taken.
    void Publish(const T& Value) noexcept
    {
        Slots[WriteSlot] = Value;
        const uint32_t Previous = Mailbox.exchange(WriteSlot | Fresh, std::memory_order_acq_rel);
        WriteSlot = Previous & SlotMask;   // whatever was in the mailbox (taken or not) becomes our next free slot
    }

    // Consumer: true and *Out = the newest published value if anything new arrived since the last Take; false otherwise
    //    (Out untouched). Never blocks, never retries.
    bool Take(T& Out) noexcept
    {
        if ((Mailbox.load(std::memory_order_acquire) & Fresh) == 0u) return false;
        const uint32_t Previous = Mailbox.exchange(ReadSlot, std::memory_order_acq_rel);   // hand our old slot back, clear Fresh
        ReadSlot = Previous & SlotMask;
        Out = Slots[ReadSlot];
        return true;
    }

    // Consumer: the value last taken (or the initial value), without consulting the mailbox.
    [[nodiscard]] const T& Latest() const noexcept { return Slots[ReadSlot]; }

    // Either side, only while the other is quiescent (construction, Prepare): reset every slot to Value.
    void Place(const T& Value) noexcept
    {
        Slots[0] = Slots[1] = Slots[2] = Value;
        WriteSlot = 0u; ReadSlot = 1u;
        Mailbox.store(2u, std::memory_order_release);
    }

private:
    static constexpr uint32_t SlotMask = 0x3u;
    static constexpr uint32_t Fresh    = 0x4u;

    T                     Slots[3] {};
    uint32_t              WriteSlot = 0u;   // producer-private
    uint32_t              ReadSlot  = 1u;   // consumer-private
    std::atomic<uint32_t> Mailbox   { 2u }; // slot index | Fresh
};

} // namespace Frontier
