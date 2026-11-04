/*============================================================================================================================================
                                                        TYPEFACECATALOGUE.CPP
============================================================================================================================================*/

#include "UI/TextureStudio/TypefaceCatalogue.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h" // RasterizeStampDocument
#include "Engine/ContentInterchange/StampTextOutline.h"
#include "Engine/ContentInterchange/VectorAuthoringDocument.h"
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>

namespace Frontier
{
namespace
{
std::filesystem::path ArchivesDirectory(const std::filesystem::path &ContentRoot)
{
    return ContentRoot.empty() ? std::filesystem::path("EngineContent/FontArchives")
                               : ContentRoot / "EngineContent/FontArchives";
}

// TrueType 'glyf' outlines only: sfnt 0x00010000 or the legacy 'true' tag. CFF ('OTTO') and
// collections ('ttcf') are left out - stb_truetype extracts no glyph shapes from them.
bool HasGlyphOutlines(const std::filesystem::path &File)
{
    std::ifstream Input(File, std::ios::binary);
    char Tag[4]{};
    if (!Input.read(Tag, 4)) return false;
    return (Tag[0] == 0 && Tag[1] == 1 && Tag[2] == 0 && Tag[3] == 0) ||
           (Tag[0] == 't' && Tag[1] == 'r' && Tag[2] == 'u' && Tag[3] == 'e');
}

// "DMSans" -> "DM Sans", "JetBrainsMono" -> "JetBrains Mono", "ExtraBold" -> "Extra Bold".
std::string PrettyName(std::string_view Compact)
{
    std::string Pretty;
    for (size_t Index = 0; Index < Compact.size(); ++Index)
    {
        const char Current = Compact[Index];
        if (Index > 0 && std::isupper(static_cast<unsigned char>(Current)))
        {
            const char Previous = Compact[Index - 1];
            const bool NextLower = Index + 1 < Compact.size() && std::islower(static_cast<unsigned char>(Compact[Index + 1]));
            if (std::islower(static_cast<unsigned char>(Previous)) || NextLower) Pretty += ' ';
        }
        Pretty += Current;
    }
    return Pretty;
}

int WeightRank(std::string_view Style)
{
    static constexpr const char *Order[]{"Thin", "Extra Light", "Light", "Regular", "Medium", "Semi Bold", "Bold", "Extra Bold", "Black"};
    for (int Rank = 0; Rank < 9; ++Rank)
        if (Style == Order[Rank]) return Rank;
    return 9;
}

bool FontExtension(const std::filesystem::path &File)
{
    std::string Extension = File.extension().string();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C) { return char(std::tolower(C)); });
    return Extension == ".ttf" || Extension == ".otf";
}

std::vector<TypefaceChoice> Enumerate(const std::filesystem::path &Archives)
{
    std::vector<TypefaceChoice> Faces;
    std::error_code Error;
    for (const auto &FamilyEntry : std::filesystem::directory_iterator(Archives, Error))
    {
        if (!FamilyEntry.is_directory(Error)) continue;
        const std::string FamilyDirectory = FamilyEntry.path().filename().string();
        for (const auto &FaceEntry : std::filesystem::directory_iterator(FamilyEntry.path(), Error))
        {
            if (!FaceEntry.is_regular_file(Error) || !FontExtension(FaceEntry.path())) continue;
            if (!HasGlyphOutlines(FaceEntry.path())) continue;
            const std::string Stem = FaceEntry.path().stem().string();
            std::string Style = Stem;
            if (Stem.rfind(FamilyDirectory, 0) == 0 && Stem.size() > FamilyDirectory.size())
            {
                Style = Stem.substr(FamilyDirectory.size());
                if (!Style.empty() && (Style.front() == '-' || Style.front() == '_' || Style.front() == ' ')) Style.erase(Style.begin());
            }
            else if (const size_t Dash = Stem.find_last_of("-_ "); Dash != std::string::npos)
                Style = Stem.substr(Dash + 1);
            TypefaceChoice Face;
            Face.Id = FamilyDirectory + "/" + Stem;
            Face.Family = PrettyName(FamilyDirectory);
            Face.Style = PrettyName(Style.empty() ? std::string("Regular") : Style);
            Face.DisplayName = Face.Family + " " + Face.Style;
            Face.Path = FaceEntry.path();
            Faces.push_back(std::move(Face));
        }
    }
    std::sort(Faces.begin(), Faces.end(), [](const TypefaceChoice &A, const TypefaceChoice &B) {
        if (A.Family != B.Family) return A.Family < B.Family;
        const int RankA = WeightRank(A.Style), RankB = WeightRank(B.Style);
        return RankA != RankB ? RankA < RankB : A.Style < B.Style;
    });
    return Faces;
}
} // namespace

const std::vector<TypefaceChoice> &QueryTypefaceChoices(const std::filesystem::path &ContentRoot)
{
    // Cached per archives directory; the lock keeps first-use enumeration single-shot even when a
    // text synthesis resolves a face off the UI thread.
    static std::mutex CacheLock;
    static std::map<std::string, std::vector<TypefaceChoice>> Cache;
    const std::filesystem::path Archives = ArchivesDirectory(ContentRoot);
    std::scoped_lock Guard(CacheLock);
    const auto [Entry, Inserted] = Cache.try_emplace(Archives.generic_string());
    if (Inserted) Entry->second = Enumerate(Archives);
    return Entry->second;
}

const TypefaceChoice *FindTypefaceChoice(const std::filesystem::path &ContentRoot, std::string_view Id)
{
    for (const auto &Face : QueryTypefaceChoices(ContentRoot))
        if (Face.Id == Id) return &Face;
    return nullptr;
}

std::string ResolveTypefacePath(const std::filesystem::path &ContentRoot, std::string_view Id)
{
    if (!Id.empty())
        if (const auto *Face = FindTypefaceChoice(ContentRoot, Id)) return Face->Path.string();
    return (ArchivesDirectory(ContentRoot) / "DMSans/DMSans-Regular.ttf").string();
}

float MeasureTextFitSize(std::string_view Content, float BoxWidth, float BoxHeight,
    const std::string &TypefacePath, bool Wrap)
{
    // Binary search over real baked outlines (the Stamp canvas's original fit law): the largest
    // point size whose instruction bounds stay inside the box (word-wrapped at the box width
    // when Wrap is set, single-line otherwise - previews must never become two-line samples).
    const float Width = std::max(1.f, BoxWidth), Height = std::max(1.f, BoxHeight);
    const std::string Text(Content);
    const auto Fits = [&](float PointSize)
    {
        VectorAuthoringDocument Scratch;
        Scratch.Stamp = true;
        const uint32_t Root = AppendStampTextOutline(Scratch, Text, PointSize, {0, 0, 0, 1}, TypefacePath, Wrap ? Width : 0.f);
        const auto &Instruction = Scratch.Instructions[Root];
        return Instruction.Bounds[2] <= Width && Instruction.Bounds[3] <= Height;
    };
    float Low = 4.f, High = 512.f;
    if (!Fits(Low)) return Low; // Even the smallest permitted size cannot fit; still return something sane.
    for (int Step = 0; Step < 28; ++Step)
    {
        const float Mid = (Low + High) * .5f;
        if (Fits(Mid)) Low = Mid; else High = Mid;
    }
    return std::clamp(Low, 4.f, 512.f);
}

std::vector<uint8_t> RenderTypefacePreview(const std::string &TypefacePath, std::string_view Sample, uint32_t Extent)
{
    try
    {
        // Fit the sample, single-line (never wrapped), into a wide, short box so the square
        // raster carries one centred ink band - font pickers fit that band into their rows.
        constexpr float BoxWidth = 470.f, BoxHeight = 120.f;
        const std::string Text(Sample);
        const float PointSize = MeasureTextFitSize(Text, BoxWidth, BoxHeight, TypefacePath, false);
        VectorAuthoringDocument Document;
        Document.Stamp = true;
        const uint32_t Root = AppendStampTextOutline(Document, Text, PointSize, {1.f, 1.f, 1.f, 1.f}, TypefacePath, 0.f);
        VectorAuthoringLayer Layer;
        Layer.Token = ++Document.TokenClock;
        Layer.Root = Root;
        Layer.ComponentKind = VectorAuthoringComponentKind::Text;
        Layer.SourceMarkup = Text;
        Layer.TextPointSize = PointSize;
        Document.Layers.push_back(Layer);
        ReclaimVectorAuthoringSources(Document);
        const auto Bounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{Layer.Token});
        VectorAuthoringSourcePatch Centre;
        Centre.TranslationX = 256.f - Bounds[0];
        Centre.TranslationY = 256.f - Bounds[1];
        TransformVectorAuthoringSelection(Document, std::array<uint32_t, 1>{Layer.Token}, Centre);
        return RasterizeStampDocument(Document, Extent);
    }
    catch (...) { return {}; }
}
} // namespace Frontier
