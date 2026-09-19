//============================================================================================================================================
// 📦 Frontier/DeviceExchange/ExecutionQueue.cpp — Lock-Free Double-Ended Task Stealing Implementation
//============================================================================================================================================

#include "ExecutionQueue.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ExecutionQueue::ExecutionQueue(size_t InitialCapacity) noexcept
    : Tasks(InitialCapacity)
    , TopIndex(0)
    , BottomIndex(0)
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                                QUEUE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool ExecutionQueue::PushBottom(TaskFunction Task) noexcept
{
    int64_t Bottom = BottomIndex.load(std::memory_order_relaxed);
    int64_t Top = TopIndex.load(std::memory_order_acquire);

    if (Bottom - Top >= static_cast<int64_t>(Tasks.size()))
    {
        return false;
    }

    Tasks[Bottom % Tasks.size()] = std::move(Task);
    BottomIndex.store(Bottom + 1, std::memory_order_release);
    return true;
}

bool ExecutionQueue::PopBottom(TaskFunction& OutTask) noexcept
{
    int64_t Bottom = BottomIndex.load(std::memory_order_relaxed) - 1;
    BottomIndex.store(Bottom, std::memory_order_seq_cst);
    int64_t Top = TopIndex.load(std::memory_order_seq_cst);

    if (Top <= Bottom)
    {
        OutTask = std::move(Tasks[Bottom % Tasks.size()]);
        if (Top == Bottom)
        {
            if (!TopIndex.compare_exchange_strong(Top, Top + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
            {
                BottomIndex.store(Bottom + 1, std::memory_order_relaxed);
                return false;
            }
            BottomIndex.store(Bottom + 1, std::memory_order_relaxed);
        }
        return true;
    }

    BottomIndex.store(Bottom + 1, std::memory_order_relaxed);
    return false;
}

bool ExecutionQueue::StealTop(TaskFunction& OutTask) noexcept
{
    int64_t Top = TopIndex.load(std::memory_order_acquire);
    int64_t Bottom = BottomIndex.load(std::memory_order_acquire);

    if (Top < Bottom)
    {
        OutTask = Tasks[Top % Tasks.size()];
        if (TopIndex.compare_exchange_strong(Top, Top + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {
            return true;
        }
    }
    return false;
}

size_t ExecutionQueue::QueryTaskCount() const noexcept
{
    int64_t Top = TopIndex.load(std::memory_order_relaxed);
    int64_t Bottom = BottomIndex.load(std::memory_order_relaxed);
    return (Bottom > Top) ? static_cast<size_t>(Bottom - Top) : 0;
}

} // namespace Frontier
