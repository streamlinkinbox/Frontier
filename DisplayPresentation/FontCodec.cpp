//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/FontCodec.cpp — Dynamic Font Discovery, Multi-Weight Classification and Content Folder Reader
//============================================================================================================================================

#include "FontCodec.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace Frontier {

namespace fs = std::filesystem;

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

FontCodec::FontCodec() noexcept
    : DiscoveredFamilies{}
{
}

void FontCodec::Clear() noexcept
{
    DiscoveredFamilies.clear();
}

size_t FontCodec::QueryTotalVariantCount() const noexcept
{
    size_t Total = 0;
    for (const auto& Fam : DiscoveredFamilies)
    {
        Total += Fam.Variants.size();
    }
    return Total;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           STRING HELPER UTILITIES
//------------------------------------------------------------------------------------------------------------------------

static std::string ToLowerString(std::string_view Text)
{
    std::string Result(Text);
    std::transform(Result.begin(), Result.end(), Result.begin(),
        [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    return Result;
}

static std::string TrimQuotes(std::string_view Text)
{
    size_t Start = 0;
    size_t End = Text.size();
    while (Start < End && (Text[Start] == ' ' || Text[Start] == '\t' || Text[Start] == '"')) ++Start;
    while (End > Start && (Text[End - 1] == ' ' || Text[End - 1] == '\t' || Text[End - 1] == '"' || Text[End - 1] == '\r' || Text[End - 1] == '\n')) --End;
    return std::string(Text.substr(Start, End - Start));
}

//------------------------------------------------------------------------------------------------------------------------
//                                           DIRECTORY SCANNING & PARSING
//------------------------------------------------------------------------------------------------------------------------

bool FontCodec::ScanDirectory(std::string_view DirectoryPath) noexcept
{
    std::error_code ErrorCode;
    fs::path SearchPath(DirectoryPath);

    if (!fs::exists(SearchPath, ErrorCode) || !fs::is_directory(SearchPath, ErrorCode))
    {
        return false;
    }

    // Pass 1: Ingest all .manifest descriptor files first
    for (const auto& Entry : fs::directory_iterator(SearchPath, ErrorCode))
    {
        if (Entry.is_regular_file())
        {
            std::string Ext = Entry.path().extension().string();
            std::string ExtLower = ToLowerString(Ext);
            if (ExtLower == ".manifest")
            {
                (void)IngestManifest(Entry.path().string());
            }
        }
    }

    // Pass 2: Ingest all binary font assets (.ttf, .otf, .woff, .woff2)
    for (const auto& Entry : fs::directory_iterator(SearchPath, ErrorCode))
    {
        if (Entry.is_regular_file())
        {
            std::string Ext = Entry.path().extension().string();
            std::string ExtLower = ToLowerString(Ext);
            if (ExtLower == ".ttf" || ExtLower == ".otf" || ExtLower == ".woff" || ExtLower == ".woff2")
            {
                uint32_t FileSize = static_cast<uint32_t>(Entry.file_size(ErrorCode));
                std::string FileName = Entry.path().filename().string();
                std::string FullPath = Entry.path().string();
                ClassifyFontFileName(FileName, FullPath, FileSize);
            }
        }
    }

    return !DiscoveredFamilies.empty();
}

bool FontCodec::ScanEngineAndGameContent(
    std::string_view EngineContentPath,
    std::string_view GameContentPath) noexcept
{
    bool ScannedEngine = ScanDirectory(EngineContentPath);
    bool ScannedGame   = ScanDirectory(GameContentPath);
    return ScannedEngine || ScannedGame;
}

bool FontCodec::IngestManifest(std::string_view ManifestPath) noexcept
{
    std::ifstream Stream{ std::string(ManifestPath) };
    if (!Stream.is_open()) return false;

    std::string Line;
    std::string Family;
    std::string Category = "SansSerif";
    std::string Description = "Clean & Modern";
    bool InVariants = false;

    fs::path ParentDir = fs::path(ManifestPath).parent_path();

    while (std::getline(Stream, Line))
    {
        // Strip comments and whitespace
        size_t CommentPos = Line.find(';');
        if (CommentPos != std::string::npos && !InVariants)
        {
            Line = Line.substr(0, CommentPos);
        }

        std::string Trimmed = TrimQuotes(Line);
        if (Trimmed.empty()) continue;

        if (Trimmed == "[FontArchive]" || Trimmed == "[Archive]")
        {
            InVariants = false;
            continue;
        }
        else if (Trimmed == "[Variants]")
        {
            InVariants = true;
            continue;
        }

        size_t EqPos = Line.find('=');
        if (EqPos == std::string::npos) continue;

        std::string Key = TrimQuotes(Line.substr(0, EqPos));
        std::string Val = Line.substr(EqPos + 1);

        if (!InVariants)
        {
            if (Key == "Family" || Key == "Name")
            {
                Family = TrimQuotes(Val);
            }
            else if (Key == "Category")
            {
                Category = TrimQuotes(Val);
            }
            else if (Key == "Description")
            {
                Description = TrimQuotes(Val);
            }
        }
        else
        {
            // Variant entry: e.g. Regular = "GeneralSans-Regular.ttf" ; Weight=400, Style=normal
            std::string VariantName = Key;
            std::string FileNamePart = Val;
            uint32_t WeightNum = 400;
            FontStyleCategory Style = FontStyleCategory::Normal;

            size_t SemiPos = FileNamePart.find(';');
            if (SemiPos != std::string::npos)
            {
                std::string Meta = FileNamePart.substr(SemiPos + 1);
                FileNamePart = FileNamePart.substr(0, SemiPos);

                size_t WPos = Meta.find("Weight=");
                if (WPos != std::string::npos)
                {
                    WeightNum = static_cast<uint32_t>(std::atoi(Meta.c_str() + WPos + 7));
                }
                if (Meta.find("italic") != std::string::npos || Meta.find("Italic") != std::string::npos)
                {
                    Style = FontStyleCategory::Italic;
                }
            }

            std::string TargetFileName = TrimQuotes(FileNamePart);
            fs::path FontFullPath = ParentDir / TargetFileName;

            // Ensure family entry exists
            if (!Family.empty())
            {
                FontFamilyRecord* ExistingFam = nullptr;
                for (auto& Fam : DiscoveredFamilies)
                {
                    if (Fam.FamilyName == Family)
                    {
                        ExistingFam = &Fam;
                        break;
                    }
                }
                if (!ExistingFam)
                {
                    DiscoveredFamilies.push_back(FontFamilyRecord{
                        Family,
                        Category,
                        Description,
                        {}
                    });
                    ExistingFam = &DiscoveredFamilies.back();
                }

                // Add or update variant
                bool Exists = false;
                for (auto& Var : ExistingFam->Variants)
                {
                    if (Var.VariantName == VariantName)
                    {
                        Var.FilePath = FontFullPath.string();
                        Var.Weight   = static_cast<FontWeightCategory>(WeightNum);
                        Var.Style    = Style;
                        Exists = true;
                        break;
                    }
                }

                if (!Exists)
                {
                    ExistingFam->Variants.push_back(FontVariantRecord{
                        static_cast<FontWeightCategory>(WeightNum),
                        Style,
                        VariantName,
                        FontFullPath.string(),
                        "TrueType",
                        512
                    });
                }
            }
        }
    }

    return !Family.empty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                           AUTOMATIC FILENAME CLASSIFIER
//------------------------------------------------------------------------------------------------------------------------

void FontCodec::ClassifyFontFileName(
    std::string_view FileName,
    std::string_view FilePath,
    uint32_t FileSizeBytes) noexcept
{
    std::string BaseName = fs::path(FileName).stem().string();
    std::string Ext = fs::path(FileName).extension().string();

    std::string FamilyPart = BaseName;
    std::string VariantPart = "Regular";

    size_t DashPos = BaseName.find('-');
    if (DashPos != std::string::npos)
    {
        FamilyPart  = BaseName.substr(0, DashPos);
        VariantPart = BaseName.substr(DashPos + 1);
    }
    else
    {
        size_t UnderscorePos = BaseName.find('_');
        if (UnderscorePos != std::string::npos)
        {
            FamilyPart  = BaseName.substr(0, UnderscorePos);
            VariantPart = BaseName.substr(UnderscorePos + 1);
        }
    }

    // Convert camel-case family name to human readable (e.g. "GeneralSans" -> "General Sans")
    std::string ReadableFamily;
    for (size_t i = 0; i < FamilyPart.size(); ++i)
    {
        char C = FamilyPart[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(C)) &&
            std::islower(static_cast<unsigned char>(FamilyPart[i - 1])))
        {
            ReadableFamily.push_back(' ');
        }
        ReadableFamily.push_back(C);
    }

    // Map variant keyword to numeric weight tier
    FontWeightCategory Weight = FontWeightCategory::Regular;
    FontStyleCategory  Style  = FontStyleCategory::Normal;
    std::string VarLower = ToLowerString(VariantPart);

    if (VarLower.find("thin") != std::string::npos || VarLower.find("hairline") != std::string::npos)
    {
        Weight = FontWeightCategory::Thin;
    }
    else if (VarLower.find("extralight") != std::string::npos || VarLower.find("ultralight") != std::string::npos)
    {
        Weight = FontWeightCategory::ExtraLight;
    }
    else if (VarLower.find("light") != std::string::npos)
    {
        Weight = FontWeightCategory::Light;
    }
    else if (VarLower.find("semibold") != std::string::npos || VarLower.find("demibold") != std::string::npos)
    {
        Weight = FontWeightCategory::SemiBold;
    }
    else if (VarLower.find("extrabold") != std::string::npos || VarLower.find("ultrabold") != std::string::npos)
    {
        Weight = FontWeightCategory::ExtraBold;
    }
    else if (VarLower.find("bold") != std::string::npos)
    {
        Weight = FontWeightCategory::Bold;
    }
    else if (VarLower.find("medium") != std::string::npos)
    {
        Weight = FontWeightCategory::Medium;
    }
    else if (VarLower.find("black") != std::string::npos || VarLower.find("heavy") != std::string::npos)
    {
        Weight = FontWeightCategory::Black;
    }

    if (VarLower.find("italic") != std::string::npos)
    {
        Style = FontStyleCategory::Italic;
    }

    // Find or insert family (normalizing spaces for deduplication)
    auto NormalizeSpace = [](std::string_view Str) {
        std::string Clean;
        for (char C : Str) if (C != ' ' && C != '_' && C != '-') Clean.push_back(static_cast<char>(std::tolower(C)));
        return Clean;
    };
    std::string NormalizedTarget = NormalizeSpace(ReadableFamily);

    FontFamilyRecord* TargetFam = nullptr;
    for (auto& Fam : DiscoveredFamilies)
    {
        if (NormalizeSpace(Fam.FamilyName) == NormalizedTarget)
        {
            TargetFam = &Fam;
            break;
        }
    }

    if (!TargetFam)
    {
        std::string Category = "SansSerif";
        std::string Description = "Clean & Modern";
        if (VarLower.find("mono") != std::string::npos || ReadableFamily.find("Mono") != std::string::npos)
        {
            Category = "Monospace";
            Description = "Monospaced Code";
        }
        else if (ReadableFamily.find("Display") != std::string::npos || ReadableFamily.find("Grotesk") != std::string::npos)
        {
            Category = "Display";
            Description = "Grotesque Display";
        }

        DiscoveredFamilies.push_back(FontFamilyRecord{
            ReadableFamily,
            Category,
            Description,
            {}
        });
        TargetFam = &DiscoveredFamilies.back();
    }

    // Check if variant already registered
    bool VariantFound = false;
    for (auto& Var : TargetFam->Variants)
    {
        if (Var.Weight == Weight && Var.Style == Style)
        {
            Var.FilePath = std::string(FilePath);
            Var.FileSize = FileSizeBytes;
            VariantFound = true;
            break;
        }
    }

    if (!VariantFound)
    {
        std::string Format = (Ext == ".otf") ? "OpenType" : ((Ext == ".woff" || Ext == ".woff2") ? "WOFF" : "TrueType");
        TargetFam->Variants.push_back(FontVariantRecord{
            Weight,
            Style,
            VariantPart,
            std::string(FilePath),
            Format,
            FileSizeBytes
        });
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           LOOKUP & QUERY IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

const FontFamilyRecord* FontCodec::QueryFamily(std::string_view DesiredFamilyName) const noexcept
{
    std::string QueryLower = ToLowerString(DesiredFamilyName);
    for (const auto& Fam : DiscoveredFamilies)
    {
        std::string FamLower = ToLowerString(Fam.FamilyName);
        if (FamLower == QueryLower || FamLower.find(QueryLower) != std::string::npos)
        {
            return &Fam;
        }
    }
    return nullptr;
}

const FontVariantRecord* FontCodec::QueryVariant(
    std::string_view DesiredFamilyName,
    FontWeightCategory DesiredWeight,
    FontStyleCategory DesiredStyle) const noexcept
{
    const FontFamilyRecord* Fam = QueryFamily(DesiredFamilyName);
    if (!Fam || Fam->Variants.empty()) return nullptr;

    const FontVariantRecord* ExactMatch = nullptr;
    const FontVariantRecord* ClosestWeight = nullptr;
    int32_t MinDistance = 9999;

    for (const auto& Var : Fam->Variants)
    {
        if (Var.Weight == DesiredWeight && Var.Style == DesiredStyle)
        {
            ExactMatch = &Var;
            break;
        }

        int32_t Dist = std::abs(static_cast<int32_t>(Var.Weight) - static_cast<int32_t>(DesiredWeight));
        if (Dist < MinDistance)
        {
            MinDistance = Dist;
            ClosestWeight = &Var;
        }
    }

    return ExactMatch ? ExactMatch : ClosestWeight;
}

} // namespace Frontier
