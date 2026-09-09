//============================================================================================================================================
// 📦 ParametricSketcher/Console/SolidArcConsole.cpp — Entry point: `SolidArc script.arc [...]`, `SolidArc -c "cmd; cmd"`, or REPL on stdin
//============================================================================================================================================

#include "ConsoleHost.h"
#include <cstring>

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;
    std::string ProofDirectory = SOLIDARC_PROOF_DIRECTORY;
    bool ContinueOnRefusal = false;
    std::vector<std::string> Scripts, Inline;
    for (int I = 1; I < ArgumentCount; ++I)
    {
        std::string A = Arguments[I];
        if (A == "--continue") ContinueOnRefusal = true;
        else if (A == "--proofs" && I + 1 < ArgumentCount) ProofDirectory = Arguments[++I];
        else if (A == "-c" && I + 1 < ArgumentCount) Inline.push_back(Arguments[++I]);
        else if (A == "--help") { std::printf("SolidArc [--continue] [--proofs DIR] [-c \"commands\"] [script.arc ...]\n  No arguments: interactive console on stdin.\n"); return 0; }
        else Scripts.push_back(A);
    }

    ConsoleHost Host(ProofDirectory);
    bool Ok = true;
    for (const std::string& S : Scripts) Ok = Host.RunScript(S, ContinueOnRefusal) && Ok;
    for (const std::string& C : Inline) Ok = Host.Execute(C) && Ok;
    if (Scripts.empty() && Inline.empty()) return Host.RunInteractive(stdin);
    return Ok ? 0 : 1;
}
