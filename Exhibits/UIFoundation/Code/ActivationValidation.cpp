/*==========================================================================================================================================
                                                      ACTIVATIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify ordering, idempotence, failure cleanup, editor gates, and derived destruction.

#include "Engine/Lifecycle/ActivationSequence.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    VALIDATION SUPPORT
//------------------------------------------------------------------------------------------------------------------------

void Require(bool Condition, const char* Description)
{
    if (!Condition) throw std::runtime_error(Description);
}

struct ActivationTrace
{
    std::string Operations;
    bool        InitializationAccepted = true;
    bool        ActivationAccepted     = true;
    bool        RecursiveRejected      = false;
};

// The same lifecycle is exercised for a control and a scene geometry allocation.
class TraceActivation : public ActivationSequence
{
public:
    explicit TraceActivation(ActivationTrace& Destination) : Trace(Destination) {}
    ~TraceActivation() noexcept override { Trace.Operations += 'D'; }

protected:
    bool InitializeContent() noexcept override
    {
        Trace.Operations += 'I';
        Trace.RecursiveRejected = !Initialize() && !Activate() && !Finalize();
        return Trace.InitializationAccepted;
    }
    bool ActivateContent() noexcept override
    {
        Trace.Operations += 'A';
        return Trace.ActivationAccepted;
    }
    void SuspendContent() noexcept override { Trace.Operations += 'S'; }
    void IntegrateContent(double) noexcept override { Trace.Operations += 'T'; }
    void FinalizeContent() noexcept override { Trace.Operations += 'F'; }
#if FRONTIER_EDITOR
    void InitializeEditorContent() noexcept override { Trace.Operations += 'E'; }
    void FinalizeEditorContent() noexcept override { Trace.Operations += 'X'; }
#endif

private:
    ActivationTrace& Trace;
};

class ControlActivation final : public TraceActivation
{
public:
    using TraceActivation::TraceActivation;
};

class GeometryActivation final : public TraceActivation
{
public:
    using TraceActivation::TraceActivation;
};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      VERIFICATION
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    using namespace Frontier;
    try
    {
        for (bool GeometryEnabled : {false, true})
        {
            ActivationTrace Trace;
            ActivationOwnership Allocation(GeometryEnabled
                ? static_cast<ActivationSequence*>(new GeometryActivation(Trace))
                : static_cast<ActivationSequence*>(new ControlActivation(Trace)));
            Require(!Allocation->Activate(), "Activation before initialization must fail");
            Require(!Allocation->Integrate(0.1), "Dormant integration must fail");
            Require(Allocation->Initialize() && Allocation->Initialize(), "Initialization must be idempotent");
            Require(Trace.RecursiveRejected, "Recursive transitions must be rejected");
            Require(Allocation->Activate() && Allocation->Activate(), "Activation must be idempotent");
            Require(!Allocation->Integrate(-1.0), "Negative duration must fail");
            Require(!Allocation->Integrate(std::numeric_limits<double>::quiet_NaN()), "NaN must fail");
            Require(!Allocation->Integrate(std::numeric_limits<double>::infinity()), "Infinity must fail");
            Require(Allocation->Integrate(0.1), "Active integration must succeed");
            Require(Allocation->Suspend() && Allocation->Suspend(), "Suspension must be idempotent");
            Require(!Allocation->Integrate(0.1), "Suspended integration must fail");
            Require(Allocation->Activate(), "Reactivation must succeed");
            Require(Allocation->Finalize() && Allocation->Finalize(), "Finalization must be idempotent");
            Require(!Allocation->Initialize() && !Allocation->Activate(), "Finalization must be terminal");
            Allocation.reset();
#if FRONTIER_EDITOR
            Require(Trace.Operations == "IEATSASXFD", "Editor lifecycle order differs");
#else
            Require(Trace.Operations == "IATSASFD", "Runtime lifecycle order differs");
#endif
        }
        ActivationTrace Rejected;
        Rejected.InitializationAccepted = false;
        ActivationOwnership Failed(new ControlActivation(Rejected));
        Require(!Failed->Initialize(), "Initialization failure must propagate");
        Failed.reset();
        Require(Rejected.Operations == "IFD", "Partial initialization must be finalized exactly once");

        ActivationTrace Retry;
        Retry.ActivationAccepted = false;
        ActivationOwnership Retried(new ControlActivation(Retry));
        Require(Retried->Initialize() && !Retried->Activate(), "Activation failure must propagate");
        Require(Retried->QueryPhase() == ActivationPhase::Ready, "Failed activation must remain ready");
        Retry.ActivationAccepted = true;
        Require(Retried->Activate(), "Failed activation must permit retry");
        Retried.reset();
        Require(Retry.Operations.ends_with("FD"), "Owner must finalize before destruction");

        ActivationTrace Dormant;
        ActivationOwnership NeverInitialized(new ControlActivation(Dormant));
        NeverInitialized.reset();
        Require(Dormant.Operations == "D", "Dormant destruction must not finalize uninitialized resources");
        std::cout << "PASS: control and geometry lifecycle, editor=" << FRONTIER_EDITOR
                  << "; ordering, idempotence, recursion, failures, reclamation, invalid durations\n";
        return 0;
    }
    catch (const std::exception& Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
