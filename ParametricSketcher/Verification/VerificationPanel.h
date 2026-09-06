//============================================================================================================================================
// 📦 ParametricSketcher/Verification/VerificationPanel.h — Console proof reporting: named checks, tolerances, summary, exit code
//============================================================================================================================================
// Every verification executable prints a table: CHECK · measured · limit · verdict. Failure count is the process exit
//    code, which is what ctest reads. No framework, no macros beyond one for source location.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <string>

namespace Frontier
{

class VerificationPanel
{
public:
    explicit VerificationPanel(const char* Title) noexcept
    {
        std::printf("\n══════════════════════════════════════════════════════════════════════════════════════════════════════════\n");
        std::printf("  %s\n", Title);
        std::printf("══════════════════════════════════════════════════════════════════════════════════════════════════════════\n");
        std::printf("  %-58s %16s %12s   %s\n", "CHECK", "MEASURED", "LIMIT", "VERDICT");
        std::printf("  ──────────────────────────────────────────────────────────────────────────────────────────────────────\n");
    }

    void Section(const char* Name) noexcept
    {
        std::printf("\n  ── %s\n", Name);
    }

    // Measured must be ≤ Limit (absolute error style).
    bool Within(const char* Name, double Measured, double Limit) noexcept
    {
        bool Verdict = std::isfinite(Measured) && Measured <= Limit;
        Report(Name, Measured, Limit, Verdict);
        return Verdict;
    }

    bool Expect(const char* Name, bool Condition) noexcept
    {
        std::printf("  %-58s %16s %12s   %s\n", Name, Condition ? "true" : "false", "true", Condition ? "PASS" : "FAIL ◄");
        ++Total; if (!Condition) ++Failed;
        return Condition;
    }

    bool Equal(const char* Name, double Measured, double Expected, double Tolerance) noexcept
    {
        double Error = std::fabs(Measured - Expected);
        bool Verdict = std::isfinite(Error) && Error <= Tolerance;
        char Text[256];
        std::snprintf(Text, sizeof Text, "%s  (got %.12g, want %.12g)", Name, Measured, Expected);
        Report(Text, Error, Tolerance, Verdict);
        return Verdict;
    }

    void Note(const char* Format, ...) noexcept
    {
        va_list Arguments;
        va_start(Arguments, Format);
        std::printf("     · ");
        std::vprintf(Format, Arguments);
        std::printf("\n");
        va_end(Arguments);
    }

    int Conclude() noexcept
    {
        std::printf("\n  ──────────────────────────────────────────────────────────────────────────────────────────────────────\n");
        std::printf("  %d checks, %d failed → %s\n\n", Total, Failed, Failed == 0 ? "ALL PASS" : "FAILURES PRESENT");
        return Failed;
    }

private:
    int Total = 0;                                                                      // [count]
    int Failed = 0;                                                                     // [count]

    void Report(const char* Name, double Measured, double Limit, bool Verdict) noexcept
    {
        std::printf("  %-58.58s %16.3e %12.1e   %s\n", Name, Measured, Limit, Verdict ? "PASS" : "FAIL ◄");
        ++Total; if (!Verdict) ++Failed;
    }
};

} // namespace Frontier
