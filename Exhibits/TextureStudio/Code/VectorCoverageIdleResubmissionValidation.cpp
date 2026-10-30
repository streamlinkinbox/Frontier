/*============================================================================================================================================
                                           VECTORCOVERAGEIDLERESUBMISSIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Headless CPU-only regression test for the reported "Pattern Studio shows a 'Baking pattern...'
//    badge while nothing is actually baking" bug. Drives the real, pure, stateless
//    VectorAuthoringCoverageRendering::RequiresUnchangedResubmission extracted out of
//    SelectProgram/SelectSurfaceProgram's "content unchanged" branch, plus a frame-by-frame
//    simulation of the exact Completed/SampledSerial bookkeeping ConstructCommands and
//    ConfirmSubmission perform (a pipelined device: Completed only catches up a few frames behind
//    whatever serial was last submitted), to prove:
//      (a) the OLD behaviour (always resubmit unchanged, already-baked content every frame) makes
//          QuerySelectedReady()'s own formula (!Pending && SampledSerial != 0 && SampledSerial <=
//          Completed) stay false forever on an idle, unedited canvas -- the "Baking..." badge never
//          clears, matching the reported bug exactly;
//      (b) the NEW behaviour (RequiresUnchangedResubmission) only resubmits a slot's very first bake
//          or an actual re-tile, so Ready settles to true a few frames after the first submission and
//          stays true for as long as nothing changes -- the badge appears only while real work is
//          outstanding, then clears and stays cleared.

#include "Engine/Render/VectorAuthoringCoverageRendering.h"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Frontier;

namespace
{
void Require(bool Condition, const std::string &Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

// 📝 QuerySelectedReady()'s own formula, reproduced here verbatim (it is a pure read of three plain
// fields -- no GPU access -- see VectorAuthoringCoverageRendering.cpp) so the simulation can report
// exactly what the live "Baking..." badge condition would observe each frame.
bool Ready(bool Pending, uint64_t SampledSerial, uint64_t Completed)
{
    return !Pending && SampledSerial != 0 && SampledSerial <= Completed;
}

// 📝 Simulates Frames of an idle canvas (unchanged content, no re-tile ever) under either the OLD
// policy (SampleRequested always true for unchanged content) or the NEW one
// (RequiresUnchangedResubmission). Mirrors ConstructCommands/ConfirmSubmission's real bookkeeping:
// Completed always advances toward (Frame - Lag) regardless of new submissions (the device retiring
// whatever is already in its queue), while SampledSerial only moves when this frame actually submits.
uint32_t FramesUntilReady(bool UseFix, uint32_t Frames, uint32_t Lag)
{
    uint64_t Completed = 0, SampledSerial = 0;
    uint32_t FirstReadyFrame = UINT32_MAX;
    for (uint32_t Frame = 1; Frame <= Frames; ++Frame)
    {
        const bool Retiled = false; // idle canvas: never a real pooled-tier change.
        const bool SampleRequested = UseFix ? VectorAuthoringCoverageRendering::RequiresUnchangedResubmission(Retiled, SampledSerial) : true;
        if (SampleRequested) SampledSerial = Frame; // ConfirmSubmission would stamp this frame's own submission serial.
        Completed = Frame > Lag ? Frame - Lag : 0; // ConstructCommands always advances Completed from the device's own fence progress.
        if (Ready(false, SampledSerial, Completed) && FirstReadyFrame == UINT32_MAX) FirstReadyFrame = Frame;
    }
    return FirstReadyFrame;
}
}

int main()
{
    try
    {
        // 📝 Truth table for the extracted decision itself.
        Require(VectorAuthoringCoverageRendering::RequiresUnchangedResubmission(true, 0) == true,
            "A real re-tile must always resubmit.");
        Require(VectorAuthoringCoverageRendering::RequiresUnchangedResubmission(true, 42) == true,
            "A real re-tile must always resubmit, even after prior successful bakes.");
        Require(VectorAuthoringCoverageRendering::RequiresUnchangedResubmission(false, 0) == true,
            "A slot that has never completed a bake must still submit once.");
        Require(VectorAuthoringCoverageRendering::RequiresUnchangedResubmission(false, 7) == false,
            "Unchanged, already-baked content must not force a fresh GPU submission every frame "
            "-- this is exactly the resubmission the reported stuck-badge bug depended on.");

        // 📝 A realistic double/triple-buffered device: the completed fence trails 2 frames behind
        // whatever was last submitted.
        constexpr uint32_t Lag = 2, Frames = 60;
        const uint32_t OldReadyFrame = FramesUntilReady(false, Frames, Lag);
        const uint32_t NewReadyFrame = FramesUntilReady(true, Frames, Lag);
        std::cout << "OLD policy (always resubmit unchanged content): first ready frame = "
            << (OldReadyFrame == UINT32_MAX ? std::string("never (stuck for all ") + std::to_string(Frames) + " frames)" : std::to_string(OldReadyFrame)) << "\n";
        std::cout << "NEW policy (RequiresUnchangedResubmission):     first ready frame = "
            << (NewReadyFrame == UINT32_MAX ? std::string("never") : std::to_string(NewReadyFrame)) << "\n";

        Require(OldReadyFrame == UINT32_MAX,
            "Expected the OLD always-resubmit policy to reproduce the reported bug: QuerySelectedReady() "
            "never settles true on an idle, unedited canvas, so the 'Baking...' badge never clears.");
        Require(NewReadyFrame != UINT32_MAX && NewReadyFrame <= Lag + 1,
            "Expected the fixed policy to settle to ready within one submission's device lag on an idle canvas.");

        // 📝 Once ready, it must STAY ready for as long as nothing changes -- not flap back to pending.
        {
            uint64_t Completed = 0, SampledSerial = 0;
            bool EverUnready = false;
            for (uint32_t Frame = 1; Frame <= Frames; ++Frame)
            {
                const bool SampleRequested = VectorAuthoringCoverageRendering::RequiresUnchangedResubmission(false, SampledSerial);
                if (SampleRequested) SampledSerial = Frame;
                Completed = Frame > Lag ? Frame - Lag : 0;
                if (Frame > NewReadyFrame && !Ready(false, SampledSerial, Completed)) EverUnready = true;
            }
            Require(!EverUnready, "Fixed policy flapped back to not-ready on an idle canvas after first settling.");
        }
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "VectorCoverageIdleResubmissionValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    std::cout << "VectorCoverageIdleResubmissionValidation passed.\n";
    return 0;
}
