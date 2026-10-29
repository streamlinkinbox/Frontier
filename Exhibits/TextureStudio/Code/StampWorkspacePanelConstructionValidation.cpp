/*============================================================================================================================================
                                         STAMPWORKSPACEPANELCONSTRUCTIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Regression test for a startup crash: StampWorkspacePanel's constructor builds a Stamp=true blank
//    document and immediately seeds it into its own VectorAuthoringHistory via ConfigureDocument, which
//    enforces that the history's workspace contract (its own Stamp flag) matches the document being
//    reset into it. A defaulted (Stamp=false) History member mismatched the Stamp=true blank document and
//    threw "VectorAuthoring history cannot change workspace contract." on every single launch, right after
//    TextureStudioApplication eagerly constructs the Stamp panel during Callbacks.Initialize. Pattern's own
//    Blank document leaves Stamp at its default false, so PatternWorkspacePanel never hit this. Simply
//    constructing both panels — exactly as TextureStudioApplication does unconditionally at startup — is
//    the whole repro; this test exists so that path is never silently untested again.

#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include "UI/TextureStudio/StampWorkspacePanel.h"
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
}

int main()
{
    using namespace Frontier;
    try
    {
        PatternWorkspacePanel Pattern;
        StampWorkspacePanel Stamp;
        Require(!Pattern.QueryDocument().Stamp, "PatternWorkspacePanel must open a non-Stamp blank document");
        Require(Stamp.QueryDocument().Stamp, "StampWorkspacePanel must open a Stamp blank document");
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "StampWorkspacePanelConstructionValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    std::cout << "StampWorkspacePanelConstructionValidation passed.\n";
    return 0;
}
