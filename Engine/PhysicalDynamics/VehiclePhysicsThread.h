//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/VehiclePhysicsThread.h — Dedicated fixed-rate physics thread + lock-free GT↔PT conduits
//============================================================================================================================================
//
//    Phase 0 of the vehicle port. GRIT ran its vehicle on Chaos's physics thread (an async SimCallback) so the sim stepped
//    on a stable cadence independent of the render frame, and talked to the game thread through double-buffered channels
//    guarded by atomic flags. Frontier's RigidBodySolver normally steps synchronously from the main loop; a car needs the
//    same decoupled, high-cadence stepping (the suspension strut and the XPBD tyre want 240–1000 Hz, far above the render
//    rate), so this owns a std::thread that drives stepping itself.
//
//    The thread is engine-agnostic: it knows nothing about Jolt or tyres. Each fixed step it calls a StepCallback the
//    vehicle layer installs — inside that callback the vehicle solver reads inputs, casts the wheels, computes suspension +
//    tyre forces (sub-stepping the XPBD tyre as it likes), applies them, and calls RigidBodySolver::StepOnce() exactly once.
//    That keeps this file free of physics detail and unit-testable with a mock callback.
//
//    Threading contract:
//      • Start()/Stop()/QueryMetrics() are called from the game thread (GT).
//      • The StepCallback runs on the physics thread (PT). It must only touch PT-owned state + the conduits below.
//      • Cross-thread data moves through DataChannel<T> (a single-slot mailbox) — GT Write / PT Read for input, the reverse
//        for output. Small POD payloads only; the point is to hand off a snapshot, not to share memory.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                          DATA CHANNEL  (single-slot GT↔PT mailbox)
//------------------------------------------------------------------------------------------------------------------------
// A minimal thread-safe hand-off of one value, mirroring GRIT's TDataChannel. Write overwrites the slot and marks it full;
//    Read consumes it (marks empty); Peek copies without consuming. Guarded by a mutex — the payloads are small snapshots
//    swapped once per step, so contention is negligible and correctness beats cleverness here.

template<typename T>
class DataChannel
{
public:
    void Write(const T& Value) noexcept
    {
        std::lock_guard<std::mutex> Guard(Mutex);
        Slot = Value;
        Full = true;
    }

    [[nodiscard]] bool Read(T& Out) noexcept
    {
        std::lock_guard<std::mutex> Guard(Mutex);
        if (!Full) return false;
        Out  = Slot;
        Full = false;
        return true;
    }

    [[nodiscard]] bool Peek(T& Out) const noexcept
    {
        std::lock_guard<std::mutex> Guard(Mutex);
        if (!Full) return false;
        Out = Slot;
        return true;
    }

    [[nodiscard]] bool HasValue() const noexcept
    {
        std::lock_guard<std::mutex> Guard(Mutex);
        return Full;
    }

private:
    mutable std::mutex Mutex;
    T                  Slot{};
    bool               Full = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                THREAD CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct VehiclePhysicsThreadConfiguration
{
    double   StepHz            = 60.0;    // [Hz] fixed simulation rate (the callback's dt is 1/StepHz)
    uint32_t MaxCatchUpSteps   = 4u;      // [-]  most steps run in a single wake before extra backlog is dropped
    bool     PinBusyWait       = false;   // [-]  false: sleep between steps (cheap); true: spin for tighter cadence
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    METRICS
//------------------------------------------------------------------------------------------------------------------------

struct VehiclePhysicsThreadMetrics
{
    uint64_t StepCount              = 0u;    // [-]  fixed steps executed since Start()
    uint32_t StepsLastWake          = 0u;    // [-]  steps run in the most recent wake
    uint64_t DroppedSteps           = 0u;    // [-]  steps skipped by the catch-up cap (a hitch)
    float    LastStepMicroseconds   = 0.0f;  // [µs] wall time inside the last StepCallback
    float    RealtimeRatio          = 1.0f;  // [-]  sim wall-time ÷ real elapsed; <1 = keeping up, >1 = falling behind
    bool     Running                = false; // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                              VEHICLE PHYSICS THREAD
//------------------------------------------------------------------------------------------------------------------------

class VehiclePhysicsThread
{
public:
    // StepIndex is the running fixed-step count; FixedDeltaSeconds is 1/StepHz. Runs on the physics thread.
    using StepCallback = std::function<void(uint64_t StepIndex, float FixedDeltaSeconds)>;

    VehiclePhysicsThread() noexcept = default;
    ~VehiclePhysicsThread() noexcept { Stop(); }

    VehiclePhysicsThread(const VehiclePhysicsThread&) = delete;
    VehiclePhysicsThread& operator=(const VehiclePhysicsThread&) = delete;

    // Spins up the thread. The callback must be valid. Returns false if already running or the config is nonsensical.
    [[nodiscard]] bool Start(const VehiclePhysicsThreadConfiguration& Configuration, StepCallback OnStep) noexcept;

    // Signals the loop to finish, joins the thread. Safe to call more than once and from the destructor.
    void Stop() noexcept;

    [[nodiscard]] bool IsRunning() const noexcept { return Running.load(std::memory_order_acquire); }

    // A consistent snapshot of the latest metrics (mutex-guarded copy).
    [[nodiscard]] VehiclePhysicsThreadMetrics QueryMetrics() const noexcept;

private:
    void Loop() noexcept;   // the physics-thread body

    VehiclePhysicsThreadConfiguration   Config;
    StepCallback                        Callback;
    std::thread                         Worker;
    std::atomic<bool>                   Running{ false };
    std::atomic<bool>                   StopRequested{ false };

    mutable std::mutex                  MetricsMutex;
    VehiclePhysicsThreadMetrics         Metrics;
};

} // namespace Frontier
