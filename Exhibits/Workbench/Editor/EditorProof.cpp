//============================================================================================================================================
//                                                      EDITORPROOF.CPP
//============================================================================================================================================
// 🧩 Headless visual proof — drives EditorHost through the engine's tick order over the showcase mirror, rasterises
//    the last tick with a dependency-free CPU rasteriser, and gates the trapezoid sheet, the seated theme tints,
//    and the four faces. No Vulkan, no GLFW, no window.

#ifndef FRONTIER_DEVELOPMENT
#error "the proof must define FRONTIER_DEVELOPMENT, or the editor records nothing and every gate fails"
#endif
#include <imgui.h>

#include "EditorHost.h"
#include "TypefaceRegistry.h"
#include "PngWriteCounterpart.h"
#include "CpuReSTIRTrace.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static_assert(sizeof(ImDrawIdx) == 2u, "the rasteriser below walks 16-bit indices");

namespace {

constexpr int kWidth  = 1280;
constexpr int kHeight = 720;

// Seated-tab tint in bytes: EditorHost::ApplyTheme seats #121212 (the sheet's seamless rule).
constexpr unsigned char kSeated[3] = { 18u, 18u, 18u };
constexpr unsigned char kGround[3] = { 5u, 5u, 5u };   // Frontier ground #050505

struct Rgba
{
    float R, G, B, A;
};

Rgba UnpackColour(uint32_t Packed) noexcept
{
    return { static_cast<float>(Packed & 0xFFu) / 255.0f,
             static_cast<float>((Packed >> 8) & 0xFFu) / 255.0f,
             static_cast<float>((Packed >> 16) & 0xFFu) / 255.0f,
             static_cast<float>((Packed >> 24) & 0xFFu) / 255.0f };
}

void OverlayPixel(unsigned char* Pixel, Rgba Over) noexcept
{
    const float Keep = 1.0f - Over.A;
    Pixel[0] = static_cast<unsigned char>(Over.R * 255.0f * Over.A + static_cast<float>(Pixel[0]) * Keep + 0.5f);
    Pixel[1] = static_cast<unsigned char>(Over.G * 255.0f * Over.A + static_cast<float>(Pixel[1]) * Keep + 0.5f);
    Pixel[2] = static_cast<unsigned char>(Over.B * 255.0f * Over.A + static_cast<float>(Pixel[2]) * Keep + 0.5f);
}

Rgba SampleGlyphSheet(const unsigned char* GlyphSheet, int GlyphSheetWidth, int GlyphSheetHeight, float U, float V) noexcept
{
    int X = static_cast<int>(U * static_cast<float>(GlyphSheetWidth));
    int Y = static_cast<int>(V * static_cast<float>(GlyphSheetHeight));
    if (X < 0) X = 0; if (X >= GlyphSheetWidth) X = GlyphSheetWidth - 1;
    if (Y < 0) Y = 0; if (Y >= GlyphSheetHeight) Y = GlyphSheetHeight - 1;
    const unsigned char* Texel = GlyphSheet + (static_cast<size_t>(Y) * static_cast<size_t>(GlyphSheetWidth) + static_cast<size_t>(X)) * 4u;
    return { static_cast<float>(Texel[0]) / 255.0f, static_cast<float>(Texel[1]) / 255.0f,
             static_cast<float>(Texel[2]) / 255.0f, static_cast<float>(Texel[3]) / 255.0f };
}

float EdgeWeight(float Ax, float Ay, float Bx, float By, float Px, float Py) noexcept
{
    return (Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax);
}

// The traced scene bound to the viewport panel: pointer, size, and the texture id the panel was handed.
const unsigned char* gSceneRgba  = nullptr;
uint32_t             gSceneW     = 0u;
uint32_t             gSceneH     = 0u;
ImTextureID          gSceneTexId = static_cast<ImTextureID>(0);

// Rasterises one draw list over the pixels. Textured the way every ImGui backend is: the glyph sheet modulated
//    by the corner colours, composited over what is already there.
void RasterizeList(const ImDrawList* List, const unsigned char* GlyphSheet, int GlyphSheetWidth, int GlyphSheetHeight,
                   unsigned char* Pixels, ImVec2 Origin, ImVec2 PixelScale) noexcept
{
    const ImDrawVert* Corners = List->VtxBuffer.Data;
    const ImDrawIdx*  Order   = List->IdxBuffer.Data;
    for (int Command = 0; Command < List->CmdBuffer.Size; ++Command)
    {
        const ImDrawCmd* Cmd = &List->CmdBuffer[Command];
        // The viewport panel binds the traced scene as its own texture (an RGBA8 pointer in this CPU harness,
        //    a descriptor set in the Vulkan build); everything else samples the glyph sheet.
        const ImTextureID CmdTex = Cmd->TexRef._TexData != nullptr ? static_cast<ImTextureID>(0) : Cmd->TexRef._TexID;
        const bool SceneTex = CmdTex != static_cast<ImTextureID>(0) && CmdTex == gSceneTexId;
        const unsigned char* Sheet = SceneTex ? gSceneRgba : GlyphSheet;
        const int SheetW = SceneTex ? static_cast<int>(gSceneW) : GlyphSheetWidth;
        const int SheetH = SceneTex ? static_cast<int>(gSceneH) : GlyphSheetHeight;
        int ScissorLeft   = static_cast<int>((Cmd->ClipRect.x - Origin.x) * PixelScale.x);
        int ScissorTop    = static_cast<int>((Cmd->ClipRect.y - Origin.y) * PixelScale.y);
        int ScissorRight  = static_cast<int>((Cmd->ClipRect.z - Origin.x) * PixelScale.x);
        int ScissorBottom = static_cast<int>((Cmd->ClipRect.w - Origin.y) * PixelScale.y);
        if (ScissorLeft < 0)
            ScissorLeft = 0;
        if (ScissorRight > kWidth)
            ScissorRight = kWidth;
        if (ScissorTop < 0)
            ScissorTop = 0;
        if (ScissorBottom > kHeight)
            ScissorBottom = kHeight;

        for (unsigned int I = 0u; I < Cmd->ElemCount; I += 3u)
        {
            const ImDrawVert& A = Corners[Order[Cmd->IdxOffset + I] + Cmd->VtxOffset];
            const ImDrawVert& B = Corners[Order[Cmd->IdxOffset + I + 1u] + Cmd->VtxOffset];
            const ImDrawVert& C = Corners[Order[Cmd->IdxOffset + I + 2u] + Cmd->VtxOffset];
            const float SignedArea = EdgeWeight(A.pos.x, A.pos.y, B.pos.x, B.pos.y, C.pos.x, C.pos.y);
            if (SignedArea == 0.0f)
                continue;

            int LoX = static_cast<int>(std::floor(std::fmin(A.pos.x, std::fmin(B.pos.x, C.pos.x))));
            int HiX = static_cast<int>(std::ceil(std::fmax(A.pos.x, std::fmax(B.pos.x, C.pos.x))));
            int LoY = static_cast<int>(std::floor(std::fmin(A.pos.y, std::fmin(B.pos.y, C.pos.y))));
            int HiY = static_cast<int>(std::ceil(std::fmax(A.pos.y, std::fmax(B.pos.y, C.pos.y))));
            if (LoX < ScissorLeft) LoX = ScissorLeft; if (HiX > ScissorRight) HiX = ScissorRight;
            if (LoY < ScissorTop) LoY = ScissorTop; if (HiY > ScissorBottom) HiY = ScissorBottom;

            const Rgba TintedA = UnpackColour(A.col);
            const Rgba TintedB = UnpackColour(B.col);
            const Rgba TintedC = UnpackColour(C.col);
            const float InverseArea = 1.0f / SignedArea;
            for (int Y = LoY; Y < HiY; ++Y)
            {
                for (int X = LoX; X < HiX; ++X)
                {
                    const float Px = static_cast<float>(X) + 0.5f;
                    const float Py = static_cast<float>(Y) + 0.5f;
                    const float W0 = EdgeWeight(B.pos.x, B.pos.y, C.pos.x, C.pos.y, Px, Py) * InverseArea;
                    const float W1 = EdgeWeight(C.pos.x, C.pos.y, A.pos.x, A.pos.y, Px, Py) * InverseArea;
                    const float W2 = EdgeWeight(A.pos.x, A.pos.y, B.pos.x, B.pos.y, Px, Py) * InverseArea;
                    if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
                        continue;
                    const float U = W0 * A.uv.x + W1 * B.uv.x + W2 * C.uv.x;
                    const float V = W0 * A.uv.y + W1 * B.uv.y + W2 * C.uv.y;
                    const Rgba Glyph = SampleGlyphSheet(Sheet, SheetW, SheetH, U, V);
                    const Rgba Tinted = { (W0 * TintedA.R + W1 * TintedB.R + W2 * TintedC.R) * Glyph.R,
                                          (W0 * TintedA.G + W1 * TintedB.G + W2 * TintedC.G) * Glyph.G,
                                          (W0 * TintedA.B + W1 * TintedB.B + W2 * TintedC.B) * Glyph.B,
                                          (W0 * TintedA.A + W1 * TintedB.A + W2 * TintedC.A) * Glyph.A };
                    OverlayPixel(&Pixels[(static_cast<size_t>(Y) * kWidth + static_cast<size_t>(X)) * 3u], Tinted);
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      CORNELL MIRROR
//------------------------------------------------------------------------------------------------------------------------

// The engine's Cornell feed at a representative instant: the roster repeats GameExecution's roster exactly,
//    and the sheet figures repeat the solvers' startup figures. Main Camera (index 15) is the picked instance.

struct MirrorEntry
{
    const char*                   Label;
    Frontier::EditorInstanceCategory    Category;
    uint32_t                      Depth;
    uint32_t                      Kids;
    float                         Tint[3];
    int                           Material;
    float                         At[3];
    float                         RotZ;
    bool                          Dynamic;
    Frontier::EditorGlyph         Glyph;
    Frontier::EditorNarrowing     Narrowing;
    const char*                   Meta;
    const char*                   Tag;
    Frontier::EditorStanding      Standing;
    const char*                   Note;
    bool                          Pinned;
    bool                          Shut;
};

constexpr MirrorEntry kMirrorEntries[] =
{
    { "World", Frontier::EditorInstanceCategory::Folder, 0u, 10u, { 0.353f, 0.663f, 1.000f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Globe, Frontier::EditorNarrowing::Auto, "", "", Frontier::EditorStanding::Auto, "", true, false },
    { "Atmosphere", Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.353f, 0.663f, 1.000f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Atmosphere, Frontier::EditorNarrowing::Sky, "AM 10.2", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Sun", Frontier::EditorInstanceCategory::Light, 1u, 0u, { 1.000f, 0.706f, 0.329f }, 3, { 0.00f, 0.00f, 0.000f }, 0.0f, true, Frontier::EditorGlyph::Sun, Frontier::EditorNarrowing::Lights, "5.2\xc2\xb0", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Sky", Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.404f, 0.910f, 0.976f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Sky, Frontier::EditorNarrowing::Sky, "5.08 kcd", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Stars", Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.769f, 0.710f, 0.992f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, true, Frontier::EditorGlyph::Stars, Frontier::EditorNarrowing::Sky, "mag -1.3", "", Frontier::EditorStanding::Warn, "Washed out by sky", false, false },
    { "Wind", Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.655f, 0.953f, 0.816f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, true, Frontier::EditorGlyph::Wind, Frontier::EditorNarrowing::Sky, "4.2 m/s SW", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Height Fog", Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.624f, 0.690f, 0.753f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Fog, Frontier::EditorNarrowing::Sky, "391 m", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Atmospheric Fog", Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.561f, 0.722f, 0.847f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::AerialFog, Frontier::EditorNarrowing::Sky, "56 km", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Local Volumetric Fog", Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.788f, 0.839f, 0.886f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::VolumeFog, Frontier::EditorNarrowing::Sky, "36Ã36 m", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Cloud Layer", Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.875f, 0.902f, 0.933f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, true, Frontier::EditorGlyph::Cloud, Frontier::EditorNarrowing::Sky, "4/8", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Clouds", Frontier::EditorInstanceCategory::Geometry, 1u, 1u, { 0.945f, 0.961f, 0.976f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, true, Frontier::EditorGlyph::VolumeClouds, Frontier::EditorNarrowing::Sky, "Cumulus \xc2\xb7 45%", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Precipitation", Frontier::EditorInstanceCategory::Geometry, 2u, 0u, { 0.788f, 0.839f, 0.886f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, true, Frontier::EditorGlyph::Rain, Frontier::EditorNarrowing::Sky, "Rain 12 mm/h", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Local Cloud", Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.910f, 0.933f, 0.965f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::LocalCloud, Frontier::EditorNarrowing::Sky, "180Ã140 m", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Moons", Frontier::EditorInstanceCategory::Folder, 1u, 1u, { 0.875f, 0.902f, 0.961f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, true, Frontier::EditorGlyph::Moon, Frontier::EditorNarrowing::Bodies, "1/4", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Luna", Frontier::EditorInstanceCategory::Geometry, 2u, 0u, { 0.875f, 0.902f, 0.961f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, true, Frontier::EditorGlyph::Moon, Frontier::EditorNarrowing::Bodies, "62%", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Ground Plane", Frontier::EditorInstanceCategory::Geometry, 0u, 0u, { 0.886f, 0.910f, 0.941f }, 0, { 0.00f, 0.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Plane, Frontier::EditorNarrowing::Geometry, "1200 m", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Height Field", Frontier::EditorInstanceCategory::Geometry, 0u, 0u, { 0.561f, 0.702f, 0.420f }, 0, { 0.00f, 0.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Globe, Frontier::EditorNarrowing::Geometry, "12 m", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Lights", Frontier::EditorInstanceCategory::Folder, 0u, 2u, { 1.000f, 0.824f, 0.478f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Bulb, Frontier::EditorNarrowing::Auto, "", "", Frontier::EditorStanding::Auto, "", true, false },
    { "Point Light", Frontier::EditorInstanceCategory::Light, 1u, 0u, { 1.000f, 0.824f, 0.478f }, 3, { 0.00f, 2.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Bulb, Frontier::EditorNarrowing::Lights, "14 cd", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Spot Light", Frontier::EditorInstanceCategory::Light, 1u, 0u, { 0.624f, 0.816f, 1.000f }, 3, { 0.00f, 2.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Flare, Frontier::EditorNarrowing::Lights, "26\xc2\xb0", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Camera", Frontier::EditorInstanceCategory::Camera, 0u, 1u, { 0.204f, 0.780f, 0.349f }, -1, { 0.00f, 2.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Camera, Frontier::EditorNarrowing::Camera, "72\xc2\xb0", "", Frontier::EditorStanding::Auto, "", false, false },
    { "Post Process", Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 1.000f, 0.541f, 0.396f }, -1, { 0.00f, 0.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Effects, Frontier::EditorNarrowing::Camera, "+0.4 EV", "Comp", Frontier::EditorStanding::Auto, "", false, false },
    { "Cine Camera", Frontier::EditorInstanceCategory::Camera, 0u, 0u, { 0.369f, 0.918f, 0.831f }, -1, { 0.00f, 2.00f, 0.000f }, 0.0f, false, Frontier::EditorGlyph::Aperture, Frontier::EditorNarrowing::Camera, "35 mm", "", Frontier::EditorStanding::Auto, "", false, false },
};

constexpr uint32_t kMirrorEntryCount = sizeof(kMirrorEntries) / sizeof(kMirrorEntries[0]);

struct MirrorMaterial
{
    float Albedo[3];
    float Emission;
    float Rough;
    float Metal;
};

constexpr MirrorMaterial kMirrorMats[9] =
{
    { { 0.75f, 0.75f, 0.75f },  0.0f, 0.50f, 0.0f },
    { { 0.85f, 0.12f, 0.12f },  0.0f, 0.50f, 0.0f },
    { { 0.12f, 0.85f, 0.15f },  0.0f, 0.50f, 0.0f },
    { { 1.00f, 1.00f, 1.00f }, 32.0f, 0.10f, 0.0f },
    { { 0.78f, 0.78f, 0.78f },  0.0f, 0.40f, 0.0f },
    { { 0.78f, 0.78f, 0.78f },  0.0f, 0.40f, 0.0f },
    { { 0.82f, 0.78f, 0.72f },  0.0f, 0.25f, 0.0f },
    { { 0.35f, 0.45f, 0.70f },  0.0f, 0.40f, 0.0f },
    { { 0.78f, 0.55f, 0.25f },  0.0f, 0.35f, 0.0f },
};

// The rows the ReSTIR viewport renders: one per object span of the traced scene (soil plus the hundred objects),
//    seated under a "Showcase" folder, the fly camera under "Cameras", then the
//    celestial page rows exactly as before. This is the same roster GameExecution seats from the level.
uint32_t FillMirrorInstances(Frontier::EditorInstance* Instances, const Frontier::ProjectZero::RayTracingSolver& Scene) noexcept
{
    uint32_t N = 0u;
    auto Seat = [&](const char* Label, Frontier::EditorInstanceCategory Cat, uint32_t Depth, const float* Tint,
                    Frontier::EditorGlyph Glyph, Frontier::EditorNarrowing Narrow, const char* Meta, bool Dynamic, bool Pinned)
    {
        Frontier::EditorInstance& Row = Instances[N++];
        Row = Frontier::EditorInstance{};
        std::snprintf(Row.Label, sizeof(Row.Label), "%s", Label);
        Row.Depth = Depth; Row.Category = Cat; Row.Glyph = Glyph; Row.Narrowing = Narrow;
        Row.Tint[0] = Tint[0]; Row.Tint[1] = Tint[1]; Row.Tint[2] = Tint[2];
        Row.Dynamic = Dynamic; Row.Pinned = Pinned;
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%s", Meta);
        return N - 1u;
    };
    static constexpr float kRoom[3] = { 0.886f, 0.910f, 0.941f };
    static constexpr float kCam[3]  = { 0.204f, 0.780f, 0.349f };
    static constexpr float kLamp[3] = { 1.000f, 0.824f, 0.478f };
    const uint32_t Folder = Seat("Showcase", Frontier::EditorInstanceCategory::Folder, 0u, kRoom,
                                 Frontier::EditorGlyph::Ground, Frontier::EditorNarrowing::Auto, "", false, true);
    const auto& Spans = Scene.QuerySpans();
    const auto& Mats  = Scene.QueryMaterials();
    const auto& Tris  = Scene.QueryTriangles();
    for (const auto& Span : Spans)
    {
        char Meta[24]; std::snprintf(Meta, sizeof(Meta), "%u tris", Span.TriangleCount);
        const auto& M = Mats[Tris[Span.FirstTriangle].MaterialIndex];
        const bool Emissive = M.EmissiveRadiance.x + M.EmissiveRadiance.y + M.EmissiveRadiance.z > 0.0f;
        const float Tint[3] = { M.AlbedoColor.x, M.AlbedoColor.y, M.AlbedoColor.z };
        if (Emissive) std::snprintf(Meta, sizeof(Meta), "%.0f cd", static_cast<double>(M.EmissiveRadiance.x));
        Seat(Span.Name.c_str(), Emissive ? Frontier::EditorInstanceCategory::Light : Frontier::EditorInstanceCategory::Geometry, 1u,
             Emissive ? kLamp : Tint, Emissive ? Frontier::EditorGlyph::Bulb : Frontier::EditorGlyph::Lattice,
             Emissive ? Frontier::EditorNarrowing::Lights : Frontier::EditorNarrowing::Geometry, Meta, Span.Dynamic, false);
        ++Instances[Folder].KidCount;
    }
    const uint32_t Cams = Seat("Cameras", Frontier::EditorInstanceCategory::Folder, 0u, kCam,
                               Frontier::EditorGlyph::Camera, Frontier::EditorNarrowing::Auto, "", false, true);
    Seat("Main Camera", Frontier::EditorInstanceCategory::Camera, 1u, kCam, Frontier::EditorGlyph::Camera,
         Frontier::EditorNarrowing::Camera, "55\xc2\xb0", false, false);
    Instances[Cams].KidCount = 1u;

    for (uint32_t i = 0u; i < kMirrorEntryCount; ++i)
    {
        const MirrorEntry&        Entry = kMirrorEntries[i];
        Frontier::EditorInstance&   Row   = Instances[N++];
        Row = Frontier::EditorInstance{};
        std::snprintf(Row.Label, sizeof(Row.Label), "%s", Entry.Label);
        Row.Depth    = Entry.Depth;
        Row.KidCount = Entry.Kids;
        Row.Category     = Entry.Category;
        Row.Tint[0]  = Entry.Tint[0];
        Row.Tint[1]  = Entry.Tint[1];
        Row.Tint[2]  = Entry.Tint[2];
        Row.Dynamic  = Entry.Dynamic;
        Row.Glyph     = Entry.Glyph;
        Row.Narrowing = Entry.Narrowing;
        Row.Standing  = Entry.Standing;
        std::snprintf(Row.StandingNote, sizeof(Row.StandingNote), "%s", Entry.Note);
        std::snprintf(Row.Meta, sizeof(Row.Meta), "%s", Entry.Meta);
        std::snprintf(Row.Tag, sizeof(Row.Tag), "%s", Entry.Tag);
        Row.Pinned   = Entry.Pinned;
        Row.Shut     = Entry.Shut;
    }
    return N;
}

Frontier::EditorPropertyGroup& OpenMirrorGroup(Frontier::EditorSheet* Sheet, const char* Title) noexcept
{
    Frontier::EditorPropertyGroup& Group = Sheet->Groups[Sheet->GroupCount++];
    std::snprintf(Group.Title, sizeof(Group.Title), "%s", Title);
    Group.PropertyCount = 0u;
    return Group;
}

Frontier::EditorProperty& OpenMirrorProp(Frontier::EditorPropertyGroup& Group, const char* Label,
                                          Frontier::EditorPropertyCategory Category) noexcept
{
    Frontier::EditorProperty& Prop = Group.Properties[Group.PropertyCount++];
    std::snprintf(Prop.Label, sizeof(Prop.Label), "%s", Label);
    Prop.Category = Category;
    return Prop;
}

void BuildMirrorSheet(uint32_t Index, Frontier::EditorInstance* Instances, Frontier::EditorSheet* Sheet) noexcept
{
    Sheet->GroupCount = 0u;
    if (Index >= kMirrorEntryCount)
    {
        return;
    }

    using Frontier::EditorPropertyCategory;
    const MirrorEntry& Entry = kMirrorEntries[Index];

    switch (Entry.Category)
    {
    case Frontier::EditorInstanceCategory::Folder:
    {
        Frontier::EditorPropertyGroup& Group = OpenMirrorGroup(Sheet, "Group");
        uint32_t Total = 0u;
        for (uint32_t j = Index + 1u; j < kMirrorEntryCount && kMirrorEntries[j].Depth > Entry.Depth; ++j)
        {
            ++Total;
        }
        Frontier::EditorProperty& Contents = OpenMirrorProp(Group, "Contents", EditorPropertyCategory::Readout);
        std::snprintf(Contents.Text, sizeof(Contents.Text), "%u direct \xc2\xb7 %u total", Entry.Kids, Total);
        Frontier::EditorProperty& Tint = OpenMirrorProp(Group, "Tint", EditorPropertyCategory::Colour);
        Tint.ColourTint[0] = Instances[Index].Tint[0];
        Tint.ColourTint[1] = Instances[Index].Tint[1];
        Tint.ColourTint[2] = Instances[Index].Tint[2];
        Tint.Swatches = true;
        break;
    }
    case Frontier::EditorInstanceCategory::Geometry:
    {
        const MirrorMaterial& Mat = kMirrorMats[Entry.Material >= 0 ? Entry.Material : 0];
        Frontier::EditorPropertyGroup& Placed = OpenMirrorGroup(Sheet, "Transform");
        Frontier::EditorProperty& Where = OpenMirrorProp(Placed, "Position", EditorPropertyCategory::AxisVec3);
        Where.Axes[0] = Entry.At[0];
        Where.Axes[1] = Entry.At[1];
        Where.Axes[2] = Entry.At[2];
        Where.AxisStep = 0.05f;
        Where.Editable = false;
        Frontier::EditorProperty& Spin = OpenMirrorProp(Placed, "Rotation", EditorPropertyCategory::Readout);
        std::snprintf(Spin.Text, sizeof(Spin.Text), "%+.0f\xc2\xb0 about Z", static_cast<double>(Entry.RotZ));
        Frontier::EditorPropertyGroup& Faced = OpenMirrorGroup(Sheet, "Surface");
        Frontier::EditorProperty& Albedo = OpenMirrorProp(Faced, "Albedo", EditorPropertyCategory::Colour);
        Albedo.ColourTint[0] = Mat.Albedo[0];
        Albedo.ColourTint[1] = Mat.Albedo[1];
        Albedo.ColourTint[2] = Mat.Albedo[2];
        Frontier::EditorProperty& Emitted = OpenMirrorProp(Faced, "Emission", EditorPropertyCategory::Slider);
        Emitted.Minimum = 0.0f; Emitted.Maximum = 64.0f; Emitted.Figure = Mat.Emission;
        Emitted.Decimals = 1u;
        std::snprintf(Emitted.Unit, sizeof(Emitted.Unit), "lx");
        Frontier::EditorProperty& Rough = OpenMirrorProp(Faced, "Roughness", EditorPropertyCategory::Slider);
        Rough.Minimum = 0.0f; Rough.Maximum = 1.0f; Rough.Figure = Mat.Rough;
        Rough.Decimals = 2u;
        Frontier::EditorProperty& Metal = OpenMirrorProp(Faced, "Metallic", EditorPropertyCategory::Readout);
        std::snprintf(Metal.Text, sizeof(Metal.Text), "%.2f", static_cast<double>(Mat.Metal));
        break;
    }
    case Frontier::EditorInstanceCategory::Light:
    {
        Frontier::EditorPropertyGroup& Lamp = OpenMirrorGroup(Sheet, "Light");
        Frontier::EditorProperty& Power = OpenMirrorProp(Lamp, "Intensity", EditorPropertyCategory::Slider);
        Power.Minimum = 0.0f; Power.Maximum = 64.0f; Power.Figure = 32.0f;
        Power.Decimals = 1u;
        std::snprintf(Power.Unit, sizeof(Power.Unit), "lx");
        Frontier::EditorProperty& Hue = OpenMirrorProp(Lamp, "Colour", EditorPropertyCategory::Colour);
        Hue.ColourTint[0] = 1.0f;
        Hue.ColourTint[1] = 1.0f;
        Hue.ColourTint[2] = 1.0f;
        Frontier::EditorPropertyGroup& Aimed = OpenMirrorGroup(Sheet, "Aim");
        Frontier::EditorProperty& Facing = OpenMirrorProp(Aimed, "Direction", EditorPropertyCategory::Readout);
        std::snprintf(Facing.Text, sizeof(Facing.Text), "-Z (nadir)");
        break;
    }
    case Frontier::EditorInstanceCategory::Camera:
    {
        Frontier::EditorPropertyGroup& Placed = OpenMirrorGroup(Sheet, "Transform");
        Frontier::EditorProperty& Where = OpenMirrorProp(Placed, "Position", EditorPropertyCategory::AxisVec3);
        Where.Axes[0] = 0.0f;
        Where.Axes[1] = -3.30f;
        Where.Axes[2] = 1.55f;
        Where.AxisStep = 0.05f;
        Where.Editable = false;
        Frontier::EditorProperty& Pitch = OpenMirrorProp(Placed, "Pitch", EditorPropertyCategory::Readout);
        std::snprintf(Pitch.Text, sizeof(Pitch.Text), "+0.0\xc2\xb0");
        Frontier::EditorProperty& Yaw = OpenMirrorProp(Placed, "Yaw", EditorPropertyCategory::Readout);
        std::snprintf(Yaw.Text, sizeof(Yaw.Text), "+0.0\xc2\xb0");
        Frontier::EditorPropertyGroup& Lens = OpenMirrorGroup(Sheet, "Lens");
        Frontier::EditorProperty& Wide = OpenMirrorProp(Lens, "Field of view", EditorPropertyCategory::Slider);
        Wide.Minimum = 20.0f; Wide.Maximum = 120.0f; Wide.Figure = 55.0f;
        Wide.Decimals = 1u; Wide.Hi = true;
        std::snprintf(Wide.Unit, sizeof(Wide.Unit), "\xc2\xb0");
        Frontier::EditorProperty& Shape = OpenMirrorProp(Lens, "Aspect", EditorPropertyCategory::Readout);
        std::snprintf(Shape.Text, sizeof(Shape.Text), "1.778");
        Frontier::EditorPropertyGroup& Moved = OpenMirrorGroup(Sheet, "Flight");
        Frontier::EditorProperty& Fast = OpenMirrorProp(Moved, "Speed", EditorPropertyCategory::Readout);
        std::snprintf(Fast.Text, sizeof(Fast.Text), "2.50 m/s");
        Frontier::EditorProperty& BaseProp = OpenMirrorProp(Moved, "Cruise", EditorPropertyCategory::Readout);
        std::snprintf(BaseProp.Text, sizeof(BaseProp.Text), "2.50 m/s");
        Frontier::EditorProperty& Boost = OpenMirrorProp(Moved, "Boost", EditorPropertyCategory::Readout);
        std::snprintf(Boost.Text, sizeof(Boost.Text), "3.00\xc3\x97");
        Frontier::EditorProperty& Feel = OpenMirrorProp(Moved, "Sensitivity", EditorPropertyCategory::Readout);
        std::snprintf(Feel.Text, sizeof(Feel.Text), "0.00125 rad/px");
        break;
    }
    default:
        break;
    }
}

} // namespace

int main()
{
    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));
    IO.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    IO.IniFilename = nullptr;
    IO.LogFilename = nullptr;
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // the swapchain seats this in the engine; here it is ours
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    Frontier::EditorHost Editor;
    Editor.ApplyTheme();   // seats the faces first: the glyph sheet below must carry them, not the raster default

    static Frontier::TypefaceRegistry Typefaces;
    const uint32_t FamilyCount = Typefaces.Load("EngineContent/FontArchives");
    Frontier::TypefaceRegistry::Install(&Typefaces);
    std::fprintf(stderr, "[EditorProof] typefaces: %u families\n", FamilyCount);

    unsigned char* GlyphSheet = nullptr;
    int GlyphSheetWidth = 0, GlyphSheetHeight = 0;
    IO.Fonts->GetTexDataAsRGBA32(&GlyphSheet, &GlyphSheetWidth, &GlyphSheetHeight);

    if (!Editor.SeatShade(kWidth, kHeight))
    {
        std::fprintf(stderr, "[EditorProof] [FAIL] the shade never seated\n");
        return 1;
    }

    Frontier::EditorReadout Readout = {};
    Readout.Fps = 60.0f;
    std::snprintf(Readout.Quality, sizeof(Readout.Quality), "Standard");
    std::snprintf(Readout.Pixels, sizeof(Readout.Pixels), "%d\xc3\x97%d", kWidth, kHeight);
    Readout.SunElevation = 5.2f;
    std::snprintf(Readout.Scene, sizeof(Readout.Scene), "Showcase");
    Readout.MoonCount = 1u;
    Readout.Cam[0] = 0.0f; Readout.Cam[1] = 2.0f; Readout.Cam[2] = 0.0f;
    Editor.AssignReadout(&Readout);
    Editor.AssignProjectName("Project-Zero");   // production always names the project; the pull captions it

    // The scene the ReSTIR viewport renders, traced here on the CPU (the Vulkan build runs the same estimator on
    //    the GPU) and handed to the Viewport panel as its texture.
    Frontier::ProjectZero::RayTracingSolver Scene;
    Scene.ConstructShowcaseScene();
    Frontier::ProjectZero::FlyThroughSolver Camera;
    Camera.AssignSpatialLocation(Frontier::Vector3{ 0.0f, -14.0f, 2.2f });
    Camera.AssignOrientationEuler(-2.0f * 3.14159265f / 180.0f, 220.0f * 3.14159265f / 180.0f, 0.0f);
    constexpr uint32_t kViewW = 480u, kViewH = 300u;
    Camera.AssignAspectRatio(static_cast<float>(kViewW) / static_cast<float>(kViewH));
    static std::vector<unsigned char> SceneRgba(static_cast<size_t>(kViewW) * kViewH * 4u);
    {
        const char* FramesEnv = std::getenv("EDITORPROOF_FRAMES");
        const uint32_t Frames = FramesEnv ? static_cast<uint32_t>(std::atoi(FramesEnv)) : 24u;
        CpuReSTIR::Render(Scene, Camera, kViewW, kViewH, Frames, 8u, 1.05f, SceneRgba.data());
        std::fprintf(stderr, "[EditorProof] traced the showcase: %ux%u, %u frames, %zu triangles, %zu spans\n",
                     kViewW, kViewH, Frames, Scene.QueryTriangles().size(), Scene.QuerySpans().size());
    }
    Readout.Triangles = static_cast<uint32_t>(Scene.QueryTriangles().size());
    gSceneRgba = SceneRgba.data(); gSceneW = kViewW; gSceneH = kViewH;
    gSceneTexId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(gSceneRgba));
    Editor.AssignViewTexture(gSceneTexId, kViewW, kViewH);

    Frontier::EditorInstance MirrorInstances[Frontier::kMaxEditorInstances] = {};
    Frontier::EditorSheet  PickedSheet = {};
    const uint32_t RosterCount = FillMirrorInstances(MirrorInstances, Scene);
    const uint32_t CelestialFirst = RosterCount - kMirrorEntryCount;
    const uint32_t SunRow = CelestialFirst + 2u;
    Editor.PickInstance(SunRow);   // Sun, the page's default pick
    BuildMirrorSheet(2u, MirrorInstances + CelestialFirst, &PickedSheet);

    std::vector<unsigned char> Pixels(static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight) * 3u);

    // One engine tick with the pointer parked where the phase wants it. The shade takes the
    //    contact first; while it owns the pointer the columns below see an empty contact.
    auto Tick = [&](float MouseX, float MouseY, bool Down)
    {
        IO.DeltaTime = 1.0f / 60.0f;
        Editor.TickShade(MouseX, MouseY, Down, 0.0f, 1.0f / 60.0f);
        if (Editor.ShadeCoversPointer())
        {
            IO.AddMousePosEvent(-1.0f, -1.0f);
            IO.AddMouseButtonEvent(0, false);
        }
        else
        {
            IO.AddMousePosEvent(MouseX, MouseY);
            IO.AddMouseButtonEvent(0, Down);
        }
        ImGui::NewFrame();
        Editor.Record(MirrorInstances, RosterCount, &PickedSheet);
        ImGui::Render();
    };
    auto Rasterise = [&]()
    {
        for (size_t I = 0u; I < Pixels.size(); I += 3u)
        {
            Pixels[I] = kGround[0]; Pixels[I + 1u] = kGround[1]; Pixels[I + 2u] = kGround[2];
        }
        const ImDrawData* Drawings = ImGui::GetDrawData();
        for (int Index = 0; Index < Drawings->CmdListsCount; ++Index)
            RasterizeList(Drawings->CmdLists[Index], GlyphSheet, GlyphSheetWidth, GlyphSheetHeight, Pixels.data(),
                          Drawings->DisplayPos, Drawings->FramebufferScale);
    };
    auto Click = [&](float X, float Y)
    {
        Tick(X, Y, false);
        Tick(X, Y, true);
        Tick(X, Y, false);
    };
    auto Rest = [&](int Ticks)
    {
        for (int i = 0; i < Ticks; ++i)
            Tick(-1.0f, -1.0f, false);
    };
    auto Type = [&](const char* Text)
    {
        IO.AddInputCharactersUTF8(Text);
        Tick(-1.0f, -1.0f, false);
    };
    auto KeyChord = [&]()
    {
        // Ctrl+K the way the engine seats it: the mod key first (without it KeyCtrl never seats — the
        //    left-Ctrl key alone leaves it false), then the chord ticks down, then up, with no repeats.
        IO.AddKeyEvent(ImGuiMod_Ctrl, true);
        IO.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        IO.AddKeyEvent(ImGuiKey_K, true);
        Tick(-1.0f, -1.0f, false);
        IO.AddKeyEvent(ImGuiKey_K, false);
        IO.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        IO.AddKeyEvent(ImGuiMod_Ctrl, false);
        Tick(-1.0f, -1.0f, false);
    };

    // The engine's tick order (RenderScheduler::Present), shade and all.
    //    Ten ticks: the built columns settle over the first two, and the gates read the last. The pointer
    //    rests nowhere near the strips, so no hover tint may pollute the gates.
    for (int i = 0; i < 10; ++i)
        Rest(1);

    Rasterise();

    const char* Sheet = "Exhibits/Gallery/Editor/EditorProof_Tabs.png";
    if (stbi_write_png(Sheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
    {
        std::fprintf(stderr, "[EditorProof] [FAIL] the sheet would not write\n");
        return 1;
    }

    bool Failed = false;
    const auto At = [&](int X, int Y) -> const unsigned char*
    {
        return &Pixels[(static_cast<size_t>(Y) * kWidth + static_cast<size_t>(X)) * 3u];
    };
    const auto IsGround = [&](const unsigned char* P) -> bool
    {
        return P[0] == kGround[0] && P[1] == kGround[1] && P[2] == kGround[2];
    };
    const auto IsSeated = [&](const unsigned char* P) -> bool
    {
        return std::abs(static_cast<int>(P[0]) - 18) <= 6
            && std::abs(static_cast<int>(P[1]) - 18) <= 6
            && std::abs(static_cast<int>(P[2]) - 18) <= 6;
    };

    // Gate 0 — four faces: the theme seats two sizes in two archives, and the glyph sheet carried them above.
    {
        const int Faces = Editor.QueryFontCount();
        std::fprintf(stderr, "[EditorProof] faces seated: %d of 4\n", Faces);
        if (Faces != 4)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the theme fell back to the raster default\n");
            Failed = true;
        }
    }

    // Gate 1 — two occupied columns: the outliner's 316 px and the viewport must carry panel ink, not bare ground.
    {
        const int LoX[2] = { 10, 400 };
        const int HiX[2] = { 300, 1260 };
        const char* Name[2] = { "outliner", "viewport" };
        for (int Third = 0; Third < 2; ++Third)
        {
            int Ink = 0;
            for (int Y = 100; Y < 700; ++Y)
                for (int X = LoX[Third]; X < HiX[Third]; ++X)
                    if (!IsGround(At(X, Y)))
                        ++Ink;
            const int Cells = (HiX[Third] - LoX[Third]) * 600;
            std::fprintf(stderr, "[EditorProof] %s third: %d ink cells of %d\n", Name[Third], Ink, Cells);
            if (Ink * 100 < Cells * 3)
            {
                std::fprintf(stderr, "[EditorProof] [FAIL] the %s third is bare ground\n", Name[Third]);
                Failed = true;
            }
        }
    }

    // Gate 2 — the trapezoid: both upper corners of the outliner tab must sit inside the lower ones by
    //    the seated slant. The tab spans y 1 … 27, at the top edge; the scanlines sit 1 px off its
    //    extremes, clear of the
    //    title glyphs that shred the mid-band runs, and the slant is linear, so each side must measure
    //    between 7 and 17 px against the seated 14. The rounded viewport corner leaves a seated sliver at
    //    the strip's left edge, so the leftward scan only trusts runs twenty cells or longer. The shade's
    //    pull floats at the centre, clear of the left column's scans.
    {
        const auto IsGlyph = [&](const unsigned char* P) -> bool
        {
            return P[0] >= 190u && P[1] >= 190u && P[2] >= 190u;
        };
        const auto LeftEdge = [&](int Y) -> int
        {
            for (int X = 0; X < 120; ++X)
            {
                if (!IsSeated(At(X, Y)))
                {
                    continue;
                }
                int End = X;
                while (End < 300 && IsSeated(At(End, Y)))
                {
                    ++End;
                }
                if (End - X >= 20)
                {
                    return X;
                }
                X = End;
            }
            return -1;
        };
        const auto RightEdge = [&](int Y, int FromX) -> int
        {
            int Last = -1;
            for (int X = FromX; X < 400; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (IsSeated(P))
                    Last = X;
                else if (!IsGlyph(P))
                    break;
            }
            return Last;
        };
        const int LoLeft = LeftEdge(26), HiLeft = LeftEdge(2);
        std::fprintf(stderr, "[EditorProof] tab edges: lower-left %d, upper-left %d", LoLeft, HiLeft);
        bool Slanted = LoLeft >= 0 && HiLeft >= 0;
        int LeftInset = 0, RightInset = 0;
        if (Slanted)
        {
            const int LoRight = RightEdge(26, LoLeft), HiRight = RightEdge(2, HiLeft);
            std::fprintf(stderr, ", lower-right %d, upper-right %d\n", LoRight, HiRight);
            Slanted = LoRight > LoLeft && HiRight > HiLeft;
            LeftInset = HiLeft - LoLeft;
            RightInset = LoRight - HiRight;
        }
        else
        {
            std::fprintf(stderr, "\n");
        }
        std::fprintf(stderr, "[EditorProof] slant: left %d px, right %d px (sheet seats 14)\n", LeftInset, RightInset);
        if (!Slanted || LeftInset < 7 || LeftInset > 17 || RightInset < 7 || RightInset > 17)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the tab is not a trapezoid\n");
            Failed = true;
        }
    }

    // Gate 3 — titled strips: the tab band must carry glyph ink (the three titles).
    {
        int Glyphs = 0;
        for (int Y = 0; Y < 32; ++Y)
            for (int X = 0; X < kWidth; ++X)
            {
                if (X >= 530 && X <= 750)
                    continue;   // the shade's pull floats here; its brand is not a title
                const unsigned char* P = At(X, Y);
                if (P[0] >= 120u && P[1] >= 120u && P[2] >= 120u)
                    ++Glyphs;   // Outfit Light at 13 px: thin strokes, so the bar is the mid-greys
            }
        std::fprintf(stderr, "[EditorProof] %d glyph cells in the tab band\n", Glyphs);
        if (Glyphs < 60)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the strips carry no titles\n");
            Failed = true;
        }
    }

    // Gate 4 — the seated theme: the outliner column must carry the page's .sel tint (.09 white over the glass,
    //    ≈ #262728) of the revealed pick, and the sun's orange accent bar beside it.
    {
        const auto IsPickedRow = [&](const unsigned char* P) -> bool
        {
            return std::abs(static_cast<int>(P[0]) - 37) <= 4
                && std::abs(static_cast<int>(P[1]) - 38) <= 4
                && std::abs(static_cast<int>(P[2]) - 39) <= 4;
        };
        const auto IsAccent = [&](const unsigned char* P) -> bool
        {
            return P[0] > 200u && P[1] > 140u && P[1] < 210u && P[2] < 120u;
        };
        int Bars = 0, Rows = 0;
        for (int Y = 100; Y < 700; ++Y)
        {
            for (int X = 10; X < 300; ++X)
            {
                if (IsPickedRow(At(X, Y)))
                    ++Rows;
                if (X < 20 && IsAccent(At(X, Y)))
                    ++Bars;
            }
        }
        std::fprintf(stderr, "[EditorProof] theme tints: %d accent-bar cells, %d seated-row cells\n", Bars, Rows);
        if (Bars < 20)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the picked row carries no accent bar\n");
            Failed = true;
        }
        if (Rows < 100)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the revealed pick carries no seated row\n");
            Failed = true;
        }
    }

    // Gate 4b — the console sleeps: shut till Ctrl+K, the command band carries the traced view, not the
    //    console's black bar. The gizmo's pads sit right of x 1100, clear of this probe.
    {
        int Black = 0;
        for (int Y = 620; Y < 670; ++Y)
            for (int X = 360; X < 1100; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] == 0u && P[1] == 0u && P[2] == 0u)
                    ++Black;
            }
        std::fprintf(stderr, "[EditorProof] shut console: %d black cells in the command band\n", Black);
        if (Black > 800)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the console bar shows before Ctrl+K\n");
            Failed = true;
        }
    }

    // Gate 4c — one unbroken sill: the outliner foot and the viewport foot open on the same row. The
    //    hairline itself will not rasterise, so this reads the wash bands' top step instead: the first
    //    wash-mean row scanning down. The dock host insets the columns eight below the sill, so the
    //    shared forty opens at 672, not 680.
    {
        const auto FootTop = [&](int X0, int X1) -> int
        {
            for (int Y = 666; Y <= 678; ++Y)
            {
                int Sum = 0;
                for (int X = X0; X <= X1; ++X)
                {
                    const unsigned char* P = At(X, Y);
                    Sum += static_cast<int>(P[0]) + static_cast<int>(P[1]) + static_cast<int>(P[2]);
                }
                const int Mean = Sum / (3 * (X1 - X0 + 1));
                if (Mean >= 19 && Mean <= 32)
                {
                    return Y;
                }
            }
            return -1;
        };
        const int OutTop  = FootTop(40, 280);
        const int ViewTop = FootTop(400, 1000);
        std::fprintf(stderr, "[EditorProof] foot tops: outliner %d, viewport %d (want 672, one line)\n",
                     OutTop, ViewTop);
        if (OutTop < 670 || OutTop > 676 || ViewTop < 670 || ViewTop > 676 || std::abs(OutTop - ViewTop) > 2)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the foot strips are not one height\n");
            Failed = true;
        }
    }

    // Gate 5 — a narrowing pill lights: a click on Camera fills it (g3 over glass) and lifts its ink to white.
    Click(46.0f, 259.0f);
    Rest(6);
    Rasterise();
    {
        const char* MenuSheet = "Exhibits/Gallery/Editor/EditorProof_Menu.png";
        if (stbi_write_png(MenuSheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the pill sheet would not write\n");
            return 1;
        }
        int Lit = 0;
        for (int Y = 247; Y < 272; ++Y)
            for (int X = 16; X < 76; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (std::abs(static_cast<int>(P[0]) - 34) <= 4 && std::abs(static_cast<int>(P[1]) - 35) <= 4
                    && std::abs(static_cast<int>(P[2]) - 37) <= 4)
                    ++Lit;
            }
        std::fprintf(stderr, "[EditorProof] camera pill: %d lit cells\n", Lit);
        if (Lit < 300)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the Camera pill never lit\n");
            Failed = true;
        }
    }

    // Gate 6 — the narrowing works: with Camera lit the outline drops to the camera rows, so the World folder's
    //    Atmosphere row (y ≈ 372) is gone and the first row under the pills is Camera. The names print dim
    //    (t2 .56, below the old 150 bar), so this counts tree ink, not bright ink, over the tree's own rows.
    Rasterise();
    {
        const char* NarrowSheet = "Exhibits/Gallery/Editor/EditorProof_Filtered.png";
        if (stbi_write_png(NarrowSheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the narrowed sheet would not write\n");
            return 1;
        }
        int Rows = 0;
        for (int Y = 284; Y < 624; ++Y)
            for (int X = 40; X < 300; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] > 60u || P[1] > 60u || P[2] > 60u)
                    ++Rows;   // name ink
            }
        int Green = 0;
        for (int Y = 284; Y < 424; ++Y)
            for (int X = 40; X < 90; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[1] > 150u && P[0] < 120u && P[2] < 140u)
                    ++Green;   // the camera glyph in its #34c759
            }
        std::fprintf(stderr, "[EditorProof] narrowed: %d ink cells, %d camera-green cells\n", Rows, Green);
        if (Rows < 1000 || Rows > 2600)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the narrowed outline is the wrong size\n");
            Failed = true;
        }
        if (Green < 10)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the narrowed outline lost the camera\n");
            Failed = true;
        }
    }

    // The pill dismisses too: one click on it clears the narrowing for the passes below.
    Click(46.0f, 259.0f);
    Rest(5);


    // Gate 7 — the palette opens: focusing the console and typing raises the suggestion stack, its
    //    standing row indigo. Back to Main Camera first, so the sheet matches the Tabs pass.
    Editor.PickInstance(SunRow);
    BuildMirrorSheet(2u, MirrorInstances + CelestialFirst, &PickedSheet);
    Rest(5);
    KeyChord();   // the console opens: shut till Ctrl+K, full black past it
    Rest(2);
    Click(700.0f, 648.0f);
    Rest(3);
    Type("p");
    Rest(3);
    Rasterise();
    {
        const char* PaletteSheet = "Exhibits/Gallery/Editor/EditorProof_Palette.png";
        if (stbi_write_png(PaletteSheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the palette sheet would not write\n");
            return 1;
        }
        int Box = 0, Indigo = 0;
        for (int Y = 300; Y < 640; ++Y)
            for (int X = 355; X < 1240; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] == 0u && P[1] == 0u && P[2] == 0u)
                    ++Box;
                if (P[2] >= 60 && static_cast<int>(P[2]) - static_cast<int>(P[0]) >= 25)
                    ++Indigo;
            }
        std::fprintf(stderr, "[EditorProof] palette: %d stack cells, %d indigo cells\n", Box, Indigo);
        if (Box < 3000)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the palette never opened\n");
            Failed = true;
        }
        if (Indigo < 300)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the standing row carries no indigo\n");
            Failed = true;
        }
        int Bar = 0;
        for (int Y = 642; Y < 676; ++Y)
            for (int X = 360; X < 1235; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] == 0u && P[1] == 0u && P[2] == 0u)
                    ++Bar;
            }
        std::fprintf(stderr, "[EditorProof] console bar: %d black cells\n", Bar);
        if (Bar < 15000)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the console bar is not full black\n");
            Failed = true;
        }
    }


    // Gate 8 — the views menu opens and snaps: the pill raises eight rows, and each compass row poses
    //    the orbit (checked here against the solver's own euler).
    Click(562.0f, 72.0f);
    Rest(14);
    Rasterise();
    {
        const char* ViewsSheet = "Exhibits/Gallery/Editor/EditorProof_Views.png";
        if (stbi_write_png(ViewsSheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the views sheet would not write\n");
            return 1;
        }
        int Black = 0;
        for (int Y = 104; Y < 344; ++Y)
            for (int X = 560; X < 710; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] == 0u && P[1] == 0u && P[2] == 0u)
                    ++Black;
            }
        std::fprintf(stderr, "[EditorProof] views menu: %d black cells\n", Black);
        if (Black < 1500)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the views menu never opened\n");
            Failed = true;
        }
    }
    {
        uint32_t LastRev = Editor.QueryViewportOrbit().Revision;
        const auto PickView = [&](float RowY, uint32_t WantSnap, float WantYaw, float WantPitch,
                                  bool WantOrtho)
        {
            Click(637.0f, RowY);
            Rest(3);
            const Frontier::ViewportOrbit& Orbit = Editor.QueryViewportOrbit();
            std::fprintf(stderr, "[EditorProof] view pick: snap %u yaw %.3f pitch %.3f ortho %d rev %u\n",
                         Orbit.ViewPoint, Orbit.Yaw, Orbit.Pitch, Orbit.Ortho ? 1 : 0, Orbit.Revision);
            if (Orbit.ViewPoint != WantSnap
                || std::fabs(Orbit.Yaw - WantYaw) > 0.01f
                || std::fabs(Orbit.Pitch - WantPitch) > 0.01f
                || Orbit.Ortho != WantOrtho
                || Orbit.Revision <= LastRev)
            {
                std::fprintf(stderr, "[EditorProof] [FAIL] the views menu never posed snap %u\n", WantSnap);
                Failed = true;
            }
            LastRev = Orbit.Revision;
            Click(562.0f, 72.0f);   // the pill again: the next pick reopens the menu
            Rest(3);
        };
        PickView(295.0f, 5u, 0.0f, -1.5707963f, false);
        PickView(175.0f, 1u, 0.0f, 0.0f, false);
        PickView(205.0f, 2u, 3.1415927f, 0.0f, false);
        PickView(235.0f, 3u, -1.5707963f, 0.0f, false);
        PickView(265.0f, 4u, 1.5707963f, 0.0f, false);
        PickView(325.0f, 6u, 0.0f, 1.5707963f, false);
        PickView(136.0f, 0u, 0.0f, 0.0f, true);
        PickView(106.0f, 0u, 0.0f, 0.0f, false);
        Click(500.0f, 500.0f);   // dismiss the reopened menu off the empty view
        Rest(3);
    }

    // Gate 9 — the gizmo answers: a pad tap snaps its view, a drag orbits, and the wheel dollies.
    Click(1233.0f, 588.0f);
    Rest(3);
    {
        const Frontier::ViewportOrbit& Orbit = Editor.QueryViewportOrbit();
        std::fprintf(stderr, "[EditorProof] gizmo tap: snap %u yaw %.3f\n", Orbit.ViewPoint, Orbit.Yaw);
        if (Orbit.ViewPoint != 3u || std::fabs(Orbit.Yaw + 1.5707963f) > 0.01f)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the +X pad never snapped right\n");
            Failed = true;
        }
    }
    {
        const float    YawBefore = Editor.QueryViewportOrbit().Yaw;
        const uint32_t RevBefore = Editor.QueryViewportOrbit().Revision;
        Tick(1213.0f, 587.0f, false);
        Tick(1213.0f, 587.0f, false);
        Tick(1213.0f, 587.0f, true);
        for (int i = 1; i <= 8; ++i)
            Tick(1213.0f + 5.0f * static_cast<float>(i), 587.0f, true);
        Tick(1253.0f, 587.0f, false);
        Rest(3);
        const Frontier::ViewportOrbit& Orbit = Editor.QueryViewportOrbit();
        std::fprintf(stderr, "[EditorProof] gizmo drag: yaw %.3f (was %.3f) snap %u rev %u\n",
                     Orbit.Yaw, YawBefore, Orbit.ViewPoint, Orbit.Revision);
        if (!(Orbit.Yaw < YawBefore - 0.1f) || Orbit.ViewPoint != 0u || Orbit.Revision <= RevBefore)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the gizmo drag never orbited\n");
            Failed = true;
        }
    }
    {
        const float DistBefore = Editor.QueryViewportOrbit().Distance;
        IO.MouseWheel = 1.0f;
        Tick(600.0f, 400.0f, false);
        IO.MouseWheel = 0.0f;
        Rest(2);
        const float DistAfter = Editor.QueryViewportOrbit().Distance;
        std::fprintf(stderr, "[EditorProof] wheel dolly: %.3f (was %.3f)\n", DistAfter, DistBefore);
        if (!(DistAfter < DistBefore))
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the wheel never dollied\n");
            Failed = true;
        }
    }

    // Gate 10 — the inspector rises: its tab raised, the foot strip sits forty above the sill, carrying
    //    the picked instance's standing beside the live realtime and triangle figures.
    ImGui::SetWindowFocus("Inspector");
    Rest(8);
    Rasterise();
    {
        const char* InspectorSheet = "Exhibits/Gallery/Editor/EditorProof_Inspector.png";
        if (stbi_write_png(InspectorSheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the inspector sheet would not write\n");
            return 1;
        }
        const auto FootTop = [&](int X0, int X1) -> int
        {
            for (int Y = 666; Y <= 678; ++Y)
            {
                int Sum = 0;
                for (int X = X0; X <= X1; ++X)
                {
                    const unsigned char* P = At(X, Y);
                    Sum += static_cast<int>(P[0]) + static_cast<int>(P[1]) + static_cast<int>(P[2]);
                }
                const int Mean = Sum / (3 * (X1 - X0 + 1));
                if (Mean >= 19 && Mean <= 32)
                {
                    return Y;
                }
            }
            return -1;
        };
        const int InspTop = FootTop(40, 280);
        const int ViewTop = FootTop(400, 1000);
        int Ink = 0;
        for (int Y = 676; Y < 710; ++Y)
            for (int X = 14; X < 302; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] > 60u || P[1] > 60u || P[2] > 60u)
                    ++Ink;
            }
        std::fprintf(stderr, "[EditorProof] inspector foot: top %d (viewport %d), %d bright cells\n",
                     InspTop, ViewTop, Ink);
        if (InspTop < 670 || InspTop > 676 || std::abs(InspTop - ViewTop) > 2)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the inspector foot strays off the shared row\n");
            Failed = true;
        }
        if (Ink < 150)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the inspector foot carries no figures\n");
            Failed = true;
        }
    }

    // Gate 11 — the pull survives: narrowing the strip must not take the shade's pull with it. Its
    //    brand sits centred, so its glyph cells must read around the pull's own centre.
    {
        const int PullX = static_cast<int>(Editor.QueryNotchX() + 0.5f);
        const int PullY = static_cast<int>(Editor.QueryNotchY() + 0.5f);
        int Brand = 0;
        for (int Y = PullY - 10; Y < PullY + 10; ++Y)
            for (int X = PullX - 40; X < PullX + 40; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] > 60u || P[1] > 60u || P[2] > 60u)
                    ++Brand;
            }
        std::fprintf(stderr, "[EditorProof] pull brand: %d bright cells\n", Brand);
        if (Brand < 50)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the shade's pull is gone from the band\n");
            Failed = true;
        }
    }

    // Gate 12 — the pull slides: a horizontal drag carries it along the top edge.
    {
        const float StartX = Editor.QueryNotchX();
        const float MidY = Editor.QueryNotchY();
        Tick(StartX, MidY, false);
        Tick(StartX, MidY, true);
        for (int i = 1; i <= 12; ++i)
            Tick(StartX + 10.0f * static_cast<float>(i), MidY, true);
        Tick(StartX + 120.0f, MidY, false);
        Rest(5);
        const float EndX = Editor.QueryNotchX();
        std::fprintf(stderr, "[EditorProof] pull slide: %.1f -> %.1f\n", StartX, EndX);
        if (EndX - StartX < 90.0f || EndX - StartX > 150.0f)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the pull never slid\n");
            Failed = true;
        }
    }

    // Gate 13 — open and close: the pull toggles, the grip closes, drags carry both ways.
    {
        Click(Editor.QueryNotchX(), Editor.QueryNotchY());
        Rest(40);
        std::fprintf(stderr, "[EditorProof] shade after pull tap: %s\n", Editor.QueryShadeOpen() ? "open" : "shut");
        if (!Editor.QueryShadeOpen())
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the pull tap never drew the shade\n");
            Failed = true;
        }
        Click(Editor.QueryGripX(), Editor.QueryGripY());
        Rest(40);
        std::fprintf(stderr, "[EditorProof] shade after grip tap: %s\n", Editor.QueryShadeOpen() ? "open" : "shut");
        if (Editor.QueryShadeOpen())
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the grip tap never shut the shade\n");
            Failed = true;
        }
        const float MidX = Editor.QueryNotchX();
        const float MidY = Editor.QueryNotchY();
        Tick(MidX, MidY, false);
        Tick(MidX, MidY, true);
        for (int i = 1; i <= 15; ++i)
            Tick(MidX, MidY + 25.0f * static_cast<float>(i), true);
        Tick(MidX, MidY + 375.0f, false);
        Rest(40);
        std::fprintf(stderr, "[EditorProof] shade after pull-down: %s\n", Editor.QueryShadeOpen() ? "open" : "shut");
        if (!Editor.QueryShadeOpen())
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the pull never drew the shade\n");
            Failed = true;
        }
        Rasterise();
        const char* ShadeSheet = "Exhibits/Gallery/Editor/EditorProof_Shade.png";
        if (stbi_write_png(ShadeSheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the sheet would not write\n");
            return 1;
        }
        Click(Editor.QueryGripX(), Editor.QueryGripY());
        Rest(40);
        std::fprintf(stderr, "[EditorProof] shade after grip tap: %s\n", Editor.QueryShadeOpen() ? "open" : "shut");
        if (Editor.QueryShadeOpen())
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the grip tap never shut the shade\n");
            Failed = true;
        }
        // The page itself pulls up: a press on a tile that wanders upward carries the shade home,
        //    and only a clean release still taps.
        const float ReX = Editor.QueryNotchX();
        const float ReY = Editor.QueryNotchY();
        Tick(ReX, ReY, false);
        Tick(ReX, ReY, true);
        for (int i = 1; i <= 15; ++i)
            Tick(ReX, ReY + 25.0f * static_cast<float>(i), true);
        Tick(ReX, ReY + 375.0f, false);
        Rest(40);
        if (!Editor.QueryShadeOpen())
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the pull never drew the shade again\n");
            Failed = true;
        }
        const float TileX = Editor.QueryGiTileX();
        const float TileY = Editor.QueryGiTileY();
        Tick(TileX, TileY, false);
        Tick(TileX, TileY, true);
        for (int i = 1; i <= 12; ++i)
            Tick(TileX, TileY - 12.5f * static_cast<float>(i), true);
        Tick(TileX, TileY - 150.0f, false);
        Rest(40);
        std::fprintf(stderr, "[EditorProof] shade after tile pull-up: %s\n", Editor.QueryShadeOpen() ? "open" : "shut");
        if (Editor.QueryShadeOpen())
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the pull-up never shut the shade\n");
            Failed = true;
        }
    }

    // Gate 14 — the add control: a plus disc must sit past the left column's last tab. Fresh pixels:
    //    the shade capture left the open sheet in the raster, and the shut pull sits under it.
    {
        Rest(3);
        Rasterise();
        const int SeamX = static_cast<int>(Editor.QueryTabAddX() + 0.5f);
        const int SeamY = static_cast<int>(Editor.QueryTabAddY() + 0.5f);
        int GlyphX = SeamX;
        int Best = -1;
        for (int X = SeamX - 30; X <= SeamX + 30; ++X)
        {
            int Column = 0;
            for (int Y = SeamY - 12; Y < SeamY + 12; ++Y)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] > 60u || P[1] > 60u || P[2] > 60u)
                    ++Column;
            }
            if (Column > Best)
            {
                Best = Column;
                GlyphX = X;
            }
        }
        int Plus = 0;
        for (int Y = SeamY - 12; Y < SeamY + 12; ++Y)
            for (int X = GlyphX - 12; X < GlyphX + 12; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] > 60u || P[1] > 60u || P[2] > 60u)
                    ++Plus;
            }
        std::fprintf(stderr, "[EditorProof] add glyph hunts to %d (seam %.1f)\n", GlyphX,
                     Editor.QueryTabAddX());
        std::fprintf(stderr, "[EditorProof] add control: %d bright cells\n", Plus);
        if (Plus < 12)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the add control never seated\n");
            Failed = true;
        }
    }

    // Gate 15 — close marks and the add menu: each tab shuts through its mark and seats again through
    //    the menu. The inspector goes first, the outliner second, the viewport last.
    auto CycleTab = [&](float MarkX, uint32_t Tab, uint32_t Row, bool Capture, float RaiseX = 0.0f)
    {
        if (RaiseX > 0.0f)
        {
            Click(RaiseX, 15.0f);
            Rest(3);
        }
        Click(MarkX, 15.0f);
        Rest(3);
        std::fprintf(stderr, "[EditorProof] tab %u after its mark: %s\n", Tab,
                     Editor.QueryTabOpen(Tab) ? "open" : "shut");
        if (Editor.QueryTabOpen(Tab))
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] tab %u never shut\n", Tab);
            Failed = true;
        }
        for (uint32_t Other = 0u; Other < 3u; ++Other)
        {
            if (Other != Tab && !Editor.QueryTabOpen(Other))
            {
                std::fprintf(stderr, "[EditorProof] [FAIL] tab %u shut alongside tab %u\n", Other,
                             Tab);
                Failed = true;
            }
        }
        const float PlusX = Editor.QueryTabAddX();
        const float PlusY = Editor.QueryTabAddY();
        std::fprintf(stderr, "[EditorProof] add control seats at %.1f, %.1f\n", PlusX, PlusY);
        Click(PlusX, PlusY);
        Rest(3);
        if (Capture)
        {
            Rasterise();
            const char* TabMenuSheet = "Exhibits/Gallery/Editor/EditorProof_TabMenu.png";
            if (stbi_write_png(TabMenuSheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
            {
                std::fprintf(stderr, "[EditorProof] [FAIL] the sheet would not write\n");
                Failed = true;
            }
        }
        Click(PlusX + 70.0f, PlusY + 18.0f + 21.0f * static_cast<float>(Row));
        Rest(3);
        if (!Editor.QueryTabOpen(Tab))
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] tab %u never seated again\n", Tab);
            Failed = true;
        }
    };
    Rest(60);   // let the tab bar settle: a click must land where the mark rendered
    CycleTab(189.0f, 2u, 2u, true, 130.0f);
    CycleTab(103.0f, 0u, 0u, false, 90.0f);
    Click(90.0f, 15.0f);   // raise the outliner again: later sheets open on it
    Rest(3);
    CycleTab(420.0f, 1u, 1u, false);

    if (Failed)
    {
        std::fprintf(stderr, "[EditorProof] [FAIL] wrote %s, but the sheet disagrees with its caption\n", Sheet);
        return 1;
    }
    std::fprintf(stderr, "[EditorProof] wrote %s: three columns, trapezoid tabs, titled strips, seated tints\n", Sheet);
    return 0;
}
