//============================================================================================================================================
//                                                       POWERTRAINRECORD.H
//============================================================================================================================================
// 🧩 The only thing a game ever feeds the acoustic integrator. Physics produces it once per main-loop Advance(Δτ); the dyno
//    cell scripts it; the integrator smooths it per sample on the realtime thread (RelayQueue<PowertrainRecord>, latest wins).
//
//    Doubles, not floats: the voice is integrated in double on both sides of the identity proof (the AudioEditor's
//    JavaScript and Engine/PlatformInterchange/AcousticIntegrator), and the demand is part of that arithmetic — a float
//    record would round the crank speed by up to 0.0005 rpm before the integrator ever sees it, which is inaudible but
//    breaks the ±1 × 10⁻⁶ per-sample agreement the C++ port is held to (References/AcousticPhaseA-CppPortPlan.md §0).
//
//    Lived in Projects/Project-Dyno/Source/DynoSequence.h through row A1; moved here for row A2 because the Engine side
//    (AcousticIntegrator) consumes it and the Engine never includes a project header.

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   POWERTRAIN RECORD
//------------------------------------------------------------------------------------------------------------------------

struct PowertrainRecord
{
    double   Rpm            = 900.0;    // [rpm]  crank speed
    double   Throttle       = 0.0;      // [-]    0 … 1 pedal
    double   Load           = 0.0;      // [-]    0 … 1 fraction of the torque the engine could make at this rpm (0 = fuel cut)
    int32_t  Gear           = 0;        // [-]    0 = neutral
    bool     ClutchEngaged  = false;    // [-]
    double   Boost          = 0.0;      // [bar]  manifold gauge pressure (forced induction only; readout, the voice derives its own spool)
};

} // namespace Frontier
