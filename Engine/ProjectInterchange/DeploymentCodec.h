//============================================================================================================================================
//                                                       DEPLOYMENTCODEC.H
//============================================================================================================================================
// 📦 Reads the optional [DeploymentPoint] section of a project's .frontier stream without changing the host ABI.

#pragma once
#include "DeploymentPoint.h"
#include <iomanip>
#include <istream>
#include <set>
#include <sstream>

namespace Frontier
{
inline bool DecodeDeploymentPoint(std::istream& Input, DeploymentPoint& Destination, std::string& Refusal)
{
    DeploymentPoint Candidate = Destination;
    bool Active = false, Seen = false;
    std::set<std::string> Assigned;
    std::string Line;
    uint32_t Number = 0u;
    const auto Trim = [](std::string Text)
    {
        const auto Begin = Text.find_first_not_of(" \t\r\n");
        return Begin == std::string::npos ? std::string{} : Text.substr(Begin, Text.find_last_not_of(" \t\r\n") - Begin + 1u);
    };
    const auto Refuse = [&](const std::string& Reason)
    {
        Refusal = "DeploymentPoint line " + std::to_string(Number) + ": " + Reason;
        return false;
    };
    while (std::getline(Input, Line))
    {
        ++Number;
        bool Quoted = false, Escaped = false;
        for (size_t Index = 0u; Index < Line.size(); ++Index)
        {
            const char Character = Line[Index];
            if (Character == '"' && !Escaped) Quoted = !Quoted;
            if (Character == '#' && !Quoted) { Line.resize(Index); break; }
            Escaped = Character == '\\' && !Escaped;
        }
        Line = Trim(Line);
        if (Line.empty()) continue;
        if (Line.front() == '[' && Line.back() == ']')
        {
            Active = Line == "[DeploymentPoint]";
            if (Active && Seen) return Refuse("repeated section");
            Seen = Seen || Active;
            continue;
        }
        if (!Active) continue;
        const auto Split = Line.find('=');
        if (Split == std::string::npos) return Refuse("expected name = value");
        const std::string Key = Trim(Line.substr(0u, Split));
        const std::string Value = Trim(Line.substr(Split + 1u));
        if (!Assigned.insert(Key).second) return Refuse("repeated property " + Key);
        std::istringstream Stream(Value);
        bool Parsed = true;
        if (Key == "DeleteAfterSpawn" || Key == "Enabled")
        {
            if (Value != "true" && Value != "false") return Refuse("expected true or false");
            (Key == "Enabled" ? Candidate.Enabled : Candidate.DeleteAfterSpawn) = Value == "true";
        }
        else if (Key == "Position" || Key == "Rotation")
        {
            char Delimiter = 0;
            Parsed = bool(Stream >> Delimiter) && Delimiter == '[';
            const size_t Count = Key == "Position" ? 3u : 4u;
            for (size_t Index = 0u; Parsed && Index < Count; ++Index)
            {
                float Component = 0;
                Parsed = bool(Stream >> Component >> Delimiter) && Delimiter == (Index + 1u == Count ? ']' : ',');
                if (Key == "Position") Candidate.Position[Index] = Component;
                else Candidate.Rotation[Index] = Component;
            }
            Parsed = Parsed && (Stream >> std::ws).eof();
        }
        else if (Key == "PlayerIdentifier" || Key == "Team")
        {
            uint64_t Integer = 0u;
            Parsed = !Value.empty() && Value.front() != '-' && bool(Stream >> Integer) && (Stream >> std::ws).eof();
            if (Key == "Team")
            {
                Parsed = Parsed && Integer <= std::numeric_limits<uint32_t>::max();
                Candidate.Player.Team = static_cast<uint32_t>(Integer);
            }
            else Candidate.Player.Identifier = Integer;
        }
        else
        {
            std::string Text;
            Parsed = !Value.empty() && Value.front() == '"' && bool(Stream >> std::quoted(Text)) && (Stream >> std::ws).eof();
            if (Key == "Name") Candidate.Name = Text;
            else if (Key == "Archetype") Candidate.Archetype = Text;
            else if (Key == "PlayerName") Candidate.Player.DisplayName = Text;
            else if (Key.starts_with("Player.") && Key.size() > 7u) Candidate.Player.Properties[Key.substr(7u)] = Text;
            else return Refuse("unknown property " + Key);
        }
        if (!Parsed) return Refuse("invalid value for " + Key);
    }
    if (Input.bad() || !DeploymentSequence::Validate(Candidate)) return Refuse("invalid player identity or spawn pose");
    Destination = std::move(Candidate);
    Refusal.clear();
    return true;
}
}
