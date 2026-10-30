/*============================================================================================================================================
                                                        TYPEFACECATALOGUE.H
============================================================================================================================================*/
// 🅰 The selectable text faces of the Texture Studio, shared by Texture Paint's Text decal tool
//    and the Stamp workspace's Text components: every glyph-outline face found under
//    <content>/EngineContent/FontArchives (family folders of .ttf/.otf), with the face-aware
//    text-fit measurement and small raster previews every font picker here is built from.

#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{
// 📝 One bakeable face. Only TrueType-'glyf' faces are listed: the outline baker (stb_truetype)
// cannot read CFF/OpenType-PS outlines, so those archives (e.g. Inter .otf) never appear.
struct TypefaceChoice
{
    std::string Id;          // "DMSans/DMSans-Bold" - family folder + face stem, no extension
    std::string Family;      // "DM Sans"
    std::string Style;       // "Bold"
    std::string DisplayName; // "DM Sans Bold"
    std::filesystem::path Path; // file to bake glyph outlines from
};

// 📝 The bundled default face (every text authored before font selection existed).
inline constexpr std::string_view DefaultTypefaceId = "DMSans/DMSans-Regular";

// 🔍 All faces under the content root's FontArchives, sorted family then weight. Enumerated once
// per content root and cached (filesystem work happens on first query only).
[[nodiscard]] const std::vector<TypefaceChoice> &QueryTypefaceChoices(const std::filesystem::path &ContentRoot);
// 🔍 One face by id, or nullptr (unknown id / empty catalogue).
[[nodiscard]] const TypefaceChoice *FindTypefaceChoice(const std::filesystem::path &ContentRoot, std::string_view Id);
// 📝 A stored face id resolved to a bakeable file path; unknown ids fall back to the default
// face's file, so documents stay readable wherever their font is missing.
[[nodiscard]] std::string ResolveTypefacePath(const std::filesystem::path &ContentRoot, std::string_view Id);

// 📏 Largest point size whose glyph outline fits the box, for one explicit typeface file -
// the measurement both text tools compose and (re)bake with. Wrap word-wraps at the box width
// (composed text); without it the copy must fit on one line (font previews).
[[nodiscard]] float MeasureTextFitSize(std::string_view Content, float BoxWidth, float BoxHeight,
    const std::string &TypefacePath, bool Wrap = true);

// 🎞️ Square straight-alpha RGBA preview of Sample set in the face (white ink band centred in
// the 512 stamp square), via the same fit + outline + ThorVG chain the decals themselves use.
// Empty when the face or sample yields no ink.
[[nodiscard]] std::vector<uint8_t> RenderTypefacePreview(const std::string &TypefacePath,
    std::string_view Sample, uint32_t Extent);
} // namespace Frontier
