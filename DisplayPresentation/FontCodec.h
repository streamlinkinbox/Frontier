//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/FontCodec.h — Dynamic Font Discovery, Multi-Weight Classification and Content Folder Reader
//============================================================================================================================================

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                           FONT WEIGHT & STYLE CATEGORIES
//------------------------------------------------------------------------------------------------------------------------

enum class FontWeightCategory : uint32_t
{
    Thin                                = 100,                  // 100: Thin / Hairline
    ExtraLight                          = 200,                  // 200: Extra Light / Ultra Light
    Light                               = 300,                  // 300: Light
    Regular                             = 400,                  // 400: Regular / Normal / Book
    Medium                              = 500,                  // 500: Medium
    SemiBold                            = 600,                  // 600: Semi Bold / Demi Bold
    Bold                                = 700,                  // 700: Bold
    ExtraBold                           = 800,                  // 800: Extra Bold / Ultra Bold
    Black                               = 900                   // 900: Black / Heavy
};

enum class FontStyleCategory : uint32_t
{
    Normal                              = 0,                    // Upright / Normal
    Italic                              = 1,                    // Italicized
    Oblique                             = 2                     // Slanted / Oblique
};

//------------------------------------------------------------------------------------------------------------------------
//                                           FONT VARIANT & FAMILY RECORDS
//------------------------------------------------------------------------------------------------------------------------

struct FontVariantRecord
{
    FontWeightCategory      Weight;                             // [category] numeric weight tier (100 - 900)
    FontStyleCategory       Style;                              // [category] italic / normal
    std::string             VariantName;                        // [name] e.g. "SemiBold", "Regular", "BoldItalic"
    std::string             FilePath;                           // [path] absolute or relative file path to font asset
    std::string             FileFormat;                         // [format] "TrueType", "OpenType", "WOFF2"
    uint32_t                FileSize;                           // [bytes] file byte count
};

struct FontFamilyRecord
{
    std::string             FamilyName;                         // [name] e.g. "General Sans", "Inter", "JetBrains Mono"
    std::string             CategoryName;                       // [category] "SansSerif", "Display", "Monospace"
    std::string             Description;                        // [desc] e.g. "Clean & Modern", "Monospaced Code"
    std::vector<FontVariantRecord> Variants;                    // [variants] list of discovered weights/styles
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    FONT CODEC
//------------------------------------------------------------------------------------------------------------------------

class FontCodec
{
public:
    FontCodec() noexcept;
    ~FontCodec() noexcept = default;

    FontCodec(const FontCodec&) = default;
    FontCodec& operator=(const FontCodec&) = default;
    FontCodec(FontCodec&&) noexcept = default;
    FontCodec& operator=(FontCodec&&) noexcept = default;

    // Content Folder Scanning & Manifest Ingestion
    bool                    ScanDirectory(std::string_view DirectoryPath) noexcept;
    bool                    ScanEngineAndGameContent(
                                std::string_view EngineContentPath,
                                std::string_view GameContentPath) noexcept;
    bool                    IngestManifest(std::string_view ManifestPath) noexcept;

    // Dynamic Font & Variant Query Lookups
    [[nodiscard]] const FontFamilyRecord* QueryFamily(std::string_view DesiredFamilyName) const noexcept;
    [[nodiscard]] const FontVariantRecord* QueryVariant(
                                std::string_view DesiredFamilyName,
                                FontWeightCategory DesiredWeight,
                                FontStyleCategory DesiredStyle = FontStyleCategory::Normal) const noexcept;

    [[nodiscard]] const std::vector<FontFamilyRecord>& QueryDiscoveredFamilies() const noexcept { return DiscoveredFamilies; }
    [[nodiscard]] size_t    QueryFamilyCount() const noexcept { return DiscoveredFamilies.size(); }
    [[nodiscard]] size_t    QueryTotalVariantCount() const noexcept;

    void                    Clear() noexcept;

    // Single unified conversion operator for discovered family count
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    void                    ClassifyFontFileName(
                                std::string_view FileName,
                                std::string_view FilePath,
                                uint32_t FileSizeBytes) noexcept;

    std::vector<FontFamilyRecord> DiscoveredFamilies;           // [families] dynamic catalog of scanned fonts
};

template<>
inline size_t FontCodec::Convert<size_t>() const noexcept
{
    return DiscoveredFamilies.size();
}

template<>
inline bool FontCodec::Convert<bool>() const noexcept
{
    return !DiscoveredFamilies.empty();
}

} // namespace Frontier
