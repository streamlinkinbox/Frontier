//============================================================================================================================================
// 📦 ParametricSketcher/Console/CommandCodec.h — Tokeniser and typed argument access for `.arc` command lines
//============================================================================================================================================
// Grammar: `verb arg arg ...` · `#` comment to end of line · numbers `1.5 -2 3e-2` · vectors `(x,y,z)` or `(x,y)` ·
//    identifiers/names bare or "quoted" · flags `--name` or `--name=value` · `;` separates commands on one line.
#pragma once

#include "Kernel/VectorSpecification.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

struct CommandLine
{
    std::string              Verb;                                                      // [-] lower-case
    std::vector<std::string> Arguments;                                                 // [-] positional, raw text
    std::vector<std::pair<std::string, std::string>> Flags;                             // [-] --name[=value]

    [[nodiscard]] size_t Count() const noexcept { return Arguments.size(); }
    [[nodiscard]] std::optional<double>      Number(size_t Index) const noexcept;
    [[nodiscard]] std::optional<Vec3>        Point(size_t Index) const noexcept;        // (x,y[,z]) — z defaults 0
    [[nodiscard]] std::optional<Vec2>        Point2(size_t Index) const noexcept;
    [[nodiscard]] std::optional<std::string> Text(size_t Index) const noexcept;
    [[nodiscard]] bool                       Flag(std::string_view Name) const noexcept;
    [[nodiscard]] std::optional<std::string> FlagValue(std::string_view Name) const noexcept;
    [[nodiscard]] std::optional<double>      FlagNumber(std::string_view Name) const noexcept;
};

struct CommandCodec
{
    // Splits a source line into zero or more commands. Returns false and fills Error on malformed input.
    [[nodiscard]] static bool Decode(std::string_view Line, std::vector<CommandLine>& Out, std::string& Error) noexcept;
    [[nodiscard]] static std::optional<double> ParseNumber(std::string_view Token) noexcept;
    [[nodiscard]] static std::optional<Vec3>   ParsePoint(std::string_view Token) noexcept;
};

} // namespace Frontier
