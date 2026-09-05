//============================================================================================================================================
//                                                        DYNOSEQUENCE.H
//============================================================================================================================================
// 🧩 Scripted dyno pulls: deterministic rpm / throttle timelines the dyno cell plays in place of a physics powertrain.
//    Each pull is a list of keyframes; Advance(Δτ) walks them and QueryRecord() returns the demand a game would produce.
//    The same pulls drive the realtime device and the offline --render, which is what makes the two outputs comparable.
//
//    Pulls (row A1 set; A2 adds gear blips and the overrun-pop script):
//        idle       900 rpm hold, closed throttle
//        sweep      linear 1 000 → 9 000 → 1 000 rpm over 12 s, half throttle
//        pull       WOT: 2 000 → redline in 6 s, hold 1 s, lift (overrun back to idle over 5 s)
//        steady     4 000 rpm hold, 40 % throttle
//        blip       three throttle stabs from idle (rev-match rehearsal)

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   POWERTRAIN RECORD
//------------------------------------------------------------------------------------------------------------------------

// The only thing a game ever feeds the acoustic integrator. Physics produces it; the dyno scripts it.
struct PowertrainRecord
{
    float    Rpm            = 900.0f;   // [rpm]  crank speed
    float    Throttle       = 0.0f;     // [-]    0 … 1 pedal
    float    Load           = 0.0f;     // [-]    0 … 1 fraction of the torque the engine could make at this rpm
    int32_t  Gear           = 0;        // [-]    0 = neutral
    bool     ClutchEngaged  = false;    // [-]
    float    Boost          = 0.0f;     // [bar]  manifold gauge pressure (forced induction only)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   DYNO SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

struct DynoKeyframe
{
    float Time      = 0.0f;   // [s]
    float Rpm       = 900.0f; // [rpm]
    float Throttle  = 0.0f;   // [-]
};

class DynoSequence
{
public:
    DynoSequence() noexcept;

    // Selects a named pull ("idle", "sweep", "pull", "steady", "blip"); unknown names fall back to "sweep" and return false.
    bool Select(std::string_view Name, float RedlineRpm) noexcept;

    void Advance(float Δτ) noexcept;
    void Restart() noexcept { Elapsed = 0.0f; }

    [[nodiscard]] const PowertrainRecord& QueryRecord()   const noexcept { return Record; }
    [[nodiscard]] float                   QueryElapsed()  const noexcept { return Elapsed; }
    [[nodiscard]] float                   QueryDuration() const noexcept { return Keyframes.empty() ? 0.0f : Keyframes.back().Time; }
    [[nodiscard]] bool                    Finished()      const noexcept { return Elapsed >= QueryDuration(); }
    [[nodiscard]] std::string_view        QueryName()     const noexcept { return Name; }

    static const char* const* PullNames(uint32_t* Count) noexcept;

private:
    void Sample(float Time) noexcept;

    std::vector<DynoKeyframe> Keyframes;
    PowertrainRecord          Record;
    std::string_view          Name    = "sweep";
    float                     Elapsed = 0.0f;   // [s]
};

} // namespace Frontier
