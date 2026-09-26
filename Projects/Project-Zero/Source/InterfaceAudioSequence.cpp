//============================================================================================================================================
//                                                  INTERFACEAUDIOSEQUENCE.CPP
//============================================================================================================================================

#include "InterfaceAudioSequence.h"

#include <algorithm>

namespace Frontier {
namespace ProjectZero {

bool InterfaceAudioSequence::Construct(const InterfaceAudioConfiguration& Configuration, std::string* Error) noexcept
{
    Config     = Configuration;
    Powertrain = std::make_unique<CrankClickIntegrator>(Config.CylinderCount);

    AudioConfiguration Device;
    Device.Driver = Config.UseNullDriver ? AudioDriverCategory::Null : AudioDriverCategory::Platform;
    // The click train is a dense impulse sequence; at unity it is harsh next to a quiet room. This is a
    //    demonstration, not a mix, so it sits well below full scale.
    Device.MasterGain = 0.35f;

    if (!Audio.Open(Device, Error)) { Running = false; return false; }

    // Clicks only. The sine sweep in the dyno cell exists to prove the transport renders continuous tone; here
    //    the crank-locked clicks ARE the signal, and a sweep underneath would mask the thing being demonstrated.
    Powertrain->AssignClicks(true);
    Powertrain->AssignSweep(false);
    Audio.Attach(Powertrain.get());

    LastRecord = ComposeDemand(0.0, 0.0, false);
    Powertrain->AssignDemand(LastRecord);
    Running = true;
    return true;
}

void InterfaceAudioSequence::Retire() noexcept
{
    if (!Running) return;
    // Detach BEFORE closing: the realtime callback may be mid-render, and Attach(nullptr) is the documented way
    //    to make it stop touching the integrator that is about to go out of scope.
    Audio.Attach(nullptr);
    Audio.Close();
    Running = false;
}

PowertrainRecord InterfaceAudioSequence::ComposeDemand(double Fill, double Sweep, bool Engaged) const noexcept
{
    PowertrainRecord Record;

    // Progress bar → crank speed. The bar is the panel's one continuous control, so it gets the continuous
    //    quantity; a listener hears the click rate rise as the fill grows, which is the whole point of the demo.
    const double Clamped = std::clamp(Fill, 0.0, 1.0);
    Record.Rpm = static_cast<float>(Config.IdleRpm + Clamped * (Config.RedlineRpm - Config.IdleRpm));

    // Meter sweep → throttle. The needle and the pedal are the same gesture on a real cluster, and driving both
    //    from one value keeps what is seen and what is heard from drifting apart.
    Record.Throttle = static_cast<float>(std::clamp(Sweep, 0.0, 1.0));

    // Toggle → clutch. A discrete control maps to the one genuinely discrete powertrain state.
    Record.ClutchEngaged = Engaged;
    Record.Gear          = Engaged ? 1 : 0;

    // Load follows throttle here. A real cell derives it from the dyno brake; there is no brake in this scene,
    //    so pretending otherwise would be inventing data rather than reporting it.
    Record.Load = Record.Throttle;
    return Record;
}

void InterfaceAudioSequence::AdvanceAudio(const InterfaceTrialSequence& Trial, float DeltaSeconds) noexcept
{
    if (!Running) return;

    // Service the device first: reopen after a loss, and clock the null driver, which has no hardware timer.
    Audio.Advance(DeltaSeconds);

    const PowertrainRecord Record = ComposeDemand(Trial.QueryFillValue(), Trial.QuerySweepValue(), false);

    // Publish unconditionally. The relay is latest-wins and wait-free, so a redundant publish costs one store and
    //    cannot block; skipping it on "unchanged" would add a comparison and a branch to save nothing.
    LastRecord = Record;
    Powertrain->AssignDemand(Record);
}

} // namespace ProjectZero
} // namespace Frontier
