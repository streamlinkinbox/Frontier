//============================================================================================================================================
//                                                    PROJECTSPECIFICATION.CPP
//============================================================================================================================================
// 📦 Strict .frontier stream decoding with project-folder containment validation.

#include "ProjectSpecification.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace
{

std::string TrimText(const std::string& Source)
{
    const auto FirstCharacter = std::find_if_not(Source.begin(), Source.end(), [](unsigned char Character)
    {
        return std::isspace(Character) != 0;
    });
    const auto LastCharacter = std::find_if_not(Source.rbegin(), Source.rend(), [](unsigned char Character)
    {
        return std::isspace(Character) != 0;
    }).base();

    return FirstCharacter < LastCharacter ? std::string(FirstCharacter, LastCharacter) : std::string{};
}

bool DecodeQuotedText(const std::string& Source, std::string& Text)
{
    const std::string TrimmedText = TrimText(Source);
    if (TrimmedText.size() < 2u || TrimmedText.front() != '"' || TrimmedText.back() != '"')
        return false;

    Text = TrimmedText.substr(1u, TrimmedText.size() - 2u);
    return Text.find('"') == std::string::npos;
}

bool DecodeUnsignedNumber(const std::string& Source, uint64_t& Number)
{
    const std::string TrimmedText = TrimText(Source);
    if (TrimmedText.empty())
        return false;

    try
    {
        size_t ConsumedCharacterCount = 0u;
        Number = std::stoull(TrimmedText, &ConsumedCharacterCount, 0);
        return ConsumedCharacterCount == TrimmedText.size();
    }
    catch (...)
    {
        return false;
    }
}

bool DecodeProjectProperties(
    std::istream& SpecificationStream,
    std::unordered_map<std::string, std::string>& Properties,
    std::string& Refusal)
{
    bool ProjectSection = false;
    std::string SourceLine;
    uint32_t LineNumber = 0u;

    while (std::getline(SpecificationStream, SourceLine))
    {
        ++LineNumber;
        const size_t CommentCharacter = SourceLine.find('#');
        const std::string ActiveLine = TrimText(SourceLine.substr(0u, CommentCharacter));
        if (ActiveLine.empty())
            continue;

        if (ActiveLine.front() == '[' && ActiveLine.back() == ']')
        {
            ProjectSection = ActiveLine == "[Project]";
            continue;
        }

        if (!ProjectSection)
            continue;

        const size_t AssignmentCharacter = ActiveLine.find('=');
        if (AssignmentCharacter == std::string::npos)
        {
            Refusal = "line " + std::to_string(LineNumber) + " is not a Project assignment";
            return false;
        }

        const std::string PropertyName = TrimText(ActiveLine.substr(0u, AssignmentCharacter));
        const std::string PropertyText = TrimText(ActiveLine.substr(AssignmentCharacter + 1u));
        if (PropertyName.empty() || Properties.find(PropertyName) != Properties.end())
        {
            Refusal = "line " + std::to_string(LineNumber) + " repeats or omits a Project property name";
            return false;
        }

        Properties.emplace(PropertyName, PropertyText);
    }

    if (Properties.empty())
    {
        Refusal = "the .frontier stream has no [Project] section";
        return false;
    }

    return true;
}

bool DecodeRequiredText(
    const std::unordered_map<std::string, std::string>& Properties,
    const char* PropertyName,
    std::string& Text,
    std::string& Refusal)
{
    const auto Property = Properties.find(PropertyName);
    if (Property == Properties.end() || !DecodeQuotedText(Property->second, Text) || Text.empty())
    {
        Refusal = std::string("[Project].") + PropertyName + " must be one nonempty quoted string";
        return false;
    }

    return true;
}

bool DecodeRequiredNumber(
    const std::unordered_map<std::string, std::string>& Properties,
    const char* PropertyName,
    uint64_t& Number,
    std::string& Refusal)
{
    const auto Property = Properties.find(PropertyName);
    if (Property == Properties.end() || !DecodeUnsignedNumber(Property->second, Number))
    {
        Refusal = std::string("[Project].") + PropertyName + " must be one unsigned number";
        return false;
    }

    return true;
}

bool ResolveProjectLocation(
    const std::filesystem::path& ProjectFolder,
    const std::string& Text,
    std::filesystem::path& Location,
    std::string& Refusal)
{
    const std::filesystem::path RelativeLocation(Text);
    if (RelativeLocation.empty() || RelativeLocation.is_absolute())
    {
        Refusal = "project locations must be nonempty paths relative to the .frontier stream";
        return false;
    }

    for (const std::filesystem::path& Component : RelativeLocation)
    {
        if (Component == "..")
        {
            Refusal = "project locations cannot leave the .frontier folder";
            return false;
        }
    }

    Location = (ProjectFolder / RelativeLocation).lexically_normal();
    return true;
}

bool ReferencesProjectZero(const Frontier::ProjectSpecification& ResolvedSpecification)
{
    if (ResolvedSpecification.ProjectName == "ProjectZero")
        return false;

    const std::string OpeningSceneText = ResolvedSpecification.OpeningSceneLocation.generic_string();
    const std::string CodeImageText = ResolvedSpecification.CodeImageLocation.generic_string();
    return OpeningSceneText.find("Project-Zero") != std::string::npos ||
           CodeImageText.find("Project-Zero") != std::string::npos;
}

} // namespace

namespace Frontier
{

bool ProjectSpecification::CodeImageDeclared() const noexcept
{
    return !CodeImageLocation.empty();
}

bool DecodeProjectSpecification(
    const std::filesystem::path& RequestedSpecificationLocation,
    ProjectSpecification& ResolvedSpecification,
    std::string& Refusal)
{
    ResolvedSpecification = {};
    Refusal.clear();

    if (RequestedSpecificationLocation.extension() != ".frontier")
    {
        Refusal = "the opening stream must use the .frontier extension";
        return false;
    }

    std::error_code FileError;
    const std::filesystem::path AbsoluteSpecificationLocation =
        std::filesystem::absolute(RequestedSpecificationLocation, FileError).lexically_normal();
    if (FileError || !std::filesystem::is_regular_file(AbsoluteSpecificationLocation, FileError))
    {
        Refusal = "cannot read the requested .frontier stream: " + RequestedSpecificationLocation.string();
        return false;
    }

    std::ifstream SpecificationStream(AbsoluteSpecificationLocation);
    if (!SpecificationStream)
    {
        Refusal = "cannot open the requested .frontier stream: " + AbsoluteSpecificationLocation.string();
        return false;
    }

    std::unordered_map<std::string, std::string> Properties;
    if (!DecodeProjectProperties(SpecificationStream, Properties, Refusal))
        return false;

    std::string ContentText;
    std::string OpeningSceneText;
    std::string CodeImageText;
    uint64_t ProjectFormatReading = 0u;
    uint64_t CodeInterchangeReading = 0u;
    uint64_t FingerprintReading = 0u;
    if (!DecodeRequiredText(Properties, "ProjectName", ResolvedSpecification.ProjectName, Refusal) ||
        !DecodeRequiredNumber(Properties, "ProjectFormatNumber", ProjectFormatReading, Refusal) ||
        !DecodeRequiredText(Properties, "ContentLocation", ContentText, Refusal) ||
        !DecodeRequiredText(Properties, "OpeningScene", OpeningSceneText, Refusal) ||
        !DecodeRequiredNumber(Properties, "CodeInterchangeNumber", CodeInterchangeReading, Refusal) ||
        !DecodeRequiredNumber(Properties, "InterfaceFingerprint", FingerprintReading, Refusal))
        return false;

    const auto CodeImageProperty = Properties.find("CodeImage");
    if (CodeImageProperty != Properties.end() && !DecodeQuotedText(CodeImageProperty->second, CodeImageText))
    {
        Refusal = "[Project].CodeImage must be one quoted string when it is declared";
        return false;
    }

    if (ProjectFormatReading != 1u || CodeInterchangeReading != FrontierCodeInterchangeNumber ||
        FingerprintReading != FrontierCodeInterchangeFingerprint)
    {
        Refusal = "the project format, code interchange number, or interface fingerprint is incompatible with Frontier.exe";
        return false;
    }

    const std::filesystem::path ProjectFolder = AbsoluteSpecificationLocation.parent_path();
    ResolvedSpecification.SpecificationLocation = AbsoluteSpecificationLocation;
    ResolvedSpecification.ProjectFormatNumber = static_cast<uint32_t>(ProjectFormatReading);
    ResolvedSpecification.CodeInterchangeNumber = static_cast<uint32_t>(CodeInterchangeReading);
    ResolvedSpecification.InterfaceFingerprint = FingerprintReading;
    if (!ResolveProjectLocation(ProjectFolder, ContentText, ResolvedSpecification.ContentLocation, Refusal) ||
        !ResolveProjectLocation(ProjectFolder, OpeningSceneText, ResolvedSpecification.OpeningSceneLocation, Refusal))
        return false;

    if (!CodeImageText.empty() && !ResolveProjectLocation(ProjectFolder, CodeImageText, ResolvedSpecification.CodeImageLocation, Refusal))
        return false;

    if (ReferencesProjectZero(ResolvedSpecification))
    {
        Refusal = "a project other than ProjectZero cannot resolve its scene or code image through Project-Zero";
        return false;
    }

    return true;
}

} // namespace Frontier
