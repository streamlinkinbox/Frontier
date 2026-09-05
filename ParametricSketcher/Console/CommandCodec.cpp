//============================================================================================================================================
// 📦 ParametricSketcher/Console/CommandCodec.cpp — Tokeniser
//============================================================================================================================================

#include "CommandCodec.h"
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace Frontier
{

std::optional<double> CommandCodec::ParseNumber(std::string_view Token) noexcept
{
    if (Token.empty()) return std::nullopt;
    char* End = nullptr;
    std::string Copy(Token);
    double Value = std::strtod(Copy.c_str(), &End);
    if (End == Copy.c_str() || *End != '\0') return std::nullopt;
    return Value;
}

std::optional<Vec3> CommandCodec::ParsePoint(std::string_view Token) noexcept
{
    if (Token.size() < 3 || Token.front() != '(' || Token.back() != ')') return std::nullopt;
    std::string_view Inner = Token.substr(1, Token.size() - 2);
    double Values[3] = { 0, 0, 0 };
    int Count = 0;
    size_t Start = 0;
    while (Start <= Inner.size() && Count < 3)
    {
        size_t Comma = Inner.find(',', Start);
        std::string_view Piece = Inner.substr(Start, Comma == std::string_view::npos ? std::string_view::npos : Comma - Start);
        auto V = ParseNumber(Piece);
        if (!V) return std::nullopt;
        Values[Count++] = *V;
        if (Comma == std::string_view::npos) break;
        Start = Comma + 1;
    }
    if (Count < 2) return std::nullopt;
    return Vec3{ Values[0], Values[1], Values[2] };
}

bool CommandCodec::Decode(std::string_view Line, std::vector<CommandLine>& Out, std::string& Error) noexcept
{
    std::vector<std::string> Tokens;
    std::string Current;
    bool InQuote = false;
    int Depth = 0;
    auto FlushToken = [&]() { if (!Current.empty()) { Tokens.push_back(Current); Current.clear(); } };
    auto FlushCommand = [&]()
    {
        FlushToken();
        if (Tokens.empty()) return;
        CommandLine C;
        C.Verb = Tokens.front();
        for (char& Ch : C.Verb) Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
        for (size_t I = 1; I < Tokens.size(); ++I)
        {
            const std::string& T = Tokens[I];
            if (T.size() > 2 && T[0] == '-' && T[1] == '-')
            {
                size_t Eq = T.find('=');
                C.Flags.emplace_back(T.substr(2, Eq == std::string::npos ? std::string::npos : Eq - 2), Eq == std::string::npos ? "" : T.substr(Eq + 1));
            }
            else C.Arguments.push_back(T);
        }
        Out.push_back(std::move(C));
        Tokens.clear();
    };

    for (size_t I = 0; I < Line.size(); ++I)
    {
        char Ch = Line[I];
        if (InQuote)
        {
            if (Ch == '"') InQuote = false; else Current.push_back(Ch);
            continue;
        }
        if (Ch == '"') { InQuote = true; continue; }
        if (Ch == '#' && Depth == 0) break;
        if (Ch == '(') ++Depth;
        if (Ch == ')') { if (--Depth < 0) { Error = "unbalanced ')'"; return false; } }
        if (Depth == 0 && Ch == ';') { FlushCommand(); continue; }
        if (Depth == 0 && std::isspace(static_cast<unsigned char>(Ch))) { FlushToken(); continue; }
        if (Depth > 0 && std::isspace(static_cast<unsigned char>(Ch))) continue;       // allow "(1, 2, 3)"
        Current.push_back(Ch);
    }
    if (InQuote) { Error = "unterminated string"; return false; }
    if (Depth != 0) { Error = "unbalanced '('"; return false; }
    FlushCommand();
    return true;
}

std::optional<double> CommandLine::Number(size_t Index) const noexcept
{
    return Index < Arguments.size() ? CommandCodec::ParseNumber(Arguments[Index]) : std::nullopt;
}

std::optional<Vec3> CommandLine::Point(size_t Index) const noexcept
{
    return Index < Arguments.size() ? CommandCodec::ParsePoint(Arguments[Index]) : std::nullopt;
}

std::optional<Vec2> CommandLine::Point2(size_t Index) const noexcept
{
    auto P = Point(Index);
    if (!P) return std::nullopt;
    return Vec2{ P->X, P->Y };
}

std::optional<std::string> CommandLine::Text(size_t Index) const noexcept
{
    return Index < Arguments.size() ? std::optional<std::string>(Arguments[Index]) : std::nullopt;
}

bool CommandLine::Flag(std::string_view Name) const noexcept
{
    for (const auto& F : Flags) if (F.first == Name) return true;
    return false;
}

std::optional<std::string> CommandLine::FlagValue(std::string_view Name) const noexcept
{
    for (const auto& F : Flags) if (F.first == Name) return F.second;
    return std::nullopt;
}

std::optional<double> CommandLine::FlagNumber(std::string_view Name) const noexcept
{
    auto V = FlagValue(Name);
    return V ? CommandCodec::ParseNumber(*V) : std::nullopt;
}

} // namespace Frontier
