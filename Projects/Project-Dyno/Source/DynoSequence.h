//============================================================================================================================================
//                                                        DYNOSEQUENCE.H
//============================================================================================================================================
// 🧩 Scripted dyno pulls: deterministic rpm / throttle timelines the dyno cell plays in place of a physics powertrain.
//    Each pull is a list of keyframes; Advance(Δτ) walks them and QueryRecord() returns the demand a game would produce.
//    The same pulls drive the realtime device and the offline --render, which is what makes the two outputs comparable —
//    and they are the same keyframes as `DynoSequence` in Tools/AudioEditor/index.html, so the editor, the JavaScript
//    reference dumps and this binary play one script (Scratchpad/AcousticIdentityTest holds them to ±1 × 10⁻⁶ per sample).
//
//    Pulls (seven, row A2 set)
//        idle       idle hold, closed throttle, 10 s
//        sweep      linear 1 000 → redline → 1 000 rpm over 12 s, half throttle
//        pull       WOT: 2 000 → redline in 6 s, hold 1 s, lift (overrun back to idle over 5 s)
//        steady     4 000 rpm hold, 40 % throttle
//        blip       three throttle stabs from idle (rev-match rehearsal)
//        overrun    3 500 → 85 % redline WOT, then a 6 s closed-throttle descent (the crackle script)
//        limiter    4 s on the fuel cut at redline (the cut phases at 14 Hz, as ScriptedRecord derives them)
//
//    Load rule: a scripted pull prescribes rpm and throttle; ScriptedRecord derives the load the way the editor's
//    `scriptedRecord` does — 0 on overrun (throttle shut above 1.6 × idle) and in the limiter's cut phases, else the
//    larger of the throttle and the idle-governor load (friction / peak torque, ≤ 0.3). Doubles throughout: the record is
//    part of the identity arithmetic (see Engine/PlatformInterchange/PowertrainRecord.h).

#pragma once

#include "../../../Engine/PlatformInterchange/AcousticStructure.h"
#include "../../../Engine/PlatformInterchange/PowertrainRecord.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   DYNO SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

struct DynoKeyframe
{
    double Time      = 0.0;    // [s]
    double Rpm       = 900.0;  // [rpm]
    double Throttle  = 0.0;    // [-]
};

class DynoSequence
{
public:
    DynoSequence() noexcept;

    // Selects a named pull; unknown names fall back to "sweep" and return false. IdleRpm ≤ 0 → 900.
    bool Select(std::string_view Name, double RedlineRpm, double IdleRpm = 900.0) noexcept;

    void Advance(double Δτ) noexcept;
    void Restart() noexcept { Elapsed = 0.0; Sample(0.0); }

    // The prescribed rpm / throttle at the current time (Load = Throttle here — use ScriptedRecord for the dyno's load rule).
    [[nodiscard]] const PowertrainRecord& QueryRecord()   const noexcept { return Record; }
    [[nodiscard]] double                  QueryRpm()      const noexcept { return Record.Rpm; }
    [[nodiscard]] double                  QueryThrottle() const noexcept { return Record.Throttle; }
    [[nodiscard]] double                  QueryElapsed()  const noexcept { return Elapsed; }
    [[nodiscard]] double                  QueryDuration() const noexcept { return Keyframes.empty() ? 0.0 : Keyframes.back().Time; }
    [[nodiscard]] bool                    Finished()      const noexcept { return Elapsed >= QueryDuration(); }
    [[nodiscard]] std::string_view        QueryName()     const noexcept { return Name; }

    // The editor's scriptedRecord(pull, vehicle, time): rpm and throttle as prescribed, load per the rule above.
    [[nodiscard]] static PowertrainRecord ScriptedRecord(double Rpm, double Throttle, const AcousticStructure::VehicleSheet& Vehicle, double Time) noexcept;

    static const char* const* PullNames(uint32_t* Count) noexcept;

private:
    void Sample(double Time) noexcept;

    std::vector<DynoKeyframe> Keyframes;
    PowertrainRecord          Record;
    std::string_view          Name    = "sweep";
    double                    Elapsed = 0.0;   // [s]
};

} // namespace Frontier
