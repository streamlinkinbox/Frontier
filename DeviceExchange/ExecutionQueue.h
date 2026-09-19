//============================================================================================================================================
// 📦 Frontier/DeviceExchange/ExecutionQueue.h — Lock-Free Double-Ended Task Stealing Queue
//============================================================================================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <atomic>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    TASK FUNCTION
//------------------------------------------------------------------------------------------------------------------------

using TaskFunction = std::function<void()>;

//------------------------------------------------------------------------------------------------------------------------
//                                                   EXECUTION QUEUE
//------------------------------------------------------------------------------------------------------------------------

class ExecutionQueue
{
public:
    explicit ExecutionQueue(size_t InitialCapacity = 1024) noexcept;
    ~ExecutionQueue() noexcept = default;

    ExecutionQueue(const ExecutionQueue&) = delete;
    ExecutionQueue& operator=(const ExecutionQueue&) = delete;

    [[nodiscard]] bool      PushBottom(TaskFunction Task) noexcept;
    [[nodiscard]] bool      PopBottom(TaskFunction& OutTask) noexcept;
    [[nodiscard]] bool      StealTop(TaskFunction& OutTask) noexcept;
    [[nodiscard]] size_t    QueryTaskCount() const noexcept;

    // Single unified conversion operator for occupancy query
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    std::vector<TaskFunction> Tasks;                            // [tasks] contiguous task ring
    std::atomic<int64_t>    TopIndex;                           // [index] top index for task stealing
    std::atomic<int64_t>    BottomIndex;                        // [index] bottom index for local push/pop
};

template<>
inline size_t ExecutionQueue::Convert<size_t>() const noexcept
{
    return QueryTaskCount();
}

template<>
inline bool ExecutionQueue::Convert<bool>() const noexcept
{
    return QueryTaskCount() > 0;
}

} // namespace Frontier
