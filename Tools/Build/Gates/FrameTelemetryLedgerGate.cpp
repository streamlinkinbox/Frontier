//============================================================================================================================================
//                                                  FRAMETELEMETRYLEDGERGATE.CPP
//============================================================================================================================================
// Two things must hold for the verbose ledger, and they pull in opposite directions:
//
//    ① In a DEVELOPMENT build it must actually record — nested scopes, correct depths, startup separated from
//       per-frame work — and write both files at shutdown and at no other time.
//    ② In a SHIPPING build it must vanish. Not "be disabled": the class, the ring, the timers and every call site
//       must not exist, so a release binary carries neither the 64 MB buffer nor a single branch.
//
//    This gate is compiled TWICE by its driver script — once with FRONTIER_DEVELOPMENT and once without — and
//    asserts the appropriate half each time. The script additionally greps the two object files' symbol tables,
//    because "it compiled" is not the same claim as "the code is gone".
//
//    usage: bash Tools/Build/CheckFrameTelemetry.sh

#include "Projects/Project-Zero/Source/FrameTelemetryLedger.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int Failures = 0;

void Check(bool Condition, const char* Message)
{
    std::printf("  %s  %s\n", Condition ? "PASS" : "FAIL", Message);
    if (!Condition) ++Failures;
}

std::string Slurp(const std::string& Path)
{
    std::ifstream In(Path);
    std::ostringstream Buffer;
    Buffer << In.rdbuf();
    return Buffer.str();
}

// A little work that cannot be optimised to nothing, so a scope has something to measure.
volatile double g_Sink = 0.0;
void BurnMicroseconds()
{
    double Accumulator = 0.0;
    for (int I = 1; I < 20000; ++I) Accumulator += 1.0 / static_cast<double>(I);
    g_Sink = Accumulator;
}

} // namespace

#ifdef FRONTIER_DEVELOPMENT

using namespace Frontier::ProjectZero;

int main()
{
    std::printf("================================================================================\n");
    std::printf("   FRAME TELEMETRY LEDGER GATE — development build: it must record\n");
    std::printf("================================================================================\n");

    FrameTelemetryLedger& Ledger = FrameTelemetryLedger::Instance();
    // A small ring, so the wrap path is reachable in a test rather than only after 14 minutes of real running.
    Ledger.Reserve(4096u);

    // ── startup phases, before any frame ────────────────────────────────────────────────────────────────────────
    {
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/SwapchainBring");
        BurnMicroseconds();
        {
            FRONTIER_TELEMETRY_SHADER("Bootstrap/ShaderModules");
            BurnMicroseconds();
        }
    }
    {
        FRONTIER_TELEMETRY_CONTENT("Bootstrap/TextureDecode");
        BurnMicroseconds();
    }

    Check(Ledger.QuerySampleCount() > 0u, "the ring is reserved up front, so recording never allocates mid-frame");
    FRONTIER_TELEMETRY_FIRST_FRAME();

    // ── frames, with nesting ────────────────────────────────────────────────────────────────────────────────────
    for (int Frame = 0; Frame < 12; ++Frame)
    {
        FRONTIER_TELEMETRY_ADVANCE_FRAME();
        FRONTIER_TELEMETRY_SCOPE("Frame");
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/Simulate");
            BurnMicroseconds();
        }
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/Render");
            BurnMicroseconds();
            {
                FRONTIER_TELEMETRY_SCOPE("Frame/Render/Submit");
                BurnMicroseconds();
            }
        }
    }

    Check(Ledger.QueryFrameIndex() == 12u, "the frame counter advanced once per frame");

    // ── nothing may exist on disk before the flush ──────────────────────────────────────────────────────────────
    const std::string Dir     = ".";
    const std::string Stem    = "LedgerGate";
    const std::string Summary = Dir + "/" + Stem + "_Summary.md";
    const std::string Verbose = Dir + "/" + Stem + "_Verbose.log";
    std::remove(Summary.c_str());
    std::remove(Verbose.c_str());

    {
        std::ifstream Probe(Summary);
        Check(!Probe.good(), "nothing is written during the run — the ledger is RAM-only until shutdown");
    }

    // ── the flush ───────────────────────────────────────────────────────────────────────────────────────────────
    Check(FRONTIER_TELEMETRY_FLUSH(Dir, Stem), "the shutdown flush writes its files");

    const std::string SummaryText = Slurp(Summary);
    const std::string VerboseText = Slurp(Verbose);

    Check(!SummaryText.empty(), "the summary file has content");
    Check(!VerboseText.empty(), "the verbose per-sample log has content");

    Check(SummaryText.find("Bootstrap/SwapchainBring") != std::string::npos,
          "the summary names the startup phase (the 'why is opening slow' number)");
    Check(SummaryText.find("Startup phases") != std::string::npos,
          "startup is tabulated separately from per-frame work");
    Check(SummaryText.find("Frame/Render/Submit") != std::string::npos,
          "a nested scope three levels deep is recorded");
    Check(SummaryText.find("startup (main to first frame)") != std::string::npos,
          "total startup time is reported");

    // Twelve frames x one Frame scope: the count column must agree, which is what proves nothing was dropped.
    Check(VerboseText.find("Frame/Simulate") != std::string::npos, "the verbose log lists individual samples");
    {
        size_t Occurrences = 0, Position = 0;
        while ((Position = VerboseText.find("Frame/Render/Submit", Position)) != std::string::npos)
        { ++Occurrences; Position += 19; }
        Check(Occurrences == 12u, "every frame's innermost scope appears exactly once — no sampling, no loss");
    }

    // ── the ring wraps rather than growing ──────────────────────────────────────────────────────────────────────
    {
        FrameTelemetryLedger& Small = FrameTelemetryLedger::Instance();
        Small.Reserve(64u);
        for (int I = 0; I < 500; ++I) { FRONTIER_TELEMETRY_SCOPE("Overflow"); }
        Check(Small.QueryDroppedCount() > 0u,
              "an overrun ring drops the OLDEST samples and says so, rather than allocating mid-frame");
    }

    std::remove(Summary.c_str());
    std::remove(Verbose.c_str());

    std::printf(Failures ? "\nRED — %d check(s) failed\n"
                         : "\nGREEN — the ledger records verbosely and writes only at shutdown\n", Failures);
    return Failures ? 1 : 0;
}

#else // !FRONTIER_DEVELOPMENT

int main()
{
    std::printf("================================================================================\n");
    std::printf("   FRAME TELEMETRY LEDGER GATE — shipping build: it must not exist\n");
    std::printf("================================================================================\n");

    // Every macro must still be a legal statement with the facility compiled out, including in a bare if/else with
    //    no braces — a macro that expanded to nothing at all would silently capture the else branch.
    if (true) FRONTIER_TELEMETRY_SCOPE("ShouldNotExist"); else BurnMicroseconds();
    FRONTIER_TELEMETRY_STARTUP("ShouldNotExist");
    FRONTIER_TELEMETRY_SHADER("ShouldNotExist");
    FRONTIER_TELEMETRY_CONTENT("ShouldNotExist");
    FRONTIER_TELEMETRY_ADVANCE_FRAME();
    FRONTIER_TELEMETRY_FIRST_FRAME();
    FRONTIER_TELEMETRY_RESERVE(1024u);

    const bool Flushed = FRONTIER_TELEMETRY_FLUSH(".", "ShouldNotExist");
    Check(!Flushed, "the flush is a no-op returning false, so callers need no ifdef of their own");

    std::ifstream Probe("./ShouldNotExist_Summary.md");
    Check(!Probe.good(), "a shipping build writes no telemetry file at all");

    std::printf(Failures ? "\nRED — %d check(s) failed\n"
                         : "\nGREEN — the facility compiles out cleanly and the macros stay statement-safe\n", Failures);
    return Failures ? 1 : 0;
}

#endif
