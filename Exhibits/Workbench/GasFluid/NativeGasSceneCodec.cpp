//==============================================================================================================================================
//                                                        NATIVEGASSCENECODEC.CPP
//==============================================================================================================================================
// 📦 Executed proof for the scene crossing: the engine reads the browser's own corpus, and writes it back byte for byte.
//
// 🔴 THE CLAIM IS BYTE EQUALITY, NOT STRUCTURAL EQUALITY, AND THE DIFFERENCE IS THE WHOLE POINT.
//    Parsing a file and comparing the resulting settings would pass while the two emitters disagreed about
//    how to spell a number, how to order the lines, or how many decimal places to keep. That disagreement is
//    invisible in a structural comparison and extremely visible in version control, where every save from a
//    different tool rewrites the whole corpus. So this reads each committed sample, writes it back out, and
//    compares the text.
//
// What the corpus is: Experimental/Fluid/Samples/*.gasscene.toml, written by the browser authoring tool and
//    committed. Not generated here — a check that regenerates its own fixture cannot notice the fixture
//    changing, which is exactly what it is for.
//
// ⚠️ The sample *content* is deferred work; see Plans/Deferred/GasSampleSceneRefinement.md. This proof is
//    about the crossing, not the tuning, and is unaffected by the scenes being retuned later. It walks
//    whatever is in the folder.

#include "CoarseGasField.h"
#include "GasPresetLibrary.h"
#include "GasSceneCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

unsigned Checks = 0;

void Check(bool Condition, const char* Claim)
{
    ++Checks;
    std::printf("  %s  %s\n", Condition ? "PASS" : "FAIL", Claim);
    if (!Condition) throw std::runtime_error(Claim);
}

void Banner(const char* Title)
{
    std::printf("\n%s\n", Title);
}

std::string Slurp(const std::filesystem::path& Path)
{
    std::ifstream Stream(Path, std::ios::binary);
    std::ostringstream Collected;
    Collected << Stream.rdbuf();
    return Collected.str();
}

// The first line the two files differ on, so a failure says where rather than that.
std::string FirstDifference(const std::string& Left, const std::string& Right)
{
    std::istringstream Mine(Left), Theirs(Right);
    std::string MyLine, TheirLine;
    unsigned Number = 0u;
    while (true)
    {
        const bool MineLeft = static_cast<bool>(std::getline(Mine, MyLine));
        const bool TheirsLeft = static_cast<bool>(std::getline(Theirs, TheirLine));
        ++Number;
        if (!MineLeft && !TheirsLeft) return {};
        if (MineLeft != TheirsLeft || MyLine != TheirLine)
            return "line " + std::to_string(Number) + ": engine wrote [" + (MineLeft ? MyLine : "<end>")
                 + "], the tool wrote [" + (TheirsLeft ? TheirLine : "<end>") + "]";
    }
}

}   // namespace


int main()
{
    std::printf("Gas scene codec checks - the engine against the authoring tool's own corpus\n");

    try
    {

    //----------------------------------------------------------------------------------------------------------------
    //                                              REFUSALS COME FIRST
    //----------------------------------------------------------------------------------------------------------------

    Banner("What the reader refuses, and what it merely reports");

    {
        GasScene Scene;

        Check(!ReadGasScene("this is not toml at all {{{", Scene).Accepted,
              "unparseable text is refused rather than crashing");
        Check(!ReadGasScene("", Scene).Accepted, "and so is an empty file");
        Check(!ReadGasScene("format = \"something-else\"\nversion = 1\nname = \"x\"\n", Scene).Accepted,
              "a file that is not a gas scene is refused by its format marker");
        Check(!ReadGasScene("format = \"frontier-fluid-scene\"\nversion = 9\nname = \"x\"\n", Scene).Accepted,
              "a version this build does not read is refused rather than guessed at");
        Check(!ReadGasScene("format = \"frontier-fluid-scene\"\nversion = 1\n", Scene).Accepted,
              "a scene with no name is refused");
        Check(!ReadGasScene("format = \"frontier-fluid-scene\"\nversion = 1\nname = \"\"\n", Scene).Accepted,
              "and so is an empty one");

        const GasSceneReading Unparseable = ReadGasScene("= = =", Scene);
        Check(!Unparseable.Refusal.empty(), "a refusal carries a sentence rather than only a false");

        // 🔴 The type rule. An integer is accepted where a float is wanted; a float is never accepted where a
        //    count is wanted, because 64.5 voxels is not a lattice and truncating it hides the mistake.
        const char* Terse = "format = \"frontier-fluid-scene\"\nversion = 1\nname = \"terse\"\n"
                            "[settings]\nexposure = 1\n";
        GasScene Read;
        const GasSceneReading Accepted = ReadGasScene(Terse, Read);
        Check(Accepted.Accepted, "an integer is accepted where a float is wanted");
        Check(Read.Settings.Exposure == 1.0f, "and arrives as the number it denotes");

        const char* Fractional = "format = \"frontier-fluid-scene\"\nversion = 1\nname = \"fractional\"\n"
                                 "[settings]\ngridResolution = 64.5\n";
        Check(!ReadGasScene(Fractional, Read).Accepted,
              "a fractional lattice is refused rather than truncated to something plausible");

        const char* Worded = "format = \"frontier-fluid-scene\"\nversion = 1\nname = \"worded\"\n"
                             "[settings]\ngridResolution = \"lots\"\n";
        const GasSceneReading Refused = ReadGasScene(Worded, Read);
        Check(!Refused.Accepted, "a setting spelled as the wrong type is refused");
        Check(Refused.RefusedSettings.size() == 1u
              && Refused.RefusedSettings.front() == "gridResolution",
              "and the refusal names the setting at fault");

        // Unknown keys are reported, not refused — a scene from a newer tool must still open.
        const char* Newer = "format = \"frontier-fluid-scene\"\nversion = 1\nname = \"from a newer tool\"\n"
                            "[settings]\ngridResolution = 96\nsomethingAddedLater = 3.5\n";
        GasScene Forward;
        const GasSceneReading Tolerated = ReadGasScene(Newer, Forward);
        Check(Tolerated.Accepted, "a scene carrying a setting this build has never heard of still opens");
        Check(Forward.Settings.LatticeResolution == 96,
              "and the settings it does understand are applied");
        Check(Tolerated.UnknownSettings.size() == 1u
              && Tolerated.UnknownSettings.front() == "somethingAddedLater",
              "while the one it does not is reported by name rather than lost silently");

        // A failed read must not half-apply. The scene handed in stays exactly as it was.
        GasScene Untouched;
        Untouched.Name = "left alone";
        Untouched.Settings.LatticeResolution = 48;
        ReadGasScene(Fractional, Untouched);
        Check(Untouched.Name == "left alone" && Untouched.Settings.LatticeResolution == 48,
              "a refused read leaves the caller's scene untouched rather than half-applied");

        const char* ShortCentre = "format = \"frontier-fluid-scene\"\nversion = 1\nname = \"bad camera\"\n"
                                  "[camera]\ncentre = [1.0, 2.0]\n";
        Check(!ReadGasScene(ShortCentre, Read).Accepted, "a camera centre of the wrong length is refused");

        // Settings absent from a file keep their default, which is what lets an older scene still open.
        const char* Sparse = "format = \"frontier-fluid-scene\"\nversion = 1\nname = \"sparse\"\n";
        GasScene Defaulted;
        Check(ReadGasScene(Sparse, Defaulted).Accepted, "a scene with no settings at all still opens");
        Check(Defaulted.Settings.LatticeResolution == GasSettings{}.LatticeResolution,
              "and every absent setting keeps its default");
        Check(!Defaulted.CameraPresent, "a scene that frames nothing says so rather than inventing a camera");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                        THE CORPUS, READ AND REWRITTEN
    //----------------------------------------------------------------------------------------------------------------

    Banner("The authoring tool's corpus, read and written back");

    {
        const std::filesystem::path Folder = "Experimental/Fluid/Samples";
        Check(std::filesystem::is_directory(Folder), "the committed corpus is where the check expects it");

        std::vector<std::filesystem::path> Samples;
        for (const auto& Entry : std::filesystem::directory_iterator(Folder))
            if (Entry.path().extension() == ".toml") Samples.push_back(Entry.path());
        std::sort(Samples.begin(), Samples.end());

        Check(!Samples.empty(), "and it is not empty");
        std::printf("    %zu samples\n", Samples.size());

        unsigned Matched = 0u;
        for (const std::filesystem::path& Path : Samples)
        {
            const std::string Authored = Slurp(Path);
            GasScene Scene;
            const GasSceneReading Reading = ReadGasScene(Authored, Scene);

            const std::string Label = Path.filename().string();
            if (!Reading.Accepted)
            {
                std::printf("    %s refused: %s\n", Label.c_str(), Reading.Refusal.c_str());
                Check(false, "every sample in the corpus is readable by the engine");
            }
            if (!Reading.UnknownSettings.empty())
            {
                std::printf("    %s carries %zu settings this build does not know, first: %s\n",
                            Label.c_str(), Reading.UnknownSettings.size(),
                            Reading.UnknownSettings.front().c_str());
                Check(false, "and carries no setting the engine has never heard of");
            }

            const std::string Rewritten = WriteGasScene(Scene);
            if (Rewritten != Authored)
            {
                std::printf("    %s differs — %s\n", Label.c_str(),
                            FirstDifference(Rewritten, Authored).c_str());
                Check(false, "and is written back byte for byte");
            }
            ++Matched;

            // Reading what was just written must give the same scene again, so the crossing is a true
            //    round-trip rather than one lucky direction.
            GasScene Again;
            Check(ReadGasScene(Rewritten, Again).Accepted, "the engine can read what the engine wrote");
            Check(WriteGasScene(Again) == Rewritten, "and writing it a second time changes nothing");
        }
        std::printf("    %u of %zu samples crossed unchanged\n", Matched, Samples.size());
        Check(Matched == Samples.size(), "every sample in the corpus crossed unchanged");
    }

    //----------------------------------------------------------------------------------------------------------------
    //                                       THE SCENE ACTUALLY DRIVES A SOLVER
    //----------------------------------------------------------------------------------------------------------------

    Banner("A crossed scene drives the solver");

    {
        // A file that only parses is not a port. The settings it carries have to reach the things that use
        //    them, so this takes one sample the whole way to a field with smoke in it.
        const std::string Authored = Slurp("Experimental/Fluid/Samples/camp_fire_steady.gasscene.toml");
        GasScene Scene;
        Check(ReadGasScene(Authored, Scene).Accepted, "the camp fire sample reads");
        Check(Scene.Name == "Camp fire, steady", "and keeps its name");
        Check(Scene.Names.Emitter == "Log fire", "and the names the outliner will show");
        Check(Scene.CameraPresent, "and the framing it was authored with");
        Check(Scene.Settings.BoundsWidth > 0.0f, "and a domain with a positive extent");

        CoarseGasField Field;
        const float Origin[3] = { 0.0f, 0.0f, 0.0f };
        ResetField(Field, Origin, Scene.Settings.BoundsWidth);

        CoarseGasSettings Solver;
        Solver.BuoyancyLift       = Scene.Settings.Buoyancy;
        Solver.SmokeWeight        = Scene.Settings.SmokeWeight;
        Solver.SmokeLossPerSecond = Scene.Settings.SmokeDissipation;
        Solver.CoolingPerSecond   = Scene.Settings.CoolingRate;
        Solver.DampingPerSecond   = Scene.Settings.VelocityDamping;

        GasEmission Source;
        Source.Position[0] = Field.Span * 0.5f;
        Source.Position[1] = Field.Span * 0.5f;
        Source.Position[2] = Field.Span * Scene.Settings.EmitterHeight;
        Source.Radius      = Field.Span * Scene.Settings.EmitterRadius;
        Source.SmokeRate   = Scene.Settings.EmitterSmoke * 30.0f;
        Source.TemperatureRate = Scene.Settings.EmitterTemperature * 4.0f;
        Source.Velocity[2] = Scene.Settings.EmitterUpwardVelocity;

        for (uint32_t Step = 0u; Step < 60u; ++Step)
        {
            InjectEmission(Field, Source, CoarseStepInterval);
            AdvanceField(Field, Solver);
        }
        Check(TotalSmoke(Field) > 0.0f, "and the scene's own settings produce smoke in the solver");
        Check(Field.AdvanceNumber == 60ull, "after the advances the check asked for");

        // The preset the sample was built from must still resolve to the same tuning, or the corpus has
        //    drifted from the library without anyone saying so.
        const GasSettings Library = ConstructPresetSettings("camp_fire");
        Check(Scene.Settings.Buoyancy == Library.Buoyancy
           && Scene.Settings.SmokeDissipation == Library.SmokeDissipation,
              "and the sample still matches the preset it was built from");
    }

    }
    catch (const std::exception& Failure)
    {
        std::printf("\nFAILED after %u checks: %s\n", Checks, Failure.what());
        return 1;
    }

    std::printf("\nPASS %u\n", Checks);
    return 0;
}
