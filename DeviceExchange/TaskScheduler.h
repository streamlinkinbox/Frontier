//============================================================================================================================================
// 📦 Frontier/DeviceExchange/TaskScheduler.h — Work-Stealing Multi-Threaded Task Coordinator Graph
//============================================================================================================================================

#pragma once

#include "ExecutionQueue.h"
#include <thread>
#include <atomic>
#include <vector>
#include <memory>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    TASK TOKEN
//------------------------------------------------------------------------------------------------------------------------

struct TaskToken
{
    std::atomic<uint32_t>*  CompletionCounter;                  // [ptr] pointer to atomic pending tasks counter
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   TASK SCHEDULER
//------------------------------------------------------------------------------------------------------------------------

class TaskScheduler
{
public:
    explicit TaskScheduler(uint32_t DesiredWorkerCount = 0) noexcept;
    ~TaskScheduler() noexcept;

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    void                    Initialize() noexcept;
    void                    Terminate() noexcept;

    [[nodiscard]] TaskToken DispatchTask(TaskFunction Task) noexcept;
    void                    DispatchParallel(uint32_t ItemCount, uint32_t ChunkSize, std::function<void(uint32_t, uint32_t)> ChunkFunction) noexcept;
    void                    WaitForToken(TaskToken Token) noexcept;

    [[nodiscard]] uint32_t  QueryWorkerCount() const noexcept;

    // Single unified conversion operator
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    void                    WorkerLoop(uint32_t WorkerIndex) noexcept;

    uint32_t                WorkerCount;                        // [count] number of worker threads
    std::vector<std::thread> Workers;                           // [threads] worker thread pool
    std::vector<std::unique_ptr<ExecutionQueue>> Queues;        // [queues] per-thread task queues
    std::atomic<bool>       RunningCondition;                   // [bool] lifecycle status
};

template<>
inline uint32_t TaskScheduler::Convert<uint32_t>() const noexcept
{
    return WorkerCount;
}

} // namespace Frontier
