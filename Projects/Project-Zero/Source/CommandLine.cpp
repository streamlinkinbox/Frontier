//============================================================================================================================================
//                                   📦 Projects/Project-Zero/Source/CommandLine.cpp — P4
//============================================================================================================================================
#include "CommandLine.h"

#include "../../../Engine/ContentInterchange/SpaceFormat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Frontier
{
namespace
{
    bool StartsWith(const std::string& Text, const char* Prefix)
    {
        const size_t Length = std::strlen(Prefix);
        return Text.size() >= Length && std::memcmp(Text.data(), Prefix, Length) == 0;
    }

    // "-Project=X" and "-Project X" are both accepted: the plan writes the first, and a shell that splits on spaces
    //    produces the second.
    bool TakeValue(const std::vector<std::string>& Arguments, size_t& At, const char* Name, std::string& OutValue)
    {
        const std::string Flag = std::string("-") + Name;
        const std::string& Current = Arguments[At];
        if (Current == Flag)
        {
            if (At + 1u >= Arguments.size()) return false;
            OutValue = Arguments[++At];
            return true;
        }
        if (StartsWith(Current, (Flag + "=").c_str()))
        {
            OutValue = Current.substr(Flag.size() + 1u);
            return true;
        }
        return false;
    }

    // Trailing-alias helper: a split comma list, empty entries dropped.
    std::vector<std::string> SplitList(const std::string& Text)
    {
        std::vector<std::string> Out;
        std::string Current;
        for (char C : Text)
        {
            if (C == ',') { if (!Current.empty()) Out.push_back(Current); Current.clear(); }
            else Current.push_back(C);
        }
        if (!Current.empty()) Out.push_back(Current);
        return Out;
    }
} // namespace

bool ParsePlusVector(const std::string& Argument, const char* Name, float Out[3]) noexcept
{
    // +Location=(0,-6.0,2.6) — a positional override, so the parser is deliberately strict: a malformed one is not
    //    silently ignored, because a camera that did not move is much harder to notice than a refused argument.
    const std::string Prefix = std::string("+") + Name + "=(";
    if (!StartsWith(Argument, Prefix.c_str())) return false;
    if (Argument.back() != ')') return false;
    const std::string Body = Argument.substr(Prefix.size(), Argument.size() - Prefix.size() - 1u);
    const std::vector<std::string> Parts = SplitList(Body);
    if (Parts.size() != 3u) return false;
    for (int I = 0; I < 3; ++I)
    {
        char* End = nullptr;
        Out[I] = std::strtof(Parts[I].c_str(), &End);
        if (End == Parts[I].c_str() || *End != '\0') return false;
    }
    return true;
}

uint32_t CommandOptions::PackPolicy() const noexcept
{
    uint32_t Policy = kSpacePackEmbedSmall;   // the plan's default: small payloads are embedded
    for (const std::string& What : Embed)
    {
        if (What == "Assets") Policy |= kSpacePackEmbedAssets;
        else if (What == "Fonts") Policy |= kSpacePackEmbedFonts;
        else if (What == "Engine") Policy |= kSpacePackEmbedEngine;
        else if (What == "All")
            Policy |= kSpacePackEmbedAssets | kSpacePackEmbedFonts | kSpacePackEmbedEngine;
    }
    return Policy;
}

std::string CommandOptions::Describe() const noexcept
{
    std::string Out = "Build=";
    Out += Build == CommandBuild::Editor ? "Editor" : "Game";
    Out += " Config=";
    Out += Config == CommandConfig::Debug ? "Debug" : (Config == CommandConfig::Shipping ? "Shipping" : "Development");
    if (!Project.empty()) Out += " Project=" + Project;
    else if (!ProjectPath.empty()) Out += " Project=" + ProjectPath;
    if (!Level.empty()) Out += " Level=" + Level;
    if (Pack) Out += " Pack";
    if (Explode) Out += " Explode";
    if (BakeSky) Out += " Bake=Sky";
    if (!Export.empty()) Out += " Export=" + Export;
    for (const std::string& What : Embed) Out += " Embed=" + What;
    if (HasLocation) Out += " +Location";
    if (HasRotation) Out += " +Rotation";
    if (!SceneAlias.empty()) Out += " --scene " + SceneAlias;
    if (HasScale) Out += " --scale";
    return Out;
}

bool ParseCommandLine(int ArgumentCount, char** Arguments, const std::string& DefaultProject,
                      const std::string& ContentRoot, CommandOptions& Out, std::string& OutError) noexcept
{
    Out = CommandOptions{};
    OutError.clear();
    std::vector<std::string> Arguments_(Arguments + 1, Arguments + ArgumentCount);

    const auto ResolveProject = [&](const std::string& Value) {
        // A path (or anything with an extension) is taken as written; a bare name resolves to the convention.
        if (Value.find('/') != std::string::npos || Value.find('\\') != std::string::npos ||
            Value.find(".projectspace") != std::string::npos)
        {
            Out.ProjectPath = Value;
            Out.Project = Value;
            return;
        }
        Out.Project = Value;
        Out.ProjectPath = (ContentRoot.empty() ? std::string("Projects") : ContentRoot) + "/" + Value + "/" + Value + ".projectspace";
    };

    for (size_t At = 0u; At < Arguments_.size(); ++At)
    {
        const std::string& Argument = Arguments_[At];
        std::string Value;

        if (Argument == "-h" || Argument == "--help" || Argument == "-?") { Out.WantsHelp = true; continue; }
        if (TakeValue(Arguments_, At, "Project", Value)) { ResolveProject(Value); continue; }
        if (TakeValue(Arguments_, At, "Level", Value))   { Out.Level = Value; Out.LevelExplicit = 1u; continue; }
        if (TakeValue(Arguments_, At, "Build", Value))
        {
            if (Value == "Editor") Out.Build = CommandBuild::Editor;
            else if (Value == "Game") Out.Build = CommandBuild::Game;
            else { OutError = "-Build must be Editor or Game, not '" + Value + "'"; return false; }
            continue;
        }
        if (TakeValue(Arguments_, At, "Config", Value))
        {
            if (Value == "Debug") Out.Config = CommandConfig::Debug;
            else if (Value == "Development") Out.Config = CommandConfig::Development;
            else if (Value == "Shipping") Out.Config = CommandConfig::Shipping;
            else { OutError = "-Config must be Debug, Development or Shipping, not '" + Value + "'"; return false; }
            continue;
        }
        if (TakeValue(Arguments_, At, "Bake", Value))
        {
            if (Value == "Sky") { Out.BakeSky = true; continue; }
            OutError = "-Bake only knows Sky, not '" + Value + "'";
            return false;
        }
        if (TakeValue(Arguments_, At, "Export", Value)) { Out.Export = Value.empty() ? "All" : Value; continue; }
        if (TakeValue(Arguments_, At, "Embed", Value))
        {
            for (const std::string& What : SplitList(Value))
            {
                if (What != "Assets" && What != "Fonts" && What != "Engine" && What != "All")
                {
                    OutError = "-Embed only knows Assets, Fonts, Engine and All, not '" + What + "'";
                    return false;
                }
                Out.Embed.push_back(What);
            }
            continue;
        }
        if (Argument == "-Pack")    { Out.Pack = true; continue; }
        if (Argument == "-Explode") { Out.Explode = true; continue; }
        if (Argument == "-silent")  { Out.Silent = true; continue; }
        if (Argument == "-game")    { Out.Game = true; continue; }

        // The `+` overrides. They take no `-` name and must parse exactly, or the caller hears about it.
        if (StartsWith(Argument, "+Location="))
        {
            if (!ParsePlusVector(Argument, "Location", Out.Location))
            {
                OutError = "+Location must be (x,y,z), not '" + Argument + "'";
                return false;
            }
            Out.HasLocation = true;
            continue;
        }
        if (StartsWith(Argument, "+Rotation="))
        {
            if (!ParsePlusVector(Argument, "Rotation", Out.Rotation))
            {
                OutError = "+Rotation must be (pitch,yaw,roll), not '" + Argument + "'";
                return false;
            }
            Out.HasRotation = true;
            continue;
        }

        // The compatibility aliases (plan §6). They land on the same fields the project path fills, so a proof can
        //    switch from `--scene materials` to `-Project=Project-Zero -Level=Materials` and compare images.
        if (TakeValue(Arguments_, At, "-scene", Value) || TakeValue(Arguments_, At, "scene", Value))
        {
            Out.SceneAlias = Value;
            continue;
        }
        if (Argument == "--scale" || Argument == "-scale")
        {
            if (At + 1u >= Arguments_.size()) { OutError = "--scale needs a value"; return false; }
            Out.Scale = std::strtof(Arguments_[++At].c_str(), nullptr);
            Out.HasScale = true;
            continue;
        }
        if (StartsWith(Argument, "--scale="))
        {
            Out.Scale = std::strtof(Argument.c_str() + 8u, nullptr);
            Out.HasScale = true;
            continue;
        }

        // Unknown: recorded, not fatal. A tool that launches the game passes flags the game owns, and refusing them
        //    here would make this parser the arbiter of every future flag.
        Out.Recognised = false;
        Out.Unknown.push_back(Argument);
    }

    if (Out.Project.empty() && Out.SceneAlias.empty() && !DefaultProject.empty()) ResolveProject(DefaultProject);
    if (Out.Build == CommandBuild::Game && Out.Game) Out.Build = CommandBuild::Game;
    return true;
}
}
