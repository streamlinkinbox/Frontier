/*============================================================================================================================================
                                                  STAMPLIBRARY.CPP
============================================================================================================================================*/

#include "UI/TextureStudio/StampLibrary.h"
#include <stb_image.h>       // declarations only; the implementation lives in FrontierSurfaceRasterization
#include <stb_image_write.h> // declarations only; the implementation is compiled into this library
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>

namespace Frontier
{
namespace
{
constexpr int PresetExtent = 256;
constexpr int MaxStampExtent = 2048;

std::string Slugify(const std::string &Name)
{
    std::string Slug;
    for (const char Raw : Name)
    {
        const unsigned char C = static_cast<unsigned char>(Raw);
        if ((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9')) Slug += char(C);
        else if (C >= 'A' && C <= 'Z') Slug += char(C - 'A' + 'a');
        else if (!Slug.empty() && Slug.back() != '-') Slug += '-';
        if (Slug.size() >= 48) break;
    }
    while (!Slug.empty() && Slug.back() == '-') Slug.pop_back();
    return Slug.empty() ? "stamp" : Slug;
}

std::string DisplayName(const std::string &Stem)
{
    std::string Name = Stem;
    for (char &C : Name)
        if (C == '-' || C == '_') C = ' ';
    return Name.empty() ? "Stamp" : Name;
}

bool HasPngExtension(const std::filesystem::path &Path)
{
    std::string Extension = Path.extension().string();
    for (char &C : Extension) C = char(std::tolower(static_cast<unsigned char>(C)));
    return Extension == ".png";
}

std::vector<uint8_t> ReadFileBytes(const std::filesystem::path &Path, size_t Limit)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    if (!Input) return {};
    const auto Bytes = Input.tellg();
    if (Bytes <= 0 || size_t(Bytes) > Limit) return {};
    std::vector<uint8_t> Data;
    Data.resize(static_cast<size_t>(Bytes));
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Data.data()), Bytes)) return {};
    return Data;
}

// ── procedural preset coverage fields: signed distance (pixels, <=0 inside) -> 1px-AA alpha ──────
float CoverageFromDistance(float Distance) noexcept
{
    return std::clamp(0.5f - Distance, 0.f, 1.f);
}
float Length2(float X, float Y) noexcept { return std::hypot(X, Y); }

// ── preset fields: pointwise coverage over [-1,1]^2, PixelInField scales the 1px AA band to the
//    render extent - one definition renders crisply at any size (picker thumb or full texture). ──
using PresetField = std::function<float(float X, float Y, float PixelInField)>;
struct PresetDefinition
{
    const char *Slug;
    const char *Name;
    PresetField Field;
};

bool PointInPolygon(float X, float Y, const std::vector<std::array<float, 2>> &Polygon) noexcept
{
    bool Inside = false;
    for (size_t I = 0, J = Polygon.size() - 1; I < Polygon.size(); J = I++)
    {
        const auto &A = Polygon[I], &B = Polygon[J];
        if ((A[1] > Y) != (B[1] > Y) && X < (B[0] - A[0]) * (Y - A[1]) / (B[1] - A[1]) + A[0]) Inside = !Inside;
    }
    return Inside;
}
float PolygonEdgeDistance(float X, float Y, const std::vector<std::array<float, 2>> &Polygon) noexcept
{
    float Best = 1e9f;
    for (size_t I = 0, J = Polygon.size() - 1; I < Polygon.size(); J = I++)
    {
        const auto &A = Polygon[J], &B = Polygon[I];
        const float Dx = B[0] - A[0], Dy = B[1] - A[1];
        const float LenSq = Dx * Dx + Dy * Dy;
        float T = LenSq > 0 ? ((X - A[0]) * Dx + (Y - A[1]) * Dy) / LenSq : 0.f;
        T = std::clamp(T, 0.f, 1.f);
        Best = std::min(Best, Length2(X - (A[0] + Dx * T), Y - (A[1] + Dy * T)));
    }
    return Best;
}

PresetField PolygonFieldFunction(std::vector<std::array<float, 2>> Polygon)
{
    return [Polygon = std::move(Polygon)](float X, float Y, float PixelInField) {
        const float Edge = PolygonEdgeDistance(X, Y, Polygon) / PixelInField;
        return CoverageFromDistance(PointInPolygon(X, Y, Polygon) ? -Edge : Edge);
    };
}
PresetField DifferenceField(PresetField Keep, PresetField Cut)
{
    return [Keep = std::move(Keep), Cut = std::move(Cut)](float X, float Y, float PixelInField) {
        return std::clamp(Keep(X, Y, PixelInField) - Cut(X, Y, PixelInField), 0.f, 1.f);
    };
}
PresetField UnionField(PresetField A, PresetField B, float BScale)
{
    return [A = std::move(A), B = std::move(B), BScale](float X, float Y, float PixelInField) {
        return std::max(A(X, Y, PixelInField), B(X, Y, PixelInField) * BScale);
    };
}

const std::vector<PresetDefinition> &PresetDefinitions()
{
    static const std::vector<PresetDefinition> Definitions = [] {
        std::vector<PresetDefinition> Defs;
        // Ring
        Defs.push_back({"ring", "Ring", [](float X, float Y, float PixelInField) {
            return CoverageFromDistance((std::fabs(Length2(X, Y) - 0.68f) - 0.13f) / PixelInField);
        }});
        // Five-point star
        {
            std::vector<std::array<float, 2>> Star;
            for (int I = 0; I < 10; ++I)
            {
                const float Angle = -1.5707963f + I * 0.6283185f;
                const float Radius = (I % 2 == 0) ? 0.82f : 0.38f;
                Star.push_back({std::cos(Angle) * Radius, std::sin(Angle) * Radius});
            }
            Defs.push_back({"star", "Star", PolygonFieldFunction(Star)});
        }
        // Up arrow (head + shaft as one polygon, pointing toward -Y = image top)
        Defs.push_back({"arrow", "Arrow", PolygonFieldFunction({
            {0.f, -0.85f}, {0.45f, -0.25f}, {0.18f, -0.25f}, {0.18f, 0.8f},
            {-0.18f, 0.8f}, {-0.18f, -0.25f}, {-0.45f, -0.25f}})});
        // Cross / plus
        Defs.push_back({"cross", "Cross", [](float X, float Y, float PixelInField) {
            const float Vertical = std::max(std::fabs(X) - 0.22f, std::fabs(Y) - 0.8f);
            const float Horizontal = std::max(std::fabs(X) - 0.8f, std::fabs(Y) - 0.22f);
            return CoverageFromDistance(std::min(Vertical, Horizontal) / PixelInField);
        }});
        // Hazard band: diagonal stripes inside a rounded frame (dim stripes stay faintly present)
        Defs.push_back({"hazard", "Hazard stripes", [](float X, float Y, float PixelInField) {
            const float BoxX = std::fabs(X) - 0.86f, BoxY = std::fabs(Y) - 0.5f;
            const float RoundedBox = Length2(std::max(BoxX, 0.f), std::max(BoxY, 0.f)) + std::min(std::max(BoxX, BoxY), 0.f) - 0.06f;
            const float Frame = CoverageFromDistance(RoundedBox / PixelInField);
            const float Stripe = std::fmod(std::fabs(X * 0.9f + Y) * 3.2f, 1.f) < 0.5f ? 1.f : 0.22f;
            return Frame * Stripe;
        }});
        // Checker disc
        Defs.push_back({"checker-disc", "Checker disc", [](float X, float Y, float PixelInField) {
            const float Disc = CoverageFromDistance((Length2(X, Y) - 0.8f) / PixelInField);
            const int CellX = int(std::floor((X + 1.f) * 4.f)), CellY = int(std::floor((Y + 1.f) * 4.f));
            return Disc * (((CellX + CellY) % 2 == 0) ? 1.f : 0.18f);
        }});
        // Soft disc: radial falloff for gentle decal fades and airbrush-style spots
        Defs.push_back({"soft-disc", "Soft disc", [](float X, float Y, float) {
            return std::clamp((0.85f - Length2(X, Y)) / 0.45f, 0.f, 1.f);
        }});
        // Crescent moon: a disc with an offset bite
        Defs.push_back({"crescent", "Crescent", [](float X, float Y, float PixelInField) {
            const float Outer = Length2(X, Y) - 0.78f;
            const float Bite = Length2(X - 0.34f, Y + 0.10f) - 0.64f;
            return CoverageFromDistance(std::max(Outer, -Bite) / PixelInField);
        }});
        // Heart: classic parametric outline sampled to a polygon (+Y is image-down, hence the flip)
        {
            std::vector<std::array<float, 2>> Heart;
            for (int I = 0; I < 72; ++I)
            {
                const float T = float(I) / 72.f * 6.2831853f;
                const float S = std::sin(T);
                const float Hx = 16.f * S * S * S;
                const float Hy = 13.f * std::cos(T) - 5.f * std::cos(2 * T) - 2.f * std::cos(3 * T) - std::cos(4 * T);
                Heart.push_back({Hx / 19.f, -Hy / 19.f});
            }
            Defs.push_back({"heart", "Heart", PolygonFieldFunction(Heart)});
        }
        // Lightning bolt
        Defs.push_back({"bolt", "Bolt", PolygonFieldFunction({
            {0.18f, -0.85f}, {-0.32f, 0.08f}, {-0.02f, 0.08f}, {-0.18f, 0.85f}, {0.34f, -0.12f}, {0.03f, -0.12f}})});
        // Hex badge: hexagon ring
        {
            std::vector<std::array<float, 2>> Outer, Inner;
            for (int I = 0; I < 6; ++I)
            {
                const float Angle = 0.5235988f + I * 1.0471976f;
                Outer.push_back({std::cos(Angle) * 0.82f, std::sin(Angle) * 0.82f});
                Inner.push_back({std::cos(Angle) * 0.60f, std::sin(Angle) * 0.60f});
            }
            Defs.push_back({"hex-badge", "Hex badge",
                DifferenceField(PolygonFieldFunction(Outer), PolygonFieldFunction(Inner))});
        }
        // Warning: triangle ring
        Defs.push_back({"warning", "Warning", DifferenceField(
            PolygonFieldFunction({{0.f, -0.80f}, {0.78f, 0.62f}, {-0.78f, 0.62f}}),
            PolygonFieldFunction({{0.f, -0.48f}, {0.50f, 0.44f}, {-0.50f, 0.44f}}))});
        // Splatter: a union of deterministic blobs
        Defs.push_back({"splatter", "Splatter", [](float X, float Y, float PixelInField) {
            static constexpr std::array<std::array<float, 3>, 9> Blobs{{
                {0.f, 0.f, .42f}, {-.38f, -.20f, .20f}, {.34f, -.30f, .16f}, {.42f, .24f, .13f}, {-.30f, .34f, .15f},
                {.08f, -.50f, .10f}, {-.55f, .05f, .08f}, {.60f, -.05f, .07f}, {-.12f, .56f, .09f}}};
            float Best = 1e9f;
            for (const auto &Blob : Blobs) Best = std::min(Best, Length2(X - Blob[0], Y - Blob[1]) - Blob[2]);
            return CoverageFromDistance(Best / PixelInField);
        }});
        // Paw print: elliptical pad plus four toes
        Defs.push_back({"paw", "Paw print", [](float X, float Y, float PixelInField) {
            float Best = (Length2(X / 0.46f, (Y - 0.28f) / 0.34f) - 1.f) * 0.34f; // scaled-space ellipse distance, approx.
            static constexpr std::array<std::array<float, 3>, 4> Toes{{
                {-.48f, -.18f, .15f}, {-.17f, -.36f, .16f}, {.17f, -.36f, .16f}, {.48f, -.18f, .15f}}};
            for (const auto &Toe : Toes) Best = std::min(Best, Length2(X - Toe[0], Y - Toe[1]) - Toe[2]);
            return CoverageFromDistance(Best / PixelInField);
        }});
        // Sunburst: twelve alternating rays inside a disc with a solid heart
        Defs.push_back({"sunburst", "Sunburst", [](float X, float Y, float PixelInField) {
            const float Radius = Length2(X, Y);
            const float Disc = CoverageFromDistance((Radius - 0.82f) / PixelInField);
            const float Sector = std::fmod(std::atan2(Y, X) * 1.9098593f + 12.f, 2.f);
            return Disc * (Radius < 0.20f ? 1.f : (Sector < 1.f ? 1.f : 0.16f));
        }});
        // Shield: flat top, sides sweeping to a bottom point
        Defs.push_back({"shield", "Shield", PolygonFieldFunction({
            {-0.55f, -0.78f}, {0.55f, -0.78f}, {0.55f, 0.0f}, {0.42f, 0.42f},
            {0.f, 0.82f}, {-0.42f, 0.42f}, {-0.55f, 0.0f}})});
        // Cog: eight-tooth gear (radial sector approximation) with a centre bore
        Defs.push_back({"cog", "Cog", [](float X, float Y, float PixelInField) {
            const float Radius = Length2(X, Y);
            const float Sector = std::fmod(std::atan2(Y, X) * 1.2732395f + 8.f, 1.f); // 8 teeth around 2pi
            const float Outer = Sector < 0.5f ? 0.84f : 0.64f;
            const float Body = Radius - Outer;
            const float Bore = Radius - 0.26f;
            return CoverageFromDistance(std::max(Body, -Bore) / PixelInField);
        }});
        // Droplet: round belly with straight tangents rising to a tip
        {
            std::vector<std::array<float, 2>> Drop{{0.f, -0.88f}};
            for (int I = 0; I <= 40; ++I)
            {
                const float Angle = (-50.f + I * 7.f) * 0.0174533f; // -50deg .. 230deg, the long way under
                Drop.push_back({std::cos(Angle) * 0.56f, 0.28f + std::sin(Angle) * 0.56f});
            }
            Defs.push_back({"droplet", "Droplet", PolygonFieldFunction(Drop)});
        }
        // Leaf: vesica (two-disc intersection) on the diagonal with a centre vein slit
        Defs.push_back({"leaf", "Leaf", [](float X, float Y, float PixelInField) {
            const float Lens = std::max(Length2(X + 0.30f, Y + 0.30f) - 0.95f, Length2(X - 0.30f, Y - 0.30f) - 0.95f);
            const float Vein = std::fabs(X + Y) * 0.7071068f - 0.018f;
            return CoverageFromDistance(std::max(Lens, -Vein) / PixelInField);
        }});
        // Diamond: gem silhouette with a bright table, slanted crown facets and a split pavilion
        Defs.push_back({"diamond", "Diamond", [](float X, float Y, float PixelInField) {
            static const std::vector<std::array<float, 2>> Gem{
                {-0.45f, -0.55f}, {0.45f, -0.55f}, {0.80f, -0.10f}, {0.f, 0.75f}, {-0.80f, -0.10f}};
            const float Edge = PolygonEdgeDistance(X, Y, Gem) / PixelInField;
            const float Base = CoverageFromDistance(PointInPolygon(X, Y, Gem) ? -Edge : Edge);
            float Facet = 1.f;
            if (Y > -0.10f) Facet = std::fabs(X) < 0.30f * (0.75f - Y) / 0.85f ? 0.45f : 0.78f; // pavilion facets
            else if (std::fabs(X) > 0.45f) Facet = 0.80f;                                       // crown slants
            return Base * Facet;
        }});
        // Radiation trefoil: centre disc plus three 60-degree annular blades (top, lower left/right)
        Defs.push_back({"radiation", "Radiation", [](float X, float Y, float PixelInField) {
            const float Radius = Length2(X, Y);
            const float Angle = std::atan2(Y, X);
            float Nearest = 1e9f;
            for (float Centre : {-1.5707963f, 0.5235988f, 2.6179939f})
            {
                float Delta = std::fabs(Angle - Centre);
                if (Delta > 3.1415927f) Delta = 6.2831853f - Delta;
                Nearest = std::min(Nearest, Delta);
            }
            const float Band = std::max(Radius - 0.82f, 0.30f - Radius);
            const float Blade = std::max(Band, (Nearest - 0.5235988f) * Radius);
            return CoverageFromDistance(std::min(Radius - 0.18f, Blade) / PixelInField);
        }});
        // Target: alternating concentric bands around a solid bullseye
        Defs.push_back({"target", "Target", [](float X, float Y, float PixelInField) {
            const float Radius = Length2(X, Y);
            const float Disc = CoverageFromDistance((Radius - 0.82f) / PixelInField);
            const int Band = int(Radius / 0.205f);
            return Disc * (Band % 2 == 0 ? 1.f : 0.22f);
        }});
        // Chevrons: a bright rank chevron over a dimmer echo
        Defs.push_back({"chevrons", "Chevrons", UnionField(
            PolygonFieldFunction({{-0.72f, 0.0f}, {0.f, -0.72f}, {0.72f, 0.0f},
                {0.72f, 0.22f}, {0.f, -0.50f}, {-0.72f, 0.22f}}),
            PolygonFieldFunction({{-0.72f, 0.50f}, {0.f, -0.22f}, {0.72f, 0.50f},
                {0.72f, 0.72f}, {0.f, 0.0f}, {-0.72f, 0.72f}}),
            0.6f)});
        // Snowflake: six-fold folded spokes with two branch pairs, joined by a centre disc
        Defs.push_back({"snowflake", "Snowflake", [](float X, float Y, float PixelInField) {
            const auto Segment = [](float Px, float Py, float Ax, float Ay, float Bx, float By) {
                const float Dx = Bx - Ax, Dy = By - Ay;
                const float T = std::clamp(((Px - Ax) * Dx + (Py - Ay) * Dy) / (Dx * Dx + Dy * Dy), 0.f, 1.f);
                return Length2(Px - (Ax + Dx * T), Py - (Ay + Dy * T));
            };
            const float Radius = Length2(X, Y);
            const float Folded = std::fmod(std::atan2(Y, X) + 3.1415927f, 1.0471976f) - 0.5235988f;
            const float LocalX = Radius * std::cos(Folded), LocalY = std::fabs(Radius * std::sin(Folded));
            float Best = Segment(LocalX, LocalY, 0.06f, 0.f, 0.82f, 0.f) - 0.045f;
            Best = std::min(Best, Segment(LocalX, LocalY, 0.44f, 0.f, 0.67f, 0.193f) - 0.035f);
            Best = std::min(Best, Segment(LocalX, LocalY, 0.64f, 0.f, 0.78f, 0.116f) - 0.030f);
            Best = std::min(Best, Radius - 0.11f);
            return CoverageFromDistance(Best / PixelInField);
        }});
        return Defs;
    }();
    return Definitions;
}

StampLibraryEntry RenderPreset(const PresetDefinition &Definition, int Extent)
{
    StampLibraryEntry Entry;
    Entry.Id = std::string("builtin:") + Definition.Slug;
    Entry.Name = Definition.Name;
    Entry.Builtin = true;
    Entry.Tintable = true;
    Entry.Width = Entry.Height = Extent;
    Entry.Rgba.resize(size_t(Extent) * Extent * 4);
    const float Centre = (Extent - 1) * 0.5f;
    const float PixelInField = 2.f / Extent;
    for (int Y = 0; Y < Extent; ++Y)
        for (int X = 0; X < Extent; ++X)
        {
            const float Coverage = Definition.Field((X - Centre) / Centre, (Y - Centre) / Centre, PixelInField);
            uint8_t *Target = &Entry.Rgba[(size_t(Y) * Extent + X) * 4];
            Target[0] = Target[1] = Target[2] = 255;
            Target[3] = uint8_t(std::clamp(Coverage, 0.f, 1.f) * 255.f + 0.5f);
        }
    return Entry;
}
} // namespace

std::vector<StampLibraryEntry> StampLibrary::BuiltinPresets()
{
    std::vector<StampLibraryEntry> Presets;
    for (const auto &Definition : PresetDefinitions())
        Presets.push_back(RenderPreset(Definition, PresetExtent));
    return Presets;
}

bool StampLibrary::RenderBuiltinAt(const std::string &Id, int Extent, StampLibraryEntry &Out)
{
    if (Id.compare(0, 8, "builtin:") != 0 || Extent < 16 || Extent > MaxStampExtent) return false;
    for (const auto &Definition : PresetDefinitions())
        if (Id == std::string("builtin:") + Definition.Slug)
        {
            Out = RenderPreset(Definition, Extent);
            return true;
        }
    return false;
}

const StampLibraryEntry *StampLibrary::Find(const std::string &Id) const noexcept
{
    for (const auto &Entry : LibraryEntries)
        if (Entry.Id == Id) return &Entry;
    return nullptr;
}

size_t StampLibrary::DiskCount() const noexcept
{
    size_t Count = 0;
    for (const auto &Entry : LibraryEntries)
        if (!Entry.Builtin) ++Count;
    return Count;
}

void StampLibrary::Reload(const std::filesystem::path &Directory)
{
    LibraryDirectory = Directory;
    LibraryEntries = BuiltinPresets();
    if (Directory.empty()) return;
    std::error_code Failure;
    if (!std::filesystem::is_directory(Directory, Failure)) return;
    std::vector<std::filesystem::path> Files;
    for (const auto &Item : std::filesystem::directory_iterator(Directory, Failure))
    {
        if (Failure) break;
        if (Item.is_regular_file(Failure) && HasPngExtension(Item.path())) Files.push_back(Item.path());
    }
    std::sort(Files.begin(), Files.end());
    for (const auto &File : Files)
    {
        const std::vector<uint8_t> Bytes = ReadFileBytes(File, 32u * 1024u * 1024u);
        if (Bytes.empty()) continue;
        int Width = 0, Height = 0, Channels = 0;
        uint8_t *Pixels = stbi_load_from_memory(Bytes.data(), int(Bytes.size()), &Width, &Height, &Channels, 4);
        if (!Pixels) continue;
        const bool Usable = Width > 0 && Height > 0 && Width <= MaxStampExtent && Height <= MaxStampExtent;
        if (Usable)
        {
            StampLibraryEntry Entry;
            Entry.Id = "disk:" + File.filename().string();
            Entry.Name = DisplayName(File.stem().string());
            Entry.Width = Width;
            Entry.Height = Height;
            Entry.Rgba.assign(Pixels, Pixels + size_t(Width) * Height * 4);
            Entry.File = File;
            // 🖤 Alpha-art detection: icon-style stamps keep their art in the alpha channel over a
            // uniform near-black/near-white RGB matte - painted RGBA-straight they would always stamp
            // that matte colour (usually solid black) no matter the colour they were placed with.
            // They are really tintable art like the white presets: normalize RGB to white so the brush
            // colour drives them. Vector-backed stamps (sidecar present) are exempt - their colours
            // are exactly what was authored in the Stamp workspace.
            const bool HasSidecar = std::filesystem::exists(
                File.parent_path() / (File.stem().string() + ".stamp.toml"), Failure);
            if (!HasSidecar)
            {
                const size_t Total = size_t(Width) * Height;
                const size_t Stride = std::max<size_t>(1, Total / 8192);
                double Sum[3]{0, 0, 0};
                size_t Solid = 0;
                for (size_t At = 0; At < Total; At += Stride)
                {
                    const uint8_t *Pixel = &Entry.Rgba[At * 4];
                    if (Pixel[3] < 128) continue;
                    ++Solid;
                    for (int Channel = 0; Channel < 3; ++Channel) Sum[Channel] += Pixel[Channel] / 255.0;
                }
                if (Solid >= 64)
                {
                    const double Mean[3]{Sum[0] / Solid, Sum[1] / Solid, Sum[2] / Solid};
                    const double Luma = (Mean[0] + Mean[1] + Mean[2]) / 3.0;
                    if (Luma < 0.08 || Luma > 0.92)
                    {
                        size_t Within = 0;
                        for (size_t At = 0; At < Total; At += Stride)
                        {
                            const uint8_t *Pixel = &Entry.Rgba[At * 4];
                            if (Pixel[3] < 128) continue;
                            double Worst = 0;
                            for (int Channel = 0; Channel < 3; ++Channel)
                                Worst = std::max(Worst, std::fabs(Pixel[Channel] / 255.0 - Mean[Channel]));
                            if (Worst <= 0.10) ++Within;
                        }
                        if (double(Within) >= 0.97 * double(Solid))
                        {
                            Entry.Tintable = true;
                            for (size_t At = 0; At < Total; ++At)
                            {
                                uint8_t *Pixel = &Entry.Rgba[At * 4];
                                if (Pixel[3] > 0) Pixel[0] = Pixel[1] = Pixel[2] = 255;
                            }
                        }
                    }
                }
            }
            LibraryEntries.push_back(std::move(Entry));
        }
        stbi_image_free(Pixels);
    }
}

bool StampLibrary::SaveRasterToFile(const std::filesystem::path &Target, int Width, int Height,
    const std::vector<uint8_t> &Rgba, const std::string &SourceToml,
    std::string &OutId, std::string &Diagnostic)
{
    if (Target.empty()) { Diagnostic = "No stamp file target is configured."; return false; }
    if (Width <= 0 || Height <= 0 || Width > MaxStampExtent || Height > MaxStampExtent ||
        Rgba.size() != size_t(Width) * Height * 4)
    {
        Diagnostic = "Stamp raster is empty or out of bounds.";
        return false;
    }
    std::error_code Failure;
    std::filesystem::create_directories(Target.parent_path(), Failure);
    if (Failure) { Diagnostic = "Could not create the stamp library directory."; return false; }
    if (!stbi_write_png(Target.string().c_str(), Width, Height, 4, Rgba.data(), Width * 4))
    {
        Diagnostic = "Could not write the stamp PNG.";
        return false;
    }
    if (!SourceToml.empty())
    {
        const std::filesystem::path Sidecar = Target.parent_path() / (Target.stem().string() + ".stamp.toml");
        std::ofstream Output(Sidecar, std::ios::binary | std::ios::trunc);
        if (Output) Output << SourceToml; // sidecar is best-effort provenance; the PNG is the stamp
    }
    OutId = "disk:" + Target.filename().string();
    return true;
}

bool StampLibrary::SaveRaster(const std::filesystem::path &Directory, const std::string &Name,
    int Width, int Height, const std::vector<uint8_t> &Rgba, const std::string &SourceToml,
    std::string &OutId, std::string &Diagnostic)
{
    if (Directory.empty()) { Diagnostic = "No stamp library directory is configured."; return false; }
    std::error_code Failure;
    const std::string Base = Slugify(Name);
    std::filesystem::path Target;
    for (int Version = 1; Version <= 99; ++Version)
    {
        const std::string Stem = Version == 1 ? Base : Base + "-" + std::to_string(Version);
        Target = Directory / (Stem + ".png");
        if (!std::filesystem::exists(Target, Failure)) break;
        if (Version == 99) { Diagnostic = "Too many stamps share this name."; return false; }
    }
    return SaveRasterToFile(Target, Width, Height, Rgba, SourceToml, OutId, Diagnostic);
}

std::array<float, 4> StampLibrary::Sample(const StampLibraryEntry &Entry, float U, float V) noexcept
{
    if (Entry.Width <= 0 || Entry.Height <= 0 || Entry.Rgba.size() != size_t(Entry.Width) * Entry.Height * 4)
        return {1.f, 1.f, 1.f, 0.f};
    const float X = std::clamp(U, 0.f, 1.f) * (Entry.Width - 1);
    const float Y = std::clamp(V, 0.f, 1.f) * (Entry.Height - 1);
    const int X0 = int(std::floor(X)), Y0 = int(std::floor(Y));
    const int X1 = std::min(Entry.Width - 1, X0 + 1), Y1 = std::min(Entry.Height - 1, Y0 + 1);
    const float Fx = X - X0, Fy = Y - Y0;
    const auto At = [&](int Px, int Py, int Channel) noexcept {
        return Entry.Rgba[(size_t(Py) * Entry.Width + Px) * 4 + Channel] / 255.f;
    };
    std::array<float, 4> Result{};
    for (int Channel = 0; Channel < 4; ++Channel)
    {
        const float Top = At(X0, Y0, Channel) + (At(X1, Y0, Channel) - At(X0, Y0, Channel)) * Fx;
        const float Bottom = At(X0, Y1, Channel) + (At(X1, Y1, Channel) - At(X0, Y1, Channel)) * Fx;
        Result[size_t(Channel)] = Top + (Bottom - Top) * Fy;
    }
    return Result;
}

StampPbrSample StampLibrary::SamplePbr(const StampLibraryEntry &Entry, float U, float V) noexcept
{
    StampPbrSample Out{};
    const int Extent = Entry.PbrExtent;
    const size_t Count = Extent > 0 ? size_t(Extent) * size_t(Extent) : 0;
    if (!Entry.HasPbrMaps || Extent <= 0 || Entry.Coverage.size() != Count) return Out;
    const float X = std::clamp(U, 0.f, 1.f) * float(Extent - 1);
    const float Y = std::clamp(V, 0.f, 1.f) * float(Extent - 1);
    const int X0 = int(std::floor(X)), Y0 = int(std::floor(Y));
    const int X1 = std::min(Extent - 1, X0 + 1), Y1 = std::min(Extent - 1, Y0 + 1);
    const float Fx = X - float(X0), Fy = Y - float(Y0);
    const size_t I00 = size_t(Y0) * size_t(Extent) + size_t(X0);
    const size_t I10 = size_t(Y0) * size_t(Extent) + size_t(X1);
    const size_t I01 = size_t(Y1) * size_t(Extent) + size_t(X0);
    const size_t I11 = size_t(Y1) * size_t(Extent) + size_t(X1);
    const auto Bilerp = [&](const std::vector<float> &Field, float Fallback) noexcept {
        if (Field.size() != Count) return Fallback;
        const float Top = Field[I00] + (Field[I10] - Field[I00]) * Fx;
        const float Bottom = Field[I01] + (Field[I11] - Field[I01]) * Fx;
        return Top + (Bottom - Top) * Fy;
    };
    Out.Coverage = Bilerp(Entry.Coverage, 0.f);
    Out.Height = Bilerp(Entry.HeightMap, 0.f);
    Out.HeightMask = Bilerp(Entry.HeightMask, 0.f);
    Out.NormalX = Bilerp(Entry.NormalX, 0.f);
    Out.NormalY = Bilerp(Entry.NormalY, 0.f);
    Out.NormalZ = Bilerp(Entry.NormalZ, 1.f);
    const float NLen = std::sqrt(Out.NormalX * Out.NormalX + Out.NormalY * Out.NormalY + Out.NormalZ * Out.NormalZ);
    if (NLen > 1e-5f) { Out.NormalX /= NLen; Out.NormalY /= NLen; Out.NormalZ /= NLen; }
    Out.NormalMask = Bilerp(Entry.NormalMask, 0.f);
    Out.Roughness = Bilerp(Entry.Roughness, .7f);
    Out.RoughnessMask = Bilerp(Entry.RoughnessMask, 0.f);
    Out.Metalness = Bilerp(Entry.Metalness, 0.f);
    Out.MetalnessMask = Bilerp(Entry.MetalnessMask, 0.f);
    Out.Opacity = Bilerp(Entry.Opacity, 0.f);
    Out.Occlusion = Bilerp(Entry.Occlusion, 1.f);
    Out.OcclusionMask = Bilerp(Entry.OcclusionMask, 0.f);
    Out.Transmission = Bilerp(Entry.Transmission, 0.f);
    Out.TransmissionMask = Bilerp(Entry.TransmissionMask, 0.f);
    Out.Clearcoat = Bilerp(Entry.Clearcoat, 0.f);
    Out.ClearcoatMask = Bilerp(Entry.ClearcoatMask, 0.f);
    return Out;
}
} // namespace Frontier
