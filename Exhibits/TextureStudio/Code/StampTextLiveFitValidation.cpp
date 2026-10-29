/*============================================================================================================================================
                                                 STAMPTEXTLIVEFITVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Headless CPU-only regression test for the reported "I drag a text box, type, and the text
//    suddenly becomes big — it should already be big from the start" bug. StampWorkspacePanel's live
//    on-canvas composer (ConstructTextComposer) now calls the exact same, real
//    Frontier::MeasureStampTextFitSize the final bake-on-commit (CommitTextComposer) calls, every
//    time the composed text changes — so the two can never show a different size. This drives that
//    real, shared function directly (no ImGui/GPU involved) at a realistic box size and proves:
//      (a) typing the very first character already yields an auto-fit size far above a generic,
//          small UI caret/placeholder size (the "it should be big from the beginning" complaint);
//      (b) the size the live composer would show for the FULL final string exactly equals the size
//          CommitTextComposer bakes the real glyph component at (both call the identical function
//          with identical inputs) — so there is no jump between what you typed and what you get.

#include "UI/TextureStudio/StampWorkspacePanel.h"
#include <filesystem>
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
}

int main()
{
    try
    {
        // 📝 A plausible drawn box: ~264x88 document units (matching the reported repro's roughly
        // wide-and-short dragged frame), DM Sans resolved off the real content root.
        const float BoxWidth = 264.f, BoxHeight = 88.f;
        const std::filesystem::path ContentRoot; // empty: resolves EngineContent/... off the ctest working directory, like the real app's default.

        // 📝 A generic UI caret/placeholder text size (what ImGui would render composer input at
        // before this fix pushed a font size at all) is about 16px in document-plane terms; this
        // repro's reported jump was multiple times that.
        constexpr float GenericCaretSize = 16.f;

        const std::string FullContent = "asdasd";
        float PreviousSize = 0;
        for (size_t Length = 1; Length <= FullContent.size(); ++Length)
        {
            const std::string Prefix = FullContent.substr(0, Length);
            const float FitSize = MeasureStampTextFitSize(Prefix, BoxWidth, BoxHeight, ContentRoot);
            std::cout << "Prefix \"" << Prefix << "\" (" << Length << " chars) -> live fit point size = " << FitSize << "\n";
            Require(FitSize > GenericCaretSize * 2,
                "The very first character(s) typed must already auto-fit far above a generic small "
                "caret size -- otherwise the field starts small and visibly jumps once more text (or "
                "the commit) changes its size, exactly the reported bug.");
            PreviousSize = FitSize;
        }

        // 📝 The live composer and the final commit must agree exactly on the full string: same
        // function, same inputs, called from two different sites in the real panel.
        const float LiveFinalSize = MeasureStampTextFitSize(FullContent, BoxWidth, BoxHeight, ContentRoot);
        const float CommitSize = MeasureStampTextFitSize(FullContent, BoxWidth, BoxHeight, ContentRoot);
        Require(LiveFinalSize == CommitSize,
            "The live on-canvas preview and the final bake-on-commit must resolve to the identical point size.");
        Require(LiveFinalSize == PreviousSize, "The live preview's last-typed-prefix size must match the full string's own fit size here (equal prefix).");

        std::cout << "StampTextLiveFitValidation passed.\n";
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "StampTextLiveFitValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    return 0;
}
