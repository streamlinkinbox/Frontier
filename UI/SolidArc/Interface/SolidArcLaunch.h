/*====================================================================================================================================
                                                         SOLIDARCLAUNCH.H
====================================================================================================================================*/
// 🧩 SolidArcWorkspace command line: the options file, the document to open and the development switches

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <string>
#include <vector>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SolidArcLaunchRequest
{
    std::string OptionsPath = "SolidArcWorkspace.toml";                                     // [-] - --options <file>
    std::string OpenPath    = {};                                                           // [-] - --open <file>, or the bare document word
    std::string CurvesPath  = {};                                                           // [-] - --curves <file>
    bool        Specimen    = false;                                                        // [-] - --specimen
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Words[0] is the executable. Windows starts the application picked in "Open with" (or associated with .solidarc, or
//    given a file dropped on its icon) as `SolidArcWorkspace.exe "<document>"`: the document arrives as a bare word, not
//    after --open, and used to be ignored, so the workspace opened an empty document. An explicit --open wins over a bare
//    word; words that start with '-' are never taken as the document.
[[nodiscard]] inline SolidArcLaunchRequest ResolveSolidArcLaunch(const std::vector<std::string>& Words)
{
    SolidArcLaunchRequest Request;
    std::string           Bare;
    for (size_t Index = 1; Index < Words.size(); ++Index)
    {
        const std::string& Word = Words[Index];
        if (Word == "--options" && Index + 1 < Words.size()) Request.OptionsPath = Words[++Index];
        else if (Word == "--open" && Index + 1 < Words.size()) Request.OpenPath = Words[++Index];
        else if (Word == "--curves" && Index + 1 < Words.size()) Request.CurvesPath = Words[++Index];
        else if (Word == "--specimen") Request.Specimen = true;
        else if (Bare.empty() && !Word.empty() && Word.front() != '-') Bare = Word;
    }
    if (Request.OpenPath.empty()) Request.OpenPath = Bare;
    return Request;
}

} // namespace Frontier::SolidArc
