//============================================================================================================================================
//                                                      EDITORPROOF.CPP
//============================================================================================================================================
// 🧩 Headless visual proof — drives EditorHost through the engine's tick order over the Cornell mirror, rasterises
//    the last tick with a dependency-free CPU rasteriser, and gates the trapezoid sheet, the seated theme tints,
//    and the four faces. No Vulkan, no GLFW, no window.

#ifndef FRONTIER_DEVELOPMENT
#error "the proof must define FRONTIER_DEVELOPMENT, or the editor records nothing and every gate fails"
#endif
#include <imgui.h>

#include "EditorHost.h"
#include "TypefaceRegistry.h"
#include "PngWriteShim.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
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
                    const Rgba Glyph = SampleGlyphSheet(GlyphSheet, GlyphSheetWidth, GlyphSheetHeight, U, V);
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
};

constexpr MirrorEntry kMirrorEntries[] =
{
    { "Room",              Frontier::EditorInstanceCategory::Folder,   0u, 5u, { 0.788f, 0.635f, 0.294f }, -1, {  0.00f,  0.00f, 0.000f },   0.0f, false },
    { "Floor",             Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.750f, 0.750f, 0.750f },  0, {  0.00f,  2.00f, 0.000f },   0.0f, false },
    { "Ceiling",           Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.750f, 0.750f, 0.750f },  0, {  0.00f,  2.00f, 3.000f },   0.0f, false },
    { "Back Wall",         Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.750f, 0.750f, 0.750f },  0, {  0.00f,  4.00f, 1.500f },   0.0f, false },
    { "Left Wall",         Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.850f, 0.120f, 0.120f },  1, { -2.00f,  2.00f, 1.500f },   0.0f, false },
    { "Right Wall",        Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.120f, 0.850f, 0.150f },  2, {  2.00f,  2.00f, 1.500f },   0.0f, false },
    { "Objects",           Frontier::EditorInstanceCategory::Folder,   0u, 5u, { 0.788f, 0.635f, 0.294f }, -1, {  0.00f,  0.00f, 0.000f },   0.0f, false },
    { "Tall Box",          Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.780f, 0.780f, 0.780f },  4, { -0.90f,  2.70f, 0.900f },  22.0f, true  },
    { "Short Box",         Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.780f, 0.780f, 0.780f },  5, {  0.85f,  1.50f, 0.450f }, -18.0f, true  },
    { "Sphere",            Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.820f, 0.780f, 0.720f },  6, { -1.15f,  1.05f, 0.450f },   0.0f, false },
    { "Cone",              Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.350f, 0.450f, 0.700f },  7, {  1.30f,  3.05f, 0.550f },   0.0f, false },
    { "Torus",             Frontier::EditorInstanceCategory::Geometry, 1u, 0u, { 0.780f, 0.550f, 0.250f },  8, {  0.00f,  1.55f, 0.320f },   0.0f, false },
    { "Lighting",          Frontier::EditorInstanceCategory::Folder,   0u, 1u, { 0.788f, 0.635f, 0.294f }, -1, {  0.00f,  0.00f, 0.000f },   0.0f, false },
    { "Ceiling Luminaire", Frontier::EditorInstanceCategory::Light,    1u, 0u, { 0.961f, 0.827f, 0.294f },  3, {  0.00f,  0.75f, 2.995f },   0.0f, false },
    { "Cameras",           Frontier::EditorInstanceCategory::Folder,   0u, 1u, { 0.788f, 0.635f, 0.294f }, -1, {  0.00f,  0.00f, 0.000f },   0.0f, false },
    { "Main Camera",       Frontier::EditorInstanceCategory::Camera,   1u, 0u, { 0.412f, 0.765f, 1.000f }, -1, {  0.00f, -3.30f, 1.550f },   0.0f, false },
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

void FillMirrorInstances(Frontier::EditorInstance* Instances) noexcept
{
    for (uint32_t i = 0u; i < kMirrorEntryCount; ++i)
    {
        const MirrorEntry&        Entry = kMirrorEntries[i];
        Frontier::EditorInstance&   Row   = Instances[i];
        std::snprintf(Row.Label, sizeof(Row.Label), "%s", Entry.Label);
        Row.Depth    = Entry.Depth;
        Row.KidCount = Entry.Kids;
        Row.Category     = Entry.Category;
        Row.Tint[0]  = Entry.Tint[0];
        Row.Tint[1]  = Entry.Tint[1];
        Row.Tint[2]  = Entry.Tint[2];
        Row.Dynamic  = Entry.Dynamic;
    }
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
        const MirrorMaterial& Mat = kMirrorMats[Entry.Material];
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

    Frontier::EditorInstance CornellInstances[kMirrorEntryCount] = {};
    Frontier::EditorSheet  PickedSheet = {};
    FillMirrorInstances(CornellInstances);
    Editor.PickInstance(15u);   // Main Camera, the last row of the mirror
    BuildMirrorSheet(15u, CornellInstances, &PickedSheet);

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
        Editor.Record(CornellInstances, kMirrorEntryCount, &PickedSheet);
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

    // The engine's tick order (RenderScheduler::Present), shade and all.
    //    Ten ticks: the built columns settle over the first two, and the gates read the last. The pointer
    //    rests nowhere near the strips, so no hover tint may pollute the gates.
    for (int i = 0; i < 10; ++i)
        Rest(1);

    Rasterise();

    const char* Sheet = "Diagnostics/EditorProof_Tabs.png";
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

    // Gate 1 — three occupied columns: each third of the sheet must carry panel ink, not bare ground.
    {
        const int LoX[3] = { 10, 400, 1020 };
        const int HiX[3] = { 270, 900, 1270 };
        const char* Name[3] = { "outliner", "viewport", "inspector" };
        for (int Third = 0; Third < 3; ++Third)
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
    //    the seated slant. The tab spans y 37 … 63, below the notch; the scanlines sit 1 px off its
    //    extremes, clear of the
    //    title glyphs that shred the mid-band runs, and the slant is linear, so each side must measure
    //    between 7 and 17 px against the seated 14. The rounded viewport corner leaves a seated sliver at
    //    the strip's left edge, so the leftward scan only trusts runs twenty cells or longer.
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
        const int LoLeft = LeftEdge(62), HiLeft = LeftEdge(38);
        std::fprintf(stderr, "[EditorProof] tab edges: lower-left %d, upper-left %d", LoLeft, HiLeft);
        bool Slanted = LoLeft >= 0 && HiLeft >= 0;
        int LeftInset = 0, RightInset = 0;
        if (Slanted)
        {
            const int LoRight = RightEdge(62, LoLeft), HiRight = RightEdge(38, HiLeft);
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
        for (int Y = 36; Y < 68; ++Y)
            for (int X = 0; X < kWidth; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] >= 190u && P[1] >= 190u && P[2] >= 190u)
                    ++Glyphs;
            }
        std::fprintf(stderr, "[EditorProof] %d glyph cells in the tab band\n", Glyphs);
        if (Glyphs < 60)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the strips carry no titles\n");
            Failed = true;
        }
    }

    // Gate 4 — the seated theme: the inspector third must carry inset card tint (#1a1a1a), and the outliner
    //    third the seated-row tint (#2a2a2a) of the revealed pick.
    {
        const auto IsInset = [&](const unsigned char* P) -> bool
        {
            return std::abs(static_cast<int>(P[0]) - 26) <= 3
                && std::abs(static_cast<int>(P[1]) - 26) <= 3
                && std::abs(static_cast<int>(P[2]) - 26) <= 3;
        };
        const auto IsPickedRow = [&](const unsigned char* P) -> bool
        {
            return std::abs(static_cast<int>(P[0]) - 42) <= 3
                && std::abs(static_cast<int>(P[1]) - 42) <= 3
                && std::abs(static_cast<int>(P[2]) - 42) <= 3;
        };
        int Cards = 0, Rows = 0;
        for (int Y = 100; Y < 700; ++Y)
        {
            for (int X = 1020; X < 1270; ++X)
                if (IsInset(At(X, Y)))
                    ++Cards;
            for (int X = 10; X < 270; ++X)
                if (IsPickedRow(At(X, Y)))
                    ++Rows;
        }
        std::fprintf(stderr, "[EditorProof] theme tints: %d inset cells, %d seated-row cells\n", Cards, Rows);
        if (Cards < 3000)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the inspector carries no cards\n");
            Failed = true;
        }
        if (Rows < 100)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the revealed pick carries no seated row\n");
            Failed = true;
        }
    }

    // Gate 5 — the category menu opens: a click on the category pill must raise the black menu.
    Click(220.0f, 152.0f);
    Rest(14);
    Rasterise();
    {
        const char* MenuSheet = "Diagnostics/EditorProof_Menu.png";
        if (stbi_write_png(MenuSheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the menu sheet would not write\n");
            return 1;
        }
        int Black = 0;
        for (int Y = 176; Y < 436; ++Y)
            for (int X = 170; X < 270; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (P[0] == 0u && P[1] == 0u && P[2] == 0u)
                    ++Black;
            }
        std::fprintf(stderr, "[EditorProof] category menu: %d black cells\n", Black);
        if (Black < 1500)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the category menu never opened\n");
            Failed = true;
        }
    }

    // Gate 6 — the narrowing works: picking Camera filters the outline and raises its chip.
    Click(220.0f, 313.0f);
    Rest(3);
    Click(500.0f, 436.0f);   // outside the menu: dismiss it, leaving the pick behind
    Rest(5);
    Rasterise();
    {
        const char* NarrowSheet = "Diagnostics/EditorProof_Filtered.png";
        if (stbi_write_png(NarrowSheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the narrowed sheet would not write\n");
            return 1;
        }
        const auto IsChip = [&](const unsigned char* P) -> bool
        {
            return std::abs(static_cast<int>(P[0]) - 36) <= 3
                && std::abs(static_cast<int>(P[1]) - 36) <= 3
                && std::abs(static_cast<int>(P[2]) - 36) <= 3;
        };
        const auto IsNarrowRow = [&](const unsigned char* P) -> bool
        {
            return std::abs(static_cast<int>(P[0]) - 42) <= 3
                && std::abs(static_cast<int>(P[1]) - 42) <= 3
                && std::abs(static_cast<int>(P[2]) - 42) <= 3;
        };
        int Chips = 0;
        for (int Y = 166; Y < 196; ++Y)
            for (int X = 14; X < 280; ++X)
                if (IsChip(At(X, Y)))
                    ++Chips;
        int Rows = 0;
        for (int Y = 196; Y < 700; ++Y)
            for (int X = 14; X < 280; ++X)
                if (IsNarrowRow(At(X, Y)))
                    ++Rows;
        std::fprintf(stderr, "[EditorProof] narrowed: %d chip cells, %d seated-row cells\n", Chips, Rows);
        if (Chips < 300)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the Camera pick raised no chip\n");
            Failed = true;
        }
        if (Rows < 100)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the narrowed outline lost the seated row\n");
            Failed = true;
        }
    }

    // The chip dismisses too: one click on it clears the narrowing for the passes below.
    Click(64.0f, 182.0f);
    Rest(5);


    // Gate 7 — the palette opens: focusing the console and typing raises the suggestion stack, its
    //    standing row indigo. Back to Main Camera first, so the sheet matches the Tabs pass.
    Editor.PickInstance(15u);
    BuildMirrorSheet(15u, CornellInstances, &PickedSheet);
    Rest(5);
    Click(500.0f, 648.0f);
    Rest(3);
    Type("p");
    Rest(3);
    Rasterise();
    {
        const char* PaletteSheet = "Diagnostics/EditorProof_Palette.png";
        if (stbi_write_png(PaletteSheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the palette sheet would not write\n");
            return 1;
        }
        int Box = 0, Indigo = 0;
        for (int Y = 300; Y < 640; ++Y)
            for (int X = 355; X < 925; ++X)
            {
                const unsigned char* P = At(X, Y);
                if (std::abs(static_cast<int>(P[0]) - 12) <= 3
                    && std::abs(static_cast<int>(P[1]) - 12) <= 3
                    && std::abs(static_cast<int>(P[2]) - 12) <= 3)
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
    }


    // Gate 8 — the views menu opens and snaps: the pill raises eight rows, and each compass row poses
    //    the orbit (checked here against the solver's own euler).
    Click(532.0f, 108.0f);
    Rest(14);
    Rasterise();
    {
        const char* ViewsSheet = "Diagnostics/EditorProof_Views.png";
        if (stbi_write_png(ViewsSheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::fprintf(stderr, "[EditorProof] [FAIL] the views sheet would not write\n");
            return 1;
        }
        int Black = 0;
        for (int Y = 140; Y < 380; ++Y)
            for (int X = 530; X < 680; ++X)
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
            Click(607.0f, RowY);
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
            Click(532.0f, 108.0f);   // the pill again: the next pick reopens the menu
            Rest(3);
        };
        PickView(331.0f, 5u, 0.0f, -1.5707963f, false);
        PickView(211.0f, 1u, 0.0f, 0.0f, false);
        PickView(241.0f, 2u, 3.1415927f, 0.0f, false);
        PickView(271.0f, 3u, -1.5707963f, 0.0f, false);
        PickView(301.0f, 4u, 1.5707963f, 0.0f, false);
        PickView(361.0f, 6u, 0.0f, 1.5707963f, false);
        PickView(172.0f, 0u, 0.0f, 0.0f, true);
        PickView(142.0f, 0u, 0.0f, 0.0f, false);
        Click(500.0f, 500.0f);   // dismiss the reopened menu off the empty view
        Rest(3);
    }

    // Gate 9 — the gizmo answers: a pad tap snaps its view, a drag orbits, and the wheel dollies.
    Click(937.0f, 588.0f);
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
        Tick(917.0f, 587.0f, false);
        Tick(917.0f, 587.0f, false);
        Tick(917.0f, 587.0f, true);
        for (int i = 1; i <= 8; ++i)
            Tick(917.0f + 5.0f * static_cast<float>(i), 587.0f, true);
        Tick(957.0f, 587.0f, false);
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

    if (Failed)
    {
        std::fprintf(stderr, "[EditorProof] [FAIL] wrote %s, but the sheet disagrees with its caption\n", Sheet);
        return 1;
    }
    std::fprintf(stderr, "[EditorProof] wrote %s: three columns, trapezoid tabs, titled strips, seated tints\n", Sheet);
    return 0;
}
