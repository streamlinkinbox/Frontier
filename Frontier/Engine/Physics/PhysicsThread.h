/*============================================================================================================================================
                                                             PHYSICTHREAD.H
============================================================================================================================================*/
// 🧩 Dedicated physics thread: fixed-Δτ world slices (vehicles substep internally at 480 Hz, the adapter at its own
//    Δτ), command mailboxes per vehicle, kinematic proxy Δv exchange, and snapshots published through the lock-free
//    SnapshotExchange. StepAll runs the identical cycle synchronously for deterministic validation.

#pragma once

#include "Engine/Physics/Adapter/PhysicsAdapter.h"
#include "Engine/Physics/VehicleSolver.h"
#include "Engine/Threading/SnapshotExchange.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THREAD CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct PhysicsThreadConfiguration
{
    double WorldSliceSeconds    = 1.0 / 120.0;  // [s] outer slice driven per cycle (vehicle + adapter per slice)
    int    MaxSlicesPerCycle    = 8;            // [-] spiral-of-death clamp while free-running
    bool   PublishEveryCycle    = true;         // [-] publish a snapshot after every consumed batch
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       SNAPSHOTS
//------------------------------------------------------------------------------------------------------------------------

struct PhysicsWheelSnapshot
{
    double Hub[3]            = { 0.0, 0.0, 0.0 };    // [m] world
    double Orientation[4]    = { 0.0, 0.0, 0.0, 1.0 }; // [-] quaternion
    double Spin              = 0.0;                // [rad/s]
    double TyreDeflection    = 0.0;                // [m]
    double Pressure          = 0.0;                // [Pa] gauge
    double TreadKelvin       = 293.15;             // [K]
    double CarcassKelvin     = 293.15;             // [K]
    double GasKelvin         = 293.15;             // [K]
    double Grip              = 1.0;                // [-]
    double TreadRemaining    = 1.0;                // [-] tread rubber share left (1 = new)
    double Load              = 0.0;                // [N]
    double SlipRatio         = 0.0;                // [-]
    double SlipAngle         = 0.0;                // [rad]
    bool   Contact           = false;              // [-]
    bool   Shredded          = false;              // [-]
};

struct PhysicsVehicleSnapshot
{
    bool   Present           = false;              // [-]
    double Position[3]       = { 0.0, 0.0, 0.0 };  // [m]
    double Orientation[4]    = { 0.0, 0.0, 0.0, 1.0 };
    double LinearVelocity[3] = { 0.0, 0.0, 0.0 };  // [m/s]
    double SpeedMps          = 0.0;                // [m/s]
    double EngineRPM         = 0.0;                // [rpm]
    int    GearIndex         = 3;                  // [-]
    double BoostBar          = 0.0;                // [bar]
    double ChargerRPM        = 0.0;                // [rpm]
    double ParasiticDrag     = 0.0;                // [N·m]
    bool   Burnout           = false;              // [-]
    PhysicsWheelSnapshot Wheels[4];
};

struct PhysicsBodySnapshot
{
    PhysicsBodyToken Token         = PhysicsBodyInvalid; // [-]
    double           Position[3]   = { 0.0, 0.0, 0.0 };
    double           Orientation[4]= { 0.0, 0.0, 0.0, 1.0 };
};

struct PhysicsFrameSnapshot
{
    uint64_t               SequenceNumber = 0;   // [-] monotonically increasing per publication
    double                 SimulationTime = 0.0; // [s]
    PhysicsVehicleSnapshot Vehicles[4];          // [-] attached solvers
    PhysicsBodySnapshot    TrackedBodies[64];    // [-] registered adapter bodies
    int                    TrackedCount   = 0;   // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PHYSICS THREAD
//------------------------------------------------------------------------------------------------------------------------

// 📝 Owns one PhysicsAdapter world plus the attached VehicleSolvers. The host enqueues frame seconds; the thread (or
//    StepAll on the caller) consumes them in WorldSliceSeconds slices: vehicle advance → proxy pose/velocities →
//    adapter substeps → collision Δv back into the vehicle → snapshot publish.
class PhysicsThread
{
public:
    PhysicsThread() = default;
    ~PhysicsThread();

    PhysicsThread(const PhysicsThread&)            = delete;
    PhysicsThread& operator=(const PhysicsThread&) = delete;

    static constexpr int MaxVehicles = 4;

    bool Start(PhysicsAdapter& Adapter, const PhysicsThreadConfiguration& Configuration);
    void Stop() noexcept;
    [[nodiscard]] bool QueryRunning() const noexcept { return Running.load(); }

    // 📝 Attaches a vehicle solver: creates its kinematic proxy body on the adapter (demo fallback-hull convex shape,
    //    vehicle mass, zero gravity/damping) and begins exchanging collision Δv each slice. Returns slot or -1.
    int  AttachVehicle(VehicleSolver& Solver, double MassKilograms = 1300.0);
    void ConfigureCommand(int Slot, const VehicleCommand& Command);

    // 📝 Registers an adapter body for snapshot shadowing (render/inspection reads).
    bool TrackBody(PhysicsBodyToken Body);

    // Host-side feeding.
    void EnqueueAdvance(double Seconds);                 // thread consumes asynchronously
    int  StepAll(double Seconds);                        // synchronous identical cycle; returns slices consumed

    [[nodiscard]] SnapshotExchange<PhysicsFrameSnapshot>& AccessSnapshotExchange() noexcept { return Snapshots; }
    [[nodiscard]] double   QuerySimulationTime() const noexcept { return SimulationTime; }
    [[nodiscard]] uint64_t QuerySlicesConsumed() const noexcept { return SlicesConsumed; }
    [[nodiscard]] PhysicsAdapter* QueryAdapter() noexcept { return WorldAdapter; }

private:
    void ThreadMain();
    int  Consume(double Seconds);                        // shared by StepAll and ThreadMain
    void PublishSnapshot();                              // one whole-frame payload through the exchange

    PhysicsAdapter*            WorldAdapter = nullptr;
    PhysicsThreadConfiguration Configuration{};
    SnapshotExchange<PhysicsFrameSnapshot> Snapshots;

    struct VehicleSlot
    {
        VehicleSolver*   Solver = nullptr;
        PhysicsBodyToken Proxy  = PhysicsBodyInvalid;
        VehicleCommand   Command{};
    };
    VehicleSlot Vehicles[MaxVehicles];

    std::vector<PhysicsBodyToken> TrackedBodies;

    std::thread             Lane;
    std::mutex              Mutex;
    std::condition_variable Wake;
    std::atomic<bool>       Running{ false };
    bool                    Stopping       = false;  // [-] guarded by Mutex
    double                  Pending        = 0.0;    // [s] guarded by Mutex
    double                  SimulationTime = 0.0;    // [s]
    uint64_t                SlicesConsumed = 0;      // [-]
    uint64_t                SequenceNumber = 0;      // [-]
};

} // namespace Frontier
