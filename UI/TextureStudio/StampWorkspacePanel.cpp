/*============================================================================================================================================
                                                         STAMPWORKSPACEPANEL.CPP
============================================================================================================================================*/
// 🧩 Native Stamp (decal) authoring: the same toolbar/canvas/inspector feel as Pattern, retargeted at one bounded
//    24-layer decal stack (Shape/Text/Vector/Image components) instead of a tileable preset library.

#include "StampWorkspacePanel.h"
#include "PanelChrome.h"
#include "StampLibrary.h"
#include "VectorMaskControls.h"
#include "PatternThumbnailRaster.h"
#include "TypefaceCatalogue.h"
#include "WorkspaceZoom.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/GlyphSpace.h"
#include "Engine/Interface/PixelSpace.h"
#include "Engine/ContentInterchange/StampTextOutline.h"
#include "Engine/ContentInterchange/StampVectorImport.h"
#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
#include "Engine/ContentInterchange/VectorGradientStops.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numbers>
#include <sstream>

namespace Frontier
{
// Shared panel UI primitives (PanelChrome.h) - one implementation for all five workspaces.
using PanelChrome::BareIconButton;
using PanelChrome::Caption;
using PanelChrome::CaptionWidth;
using PanelChrome::DrawGlyph;
using PanelChrome::Ink32;
using PanelChrome::Spectrum;
using PanelChrome::SpectrumQuad;
using PanelChrome::ThemeQuad;
namespace
{
constexpr const char *ShapeCaptions[]{"Rectangle", "Ellipse", "Diamond", "Triangle", "Polygon", "Star"};
constexpr const char *ShapeNames[]{"rect", "ellipse", "diamond", "triangle", "polygon", "star"};
constexpr const char *ShapePaths[]{"M4 4H20V20H4Z", "M12 3A9 9 0 1 0 12 21A9 9 0 1 0 12 3Z", "M12 3L21 12L12 21L3 12Z", "M12 3L22 21H2Z", "M7 3H17L22 12L17 21H7L2 12Z", "M12 2L15 8L22 9L17 14L18 22L12 18L6 22L7 14L2 9L9 8Z"};
constexpr const char *SelectPath = "M4 3L11 21L14 14L21 11Z";
constexpr const char *PlusPath = "M12 5V19M5 12H19";
constexpr const char *VisiblePath = "M2 12Q12 0 22 12Q12 24 2 12ZM12 9A3 3 0 1 0 12 15A3 3 0 1 0 12 9Z";
constexpr const char *LockPath = "M6 10H18V21H6ZM8 10V7A4 4 0 0 1 16 7V10";
constexpr const char *TrashPath = "M3 6H21M8 6V3H16V6M6 6L7 21H17L18 6M10 10V17M14 10V17";
constexpr const char *CopyPath = "M8 8H21V21H8ZM16 8V3H3V16H8";
constexpr const char *TextIconPath = "M4 5H20M12 5V20M8 20H16";
constexpr const char *VectorIconPath = "M4 19L9 5L13 15L20 4";
constexpr const char *ImageIconPath = "M3 5H21V19H3ZM3 15L8 10L12 14L16 10L21 15M9 9A1 1 0 1 0 9 9.01";
constexpr const char *SaveIconPath = "M5 3H16L21 8V21H3V3H5ZM7 3V9H15V3M7 21V13H17V21";
constexpr const char *ExportIconPath = "M12 15V3M7 8L12 3L17 8M4 15V20H20V15";
constexpr const char *ChevronDownPath = "M6 9L12 15L18 9";

constexpr const char *HiddenPath = "M2 12Q12 0 22 12Q12 24 2 12ZM12 9A3 3 0 1 0 12 15A3 3 0 1 0 12 9ZM4 4L20 20";

PlanePoint QueryGradientPlanePoint(std::array<float, 2> LocalNorm, const std::array<float, 4> &Bounds, float ShapeRotation)
{
    const float LocalX = LocalNorm[0] * Bounds[2], LocalY = LocalNorm[1] * Bounds[3];
    return {Bounds[0] + LocalX * std::cos(ShapeRotation) - LocalY * std::sin(ShapeRotation),
            Bounds[1] + LocalX * std::sin(ShapeRotation) + LocalY * std::cos(ShapeRotation)};
}

std::array<PlanePoint, 2> QueryGradientAxisEndpoints(const VectorCoverageInstruction &Source, const std::array<float, 4> &Bounds, float ShapeRotation)
{
    return {QueryGradientPlanePoint({Source.GradientAxis[0], Source.GradientAxis[1]}, Bounds, ShapeRotation),
            QueryGradientPlanePoint({Source.GradientAxis[2], Source.GradientAxis[3]}, Bounds, ShapeRotation)};
}

std::array<float, 2> QueryGradientLocalNorm(PlanePoint Point, const std::array<float, 4> &Bounds, float ShapeRotation)
{
    const float Δx = Point.X - Bounds[0], Δy = Point.Y - Bounds[1];
    const float Cosine = std::cos(ShapeRotation), Sine = std::sin(ShapeRotation);
    return {(Cosine * Δx + Sine * Δy) / std::max(Bounds[2], 1e-4f), (-Sine * Δx + Cosine * Δy) / std::max(Bounds[3], 1e-4f)};
}

// 📝 Projects Point onto the Start→End segment and returns how far along it landed, unclamped — reused
// to turn a mouse position into a gradient Fraction (clamp at each call site, since the drag case clamps
// to neighbouring stops instead of a flat 0..1).
float ProjectAlongSegment(PlanePoint Point, PlanePoint Start, PlanePoint End)
{
    const float Length2 = std::max(1e-4f, (End.X - Start.X) * (End.X - Start.X) + (End.Y - Start.Y) * (End.Y - Start.Y));
    return ((Point.X - Start.X) * (End.X - Start.X) + (Point.Y - Start.Y) * (End.Y - Start.Y)) / Length2;
}

// 📝 Rounded, padded modal chrome, matching Pattern's own close/unsaved-changes prompt and the
// new Add Text/Vector/Image popups — ConstructInterface zeroes WindowRounding/WindowPadding for the
// whole docked workspace, so a modal needs its own scope or it inherits flush, square edges.
struct StampModalScope
{
    explicit StampModalScope(float Scale)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * Scale, 18 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .10f));
    }
    ~StampModalScope()
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
};

std::string FoldCaption(std::string CaptionText)
{
    for (auto &Letter : CaptionText) if (Letter >= 'A' && Letter <= 'Z') Letter = char(Letter + 32);
    return CaptionText;
}

// 📝 A tiny, self-contained PNG IHDR sniff (independent of MaterialAuthoring's validator) so the Add
// Image popup can size its placeholder rectangle to the pasted image's real aspect ratio before the
// engine's own ValidateVectorAuthoringOrganization pass fully validates/accepts the embedded bytes.
std::optional<std::array<uint32_t, 2>> SniffEmbeddedImageExtent(std::string_view DataUrl)
{
    constexpr std::string_view Prefix = "data:image/png;base64,";
    if (!DataUrl.starts_with(Prefix)) return std::nullopt;
    const std::string_view Encoded = DataUrl.substr(Prefix.size());
    constexpr const char *Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::array<uint8_t, 36> Header{};
    size_t Produced = 0;
    uint32_t Accumulator = 0;
    int Bits = 0;
    for (const char Letter : Encoded.substr(0, 48))
    {
        if (Letter == '=') break;
        const char *Position = std::strchr(Alphabet, Letter);
        if (!Position) return std::nullopt;
        Accumulator = (Accumulator << 6) | uint32_t(Position - Alphabet);
        Bits += 6;
        if (Bits >= 8) { Bits -= 8; if (Produced < Header.size()) Header[Produced++] = uint8_t((Accumulator >> Bits) & 0xff); }
    }
    if (Produced < 24) return std::nullopt;
    constexpr std::array<uint8_t, 8> Signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (!std::equal(Signature.begin(), Signature.end(), Header.begin())) return std::nullopt;
    const auto ReadExtent = [&Header](size_t Offset)
    { return (uint32_t(Header[Offset]) << 24) | (uint32_t(Header[Offset + 1]) << 16) | (uint32_t(Header[Offset + 2]) << 8) | uint32_t(Header[Offset + 3]); };
    return std::array<uint32_t, 2>{ReadExtent(16), ReadExtent(20)};
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DOCUMENT AND PRESENTATION LIFETIME
//------------------------------------------------------------------------------------------------------------------------

StampWorkspacePanel::StampWorkspacePanel()
{
    VectorAuthoringDocument Blank;
    Blank.Stamp = true;
    Blank.Name = "Untitled stamp";
    ConfigureDocument(Blank);
    Controls.RegisterExtent = [&](std::string_view CaptionText, PlaneExtent Extent) { ControlExtents[std::string(CaptionText)] = Extent; };
    (void)Panels.ConfigureDockspaceKey("TextureWorkspace Stamp panels");
    Panels.ConfigureWorkspaceInsets(43, 0);
    Panels.ConfigureTabExtent(130, 26);
    Panels.Register({"stamp-library", "Stamp library", [&]() { ConstructLibrary(); }});
    Panels.Register({"stamp-layers", "Layers", [&]() { ConstructLayers(); }});
    Panels.Register({"stamp-canvas", "2D viewport", [&]() { ConstructCanvas(); }});
    Panels.Register({"stamp-inspector", "Inspector", [&]() { ConstructInspector(); }});
    // 📝 Same left-column arrangement as Pattern: the library stacks above Layers (32% of the column),
    // so the saved-stamp catalogue and the open decal's stack stay visible together.
    (void)Panels.ConfigureStackedPanels("stamp-library", "stamp-layers", 32.f);
    Panels.ConfigureColumns({{"stamp-layers", 278.f / 1436}, {"stamp-canvas", 820.f / 1436},
                             {"stamp-inspector", 338.f / 1436}});
}

void StampWorkspacePanel::ConfigureInterfaceScale(float Replacement)
{
    Scale = std::isfinite(Replacement) ? std::clamp(Replacement, .75f, 1.5f) : 1;
    Controls.Scale = Scale;
}

void StampWorkspacePanel::ConfigureTypeface(void *Regular, void *Light)
{
    RegularTypeface = Regular;
    LightTypeface = Light;
}

void StampWorkspacePanel::ConfigureDocument(VectorAuthoringDocument Document)
{
    if (!Document.Stamp) throw std::runtime_error("Open a native Stamp document, not a Pattern document.");
    History.Reset(std::move(Document));
    SavedDocument = History.QueryDocument();
    Selection.clear();
    UndoSelections.clear(); RedoSelections.clear();
    GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    MaskSelection = 0;
    LibraryFile.clear(); // a freshly configured document is unbound until opened from / saved to the library
    Diagnostic.clear();
}

void StampWorkspacePanel::RefreshLayerAtlas()
{
    const uint64_t CurrentRevision = QueryRevision();
    const uint32_t CardWell = QueryCanvasGroundAbgr();
    if (CurrentRevision == LayerAtlasBakedRevision && CardWell == LayerAtlasBakedGroundAbgr) return;
    LayerAtlasBakedRevision = CurrentRevision;
    LayerAtlasBakedGroundAbgr = CardWell;
    const auto &Document = QueryDocument();
    if (Document.Layers.empty()) { LayerAtlasPixels.clear(); return; }
    const size_t Slotted = std::min(Document.Layers.size(), LayerAtlasCapacity);
    std::vector<uint8_t> Blank;
    const auto EnsureBlank = [&]() -> const std::vector<uint8_t> &
    {
        if (Blank.empty())
        {
            Blank.resize(size_t(LayerAtlasTile) * size_t(LayerAtlasTile) * 4u);
            for (size_t Pixel = 0; Pixel < Blank.size(); Pixel += 4u)
            {
                Blank[Pixel] = uint8_t(CardWell & 255u);
                Blank[Pixel + 1u] = uint8_t((CardWell >> 8) & 255u);
                Blank[Pixel + 2u] = uint8_t((CardWell >> 16) & 255u);
                Blank[Pixel + 3u] = 255u;
            }
        }
        return Blank;
    };
    std::vector<std::vector<uint8_t>> Tiles;
    Tiles.reserve(Slotted);
    std::map<uint32_t, std::pair<std::string, std::vector<uint8_t>>> FreshCache;
    for (size_t Slot = 0; Slot < Slotted; ++Slot)
    {
        const uint32_t Token = Document.Layers[Slot].Token;
        try
        {
            char WellTag[24]{};
            std::snprintf(WellTag, sizeof(WellTag), "%08x:", CardWell);
            std::string Markup = std::string(WellTag) + EncodePatternLayerThumbnailSvg(Document, Token, CardWell);
            const auto Cached = LayerThumbnailCache.find(Token);
            if (Cached != LayerThumbnailCache.end() && Cached->second.first == Markup)
                Tiles.push_back(Cached->second.second);
            else
                Tiles.push_back(RasterizePatternLayerThumbnail(Document, Token, LayerAtlasTile, CardWell));
            FreshCache.emplace(Token, std::make_pair(std::move(Markup), Tiles.back()));
        }
        catch (const std::exception &) { Tiles.push_back(EnsureBlank()); }
    }
    LayerThumbnailCache = std::move(FreshCache);
    try { LayerAtlasPixels = BlitPatternThumbnailAtlas(Tiles, LayerAtlasTile, LayerAtlasColumns, CardWell); ++LayerAtlasRevision; }
    catch (const std::exception &Failure) { Frontier::DiagnosticJournal::Append("StampLayerAtlasBlitError", Failure.what()); }
}

uint64_t StampWorkspacePanel::QueryLayerAtlasRevision()
{
    RefreshLayerAtlas();
    return LayerAtlasRevision;
}

const std::vector<uint8_t> &StampWorkspacePanel::QueryLayerAtlasPixels()
{
    RefreshLayerAtlas();
    return LayerAtlasPixels;
}

void StampWorkspacePanel::EnsureStampLibrary(bool ForceReload)
{
    if (LibraryScanned && !ForceReload) return;
    LibraryScanned = true;
    Library.Reload(StampLibraryDirectory);
    ++LibraryScanGeneration;
    // 📝 Reopenability (a .stamp.toml vector sidecar beside the PNG) is probed once per scan, never
    // per frame per card — cards just read the cached flag for their slot.
    LibraryEntryReopenable.clear();
    for (const auto &Entry : Library.Entries())
    {
        bool Reopenable = false;
        if (!Entry.Builtin && !Entry.File.empty())
        {
            std::error_code Failure;
            Reopenable = std::filesystem::exists(
                Entry.File.parent_path() / (Entry.File.stem().string() + ".stamp.toml"), Failure);
        }
        LibraryEntryReopenable.push_back(Reopenable ? 1 : 0);
    }
}

void StampWorkspacePanel::RefreshLibraryAtlas()
{
    // 📝 One cover-fit tile per library entry (slot = entry index, so searches/filters only re-point
    // card UVs), composited over the stamp picker's two-tone checker (0x1e1e1e/0x2a2a2a): white
    // preset art and dark authored ink both stay visible, and a card here matches the same stamp's
    // card in Texture Paint's picker. Rebakes wholesale per scan generation — tiles are bilinear
    // downsamples of rasters the library already holds, so no Disk cache (unlike Pattern's atlas).
    EnsureStampLibrary();
    if (LibraryAtlasBakedGeneration == LibraryScanGeneration && !LibraryAtlasPixels.empty()) return;
    LibraryAtlasBakedGeneration = LibraryScanGeneration;
    constexpr uint32_t CardWell = uint32_t(255) << 24 | uint32_t(30) << 16 | uint32_t(30) << 8 | uint32_t(30);
    const auto &Entries = Library.Entries();
    const size_t Slotted = std::min(Entries.size(), LibraryAtlasCapacity);
    std::vector<std::vector<uint8_t>> Tiles;
    Tiles.reserve(Slotted);
    for (size_t Slot = 0; Slot < Slotted; ++Slot)
    {
        const auto &Entry = Entries[Slot];
        // 🎨 Full-fidelity tiles: builtins bake from a fresh 512px field render (not the 256px picker
        // raster), and every tile texel is a 2x2-supersampled box of the source - cards show the
        // stamp's real detail, never a point-sampled miniature.
        const StampLibraryEntry *Source = &Entry;
        StampLibraryEntry HiRes;
        if (Entry.Builtin && StampLibrary::RenderBuiltinAt(Entry.Id, 512, HiRes)) Source = &HiRes;
        const float SourceAspect = Entry.Height > 0 ? float(Entry.Width) / float(Entry.Height) : 1.f;
        std::vector<uint8_t> Tile(size_t(LibraryAtlasTile) * LibraryAtlasTile * 4);
        for (uint32_t Y = 0; Y < LibraryAtlasTile; ++Y)
            for (uint32_t X = 0; X < LibraryAtlasTile; ++X)
            {
                std::array<float, 4> Texel{0, 0, 0, 0};
                for (int Sy = 0; Sy < 2; ++Sy)
                    for (int Sx = 0; Sx < 2; ++Sx)
                    {
                        float U = (X + .25f + .5f * Sx) / LibraryAtlasTile, V = (Y + .25f + .5f * Sy) / LibraryAtlasTile;
                        if (SourceAspect > 1) U = .5f + (U - .5f) / SourceAspect;
                        else if (SourceAspect < 1) V = .5f + (V - .5f) * SourceAspect;
                        const auto Tap = StampLibrary::Sample(*Source, U, V);
                        for (int Channel = 0; Channel < 4; ++Channel) Texel[size_t(Channel)] += Tap[size_t(Channel)] * .25f;
                    }
                const float Well = ((X < LibraryAtlasTile / 2) == (Y < LibraryAtlasTile / 2)) ? 42.f : 30.f;
                uint8_t *Target = &Tile[(size_t(Y) * LibraryAtlasTile + X) * 4];
                for (int Channel = 0; Channel < 3; ++Channel)
                    Target[Channel] = uint8_t(std::clamp(Texel[size_t(Channel)] * Texel[3] +
                        (Well / 255.f) * (1.f - Texel[3]), 0.f, 1.f) * 255.f + .5f);
                Target[3] = 255;
            }
        Tiles.push_back(std::move(Tile));
    }
    // 📝 Same guard as the layer atlas: a blit failure must never reach the render loop uncaught;
    // the next scan generation simply retries the bake.
    try { LibraryAtlasPixels = BlitPatternThumbnailAtlas(Tiles, LibraryAtlasTile, LibraryAtlasColumns, CardWell); ++LibraryAtlasRevision; }
    catch (const std::exception &Failure) { Frontier::DiagnosticJournal::Append("StampLibraryAtlasBlitError", Failure.what()); }
}

uint64_t StampWorkspacePanel::QueryLibraryAtlasRevision()
{
    RefreshLibraryAtlas();
    return LibraryAtlasRevision;
}

const std::vector<uint8_t> &StampWorkspacePanel::QueryLibraryAtlasPixels()
{
    RefreshLibraryAtlas();
    return LibraryAtlasPixels;
}

std::vector<size_t> StampWorkspacePanel::QueryLibraryMatches() const
{
    std::string Query = LibrarySearchText.data();
    const auto Trimmed = Query.find_last_not_of(" \t\n\r");
    Query = Trimmed == std::string::npos ? "" : Query.substr(0, Trimmed + 1);
    Query = PanelChrome::Lowered(Query);
    std::vector<size_t> Matches;
    const auto &Entries = Library.Entries();
    for (size_t Index = 0; Index < Entries.size(); ++Index)
    {
        const auto &Entry = Entries[Index];
        if (LibraryFilter == 1 && !Entry.Builtin) continue;
        if (LibraryFilter == 2 && Entry.Builtin) continue;
        if (!Query.empty() && PanelChrome::Lowered(Entry.Name).find(Query) == std::string::npos) continue;
        Matches.push_back(Index);
    }
    return Matches;
}

void StampWorkspacePanel::OpenLibraryStamp(const StampLibraryEntry &Entry, bool Reopenable)
{
    LibrarySelected = Entry.Id;
    if (!Reopenable)
    {
        // 📝 Preset art and raster-only stamps have no vector source to author - say so plainly
        // instead of opening the grey placeholder box an imported-image component would show.
        Diagnostic = Entry.Builtin
            ? "'" + Entry.Name + "' is preset art - place it from Texture Paint's stamp tool. Stamps you save from this workspace reopen here as editable vector designs."
            : "'" + Entry.Name + "' is a raster-only stamp (no saved vector source) - place it from Texture Paint's stamp tool.";
        return;
    }
    try
    {
        const auto Sidecar = Entry.File.parent_path() / (Entry.File.stem().string() + ".stamp.toml");
        std::ifstream Input(Sidecar, std::ios::binary);
        if (!Input) throw std::runtime_error("The stamp's vector source could not be read.");
        std::ostringstream Buffer;
        Buffer << Input.rdbuf();
        ConfigureDocument(DecodeVectorAuthoring(Buffer.str()));
        LibraryFile = Entry.File; // Save now updates this very library stamp in place
        Diagnostic = "Opened '" + Entry.Name + "' from the stamp library.";
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

const VectorAuthoringDocument &StampWorkspacePanel::QueryDocument() const
{
    return GestureDraft ? *GestureDraft : History.QueryDocument();
}

PlaneExtent StampWorkspacePanel::QueryControlExtent(const std::string &CaptionText) const
{
    const auto Position = ControlExtents.find(CaptionText);
    return Position == ControlExtents.end() ? PlaneExtent{} : Position->second;
}

PlaneExtent StampWorkspacePanel::QueryPanelExtent(const std::string &CaptionText) const
{
    const auto Position = PanelExtents.find(CaptionText);
    return Position == PanelExtents.end() ? PlaneExtent{} : Position->second;
}

void StampWorkspacePanel::CapturePanel(const char *CaptionText)
{
    const auto Position = ImGui::GetWindowPos(), Extent = ImGui::GetWindowSize();
    PanelExtents[CaptionText] = {Position.x, Position.y, Position.x + Extent.x, Position.y + Extent.y};
}

void StampWorkspacePanel::Finalize(VectorAuthoringDocument Document, std::vector<uint32_t> Tokens)
{
    const auto PreviousSelection = GestureSource ? GestureSelection : Selection;
    if (History.Finalize(std::move(Document)))
    {
        UndoSelections.push_back(PreviousSelection);
        if (UndoSelections.size() > 80) UndoSelections.erase(UndoSelections.begin());
        RedoSelections.clear();
    }
    ++GestureRevision;
    Selection = std::move(Tokens);
    std::erase_if(Selection, [&](uint32_t Token) { return std::none_of(History.QueryDocument().Layers.begin(), History.QueryDocument().Layers.end(), [Token](const auto &Layer) { return Layer.Token == Token; }); });
    if (std::find(Selection.begin(), Selection.end(), MaskSelection) == Selection.end()) MaskSelection = 0;
    Diagnostic.clear();
}

void StampWorkspacePanel::EvaluateEdit(const std::function<void(VectorAuthoringDocument &)> &Edit)
{
    try
    {
        auto Document = History.QueryDocument();
        Edit(Document);
        Finalize(std::move(Document), Selection);
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::TransferSelection(bool Remove)
{
    try
    {
        if (Gesture != 0 || MaskSelection) throw std::runtime_error("Finish the current gesture and select components before copying.");
        auto Fragment = ExtractVectorAuthoringSelection(History.QueryDocument(), Selection);
        if (Remove)
        {
            auto Document = History.QueryDocument();
            PartitionVectorAuthoringSelection(Document, Selection);
            Finalize(std::move(Document), {});
        }
        SelectionClipboard = std::move(Fragment);
        Diagnostic.clear();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::InsertSelection(bool InPlace)
{
    try
    {
        if (Gesture != 0 || MaskSelection || !SelectionClipboard) throw std::runtime_error("Copy components before pasting; finish any active gesture first.");
        auto Document = History.QueryDocument();
        auto Tokens = IntegrateVectorAuthoringSelection(Document, *SelectionClipboard,
            InPlace ? std::array<float, 2>{0, 0} : std::array<float, 2>{18, 18});
        Finalize(std::move(Document), std::move(Tokens));
        Tool = VectorAuthoringTool::Select;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::ReplicateSelection()
{
    try
    {
        if (Gesture != 0 || MaskSelection) throw std::runtime_error("Finish the current gesture before duplicating a component.");
        auto Document = History.QueryDocument();
        TransformVectorAuthoringSelection(Document, Selection, {});
        const auto Fragment = ExtractVectorAuthoringSelection(Document, Selection);
        auto Tokens = IntegrateVectorAuthoringSelection(Document, Fragment);
        Finalize(std::move(Document), std::move(Tokens));
        Tool = VectorAuthoringTool::Select;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::SelectEditableVectors()
{
    Selection.clear(); MaskSelection = 0;
    for (const auto &Layer : History.QueryDocument().Layers)
        if (QueryVectorAuthoringVisibility(History.QueryDocument(), Layer) && !QueryVectorAuthoringProtection(History.QueryDocument(), Layer))
            Selection.push_back(Layer.Token);
}

void StampWorkspacePanel::ApplySourcePatch(const VectorAuthoringSourcePatch &Patch)
{
    EvaluateEdit([&](auto &Document) { TransformVectorAuthoringSelection(Document, Selection, Patch); });
}

void StampWorkspacePanel::RestoreHistory(bool Redo)
{
    GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    auto &Source = Redo ? RedoSelections : UndoSelections;
    auto &Destination = Redo ? UndoSelections : RedoSelections;
    if (History.RestoreHistory(Redo) && !Source.empty())
    {
        ++GestureRevision;
        Destination.push_back(Selection);
        Selection = std::move(Source.back());
        Source.pop_back();
        Diagnostic.clear();
    }
    std::erase_if(Selection, [&](uint32_t Token) { return std::none_of(QueryDocument().Layers.begin(), QueryDocument().Layers.end(), [Token](const auto &Layer) { return Layer.Token == Token; }); });
    MaskSelection = 0;
}

void StampWorkspacePanel::Select(uint32_t Token, bool Extend)
{
    if (!Extend) Selection.clear();
    const auto Position = std::find(Selection.begin(), Selection.end(), Token);
    if (Extend && Position != Selection.end()) Selection.erase(Position);
    else Selection.push_back(Token);
    MaskSelection = 0;
}

float StampWorkspacePanel::QueryCanvasExtent() const noexcept
{
    return std::clamp(QueryDocument().CanvasExtent, 64.f, 4096.f);
}

void StampWorkspacePanel::ApplyCanvasExtent(float NewExtent)
{
    const float Target = std::clamp(std::round(NewExtent), 64.f, 4096.f);
    const float Previous = QueryCanvasExtent();
    if (std::abs(Target - Previous) < .01f) return;
    const float Ratio = Target / Previous;
    EvaluateEdit([&](auto &Document)
    {
        Document.CanvasExtent = Target;
        if (ScaleComponentsOnCanvasResize)
        {
            const auto RescaleInstruction = [&](VectorCoverageInstruction &Instruction)
            {
                if (Instruction.Topology[0] > 6) return;
                for (int Axis = 0; Axis < 4; ++Axis) Instruction.Bounds[Axis] *= Ratio;
                Instruction.Transform[1] = std::clamp(Instruction.Transform[1] * Ratio, 0.f, 100.f);
                if (Instruction.Topology[0] == 6 && Instruction.Paint[3] != 0) Instruction.Paint[0] *= Ratio;
                if (Instruction.Topology[0] == 0) Instruction.ShapeParameters[0] = std::clamp(Instruction.ShapeParameters[0] * Ratio, 0.f, 50.f);
            };
            for (auto &Instruction : Document.Instructions) RescaleInstruction(Instruction);
            for (auto &Layer : Document.Layers)
            {
                if (Layer.Mask) RescaleInstruction(*Layer.Mask);
                if (Layer.ComponentKind == VectorAuthoringComponentKind::Text)
                    Layer.TextPointSize = std::clamp(Layer.TextPointSize * Ratio, 4.f, 512.f);
            }
        }
    });
    PbrLightX *= Ratio;
    PbrLightY *= Ratio;
}

uint32_t StampWorkspacePanel::QueryCanvasGroundAbgr() const noexcept
{
    if (TransparentCanvas)
        return uint32_t(255) << 24 | uint32_t(36) << 16 | uint32_t(38) << 8 | uint32_t(36);
    const uint32_t R = uint32_t(std::lround(std::clamp(CanvasGroundColour[0], 0.f, 1.f) * 255.f));
    const uint32_t G = uint32_t(std::lround(std::clamp(CanvasGroundColour[1], 0.f, 1.f) * 255.f));
    const uint32_t B = uint32_t(std::lround(std::clamp(CanvasGroundColour[2], 0.f, 1.f) * 255.f));
    return uint32_t(255) << 24 | B << 16 | G << 8 | R;
}

std::array<float, 4> StampWorkspacePanel::OppositeCanvasColour() const noexcept
{
    if (TransparentCanvas) return {1.f, 1.f, 1.f, 1.f};
    const float R = std::clamp(CanvasGroundColour[0], 0.f, 1.f);
    const float G = std::clamp(CanvasGroundColour[1], 0.f, 1.f);
    const float B = std::clamp(CanvasGroundColour[2], 0.f, 1.f);
    std::array<float, 4> Opposite{1.f - R, 1.f - G, 1.f - B, 1.f};
    const float BgLum = .2126f * R + .7152f * G + .0722f * B;
    const float OppLum = .2126f * Opposite[0] + .7152f * Opposite[1] + .0722f * Opposite[2];
    if (std::abs(OppLum - BgLum) < .40f)
    {
        const float Pole = BgLum > .5f ? 0.f : 1.f;
        for (int Channel = 0; Channel < 3; ++Channel)
            Opposite[Channel] = std::clamp(Opposite[Channel] * .28f + Pole * .72f, 0.f, 1.f);
    }
    return Opposite;
}

std::array<float, 4> StampWorkspacePanel::CanvasInkColour() const noexcept
{
    return OppositeCanvasColour();
}

void StampWorkspacePanel::ConstructPrimitive(uint32_t Category, const char *CaptionText)
{
    try
    {
        auto Document = History.QueryDocument();
        const float CanvasUnits = std::clamp(Document.CanvasExtent, 64.f, 4096.f);
        const float DefaultExtent = std::clamp(CanvasUnits * .3125f, 16.f, 1024.f);
        const auto Ink = OppositeCanvasColour();
        VectorCoverageInstruction Source;
        Source.Topology[0] = Category;
        Source.Bounds = {CanvasUnits * .5f, CanvasUnits * .5f, DefaultExtent, DefaultExtent};
        Source.Colour = Ink;
        Source.ColourEnd = {.35f, .55f, .45f, 1};
        Source.StrokeColour = Ink;
        Source.Topology[3] |= VectorCoverageStrokeColourExplicit;
        if (Category == 4) Source.Paint[2] = 5.f;
        else if (Category == 5) { Source.Paint[2] = 5.f; Source.Paint[1] = .5f; }
        if (Document.Layers.empty()) Document.Name = "Custom stamp";
        const auto Token = ConstructVectorAuthoringPrimitive(Document, Source, CaptionText == std::string_view(ShapeCaptions[Category]) ? ShapeNames[Category] : CaptionText);
        Finalize(std::move(Document), {Token});
        SearchText[0] = 0; Filter = 0;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                               PEN, POLYLINE AND POINTS TOOLS
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::CancelPen()
{
    PenSource.reset(); PenPreview.reset(); PendingPath = {}; PenSelection.clear(); ++GestureRevision;
    PenToken = 0; PenAnchorFloor = 0; PenReversed = false; IndependentPenContinuation = false; PolylineSession = false;
    Diagnostic.clear();
    if (Gesture == 12) Gesture = 0;
}

void StampWorkspacePanel::ResumePen(uint32_t Token, bool FromStart)
{
    const auto &Source = History.QueryDocument();
    const auto Position = std::find_if(Source.Layers.begin(), Source.Layers.end(), [Token](const auto &Layer) { return Layer.Token == Token; });
    if (Position == Source.Layers.end() || MaskSelection || QueryVectorAuthoringProtection(Source, *Position)
        || !QueryVectorAuthoringVisibility(Source, *Position)) throw std::runtime_error("Select one visible, editable open cubic source to continue.");
    auto Path = DecodeCubicPath(Source, Token);
    if (Path.Closed || Path.Anchors.size() >= 400) throw std::runtime_error("Continue an open path below its 400-anchor budget.");
    if (FromStart) ReverseCubicPath(Path);
    auto &Endpoint = Path.Anchors.back();
    for (size_t Axis = 0; Axis < 2; ++Axis) Endpoint.Outgoing[Axis] = 2 * Endpoint.Position[Axis] - Endpoint.Incoming[Axis];
    CancelPen();
    PenSource = Source;
    PenSelection = Selection;
    PendingPath = std::move(Path);
    PenToken = Token; PenAnchorFloor = PendingPath.Anchors.size(); PenReversed = FromStart;
    Tool = VectorAuthoringTool::Pen;
}

void StampWorkspacePanel::RefreshPenPreview()
{
    PenPreview.reset();
    ++GestureRevision;
    if (!PenSource || PendingPath.Anchors.size() < 2) return;
    if (PenToken && PendingPath.Anchors.size() == PenAnchorFloor && !PendingPath.Closed) return;
    auto Draft = *PenSource;
    if (PenToken)
    {
        auto Path = PendingPath;
        if (PenReversed) ReverseCubicPath(Path);
        ReplaceCubicPath(Draft, PenToken, Path);
        PenPreview = std::move(Draft);
        return;
    }
    VectorCoverageInstruction Style;
    Style.Transform[1] = 1.4f;
    // New pen/polyline paths draw in canvas-contrast ink (the instruction default is white,
    // which vanishes on the light board); recolour any time from the Inspector.
    Style.Colour = CanvasInkColour();
    Style.StrokeColour = Style.Colour;
    Style.Topology[3] |= VectorCoverageStrokeColourExplicit;
    Style.Paint[3] = PendingPath.Closed ? 1.f : 0.f;
    auto Path = PendingPath;
    if (AutomaticPen && !PolylineSession)
    {
        std::vector<size_t> Indices;
        for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) Indices.push_back(Index);
        ConfigureAutomaticCubicTangents(Path, Indices);
    }
    ConstructCubicPath(Draft, Path, Style, "Pen contour");
    PenPreview = std::move(Draft);
}

bool StampWorkspacePanel::FinishPen(bool Closed)
{
    if (!PenSource) return true;
    try
    {
        if (PenToken && PendingPath.Anchors.size() == PenAnchorFloor && !Closed) { CancelPen(); return true; }
        auto Path = PendingPath; Path.Closed = Closed;
        if (AutomaticPen && !PenToken && !PolylineSession)
        {
            std::vector<size_t> Indices;
            for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) Indices.push_back(Index);
            ConfigureAutomaticCubicTangents(Path, Indices);
        }
        if (PenReversed) ReverseCubicPath(Path);
        auto Draft = *PenSource;
        VectorCoverageInstruction Style;
        Style.Transform[1] = 1.4f;
        Style.Colour = CanvasInkColour();
        Style.StrokeColour = Style.Colour;
        Style.Topology[3] |= VectorCoverageStrokeColourExplicit;
        // 📝 Seed the closed/filled state's own independent outline width (Paint[0]) to match the
        // line's starting thickness, purely for visual continuity the instant a brand-new path first
        // closes into a shape - from here on the two widths are edited and stored completely separately.
        if (Closed) Style.Paint[0] = Style.Transform[1];
        uint32_t Token = PenToken;
        if (Token) ReplaceCubicPath(Draft, Token, Path);
        else Token = ConstructCubicPath(Draft, Path, Style, "Pen contour");
        Selection = PenSelection;
        CancelPen();
        Finalize(std::move(Draft), {Token});
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); return false; }
}

bool StampWorkspacePanel::ConstructPenInteraction(PlaneExtent Canvas, PlaneExtent Board, bool Hovered)
{
    if (Tool != VectorAuthoringTool::Pen && Tool != VectorAuthoringTool::Polyline && !PenSource) return false;
    const auto &Input = ImGui::GetIO();
    if (PenSource && Tool != VectorAuthoringTool::Pen && Tool != VectorAuthoringTool::Polyline)
    {
        if (!FinishPen(false)) Tool = PolylineSession ? VectorAuthoringTool::Polyline : VectorAuthoringTool::Pen;
        return true;
    }
    if (PenSource && (Input.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape)))
    {
        CancelPen(); return true;
    }
    if (PenSource && !Input.WantTextInput)
    {
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) { Save(); return true; }
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) { CancelPen(); return true; }
        if (Gesture == 0 && !Input.KeyCtrl)
        {
            std::optional<VectorAuthoringTool> Requested;
            if (ImGui::IsKeyPressed(ImGuiKey_V)) Requested = VectorAuthoringTool::Select;
            if (ImGui::IsKeyPressed(ImGuiKey_A)) Requested = VectorAuthoringTool::Points;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) Requested = VectorAuthoringTool::Rectangle;
            if (ImGui::IsKeyPressed(ImGuiKey_O)) Requested = VectorAuthoringTool::Ellipse;
            if (!PolylineSession && ImGui::IsKeyPressed(ImGuiKey_L)) Requested = VectorAuthoringTool::Polyline;
            if (PolylineSession && ImGui::IsKeyPressed(ImGuiKey_P)) Requested = VectorAuthoringTool::Pen;
            if (Requested) { if (FinishPen(false)) Tool = *Requested; return true; }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) { FinishPen(false); return true; }
        if (Gesture == 0 && ImGui::IsKeyPressed(ImGuiKey_Backspace))
        {
            if (PenToken && PendingPath.Anchors.size() <= PenAnchorFloor) { CancelPen(); return true; }
            PendingPath.Anchors.pop_back();
            if (PendingPath.Anchors.empty()) CancelPen();
            else RefreshPenPreview();
            return true;
        }
    }
    if (MaskSelection) return false;
    const float CanvasUnits = QueryCanvasExtent();
    const auto DocumentPoint = [&]()
    {
        std::array<float, 2> Coordinate{(Input.MousePos.x - Board.MinimumX) / Board.Width() * CanvasUnits,
            (Input.MousePos.y - Board.MinimumY) / Board.Height() * CanvasUnits};
        if (SnappingEnabled) for (auto &Position : Coordinate) Position = std::round(Position / 16) * 16;
        return Coordinate;
    };
    const auto Project = [&](std::array<float, 2> Coordinate)
    {
        return ImVec2(Board.MinimumX + Coordinate[0] / CanvasUnits * Board.Width(), Board.MinimumY + Coordinate[1] / CanvasUnits * Board.Height());
    };
    uint32_t ResumeToken = 0;
    bool ResumeStart = false;
    if (!PenSource && Selection.size() == 1)
    {
        const auto &Source = History.QueryDocument();
        const auto Position = std::find_if(Source.Layers.begin(), Source.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
        if (Position != Source.Layers.end() && !QueryVectorAuthoringProtection(Source, *Position)
            && QueryVectorAuthoringVisibility(Source, *Position) && Source.Instructions[Position->Root].Topology[0] == 6)
        {
            try
            {
                const auto Path = DecodeCubicPath(Source, Position->Token);
                if (!Path.Closed && Path.Anchors.size() < 400)
                {
                    auto *Ink = ImGui::GetWindowDrawList();
                    Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
                    for (bool FromStart : {true, false})
                    {
                        const auto Endpoint = Project(FromStart ? Path.Anchors.front().Position : Path.Anchors.back().Position);
                        const float Radius = 7 * Scale;
                        ControlExtents[FromStart ? "Resume start endpoint" : "Resume end endpoint"] = {Endpoint.x - Radius, Endpoint.y - Radius, Endpoint.x + Radius, Endpoint.y + Radius};
                        Ink->AddCircle(Endpoint, 5 * Scale, IM_COL32(204, 230, 181, 255));
                        if (std::hypot(Input.MousePos.x - Endpoint.x, Input.MousePos.y - Endpoint.y) <= Radius)
                        {
                            ResumeToken = Position->Token; ResumeStart = FromStart;
                        }
                    }
                    Ink->PopClipRect();
                }
            }
            catch (const std::exception &) {}
        }
    }
    uint32_t JoinToken = 0;
    bool JoinEnd = false;
    if (PenSource && PenToken && Gesture == 0)
    {
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
        float Nearest = 7 * Scale;
        for (auto Layer = PenSource->Layers.rbegin(); Layer != PenSource->Layers.rend(); ++Layer)
        {
            if (Layer->Token == PenToken || !QueryVectorAuthoringVisibility(*PenSource, *Layer)
                || PenSource->Instructions[Layer->Root].Topology[0] != 6) continue;
            CubicPath Path;
            try { Path = DecodeCubicPath(*PenSource, Layer->Token); }
            catch (const std::exception &) { continue; } // Compound contours remain visible but are not endpoint targets.
            if (Path.Closed) continue;
            for (bool End : {false, true})
            {
                const auto Endpoint = Project(End ? Path.Anchors.back().Position : Path.Anchors.front().Position);
                const float Radius = 7 * Scale;
                ControlExtents[std::string(End ? "Join end endpoint:" : "Join start endpoint:") + std::to_string(Layer->Token)]
                    = {Endpoint.x - Radius, Endpoint.y - Radius, Endpoint.x + Radius, Endpoint.y + Radius};
                Ink->AddCircle(Endpoint, 5 * Scale, IM_COL32(204, 230, 181, 255));
                const float Distance = std::hypot(Input.MousePos.x - Endpoint.x, Input.MousePos.y - Endpoint.y);
                if (Distance < Nearest)
                {
                    Nearest = Distance; JoinToken = Layer->Token; JoinEnd = End;
                }
            }
        }
        Ink->PopClipRect();
    }
    bool Consumed = Gesture == 12;
    try
    {
        if (Gesture == 0 && Hovered && ImGui::IsMouseClicked(0) && !ImGui::IsKeyDown(ImGuiKey_Space))
        {
            Consumed = true;
            if (!PenSource && ResumeToken && !Input.KeyCtrl)
            {
                ResumePen(ResumeToken, ResumeStart);
                IndependentPenContinuation = false;
                PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * CanvasUnits,
                    (Input.MousePos.y - Board.MinimumY) / Board.Height() * CanvasUnits};
                Gesture = 12;
                return true;
            }
            if (PenSource && !PendingPath.Anchors.empty())
            {
                const auto Last = Project(PendingPath.Anchors.back().Position);
                if (!PolylineSession && Input.KeyAlt && std::hypot(Input.MousePos.x - Last.x, Input.MousePos.y - Last.y) <= 7 * Scale)
                {
                    if (AutomaticPen && !PenToken) throw std::runtime_error("Disable automatic Pen curves before breaking an outgoing tangent.");
                    // 📝 Keep the incoming curve; only the following segment receives this corner or independent drag.
                    PendingPath.Anchors.back().Outgoing = PendingPath.Anchors.back().Position;
                    IndependentPenContinuation = true;
                    PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * CanvasUnits,
                        (Input.MousePos.y - Board.MinimumY) / Board.Height() * CanvasUnits};
                    Gesture = 12;
                    RefreshPenPreview();
                    return true;
                }
                const auto First = Project(PendingPath.Anchors.front().Position);
                if (std::hypot(Input.MousePos.x - First.x, Input.MousePos.y - First.y) <= 7 * Scale)
                {
                    if (PendingPath.Anchors.size() < 3) throw std::runtime_error("A closed path needs at least three anchors.");
                    FinishPen(true); return true;
                }
            }
            if (JoinToken && !Input.KeyCtrl)
            {
                // 📝 Build and validate the entire continuation/join before consuming either retained source.
                auto Draft = *PenSource;
                auto Path = PendingPath;
                if (PenReversed) ReverseCubicPath(Path);
                ReplaceCubicPath(Draft, PenToken, Path);
                const auto Token = IntegrateCubicPaths(Draft, PenToken, JoinToken, PenReversed, JoinEnd);
                Selection = PenSelection;
                CancelPen();
                Finalize(std::move(Draft), {Token});
                return true;
            }
            if (PendingPath.Anchors.size() >= 400) throw std::runtime_error("Pen paths support at most 400 anchors.");
            if (!PenSource)
            {
                PenSource = History.QueryDocument(); PenSelection = Selection;
                PendingPath = {};
                PolylineSession = Tool == VectorAuthoringTool::Polyline;
            }
            auto Position = DocumentPoint();
            if (Input.KeyShift && !PendingPath.Anchors.empty()) Position = ConstrainCubicDirection(PendingPath.Anchors.back().Position, Position);
            IndependentPenContinuation = false;
            PendingPath.Anchors.push_back({Position, Position, Position});
            PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * CanvasUnits,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * CanvasUnits};
            Gesture = 12;
            RefreshPenPreview();
        }
        if (Gesture == 12 && PenSource && ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMousePosValid())
        {
            // 📝 Move only the newly placed anchor, never the retained endpoint being resumed.
            if (!PenToken || PendingPath.Anchors.size() > PenAnchorFloor)
            {
                auto &Anchor = PendingPath.Anchors.back();
                const std::array<float, 2> Displacement{Input.MouseDelta.x / Board.Width() * CanvasUnits,
                    Input.MouseDelta.y / Board.Height() * CanvasUnits};
                for (auto *Point : {&Anchor.Position, &Anchor.Incoming, &Anchor.Outgoing})
                    for (size_t Axis = 0; Axis < 2; ++Axis) (*Point)[Axis] += Displacement[Axis];
                PointerOrigin.X += Displacement[0]; PointerOrigin.Y += Displacement[1];
                RefreshPenPreview();
            }
        }
        else
        if (Gesture == 12 && PenSource && (ImGui::IsMouseDragging(0, 2) || ImGui::IsMouseReleased(0)) && ImGui::IsMousePosValid()
            && std::hypot((Input.MousePos.x - Board.MinimumX) / Board.Width() * CanvasUnits - PointerOrigin.X,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * CanvasUnits - PointerOrigin.Y) * Board.Width() / CanvasUnits >= 2 * Scale)
        {
            if (!PolylineSession)
            {
                auto &Anchor = PendingPath.Anchors.back();
                auto Position = DocumentPoint();
                if (Input.KeyShift) Position = ConstrainCubicDirection(Anchor.Position, Position);
                Anchor.Outgoing = Position;
                // 📝 The resumed endpoint's incoming control belongs to an existing segment; never mirror over it.
                if (!IndependentPenContinuation && (!PenToken || PendingPath.Anchors.size() > PenAnchorFloor))
                    for (size_t Axis = 0; Axis < 2; ++Axis)
                        Anchor.Incoming[Axis] = Input.KeyAlt ? Anchor.Position[Axis] : 2 * Anchor.Position[Axis] - Position[Axis];
                RefreshPenPreview();
            }
        }
        if (PenSource)
        {
            auto *Ink = ImGui::GetWindowDrawList();
            Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
            auto Guides = PendingPath;
            if (AutomaticPen && !PenToken && !PolylineSession && Guides.Anchors.size() >= 2)
            {
                std::vector<size_t> Indices;
                for (size_t Index = 0; Index < Guides.Anchors.size(); ++Index) Indices.push_back(Index);
                ConfigureAutomaticCubicTangents(Guides, Indices);
            }
            for (size_t Index = 0; Index < Guides.Anchors.size(); ++Index)
            {
                const auto &Anchor = Guides.Anchors[Index];
                const auto Position = Project(Anchor.Position);
                ControlExtents["Pen anchor " + std::to_string(Index)] = {Position.x - 6 * Scale, Position.y - 6 * Scale, Position.x + 6 * Scale, Position.y + 6 * Scale};
                Ink->AddCircle(Position, 4 * Scale, IM_COL32(204, 230, 181, 255));
                if (Index + 1 == PendingPath.Anchors.size())
                {
                    Ink->AddLine(Project(Anchor.Incoming), Project(Anchor.Outgoing), IM_COL32(170, 197, 150, 210));
                    for (auto Coordinate : {Anchor.Incoming, Anchor.Outgoing}) Ink->AddCircleFilled(Project(Coordinate), 3 * Scale, IM_COL32(204, 230, 181, 255));
                }
            }
            Ink->PopClipRect();
        }
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
    return Consumed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RETAINED POINT GUIDES AND INPUT
//------------------------------------------------------------------------------------------------------------------------

bool StampWorkspacePanel::ConstructPointInteraction(PlaneExtent Canvas, PlaneExtent Board, bool Hovered)
{
    if ((Tool != VectorAuthoringTool::Points && Gesture != 13 && Gesture != 14) || MaskSelection || Selection.size() != 1) return false;
    const auto Snapshot = QueryDocument();
    const auto Position = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
    // 📝 Any non-Boolean source (0-5 primitives, or an existing 6=cubic path) can be driven by this
    // tool: dragging a primitive's corner the first time implicitly converts it via DerivePrimitiveCubicPath,
    // exactly what the Inspector's own "Convert to cubic path" button does explicitly.
    if (Position == Snapshot.Layers.end() || Snapshot.Instructions[Position->Root].Topology[0] > 6
        || QueryVectorAuthoringProtection(Snapshot, *Position) || !QueryVectorAuthoringVisibility(Snapshot, *Position)) return false;
    if (PointToken != Position->Token) { PointToken = Position->Token; ActiveAnchor = SIZE_MAX; SelectedAnchors.clear(); }
    const auto &Input = ImGui::GetIO();
    try
    {
        const auto Category = Snapshot.Instructions[Position->Root].Topology[0];
        const auto Path = Category == 6 ? DecodeCubicPath(Snapshot, PointToken) : DerivePrimitiveCubicPath(Snapshot, PointToken);
        if (ActiveAnchor >= Path.Anchors.size()) ActiveAnchor = SIZE_MAX;
        std::erase_if(SelectedAnchors, [&](size_t Index) { return Index >= Path.Anchors.size(); });
        const float CanvasUnits = std::clamp(Snapshot.CanvasExtent, 64.f, 4096.f);
        const auto Project = [&](std::array<float, 2> Coordinate)
        {
            return ImVec2(Board.MinimumX + Coordinate[0] / CanvasUnits * Board.Width(), Board.MinimumY + Coordinate[1] / CanvasUnits * Board.Height());
        };
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
        size_t Hit = SIZE_MAX;
        CubicControl HitControl = CubicControl::Anchor;
        const auto Grip = [&](size_t Index, CubicControl Control, const char *Caption, std::array<float, 2> Coordinate)
        {
            const auto Point = Project(Coordinate);
            const float Radius = 6 * Scale;
            ControlExtents[std::string(Caption) + std::to_string(Index)] = {Point.x - Radius, Point.y - Radius, Point.x + Radius, Point.y + Radius};
            const bool Selected = std::find(SelectedAnchors.begin(), SelectedAnchors.end(), Index) != SelectedAnchors.end();
            Ink->AddCircleFilled(Point, (Control == CubicControl::Anchor ? 4 : 3) * Scale,
                Selected ? IM_COL32(230, 246, 211, 255) : IM_COL32(130, 153, 117, 255));
            Ink->AddCircle(Point, (Control == CubicControl::Anchor ? 4 : 3) * Scale, IM_COL32(45, 65, 40, 255));
            if (std::hypot(Input.MousePos.x - Point.x, Input.MousePos.y - Point.y) <= Radius)
            {
                Hit = Index; HitControl = Control;
            }
        };
        if (ActiveAnchor != SIZE_MAX)
        {
            const auto &Anchor = Path.Anchors[ActiveAnchor];
            for (auto Control : {CubicControl::Incoming, CubicControl::Outgoing})
            {
                if ((!Path.Closed && ActiveAnchor == 0 && Control == CubicControl::Incoming)
                    || (!Path.Closed && ActiveAnchor + 1 == Path.Anchors.size() && Control == CubicControl::Outgoing)) continue;
                const auto Coordinate = Control == CubicControl::Incoming ? Anchor.Incoming : Anchor.Outgoing;
                Ink->AddLine(Project(Anchor.Position), Project(Coordinate), IM_COL32(170, 197, 150, 210));
                Grip(ActiveAnchor, Control, Control == CubicControl::Incoming ? "Path incoming " : "Path outgoing ", Coordinate);
            }
        }
        // 📝 Anchors take hit precedence over coincident corner tangents.
        for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) Grip(Index, CubicControl::Anchor, "Path anchor ", Path.Anchors[Index].Position);
        Ink->PopClipRect();
        const std::array<float, 2> Pointer{(Input.MousePos.x - Board.MinimumX) / Board.Width() * CanvasUnits,
            (Input.MousePos.y - Board.MinimumY) / Board.Height() * CanvasUnits};
        if (Gesture == 0 && Hovered && Hit == SIZE_MAX && ImGui::IsMousePosValid() && !ImGui::IsKeyDown(ImGuiKey_Space))
        {
            const auto Projection = ProjectCubicSegment(Path, Pointer);
            if (Projection.Distance * Board.Width() / CanvasUnits <= 6 * Scale && Projection.Parameter >= .02f && Projection.Parameter <= .98f)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(0))
                {
                    SegmentProjection = Projection; SegmentBending = false; PointDragSource = Path;
                    GestureSource = History.QueryDocument(); GestureSelection = Selection;
                    PointerOrigin = {Pointer[0], Pointer[1]}; Gesture = 14;
                }
            }
        }
        if (Gesture == 14 && GestureSource)
        {
            if (std::hypot(Pointer[0] - PointerOrigin.X, Pointer[1] - PointerOrigin.Y) * Board.Width() / CanvasUnits >= 2 * Scale)
                SegmentBending = true;
            if (SegmentBending || ImGui::IsMouseReleased(0))
            {
                auto Edited = PointDragSource;
                if (SegmentBending) BendCubicSegment(Edited, SegmentProjection.SegmentIndex, SegmentProjection.Parameter,
                    {Pointer[0] - PointerOrigin.X, Pointer[1] - PointerOrigin.Y});
                else
                {
                    SubdivideCubicSegment(Edited, SegmentProjection.SegmentIndex, SegmentProjection.Parameter);
                    ActiveAnchor = SegmentProjection.SegmentIndex + 1; SelectedAnchors = {ActiveAnchor};
                }
                auto Draft = *GestureSource; ReplaceCubicPath(Draft, PointToken, Edited); GestureDraft = std::move(Draft); ++GestureRevision;
            }
            return true;
        }
        if (Hovered && Hit != SIZE_MAX) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (Gesture == 0 && Hovered && Hit != SIZE_MAX && ImGui::IsMouseClicked(0) && !ImGui::IsKeyDown(ImGuiKey_Space))
        {
            if (HitControl == CubicControl::Anchor)
            {
                const auto Included = std::find(SelectedAnchors.begin(), SelectedAnchors.end(), Hit);
                if (Input.KeyShift && Included != SelectedAnchors.end())
                {
                    SelectedAnchors.erase(Included);
                    ActiveAnchor = SelectedAnchors.empty() ? SIZE_MAX : SelectedAnchors.back();
                    return true;
                }
                if (Included == SelectedAnchors.end())
                {
                    if (!Input.KeyShift) SelectedAnchors.clear();
                    SelectedAnchors.push_back(Hit);
                }
            }
            ActiveAnchor = Hit; ActiveControl = HitControl;
            PointDragSource = Path;
            GestureSource = History.QueryDocument(); GestureSelection = Selection;
            PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * CanvasUnits,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * CanvasUnits};
            Gesture = 13;
        }
        if (Gesture == 13 && GestureSource && (ImGui::IsMouseDragging(0, 2) || ImGui::IsMouseReleased(0)) && ImGui::IsMousePosValid()
            && std::hypot((Input.MousePos.x - Board.MinimumX) / Board.Width() * CanvasUnits - PointerOrigin.X,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * CanvasUnits - PointerOrigin.Y) * Board.Width() / CanvasUnits >= 2 * Scale)
        {
            const auto &Anchor = PointDragSource.Anchors.at(ActiveAnchor);
            const auto Origin = ActiveControl == CubicControl::Anchor ? Anchor.Position
                : ActiveControl == CubicControl::Incoming ? Anchor.Incoming : Anchor.Outgoing;
            std::array<float, 2> Destination{Origin[0] + (Input.MousePos.x - Board.MinimumX) / Board.Width() * CanvasUnits - PointerOrigin.X,
                Origin[1] + (Input.MousePos.y - Board.MinimumY) / Board.Height() * CanvasUnits - PointerOrigin.Y};
            if (SnappingEnabled) for (auto &Coordinate : Destination) Coordinate = std::round(Coordinate / 16) * 16;
            if (Input.KeyShift && ActiveControl != CubicControl::Anchor) Destination = ConstrainCubicDirection(Anchor.Position, Destination);
            auto Edited = PointDragSource;
            if (ActiveControl == CubicControl::Anchor)
                TranslateCubicAnchors(Edited, SelectedAnchors, {Destination[0] - Origin[0], Destination[1] - Origin[1]});
            else TranslateCubicControl(Edited, ActiveAnchor, ActiveControl, Destination, Input.KeyAlt);
            auto Draft = *GestureSource;
            ReplaceCubicPath(Draft, PointToken, Edited);
            GestureDraft = std::move(Draft);
            ++GestureRevision;
        }
        return Gesture == 13;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        if (Gesture == 13 || Gesture == 14) { GestureDraft.reset(); GestureSource.reset(); Gesture = 0; ++GestureRevision; }
        return true;
    }
}

bool StampWorkspacePanel::Save()
{
    bool DocumentSaved = false;
    if (SaveSource)
    {
        try
        {
            if (!SaveSource(EncodeVectorAuthoring(QueryDocument()))) { Diagnostic = "The host refused the document source (too large or unwritable)."; return false; }
            SavedDocument = QueryDocument();
            DocumentSaved = true;
        }
        catch (const std::exception &Failure) { Diagnostic = Failure.what(); return false; }
    }
    // 📝 Saving in this workspace also publishes the decal into the shared stamp library - updating
    // the library file this document is bound to, or creating its first entry - so the stamp shows
    // in the Stamp library tab and in Texture Paint the moment it is saved, with no second command
    // to discover. "Save to stamp library" (Document actions) remains the explicit save-a-copy path.
    if (PublishToLibrary(false))
    {
        Diagnostic = "Saved '" + QueryDocument().Name + "' - it now shows in the Stamp library tab and Texture Paint's stamp tool.";
        return true;
    }
    if (DocumentSaved)
    {
        // PublishToLibrary left its refusal in Diagnostic; keep it visible next to the good news.
        Diagnostic = "Document saved; stamp library not updated - " + Diagnostic;
        return true;
    }
    return false;
}

bool StampWorkspacePanel::PublishToLibrary(bool AsNewStamp)
{
    if (StampLibraryDirectory.empty()) { Diagnostic = "No stamp library directory is configured by the host."; return false; }
    const auto &Document = QueryDocument();
    if (Document.Layers.empty()) { Diagnostic = "Add a component before saving this stamp to the library."; return false; }
    try
    {
        // 📝 ThorVG's ABGR8888S target lands in memory as R,G,B,A bytes (little-endian word), which is
        // exactly the straight-alpha RGBA the library PNG stores - no swizzle, no unpremultiply.
        // 1024 (was 512): the stored PNG is the fallback raster and every thumbnail's source, so it
        // should itself carry the design at full fidelity even where the vector sidecar can't follow.
        constexpr uint32_t Extent = 1024;
        const std::vector<uint8_t> Pixels = RasterizeStampDocument(Document, Extent);
        bool AnyArt = false;
        for (size_t Pixel = 3; Pixel < Pixels.size(); Pixel += 4)
            if (Pixels[Pixel] > 0) { AnyArt = true; break; }
        if (!AnyArt) { Diagnostic = "The stamp rasterized fully transparent - check component visibility."; return false; }
        const std::string SourceToml = EncodeVectorAuthoring(Document);
        std::error_code PathFailure;
        const bool Bound = !AsNewStamp && !LibraryFile.empty() &&
            std::filesystem::equivalent(LibraryFile.parent_path(), StampLibraryDirectory, PathFailure);
        std::string Id;
        const bool Written = Bound
            ? StampLibrary::SaveRasterToFile(LibraryFile, int(Extent), int(Extent), Pixels, SourceToml, Id, Diagnostic)
            : StampLibrary::SaveRaster(StampLibraryDirectory, Document.Name, int(Extent), int(Extent), Pixels, SourceToml, Id, Diagnostic);
        if (!Written) return false;
        // Bind (or re-bind) this document to the file just written, so the next Save updates the same
        // library card instead of adding another versioned copy; the tab rescans and shows it at once.
        LibraryFile = StampLibraryDirectory / Id.substr(5); // strip the "disk:" prefix
        LibraryScanned = false;
        LibrarySelected = Id;
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); return false; }
}

bool StampWorkspacePanel::SaveToLibrary()
{
    if (!PublishToLibrary(true)) return false;
    Diagnostic = "Saved '" + QueryDocument().Name + "' to the stamp library as a new stamp - pick it from the Stamp library tab or Texture Paint's stamp tool.";
    return true;
}

bool StampWorkspacePanel::RequestClose()
{
    if (GestureSource) { Selection = GestureSelection; GestureSource.reset(); GestureDraft.reset(); Gesture = 0; }
    if (!QueryModified()) return true;
    CloseRequested = true;
    return false;
}

bool StampWorkspacePanel::ConsumeMaterialApplyRequest() noexcept
{
    const bool Requested = ApplyToMaterialRequested;
    ApplyToMaterialRequested = false;
    return Requested;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HEADER AND ADD-COMPONENT MENU
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructHeader()
{
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(Viewport->WorkSize.x, 43 * Scale));
    ImGui::Begin("Stamp header", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar);
    const auto Origin = ImGui::GetWindowPos();
    const float Width = ImGui::GetWindowWidth();
    ImGui::GetWindowDrawList()->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 43 * Scale), ImGui::GetColorU32(Spectrum(0x0b0b0b)));
    Caption(ImVec2(Origin.x + 16 * Scale, Origin.y + 16 * Scale), QueryDocument().Name.c_str(), 10 * Scale, 0xc5c5b8);
    Caption(ImVec2(Origin.x + 22 * Scale + ImGui::CalcTextSize(QueryDocument().Name.c_str()).x, Origin.y + 19 * Scale), ".stamp", 7 * Scale, 0x67675b);
    Controls.ControlHeight = 24;
    const float NavigationX = Origin.x + std::max(230 * Scale, Width - 620 * Scale);
    const char *Workspaces[]{"Material", "Pattern", "Texture", "Stamp", "Baking"};
    const float Widths[]{64, 58, 54, 52, 64};
    float PositionX = NavigationX;
    for (size_t Index = 0; Index < 5; ++Index)
    {
        const bool Available = true;
        ImGui::SetCursorScreenPos(ImVec2(PositionX, Origin.y + 9 * Scale));
        ImGui::BeginDisabled((Index == 0 && !ActivateMaterial) || (Index == 1 && !ActivatePattern) || (Index == 2 && !ActivateTexture) || (Index == 4 && !ActivateBaking));
        if (Controls.Button(Workspaces[Index], Widths[Index] * Scale, Index == 3, {}, true))
        {
            if (Index == 0 && ActivateMaterial) ActivateMaterial();
            if (Index == 1 && ActivatePattern) ActivatePattern();
            if (Index == 2 && ActivateTexture) ActivateTexture();
            if (Index == 4 && ActivateBaking) ActivateBaking();
        }
        Controls.Tooltip(Workspaces[Index], Available ? "Switch workspace without losing your document." : "This native workspace is not hosted yet.");
        ImGui::EndDisabled();
        PositionX += Widths[Index] * Scale;
    }
    const auto HeaderButton = [&](const char *Label, float X, const char *Path, bool Enabled)
    {
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width + X * Scale, Origin.y + 9 * Scale));
        ImGui::BeginDisabled(!Enabled);
        const bool Pressed = Controls.Button(Label, 28 * Scale, false, Path);
        Controls.Tooltip(Label, Label);
        ImGui::EndDisabled();
        return Pressed;
    };
    if (HeaderButton("Undo", -308, "M3 10L7 6L11 10M7 6V15A5 5 0 0 0 17 15", History.QueryUndoAvailable())) RestoreHistory(false);
    if (HeaderButton("Redo", -272, "M21 10L17 6L13 10M17 6V15A5 5 0 0 1 7 15", History.QueryRedoAvailable())) RestoreHistory(true);
    if (HeaderButton("Save document", -236, "M12 3V16M7 11L12 16L17 11M4 17V21H20V17", bool(SaveSource))) Save();
    if (HeaderButton("Document actions", -200, "M4 12H5M11 12H12M18 12H19", true)) ImGui::OpenPopup("Stamp document actions");
    if (DropdownControlSurface::BeginMenu("Stamp document actions", Scale))
    {
        if (DropdownControlSurface::Option("New stamp", false, Scale))
        {
            if (QueryModified()) Diagnostic = "Save or close the current document before creating a new stamp.";
            else { VectorAuthoringDocument Blank; Blank.Stamp = true; Blank.Name = "Untitled stamp"; ConfigureDocument(Blank); }
        }
        if (DropdownControlSurface::Option("Open native stamp", false, Scale, bool(LoadSource)))
        {
            if (QueryModified()) Diagnostic = "Save or close the current document before opening another.";
            else try { if (const auto Source = LoadSource()) ConfigureDocument(DecodeVectorAuthoring(*Source)); }
                catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        const auto DocumentOption = [&](const char *CaptionText, bool Enabled)
        {
            const bool Activated = DropdownControlSurface::Option(CaptionText, false, Scale, Enabled);
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            ControlExtents[std::string("Document ") + CaptionText] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
            return Activated;
        };
        if (DocumentOption("Save document", bool(SaveSource))) Save();
        // 🖨️ Rasterises this document into the project Stamps folder so Texture Paint's stamp tool
        // can place it (its right-click grid rescans the folder every time it opens).
        if (DocumentOption("Save to stamp library", true)) SaveToLibrary();
        const auto EditOption = [&](const char *CaptionText, bool Enabled)
        {
            const bool Activated = DropdownControlSurface::Option(CaptionText, false, Scale, Enabled);
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            ControlExtents[std::string("Document ") + CaptionText] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
            return Activated;
        };
        const bool Idle = Gesture == 0 && !MaskSelection;
        const bool Selected = Idle && !Selection.empty();
        bool Editable = Selected;
        for (const auto &Layer : History.QueryDocument().Layers)
            if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end()
                && (QueryVectorAuthoringProtection(History.QueryDocument(), Layer) || !QueryVectorAuthoringVisibility(History.QueryDocument(), Layer))) Editable = false;
        if (EditOption("Copy selection", Selected)) TransferSelection(false);
        if (EditOption("Cut selection", Editable)) TransferSelection(true);
        if (EditOption("Paste selection", Idle && bool(SelectionClipboard))) InsertSelection(false);
        if (EditOption("Paste in place", Idle && bool(SelectionClipboard))) InsertSelection(true);
        if (EditOption("Duplicate selection", Editable)) ReplicateSelection();
        if (EditOption("Select all editable", Idle)) SelectEditableVectors();
        const char *OrderCaptions[]{"Send to back", "Send backward", "Bring forward", "Bring to front"};
        for (uint32_t Index = 0; Index < 4; ++Index)
            if (EditOption(OrderCaptions[Index], Editable))
                EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringSelection(Document, Selection, VectorSelectionOrder(Index)); });
        ImGui::TextDisabled("Clipboard: %zu components (editor session)", SelectionClipboard ? SelectionClipboard->Layers.size() : size_t(0));
        ImGui::TextDisabled("Native authoring uses TOML, not HTML JSON.");
        DropdownControlSurface::EndMenu();
    }
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 168 * Scale, Origin.y + 9 * Scale));
    if (Controls.Button("Apply to material", 116 * Scale)) ApplyToMaterialRequested = true;
    Controls.Tooltip(
        "Apply to material",
        "Bake the decal (shape/text/vector + channel compositing) onto the selected material preview. "
        "Stamp edits never touch materials until applied.");
    if (HeaderButton("Close stamp studio", -44, "M6 6L18 18M6 18L18 6", bool(ActivateMaterial)))
        if (RequestClose()) ActivateMaterial();
    Controls.ControlHeight = 30;
    ImGui::End();
}

void StampWorkspacePanel::ConstructAddComponentMenu()
{
    const auto &Snapshot = QueryDocument();
    const bool AtCap = Snapshot.Layers.size() >= 24;
    ImGui::TextDisabled("Shapes");
    for (uint32_t Category = 0; Category < 6; ++Category)
    {
        ImGui::BeginDisabled(AtCap);
        if (DropdownControlSurface::Option(ShapeCaptions[Category], false, Scale)) { ConstructPrimitive(Category, ShapeCaptions[Category]); ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled();
        const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
        ControlExtents[ShapeCaptions[Category]] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    }
    ImGui::Separator();
    ImGui::TextDisabled("Decal components");
    const auto DecalOption = [&](const char *CaptionText, const char *PopupKey)
    {
        ImGui::BeginDisabled(AtCap);
        const bool Activated = DropdownControlSurface::Option(CaptionText, false, Scale);
        ImGui::EndDisabled();
        if (Activated)
        {
            ComponentEditToken = 0;
            ComponentTextBuffer[0] = 0;
            ComponentSvgBuffer.clear();
            ComponentImageBuffer.clear();
            ImGui::CloseCurrentPopup();
            // 📝 Do not OpenPopup(PopupKey) here: this lambda runs inside the "Add a stamp component"
            // dropdown's own popup window, a different ID-stack scope than where the modal is polled.
            // Defer to the stable scope in ConstructLayers() instead (see PendingComponentPopup).
            PendingComponentPopup = PopupKey;
        }
    };
    DecalOption("Add vector (SVG)...", "Add stamp vector");
    DecalOption("Add image...", "Add stamp image");
    ImGui::TextDisabled("Use the Text tool in the canvas toolbar to draw and write a text component.");
    if (AtCap) ImGui::TextWrapped("Stamp decals are capped at 24 components (Pattern allows 64).");
}

// 📝 Finds the largest point size whose word-wrapped glyph-outline layout still fits inside
// {BoxWidth, BoxHeight} (into a throwaway scratch document, never touching the real one) — the
// "fit to box" behaviour an SVG/Illustrator-style text tool gives when you drag out a frame and
// type into it. Text wraps onto as many lines as needed within BoxWidth, so a longer caption grows
// downward with more lines instead of shrinking indefinitely onto one ever-thinner line; the point
// size only shrinks once the wrapped block's own height would overflow the box. Binary-searches the
// real glyph bounds directly (reading the baked instruction's own ink extents, never the selection
// query's visible-canvas-clamped bounds, since this scratch text is never actually placed on a
// canvas) because wrapping makes line count — and so required height — a step function of point
// size, not the simple linear ratio a single probe size could extrapolate from.
float MeasureStampTextFitSize(const std::string &Content, float BoxWidth, float BoxHeight, const std::filesystem::path &ContentRoot)
{
    // The original DM Sans fit, now the default-face case of the shared face-aware measurement
    // (TypefaceCatalogue) both text tools compose with.
    return MeasureTextFitSize(Content, BoxWidth, BoxHeight, ResolveTypefacePath(ContentRoot, DefaultTypefaceId));
}

void StampWorkspacePanel::RebakeTextLayer(uint32_t Token, const std::string &Content, float PointSize, float WrapWidth, const std::string &TypefaceId)
{
    if (Content.empty()) { Diagnostic = "Text content cannot be empty."; return; }
    try
    {
        EvaluateEdit([&](auto &Document)
        {
            const auto Existing = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Layer) { return Layer.Token == Token; });
            if (Existing == Document.Layers.end()) throw std::runtime_error("The edited text component no longer exists.");
            const auto OldBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{Token});
            const auto PreviousInstruction = Document.Instructions[Existing->Root];
            const std::array<float, 4> Colour = PreviousInstruction.Colour;
            // 🅰 Bake in the face the caller picked, or the one the layer already carries
            // (empty = keep); resolved against the host's content root via the shared catalogue.
            const std::string Face = TypefaceId.empty() ? Existing->TextTypeface : TypefaceId;
            const uint32_t NewRoot = AppendStampTextOutline(Document, Content, PointSize, Colour,
                ResolveTypefacePath(ContentRoot, Face), WrapWidth);
            auto &NewInstruction = Document.Instructions[NewRoot];
            NewInstruction.Transform[3] = PreviousInstruction.Transform[3];
            NewInstruction.ColourEnd = PreviousInstruction.ColourEnd;
            NewInstruction.StrokeColour = PreviousInstruction.StrokeColour;
            NewInstruction.Topology[3] = (NewInstruction.Topology[3] & ~VectorCoverageStrokeColourExplicit) |
                (PreviousInstruction.Topology[3] & VectorCoverageStrokeColourExplicit);
            NewInstruction.Paint[0] = PreviousInstruction.Paint[0];
            NewInstruction.Paint[3] = PreviousInstruction.Paint[3];
            NewInstruction.GradientStopPositions = PreviousInstruction.GradientStopPositions;
            NewInstruction.GradientStopColour2 = PreviousInstruction.GradientStopColour2;
            NewInstruction.GradientStopColour3 = PreviousInstruction.GradientStopColour3;
            NewInstruction.GradientAxis = PreviousInstruction.GradientAxis;
            NewInstruction.ShapeParameters = PreviousInstruction.ShapeParameters;
            NewInstruction.StrokeColourEnd = PreviousInstruction.StrokeColourEnd;
            NewInstruction.StrokeGradientStopPositions = PreviousInstruction.StrokeGradientStopPositions;
            NewInstruction.StrokeGradientStopColour2 = PreviousInstruction.StrokeGradientStopColour2;
            NewInstruction.StrokeGradientStopColour3 = PreviousInstruction.StrokeGradientStopColour3;
            NewInstruction.StrokeGradientAxis = PreviousInstruction.StrokeGradientAxis;
            NewInstruction.FreeformPointPositions01 = PreviousInstruction.FreeformPointPositions01;
            NewInstruction.FreeformPointPositions23 = PreviousInstruction.FreeformPointPositions23;
            NewInstruction.FreeformPointPositions45 = PreviousInstruction.FreeformPointPositions45;
            NewInstruction.FreeformPointPositions67 = PreviousInstruction.FreeformPointPositions67;
            NewInstruction.FreeformPointColour4 = PreviousInstruction.FreeformPointColour4;
            NewInstruction.FreeformPointColour5 = PreviousInstruction.FreeformPointColour5;
            NewInstruction.FreeformPointColour6 = PreviousInstruction.FreeformPointColour6;
            NewInstruction.FreeformPointColour7 = PreviousInstruction.FreeformPointColour7;
            NewInstruction.FreeformParameters = PreviousInstruction.FreeformParameters;
            for (auto &Layer : Document.Layers) if (Layer.Token == Token) { Layer.Root = NewRoot; Layer.SourceMarkup = Content; Layer.TextPointSize = PointSize; Layer.TextTypeface = Face; }
            ReclaimVectorAuthoringSources(Document);
            const auto NewBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{Token});
            VectorAuthoringSourcePatch Patch;
            Patch.TranslationX = OldBounds[0] - NewBounds[0];
            Patch.TranslationY = OldBounds[1] - NewBounds[1];
            TransformVectorAuthoringSelection(Document, std::array<uint32_t, 1>{Token}, Patch);
        });
        Diagnostic.clear();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::CommitTextComposer()
{
    std::string Content = ComponentTextBuffer.data();
    const auto NotSpace = [](unsigned char Character) { return !std::isspace(Character); };
    Content.erase(Content.begin(), std::find_if(Content.begin(), Content.end(), NotSpace));
    Content.erase(std::find_if(Content.rbegin(), Content.rend(), NotSpace).base(), Content.end());
    const uint32_t EditToken = ComponentEditToken;
    const auto Bounds = TextEditBounds;
    Gesture = 0; ComponentTextBuffer[0] = 0; ComponentEditToken = 0;
    if (Content.empty()) { Diagnostic.clear(); return; } // Nothing was written; discard the box silently.
    try
    {
        const std::string TypefacePath = ResolveTypefacePath(ContentRoot, TextTypefaceId);
        const float PointSize = MeasureTextFitSize(Content, Bounds[2], Bounds[3], TypefacePath);
        if (EditToken) { RebakeTextLayer(EditToken, Content, PointSize, Bounds[2], TextTypefaceId); return; }
        const auto DefaultTextColour = OppositeCanvasColour();
        EvaluateEdit([&](auto &Document)
        {
            if (Document.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
            const uint32_t NewRoot = AppendStampTextOutline(Document, Content, PointSize, DefaultTextColour, TypefacePath, Bounds[2]);
            VectorAuthoringLayer Layer;
            Layer.Token = ++Document.TokenClock;
            Layer.Root = NewRoot;
            Layer.Name = Content.size() > 24 ? Content.substr(0, 24) + "..." : Content;
            Layer.ComponentKind = VectorAuthoringComponentKind::Text;
            Layer.SourceMarkup = Content;
            Layer.TextPointSize = PointSize;
            Layer.TextTypeface = TextTypefaceId;
            if (Document.Layers.empty()) Document.Name = "Custom stamp";
            Document.Layers.push_back(Layer);
            ReclaimVectorAuthoringSources(Document);
            const auto NewBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{Layer.Token});
            VectorAuthoringSourcePatch Patch;
            Patch.TranslationX = Bounds[0] - NewBounds[0];
            Patch.TranslationY = Bounds[1] - NewBounds[1];
            TransformVectorAuthoringSelection(Document, std::array<uint32_t, 1>{Layer.Token}, Patch);
            Selection = {Layer.Token};
        });
        Diagnostic.clear();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::ConstructTextComposer(float BoardMinimumX, float BoardMinimumY, float BoardSize)
{
    const float CanvasUnits = QueryCanvasExtent();
    const ImVec2 Minimum(BoardMinimumX + (TextEditBounds[0] - TextEditBounds[2] * .5f) / CanvasUnits * BoardSize,
                          BoardMinimumY + (TextEditBounds[1] - TextEditBounds[3] * .5f) / CanvasUnits * BoardSize);
    const ImVec2 Maximum(BoardMinimumX + (TextEditBounds[0] + TextEditBounds[2] * .5f) / CanvasUnits * BoardSize,
                          BoardMinimumY + (TextEditBounds[1] + TextEditBounds[3] * .5f) / CanvasUnits * BoardSize);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Minimum, Maximum, IM_COL32(255, 255, 255, 240));
    Ink->AddRect(Minimum, Maximum, IM_COL32(134, 164, 112, 255), 0, 0, std::max(1.f, 1.5f * Scale));
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::PushID("StampTextComposer");
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.06f, .06f, .05f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    // 📝 Keep the live text already at the size the commit will actually bake at — recomputed (via
    // the same MeasureStampTextFitSize the commit itself calls) only when the composed text changes,
    // never every frame. Without this the field typed at a small, generic caret size and only jumped
    // to the real auto-fit size (often much larger) the instant the box committed.
    const std::string CurrentContent = ComponentTextBuffer.data();
    char BoxKeySuffix[64]{};
    std::snprintf(BoxKeySuffix, sizeof(BoxKeySuffix), "\x01%.3f\x01%.3f", double(TextEditBounds[2]), double(TextEditBounds[3]));
    const std::string CacheKey = TextTypefaceId + "\x02" + CurrentContent + BoxKeySuffix;
    if (CacheKey != TextComposerFitCacheKey)
    {
        TextComposerFitCacheKey = CacheKey;
        try { TextComposerFitPointSize = CurrentContent.empty() ? 0.f : MeasureTextFitSize(CurrentContent, TextEditBounds[2], TextEditBounds[3], ResolveTypefacePath(ContentRoot, TextTypefaceId)); }
        catch (const std::exception &) { TextComposerFitPointSize = 0.f; }
    }
    const float LivePixelSize = TextComposerFitPointSize > 0
        ? std::clamp(TextComposerFitPointSize * (BoardSize / CanvasUnits), 8.f, 400.f) : 16.f * Scale;
    ImGui::PushFont(static_cast<ImFont *>(RegularTypeface ? RegularTypeface : LightTypeface), LivePixelSize);
    if (TextEditFocusPending) { ImGui::SetKeyboardFocusHere(); TextEditFocusPending = false; }
    // 📝 ImGui's multiline default is backwards for this use case (plain Enter inserts a newline;
    // Ctrl+Enter validates) — CtrlEnterForNewLine flips it so plain Enter commits the component like
    // the spec calls for, and Ctrl+Enter is the escape hatch for an actual multi-line caption.
    const bool EnterCommit = ImGui::InputTextMultiline("##Compose", ComponentTextBuffer.data(), ComponentTextBuffer.size(),
        ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    const bool Deactivated = ImGui::IsItemDeactivated();
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { Gesture = 0; ComponentTextBuffer[0] = 0; ComponentEditToken = 0; Diagnostic.clear(); }
    else if (EnterCommit || Deactivated) CommitTextComposer();
}

namespace
{
// 🎞️ A font-preview square's ink fit into a rect at its TRUE aspect (measured ink box, not a
// stretched crop), block-drawn at ~1.5px blocks with 3x3-averaged samples, run-merged per row.
void DrawTypefaceStrip(ImDrawList *Draw, const std::vector<uint8_t> &Pixels, int Extent,
    const ImVec2 &Minimum, const ImVec2 &Maximum)
{
    if (Pixels.size() != size_t(Extent) * size_t(Extent) * 4 || Extent <= 0) return;
    if (!ImGui::IsRectVisible(Minimum, Maximum)) return; // scrolled away
    const float Width = Maximum.x - Minimum.x, Height = Maximum.y - Minimum.y;
    if (Width < 4 || Height < 4) return;
    int U0 = Extent, V0 = Extent, U1 = -1, V1 = -1;
    for (int Y = 0; Y < Extent; Y += 2)
        for (int X = 0; X < Extent; X += 2)
            if (Pixels[(size_t(Y) * Extent + X) * 4 + 3] >= 24)
            {
                U0 = std::min(U0, X); U1 = std::max(U1, X);
                V0 = std::min(V0, Y); V1 = std::max(V1, Y);
            }
    if (U1 < 0) return;
    const float Pad = Extent * .02f;
    const float InkU0 = (U0 - Pad) / Extent, InkV0 = (V0 - Pad) / Extent;
    const float InkW = (U1 - U0 + 2 * Pad) / Extent, InkH = (V1 - V0 + 2 * Pad) / Extent;
    const float Fit = std::min(Width / InkW, Height / InkH);
    const float DrawW = InkW * Fit, DrawH = InkH * Fit;
    const float DrawX = Minimum.x + (Width - DrawW) * .5f, DrawY = Minimum.y + (Height - DrawH) * .5f;
    const int BlocksX = std::clamp(int(DrawW / 1.5f), 4, 384);
    const int BlocksY = std::clamp(int(DrawH / 1.5f), 4, 64);
    const float BlockW = DrawW / BlocksX, BlockH = DrawH / BlocksY;
    for (int By = 0; By < BlocksY; ++By)
    {
        int RunStart = -1;
        uint32_t RunKey = 0;
        const auto Flush = [&](int EndX) {
            if (RunStart < 0) return;
            Draw->AddRectFilled(ImVec2(DrawX + RunStart * BlockW, DrawY + By * BlockH),
                ImVec2(DrawX + EndX * BlockW + .5f, DrawY + (By + 1) * BlockH + .5f),
                PanelChrome::Colour32(0xdbdbd0, RunKey / 255.f));
            RunStart = -1;
        };
        for (int Bx = 0; Bx < BlocksX; ++Bx)
        {
            float Alpha = 0;
            for (int Sy = 0; Sy < 3; ++Sy)
                for (int Sx = 0; Sx < 3; ++Sx)
                {
                    const int X = std::clamp(int((InkU0 + (Bx + .2f + .3f * Sx) / BlocksX * InkW) * Extent), 0, Extent - 1);
                    const int Y = std::clamp(int((InkV0 + (By + .2f + .3f * Sy) / BlocksY * InkH) * Extent), 0, Extent - 1);
                    Alpha += Pixels[(size_t(Y) * Extent + X) * 4 + 3] / 255.f / 9.f;
                }
            const uint32_t Key = uint32_t(std::clamp(Alpha, 0.f, 1.f) * 255) & ~3u;
            if (Key < 8) { Flush(Bx); continue; }
            if (RunStart < 0) { RunStart = Bx; RunKey = Key; }
            else if (Key != RunKey) { Flush(Bx); RunStart = Bx; RunKey = Key; }
        }
        Flush(BlocksX);
    }
}
} // namespace

bool StampWorkspacePanel::ConstructFontList(const std::string &CurrentId, std::string &PickedId, float Width)
{
    // 🅰 Every outline-capable face in EngineContent/FontArchives (the shared TypefaceCatalogue),
    // grouped by family, each row previewed in its own glyphs. Previews build a few per frame so
    // opening the list never hitches; a row shows its name until its preview lands.
    const auto &Faces = QueryTypefaceChoices(ContentRoot);
    int BuildBudget = 3;
    for (const auto &Face : Faces)
    {
        if (BuildBudget <= 0) break;
        if (!FontPreviewCache.count(Face.Id))
        {
            FontPreviewCache[Face.Id] = RenderTypefacePreview(Face.Path.string(), Face.DisplayName, 256);
            --BuildBudget;
        }
    }
    bool Chosen = false;
    ImGui::BeginChild("StampTypefaceList", ImVec2(Width, 252 * Scale), ImGuiChildFlags_None);
    ImDrawList *Draw = ImGui::GetWindowDrawList();
    const float RowHeight = 46 * Scale;
    std::string LastFamily;
    for (const auto &Face : Faces)
    {
        if (Face.Family != LastFamily)
        {
            LastFamily = Face.Family;
            ImGui::Dummy(ImVec2(1, 6 * Scale));
            const ImVec2 At = ImGui::GetCursorScreenPos();
            Caption(ImVec2(At.x + 4 * Scale, At.y), Face.Family.c_str(), 8 * Scale, 0x7e7e70);
            ImGui::Dummy(ImVec2(1, 14 * Scale));
        }
        const ImVec2 RowMin = ImGui::GetCursorScreenPos();
        const ImVec2 RowMax(RowMin.x + Width - 4 * Scale, RowMin.y + RowHeight);
        ImGui::PushID(Face.Id.c_str());
        const bool Clicked = ImGui::InvisibleButton("Typeface row", ImVec2(RowMax.x - RowMin.x, RowHeight));
        ImGui::PopID();
        const bool Hovered = ImGui::IsItemHovered();
        const bool Current = Face.Id == CurrentId;
        if (Clicked) { PickedId = Face.Id; Chosen = true; }
        Draw->AddRectFilled(RowMin, RowMax, Ink32(Current ? 0x232720 : 0x191919), 8 * Scale);
        const auto Preview = FontPreviewCache.find(Face.Id);
        if (Preview != FontPreviewCache.end() && !Preview->second.empty())
            DrawTypefaceStrip(Draw, Preview->second, 256,
                ImVec2(RowMin.x + 12 * Scale, RowMin.y + 7 * Scale), ImVec2(RowMax.x - 12 * Scale, RowMax.y - 7 * Scale));
        else Caption(ImVec2(RowMin.x + 12 * Scale, RowMin.y + 16 * Scale), Face.DisplayName.c_str(), 10 * Scale, 0xc8c8b6);
        if (Current) PanelChrome::CheckDisc(Draw, RowMin, Scale);
        if (Hovered)
        {
            Draw->AddRect(RowMin, RowMax, Ink32(0x707173), 8 * Scale);
            ImGui::SetTooltip("%s", Face.DisplayName.c_str());
        }
        ImGui::SetCursorScreenPos(ImVec2(RowMin.x, RowMax.y + 4 * Scale));
    }
    if (Faces.empty())
        Caption(ImGui::GetCursorScreenPos(), "No outline fonts in EngineContent/FontArchives.", 9 * Scale, 0x777770);
    ImGui::Dummy(ImVec2(1, 1)); // SetCursorScreenPos grew the parent above; an item must claim it
    ImGui::EndChild();
    return Chosen;
}

void StampWorkspacePanel::ConstructVectorPopup()
{
    ImGui::SetNextWindowSize(ImVec2(520 * Scale, 0));
    const StampModalScope ModalScope(Scale);
    if (ImGui::BeginPopupModal("Add stamp vector", nullptr, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextWrapped(ComponentEditToken ? "Replace this component's SVG markup." : "Paste sanitized SVG markup. Every solid-filled shape imports with its own colour and transform; gradients fall back to a neutral grey; scripts and external references are never evaluated.");
        ImGui::Spacing();
        ComponentSvgBuffer.resize(std::max<size_t>(ComponentSvgBuffer.size(), VectorAuthoringSourceMarkupLimit));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##StampSvgContent", ComponentSvgBuffer.data(), ComponentSvgBuffer.size(), ImVec2(0, 160 * Scale));
        if (!Diagnostic.empty()) ImGui::TextWrapped("%s", Diagnostic.c_str());
        ImGui::Spacing();
        if (Controls.Button("Cancel", 120 * Scale)) { ImGui::CloseCurrentPopup(); Diagnostic.clear(); }
        ImGui::SameLine(0, 8 * Scale);
        if (Controls.Button(ComponentEditToken ? "Apply" : "Import", 120 * Scale, true))
        {
            const std::string Markup = ComponentSvgBuffer.c_str();
            const uint32_t EditToken = ComponentEditToken;
            try
            {
                EvaluateEdit([&](auto &Document)
                {
                    const float HalfCanvas = std::clamp(Document.CanvasExtent, 64.f, 4096.f) * .5f;
                    std::array<float, 2> AnchorCentre{HalfCanvas, HalfCanvas};
                    if (EditToken)
                    {
                        const auto Existing = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Layer) { return Layer.Token == EditToken; });
                        if (Existing == Document.Layers.end()) throw std::runtime_error("The edited vector component no longer exists.");
                        const auto OldBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{EditToken});
                        AnchorCentre = {OldBounds[0], OldBounds[1]};
                    }
                    const uint32_t NewRoot = AppendStampVectorImport(Document, Markup);
                    if (EditToken)
                    {
                        for (auto &Layer : Document.Layers)
                            if (Layer.Token == EditToken) { Layer.Root = NewRoot; Layer.SourceMarkup = Markup; }
                    }
                    else
                    {
                        if (Document.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
                        VectorAuthoringLayer Layer;
                        Layer.Token = ++Document.TokenClock;
                        Layer.Root = NewRoot;
                        Layer.Name = "Imported vector";
                        Layer.ComponentKind = VectorAuthoringComponentKind::ImportedVector;
                        Layer.SourceMarkup = Markup;
                        if (Document.Layers.empty()) Document.Name = "Custom stamp";
                        Document.Layers.push_back(Layer);
                    }
                    ReclaimVectorAuthoringSources(Document);
                    const uint32_t FinalToken = EditToken ? EditToken : Document.Layers.back().Token;
                    const auto NewBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{FinalToken});
                    VectorAuthoringSourcePatch Patch;
                    Patch.TranslationX = AnchorCentre[0] - NewBounds[0];
                    Patch.TranslationY = AnchorCentre[1] - NewBounds[1];
                    TransformVectorAuthoringSelection(Document, std::array<uint32_t, 1>{FinalToken}, Patch);
                    Selection = {FinalToken};
                });
                if (Diagnostic.empty()) { ImGui::CloseCurrentPopup(); ComponentEditToken = 0; }
            }
            catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        ImGui::EndPopup();
    }
}

void StampWorkspacePanel::ConstructImagePopup()
{
    ImGui::SetNextWindowSize(ImVec2(520 * Scale, 0));
    const StampModalScope ModalScope(Scale);
    if (ImGui::BeginPopupModal("Add stamp image", nullptr, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextWrapped(
            "Paste a PNG data URL (data:image/png;base64,...), up to 256\u00d7256 px. The native compute "
            "shader does not sample image pixels yet, so this renders as a placeholder box sized to the "
            "image's own aspect ratio until pixel sampling ships; the embedded bytes still round-trip "
            "through save/load and the HTML Stamp Studio.");
        ImGui::Spacing();
        ComponentImageBuffer.resize(std::max<size_t>(ComponentImageBuffer.size(), 300064));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##StampImageContent", ComponentImageBuffer.data(), ComponentImageBuffer.size(), ImVec2(0, 100 * Scale));
        if (!Diagnostic.empty()) ImGui::TextWrapped("%s", Diagnostic.c_str());
        ImGui::Spacing();
        if (Controls.Button("Cancel", 120 * Scale)) { ImGui::CloseCurrentPopup(); Diagnostic.clear(); }
        ImGui::SameLine(0, 8 * Scale);
        if (Controls.Button(ComponentEditToken ? "Apply" : "Import", 120 * Scale, true))
        {
            const std::string DataUrl = ComponentImageBuffer.c_str();
            const uint32_t EditToken = ComponentEditToken;
            try
            {
                const auto Extent = SniffEmbeddedImageExtent(DataUrl);
                if (!Extent) throw std::runtime_error("That is not a recognizable PNG data URL.");
                const float Aspect = float((*Extent)[0]) / std::max(1.f, float((*Extent)[1]));
                EvaluateEdit([&](auto &Document)
                {
                    if (EditToken)
                    {
                        const auto Existing = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Layer) { return Layer.Token == EditToken; });
                        if (Existing == Document.Layers.end()) throw std::runtime_error("The edited image component no longer exists.");
                        auto &Placeholder = Document.Instructions[Existing->Root];
                        const float Height = Placeholder.Bounds[3];
                        Placeholder.Bounds[2] = std::clamp(Height * Aspect, 4.f, 2048.f);
                        Existing->EmbeddedImage = DataUrl;
                    }
                    else
                    {
                        if (Document.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
                        const float CanvasUnits = std::clamp(Document.CanvasExtent, 64.f, 4096.f);
                        const auto Ink = OppositeCanvasColour();
                        VectorCoverageInstruction Source;
                        Source.Topology[0] = 0;
                        const float Height = std::clamp(CanvasUnits * .3125f, 16.f, 1024.f);
                        Source.Bounds = {CanvasUnits * .5f, CanvasUnits * .5f, std::clamp(Height * Aspect, 4.f, 2048.f), Height};
                        Source.Colour = Ink;
                        Source.StrokeColour = Ink;
                        Source.Topology[3] |= VectorCoverageStrokeColourExplicit;
                        const auto Token = ConstructVectorAuthoringPrimitive(Document, Source, "Imported image");
                        for (auto &Layer : Document.Layers) if (Layer.Token == Token)
                        {
                            Layer.ComponentKind = VectorAuthoringComponentKind::ImportedImage;
                            Layer.EmbeddedImage = DataUrl;
                        }
                        if (Document.Layers.size() == 1) Document.Name = "Custom stamp";
                        Selection = {Token};
                    }
                });
                if (Diagnostic.empty()) { ImGui::CloseCurrentPopup(); ComponentEditToken = 0; }
            }
            catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        ImGui::EndPopup();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     STAMP LIBRARY
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructLibraryCard(const StampLibraryEntry &Entry, float Width, bool Selected, size_t Slot, bool Reopenable)
{
    // Stamp card, identical to Texture Paint's stamp picker: fixed 125px box, centred 72px square
    // thumb over a two-tone checker, 9px name + 7px provenance, green check disc when selected.
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const ImVec2 Size(Width, 125 * Scale);
    const ImVec2 Far(Origin.x + Size.x, Origin.y + Size.y);
    ImGui::InvisibleButton(("##StampCard-" + Entry.Id).c_str(), Size);
    const bool Clicked = ImGui::IsItemClicked(), Hovered = ImGui::IsItemHovered();
    ControlExtents[Entry.Name] = {Origin.x, Origin.y, Far.x, Far.y};
    // Fully offscreen cards register their extent but skip all drawing (mirrors the Pattern card).
    const float ClipTop = ImGui::GetWindowPos().y, ClipBottom = ClipTop + ImGui::GetWindowSize().y;
    if (Far.y < ClipTop || Origin.y > ClipBottom)
    {
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Far.y));
        return;
    }
    Controls.Tooltip(Entry.Name.c_str(), Reopenable ? "Open as an editable vector stamp."
        : (Entry.Builtin ? "Preset art for Texture Paint's stamp tool." : "Raster stamp for Texture Paint's stamp tool."));
    ImDrawList *Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Origin, Far, Ink32(0x191919), 15 * Scale);
    const float Thumb = 72 * Scale;
    const ImVec2 ThumbMin(Origin.x + (Size.x - Thumb) * .5f, Origin.y + 6 * Scale);
    const ImVec2 ThumbMax(ThumbMin.x + Thumb, ThumbMin.y + Thumb);
    StampAtlasImage Atlas{};
    if (QueryLibraryThumbnailAtlas) Atlas = QueryLibraryThumbnailAtlas();
    if (Atlas.Texture != 0 && Slot < LibraryAtlasCapacity)
    {
        // Atlas fast path: one textured quad (checker already baked under the art); the host returns
        // the slot-0 rectangle and each card offsets it by its stable library slot.
        const float Step = float(LibraryAtlasTile + 2) / LibraryAtlasExtent;
        const float Span = float(LibraryAtlasTile) / LibraryAtlasExtent;
        const float U0 = Atlas.MinimumU + (Slot % LibraryAtlasColumns) * Step;
        const float V0 = Atlas.MinimumV + (Slot / LibraryAtlasColumns) * Step;
        Draw->AddImageRounded(ImTextureRef(Atlas.Texture), ThumbMin, ThumbMax, ImVec2(U0, V0),
            ImVec2(U0 + Span, V0 + Span), IM_COL32_WHITE, 8 * Scale);
    }
    else
    {
        // CPU fallback while the host atlas is unuploaded: checker plus directly sampled blocks.
        Draw->AddRectFilled(ThumbMin, ThumbMax, Ink32(0x1e1e1e), 8 * Scale);
        Draw->AddRectFilled(ThumbMin, ImVec2(ThumbMin.x + Thumb * .5f, ThumbMin.y + Thumb * .5f), Ink32(0x2a2a2a), 8 * Scale);
        Draw->AddRectFilled(ImVec2(ThumbMin.x + Thumb * .5f, ThumbMin.y + Thumb * .5f), ThumbMax, Ink32(0x2a2a2a), 8 * Scale);
        constexpr int Blocks = 72;
        const float Block = Thumb / Blocks;
        for (int By = 0; By < Blocks; ++By)
            for (int Bx = 0; Bx < Blocks; ++Bx)
            {
                const auto Texel = StampLibrary::Sample(Entry, (Bx + .5f) / Blocks, (By + .5f) / Blocks);
                if (Texel[3] < .03f) continue;
                const uint32_t Packed = (uint32_t(std::clamp(Texel[0], 0.f, 1.f) * 255) << 16) |
                    (uint32_t(std::clamp(Texel[1], 0.f, 1.f) * 255) << 8) | uint32_t(std::clamp(Texel[2], 0.f, 1.f) * 255);
                Draw->AddRectFilled(ImVec2(ThumbMin.x + Bx * Block, ThumbMin.y + By * Block),
                    ImVec2(ThumbMin.x + (Bx + 1) * Block + .5f, ThumbMin.y + (By + 1) * Block + .5f),
                    PanelChrome::Colour32(Packed, Texel[3]));
            }
    }
    Caption(ImVec2(Origin.x + 12 * Scale, Origin.y + 89 * Scale), Entry.Name.c_str(), 9 * Scale, 0xc8c8b6);
    Caption(ImVec2(Origin.x + 12 * Scale, Origin.y + 105 * Scale),
        Entry.Builtin ? (Entry.Tintable ? "Preset \xc2\xb7 tintable" : "Preset")
                      : (Reopenable ? "Saved stamp \xc2\xb7 vector" : "Saved stamp"),
        7 * Scale, 0x767665);
    if (Selected) PanelChrome::CheckDisc(Draw, Origin, Scale);
    if (Hovered) Draw->AddRect(Origin, Far, Ink32(0x707173), 15 * Scale);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Far.y));
    if (Clicked) OpenLibraryStamp(Entry, Reopenable);
}

void StampWorkspacePanel::ConstructLibrary()
{
    // Stamp library gallery in the Material workspace's catalogue design (the same page chrome as
    // the Pattern library): two shortcut pills, 18px heading + count, padded search, wrapping
    // collection pills, hairline + overline, then every match as a two-column 125px card inside one
    // scrolling grid (no paging), closing footer tag. The entries are the shared StampLibrary -
    // presets plus everything in the library directory - exactly what Texture Paint's stamp tool
    // places; clicking a saved stamp reopens its vector design here for editing.
    CapturePanel("Library");
    if (ImGui::GetContentRegionAvail().y < 35 * Scale) return;
    EnsureStampLibrary();
    RefreshLibraryAtlas();
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const float Bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    const auto At = [&](float X, float Y) { return ImVec2(Origin.x + X * Scale, Origin.y + Y * Scale); };
    const auto SmallFont = [&](float Pixels)
    {
        if (RegularTypeface) ImGui::PushFont(static_cast<ImFont *>(RegularTypeface), Pixels * 1.302f * Scale);
    };
    const auto PopSmallFont = [&]() { if (RegularTypeface) ImGui::PopFont(); };
    // Shared capsule pill (PanelChrome::Pill) at this library's 8.5px label size, clipped to the capsule.
    const auto Pill = [&](const char *Id, const char *Label, ImVec2 Position, float PillWidth, float PillHeight, bool Selected) {
        return PanelChrome::Pill(Id, Label, Position, PillWidth, PillHeight, Selected, Scale, nullptr, 0, 8.5f, true);
    };

    // Top shortcut row, mirroring Material's Workspace / My collection pills.
    const float HalfPill = (Width - 34 * Scale) * .5f;
    if (Pill("##ShortcutPresets", "Preset stamps", At(14, 10), HalfPill, 27 * Scale, LibraryFilter == 1))
    { LibraryFilter = 1; LibrarySearchText[0] = 0; }
    if (Pill("##ShortcutMine", "My stamps", ImVec2(Origin.x + Width * .5f + 3 * Scale, At(0, 10).y), HalfPill, 27 * Scale, LibraryFilter == 2))
    { LibraryFilter = 2; LibrarySearchText[0] = 0; }

    Caption(At(20, 50), "Stamp library", 18 * Scale, 0xd3d3c8, LightTypeface);
    Caption(ImVec2(Origin.x + 28 * Scale + CaptionWidth("Stamp library", 18 * Scale, LightTypeface), At(0, 59).y),
        std::to_string(Library.Entries().size()).c_str(), 8 * Scale, 0x777766);

    ImGui::SetCursorScreenPos(At(14, 82));
    ImGui::SetNextItemWidth(Width - 28 * Scale);
    SmallFont(11);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Spectrum(0x191919));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(35 * Scale, 11.5f * Scale));
    ImGui::InputTextWithHint("##StampLibrarySearch", "Search rings, badges, marks\xe2\x80\xa6", LibrarySearchText.data(), LibrarySearchText.size());
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    PopSmallFont();
    DrawGlyph("M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14ZM21 21l-4.3-4.3", At(27, 94), 15 * Scale, 0x77776f);

    // Collection filter as wrapping pills instead of a dropdown, like Material's category row;
    // Reload rides the same row as an unlatched action pill and rescans the library directory.
    float Y = 130;
    float PillX = 14;
    const auto RowPill = [&](const char *Id, const char *Label, bool Selected) {
        const float Edge = CaptionWidth(Label, 8.5f * Scale) / Scale + 18;
        if (PillX + Edge > Width / Scale - 14) { PillX = 14; Y += 29; }
        const bool Pressed = Pill(Id, Label, At(PillX, Y), Edge * Scale, 24 * Scale, Selected);
        PillX += Edge + 5;
        return Pressed;
    };
    if (RowPill("All stamps", "All stamps", LibraryFilter == 0)) LibraryFilter = 0;
    if (RowPill("Presets", "Presets", LibraryFilter == 1)) LibraryFilter = 1;
    if (RowPill("My stamps", "My stamps", LibraryFilter == 2)) LibraryFilter = 2;
    if (RowPill("##LibraryReload", "Reload", false)) EnsureStampLibrary(true);
    Y += 24 + 8;

    const auto Matches = QueryLibraryMatches();
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddLine(At(0, Y), ImVec2(Origin.x + Width, At(0, Y).y), Ink32(0xffffff, .051f));
    Caption(At(17, Y + 12), "THE STAMP COLLECTION", 8 * Scale, 0x7e7e70);
    const std::string MatchCount = std::to_string(Matches.size());
    Caption(ImVec2(Origin.x + Width - 17 * Scale - CaptionWidth(MatchCount.c_str(), 8 * Scale), At(0, Y + 12).y),
        MatchCount.c_str(), 8 * Scale, 0x7e7e70);

    const float FooterY = Bottom - 71 * Scale;
    ImGui::SetCursorScreenPos(At(16, Y + 31));
    ImGui::BeginChild("Stamp grid", ImVec2(Width - 32 * Scale, std::max(1.f, FooterY - At(0, Y + 31).y)), ImGuiChildFlags_None);
    const float Gap = 9 * Scale, CardHeight = 125 * Scale;
    const float CardWidth = std::max(32.f, (ImGui::GetContentRegionAvail().x - Gap) * .5f);
    const auto GridOrigin = ImGui::GetCursorScreenPos();
    for (size_t Ordinal = 0; Ordinal < Matches.size(); ++Ordinal)
    {
        const ImVec2 Position(GridOrigin.x + (Ordinal % 2) * (CardWidth + Gap), GridOrigin.y + (Ordinal / 2) * (CardHeight + Gap));
        ImGui::SetCursorScreenPos(Position);
        const size_t Slot = Matches[Ordinal];
        const bool Reopenable = Slot < LibraryEntryReopenable.size() && LibraryEntryReopenable[Slot] != 0;
        const auto &Entry = Library.Entries()[Slot];
        ConstructLibraryCard(Entry, CardWidth, LibrarySelected == Entry.Id, Slot, Reopenable);
    }
    const size_t Rows = (Matches.size() + 1) / 2;
    ImGui::SetCursorScreenPos(ImVec2(GridOrigin.x, GridOrigin.y + float(Rows) * (CardHeight + Gap) + 2 * Scale));
    SmallFont(10);
    ImGui::PushTextWrapPos(GridOrigin.x + ImGui::GetContentRegionAvail().x);
    if (Matches.empty())
        ImGui::TextColored(ImVec4(147 / 255.f, 158 / 255.f, 153 / 255.f, 1), LibraryFilter == 2
            ? "No saved stamps yet. Design a decal, then Save (Ctrl+S) to publish it here."
            : "No matches. Try another collection or search.");
    ImGui::TextColored(ImVec4(147 / 255.f, 158 / 255.f, 153 / 255.f, 1),
        "Every stamp here is placeable from Texture Paint's stamp tool. Clicking one of your saved stamps reopens its editable vector design.");
    ImGui::PopTextWrapPos();
    PopSmallFont();
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();

    Ink->AddRectFilled(ImVec2(Origin.x, FooterY), ImVec2(Origin.x + Width, Bottom), Ink32(0x151515));
    DrawGlyph("M5 22H19M12 2V10M8 10H16L17 18H7Z", ImVec2(Origin.x + 19 * Scale, FooterY + 20 * Scale), 19 * Scale, 0x9a9a8e);
    Caption(ImVec2(Origin.x + 51 * Scale, FooterY + 20 * Scale), "Decals for Texture Paint.", 9 * Scale, 0xaeae9b);
    const std::string Footer = std::to_string(Library.Entries().size()) + " stamps \xc2\xb7 presets and your saved designs.";
    Caption(ImVec2(Origin.x + 51 * Scale, FooterY + 37 * Scale), Footer.c_str(), 7 * Scale, 0x6b6b60);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LAYER STACK
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructLayers()
{
    CapturePanel("Layers");
    // 📝 Controls.Register is private; restyled bare buttons record the same captions/extents the old
    // capsule buttons did, straight into the panel map the RegisterExtent callback writes.
    const auto NoteExtent = [&](const std::string &CaptionText) {
        const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
        ControlExtents[CaptionText] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    };
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    Caption(ImVec2(Origin.x + 16 * Scale, Origin.y + 15 * Scale), "Components", 23 * Scale, 0xd9d9cc, LightTypeface);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 44 * Scale, Origin.y + 14 * Scale));
    const auto &Snapshot = QueryDocument();
    if (Controls.Button("Add stamp component", 28 * Scale, false, PlusPath)) ImGui::OpenPopup("Add a stamp component");
    if (DropdownControlSurface::BeginMenu("Add a stamp component", Scale))
    {
        ConstructAddComponentMenu();
        DropdownControlSurface::EndMenu();
    }
    // 📝 Open the requested add-component popup here, in the same stable ID-stack scope that
    // ConstructVectorPopup/ConstructImagePopup poll with BeginPopupModal below — see
    // PendingComponentPopup for why this cannot happen inside ConstructAddComponentMenu itself.
    if (PendingComponentPopup)
    {
        ImGui::OpenPopup(PendingComponentPopup);
        PendingComponentPopup = nullptr;
    }
    ConstructVectorPopup();
    ConstructImagePopup();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 8 * Scale, Origin.y + 64 * Scale));
    ImGui::BeginChild("Stamp layers body", ImVec2(Width - 16 * Scale, -45 * Scale), ImGuiChildFlags_None);
    size_t Visible = 0;
    for (const auto &Layer : Snapshot.Layers) if (Layer.Visible) ++Visible;
    const size_t Counts[]{Visible, Snapshot.Layers.size() - Visible};
    // .tp-layer-stats treatment: Texture Paint's two 66px filter cards (Visible / Hidden).
    {
        // Shared Visible/Hidden stat cards (PanelChrome::VisibilityStatCards).
        int ClickedCard = -1;
        PanelChrome::VisibilityStatCards(ImGui::GetCursorScreenPos(), (ImGui::GetContentRegionAvail().x - 20 * Scale) / 2.f, 6 * Scale, Scale,
            Counts[0], Counts[1], Filter == 1 ? 0 : Filter == 2 ? 1 : -1, ClickedCard, LightTypeface, nullptr, NoteExtent);
        if (ClickedCard >= 0) Filter = Filter == ClickedCard + 1 ? 0 : ClickedCard + 1;
    }
    // .tp-layer-search treatment: 36px pill, search glyph, frameless entry, clear icon.
    {
        const ImVec2 SearchOrigin = ImGui::GetCursorScreenPos();
        const float InnerWidth = ImGui::GetContentRegionAvail().x;
        const ImVec2 BoxMin(SearchOrigin.x + 6 * Scale, SearchOrigin.y + 12 * Scale), BoxMax(SearchOrigin.x + InnerWidth - 6 * Scale, SearchOrigin.y + 48 * Scale);
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(BoxMin, BoxMax, Ink32(0x191919), 18 * Scale);
        Ink->AddRect(BoxMin, BoxMax, Ink32(0x2c2c2c), 18 * Scale);
        DrawGlyph("M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14ZM21 21l-4.3-4.3", ImVec2(BoxMin.x + 11 * Scale, BoxMin.y + 11 * Scale), 14 * Scale, 0x77776f);
        ImGui::SetCursorScreenPos(ImVec2(BoxMin.x + 33 * Scale, BoxMin.y + 6 * Scale));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::SetNextItemWidth(BoxMax.x - BoxMin.x - 66 * Scale);
        ImGui::InputText("##FindComponent", SearchText.data(), SearchText.size());
        const bool SearchActive = ImGui::IsItemActive();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (!SearchText[0] && !SearchActive)
            Caption(ImVec2(BoxMin.x + 37 * Scale, BoxMin.y + 12 * Scale), "Find a component...", 10 * Scale, 0x787871);
        if (SearchText[0])
        {
            ImGui::SetCursorScreenPos(ImVec2(BoxMax.x - 28 * Scale, BoxMin.y + 7 * Scale));
            if (BareIconButton("ClearComponentSearch", "M6 6L18 18M6 18L18 6", 22 * Scale, Scale, false, 0x8a8a80)) SearchText[0] = '\0';
        }
        ImGui::SetCursorScreenPos(ImVec2(SearchOrigin.x, SearchOrigin.y + 56 * Scale));
    }
    // .tp-layer-order treatment.
    {
        size_t Shown = 0;
        for (const auto &Layer : Snapshot.Layers)
        {
            if (Filter == 1 && !Layer.Visible) continue;
            if (Filter == 2 && Layer.Visible) continue;
            if (FoldCaption(Layer.Name).find(FoldCaption(SearchText.data())) == std::string::npos) continue;
            ++Shown;
        }
        const ImVec2 OrderOrigin = ImGui::GetCursorScreenPos();
        const float InnerWidth = ImGui::GetContentRegionAvail().x;
        Caption(ImVec2(OrderOrigin.x + 10 * Scale, OrderOrigin.y + 2 * Scale), "TOP -> BOTTOM", 8 * Scale, 0x686862);
        char ShownText[32];
        std::snprintf(ShownText, sizeof(ShownText), "%zu shown", Shown);
        Caption(ImVec2(OrderOrigin.x + InnerWidth - 10 * Scale - CaptionWidth(ShownText, 8 * Scale), OrderOrigin.y + 2 * Scale), ShownText, 8 * Scale, 0x55554f);
        ImGui::SetCursorScreenPos(ImVec2(OrderOrigin.x, OrderOrigin.y + 21 * Scale));
        ImGui::Dummy(ImVec2(0, 0));
    }
    StampAtlasImage LayerAtlas{};
    if (QueryLayerThumbnailAtlas) LayerAtlas = QueryLayerThumbnailAtlas();
    const uint64_t ReadyLayerAtlasRevision = LayerAtlas.Texture != 0 ? QueryLayerAtlasRevision() : 0;
    const char *KindCaptions[]{"", "Text", "Imported vector", "Imported image"};
    for (auto Position = Snapshot.Layers.rbegin(); Position != Snapshot.Layers.rend(); ++Position)
    {
        const auto &Layer = *Position;
        if (Filter == 1 && !Layer.Visible) continue;
        if (Filter == 2 && Layer.Visible) continue;
        if (FoldCaption(Layer.Name).find(FoldCaption(SearchText.data())) == std::string::npos) continue;
        // 🎨 Row chrome mirrors Texture Paint's layer rows: 62px, hover/selected washes only, gold
        // accent bar, grip dots, 34px thumbnail chip, bare eye/lock glyphs at the right edge.
        const auto RowOrigin = ImGui::GetCursorScreenPos();
        const float RowX = RowOrigin.x + 2 * Scale;
        const float RowWidth = ImGui::GetContentRegionAvail().x - 4 * Scale;
        constexpr float RowHeightUnits = 62.f;
        const bool Selected = !MaskSelection && std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end();
        auto *Ink = ImGui::GetWindowDrawList();
        ImGui::PushID(int(Layer.Token));
        const std::string SelectCaption = "Select:" + std::to_string(Layer.Token);
        ImGui::SetCursorScreenPos(ImVec2(RowX + 20 * Scale, RowOrigin.y));
        const bool RowClicked = ImGui::InvisibleButton(SelectCaption.c_str(), ImVec2(std::max(30.f, RowWidth - 68 * Scale), RowHeightUnits * Scale));
        const bool RowHovered = ImGui::IsItemHovered();
        if (RowClicked) Select(Layer.Token, ImGui::GetIO().KeyShift);
        ControlExtents[SelectCaption] = {RowX, RowOrigin.y, RowX + RowWidth - 48 * Scale, RowOrigin.y + RowHeightUnits * Scale};
        if (Selected || RowHovered)
            Ink->AddRectFilled(ImVec2(RowX, RowOrigin.y), ImVec2(RowX + RowWidth, RowOrigin.y + RowHeightUnits * Scale), Ink32(Selected ? 0x262626 : 0x1b1b1b), 9 * Scale);
        if (Selected)
        {
            Ink->AddRect(ImVec2(RowX, RowOrigin.y), ImVec2(RowX + RowWidth, RowOrigin.y + RowHeightUnits * Scale), Ink32(0x3e3e3c), 9 * Scale);
            Ink->AddRectFilled(ImVec2(RowX, RowOrigin.y), ImVec2(RowX + 3 * Scale, RowOrigin.y + RowHeightUnits * Scale), Ink32(0xd9b281), 3 * Scale);
        }
        for (int DotY = 0; DotY < 3; ++DotY)
            for (int DotX = 0; DotX < 2; ++DotX)
                Ink->AddCircleFilled(ImVec2(RowX + (7 + DotX * 4) * Scale, RowOrigin.y + (25 + DotY * 6) * Scale), 1.1f * Scale, Ink32(0x55534a), 8);
        // 34px thumbnail chip: the component's own-bounds atlas raster once baked (square, centred).
        const ImVec2 ChipMin(RowX + 20 * Scale, RowOrigin.y + 14 * Scale), ChipMax(ChipMin.x + 34 * Scale, ChipMin.y + 34 * Scale);
        Ink->AddRectFilled(ChipMin, ChipMax, Ink32(0x161614), 9 * Scale);
        {
            const size_t Slot = size_t(&Layer - Snapshot.Layers.data());
            bool ThumbnailDrawn = false;
            if (LayerAtlas.Texture != 0 && Slot < LayerAtlasCapacity && ReadyLayerAtlasRevision > 0)
            {
                const float Step = float(LayerAtlasTile + 2) / LayerAtlasExtent;
                const float Span = float(LayerAtlasTile) / LayerAtlasExtent;
                const float U0 = LayerAtlas.MinimumU + (Slot % LayerAtlasColumns) * Step;
                const float V0 = LayerAtlas.MinimumV + (Slot / LayerAtlasColumns) * Step;
                Ink->AddImageRounded(ImTextureRef(LayerAtlas.Texture), ChipMin, ChipMax, ImVec2(U0, V0),
                    ImVec2(U0 + Span, V0 + Span), IM_COL32_WHITE, 9 * Scale);
                ThumbnailDrawn = true;
            }
            if (!ThumbnailDrawn)
                Ink->AddRect(ImVec2(ChipMin.x + 10 * Scale, ChipMin.y + 10 * Scale), ImVec2(ChipMin.x + 24 * Scale, ChipMin.y + 24 * Scale),
                    ImGui::GetColorU32(Spectrum(0xc7d9b8)), 2 * Scale);
        }
        const auto &Source = Snapshot.Instructions[Layer.Root];
        Ink->PushClipRect(ImVec2(ChipMax.x + 7 * Scale, RowOrigin.y), ImVec2(RowX + RowWidth - 52 * Scale, RowOrigin.y + RowHeightUnits * Scale), true);
        Caption(ImVec2(ChipMax.x + 7 * Scale, RowOrigin.y + 15 * Scale), Layer.Name.c_str(), 11 * Scale,
            !Layer.Visible ? 0x6a6a62 : Selected ? 0xeeeee8 : 0xc0c0b8);
        const char *Kind = Layer.ComponentKind != VectorAuthoringComponentKind::Freeform ? KindCaptions[uint32_t(Layer.ComponentKind)]
            : Source.Topology[0] < 6 ? ShapeCaptions[Source.Topology[0]] : "Retained vector";
        char Sub[96];
        std::snprintf(Sub, sizeof(Sub), "%s · %s%s%s", Kind, Layer.Visible ? "Visible" : "Hidden",
            Layer.Locked ? " · Locked" : "", Layer.Mask ? " · Mask" : "");
        Caption(ImVec2(ChipMax.x + 7 * Scale, RowOrigin.y + 34 * Scale), Sub, 8 * Scale, Selected ? 0xa29c90 : 0x696964);
        Ink->PopClipRect();
        if (!Layer.Locked && ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("FrontierStampComponentLayer", &Layer.Token, sizeof(Layer.Token));
            ImGui::TextUnformatted(Layer.Name.c_str());
            ImGui::EndDragDropSource();
        }
        if (!Layer.Locked && ImGui::BeginDragDropTarget())
        {
            if (const auto *Payload = ImGui::AcceptDragDropPayload("FrontierStampComponentLayer"); Payload && Payload->IsDelivery())
            {
                const auto SourceToken = *static_cast<const uint32_t *>(Payload->Data);
                EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringLayerByDrag(Document, SourceToken, Layer.Token); });
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SetCursorScreenPos(ImVec2(RowX + RowWidth - 46 * Scale, RowOrigin.y + 19 * Scale));
        const std::string VisibilityCaption = "Visibility:" + std::to_string(Layer.Token);
        const bool ToggleVisible = BareIconButton(VisibilityCaption.c_str(), Layer.Visible ? VisiblePath : HiddenPath, 20 * Scale, Scale, false, 0x8a8a80);
        NoteExtent(VisibilityCaption);
        if (ToggleVisible) EvaluateEdit([&](auto &Document) { for (auto &Candidate : Document.Layers) if (Candidate.Token == Layer.Token) Candidate.Visible = !Candidate.Visible; });
        ImGui::SetCursorScreenPos(ImVec2(RowX + RowWidth - 24 * Scale, RowOrigin.y + 19 * Scale));
        const std::string LockCaption = "Lock:" + std::to_string(Layer.Token);
        const bool ToggleLock = BareIconButton(LockCaption.c_str(), LockPath, 20 * Scale, Scale, Layer.Locked, Layer.Locked ? 0xbaa789 : 0x8a8a80);
        NoteExtent(LockCaption);
        if (ToggleLock) EvaluateEdit([&](auto &Document) { for (auto &Candidate : Document.Layers) if (Candidate.Token == Layer.Token) Candidate.Locked = !Candidate.Locked; });
        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowOrigin.y + (RowHeightUnits + 2) * Scale));
        ImGui::Dummy(ImVec2(0, 0));
        if (Layer.Mask)
        {
            const ImVec2 MaskOrigin = ImGui::GetCursorScreenPos();
            const float MaskX = MaskOrigin.x + 27 * Scale;
            const float MaskWidth = ImGui::GetContentRegionAvail().x - 29 * Scale;
            const bool MaskSelectedRow = MaskSelection == Layer.Token;
            ImGui::SetCursorScreenPos(ImVec2(MaskX, MaskOrigin.y));
            const bool MaskClicked = ImGui::InvisibleButton("##MaskSelect", ImVec2(MaskWidth, 43 * Scale));
            NoteExtent("Mask · " + Layer.Name);
            if (MaskClicked) { Selection = {Layer.Token}; MaskSelection = Layer.Token; }
            if (MaskSelectedRow || ImGui::IsItemHovered())
                Ink->AddRectFilled(ImVec2(MaskX, MaskOrigin.y), ImVec2(MaskX + MaskWidth, MaskOrigin.y + 43 * Scale), Ink32(MaskSelectedRow ? 0x262626 : 0x1b1b1b), 8 * Scale);
            if (MaskSelectedRow)
            {
                Ink->AddRect(ImVec2(MaskX, MaskOrigin.y), ImVec2(MaskX + MaskWidth, MaskOrigin.y + 43 * Scale), Ink32(0x3c3c37), 8 * Scale);
                Ink->AddRectFilled(ImVec2(MaskX, MaskOrigin.y), ImVec2(MaskX + 3 * Scale, MaskOrigin.y + 43 * Scale), Ink32(0xd9b281), 3 * Scale);
            }
            const ImVec2 MaskChip(MaskX + 8 * Scale, MaskOrigin.y + 11.5f * Scale);
            Ink->AddRectFilled(MaskChip, ImVec2(MaskChip.x + 20 * Scale, MaskChip.y + 20 * Scale), Ink32(0x0c0c0c), 5 * Scale);
            Ink->AddRect(MaskChip, ImVec2(MaskChip.x + 20 * Scale, MaskChip.y + 20 * Scale), Ink32(0x595954), 5 * Scale);
            Ink->AddRectFilled(ImVec2(MaskChip.x + 2 * Scale, MaskChip.y + 2 * Scale), ImVec2(MaskChip.x + 18 * Scale, MaskChip.y + 18 * Scale), Ink32(0xe6e6df), 2 * Scale);
            Caption(ImVec2(MaskChip.x + 29 * Scale, MaskOrigin.y + 8 * Scale), "Layer mask", 10 * Scale, 0xc8c8ba);
            Caption(ImVec2(MaskChip.x + 29 * Scale, MaskOrigin.y + 25 * Scale), Layer.MaskEnabled ? "Owned mask" : "Owned mask · off", 8 * Scale, 0x777770);
            ImGui::SetCursorScreenPos(ImVec2(MaskOrigin.x, MaskOrigin.y + 45 * Scale));
            ImGui::Dummy(ImVec2(0, 0));
        }
        ImGui::PopID();
    }
    Controls.ControlHeight = 30;
    if (Snapshot.Layers.empty()) ImGui::TextWrapped("Add your first component to start a decal.");
    ImGui::EndChild();
    // .tp-pane-footer treatment: stack details on the left, selection controls on the right.
    {
        const ImVec2 FooterOrigin(Origin.x, ImGui::GetCursorScreenPos().y);
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(FooterOrigin, ImVec2(Origin.x + Width, FooterOrigin.y + 45 * Scale), Ink32(0x151515));
        Ink->AddLine(FooterOrigin, ImVec2(Origin.x + Width, FooterOrigin.y), Ink32(0xffffff, .02f));
        const auto Stat = [&](float X, const char *Label, const std::string &Value, uint32_t ValueColour)
        {
            Caption(ImVec2(Origin.x + X, FooterOrigin.y + 9 * Scale), Label, 7 * Scale, 0x5f5f58);
            Caption(ImVec2(Origin.x + X, FooterOrigin.y + 23 * Scale), Value.c_str(), 9 * Scale, ValueColour);
        };
        const float StatsAvailable = std::max(96.f * Scale, Width - 122.f * Scale);
        const bool CompactFooter = StatsAvailable < 155.f * Scale;
        Stat(10 * Scale, CompactFooter ? "COMP" : "COMPONENTS", std::to_string(Snapshot.Layers.size()), 0x8eb896);
        Stat(10 * Scale + StatsAvailable * (CompactFooter ? 0.36f : 0.40f), CompactFooter ? "VIS" : "VISIBLE", std::to_string(Visible), 0xa6a699);
        Stat(10 * Scale + StatsAvailable * (CompactFooter ? 0.68f : 0.72f), CompactFooter ? "SEL" : "SELECTED", std::to_string(Selection.size()), 0xa6a699);
        const bool FooterEditable = !Selection.empty() && !MaskSelection && !PenSource && Gesture == 0 &&
            std::none_of(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer)
            {
                return std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end() &&
                    (QueryVectorAuthoringProtection(Snapshot, Layer) || !QueryVectorAuthoringVisibility(Snapshot, Layer));
            });
        float ButtonX = Origin.x + Width - 108 * Scale;
        const float ButtonY = FooterOrigin.y + 11.5f * Scale;
        ImGui::BeginDisabled(!FooterEditable);
        ImGui::SetCursorScreenPos(ImVec2(ButtonX, ButtonY));
        if (BareIconButton("LayerFooterDuplicate", CopyPath, 22 * Scale, Scale, false, 0x8a8a80)) ReplicateSelection();
        NoteExtent("Layer footer duplicate");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Duplicate selection");
        ButtonX += 26 * Scale;
        ImGui::SetCursorScreenPos(ImVec2(ButtonX, ButtonY));
        if (BareIconButton("LayerFooterBringForward", "M12 19V5M6 11L12 5L18 11", 22 * Scale, Scale, false, 0x8a8a80))
            EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringSelection(Document, Selection, VectorSelectionOrder::Forward); });
        NoteExtent("Layer footer bring forward");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bring selection forward");
        ButtonX += 26 * Scale;
        ImGui::SetCursorScreenPos(ImVec2(ButtonX, ButtonY));
        if (BareIconButton("LayerFooterSendBackward", "M12 5V19M6 13L12 19L18 13", 22 * Scale, Scale, false, 0x8a8a80))
            EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringSelection(Document, Selection, VectorSelectionOrder::Backward); });
        NoteExtent("Layer footer send backward");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Send selection backward");
        ButtonX += 26 * Scale;
        ImGui::SetCursorScreenPos(ImVec2(ButtonX, ButtonY));
        if (BareIconButton("LayerFooterDelete", TrashPath, 22 * Scale, Scale, false, 0x8a8a80))
            EvaluateEdit([&](auto &Document) { PartitionVectorAuthoringSelection(Document, Selection); });
        NoteExtent("Layer footer delete");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Delete selection");
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(ImVec2(FooterOrigin.x, FooterOrigin.y + 45 * Scale));
        ImGui::Dummy(ImVec2(0, 0));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     GPU CANVAS, VIEW NAVIGATION AND GESTURES
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::RebuildPbrMaps(const VectorAuthoringDocument &Snapshot)
{
    PbrMapsValid = false;
    PbrMapsBuiltAt = ImGui::GetTime();
    try
    {
        auto Maps = EvaluateStampSurfaceMaps(Snapshot, uint32_t(PbrExtent));
        const size_t Count = size_t(PbrExtent) * size_t(PbrExtent);
        if (Maps.ColourRgba.size() != Count * 4 || Maps.Coverage.size() != Count) return;
        PbrBase = std::move(Maps.ColourRgba);
        PbrCoverage = std::move(Maps.Coverage);
        PbrRoughness = std::move(Maps.Roughness);
        PbrMetalness = std::move(Maps.Metalness);
        PbrHeight = std::move(Maps.Height);
        PbrNormalScale = std::move(Maps.NormalScale);
        PbrNormalX = std::move(Maps.NormalX);
        PbrNormalY = std::move(Maps.NormalY);
        PbrNormalZ = std::move(Maps.NormalZ);
        PbrOpacity = std::move(Maps.Opacity);
        PbrOcclusion = std::move(Maps.Occlusion);
        PbrTransmission = std::move(Maps.Transmission);
        PbrClearcoat = std::move(Maps.Clearcoat);
        PbrMapsValid = true;
    }
    catch (...) { PbrMapsValid = false; }
}

void StampWorkspacePanel::DrawPbrPreview(float BoardMinimumX, float BoardMinimumY, float BoardSize)
{
    const size_t Count = size_t(PbrExtent) * size_t(PbrExtent);
    if (!PbrMapsValid || PbrBase.size() != Count * 4 || PbrCoverage.size() != Count) return;
    auto *Ink = ImGui::GetWindowDrawList();
    const float CanvasUnits = QueryCanvasExtent();
    const int Blocks = std::clamp(int(BoardSize / (4.f * Scale)), 64, 176);
    const float BlockPx = BoardSize / float(Blocks);
    const float Light[3]{PbrLightX, PbrLightY, CanvasUnits * .508f}; // document-plane units, z toward the viewer
    const float BoardColour[3]{std::clamp(CanvasGroundColour[0], 0.f, 1.f),
        std::clamp(CanvasGroundColour[1], 0.f, 1.f), std::clamp(CanvasGroundColour[2], 0.f, 1.f)};
    for (int By = 0; By < Blocks; ++By)
    {
        int RunStart = -1;
        uint32_t RunKey = 0;
        ImU32 RunColour = 0;
        const auto Flush = [&](int EndX)
        {
            if (RunStart < 0) return;
            Ink->AddRectFilled(ImVec2(BoardMinimumX + RunStart * BlockPx, BoardMinimumY + By * BlockPx),
                ImVec2(BoardMinimumX + EndX * BlockPx + .5f, BoardMinimumY + (By + 1) * BlockPx + .5f), RunColour);
            RunStart = -1;
        };
        for (int Bx = 0; Bx < Blocks; ++Bx)
        {
            const int Tx = std::min(PbrExtent - 1, int((Bx + .5f) / Blocks * PbrExtent));
            const int Ty = std::min(PbrExtent - 1, int((By + .5f) / Blocks * PbrExtent));
            const size_t Texel = size_t(Ty) * size_t(PbrExtent) + size_t(Tx);
            const uint8_t *Pixel = &PbrBase[Texel * 4];
            const float Coverage = PbrCoverage[Texel];
            const float ColourAlpha = Pixel[3] / 255.f;
            if (PbrViewChannel == 0 && Coverage < .015f && ColourAlpha < .015f) { Flush(Bx); continue; }
            float Lit[3]{BoardColour[0], BoardColour[1], BoardColour[2]};
            if (PbrViewChannel != 0)
            {
                if (PbrViewChannel == 1)
                {
                    for (int Ch = 0; Ch < 3; ++Ch)
                        Lit[Ch] = (Pixel[Ch] / 255.f) * ColourAlpha + BoardColour[Ch] * (1.f - ColourAlpha);
                }
                else if (PbrViewChannel == 2)
                {
                    const float H = std::clamp(PbrHeight[Texel] * .5f + .5f, 0.f, 1.f);
                    Lit[0] = Lit[1] = Lit[2] = H;
                }
                else if (PbrViewChannel == 3)
                {
                    Lit[0] = std::clamp(PbrNormalX[Texel] * .5f + .5f, 0.f, 1.f);
                    Lit[1] = std::clamp(PbrNormalY[Texel] * .5f + .5f, 0.f, 1.f);
                    Lit[2] = std::clamp(PbrNormalZ[Texel] * .5f + .5f, 0.f, 1.f);
                }
                else
                {
                    float Scalar = 0.f;
                    if (PbrViewChannel == 4) Scalar = PbrRoughness[Texel];
                    else if (PbrViewChannel == 5) Scalar = PbrMetalness[Texel];
                    else if (PbrViewChannel == 6) Scalar = PbrOpacity[Texel];
                    else if (PbrViewChannel == 7) Scalar = PbrOcclusion[Texel];
                    else if (PbrViewChannel == 8) Scalar = PbrClearcoat[Texel];
                    else if (PbrViewChannel == 9) Scalar = PbrTransmission[Texel];
                    Scalar = std::clamp(Scalar, 0.f, 1.f);
                    Lit[0] = Lit[1] = Lit[2] = Scalar;
                }
            }
            else
            {
                const float Rough = std::clamp(PbrRoughness[Texel], .06f, 1.f);
                const float Metal = std::clamp(PbrMetalness[Texel], 0.f, 1.f);
                const float Ao = std::clamp(PbrOcclusion[Texel], 0.f, 1.f);
                const float Trans = std::clamp(PbrTransmission[Texel], 0.f, 1.f);
                const float Coat = std::clamp(PbrClearcoat[Texel], 0.f, 1.f);
                const float Normal[3]{PbrNormalX[Texel], PbrNormalY[Texel], PbrNormalZ[Texel]};

                float BaseC[3];
                const float PigmentMix = ColourAlpha * (1.f - Trans * .78f);
                for (int Ch = 0; Ch < 3; ++Ch)
                    BaseC[Ch] = (Pixel[Ch] / 255.f) * PigmentMix + BoardColour[Ch] * (1.f - PigmentMix);

                const float Point[3]{(Bx + .5f) / Blocks * CanvasUnits, (By + .5f) / Blocks * CanvasUnits, 0.f};
                float ToLight[3]{Light[0] - Point[0], Light[1] - Point[1], Light[2]};
                const float Distance = std::sqrt(ToLight[0] * ToLight[0] + ToLight[1] * ToLight[1] + ToLight[2] * ToLight[2]);
                ToLight[0] /= Distance; ToLight[1] /= Distance; ToLight[2] /= Distance;
                const float FalloffRef = CanvasUnits * 1.21f;
                const float Attenuation = 1.f / (1.f + (Distance / FalloffRef) * (Distance / FalloffRef));
                const float NdL = std::max(0.f, Normal[0] * ToLight[0] + Normal[1] * ToLight[1] + Normal[2] * ToLight[2]);
                float Half[3]{ToLight[0], ToLight[1], ToLight[2] + 1.f};
                const float HalfLength = std::sqrt(Half[0] * Half[0] + Half[1] * Half[1] + Half[2] * Half[2]);
                Half[0] /= HalfLength; Half[1] /= HalfLength; Half[2] /= HalfLength;
                const float NdH = std::max(0.f, Normal[0] * Half[0] + Normal[1] * Half[1] + Normal[2] * Half[2]);
                const float VdH = std::clamp(Half[2], 0.f, 1.f);
                const float A = Rough * Rough, A2 = A * A;
                const float Denominator = NdH * NdH * (A2 - 1.f) + 1.f;
                const float Distribution = A2 / std::max(1e-3f, Denominator * Denominator);
                const float FresnelBase = std::pow(1.f - VdH, 5.f);

                const float CoatAlpha = .06f * .06f, CoatA2 = CoatAlpha * CoatAlpha;
                const float CoatDenom = NdH * NdH * (CoatA2 - 1.f) + 1.f;
                const float CoatDist = CoatA2 / std::max(1e-4f, CoatDenom * CoatDenom);
                const float CoatFresnel = .04f + .96f * FresnelBase;
                const float CoatSpec = Coat * CoatDist * CoatFresnel * NdL * .42f;
                const float UnderAtten = 1.f - Coat * CoatFresnel;

                const float BlendAlpha = std::clamp(std::max(Coverage, ColourAlpha), 0.f, 1.f);
                for (int Ch = 0; Ch < 3; ++Ch)
                {
                    const float F0 = .04f + (BaseC[Ch] - .04f) * Metal;
                    const float Fresnel = F0 + (1.f - F0) * FresnelBase;
                    const float Ambient = BaseC[Ch] * .18f * Ao * (.4f + .6f * Normal[2]) * (1.f - Metal * .55f);
                    const float Diffuse = BaseC[Ch] * (1.f - Metal) * (1.f - Trans * .75f) * NdL;
                    const float Specular = Distribution * Fresnel * NdL * .55f;
                    const float Refracted = Trans * BoardColour[Ch] * (.68f + .32f * NdL) * Ao;
                    const float CoatAmbient = Coat * .035f * Ao * (.5f + .5f * Normal[2]);
                    const float Shaded = UnderAtten * (Ambient + Refracted + Attenuation * (Diffuse + Specular)) + CoatAmbient + Attenuation * CoatSpec;
                    Lit[Ch] = std::clamp(Shaded * BlendAlpha + BoardColour[Ch] * (1.f - BlendAlpha), 0.f, 1.f);
                }
            }
            const uint32_t Qr = uint32_t(Lit[0] * 255) >> 3, Qg = uint32_t(Lit[1] * 255) >> 3, Qb = uint32_t(Lit[2] * 255) >> 3;
            const uint32_t Key = (Qr << 10) | (Qg << 5) | Qb;
            const ImU32 Colour = IM_COL32(int(Qr << 3 | Qr >> 2), int(Qg << 3 | Qg >> 2), int(Qb << 3 | Qb >> 2), 255);
            if (RunStart < 0) { RunStart = Bx; RunKey = Key; RunColour = Colour; }
            else if (Key != RunKey) { Flush(Bx); RunStart = Bx; RunKey = Key; RunColour = Colour; }
        }
        Flush(Blocks);
    }
}

void StampWorkspacePanel::ConstructCanvas()
{
    CapturePanel("Viewport");
    const auto ContentOrigin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const float AvailableHeight = ImGui::GetContentRegionAvail().y;
    const float CanvasUnits = QueryCanvasExtent();
    const int CanvasUnitsInt = int(std::lround(CanvasUnits));
    const auto At = [&](float X, float Y) { return ImVec2(ContentOrigin.x + X * Scale, ContentOrigin.y + Y * Scale); };
    // Slim document row: editable DECAL CANVAS (<N> UNITS) + stamp name + '*' unsaved marker + Save + Export icon buttons.
    const char *OverlinePrefix = MaskSelection ? "OWNED MASK (" : "DECAL CANVAS (";
    Caption(At(24, 16), OverlinePrefix, 8 * Scale, 0x8d8d7b);
    float HeaderCursorX = 24.f + CaptionWidth(OverlinePrefix, 8 * Scale) / Scale + 2.f;
    {
        int EditableUnits = CanvasUnitsInt;
        ImGui::SetCursorScreenPos(At(HeaderCursorX, 9));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Spectrum(0x1f201d));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Spectrum(0x282a25));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Spectrum(0x2f322b));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5 * Scale, 2 * Scale));
        ImGui::SetNextItemWidth(50 * Scale);
        const bool ValueEdited = ImGui::DragInt("##StampCanvasUnits", &EditableUnits, 4.f, 64, 4096, "%d");
        const bool Committed = ImGui::IsItemDeactivatedAfterEdit() || (ValueEdited && ImGui::IsMouseDragging(0));
        const auto UnitMin = ImGui::GetItemRectMin(), UnitMax = ImGui::GetItemRectMax();
        ControlExtents["Canvas units"] = {UnitMin.x, UnitMin.y, UnitMax.x, UnitMax.y};
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        if (Committed) ApplyCanvasExtent(float(EditableUnits));
        Controls.Tooltip("Decal canvas units", "Drag or double-click to edit the decal canvas size (64 - 4096 units).");
        HeaderCursorX += 53.f;
    }
    Caption(At(HeaderCursorX, 16), "UNITS)", 8 * Scale, 0x8d8d7b);
    HeaderCursorX += CaptionWidth("UNITS)", 8 * Scale) / Scale + 3.f;
    ImGui::SetCursorScreenPos(At(HeaderCursorX, 9));
    if (BareIconButton("Canvas size presets", ChevronDownPath, 18 * Scale, Scale, false, 0x9a9e8c))
        ImGui::OpenPopup("Stamp canvas size menu");
    {
        const auto PresetMin = ImGui::GetItemRectMin(), PresetMax = ImGui::GetItemRectMax();
        ControlExtents["Canvas size presets"] = {PresetMin.x, PresetMin.y, PresetMax.x, PresetMax.y};
    }
    Controls.Tooltip("Canvas size presets", "Choose a standard decal resolution or toggle component scaling on resize.");
    if (DropdownControlSurface::BeginMenu("Stamp canvas size menu", Scale))
    {
        static constexpr int Presets[]{128, 256, 512, 1024, 2048, 4096};
        for (const int Preset : Presets)
        {
            char PresetLabel[48]{};
            std::snprintf(PresetLabel, sizeof(PresetLabel), "%d \u00d7 %d units", Preset, Preset);
            if (DropdownControlSurface::Option(PresetLabel, CanvasUnitsInt == Preset, Scale))
            {
                ApplyCanvasExtent(float(Preset));
                ImGui::CloseCurrentPopup();
            }
        }
        Controls.Toggle("Scale components on resize", ScaleComponentsOnCanvasResize);
        DropdownControlSurface::EndMenu();
    }
    HeaderCursorX += 26.f;
    auto Name = QueryDocument().Name;
    std::snprintf(NameText.data(), NameText.size(), "%s", Name.c_str());
    const float NameFontPx = 11 * 1.302f * Scale;
    ImGui::PushFont(static_cast<ImFont *>(LightTypeface ? LightTypeface : RegularTypeface), NameFontPx);
    const float DynamicNameWidth = std::clamp((ImGui::CalcTextSize(NameText.data()).x / Scale) + 22.f, 96.f, 210.f);
    ImGui::SetCursorScreenPos(At(HeaderCursorX, 6));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::SetNextItemWidth(DynamicNameWidth * Scale);
    if (ImGui::InputText("##StampName", NameText.data(), NameText.size())) EvaluateEdit([&](auto &Document) { Document.Name = NameText.data(); });
    ImGui::PopStyleColor();
    ImGui::PopFont();
    Controls.Tooltip("Stamp name", "Names the saved document and its stamp-library PNG.");
    HeaderCursorX += DynamicNameWidth + 2.f;
    const bool IsModified = QueryModified();
    if (IsModified)
    {
        Caption(At(HeaderCursorX, 11), "*", 14 * Scale, 0xe6be6e);
        ControlExtents["Unsaved indicator"] = {At(HeaderCursorX, 9).x, At(HeaderCursorX, 9).y, At(HeaderCursorX + 12, 29).x, At(HeaderCursorX + 12, 29).y};
        HeaderCursorX += 12.f;
    }
    else HeaderCursorX += 4.f;
    ImGui::SetCursorScreenPos(At(HeaderCursorX, 6));
    if (BareIconButton("Save stamp", SaveIconPath, 24 * Scale, Scale, false, IsModified ? 0xe6be6eu : 0x9a9e8cu))
        Save();
    {
        const auto SaveMin = ImGui::GetItemRectMin(), SaveMax = ImGui::GetItemRectMax();
        ControlExtents["Save stamp"] = {SaveMin.x, SaveMin.y, SaveMax.x, SaveMax.y};
    }
    Controls.Tooltip("Save stamp (Ctrl+S)", IsModified ? "Save unsaved changes to this stamp and bake its library PNG." : "Stamp is saved — click to re-save to the stamp library.");
    HeaderCursorX += 26.f;
    ImGui::SetCursorScreenPos(At(HeaderCursorX, 6));
    if (BareIconButton("Export stamp", ExportIconPath, 24 * Scale, Scale, false, 0xb8bdabu))
    {
        if (!ExportPrefixSeeded || ExportPrefixText[0] == '\0')
        {
            std::snprintf(ExportPrefixText.data(), ExportPrefixText.size(), "%s", Name.empty() ? "CustomStamp" : Name.c_str());
            ExportPrefixSeeded = true;
        }
        ExportDialogRequested = true;
    }
    {
        const auto ExportMin = ImGui::GetItemRectMin(), ExportMax = ImGui::GetItemRectMax();
        ControlExtents["Export stamp"] = {ExportMin.x, ExportMin.y, ExportMax.x, ExportMax.y};
    }
    Controls.Tooltip("Export stamp", "Configure stamp export format, resolution, and PBR channel maps.");
    if (Width / Scale - HeaderCursorX > 156.f)
        Caption(At(Width / Scale - 138, 16), IsModified ? "Unsaved changes *" : "Document saved", 9 * Scale, IsModified ? 0xd9b281 : 0x969b81);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(At(24, 44), At(Width / Scale - 24, 94), ImGui::GetColorU32(Spectrum(0x191919)), 8 * Scale);
    ImGui::SetCursorScreenPos(At(32, 53));
    Controls.ControlHeight = 32;
    if (Controls.Button("Select tool (V)", 32 * Scale, Tool == VectorAuthoringTool::Select, SelectPath)) Tool = VectorAuthoringTool::Select;
    Controls.Tooltip("Select", "Select visible GPU contours; Shift-click to extend. Drag once for one undoable source edit.");
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Point tool (A)", 32 * Scale, Tool == VectorAuthoringTool::Points, "M4 3L11 21L14 14L21 11Z")) Tool = VectorAuthoringTool::Points;
    Controls.Tooltip("Point editing", "Select one non-Boolean source (a primitive is converted to an editable path on first drag). Click a segment to split; drag it to bend with fixed anchors. Drag anchors/tangents; Alt moves tangents independently. Escape cancels.");
    for (uint32_t Category = 0; Category < 2; ++Category)
    {
        ImGui::SameLine(0, 4 * Scale);
        if (Controls.Button(Category ? "Ellipse tool (O)" : "Rectangle tool (R)", 32 * Scale, Tool == (Category ? VectorAuthoringTool::Ellipse : VectorAuthoringTool::Rectangle), ShapePaths[Category])) Tool = Category ? VectorAuthoringTool::Ellipse : VectorAuthoringTool::Rectangle;
        Controls.Tooltip(ShapeCaptions[Category], "Drag on the canvas to author an editable source shape.");
    }
    // Polygon + Star are first-class toolbar tools (they used to hide in "More tools").
    for (uint32_t Category = 4; Category < 6; ++Category)
    {
        ImGui::SameLine(0, 4 * Scale);
        const VectorAuthoringTool ShapeTool = VectorAuthoringTool(uint32_t(VectorAuthoringTool::Diamond) + Category - 2);
        if (Controls.Button(Category == 4 ? "Polygon tool" : "Star tool", 32 * Scale, Tool == ShapeTool, ShapePaths[Category])) Tool = ShapeTool;
        const auto ItemMinimum = ImGui::GetItemRectMin(), ItemMaximum = ImGui::GetItemRectMax();
        ControlExtents[ShapeCaptions[Category]] = {ItemMinimum.x, ItemMinimum.y, ItemMaximum.x, ItemMaximum.y};
        Controls.Tooltip(ShapeCaptions[Category], "Drag on the canvas to author an editable source shape.");
    }
    ImGui::SameLine(0, 4 * Scale);
    // 📝 Text is a real drag-to-draw tool like Rectangle/Ellipse, not a modal trigger: drag a box on
    // the canvas (a plain click uses a sensible default box), then write directly into it — see the
    // Gesture == 2 (drawing the box) / Gesture == 8 (composing text, ConstructTextComposer) handling
    // further down in this function.
    const bool AtComponentCap = QueryDocument().Layers.size() >= 24;
    ImGui::BeginDisabled(AtComponentCap);
    if (Controls.Button("Text tool (T)", 32 * Scale, Tool == VectorAuthoringTool::Text, TextIconPath)) Tool = VectorAuthoringTool::Text;
    ImGui::EndDisabled();
    Controls.Tooltip("Text", AtComponentCap ? "Stamp decals are capped at 24 components (Pattern allows 64)." : "Drag a box on the canvas and write directly into it. Real glyph-outline geometry, read from the bundled DM Sans font.");
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Pen tool (P)", 32 * Scale, Tool == VectorAuthoringTool::Pen, "M12 3L21 12L15 21L3 9ZM3 9L12 12L15 21M12 12V3")) Tool = VectorAuthoringTool::Pen;
    Controls.Tooltip("Pen", "Click anchors; drag tangents for curves. Hold Space during placement to reposition the new anchor and controls. Alt-click the last pending anchor to break its outgoing tangent; drag independently. Click an open endpoint to continue; while continuing, click another path endpoint to join. Ctrl bypasses endpoint joining or starts a separate path. Shift constrains direction; Alt moves independently. First anchor closes; Enter finishes; Escape cancels.");
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Polyline tool (L)", 32 * Scale, Tool == VectorAuthoringTool::Polyline, "M4 19L9 7L15 16L20 5")) Tool = VectorAuthoringTool::Polyline;
    Controls.Tooltip("Polyline", "Same as Pen, but every anchor stays a sharp corner - straight segments only, no curve tangents. Click the first anchor again to close it into a filled shape; Enter finishes it open as a plain line. Shift constrains direction; Enter finishes; Escape cancels.");
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Pan tool", 32 * Scale, Tool == VectorAuthoringTool::Pan, "M7 12V6Q8 3 9 6V11V4Q11 1 12 4V11V5Q15 2 15 5V12V8Q18 5 18 8V16Q16 23 10 21L3 14Q2 10 5 12L7 14")) Tool = VectorAuthoringTool::Pan;
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("More tools", 78 * Scale)) ImGui::OpenPopup("Stamp more tools");
    if (DropdownControlSurface::BeginMenu("Stamp more tools", Scale))
    {
        for (uint32_t Category = 2; Category < 4; ++Category)
        {
            if (DropdownControlSurface::Option(ShapeCaptions[Category], false, Scale)) { Tool = VectorAuthoringTool(uint32_t(VectorAuthoringTool::Diamond) + Category - 2); ImGui::CloseCurrentPopup(); }
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            ControlExtents[ShapeCaptions[Category]] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
        }
        Controls.Colour("Canvas colour", CanvasGroundColour);
        Controls.Toggle("Transparent canvas", TransparentCanvas);
        Controls.Toggle("Scale components on resize", ScaleComponentsOnCanvasResize);
        Controls.Toggle("Show grid", GridEnabled);
        Controls.Toggle("Snap to grid", SnappingEnabled);
        DropdownControlSurface::EndMenu();
    }
    Controls.ControlHeight = 24;
    Ink->AddRectFilled(At(24, 94), At(Width / Scale - 24, 132), ImGui::GetColorU32(ThemeQuad(ControlKit::Palette().Raised)));
    char VectorCanvasLabel[64]{};
    std::snprintf(VectorCanvasLabel, sizeof(VectorCanvasLabel), "VECTOR CANVAS    %d \u00d7 %d", CanvasUnitsInt, CanvasUnitsInt);
    Caption(At(36, 109), VectorCanvasLabel, 8 * Scale, 0x888888);
    // 💡 Lit-material view of the same canvas: layers shaded by their PBR surface channels under a
    // light you drag across the board (the sun handle). Pure preview; the tools keep editing.
    if (PbrPreview && !MaskSelection && Width / Scale >= 590.f)
    {
        static constexpr const char *ChannelModes[]{
            "Lit", "Colour", "Height", "Normal", "Roughness", "Metalness", "Opacity", "AO", "Clearcoat", "Transmission"};
        ImGui::SetCursorScreenPos(At(Width / Scale - 421, 101));
        std::string ModeCaption = std::string("View: ") + ChannelModes[std::clamp(PbrViewChannel, 0, 9)];
        if (Controls.Button(ModeCaption.c_str(), 104 * Scale, false, {}, true))
            ImGui::OpenPopup("Stamp PBR view");
        if (DropdownControlSurface::BeginMenu("Stamp PBR view", Scale))
        {
            for (int Mode = 0; Mode < 10; ++Mode)
                if (DropdownControlSurface::Option(ChannelModes[Mode], PbrViewChannel == Mode, Scale))
                {
                    PbrViewChannel = Mode;
                    ImGui::CloseCurrentPopup();
                }
            DropdownControlSurface::EndMenu();
        }
    }
    ImGui::SetCursorScreenPos(At(Width / Scale - 311, 101));
    ImGui::BeginDisabled(MaskSelection != 0);
    if (Controls.Button("PBR preview", 84 * Scale, PbrPreview && !MaskSelection, "M12 17a5 5 0 1 0 0-10 5 5 0 0 0 0 10ZM12 2V4M12 20V22M2 12H4M20 12H22M4.9 4.9L6.3 6.3M17.7 17.7L19.1 19.1M4.9 19.1L6.3 17.7M17.7 6.3L19.1 4.9")) PbrPreview = !PbrPreview;
    ImGui::EndDisabled();
    Controls.Tooltip("PBR preview", "Shade the design with all 9 PBR surface channels (colour, signed height, derived normal, roughness, metalness, opacity, AO, clearcoat, transmission) under a movable light, or inspect each channel output individually.");
    ImGui::SetCursorScreenPos(At(Width / Scale - 219, 101));
    if (Controls.Button("Zoom out", 26 * Scale, false, "M5 12H19")) Zoom = ApplyWorkspaceZoomStep(Zoom, 1.f / 1.25f);
    ImGui::SameLine(0, 4 * Scale);
    char ZoomText[32]; std::snprintf(ZoomText, sizeof(ZoomText), "%.0f%%", Zoom * 100);
    Controls.Button(ZoomText, 62 * Scale, false, {}, true);
    ImGui::SameLine(0, 4 * Scale);
    // 📝 Raised from 8x (800%) to 10x (1000%): the bake ceiling both canvases clamp their
    // on-screen device-pixel request to is 4096, so even a fit-sized board at 1000% zoom just
    // clamps its coverage request to that ceiling instead of growing unbounded.
    if (Controls.Button("Zoom in", 26 * Scale, false, PlusPath)) Zoom = ApplyWorkspaceZoomStep(Zoom, 1.25f);
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Fit", 46 * Scale, false, {}, true)) { Zoom = 1; Pan = {}; }
    Controls.ControlHeight = 30;
    // The canvas owns whatever height the dock gives it (minus the hint bar): at Zoom 1 ("Fit") the
    // board fills the region's short side, grows/shrinks with the window, and the panel never scrolls.
    const ImVec2 Minimum = At(25, 131);
    const ImVec2 Maximum(At(Width / Scale - 25, 0).x, std::max(Minimum.y + 120 * Scale, ContentOrigin.y + AvailableHeight - 41 * Scale));
    Ink->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(ThemeQuad(ControlKit::Palette().Inset)));
    const float FitSize = std::max(32.f, std::min(Maximum.x - Minimum.x, Maximum.y - Minimum.y) - 56 * Scale);
    const float BoardSize = FitSize * Zoom;
    const ImVec2 Centre((Minimum.x + Maximum.x) * .5f + Pan.X, (Minimum.y + Maximum.y) * .5f + Pan.Y);
    const ImVec2 BoardMinimum(Centre.x - BoardSize * .5f, Centre.y - BoardSize * .5f), BoardMaximum(Centre.x + BoardSize * .5f, Centre.y + BoardSize * .5f);
    BoardExtent = {BoardMinimum.x, BoardMinimum.y, BoardMaximum.x, BoardMaximum.y};
    Ink->PushClipRect(Minimum, Maximum, true);
    Ink->AddRectFilled(BoardMinimum, BoardMaximum,
        ImGui::GetColorU32(ImVec4(std::clamp(CanvasGroundColour[0], 0.f, 1.f),
            std::clamp(CanvasGroundColour[1], 0.f, 1.f), std::clamp(CanvasGroundColour[2], 0.f, 1.f), 1.f)));
    if (TransparentCanvas)
    {
        for (float Y = BoardMinimum.y; Y < BoardMaximum.y; Y += 12 * Scale)
            for (float X = BoardMinimum.x; X < BoardMaximum.x; X += 12 * Scale)
                if ((int((X - BoardMinimum.x) / (12 * Scale)) + int((Y - BoardMinimum.y) / (12 * Scale))) % 2)
                    Ink->AddRectFilled(ImVec2(X, Y), ImVec2(std::min(X + 12 * Scale, BoardMaximum.x), std::min(Y + 12 * Scale, BoardMaximum.y)), ImGui::GetColorU32(Spectrum(0xb5b8ae)));
    }
    float GridStepUnits = 64.0f;
    const float MaxRulerPixelSpacing = 140.0f * Scale, MinRulerPixelSpacing = 28.0f * Scale;
    while (GridStepUnits / CanvasUnits * BoardSize > MaxRulerPixelSpacing && GridStepUnits > 4.0f) GridStepUnits *= 0.5f;
    while (GridStepUnits / CanvasUnits * BoardSize < MinRulerPixelSpacing && GridStepUnits < CanvasUnits) GridStepUnits *= 2.0f;
    const auto &Snapshot = QueryDocument();
    const uint64_t HistoryRevision = History.QueryRevision();
    if (!CachedCoverageValid || CachedCoverageMask != MaskSelection || CachedHistoryRevision != HistoryRevision ||
        CachedGestureRevision != GestureRevision)
    {
        CachedCoverageProgram = ConstructVectorAuthoringCoverage(ConstructEvaluatedVectorAuthoringDocument(Snapshot));
        if (MaskSelection)
        {
            const auto Position = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Layer.Token == MaskSelection; });
            if (Position != Snapshot.Layers.end() && Position->Mask)
                CachedCoverageProgram = VectorCoverageProgram{{NormalizeVectorInstructionForGpu(*Position->Mask, Snapshot.CanvasExtent)}, Snapshot.Segments, 0};
        }
        CachedCoverageMask = MaskSelection;
        CachedHistoryRevision = HistoryRevision;
        CachedGestureRevision = GestureRevision;
        CachedCoverageValid = true;
    }
    // 💡 Channel maps follow the same revisions the coverage program does (throttled while a
    // gesture streams edits, so dragging a shape never pays 25 rasterizations per pointer move).
    const bool PbrActive = PbrPreview && !MaskSelection;
    if ((PbrActive || (!QueryCoverage && !Snapshot.Layers.empty())) &&
        (!PbrMapsValid || PbrMapsHistoryRevision != HistoryRevision || PbrMapsGestureRevision != GestureRevision) &&
        (!PbrMapsValid || ImGui::GetTime() - PbrMapsBuiltAt > .12))
    {
        RebuildPbrMaps(Snapshot);
        PbrMapsHistoryRevision = HistoryRevision;
        PbrMapsGestureRevision = GestureRevision;
    }
    const float DisplayPixels = BoardSize * ImGui::GetIO().DisplayFramebufferScale.x;
    const uint32_t RequestedExtent = uint32_t(std::clamp(std::ceil(DisplayPixels), 1.f, 4096.f));
    const auto Texture = QueryCoverage ? QueryCoverage(CachedCoverageProgram, RequestedExtent) : 0;
    if ((PbrActive || (!Texture && !Snapshot.Layers.empty())) && PbrMapsValid) DrawPbrPreview(BoardMinimum.x, BoardMinimum.y, BoardSize);
    else if (Texture) Ink->AddImage(ImTextureRef(Texture), BoardMinimum, BoardMaximum);
    else if (Snapshot.Layers.empty()) Caption(ImVec2(BoardMinimum.x + 12 * Scale, BoardMinimum.y + 12 * Scale), "Empty decal canvas", 11 * Scale, 0x68695a);
    else Caption(ImVec2(BoardMinimum.x + 12 * Scale, BoardMinimum.y + 12 * Scale), "GPU coverage unavailable", 11 * Scale, 0x68695a);
    const bool BakePending = Texture && QueryCoverageReady && !QueryCoverageReady();
    // 📝 Same rule as Pattern: every fresh edit revision restarts the pending clock, so the badge
    // only ever means "still catching up after you stopped editing", never mid-drag noise.
    const uint64_t EditRevision = History.QueryRevision() * 1315423911ull ^ uint64_t(GestureRevision);
    if (BakePending)
    {
        if (PendingBakeSince < 0 || EditRevision != PendingBakeEditRevision) { PendingBakeSince = ImGui::GetTime(); PendingBakeEditRevision = EditRevision; }
    }
    else PendingBakeSince = -1;
    if (BakePending && PendingBakeSince >= 0 && ImGui::GetTime() - PendingBakeSince > 0.15)
    {
        const char *Badge = "Baking decal...";
        const ImVec2 BadgeExtent = ImGui::CalcTextSize(Badge);
        const ImVec2 BadgeMaximum(BoardMaximum.x - 10 * Scale, BoardMinimum.y + 10 * Scale + BadgeExtent.y + 10 * Scale);
        const ImVec2 BadgeMinimum(BadgeMaximum.x - BadgeExtent.x - 20 * Scale, BoardMinimum.y + 10 * Scale);
        Ink->AddRectFilled(BadgeMinimum, BadgeMaximum, IM_COL32(26, 30, 24, 225), 6 * Scale);
        Ink->AddRect(BadgeMinimum, BadgeMaximum, IM_COL32(191, 216, 169, 255), 6 * Scale);
        Ink->AddText(ImVec2(BadgeMinimum.x + 10 * Scale, BadgeMinimum.y + 5 * Scale), IM_COL32(212, 232, 192, 255), Badge);
    }
    if (GridEnabled)
    {
        for (float Coordinate = 0; Coordinate <= CanvasUnits + .01f; Coordinate += GridStepUnits)
        {
            const float Pixel = Coordinate / CanvasUnits * BoardSize;
            Ink->AddLine(ImVec2(BoardMinimum.x + Pixel, BoardMinimum.y), ImVec2(BoardMinimum.x + Pixel, BoardMaximum.y), IM_COL32(80, 80, 80, 75));
            Ink->AddLine(ImVec2(BoardMinimum.x, BoardMinimum.y + Pixel), ImVec2(BoardMaximum.x, BoardMinimum.y + Pixel), IM_COL32(80, 80, 80, 75));
        }
    }
    Ink->AddRectFilled(Minimum, ImVec2(Maximum.x, Minimum.y + 21 * Scale), ImGui::GetColorU32(ThemeQuad(ControlKit::Palette().Panel)));
    Ink->AddRectFilled(Minimum, ImVec2(Minimum.x + 23 * Scale, Maximum.y), ImGui::GetColorU32(ThemeQuad(ControlKit::Palette().Panel)));
    const int GridStep = std::max(1, int(GridStepUnits));
    const int RulerHalf = int(std::ceil(CanvasUnits * .5f / float(GridStep))) * GridStep;
    const int RulerMax = CanvasUnitsInt + RulerHalf;
    for (int Coordinate = -RulerHalf; Coordinate <= RulerMax; Coordinate += GridStep)
    {
        const float Horizontal = BoardMinimum.x + float(Coordinate) / CanvasUnits * BoardSize;
        const float Vertical = BoardMinimum.y + float(Coordinate) / CanvasUnits * BoardSize;
        char Label[16]; std::snprintf(Label, sizeof(Label), "%d", Coordinate);
        if (Horizontal > Minimum.x + 24 * Scale && Horizontal < Maximum.x)
        {
            Ink->AddLine(ImVec2(Horizontal, Minimum.y), ImVec2(Horizontal, Minimum.y + 20 * Scale), IM_COL32(136, 136, 136, 190));
            Caption(ImVec2(Horizontal + 4 * Scale, Minimum.y + 4 * Scale), Label, 7 * Scale, 0x888888);
        }
        if (Vertical > Minimum.y + 22 * Scale && Vertical < Maximum.y)
        {
            Ink->AddLine(ImVec2(Minimum.x, Vertical), ImVec2(Minimum.x + 22 * Scale, Vertical), IM_COL32(136, 136, 136, 190));
            Caption(ImVec2(Minimum.x + 3 * Scale, Vertical + 3 * Scale), Label, 7 * Scale, 0x888888);
        }
    }
    std::array<int, 2> HoveredResize{};
    bool HoveredRotation = false;
    int GradientHoverSlot = -1;
    float GradientLineHoverT = -1;
    int GradientAxisHoverEnd = -1;
    int FreeformHoverSlot = -1;
    if (!Selection.empty() && !MaskSelection)
    {
        try
        {
            const auto Extent = QueryVectorSelectionExtent(Snapshot, Selection);
            const auto &Bounds = Extent.Bounds;
            const auto ScreenPoint = [&](float X, float Y)
            {
                const float θ = Extent.Rotation;
                return ImVec2(BoardMinimum.x + (Bounds[0] + X * std::cos(θ) - Y * std::sin(θ)) / CanvasUnits * BoardSize,
                              BoardMinimum.y + (Bounds[1] + X * std::sin(θ) + Y * std::cos(θ)) / CanvasUnits * BoardSize);
            };
            const std::array<ImVec2, 4> Corners{ScreenPoint(-Bounds[2] * .5f, -Bounds[3] * .5f),
                ScreenPoint(Bounds[2] * .5f, -Bounds[3] * .5f), ScreenPoint(Bounds[2] * .5f, Bounds[3] * .5f),
                ScreenPoint(-Bounds[2] * .5f, Bounds[3] * .5f)};
            Ink->AddPolyline(Corners.data(), 4, IM_COL32(191, 216, 169, 255), ImDrawFlags_Closed, 1.3f * Scale);
            bool Editable = true;
            for (const auto &Layer : Snapshot.Layers)
                if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end() &&
                    (QueryVectorAuthoringProtection(Snapshot, Layer) || !QueryVectorAuthoringVisibility(Snapshot, Layer))) Editable = false;
            if (Editable)
            {
                if (Tool == VectorAuthoringTool::Select)
                {
                    const auto RegisterGrip = [&](const char *Name, ImVec2 Point)
                    {
                        const float Radius = 6 * Scale;
                        ControlExtents[Name] = {Point.x - Radius, Point.y - Radius, Point.x + Radius, Point.y + Radius};
                        const auto Mouse = ImGui::GetIO().MousePos;
                        return std::abs(Mouse.x - Point.x) <= Radius && std::abs(Mouse.y - Point.y) <= Radius;
                    };
                    const ImVec2 Rotate = ScreenPoint(0, -Bounds[3] * .5f - 29 * Scale * CanvasUnits / BoardSize);
                    Ink->AddLine(ScreenPoint(0, -Bounds[3] * .5f), Rotate, IM_COL32(191, 216, 169, 255));
                    Ink->AddCircleFilled(Rotate, 4 * Scale, IM_COL32(212, 232, 192, 255));
                    Ink->AddCircle(Rotate, 4 * Scale, IM_COL32(40, 56, 43, 255));
                    HoveredRotation = RegisterGrip("Rotate selection", Rotate);
                    for (int Y = -1; Y <= 1; ++Y) for (int X = -1; X <= 1; ++X)
                    {
                        if (!X && !Y) continue;
                        const ImVec2 Point = ScreenPoint(float(X) * Bounds[2] * .5f, float(Y) * Bounds[3] * .5f);
                        Ink->AddRectFilled(ImVec2(Point.x - 3 * Scale, Point.y - 3 * Scale), ImVec2(Point.x + 3 * Scale, Point.y + 3 * Scale), IM_COL32(203, 225, 184, 255), Scale);
                        Ink->AddRect(ImVec2(Point.x - 3 * Scale, Point.y - 3 * Scale), ImVec2(Point.x + 3 * Scale, Point.y + 3 * Scale), IM_COL32(40, 60, 40, 255), Scale);
                        const std::string Name = std::string("Resize:") + (Y < 0 ? "n" : Y > 0 ? "s" : "") + (X < 0 ? "w" : X > 0 ? "e" : "");
                        if (RegisterGrip(Name.c_str(), Point)) HoveredResize = {X, Y};
                    }
                }
                // 🎨 Interactive gradient colour-stop markers: only for a single selected shape whose fill
                // is a gradient. Lets the ramp be edited directly on the artwork — click the axis line to
                // drop a new stop where you clicked, drag an existing marker to retime it, click one to
                // select it for colour-editing in the Inspector's ramp editor (kept in sync both ways).
                // Shown under EVERY tool, not just Select: the natural flow is "make text, give it a
                // gradient in the Inspector, drag its points" without first switching tools - gating this
                // on Select (and drawing it plain white on the pale board) made the points undiscoverable.
                // The click dispatch below already gives these grips priority over tool actions.
                if (Selection.size() == 1)
                {
                    const auto GradientLayer = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
                    if (GradientLayer != Snapshot.Layers.end() && GradientLayer->Root < Snapshot.Instructions.size())
                    {
                        const auto &GradientSource = Snapshot.Instructions[GradientLayer->Root];
                        if (uint32_t(GradientSource.Transform[3]) == 1 || uint32_t(GradientSource.Transform[3]) == 2)
                        {
                            const auto Axis = QueryGradientAxisEndpoints(GradientSource, Bounds, Extent.Rotation);
                            const ImVec2 AxisStart(BoardMinimum.x + Axis[0].X / CanvasUnits * BoardSize, BoardMinimum.y + Axis[0].Y / CanvasUnits * BoardSize);
                            const ImVec2 AxisEnd(BoardMinimum.x + Axis[1].X / CanvasUnits * BoardSize, BoardMinimum.y + Axis[1].Y / CanvasUnits * BoardSize);
                            Ink->AddLine(AxisStart, AxisEnd, IM_COL32(30, 34, 30, 200), 3.6f * Scale);
                            Ink->AddLine(AxisStart, AxisEnd, IM_COL32(255, 255, 255, 230), 1.6f * Scale);
                            const auto Mouse = ImGui::GetIO().MousePos;
                            const ImVec2 LineDelta(AxisEnd.x - AxisStart.x, AxisEnd.y - AxisStart.y);
                            const float LineLength = std::hypot(LineDelta.x, LineDelta.y);
                            const ImVec2 LineDirection = LineLength > 1e-4f ? ImVec2(LineDelta.x / LineLength, LineDelta.y / LineLength) : ImVec2(1, 0);
                            constexpr float HandleStandoff = 11.f;
                            const ImVec2 AxisHandleA(AxisStart.x - LineDirection.x * HandleStandoff * Scale, AxisStart.y - LineDirection.y * HandleStandoff * Scale);
                            const ImVec2 AxisHandleB(AxisEnd.x + LineDirection.x * HandleStandoff * Scale, AxisEnd.y + LineDirection.y * HandleStandoff * Scale);
                            for (int End = 0; End < 2; ++End)
                            {
                                const ImVec2 &LineEnd = End == 0 ? AxisStart : AxisEnd;
                                const ImVec2 &Point = End == 0 ? AxisHandleA : AxisHandleB;
                                Ink->AddLine(LineEnd, Point, IM_COL32(30, 34, 30, 200), 3 * Scale);
                                Ink->AddLine(LineEnd, Point, IM_COL32(255, 255, 255, 190), 1.2f * Scale);
                                const float HalfExtent = 5 * Scale;
                                Ink->AddRectFilled(ImVec2(Point.x - HalfExtent, Point.y - HalfExtent), ImVec2(Point.x + HalfExtent, Point.y + HalfExtent), IM_COL32(245, 248, 240, 255), 1.5f * Scale);
                                Ink->AddRect(ImVec2(Point.x - HalfExtent, Point.y - HalfExtent), ImVec2(Point.x + HalfExtent, Point.y + HalfExtent), IM_COL32(30, 34, 30, 255), 1.5f * Scale, 0, 1.8f * Scale);
                            }
                            const auto Stops = QueryVectorGradientStops(GradientSource);
                            SelectedGradientStop = std::clamp(SelectedGradientStop, 0, int(Stops.size()) - 1);
                            for (size_t Index = 0; Index < Stops.size(); ++Index)
                            {
                                const ImVec2 Point(AxisStart.x + (AxisEnd.x - AxisStart.x) * Stops[Index].Position, AxisStart.y + (AxisEnd.y - AxisStart.y) * Stops[Index].Position);
                                const bool IsSelected = int(Index) == SelectedGradientStop;
                                const float Radius = (IsSelected ? 7.f : 5.5f) * Scale;
                                const auto &C = Stops[Index].Colour;
                                const ImU32 StopColour = IM_COL32(uint8_t(std::clamp(C[0], 0.f, 1.f) * 255), uint8_t(std::clamp(C[1], 0.f, 1.f) * 255), uint8_t(std::clamp(C[2], 0.f, 1.f) * 255), 255);
                                Ink->AddCircleFilled(Point, Radius + 1.8f * Scale, IM_COL32(30, 34, 30, 215));
                                Ink->AddCircleFilled(Point, Radius, StopColour);
                                Ink->AddCircle(Point, Radius, IsSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 220), 0, (IsSelected ? 2.4f : 1.4f) * Scale);
                                const std::string Name = std::string("Gradient stop:") + std::to_string(Index);
                                const float HitRadius = 9 * Scale;
                                ControlExtents[Name] = {Point.x - HitRadius, Point.y - HitRadius, Point.x + HitRadius, Point.y + HitRadius};
                                if (std::abs(Mouse.x - Point.x) <= HitRadius && std::abs(Mouse.y - Point.y) <= HitRadius) GradientHoverSlot = int(Index);
                            }
                            {
                                const float AxisHitRadius = 9 * Scale;
                                for (int End = 0; End < 2; ++End)
                                {
                                    const ImVec2 &Point = End == 0 ? AxisHandleA : AxisHandleB;
                                    const std::string Name = std::string("Gradient axis:") + std::to_string(End);
                                    ControlExtents[Name] = {Point.x - AxisHitRadius, Point.y - AxisHitRadius, Point.x + AxisHitRadius, Point.y + AxisHitRadius};
                                    if (std::abs(Mouse.x - Point.x) <= AxisHitRadius && std::abs(Mouse.y - Point.y) <= AxisHitRadius) GradientAxisHoverEnd = End;
                                }
                            }
                            if (GradientHoverSlot < 0 && GradientAxisHoverEnd < 0 && Stops.size() < size_t(VectorGradientStopCapacity))
                            {
                                const float Along = ProjectAlongSegment({Mouse.x, Mouse.y}, {AxisStart.x, AxisStart.y}, {AxisEnd.x, AxisEnd.y});
                                if (Along > .02f && Along < .98f)
                                {
                                    const ImVec2 Nearest(AxisStart.x + Along * (AxisEnd.x - AxisStart.x), AxisStart.y + Along * (AxisEnd.y - AxisStart.y));
                                    if (std::hypot(Mouse.x - Nearest.x, Mouse.y - Nearest.y) <= 10 * Scale) GradientLineHoverT = Along;
                                }
                            }
                        }
                        else if (uint32_t(GradientSource.Transform[3]) == 3)
                        {
                            const auto Points = QueryVectorFreeformPoints(GradientSource);
                            SelectedFreeformPoint = std::clamp(SelectedFreeformPoint, 0, int(Points.size()) - 1);
                            const auto Mouse = ImGui::GetIO().MousePos;
                            for (size_t Index = 0; Index < Points.size(); ++Index)
                            {
                                const auto PlanePos = QueryGradientPlanePoint(Points[Index].Position, Bounds, Extent.Rotation);
                                const ImVec2 Pin(BoardMinimum.x + PlanePos.X / CanvasUnits * BoardSize,
                                                 BoardMinimum.y + PlanePos.Y / CanvasUnits * BoardSize);
                                const bool IsSelected = int(Index) == SelectedFreeformPoint;
                                const float Radius = (IsSelected ? 8.2f : 6.4f) * Scale;
                                const auto &C = Points[Index].Colour;
                                const ImU32 PinColour = IM_COL32(uint8_t(std::clamp(C[0], 0.f, 1.f) * 255),
                                    uint8_t(std::clamp(C[1], 0.f, 1.f) * 255), uint8_t(std::clamp(C[2], 0.f, 1.f) * 255), 255);
                                Ink->AddCircleFilled(Pin, Radius + 2.2f * Scale, IM_COL32(22, 24, 22, 230));
                                Ink->AddCircleFilled(Pin, Radius, PinColour);
                                Ink->AddCircle(Pin, Radius, IsSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(235, 235, 230, 210), 0, (IsSelected ? 2.5f : 1.5f) * Scale);
                                char PinTag[8]{};
                                std::snprintf(PinTag, sizeof(PinTag), "%zu", Index + 1u);
                                const float Lum = .2126f * C[0] + .7152f * C[1] + .0722f * C[2];
                                Caption(ImVec2(Pin.x - 2.6f * Scale, Pin.y - 4.8f * Scale), PinTag, 7.5f * Scale, Lum > .55f ? 0x141614u : 0xf6f8f2u);
                                const std::string Name = std::string("Freeform point:") + std::to_string(Index);
                                const float HitRadius = 10.f * Scale;
                                ControlExtents[Name] = {Pin.x - HitRadius, Pin.y - HitRadius, Pin.x + HitRadius, Pin.y + HitRadius};
                                if (std::hypot(Mouse.x - Pin.x, Mouse.y - Pin.y) <= HitRadius) FreeformHoverSlot = int(Index);
                            }
                        }
                    }
                }
            }
        }
        catch (const std::exception &) {}
    }
    if (Gesture == 4)
    {
        const ImVec2 Origin(BoardMinimum.x + PointerOrigin.X / CanvasUnits * BoardSize, BoardMinimum.y + PointerOrigin.Y / CanvasUnits * BoardSize);
        const auto Mouse = ImGui::GetIO().MousePos;
        const ImVec2 Lower(std::min(Origin.x, Mouse.x), std::min(Origin.y, Mouse.y));
        const ImVec2 Upper(std::max(Origin.x, Mouse.x), std::max(Origin.y, Mouse.y));
        Ink->AddRectFilled(Lower, Upper, IM_COL32(191, 216, 169, 24));
        Ink->AddRect(Lower, Upper, IM_COL32(191, 216, 169, 220));
    }
    if (Gesture == 2 && Tool == VectorAuthoringTool::Text)
    {
        const ImVec2 Origin(BoardMinimum.x + PointerOrigin.X / CanvasUnits * BoardSize, BoardMinimum.y + PointerOrigin.Y / CanvasUnits * BoardSize);
        const ImVec2 Current(BoardMinimum.x + TextBoxCurrent.X / CanvasUnits * BoardSize, BoardMinimum.y + TextBoxCurrent.Y / CanvasUnits * BoardSize);
        const ImVec2 Lower(std::min(Origin.x, Current.x), std::min(Origin.y, Current.y));
        const ImVec2 Upper(std::max(Origin.x, Current.x), std::max(Origin.y, Current.y));
        Ink->AddRectFilled(Lower, Upper, IM_COL32(134, 164, 112, 40));
        Ink->AddRect(Lower, Upper, IM_COL32(134, 164, 112, 230));
    }
    bool PbrLightHovered = false;
    if (PbrActive)
    {
        const ImVec2 Sun(BoardMinimum.x + PbrLightX / CanvasUnits * BoardSize, BoardMinimum.y + PbrLightY / CanvasUnits * BoardSize);
        const auto MousePosition = ImGui::GetIO().MousePos;
        PbrLightHovered = std::hypot(MousePosition.x - Sun.x, MousePosition.y - Sun.y) <= 15 * Scale;
        Ink->AddCircleFilled(Sun, 18 * Scale, IM_COL32(255, 214, 120, (PbrLightDrag || PbrLightHovered) ? 64 : 38), 24);
        Ink->AddCircleFilled(Sun, 9 * Scale, IM_COL32(32, 30, 24, 255), 20);
        DrawGlyph("M12 17a5 5 0 1 0 0-10 5 5 0 0 0 0 10ZM12 2V4M12 20V22M2 12H4M20 12H22M4.9 4.9L6.3 6.3M17.7 17.7L19.1 19.1M4.9 19.1L6.3 17.7M17.7 6.3L19.1 4.9", Sun, 16 * Scale, 0xffd678);
        ControlExtents["PBR light"] = {Sun.x - 15 * Scale, Sun.y - 15 * Scale, Sun.x + 15 * Scale, Sun.y + 15 * Scale};
    }
    Ink->PopClipRect();
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::InvisibleButton("Stamp canvas", ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    ControlExtents["Stamp canvas"] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    const auto &Input = ImGui::GetIO();
    const bool Hovered = ImGui::IsItemHovered();
    if (Gesture == 8) ConstructTextComposer(BoardMinimum.x, BoardMinimum.y, BoardSize);
    const auto DocumentPoint = [&](ImVec2 Point, bool Snap = true)
    {
        PlanePoint Position{(Point.x - BoardMinimum.x) / BoardSize * CanvasUnits, (Point.y - BoardMinimum.y) / BoardSize * CanvasUnits};
        if (SnappingEnabled && Snap) { Position.X = std::round(Position.X / 16) * 16; Position.Y = std::round(Position.Y / 16) * 16; }
        return Position;
    };
    if (Hovered && Input.MouseWheel != 0 && Gesture == 0)
    {
        const float Previous = Zoom;
        Zoom = ApplyWorkspaceWheelZoom(Zoom, Input.MouseWheel);
        Pan.X += (Input.MousePos.x - Centre.x) * (1 - Zoom / Previous);
        Pan.Y += (Input.MousePos.y - Centre.y) * (1 - Zoom / Previous);
    }
    if (PbrLightDrag)
    {
        if (ImGui::IsMouseDown(0))
        {
            const auto LightPoint = DocumentPoint(Input.MousePos, false);
            const float Margin = CanvasUnits * .332f;
            PbrLightX = std::clamp(LightPoint.X, -Margin, CanvasUnits + Margin);
            PbrLightY = std::clamp(LightPoint.Y, -Margin, CanvasUnits + Margin);
        }
        else PbrLightDrag = false;
    }
    if (Hovered && Tool == VectorAuthoringTool::Select)
    {
        if (HoveredRotation || Gesture == 6) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        else if (HoveredResize[0] || HoveredResize[1]) ImGui::SetMouseCursor(!HoveredResize[0] ? ImGuiMouseCursor_ResizeNS : !HoveredResize[1] ? ImGuiMouseCursor_ResizeEW : HoveredResize[0] == HoveredResize[1] ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
    }
    if (Hovered && (GradientAxisHoverEnd >= 0 || GradientHoverSlot >= 0 || GradientLineHoverT >= 0 || FreeformHoverSlot >= 0 ||
        Gesture == 9 || Gesture == 10 || Gesture == 11 || Gesture == 15))
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    else if (Hovered && FreeformPlacePointOnCanvas)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (Hovered && (PbrLightHovered || PbrLightDrag)) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const bool PenInput = ConstructPenInteraction({Minimum.x, Minimum.y, Maximum.x, Maximum.y}, BoardExtent, Hovered);
    const bool PointInput = ConstructPointInteraction({Minimum.x, Minimum.y, Maximum.x, Maximum.y}, BoardExtent, Hovered);
    bool TextReEditEntered = false;
    if (Hovered && Gesture == 0 && Tool == VectorAuthoringTool::Select && ImGui::IsMouseDoubleClicked(0) && QuerySelectionToken)
    {
        // 📝 Double-clicking an existing text component re-enters the same on-canvas composer a
        // freshly drawn box would, seeded with its current content/bounds, instead of only being
        // editable through the Inspector.
        try
        {
            const auto &Source = History.QueryDocument();
            if (const auto Hit = QuerySelectionToken(Source, DocumentPoint(Input.MousePos, false)); Hit && *Hit)
            {
                const auto Position = std::find_if(Source.Layers.begin(), Source.Layers.end(), [&](const auto &Layer) { return Layer.Token == *Hit; });
                if (Position != Source.Layers.end() && Position->ComponentKind == VectorAuthoringComponentKind::Text &&
                    !QueryVectorAuthoringProtection(Source, *Position) && QueryVectorAuthoringVisibility(Source, *Position))
                {
                    TextEditBounds = QueryVectorAuthoringSelectionBounds(Source, std::array<uint32_t, 1>{*Hit});
                    ComponentEditToken = *Hit;
                    TextTypefaceId = Position->TextTypeface.empty() ? std::string("DMSans/DMSans-Regular") : Position->TextTypeface;
                    std::snprintf(ComponentTextBuffer.data(), ComponentTextBuffer.size(), "%s", Position->SourceMarkup.c_str());
                    Selection = {*Hit};
                    Gesture = 8; TextEditFocusPending = true;
                    TextReEditEntered = true;
                }
            }
        }
        catch (const std::exception &) {}
    }
    if (!PenInput && !PointInput && Hovered && Gesture == 0 && !TextReEditEntered && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(2)))
    {
        GestureSelection = Selection;
        PointerOrigin = DocumentPoint(Input.MousePos, false);
        if (ImGui::IsMouseClicked(2) || Tool == VectorAuthoringTool::Pan || ImGui::IsKeyDown(ImGuiKey_Space)) { Gesture = 3; PanOrigin = Pan; }
        else if (MaskSelection) { /* Mask descriptor interaction remains in the Inspector. */ }
        else if (PbrLightHovered) { PbrLightDrag = true; } // 💡 relight the preview; no document gesture starts
        else if (FreeformHoverSlot >= 0)
        {
            Gesture = 15;
            FreeformDragPoint = FreeformHoverSlot;
            SelectedFreeformPoint = FreeformHoverSlot;
            GestureSource = History.QueryDocument();
        }
        else if (FreeformPlacePointOnCanvas && Selection.size() == 1)
        {
            try
            {
                const auto &Live = History.QueryDocument();
                const auto GradientLayer = std::find_if(Live.Layers.begin(), Live.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
                if (GradientLayer != Live.Layers.end() && uint32_t(Live.Instructions[GradientLayer->Root].Transform[3]) == 3)
                {
                    const auto &Instruction = Live.Instructions[GradientLayer->Root];
                    auto Points = QueryVectorFreeformPoints(Instruction);
                    if (Points.size() < size_t(VectorFreeformPointCapacity))
                    {
                        const auto Extent = QueryVectorSelectionExtent(Live, Selection);
                        const auto LocalNorm = QueryGradientLocalNorm(PointerOrigin, Extent.Bounds, Extent.Rotation);
                        const auto Interp = QueryVectorFillInterpolation(Instruction);
                        const float Sharpness = std::clamp(Instruction.FreeformParameters[1], .25f, 8.f);
                        auto SeedColour = EvaluateVectorFreeformGradient2D(Points, LocalNorm, Sharpness, Interp);
                        SeedColour[3] = 1.f;
                        Points.push_back({{std::clamp(LocalNorm[0], -2.f, 2.f), std::clamp(LocalNorm[1], -2.f, 2.f)}, SeedColour});
                        SelectedFreeformPoint = int(Points.size()) - 1;
                        VectorAuthoringSourcePatch Patch;
                        ApplyVectorFreeformPoints(Patch, Points, Sharpness);
                        ApplySourcePatch(Patch);
                        if (Points.size() >= size_t(VectorFreeformPointCapacity))
                            FreeformPlacePointOnCanvas = false;
                    }
                }
            }
            catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        else if (GradientAxisHoverEnd >= 0)
        {
            Gesture = GradientAxisHoverEnd == 0 ? 10 : 11;
            GestureSource = History.QueryDocument();
        }
        else if (GradientHoverSlot >= 0)
        {
            Gesture = 9;
            GradientDragSlot = GradientHoverSlot;
            SelectedGradientStop = GradientHoverSlot;
            GestureSource = History.QueryDocument();
        }
        else if (GradientLineHoverT >= 0)
        {
            try
            {
                const auto &Live = History.QueryDocument();
                const auto GradientLayer = std::find_if(Live.Layers.begin(), Live.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
                if (GradientLayer != Live.Layers.end())
                {
                    auto Stops = QueryVectorGradientStops(Live.Instructions[GradientLayer->Root]);
                    const auto Interp = QueryVectorFillInterpolation(Live.Instructions[GradientLayer->Root]);
                    const auto Colour = EvaluateVectorGradientColour(Stops, GradientLineHoverT, Interp);
                    Stops.push_back({std::clamp(GradientLineHoverT, 0.f, 1.f), Colour});
                    std::sort(Stops.begin(), Stops.end(), [](const auto &A, const auto &B) { return A.Position < B.Position; });
                    const auto Inserted = std::find_if(Stops.begin(), Stops.end(), [&](const auto &Stop) { return Stop.Position == std::clamp(GradientLineHoverT, 0.f, 1.f); });
                    SelectedGradientStop = int(Inserted - Stops.begin());
                    VectorAuthoringSourcePatch Patch; ApplyVectorGradientStops(Patch, Stops);
                    ApplySourcePatch(Patch);
                }
            }
            catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        else if (HoveredRotation || HoveredResize[0] || HoveredResize[1])
        {
            Gesture = HoveredRotation ? 6 : 5;
            ResizeDirection = HoveredResize;
            GestureSource = History.QueryDocument();
        }
        else if (Tool != VectorAuthoringTool::Select)
        {
            Gesture = 2;
            GestureSource = History.QueryDocument();
            GestureDraft.reset();
            GesturePrimitiveToken = 0;
            TextBoxCurrent = PointerOrigin; // Seeds the Text tool's drawn box so a plain click (no drag) still has a point to measure from.
        }
        else
        {
            SelectionExtend = Input.KeyShift;
            GestureSource = History.QueryDocument();
            SelectionPointer = PointerOrigin; SelectionTravel = 0; SelectionReleased = false;
            Gesture = 7;
        }
    }
    if (Gesture == 7 && (ImGui::IsKeyPressed(ImGuiKey_Escape) || Input.AppFocusLost || !ImGui::IsMousePosValid()))
    {
        Selection = GestureSelection; GestureSource.reset(); Gesture = 0;
    }
    if (Gesture == 7)
    {
        if (!SelectionReleased)
        {
            SelectionPointer = DocumentPoint(Input.MousePos, false);
            SelectionTravel = std::max(SelectionTravel, std::hypot(SelectionPointer.X - PointerOrigin.X, SelectionPointer.Y - PointerOrigin.Y) * BoardSize / CanvasUnits);
            SelectionReleased = !ImGui::IsMouseDown(0);
        }
        try
        {
            if (!GestureSource || History.QueryDocument() != *GestureSource)
                throw std::runtime_error("Source changed while GPU selection was pending; click again.");
            if (!QuerySelectionToken) throw std::runtime_error("GPU contour selection is unavailable; use the layer list.");
            if (const auto Hit = QuerySelectionToken(*GestureSource, PointerOrigin))
            {
                if (*Hit)
                {
                    const auto Position = std::find_if(GestureSource->Layers.begin(), GestureSource->Layers.end(), [&](const auto &Layer) { return Layer.Token == *Hit; });
                    if (Position == GestureSource->Layers.end() || QueryVectorAuthoringProtection(*GestureSource, *Position) || !QueryVectorAuthoringVisibility(*GestureSource, *Position))
                        throw std::runtime_error("GPU selection token no longer authenticates to an editable source.");
                    if (SelectionExtend || std::find(Selection.begin(), Selection.end(), *Hit) == Selection.end()) Select(*Hit, SelectionExtend);
                    Gesture = Selection.empty() ? 0 : 1;
                    if (Selection.empty()) GestureSource.reset();
                }
                else
                {
                    MarqueeAdditive = SelectionExtend;
                    Gesture = 4;
                    if (!MarqueeAdditive) Selection.clear();
                }
                Diagnostic.clear();
            }
        }
        catch (const std::exception &Failure)
        {
            Diagnostic = Failure.what(); GestureDraft.reset(); GestureSource.reset(); Gesture = 0; ++GestureRevision;
        }
    }
    if (Gesture == 3 && (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(2)))
    {
        const auto Δ = EvaluateNavigationDelta ? EvaluateNavigationDelta({Input.MouseDelta.x, Input.MouseDelta.y}) : PlanePoint{Input.MouseDelta.x, Input.MouseDelta.y};
        Pan.X += Δ.X; Pan.Y += Δ.Y;
    }
    if ((Gesture == 1 || Gesture == 2 || Gesture == 5 || Gesture == 6 || Gesture == 9 || Gesture == 10 || Gesture == 11 || Gesture == 15) &&
        (ImGui::IsMouseDragging(0, 2) || (Gesture == 1 && SelectionReleased && SelectionTravel >= 2 * Scale)) && GestureSource)
    {
        auto Position = DocumentPoint(Input.MousePos, Gesture != 6 && Gesture != 15);
        if (Gesture == 1 && SelectionReleased)
        {
            Position = SelectionPointer;
            if (SnappingEnabled) { Position.X = std::round(Position.X / 16) * 16; Position.Y = std::round(Position.Y / 16) * 16; }
        }
        try
        {
            if (Gesture == 2 && Tool == VectorAuthoringTool::Text)
            {
                TextBoxCurrent = Position;
            }
            else if (Gesture == 2)
            {
                const auto Ink = OppositeCanvasColour();
                VectorCoverageInstruction Source;
                Source.Topology[0] = Tool == VectorAuthoringTool::Rectangle ? 0u : Tool == VectorAuthoringTool::Ellipse ? 1u : uint32_t(Tool) - uint32_t(VectorAuthoringTool::Diamond) + 2;
                if (Input.KeyCtrl || Input.KeySuper)
                    Source.Bounds = {PointerOrigin.X, PointerOrigin.Y,
                        std::max(1.f, 2.f * std::abs(Position.X - PointerOrigin.X)), std::max(1.f, 2.f * std::abs(Position.Y - PointerOrigin.Y))};
                else
                    Source.Bounds = {(Position.X + PointerOrigin.X) * .5f, (Position.Y + PointerOrigin.Y) * .5f,
                        std::max(1.f, std::abs(Position.X - PointerOrigin.X)), std::max(1.f, std::abs(Position.Y - PointerOrigin.Y))};
                Source.Colour = Ink;
                Source.StrokeColour = Ink;
                Source.Topology[3] |= VectorCoverageStrokeColourExplicit;
                if (Source.Topology[0] == 4) Source.Paint[2] = 5;
                else if (Source.Topology[0] == 5) { Source.Paint[2] = 5; Source.Paint[1] = .5f; }
                if (!GestureDraft)
                {
                    auto Draft = *GestureSource;
                    GesturePrimitiveToken = ConstructVectorAuthoringPrimitive(Draft, Source, ShapeNames[Source.Topology[0]]);
                    GestureDraft = std::move(Draft);
                    Selection = {GesturePrimitiveToken};
                }
                else
                {
                    auto &Draft = *GestureDraft;
                    const auto Layer = std::find_if(Draft.Layers.begin(), Draft.Layers.end(), [&](const auto &Candidate)
                        { return Candidate.Token == GesturePrimitiveToken; });
                    if (Layer == Draft.Layers.end()) throw std::runtime_error("The in-progress shape lost its authenticated layer token.");
                    Draft.Instructions.at(Layer->Root).Bounds = Source.Bounds;
                }
                ++GestureRevision;
            }
            else if (Gesture == 15)
            {
                auto Draft = *GestureSource;
                const auto GradientLayer = std::find_if(Draft.Layers.begin(), Draft.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
                if (GradientLayer == Draft.Layers.end()) throw std::runtime_error("Freeform gradient selection lost its authenticated layer token.");
                const auto &GradientSource = Draft.Instructions.at(GradientLayer->Root);
                const auto Extent = QueryVectorSelectionExtent(Draft, Selection);
                const auto LocalNorm = QueryGradientLocalNorm(Position, Extent.Bounds, Extent.Rotation);
                auto Points = QueryVectorFreeformPoints(GradientSource);
                if (FreeformDragPoint < 0 || FreeformDragPoint >= int(Points.size())) throw std::runtime_error("Freeform point drag lost its slot.");
                Points[size_t(FreeformDragPoint)].Position = {std::clamp(LocalNorm[0], -2.f, 2.f), std::clamp(LocalNorm[1], -2.f, 2.f)};
                VectorAuthoringSourcePatch Patch;
                ApplyVectorFreeformPoints(Patch, Points, std::clamp(GradientSource.FreeformParameters[1], .25f, 8.f));
                TransformVectorAuthoringSelection(Draft, Selection, Patch);
                GestureDraft = std::move(Draft);
                ++GestureRevision;
            }
            else if (Gesture == 9)
            {
                // 📝 Reprojects the live mouse position onto the gradient's own axis (recomputed from
                // GestureSource so a drag mid-flight always measures against the pre-drag shape, exactly
                // like Gesture 1/5/6 above) to get a Fraction, then clamps it strictly between its two
                // immediate neighbours (plus a small gap) so dragging a marker can never cross another
                // one and silently reorder the packed stop slots underneath the user.
                auto Draft = *GestureSource;
                const auto GradientLayer = std::find_if(Draft.Layers.begin(), Draft.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
                if (GradientLayer == Draft.Layers.end()) throw std::runtime_error("Gradient selection lost its authenticated layer token.");
                const auto &GradientSource = Draft.Instructions.at(GradientLayer->Root);
                const auto Extent = QueryVectorSelectionExtent(Draft, Selection);
                const auto Axis = QueryGradientAxisEndpoints(GradientSource, Extent.Bounds, Extent.Rotation);
                const float Along = std::clamp(ProjectAlongSegment(Position, Axis[0], Axis[1]), 0.f, 1.f);
                auto Stops = QueryVectorGradientStops(GradientSource);
                if (GradientDragSlot < 0 || GradientDragSlot >= int(Stops.size())) throw std::runtime_error("Gradient stop drag lost its slot.");
                constexpr float Gap = .01f;
                const float LowerBound = GradientDragSlot > 0 ? Stops[GradientDragSlot - 1].Position + Gap : 0.f;
                const float UpperBound = GradientDragSlot + 1 < int(Stops.size()) ? Stops[GradientDragSlot + 1].Position - Gap : 1.f;
                Stops[GradientDragSlot].Position = std::clamp(Along, std::min(LowerBound, UpperBound), std::max(LowerBound, UpperBound));
                VectorAuthoringSourcePatch Patch; Patch.GradientStopPositions = {Stops.size() > 0 ? Stops[0].Position : 0.f, Stops.size() > 1 ? Stops[1].Position : 1.f,
                    Stops.size() > 2 ? Stops[2].Position : -1.f, Stops.size() > 3 ? Stops[3].Position : -1.f};
                TransformVectorAuthoringSelection(Draft, Selection, Patch);
                GestureDraft = std::move(Draft);
                ++GestureRevision;
            }
            else if (Gesture == 10 || Gesture == 11)
            {
                // 📝 Repositions the ramp's own A (Gesture 10) or B (Gesture 11) control point to wherever
                // the mouse is now, in the shape's own LocalNorm frame (QueryGradientLocalNorm, the exact
                // inverse of the marker-drawing math) — re-anchored from the frozen pre-drag GestureSource,
                // same convention as every other drag gesture here. Per-stop Fractions are left untouched,
                // so the ramp itself doesn't visually jump; only where its line/radius sits does.
                auto Draft = *GestureSource;
                const auto GradientLayer = std::find_if(Draft.Layers.begin(), Draft.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
                if (GradientLayer == Draft.Layers.end()) throw std::runtime_error("Gradient selection lost its authenticated layer token.");
                const auto &GradientSource = Draft.Instructions.at(GradientLayer->Root);
                const auto Extent = QueryVectorSelectionExtent(Draft, Selection);
                const auto LocalNorm = QueryGradientLocalNorm(Position, Extent.Bounds, Extent.Rotation);
                auto Axis = GradientSource.GradientAxis;
                if (Gesture == 10) { Axis[0] = LocalNorm[0]; Axis[1] = LocalNorm[1]; }
                else { Axis[2] = LocalNorm[0]; Axis[3] = LocalNorm[1]; }
                VectorAuthoringSourcePatch Patch; Patch.GradientAxis = Axis;
                TransformVectorAuthoringSelection(Draft, Selection, Patch);
                GestureDraft = std::move(Draft);
                ++GestureRevision;
            }
            else
            {
                auto Draft = *GestureSource;
                if (Gesture == 1)
                {
                    VectorAuthoringSourcePatch Patch; Patch.TranslationX = Position.X - PointerOrigin.X; Patch.TranslationY = Position.Y - PointerOrigin.Y;
                    TransformVectorAuthoringSelection(Draft, Selection, Patch);
                }
                else
                {
                    const auto Patch = Gesture == 5
                        ? EvaluateVectorSelectionResize(Draft, Selection, ResizeDirection, {Position.X, Position.Y}, Input.KeyShift, Input.KeyAlt)
                        : EvaluateVectorSelectionRotation(Draft, Selection, {PointerOrigin.X, PointerOrigin.Y}, {Position.X, Position.Y}, Input.KeyShift || SnappingEnabled);
                    TransformVectorAuthoringSelection(Draft, Selection, Patch);
                }
                GestureDraft = std::move(Draft);
                ++GestureRevision;
            }
            Diagnostic.clear();
        }
        catch (const std::exception &Failure) { GestureDraft.reset(); GesturePrimitiveToken = 0; ++GestureRevision; Diagnostic = Failure.what(); }
    }
    if ((ImGui::IsKeyPressed(ImGuiKey_Escape) || Input.AppFocusLost || !ImGui::IsMousePosValid()) && Gesture)
    {
        Selection = GestureSelection; GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    }
    else if (Gesture == 2 && Tool == VectorAuthoringTool::Text && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2))
    {
        // 📝 The box is drawn; hand off to the on-canvas composer (Gesture 8) instead of the generic
        // shape-finalize path below, which only knows how to finalize a GestureDraft primitive.
        constexpr float MinimumBoxExtent = 8.f, DefaultBoxWidth = 220.f, DefaultBoxHeight = 64.f;
        const float Width = std::abs(TextBoxCurrent.X - PointerOrigin.X), Height = std::abs(TextBoxCurrent.Y - PointerOrigin.Y);
        if (Width < MinimumBoxExtent && Height < MinimumBoxExtent)
            TextEditBounds = {PointerOrigin.X, PointerOrigin.Y, DefaultBoxWidth, DefaultBoxHeight};
        else
            TextEditBounds = {(PointerOrigin.X + TextBoxCurrent.X) * .5f, (PointerOrigin.Y + TextBoxCurrent.Y) * .5f,
                std::max(MinimumBoxExtent, Width), std::max(MinimumBoxExtent, Height)};
        ComponentEditToken = 0;
        ComponentTextBuffer[0] = 0;
        GestureSource.reset(); Gesture = 8; TextEditFocusPending = true;
    }
    // 📝 Gesture 7 (marquee) and Gesture 8 (on-canvas text composer) both legitimately persist across
    // many idle-mouse frames after the mouse that opened them is released — Gesture 8 especially, since
    // the whole point is to keep the InputTextMultiline mounted and focused while the user types with the
    // mouse sitting untouched. Excluding only 7 here let this catch-all drag-release cleanup fire on the
    // very next frame after the Text tool handed off to the composer, snapping Gesture back to 0 (with no
    // GestureDraft to finalize) before a single character could be typed — the composer box vanished
    // instantly, nothing reached the layer stack, exactly the reported "Text tool is broken" symptom.
    else if (Gesture && Gesture != 7 && Gesture != 8 && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2))
    {
        if (Gesture == 4 && GestureSource)
        {
            const auto Position = SelectionReleased ? SelectionPointer : DocumentPoint(Input.MousePos, false);
            if (std::hypot(Position.X - PointerOrigin.X, Position.Y - PointerOrigin.Y) * BoardSize / CanvasUnits >= 3 * Scale)
                Selection = QueryVectorSelectionRegion(*GestureSource, {PointerOrigin.X, PointerOrigin.Y}, {Position.X, Position.Y}, MarqueeAdditive ? std::span<const uint32_t>(GestureSelection) : std::span<const uint32_t>{});
        }
        if (GestureDraft) { auto Document = std::move(*GestureDraft); GestureDraft.reset(); Finalize(std::move(Document), Selection); }
        GestureSource.reset(); Gesture = 0;
    }
    Ink->AddRectFilled(ImVec2(At(24, 0).x, Maximum.y), ImVec2(At(Width / Scale - 24, 0).x, Maximum.y + 33 * Scale), ImGui::GetColorU32(Spectrum(0x191c16)), 6 * Scale);
    Caption(ImVec2(At(35, 0).x, Maximum.y + 11 * Scale), "Marquee to select · Shift constrain · Alt centred resize · Ctrl centre draw · Space pan", 8 * Scale, 0x909a7c);
    Caption(ImVec2(At(Width / Scale - 114, 0).x, Maximum.y + 11 * Scale), (std::to_string(Selection.size()) + " selected").c_str(), 8 * Scale, 0xc6d2b5);
    // 📝 The standalone "Decal preview on a plain surface" panel that used to live here was removed —
    // it duplicated the main board above with no extra information. Content height is exactly the
    // dock's available height (see Maximum), so the panel never scrolls: canvas + hint bar always fit.
    ImGui::SetCursorScreenPos(ImVec2(At(Width / Scale - 24, 0).x, ContentOrigin.y + AvailableHeight - 1));
    ImGui::Dummy(ImVec2(1, 1));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SELECTION-AWARE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructInspector()
{
    CapturePanel("Inspector");
    const auto Position = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(Position.x + 18 * Scale, Position.y + 30 * Scale));
    ImGui::BeginChild("Stamp inspector scroll", ImVec2(ImGui::GetContentRegionAvail().x - 18 * Scale, 0));
    if (PenSource)
    {
        Controls.BeginCard(PolylineSession ? "Pending polyline path" : "Pending pen path", true);
        ImGui::TextWrapped(PolylineSession
            ? "Click to place straight-corner anchors. Ctrl-click a retained endpoint to start a separate path. While continuing a retained path, click another endpoint to join; Ctrl bypasses joining. Hold Space during placement to reposition the new anchor. Click the first anchor to close it into a filled shape. Enter finishes it open as a plain line; Escape cancels; Backspace removes the last anchor."
            : "Click to place anchors; drag tangents. Ctrl-click a retained endpoint to start a separate path. While continuing a retained path, click another endpoint to join; Ctrl bypasses joining. Joins require adjacent matching opaque strokes, matching group/channels and no owned masks. Hold Space during placement to reposition the new anchor. Alt-click the last anchor to break its outgoing tangent (manual curves). Click the first anchor to close. Enter finishes open; Escape cancels; Backspace removes the last anchor.");
        if (Controls.Button("Finish open path", 200 * Scale)) FinishPen(false);
        if (Controls.Button(PolylineSession ? "Cancel polyline path" : "Cancel pen path", 200 * Scale)) CancelPen();
        Controls.EndCard();
        ImGui::EndChild();
        return;
    }
    const auto Snapshot = QueryDocument();
    const float CanvasUnits = std::clamp(Snapshot.CanvasExtent, 64.f, 4096.f);
    const auto SelectedTokens = Selection;
    const auto Selected = [&](const auto &Layer) { return std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end(); };
    const auto First = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), Selected);
    const bool Protected = std::any_of(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Selected(Layer) && QueryVectorAuthoringProtection(Snapshot, Layer); });
    if (First == Snapshot.Layers.end())
    {
        Controls.BeginCard("Decal canvas", true);
        float EditableCanvasUnits = CanvasUnits;
        if (Controls.Number("Canvas size (units)", EditableCanvasUnits, 64.f, 4096.f))
            ApplyCanvasExtent(EditableCanvasUnits);
        Controls.Toggle("Scale components on resize", ScaleComponentsOnCanvasResize);
        Controls.Colour("Canvas colour", CanvasGroundColour);
        Controls.Toggle("Transparent canvas", TransparentCanvas);
        {
            const auto AutoInk = OppositeCanvasColour();
            ImGui::TextDisabled("Default new component colour:");
            ImGui::SameLine();
            const ImVec2 SwatchMin = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(SwatchMin, ImVec2(SwatchMin.x + 18 * Scale, SwatchMin.y + 14 * Scale),
                ImGui::GetColorU32(ImVec4(AutoInk[0], AutoInk[1], AutoInk[2], 1.f)), 3 * Scale);
            ImGui::GetWindowDrawList()->AddRect(SwatchMin, ImVec2(SwatchMin.x + 18 * Scale, SwatchMin.y + 14 * Scale),
                IM_COL32(255, 255, 255, 90), 3 * Scale);
            ImGui::Dummy(ImVec2(20 * Scale, 14 * Scale));
        }
        Controls.EndCard();
        Controls.BeginCard("Your next component", true);
        ImGui::TextWrapped("Pick a tool and draw a shape, or add Text/Vector/Image with the + beside Components.");
        Controls.EndCard();
        ImGui::EndChild();
        return;
    }
    if (!MaskSelection)
    {
        Controls.BeginCard(Selection.size() == 1 ? "Selected component" : "Selected components");
        ImGui::BeginDisabled(Protected);
        if (Selection.size() >= 2)
        {
            bool Eligible = !Protected && std::all_of(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return !Selected(Layer) || !Layer.Mask; });
            if (const auto Operation = ConstructVectorBooleanControls(Eligible))
            {
                try { auto Document = Snapshot; const auto Token = IntegrateVectorAuthoringBoolean(Document, Selection, VectorCoverageCategory(*Operation)); Finalize(std::move(Document), {Token}); }
                catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
            }
        }
        ImGui::Spacing();
        if (Controls.Button("Duplicate selection", 30 * Scale, false, CopyPath)) ReplicateSelection();
        Controls.Tooltip("Duplicate", "Retain editable source, mask and channel settings in an independent copy.");
        ImGui::SameLine(0, 6 * Scale);
        if (Controls.Button("Delete selection", 30 * Scale, false, TrashPath)) EvaluateEdit([&](auto &Document) { PartitionVectorAuthoringSelection(Document, Selection); });
        ImGui::EndDisabled();
        Controls.EndCard();
        if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
        // 🧭 Transform: identity, position/alignment/size — everything not related to PBR, in
        // its own card above the surface channels (the Alloy inspector separation).
        Controls.BeginCard("Transform", true);
        {
            bool Locked = First->Locked;
            if (Controls.Toggle("Locked", Locked)) EvaluateEdit([&](auto &Document)
            {
                for (auto &Layer : Document.Layers)
                    if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end()) Layer.Locked = Locked;
            });
        }
        ImGui::BeginDisabled(Protected);
        char LayerName[81]; std::snprintf(LayerName, sizeof(LayerName), "%s", First->Name.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Layer name", LayerName, sizeof(LayerName))) EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == First->Token) Layer.Name = LayerName; });
        const auto Bounds = QueryVectorAuthoringSelectionBounds(Snapshot, Selection);
        if (ImGui::BeginTable("Transform fields", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
        ImGui::TableSetupColumn("Horizontal", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("Vertical", ImGuiTableColumnFlags_WidthStretch, 1);
        float RowCursorY = 0.f;
        for (size_t Index = 0; Index < 4; ++Index)
        {
            if (Index % 2 == 0) ImGui::TableNextRow(ImGuiTableRowFlags_None, 60 * Scale);
            ImGui::TableNextColumn();
            if (Index % 2 == 0) RowCursorY = ImGui::GetCursorPosY();
            else ImGui::SetCursorPosY(RowCursorY);
            float Quantity = Bounds[Index];
            const char *Captions[]{"X", "Y", "Width", "Height"};
            if (Controls.Number(Captions[Index], Quantity, Index < 2 ? -CanvasUnits : 1.f, CanvasUnits * 2.f))
            {
                VectorAuthoringSourcePatch Patch;
                if (Index == 0) Patch.TranslationX = Quantity - Bounds[0];
                if (Index == 1) Patch.TranslationY = Quantity - Bounds[1];
                if (Index == 2) Patch.ScaleX = Quantity / std::max(.01f, Bounds[2]);
                if (Index == 3) Patch.ScaleY = Quantity / std::max(.01f, Bounds[3]);
                ApplySourcePatch(Patch);
            }
        }
        ImGui::EndTable();
        }
        const auto &Source = Snapshot.Instructions[First->Root];
        float Rotation = Source.Topology[0] <= 6 ? Source.Transform[0] * 180 / std::numbers::pi_v<float> : 0;
        if (Controls.Number("Rotation", Rotation, -360, 360)) { VectorAuthoringSourcePatch Patch; Patch.Rotation = Rotation * std::numbers::pi_v<float> / 180 - Source.Transform[0]; ApplySourcePatch(Patch); }
        float Opacity = Source.Topology[0] <= 6 ? Source.Transform[2] : First->ExpressionOpacity;
        if (Controls.Number("Opacity", Opacity, 0, 1)) EvaluateEdit([&](auto &Document)
        {
            std::vector<uint32_t> Primitives;
            for (auto &Layer : Document.Layers)
            {
                if (std::find(Selection.begin(), Selection.end(), Layer.Token) == Selection.end()) continue;
                if (Document.Instructions[Layer.Root].Topology[0] <= 6) Primitives.push_back(Layer.Token);
                else Layer.ExpressionOpacity = Opacity;
            }
            if (!Primitives.empty())
            {
                VectorAuthoringSourcePatch Patch; Patch.Opacity = Opacity;
                TransformVectorAuthoringSelection(Document, Primitives, Patch);
            }
        });
        ImGui::TextUnformatted(Selection.size() == 1 ? "Align to canvas" : "Align selection");
        for (int Axis = 0; Axis < 2; ++Axis)
        {
            for (int Boundary = 0; Boundary < 3; ++Boundary)
            {
                if (Boundary) ImGui::SameLine(0, 6 * Scale);
                const char *Captions[]{"Align left", "Centre horizontally", "Align right", "Align top", "Centre vertically", "Align bottom"};
                if (Controls.Button(Captions[Axis * 3 + Boundary], 30 * Scale, false, Axis == 0 ? "M3 3V21M7 7H16V17H7Z" : "M3 3H21M7 7H17V16H7Z"))
                {
                    const auto Bounds = QueryVectorAuthoringSelectionBounds(Snapshot, Selection);
                    const float Centre = Axis ? Bounds[1] : Bounds[0], Extent = Axis ? Bounds[3] : Bounds[2];
                    const float Destination = Boundary == 0 ? Extent * .5f : Boundary == 1 ? CanvasUnits * .5f : CanvasUnits - Extent * .5f;
                    VectorAuthoringSourcePatch Patch; if (Axis) Patch.TranslationY = Destination - Centre; else Patch.TranslationX = Destination - Centre;
                    ApplySourcePatch(Patch);
                }
            }
        }
        ImGui::EndDisabled();
        Controls.EndCard();
        if (Source.Topology[0] == 0 || Source.Topology[0] == 4 || Source.Topology[0] == 5)
        {
            Controls.BeginCard("Shape geometry", false, true);
            ImGui::BeginDisabled(Protected);
        if (Source.Topology[0] == 0)
        {
            float Radius = Source.ShapeParameters[0];
            if (Controls.Number("Corner radius", Radius, 0, 50))
            { VectorAuthoringSourcePatch Patch; Patch.CornerRadius = Radius; ApplySourcePatch(Patch); }
        }
        // 📝 Polygon/Star stay this parametric corner-count category only until a corner is first dragged
        // with the Points tool - that one-time edit bakes it into a plain cubic path (see CubicPathEditing's
        // ReplaceCubicPath), after which it is an ordinary freeform shape and this field naturally disappears,
        // exactly like "outlining" a parametric shape in other vector editors.
        if (Source.Topology[0] == 4 || Source.Topology[0] == 5)
        {
            float Sides = std::clamp(Source.Paint[2], 3.f, 35.f);
            if (Controls.Range(Source.Topology[0] == 4 ? "Sides" : "Points", Sides, 3, 35))
            {
                VectorAuthoringSourcePatch Patch; Patch.PolygonSides = uint32_t(std::lround(Sides)); ApplySourcePatch(Patch);
            }
        }
        if (Source.Topology[0] == 5)
        {
            float InnerRadius = std::clamp(Source.Paint[1], .05f, .95f) * 100;
            if (Controls.Range("Inner radius %", InnerRadius, 5, 95))
            {
                VectorAuthoringSourcePatch Patch; Patch.StarInnerRadius = std::clamp(InnerRadius / 100.f, .05f, .95f); ApplySourcePatch(Patch);
            }
        }
            ImGui::EndDisabled();
            Controls.EndCard();
        }
    if (!MaskSelection && Selection.size() == 1 && First->ComponentKind != VectorAuthoringComponentKind::Freeform)
    {
        const bool IsText = First->ComponentKind == VectorAuthoringComponentKind::Text;
        const bool IsVector = First->ComponentKind == VectorAuthoringComponentKind::ImportedVector;
        Controls.BeginCard(IsText ? "Text content" : IsVector ? "Imported vector" : "Imported image", true);
        ImGui::BeginDisabled(Protected);
        if (IsText)
        {
            // 📝 Text properties live inline here (content + font size), not behind a modal: editing
            // either commits (rebakes the real glyph outline, anchor held at its current centre) the
            // moment you leave the field, via IsItemDeactivatedAfterEdit — not on every keystroke.
            if (InspectorTextToken != First->Token)
            {
                std::snprintf(InspectorTextBuffer.data(), InspectorTextBuffer.size(), "%s", First->SourceMarkup.c_str());
                InspectorTextToken = First->Token;
                InspectorFontListOpen = false;
            }
            ImGui::TextDisabled("Content");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextMultiline("##StampTextInspectorContent", InspectorTextBuffer.data(), InspectorTextBuffer.size(), ImVec2(0, 56 * Scale));
            const bool ContentCommit = ImGui::IsItemDeactivatedAfterEdit();
            float PointSize = First->TextPointSize;
            Controls.Number("Font size", PointSize, 4, 512);
            const bool SizeCommit = ImGui::IsItemDeactivatedAfterEdit();
            if (ContentCommit || SizeCommit) RebakeTextLayer(First->Token, InspectorTextBuffer.data(), PointSize);
            // 🅰 Font: every outline face in EngineContent/FontArchives, each row previewed in its
            // own glyphs; picking one re-bakes this component's outline in it (and the next text
            // you compose on the canvas starts in it too).
            const std::string LayerFace = First->TextTypeface.empty() ? std::string("DMSans/DMSans-Regular") : First->TextTypeface;
            const auto *Face = FindTypefaceChoice(ContentRoot, LayerFace);
            ImGui::TextDisabled("Font");
            if (Controls.Button(Face ? Face->DisplayName.c_str() : "DM Sans Regular", ImGui::GetContentRegionAvail().x, InspectorFontListOpen))
                InspectorFontListOpen = !InspectorFontListOpen;
            if (InspectorFontListOpen)
            {
                std::string Picked;
                ImGui::Dummy(ImVec2(1, 6 * Scale));
                if (ConstructFontList(LayerFace, Picked, ImGui::GetContentRegionAvail().x))
                {
                    InspectorFontListOpen = false;
                    TextTypefaceId = Picked;
                    RebakeTextLayer(First->Token, InspectorTextBuffer.data(), PointSize, 0.f, Picked);
                }
            }
            if (Controls.Button("Edit on canvas...", 160 * Scale))
            {
                TextEditBounds = QueryVectorAuthoringSelectionBounds(Snapshot, std::array<uint32_t, 1>{First->Token});
                ComponentEditToken = First->Token;
                TextTypefaceId = LayerFace;
                std::snprintf(ComponentTextBuffer.data(), ComponentTextBuffer.size(), "%s", First->SourceMarkup.c_str());
                Gesture = 8; TextEditFocusPending = true;
            }
        }
        else if (IsVector)
        {
            ImGui::Text("%zu bytes of SVG markup", First->SourceMarkup.size());
            if (Controls.Button("Replace SVG...", 160 * Scale))
            {
                ComponentEditToken = First->Token;
                ComponentSvgBuffer = First->SourceMarkup;
                ImGui::OpenPopup("Add stamp vector");
            }
            ImGui::TextWrapped("Linear and radial SVG gradients, strokes, and group opacity are imported natively; clip-paths and masks are skipped.");
        }
        else
        {
            ImGui::Text("%zu bytes embedded (PNG data URL)", First->EmbeddedImage.size());
            ImGui::TextWrapped("Pixel sampling is not implemented yet; this renders as a placeholder box.");
            if (Controls.Button("Replace image...", 160 * Scale))
            {
                ComponentEditToken = First->Token;
                ComponentImageBuffer = First->EmbeddedImage;
                ImGui::OpenPopup("Add stamp image");
            }
        }
        ImGui::EndDisabled();
        Controls.EndCard();
        ConstructVectorPopup();
        ConstructImagePopup();
        if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
    }
    if (!MaskSelection && Selection.size() == 1 && Tool == VectorAuthoringTool::Points)
    {
        Controls.BeginCard("Path points", true);
        const auto Category = Snapshot.Instructions[First->Root].Topology[0];
        ImGui::BeginDisabled(Protected || Gesture != 0);
        if (Category < 6)
        {
            ImGui::TextWrapped("Convert this primitive to an editable cubic path. Ellipses use a four-curve approximation; Polygon/Star keep their own corner count.");
            if (Controls.Button("Convert to cubic path", 200 * Scale))
                EvaluateEdit([&](auto &Document) { ReplaceCubicPath(Document, First->Token, DerivePrimitiveCubicPath(Document, First->Token)); });
        }
        else if (Category == 6)
        {
            ImGui::TextWrapped("Shift-click anchors to select or deselect several. Drag or nudge selected anchors together. Automatic tangents use neighbours; Delete removes selected anchors. Escape cancels a drag.");
            ImGui::Text("%zu anchors selected", SelectedAnchors.size());
            if (Controls.Button("Select all anchors", 200 * Scale))
            {
                try
                {
                    const auto Path = DecodeCubicPath(Snapshot, First->Token);
                    PointToken = First->Token; SelectedAnchors.clear();
                    for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) SelectedAnchors.push_back(Index);
                    ActiveAnchor = SelectedAnchors.back();
                }
                catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
            }
            ImGui::BeginDisabled(PointToken != First->Token || SelectedAnchors.empty());
            if (Controls.Button("Automatic selected tangents", 220 * Scale))
                EvaluateEdit([&](auto &Document)
                {
                    auto Path = DecodeCubicPath(Document, First->Token);
                    ConfigureAutomaticCubicTangents(Path, SelectedAnchors);
                    ReplaceCubicPath(Document, First->Token, Path);
                });
            Controls.Tooltip("Automatic selected tangents", "Derive aligned tangents from neighbouring anchor positions. Lengths use one third of each adjacent chord. Only selected anchors change; one undo restores the source.");
            for (bool Smooth : {false, true})
                if (Controls.Button(SelectedAnchors.size() > 1
                    ? (Smooth ? "Smooth selected anchors" : "Corner selected anchors")
                    : (Smooth ? "Smooth active anchor" : "Corner active anchor"), 200 * Scale))
                    EvaluateEdit([&](auto &Document)
                    {
                        auto Path = DecodeCubicPath(Document, First->Token);
                        for (size_t Index : SelectedAnchors) ConfigureCubicTangent(Path, Index, Smooth);
                        ReplaceCubicPath(Document, First->Token, Path);
                    });
            bool SegmentEligible = false;
            try
            {
                const auto Path = DecodeCubicPath(Snapshot, First->Token);
                SegmentEligible = ActiveAnchor < Path.Anchors.size() && Path.Anchors.size() < 400
                    && (Path.Closed || ActiveAnchor + 1 < Path.Anchors.size());
            }
            catch (const std::exception &) {}
            ImGui::BeginDisabled(!SegmentEligible);
            if (Controls.Button("Subdivide following segment", 220 * Scale))
                EvaluateEdit([&](auto &Document)
                {
                    auto Path = DecodeCubicPath(Document, First->Token);
                    SubdivideCubicSegment(Path, ActiveAnchor);
                    ReplaceCubicPath(Document, First->Token, Path);
                    ++ActiveAnchor; SelectedAnchors = {ActiveAnchor};
                });
            Controls.Tooltip("Subdivide segment", "Insert an anchor halfway along the following cubic using de Casteljau subdivision. The curve is preserved; one undo restores the source.");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            bool ResumeEligible = false;
            try
            {
                const auto Path = DecodeCubicPath(Snapshot, First->Token);
                ResumeEligible = !Path.Closed && Path.Anchors.size() < 400;
            }
            catch (const std::exception &) {}
            ImGui::BeginDisabled(!ResumeEligible);
            for (bool FromStart : {true, false})
                if (Controls.Button(FromStart ? "Resume from start" : "Resume from end", 200 * Scale))
                {
                    try { ResumePen(First->Token, FromStart); }
                    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
                }
            ImGui::EndDisabled();
        }
        else ImGui::TextWrapped("Select an independent source path; aggregate expressions are not flattened for point editing.");
        ImGui::EndDisabled();
        Controls.EndCard();
        if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
    }
    if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
        // 🎛️ Surface channels, in the frame the Texture Paint channel targets card uses:
        // removable chips for the enabled channels and a grouped checklist behind the +
        // toggle — unchecking (or a chip's ×) returns a channel to the dropdown — then one
        // card per enabled channel. The component's colour-only paint (fill/stroke sources,
        // gradients included) lives on the Base color card. Display state comes from the
        // first selected component; edits apply to every selected component, exactly as
        // the old Surface channels expander behaved.
        struct StampChannelMeta { size_t Index; const char *Label; const char *StrengthCaption; const char *Group; uint32_t Colour; float Maximum; };
        static constexpr StampChannelMeta StampChannels[]{
            {size_t(VectorAuthoringSurfaceChannel::Colour), "Base color", "Colour strength", "Surface", 0xb87333, 1.f},
            {size_t(VectorAuthoringSurfaceChannel::Metalness), "Metalness", "Metalness strength", "Surface", 0x8b5cf6, 1.f},
            {size_t(VectorAuthoringSurfaceChannel::Roughness), "Roughness", "Roughness strength", "Surface", 0x3b82f6, 1.f},
            {size_t(VectorAuthoringSurfaceChannel::Height), "Height", "Height strength", "Surface", 0x8a8a8a, 1.f},
            {size_t(VectorAuthoringSurfaceChannel::Normal), "Normal", "Normal strength", "Surface", 0x10b981, 4.f},
            {size_t(VectorAuthoringSurfaceChannel::Opacity), "Opacity", "Opacity strength", "Surface", 0x94a3b8, 1.f},
            {size_t(VectorAuthoringSurfaceChannel::Occlusion), "Ambient occlusion", "Ambient occlusion strength", "Radiance", 0x6b7280, 1.f},
            {size_t(VectorAuthoringSurfaceChannel::Clearcoat), "Clearcoat", "Clearcoat strength", "Reflectance", 0xe2e8f0, 1.f},
            {size_t(VectorAuthoringSurfaceChannel::Transmission), "Transmission", "Transmission strength", "Reflectance", 0x7dd3fc, 1.f},
        };
        const auto SetChannelEnabled = [&](size_t Index, bool Enabled)
        {
            EvaluateEdit([&](auto &Document)
            {
                for (auto &Layer : Document.Layers)
                    if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                        Layer.SurfaceChannels[Index].Enabled = Enabled;
            });
        };
        size_t EnabledChannelCount = 0;
        for (const auto &Channel : StampChannels) EnabledChannelCount += First->SurfaceChannels[Channel.Index].Enabled ? 1 : 0;
        Controls.BeginCard("", false);
        {
            const float S = Scale;
            auto *Ink = ImGui::GetWindowDrawList();
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            const float Width = ImGui::GetContentRegionAvail().x;
            const float Right = Origin.x + Width;
            DrawGlyph("M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83Z M2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12 M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17", ImVec2(Origin.x, Origin.y + 5 * S), 14 * S, 0xaaa99b);
            Caption(ImVec2(Origin.x + 21 * S, Origin.y + 6 * S), "Channel targets", 11 * S, 0xaaa99b);
            char Count[8];
            std::snprintf(Count, sizeof(Count), "%zu", EnabledChannelCount);
            const float BadgeX = Right - 42 * S;
            Ink->AddCircleFilled(ImVec2(BadgeX, Origin.y + 12.5f * S), 10 * S, PanelChrome::Colour32(0x3b3b37), 20);
            Caption(ImVec2(BadgeX - CaptionWidth(Count, 9 * S) * .5f, Origin.y + 7.5f * S), Count, 9 * S, 0xd8d8ca);
            ImGui::BeginDisabled(Protected);
            ImGui::SetCursorScreenPos(ImVec2(Right - 25 * S, Origin.y));
            if (BareIconButton("StampChannelPickerToggle", "M12 5v14M5 12h14", 25 * S, S, ChannelsOpen, 0x9a9a8e)) ChannelsOpen = !ChannelsOpen;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Add surface channel");
            ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 38 * S));
            ImGui::Dummy(ImVec2(0, 0));
            const ImVec2 ChipsOrigin = ImGui::GetCursorScreenPos();
            float ChipX = ChipsOrigin.x, ChipY = ChipsOrigin.y;
            bool AnyChip = false;
            for (const auto &Channel : StampChannels)
            {
                if (!First->SurfaceChannels[Channel.Index].Enabled) continue;
                AnyChip = true;
                const float ChipWidth = 41 * S + CaptionWidth(Channel.Label, 9 * S);
                if (ChipX + ChipWidth > Right && ChipX > ChipsOrigin.x) { ChipX = ChipsOrigin.x; ChipY += 30 * S; }
                Ink->AddRectFilled(ImVec2(ChipX, ChipY), ImVec2(ChipX + ChipWidth, ChipY + 25 * S), PanelChrome::Colour32(0x171717), 12.5f * S);
                Ink->AddRect(ImVec2(ChipX, ChipY), ImVec2(ChipX + ChipWidth, ChipY + 25 * S), PanelChrome::Colour32(0xffffff, .047f), 12.5f * S);
                Ink->AddCircleFilled(ImVec2(ChipX + 11 * S, ChipY + 12.5f * S), 3 * S, PanelChrome::Colour32(Channel.Colour), 10);
                Caption(ImVec2(ChipX + 19 * S, ChipY + 7.5f * S), Channel.Label, 9 * S, 0xb8b8aa);
                ImGui::BeginDisabled(Protected);
                ImGui::SetCursorScreenPos(ImVec2(ChipX + ChipWidth - 22 * S, ChipY + 4 * S));
                ImGui::PushID(int(Channel.Index) + 800);
                if (ImGui::InvisibleButton("##StampChipRemove", ImVec2(17 * S, 17 * S))) SetChannelEnabled(Channel.Index, false);
                ImGui::PopID();
                ImGui::EndDisabled();
                DrawGlyph("M6 6L18 18M6 18L18 6", ImVec2(ChipX + ChipWidth - 18.5f * S, ChipY + 7.5f * S), 10 * S, 0x86867d);
                ChipX += ChipWidth + 5 * S;
            }
            ImGui::SetCursorScreenPos(ImVec2(ChipsOrigin.x, AnyChip ? ChipY + 25 * S : ChipsOrigin.y));
            ImGui::Dummy(ImVec2(0, 0));
            if (EnabledChannelCount)
            {
                const float ClearWidth = CaptionWidth("Clear all", 8 * S) + 14 * S;
                const ImVec2 ClearOrigin(Right - ClearWidth, ImGui::GetCursorScreenPos().y + 10 * S);
                ImGui::BeginDisabled(Protected);
                ImGui::SetCursorScreenPos(ClearOrigin);
                if (ImGui::InvisibleButton("##StampChannelClearAll", ImVec2(ClearWidth, 20 * S)))
                    EvaluateEdit([&](auto &Document)
                    {
                        for (auto &Layer : Document.Layers)
                            if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                                for (auto &Contribution : Layer.SurfaceChannels) Contribution.Enabled = false;
                    });
                ImGui::EndDisabled();
                Caption(ImVec2(ClearOrigin.x + 7 * S, ClearOrigin.y + 5 * S), "Clear all", 8 * S, 0x828277);
                ImGui::SetCursorScreenPos(ImVec2(Origin.x, ClearOrigin.y + 20 * S));
                ImGui::Dummy(ImVec2(0, 0));
            }
            if (ChannelsOpen)
            {
                ImGui::Dummy(ImVec2(1, 14 * S));
                const ImVec2 RuleOrigin = ImGui::GetCursorScreenPos();
                Ink->AddLine(RuleOrigin, ImVec2(Right, RuleOrigin.y), PanelChrome::Colour32(0xffffff, .051f));
                ImGui::Dummy(ImVec2(1, 11 * S));
                const char *Groups[] = {"Surface", "Radiance", "Reflectance"};
                bool FirstGroup = true;
                for (const char *Group : Groups)
                {
                    if (!FirstGroup) ImGui::Dummy(ImVec2(1, 12 * S));
                    FirstGroup = false;
                    std::string Heading = Group;
                    for (auto &Character : Heading) Character = char(std::toupper(uint8_t(Character)));
                    Caption(ImVec2(ImGui::GetCursorScreenPos().x + 3 * S, ImGui::GetCursorScreenPos().y), Heading.c_str(), 8 * S, 0x8b8b7f);
                    ImGui::Dummy(ImVec2(1, 16 * S));
                    for (const auto &Channel : StampChannels)
                    {
                        if (std::string(Channel.Group) != Group) continue;
                        const bool On = First->SurfaceChannels[Channel.Index].Enabled;
                        const ImVec2 RowOrigin = ImGui::GetCursorScreenPos();
                        ImGui::BeginDisabled(Protected);
                        ImGui::PushID(int(Channel.Index) + 900);
                        const bool Clicked = ImGui::InvisibleButton("##StampPickerRow", ImVec2(Width, 25 * S));
                        ImGui::PopID();
                        ImGui::EndDisabled();
                        if (Clicked && !Protected) SetChannelEnabled(Channel.Index, !On);
                        if (On) Ink->AddRectFilled(RowOrigin, ImVec2(Right, RowOrigin.y + 25 * S), PanelChrome::Colour32(0x31392b), 8 * S);
                        else if (ImGui::IsItemHovered()) Ink->AddRectFilled(RowOrigin, ImVec2(Right, RowOrigin.y + 25 * S), PanelChrome::Colour32(0xffffff, .031f), 8 * S);
                        Ink->AddCircleFilled(ImVec2(RowOrigin.x + 10 * S, RowOrigin.y + 12.5f * S), 3 * S, PanelChrome::Colour32(Channel.Colour), 10);
                        Caption(ImVec2(RowOrigin.x + 20 * S, RowOrigin.y + 7 * S), Channel.Label, 10 * S, On ? 0xd9e6d7 : 0xaaa99c);
                        const ImVec2 BoxMin(Right - 20 * S, RowOrigin.y + 6 * S), BoxMax(BoxMin.x + 13 * S, BoxMin.y + 13 * S);
                        if (On) Ink->AddRectFilled(BoxMin, BoxMax, PanelChrome::Colour32(0xbfc8a8), 3 * S);
                        Ink->AddRect(BoxMin, BoxMax, PanelChrome::Colour32(0xffffff, .25f), 3 * S);
                        if (On) DrawGlyph("M20 6L9 17l-5-5", ImVec2(BoxMin.x + 2 * S, BoxMin.y + 2 * S), 9 * S, 0x23281f, 2.4f);
                        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowOrigin.y + 25 * S));
                    }
                }
                ImGui::Dummy(ImVec2(0, 0));
            }
            if (!EnabledChannelCount)
            {
                ImGui::Dummy(ImVec2(1, 10 * S));
                ImGui::TextDisabled("No channels enabled. Existing channel values are retained.");
            }
        }
        Controls.EndCard();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(80.f, ImGui::GetContentRegionAvail().x - 14.f * Scale));
        ImGui::TextWrapped("For a plastic/film sticker over a decal: enable Transmission + Clearcoat + Roughness on the "
            "film layer, and Height/Normal on the decal layer beneath it for embossed text or bump.");
        ImGui::PopTextWrapPos();
        if (Selection.size() > 1)
            ImGui::TextDisabled("Toggling a channel applies the displayed setting to every selected component.");
        for (const auto &Channel : StampChannels)
        {
            if (!First->SurfaceChannels[Channel.Index].Enabled) continue;
            Controls.BeginCard(Channel.Label, false, true);
            ImGui::BeginDisabled(Protected);
            if (Channel.Index == size_t(VectorAuthoringSurfaceChannel::Colour))
            {
                ImGui::TextDisabled("Fill");
        int Category = std::clamp(int(Source.Transform[3]), 0, 3);
        const char *Sources[]{"Solid colour", "Linear gradient", "Radial gradient", "Freeform 2D gradient"};
        const char *StrokeSources[]{"Solid colour", "Linear gradient", "Radial gradient"};
        const char *InterpolationModes[]{"Linear", "Hermite", "Catmull-Rom", "B-Spline", "Step"};
        if (Controls.Dropdown("Component fill source", Category, Sources))
        {
            VectorAuthoringSourcePatch Patch;
            Patch.PaintCategory = uint32_t(Category);
            if (Category == 1 || Category == 2)
                Patch.GradientAxis = Category == 2 ? std::array<float, 4>{0.f, 0.f, .5f, 0.f} : std::array<float, 4>{-.5f, 0.f, .5f, 0.f};
            else if (Category == 3)
            {
                auto ExistingPoints = QueryVectorFreeformPoints(Source);
                bool AllSame = true;
                for (size_t Index = 1; Index < ExistingPoints.size(); ++Index)
                {
                    if (std::abs(ExistingPoints[Index].Colour[0] - ExistingPoints[0].Colour[0]) > .04f ||
                        std::abs(ExistingPoints[Index].Colour[1] - ExistingPoints[0].Colour[1]) > .04f ||
                        std::abs(ExistingPoints[Index].Colour[2] - ExistingPoints[0].Colour[2]) > .04f)
                    {
                        AllSame = false;
                        break;
                    }
                }
                if (AllSame || int(Source.Transform[3]) != 3)
                {
                    const std::vector<VectorFreeformGradientPoint> Seeded{
                        {{-.34f, -.34f}, {.95f, .33f, .28f, 1.f}},
                        {{.34f, -.34f}, {.98f, .78f, .18f, 1.f}},
                        {{-.34f, .34f}, {.14f, .82f, .76f, 1.f}},
                        {{.34f, .34f}, {.46f, .30f, .96f, 1.f}}};
                    ApplyVectorFreeformPoints(Patch, Seeded, std::clamp(Source.FreeformParameters[1], .25f, 8.f));
                    SelectedFreeformPoint = 0;
                }
            }
            if (Category != 3) FreeformPlacePointOnCanvas = false;
            ApplySourcePatch(Patch);
        }
        if (!Category)
        {
            auto Colour = Source.Colour;
            if (Controls.Colour("Fill colour", Colour)) { VectorAuthoringSourcePatch Patch; Patch.Colour = Colour; ApplySourcePatch(Patch); }
        }
        else if (Category == 1 || Category == 2)
        {
            int InterpMode = int(QueryVectorFillInterpolation(Source));
            if (Controls.Dropdown("Gradient interpolation", InterpMode, InterpolationModes))
            {
                VectorAuthoringSourcePatch Patch;
                Patch.GradientInterpolation = uint32_t(std::clamp(InterpMode, 0, 4));
                ApplySourcePatch(Patch);
            }
            auto Stops = QueryVectorGradientStops(Source);
            SelectedGradientStop = std::clamp(SelectedGradientStop, 0, int(Stops.size()) - 1);
            const float CardWidth = ImGui::GetContentRegionAvail().x;
            const ImVec2 BarOrigin = ImGui::GetCursorScreenPos();
            const float BarHeight = 18 * Scale;
            auto *RampInk = ImGui::GetWindowDrawList();
            const auto RampColour = [](const std::array<float, 4> &C) { return ImGui::GetColorU32(ImVec4(C[0], C[1], C[2], 1)); };
            RampInk->AddRectFilled(BarOrigin, ImVec2(BarOrigin.x + CardWidth, BarOrigin.y + BarHeight), IM_COL32(16, 16, 16, 255), 4 * Scale);
            constexpr int RampSlices = 32;
            for (int Slice = 0; Slice < RampSlices; ++Slice)
            {
                const float T0 = float(Slice) / float(RampSlices), T1 = float(Slice + 1) / float(RampSlices);
                const auto C0 = EvaluateVectorGradientColour(Stops, T0, uint32_t(InterpMode));
                const auto C1 = EvaluateVectorGradientColour(Stops, T1, uint32_t(InterpMode));
                RampInk->AddRectFilledMultiColor(ImVec2(BarOrigin.x + T0 * CardWidth, BarOrigin.y),
                    ImVec2(BarOrigin.x + T1 * CardWidth, BarOrigin.y + BarHeight),
                    RampColour(C0), RampColour(C1), RampColour(C1), RampColour(C0));
            }
            RampInk->AddRect(BarOrigin, ImVec2(BarOrigin.x + CardWidth, BarOrigin.y + BarHeight), IM_COL32(255, 255, 255, 26), 4 * Scale);
            for (size_t Index = 0; Index < Stops.size(); ++Index)
            {
                const float MarkerX = BarOrigin.x + Stops[Index].Position * CardWidth;
                ImGui::SetCursorScreenPos(ImVec2(MarkerX - 6 * Scale, BarOrigin.y + BarHeight + 2 * Scale));
                ImGui::PushID(int(Index) + 500);
                if (ImGui::InvisibleButton("##GradientStop", ImVec2(12 * Scale, 8 * Scale))) SelectedGradientStop = int(Index);
                ImGui::PopID();
                const bool IsSelected = int(Index) == SelectedGradientStop;
                RampInk->AddTriangleFilled(ImVec2(MarkerX, BarOrigin.y + BarHeight + 2 * Scale),
                    ImVec2(MarkerX - 5 * Scale, BarOrigin.y + BarHeight + 9 * Scale), ImVec2(MarkerX + 5 * Scale, BarOrigin.y + BarHeight + 9 * Scale),
                    IsSelected ? IM_COL32(255, 255, 255, 255) : RampColour(Stops[Index].Colour));
            }
            ImGui::SetCursorScreenPos(ImVec2(BarOrigin.x, BarOrigin.y + BarHeight + 18 * Scale));
            auto SelectedColour = Stops[SelectedGradientStop].Colour;
            char StopLabel[24]; std::snprintf(StopLabel, sizeof(StopLabel), "Stop %d colour", SelectedGradientStop + 1);
            if (Controls.Colour(StopLabel, SelectedColour))
            {
                Stops[SelectedGradientStop].Colour = SelectedColour;
                VectorAuthoringSourcePatch Patch; ApplyVectorGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            float StopPosition = Stops[SelectedGradientStop].Position * 100;
            const float LowerBound = SelectedGradientStop > 0 ? Stops[SelectedGradientStop - 1].Position * 100 + 1 : 0.f;
            const float UpperBound = SelectedGradientStop + 1 < int(Stops.size()) ? Stops[SelectedGradientStop + 1].Position * 100 - 1 : 100.f;
            if (Controls.Number("Stop position %", StopPosition, LowerBound, UpperBound))
            {
                Stops[SelectedGradientStop].Position = std::clamp(StopPosition / 100.f, 0.f, 1.f);
                VectorAuthoringSourcePatch Patch; ApplyVectorGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            ImGui::BeginDisabled(Stops.size() >= size_t(VectorGradientStopCapacity));
            if (Controls.Button("Add stop", (CardWidth - 8 * Scale) * .5f))
            {
                float Previous = 0.f, BestGap = 0.f, BestPosition = .5f;
                for (size_t Index = 0; Index <= Stops.size(); ++Index)
                {
                    const float Current = Index < Stops.size() ? Stops[Index].Position : 1.f;
                    if (Current - Previous > BestGap) { BestGap = Current - Previous; BestPosition = (Current + Previous) * .5f; }
                    Previous = Current;
                }
                const auto NewColour = EvaluateVectorGradientColour(Stops, BestPosition, uint32_t(InterpMode));
                Stops.push_back({BestPosition, NewColour});
                std::sort(Stops.begin(), Stops.end(), [](const auto &A, const auto &B) { return A.Position < B.Position; });
                const auto Inserted = std::find_if(Stops.begin(), Stops.end(), [&](const auto &Stop) { return Stop.Position == BestPosition; });
                SelectedGradientStop = int(Inserted - Stops.begin());
                VectorAuthoringSourcePatch Patch; ApplyVectorGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 8 * Scale);
            ImGui::BeginDisabled(Stops.size() <= 2);
            if (Controls.Button("Remove stop", (CardWidth - 8 * Scale) * .5f))
            {
                Stops.erase(Stops.begin() + SelectedGradientStop);
                SelectedGradientStop = std::clamp(SelectedGradientStop, 0, int(Stops.size()) - 1);
                VectorAuthoringSourcePatch Patch; ApplyVectorGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::PushID("Freeform2DGradient");
            int InterpMode = int(QueryVectorFillInterpolation(Source));
            if (Controls.Dropdown("Gradient interpolation", InterpMode, InterpolationModes))
            {
                VectorAuthoringSourcePatch Patch;
                Patch.GradientInterpolation = uint32_t(std::clamp(InterpMode, 0, 4));
                ApplySourcePatch(Patch);
            }
            auto Points = QueryVectorFreeformPoints(Source);
            float Sharpness = std::clamp(Source.FreeformParameters[1], .25f, 8.f);
            SelectedFreeformPoint = std::clamp(SelectedFreeformPoint, 0, int(Points.size()) - 1);
            const float CardWidth = ImGui::GetContentRegionAvail().x;
            const float PadExtent = std::clamp(CardWidth, 120.f * Scale, 210.f * Scale);
            const ImVec2 PadOrigin(ImGui::GetCursorScreenPos().x + (CardWidth - PadExtent) * .5f, ImGui::GetCursorScreenPos().y);
            auto *MeshInk = ImGui::GetWindowDrawList();
            constexpr int MeshSubdiv = 14;
            const auto MeshCol = [](const std::array<float, 4> &C) { return ImGui::GetColorU32(ImVec4(C[0], C[1], C[2], 1.f)); };
            std::array<std::array<ImU32, MeshSubdiv + 1>, MeshSubdiv + 1> GridColours{};
            for (int Gy = 0; Gy <= MeshSubdiv; ++Gy)
                for (int Gx = 0; Gx <= MeshSubdiv; ++Gx)
                {
                    const float NormX = float(Gx) / float(MeshSubdiv) - .5f;
                    const float NormY = float(Gy) / float(MeshSubdiv) - .5f;
                    GridColours[size_t(Gy)][size_t(Gx)] = MeshCol(EvaluateVectorFreeformGradient2D(Points, {NormX, NormY}, Sharpness, uint32_t(InterpMode)));
                }
            for (int Gy = 0; Gy < MeshSubdiv; ++Gy)
                for (int Gx = 0; Gx < MeshSubdiv; ++Gx)
                {
                    const float X0 = PadOrigin.x + float(Gx) / float(MeshSubdiv) * PadExtent;
                    const float X1 = PadOrigin.x + float(Gx + 1) / float(MeshSubdiv) * PadExtent;
                    const float Y0 = PadOrigin.y + float(Gy) / float(MeshSubdiv) * PadExtent;
                    const float Y1 = PadOrigin.y + float(Gy + 1) / float(MeshSubdiv) * PadExtent;
                    MeshInk->AddRectFilledMultiColor(ImVec2(X0, Y0), ImVec2(X1, Y1),
                        GridColours[size_t(Gy)][size_t(Gx)], GridColours[size_t(Gy)][size_t(Gx + 1)],
                        GridColours[size_t(Gy + 1)][size_t(Gx + 1)], GridColours[size_t(Gy + 1)][size_t(Gx)]);
                }
            MeshInk->AddRect(PadOrigin, ImVec2(PadOrigin.x + PadExtent, PadOrigin.y + PadExtent), IM_COL32(255, 255, 255, 52), 4 * Scale);
            for (size_t Index = 0; Index < Points.size(); ++Index)
            {
                const float U = std::clamp(Points[Index].Position[0] + .5f, 0.f, 1.f);
                const float V = std::clamp(Points[Index].Position[1] + .5f, 0.f, 1.f);
                const ImVec2 Pin(PadOrigin.x + U * PadExtent, PadOrigin.y + V * PadExtent);
                const bool IsSel = int(Index) == SelectedFreeformPoint;
                const float R = (IsSel ? 6.5f : 5.f) * Scale;
                MeshInk->AddCircleFilled(Pin, R + 1.8f * Scale, IM_COL32(20, 22, 20, 230));
                MeshInk->AddCircleFilled(Pin, R, MeshCol(Points[Index].Colour));
                MeshInk->AddCircle(Pin, R, IsSel ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 215, 200), 0, (IsSel ? 2.2f : 1.2f) * Scale);
            }
            ImGui::SetCursorScreenPos(PadOrigin);
            ImGui::InvisibleButton("##FreeformPad", ImVec2(PadExtent, PadExtent));
            {
                const auto PadMin = ImGui::GetItemRectMin(), PadMax = ImGui::GetItemRectMax();
                ControlExtents["Freeform gradient pad"] = {PadMin.x, PadMin.y, PadMax.x, PadMax.y};
            }
            if (ImGui::IsItemClicked(0))
            {
                const ImVec2 Mouse = ImGui::GetIO().MousePos;
                int HitPin = -1;
                for (size_t Index = 0; Index < Points.size(); ++Index)
                {
                    const ImVec2 Pin(PadOrigin.x + std::clamp(Points[Index].Position[0] + .5f, 0.f, 1.f) * PadExtent,
                                     PadOrigin.y + std::clamp(Points[Index].Position[1] + .5f, 0.f, 1.f) * PadExtent);
                    if (std::hypot(Mouse.x - Pin.x, Mouse.y - Pin.y) <= 10.f * Scale) { HitPin = int(Index); break; }
                }
                if (HitPin >= 0) SelectedFreeformPoint = HitPin;
                else if (Points.size() < size_t(VectorFreeformPointCapacity))
                {
                    const float NormX = std::clamp((Mouse.x - PadOrigin.x) / PadExtent - .5f, -.5f, .5f);
                    const float NormY = std::clamp((Mouse.y - PadOrigin.y) / PadExtent - .5f, -.5f, .5f);
                    auto Seed = EvaluateVectorFreeformGradient2D(Points, {NormX, NormY}, Sharpness, uint32_t(InterpMode));
                    Seed[3] = 1.f;
                    Points.push_back({{NormX, NormY}, Seed});
                    SelectedFreeformPoint = int(Points.size()) - 1;
                    VectorAuthoringSourcePatch Patch; ApplyVectorFreeformPoints(Patch, Points, Sharpness); ApplySourcePatch(Patch);
                }
            }
            else if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 1.f) && SelectedFreeformPoint >= 0 && SelectedFreeformPoint < int(Points.size()))
            {
                const ImVec2 Mouse = ImGui::GetIO().MousePos;
                Points[size_t(SelectedFreeformPoint)].Position = {
                    std::clamp((Mouse.x - PadOrigin.x) / PadExtent - .5f, -.5f, .5f),
                    std::clamp((Mouse.y - PadOrigin.y) / PadExtent - .5f, -.5f, .5f)};
                VectorAuthoringSourcePatch Patch; ApplyVectorFreeformPoints(Patch, Points, Sharpness); ApplySourcePatch(Patch);
            }
            ImGui::Spacing();
            if (Controls.Range("Blend sharpness", Sharpness, .25f, 8.f))
            {
                VectorAuthoringSourcePatch Patch;
                ApplyVectorFreeformPoints(Patch, Points, Sharpness);
                ApplySourcePatch(Patch);
            }
            Controls.Toggle("Click canvas to add points", FreeformPlacePointOnCanvas);
            SelectedFreeformPoint = std::clamp(SelectedFreeformPoint, 0, int(Points.size()) - 1);
            auto PointColour = Points[size_t(SelectedFreeformPoint)].Colour;
            char PointLabel[32]{};
            std::snprintf(PointLabel, sizeof(PointLabel), "Point %d colour", SelectedFreeformPoint + 1);
            if (Controls.Colour(PointLabel, PointColour))
            {
                Points[size_t(SelectedFreeformPoint)].Colour = PointColour;
                VectorAuthoringSourcePatch Patch; ApplyVectorFreeformPoints(Patch, Points, Sharpness); ApplySourcePatch(Patch);
            }
            float PosXPercent = (Points[size_t(SelectedFreeformPoint)].Position[0] + .5f) * 100.f;
            float PosYPercent = (Points[size_t(SelectedFreeformPoint)].Position[1] + .5f) * 100.f;
            bool CoordEdited = Controls.Number("Point X %", PosXPercent, -150.f, 250.f);
            CoordEdited |= Controls.Number("Point Y %", PosYPercent, -150.f, 250.f);
            if (CoordEdited)
            {
                Points[size_t(SelectedFreeformPoint)].Position = {PosXPercent / 100.f - .5f, PosYPercent / 100.f - .5f};
                VectorAuthoringSourcePatch Patch; ApplyVectorFreeformPoints(Patch, Points, Sharpness); ApplySourcePatch(Patch);
            }
            ImGui::BeginDisabled(Points.size() >= size_t(VectorFreeformPointCapacity));
            if (Controls.Button("Add point", (CardWidth - 8 * Scale) * .5f))
            {
                static constexpr std::array<std::array<float, 4>, 8> Palette{{
                    {.95f, .33f, .28f, 1.f}, {.98f, .78f, .18f, 1.f}, {.14f, .82f, .76f, 1.f}, {.46f, .30f, .96f, 1.f},
                    {.96f, .32f, .72f, 1.f}, {.26f, .88f, .36f, 1.f}, {.22f, .58f, .98f, 1.f}, {.98f, .54f, .16f, 1.f}}};
                const size_t Slot = Points.size();
                const float Angle = float(Slot) * 2.399963f;
                const std::array<float, 2> Pos{std::cos(Angle) * .24f, std::sin(Angle) * .24f};
                Points.push_back({Pos, Palette[Slot % Palette.size()]});
                SelectedFreeformPoint = int(Points.size()) - 1;
                VectorAuthoringSourcePatch Patch; ApplyVectorFreeformPoints(Patch, Points, Sharpness); ApplySourcePatch(Patch);
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 8 * Scale);
            ImGui::BeginDisabled(Points.size() <= 2);
            if (Controls.Button("Remove point", (CardWidth - 8 * Scale) * .5f))
            {
                Points.erase(Points.begin() + SelectedFreeformPoint);
                SelectedFreeformPoint = std::clamp(SelectedFreeformPoint, 0, int(Points.size()) - 1);
                VectorAuthoringSourcePatch Patch; ApplyVectorFreeformPoints(Patch, Points, Sharpness); ApplySourcePatch(Patch);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        bool Filled = Source.Paint[3] != 0;
        if (Controls.Toggle("Fill enabled", Filled)) { VectorAuthoringSourcePatch Patch; Patch.FillEnabled = Filled; ApplySourcePatch(Patch); }
                ImGui::TextDisabled("Stroke");
        const bool ClosedFilledPath = Source.Topology[0] == 6 && Source.Paint[3] != 0;
        float Stroke = ClosedFilledPath ? Source.Paint[0] : Source.Transform[1];
        if (Controls.Number(ClosedFilledPath ? "Outline width" : "Stroke width", Stroke, 0, 25))
        {
            VectorAuthoringSourcePatch Patch;
            if (ClosedFilledPath) Patch.ClosedOutlineWidth = Stroke; else Patch.StrokeWidth = Stroke;
            ApplySourcePatch(Patch);
        }
        int StrokeCategory = std::clamp(int(Source.ShapeParameters[1]), 0, 2);
        if (Controls.Dropdown("Component stroke source", StrokeCategory, StrokeSources))
        {
            VectorAuthoringSourcePatch Patch; Patch.StrokePaintCategory = uint32_t(StrokeCategory);
            Patch.StrokeGradientAxis = StrokeCategory == 2 ? std::array<float, 4>{0.f, 0.f, .5f, 0.f} : std::array<float, 4>{-.5f, 0.f, .5f, 0.f};
            if (!(Source.Topology[3] & VectorCoverageStrokeColourExplicit)) Patch.StrokeColour = Source.Colour;
            ApplySourcePatch(Patch);
        }
        if (!StrokeCategory)
        {
            auto StrokeColour = (Source.Topology[3] & VectorCoverageStrokeColourExplicit) ? Source.StrokeColour : Source.Colour;
            if (Controls.Colour(ClosedFilledPath ? "Outline colour" : "Stroke colour", StrokeColour)) { VectorAuthoringSourcePatch Patch; Patch.StrokeColour = StrokeColour; ApplySourcePatch(Patch); }
        }
        else
        {
            ImGui::PushID("StrokeGradient");
            int StrokeInterp = int(QueryVectorStrokeInterpolation(Source));
            if (Controls.Dropdown("Stroke gradient interpolation", StrokeInterp, InterpolationModes))
            {
                VectorAuthoringSourcePatch Patch;
                Patch.StrokeGradientInterpolation = uint32_t(std::clamp(StrokeInterp, 0, 4));
                ApplySourcePatch(Patch);
            }
            auto Stops = QueryVectorStrokeGradientStops(Source);
            SelectedStrokeGradientStop = std::clamp(SelectedStrokeGradientStop, 0, int(Stops.size()) - 1);
            const float CardWidth = ImGui::GetContentRegionAvail().x;
            const ImVec2 BarOrigin = ImGui::GetCursorScreenPos();
            const float BarHeight = 18 * Scale;
            auto *RampInk = ImGui::GetWindowDrawList();
            const auto RampColour = [](const std::array<float, 4> &C) { return ImGui::GetColorU32(ImVec4(C[0], C[1], C[2], 1)); };
            RampInk->AddRectFilled(BarOrigin, ImVec2(BarOrigin.x + CardWidth, BarOrigin.y + BarHeight), IM_COL32(16, 16, 16, 255), 4 * Scale);
            constexpr int RampSlices = 32;
            for (int Slice = 0; Slice < RampSlices; ++Slice)
            {
                const float T0 = float(Slice) / float(RampSlices), T1 = float(Slice + 1) / float(RampSlices);
                const auto C0 = EvaluateVectorGradientColour(Stops, T0, uint32_t(StrokeInterp));
                const auto C1 = EvaluateVectorGradientColour(Stops, T1, uint32_t(StrokeInterp));
                RampInk->AddRectFilledMultiColor(ImVec2(BarOrigin.x + T0 * CardWidth, BarOrigin.y),
                    ImVec2(BarOrigin.x + T1 * CardWidth, BarOrigin.y + BarHeight),
                    RampColour(C0), RampColour(C1), RampColour(C1), RampColour(C0));
            }
            RampInk->AddRect(BarOrigin, ImVec2(BarOrigin.x + CardWidth, BarOrigin.y + BarHeight), IM_COL32(255, 255, 255, 26), 4 * Scale);
            for (size_t Index = 0; Index < Stops.size(); ++Index)
            {
                const float MarkerX = BarOrigin.x + Stops[Index].Position * CardWidth;
                ImGui::SetCursorScreenPos(ImVec2(MarkerX - 6 * Scale, BarOrigin.y + BarHeight + 2 * Scale));
                ImGui::PushID(int(Index) + 500);
                if (ImGui::InvisibleButton("##GradientStop", ImVec2(12 * Scale, 8 * Scale))) SelectedStrokeGradientStop = int(Index);
                ImGui::PopID();
                const bool IsSelected = int(Index) == SelectedStrokeGradientStop;
                RampInk->AddTriangleFilled(ImVec2(MarkerX, BarOrigin.y + BarHeight + 2 * Scale),
                    ImVec2(MarkerX - 5 * Scale, BarOrigin.y + BarHeight + 9 * Scale), ImVec2(MarkerX + 5 * Scale, BarOrigin.y + BarHeight + 9 * Scale),
                    IsSelected ? IM_COL32(255, 255, 255, 255) : RampColour(Stops[Index].Colour));
            }
            ImGui::SetCursorScreenPos(ImVec2(BarOrigin.x, BarOrigin.y + BarHeight + 18 * Scale));
            auto SelectedColour = Stops[SelectedStrokeGradientStop].Colour;
            char StopLabel[24]; std::snprintf(StopLabel, sizeof(StopLabel), "Stroke stop %d colour", SelectedStrokeGradientStop + 1);
            if (Controls.Colour(StopLabel, SelectedColour))
            {
                Stops[SelectedStrokeGradientStop].Colour = SelectedColour;
                VectorAuthoringSourcePatch Patch; ApplyVectorStrokeGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            float StopPosition = Stops[SelectedStrokeGradientStop].Position * 100;
            const float LowerBound = SelectedStrokeGradientStop > 0 ? Stops[SelectedStrokeGradientStop - 1].Position * 100 + 1 : 0.f;
            const float UpperBound = SelectedStrokeGradientStop + 1 < int(Stops.size()) ? Stops[SelectedStrokeGradientStop + 1].Position * 100 - 1 : 100.f;
            if (Controls.Number("Stroke stop position %", StopPosition, LowerBound, UpperBound))
            {
                Stops[SelectedStrokeGradientStop].Position = std::clamp(StopPosition / 100.f, 0.f, 1.f);
                VectorAuthoringSourcePatch Patch; ApplyVectorStrokeGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            ImGui::BeginDisabled(Stops.size() >= size_t(VectorGradientStopCapacity));
            if (Controls.Button("Add stroke stop", (CardWidth - 8 * Scale) * .5f))
            {
                float Previous = 0.f, BestGap = 0.f, BestPosition = .5f;
                for (size_t Index = 0; Index <= Stops.size(); ++Index)
                {
                    const float Current = Index < Stops.size() ? Stops[Index].Position : 1.f;
                    if (Current - Previous > BestGap) { BestGap = Current - Previous; BestPosition = (Current + Previous) * .5f; }
                    Previous = Current;
                }
                const auto NewColour = EvaluateVectorGradientColour(Stops, BestPosition, uint32_t(StrokeInterp));
                Stops.push_back({BestPosition, NewColour});
                std::sort(Stops.begin(), Stops.end(), [](const auto &A, const auto &B) { return A.Position < B.Position; });
                const auto Inserted = std::find_if(Stops.begin(), Stops.end(), [&](const auto &Stop) { return Stop.Position == BestPosition; });
                SelectedStrokeGradientStop = int(Inserted - Stops.begin());
                VectorAuthoringSourcePatch Patch; ApplyVectorStrokeGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 8 * Scale);
            ImGui::BeginDisabled(Stops.size() <= 2);
            if (Controls.Button("Remove stroke stop", (CardWidth - 8 * Scale) * .5f))
            {
                Stops.erase(Stops.begin() + SelectedStrokeGradientStop);
                SelectedStrokeGradientStop = std::clamp(SelectedStrokeGradientStop, 0, int(Stops.size()) - 1);
                VectorAuthoringSourcePatch Patch; ApplyVectorStrokeGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            ImGui::EndDisabled();
            auto Axis = Source.StrokeGradientAxis;
            const char *AxisCaptions[]{"Stroke gradient start X", "Stroke gradient start Y", "Stroke gradient end X", "Stroke gradient end Y"};
            for (size_t Index = 0; Index < Axis.size(); ++Index)
                if (Controls.Number(AxisCaptions[Index], Axis[Index], -2, 2))
                { VectorAuthoringSourcePatch Patch; Patch.StrokeGradientAxis = Axis; ApplySourcePatch(Patch); }
            ImGui::PopID();
        }
        if (ClosedFilledPath) ImGui::TextWrapped("This outline width is independent of the same path's open-line thickness.");
            }
            if (Channel.Index == size_t(VectorAuthoringSurfaceChannel::Normal))
                ImGui::TextWrapped("Derived from the stamped Height. Amount scales the relief.");
            float Strength = First->SurfaceChannels[Channel.Index].Strength;
            if (Controls.Range(Channel.StrengthCaption, Strength, 0.f, Channel.Maximum))
                EvaluateEdit([&](auto &Document)
                {
                    for (auto &Layer : Document.Layers)
                        if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                            Layer.SurfaceChannels[Channel.Index].Strength = Strength;
                });
            if (Channel.Index == size_t(VectorAuthoringSurfaceChannel::Height))
            {
                float SurfaceRelief = First->SurfaceRelief;
                if (Controls.Range("Surface relief", SurfaceRelief, -1.f, 1.f))
                    EvaluateEdit([&](auto &Document)
                    {
                        for (auto &Layer : Document.Layers)
                            if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                                Layer.SurfaceRelief = SurfaceRelief;
                    });
                Controls.Tooltip("Surface relief", "Signed height amplitude used by the Height map and its derived tangent normals.");
            }
            ImGui::EndDisabled();
            Controls.EndCard();
        }
    }
    // 🎭 Mask property card — the Texture Studio mask frame (enabled / invert / strength,
    // then the source shape and its geometry) over the vector-shape mask.
    Controls.BeginCard(MaskSelection ? "Vector mask" : "Mask", true);
    ImGui::BeginDisabled(Protected);
    {
        auto EditedLayer = *First;
        if (!EditedLayer.Mask)
        {
            ImGui::TextWrapped("No mask on this component. A mask limits where the component shows; white includes and black excludes. Masks ignore the surface channel targets.");
        if (Controls.Button("Add mask")) EvaluateEdit([&](auto &Document)
        {
            for (auto &Layer : Document.Layers) if (Layer.Token == First->Token)
            {
                auto Mask = Snapshot.Instructions[First->Root];
                if (Mask.Topology[0] > 6) { Mask = VectorCoverageInstruction{}; Mask.Bounds = QueryVectorAuthoringSelectionBounds(Snapshot, Selection); }
                Mask.Colour = Mask.ColourEnd = {1, 1, 1, 1}; Layer.Mask = Mask;
            }
        });
        }
        else
        {
            bool Changed = Controls.Toggle("Enable vector mask", EditedLayer.MaskEnabled);
            Changed |= Controls.Toggle("Invert vector mask", EditedLayer.MaskInverted);
            Changed |= Controls.Number("Mask opacity", EditedLayer.Mask->Transform[2], 0, 1);
            int Category = EditedLayer.Mask->Topology[0] == 1 ? 1 : 0;
            const char *Categories[]{"Rectangle", "Ellipse"};
            if (EditedLayer.Mask->Topology[0] <= 1 && Controls.Dropdown("Mask shape", Category, Categories)) { EditedLayer.Mask->Topology[0] = uint32_t(Category); Changed = true; }
            Changed |= Controls.Toggle("Luminance mask", EditedLayer.MaskLuminance);
            Changed |= Controls.Number("Mask X", EditedLayer.Mask->Bounds[0], -CanvasUnits, CanvasUnits * 2.f);
            Changed |= Controls.Number("Mask Y", EditedLayer.Mask->Bounds[1], -CanvasUnits, CanvasUnits * 2.f);
            Changed |= Controls.Number("Mask width", EditedLayer.Mask->Bounds[2], 1, CanvasUnits * 2.f);
            Changed |= Controls.Number("Mask height", EditedLayer.Mask->Bounds[3], 1, CanvasUnits * 2.f);
            if (Changed) EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == First->Token) Layer = EditedLayer; });
            try
            {
                constexpr uint32_t PreviewExtent = 48;
                VectorAuthoringDocument MaskDocument;
                MaskDocument.Stamp = true; MaskDocument.TokenClock = 1;
                MaskDocument.CanvasExtent = Snapshot.CanvasExtent;
                MaskDocument.Instructions = {*EditedLayer.Mask};
                MaskDocument.Segments = Snapshot.Segments;
                VectorAuthoringLayer PreviewLayer; PreviewLayer.Token = 1; PreviewLayer.Root = 0; PreviewLayer.Name = "Mask preview";
                MaskDocument.Layers.push_back(PreviewLayer);
                const auto Key = EncodeStampDocumentSvg(MaskDocument);
                if (Key != MaskPreviewCacheKey || MaskPreviewPixels.empty())
                {
                    auto Pixels = RasterizeStampDocument(MaskDocument, PreviewExtent);
                    MaskPreviewPixels = std::move(Pixels); MaskPreviewCacheKey = Key;
                }
                ImGui::TextUnformatted("Mask preview");
                const ImVec2 Origin = ImGui::GetCursorScreenPos();
                const float Size = 96 * Scale, Cell = Size / float(PreviewExtent);
                ImDrawList *Draw = ImGui::GetWindowDrawList();
                for (uint32_t Y = 0; Y < PreviewExtent; ++Y) for (uint32_t X = 0; X < PreviewExtent; ++X)
                {
                    const size_t Index = (size_t(Y) * PreviewExtent + X) * 4;
                    float Value = float(MaskPreviewPixels[Index + 3]) / 255.f;
                    if (EditedLayer.MaskLuminance)
                        Value *= (.2126f * MaskPreviewPixels[Index] + .7152f * MaskPreviewPixels[Index + 1] + .0722f * MaskPreviewPixels[Index + 2]) / 255.f;
                    if (EditedLayer.MaskInverted) Value = 1.f - Value;
                    if (!EditedLayer.MaskEnabled) Value = 1.f;
                    const int Grey = int(std::clamp(Value, 0.f, 1.f) * 255.f + .5f);
                    Draw->AddRectFilled(ImVec2(Origin.x + X * Cell, Origin.y + Y * Cell),
                        ImVec2(Origin.x + (X + 1) * Cell, Origin.y + (Y + 1) * Cell), IM_COL32(Grey, Grey, Grey, 255));
                }
                ImGui::Dummy(ImVec2(Size, Size));
            }
            catch (const std::exception &) { ImGui::TextDisabled("Mask preview unavailable."); }
            if (Controls.Button("Edit mask")) MaskSelection = First->Token;
            if (Controls.Button("Remove mask")) EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == First->Token) Layer.Mask.reset(); });
            if (MaskSelection && Controls.Button("Done editing mask")) MaskSelection = 0;
            ImGui::TextDisabled("Paint/image mask authoring is not ported yet.");
        }
    }
    ImGui::EndDisabled();
    Controls.EndCard();

    if (!Diagnostic.empty()) ImGui::TextWrapped("%s", Diagnostic.c_str());
    ImGui::EndChild();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED HOSTING AND KEYBOARD INPUT
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructCloseDialogue()
{
    if (CloseRequested && !ImGui::IsPopupOpen("Unsaved stamp")) ImGui::OpenPopup("Unsaved stamp");
    ImGui::SetNextWindowSize(ImVec2(460 * Scale, 0));
    const StampModalScope ModalScope(Scale);
    if (ImGui::BeginPopupModal("Unsaved stamp", nullptr, ImGuiWindowFlags_NoResize))
    {
        const auto Dialogue = ImGui::GetCursorScreenPos();
        Caption(ImVec2(Dialogue.x, Dialogue.y + 4 * Scale), "UNSAVED STAMP", 7 * Scale, 0x8b8d79);
        Caption(ImVec2(Dialogue.x, Dialogue.y + 26 * Scale), "Save this stamp?", 17 * Scale, 0xdcdccd, LightTypeface);
        ImGui::Dummy(ImVec2(1, 58 * Scale));
        const auto &Name = QueryDocument().Name;
        ImGui::TextWrapped("\u201c%s\u201d has unsaved changes. Save keeps them; Discard restores the last save.",
            Name.empty() ? "Untitled stamp" : Name.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const float ButtonWidth = 128 * Scale, ButtonGap = 8 * Scale;
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 3 * ButtonWidth - 2 * ButtonGap - 16 * Scale);
        if (Controls.Button("Cancel", ButtonWidth)) { CloseRequested = false; ImGui::CloseCurrentPopup(); if (CancelClose) CancelClose(); }
        ImGui::SameLine(0, ButtonGap);
        if (Controls.Button("Discard", ButtonWidth)) { History.Reset(SavedDocument); CloseRequested = false; ImGui::CloseCurrentPopup(); if (ActivateMaterial) ActivateMaterial(); }
        ImGui::SameLine(0, ButtonGap);
        if (Controls.Button("Save and close", ButtonWidth, true) && Save()) { CloseRequested = false; ImGui::CloseCurrentPopup(); if (ActivateMaterial) ActivateMaterial(); }
        ImGui::EndPopup();
    }
}

void StampWorkspacePanel::ConstructExportDialogue()
{
    if (ExportDialogRequested && !ImGui::IsPopupOpen("Export stamp options"))
    {
        ImGui::OpenPopup("Export stamp options");
        ExportDialogRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(500 * Scale, 0));
    const StampModalScope ModalScope(Scale);
    if (ImGui::BeginPopupModal("Export stamp options", nullptr, ImGuiWindowFlags_NoResize))
    {
        CapturePanel("Export stamp options");
        const auto Dialogue = ImGui::GetCursorScreenPos();
        Caption(ImVec2(Dialogue.x, Dialogue.y + 4 * Scale), "EXPORT STAMP", 7 * Scale, 0x8b8d79);
        Caption(ImVec2(Dialogue.x, Dialogue.y + 24 * Scale), "Export stamp options", 17 * Scale, 0xdcdccd, LightTypeface);
        ImGui::Dummy(ImVec2(1, 54 * Scale));

        ImGui::TextDisabled("File name prefix");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##StampExportFilenamePrefix", ExportPrefixText.data(), ExportPrefixText.size());

        static constexpr const char *ExportFormats[]{"PNG (.png)", "OpenEXR (.exr)", "TIFF (.tif)", "TGA (.tga)", "SVG Vector (.svg)"};
        Controls.Dropdown("File format", ExportFormat, ExportFormats);

        char MatchCanvasOption[48]{};
        const int CanvasInt = int(std::lround(QueryCanvasExtent()));
        std::snprintf(MatchCanvasOption, sizeof(MatchCanvasOption), "Match canvas (%d \u00d7 %d)", CanvasInt, CanvasInt);
        const char *ResolutionOptions[]{"256 \u00d7 256", "512 \u00d7 512", "1024 \u00d7 1024", "2048 \u00d7 2048", "4096 \u00d7 4096", MatchCanvasOption};
        Controls.Dropdown("Resolution", ExportResolution, ResolutionOptions);

        static constexpr const char *BitDepths[]{"8-bit integer", "16-bit integer", "32-bit float (linear)"};
        Controls.Dropdown("Bit depth", ExportBitDepth, BitDepths);

        static constexpr const char *BackgroundModes[]{"Transparent alpha", "Canvas colour", "Solid black"};
        Controls.Dropdown("Background", ExportBackground, BackgroundModes);
        Controls.Number("UV dilation / bleed (px)", ExportPaddingPixels, 0.f, 64.f);
        Controls.Toggle("DirectX normal Y-flip", ExportFlipNormalY);

        ImGui::Spacing();
        ImGui::TextDisabled("PBR maps to export");
        if (ImGui::BeginTable("StampExportChannelGrid", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); Controls.Toggle("Composite preview", ExportCompositeDecal);
            ImGui::TableNextColumn(); Controls.Toggle("Base colour", ExportChannelSelection[0]);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); Controls.Toggle("Height", ExportChannelSelection[1]);
            ImGui::TableNextColumn(); Controls.Toggle("Normal", ExportChannelSelection[2]);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); Controls.Toggle("Roughness", ExportChannelSelection[3]);
            ImGui::TableNextColumn(); Controls.Toggle("Metalness", ExportChannelSelection[4]);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); Controls.Toggle("Opacity", ExportChannelSelection[5]);
            ImGui::TableNextColumn(); Controls.Toggle("Ambient occlusion", ExportChannelSelection[6]);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); Controls.Toggle("Clearcoat", ExportChannelSelection[7]);
            ImGui::TableNextColumn(); Controls.Toggle("Transmission", ExportChannelSelection[8]);
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const float ButtonWidth = 128 * Scale, ButtonGap = 8 * Scale;
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 2 * ButtonWidth - ButtonGap - 16 * Scale);
        if (Controls.Button("Cancel", ButtonWidth)) ImGui::CloseCurrentPopup();
        ImGui::SameLine(0, ButtonGap);
        if (Controls.Button("Export", ButtonWidth, true)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void StampWorkspacePanel::SuspendContent() noexcept
{
    if (GestureSource) Selection = std::move(GestureSelection);
    GestureSource.reset(); GestureDraft.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
}

void StampWorkspacePanel::FinalizeContent() noexcept
{
    SuspendContent();
    QueryCoverage = {}; QueryCoverageReady = {}; QuerySelectionToken = {}; SaveSource = {}; LoadSource = {}; ActivateMaterial = {}; ActivatePattern = {}; ActivateBaking = {}; CancelClose = {}; EvaluateNavigationDelta = {};
}

void StampWorkspacePanel::ConstructInterface()
{
    if (QueryPhase() != ActivationPhase::Active) return;
    ControlExtents.clear(); PanelExtents.clear();
    const auto SavedPalette = ControlKit::Palette();
    const auto Palette = PanelChrome::StudioPalette(SavedPalette, {.20f, .208f, .173f, 1}, {.39f, .45f, .31f, 1});
    ControlKit::ConfigurePalette(Palette);
    ImGui::PushFont(static_cast<ImFont *>(RegularTypeface), 11 * 1.302f * Scale);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x121212));
    ImGui::PushStyleColor(ImGuiCol_Text, ThemeQuad(Palette.Text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ThemeQuad(Palette.TextDim));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Spectrum(0x090909));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Spectrum(0x191919));
    ImGui::PushStyleColor(ImGuiCol_Header, Spectrum(0x191919));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Spectrum(0x272923));
    ImGui::PushStyleColor(ImGuiCol_Button, Spectrum(0x222222));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Spectrum(0x2e2e28));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Spectrum(0x33352c));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .055f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(32, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * Scale, 9 * Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * Scale, 10 * Scale));
    // Shared dock chrome (PanelChrome) - pushed last here, popped first in the tail below.
    const auto Chrome = PanelChrome::PushDockChrome(Spectrum(0x090909), Spectrum(0x101010), true, true, Scale);
    ConstructHeader();
    Panels.ConstructInterface(Scale);
    ConstructCloseDialogue();
    ConstructExportDialogue();
    const auto &Input = ImGui::GetIO();
    if (Gesture == 0 && !Input.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
    {
        const bool Command = Input.KeyCtrl || Input.KeySuper;
        if (Command && ImGui::IsKeyPressed(ImGuiKey_C)) TransferSelection(false);
        if (Command && ImGui::IsKeyPressed(ImGuiKey_X)) TransferSelection(true);
        if (Command && ImGui::IsKeyPressed(ImGuiKey_V)) InsertSelection(Input.KeyShift);
        if (Command && ImGui::IsKeyPressed(ImGuiKey_D)) ReplicateSelection();
        if (Command && ImGui::IsKeyPressed(ImGuiKey_A)) SelectEditableVectors();
        if (!Command && (ImGui::IsKeyPressed(ImGuiKey_LeftBracket) || ImGui::IsKeyPressed(ImGuiKey_RightBracket)))
        {
            const bool Forward = ImGui::IsKeyPressed(ImGuiKey_RightBracket);
            const auto Order = Input.KeyShift ? (Forward ? VectorSelectionOrder::Front : VectorSelectionOrder::Back)
                : (Forward ? VectorSelectionOrder::Forward : VectorSelectionOrder::Backward);
            if (!Selection.empty() && !MaskSelection) EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringSelection(Document, Selection, Order); });
        }
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) RestoreHistory(Input.KeyShift);
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) RestoreHistory(true);
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) Save();
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_V)) Tool = VectorAuthoringTool::Select;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_R)) Tool = VectorAuthoringTool::Rectangle;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_O)) Tool = VectorAuthoringTool::Ellipse;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_T)) Tool = VectorAuthoringTool::Text;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_A)) Tool = VectorAuthoringTool::Points;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_P)) Tool = VectorAuthoringTool::Pen;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_L)) Tool = VectorAuthoringTool::Polyline;
        if (ImGui::IsKeyPressed(ImGuiKey_0)) { Pan = {}; Zoom = 1; }
        if (!Selection.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            if (MaskSelection)
            {
                EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == MaskSelection && !QueryVectorAuthoringProtection(Document, Layer)) Layer.Mask.reset(); });
                MaskSelection = 0;
            }
            else EvaluateEdit([&](auto &Document) { PartitionVectorAuthoringSelection(Document, Selection); });
        }
        if (!Selection.empty())
        {
            VectorAuthoringSourcePatch Patch;
            const float Step = Input.KeyShift ? 10.f : 1.f;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Patch.TranslationX -= Step;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Patch.TranslationX += Step;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) Patch.TranslationY -= Step;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) Patch.TranslationY += Step;
            if (Patch.TranslationX || Patch.TranslationY) ApplySourcePatch(Patch);
        }
    }
    PanelChrome::PopDockChrome(Chrome);
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(11);
    ImGui::PopFont();
    ControlKit::ConfigurePalette(SavedPalette);
}
}
