//============================================================================================================================================
//                                                     PROJECTSPECIFICATION.H
//============================================================================================================================================
// 📦 Parses and validates one declarative .frontier project opening description.

#pragma once

#include "ProjectInterchange.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Frontier
{

/// 📦 A resolved project opening whose locations cannot escape its .frontier folder.
/// out   ProjectSpecification  [-]  one validated declarative project opening
/// err   records a refusal instead of throwing across the project-code edge
struct ProjectSpecification
{
    std::string           ProjectName;
    uint32_t              ProjectFormatNumber = 0u;
    std::filesystem::path SpecificationLocation;
    std::filesystem::path ContentLocation;
    std::filesystem::path OpeningSceneLocation;
    std::filesystem::path CodeImageLocation;
    uint32_t              CodeInterchangeNumber = 0u;
    uint64_t              InterfaceFingerprint = 0u;

    [[nodiscard]] bool CodeImageDeclared() const noexcept;
};

/// 📦 Reads one .frontier stream, resolves its relative locations, and enforces the first project-format revision.
/// in    SpecificationLocation  [-]  user-selected .frontier stream
/// out   ResolvedSpecification  [-]  validated locations and interchange readings
/// err   returns false and writes the exact refusal when parsing or validation cannot proceed
[[nodiscard]] bool DecodeProjectSpecification(
    const std::filesystem::path& SpecificationLocation,
    ProjectSpecification& ResolvedSpecification,
    std::string& Refusal);

} // namespace Frontier
