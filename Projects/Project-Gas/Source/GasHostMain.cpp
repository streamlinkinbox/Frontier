//==============================================================================================================================================
//                                                           GASHOSTMAIN.CPP
//==============================================================================================================================================
// 📦 The standalone gas host: opens an authored scene, runs it, renders it, writes it back — with no window, no Vulkan and no SDK.
//
// 💡 WHY A SEPARATE HOST EXISTS AT ALL, WHEN Frontier.exe WILL EVENTUALLY DO THIS.
//    Frontier.exe needs a Vulkan loader, a swapchain, a patched ImGui and a GPU. A machine working on the gas
//    solver needs none of those, and asking for them is how a solver stops being worked on. FRONTIER_GAS_ONLY
//    builds this and nothing else: four engine headers, toml++, a PNG writer, and a main(). It compiles in
//    seconds on a laptop with no graphics driver, which is the whole point, and it is the same code the engine
//    will call — the headers are the product, this is a caller.
//
// 🔴 THE HOST DECIDES NOTHING. Every conversion it needs comes from GasSceneResolve.h, every reading from
//    GasSceneCodec.h, every advance from CoarseGasField.h. If a number appears in this file that is not an
//    argument or a report, it is in the wrong file — a standalone tool that quietly tunes its own copy of the
//    effect is worse than no standalone tool, because its pictures then prove nothing about the engine.
//
// Four verbs, each answering one question a port has to be able to answer:
//
//    read      does this build understand the file, and what did it not understand
//    cross     does writing it back produce the same bytes
//    advance   does it still simulate once the readings are in the solver's units
//    view      and does it look like what was authored
//
// ⚠️ `cross` is the verb that catches the expensive mistakes. It is the same comparison the committed corpus
//    is checked against in CI, exposed here so it can be run against a file that is not in the corpus — which
//    is what anyone porting a new scene actually needs.

#include "CoarseGasField.h"
#include "GasPresetLibrary.h"
#include "GasQualityAllowance.h"
#include "GasSceneCodec.h"
#include "GasSceneResolve.h"
#include "VolumeRaymarch.h"
#include "PngWriteCounterpart.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     FILES AND ARGUMENTS
//------------------------------------------------------------------------------------------------------------------------

// Read whole, or report why not. A scene is kilobytes; streaming it would be ceremony.
bool ReadWholeFile(const char* Path, std::string& OutText)
{
    std::FILE* Handle = std::fopen(Path, "rb");
    if (Handle == nullptr) return false;
    char Chunk[4096];
    std::size_t Taken = 0u;
    while ((Taken = std::fread(Chunk, 1u, sizeof(Chunk), Handle)) > 0u) OutText.append(Chunk, Taken);
    std::fclose(Handle);
    return true;
}


bool WriteWholeFile(const char* Path, const std::string& Text)
{
    std::FILE* Handle = std::fopen(Path, "wb");
    if (Handle == nullptr) return false;
    const std::size_t Written = std::fwrite(Text.data(), 1u, Text.size(), Handle);
    std::fclose(Handle);
    return Written == Text.size();
}


// An option's reading, or the fallback. Options are `--name reading`; anything else is positional.
const char* OptionReading(int Count, char** Arguments, const char* Name, const char* Fallback)
{
    for (int Entry = 1; Entry + 1 < Count; ++Entry)
        if (std::strcmp(Arguments[Entry], Name) == 0) return Arguments[Entry + 1];
    return Fallback;
}


bool OptionPresent(int Count, char** Arguments, const char* Name)
{
    for (int Entry = 1; Entry < Count; ++Entry)
        if (std::strcmp(Arguments[Entry], Name) == 0) return true;
    return false;
}


// The first argument that is neither a verb nor part of an option. The scene path, in practice.
const char* PositionalReading(int Count, char** Arguments)
{
    for (int Entry = 2; Entry < Count; ++Entry)
    {
        if (Arguments[Entry][0] == '-' && Arguments[Entry][1] == '-') { ++Entry; continue; }
        return Arguments[Entry];
    }
    return nullptr;
}


// A scene from a path, with every refusal and every surprise printed. Returns false on a refusal, which is
//    the one case where the caller must stop: an unknown setting is a remark, a refused file is not.
bool OpenScene(const char* Path, GasScene& OutScene)
{
    std::string Text;
    if (!ReadWholeFile(Path, Text))
    {
        std::printf("  🔴 cannot open %s\n", Path);
        return false;
    }

    const GasSceneReading Reading = ReadGasScene(Text, OutScene);
    for (const std::string& Name : Reading.UnknownSettings)
        std::printf("  ⚠️  setting this build does not know, left at its default: %s\n", Name.c_str());
    for (const std::string& Name : Reading.RefusedSettings)
        std::printf("  ⚠️  setting of the wrong type, left at its default: %s\n", Name.c_str());

    if (!Reading.Accepted)
    {
        std::printf("  🔴 refused: %s\n", Reading.Refusal.c_str());
        return false;
    }
    return true;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        REPORTING
//------------------------------------------------------------------------------------------------------------------------

const char* QualityName(GasQuality Quality)
{
    switch (Quality)
    {
        case GasQuality::Hero:     return "Hero";
        case GasQuality::Near:     return "Near";
        case GasQuality::Mid:      return "Mid";
        case GasQuality::Far:      return "Far";
        case GasQuality::Flipbook: return "Flipbook";
    }
    return "unknown";
}


void ReportScene(const GasScene& Scene, const GasSceneRun& Run)
{
    std::printf("  name              %s\n", Scene.Name.c_str());
    std::printf("  outliner          %s · %s · %s · %s\n", Scene.Names.Domain.c_str(), Scene.Names.Emitter.c_str(),
                Scene.Names.Collider.c_str(), Scene.Names.Sun.c_str());
    std::printf("  authored box      %.3f × %.3f × %.3f m at full surge\n", Run.Measure.Width, Run.Measure.Height,
                Run.Measure.Width);
    std::printf("  solver cube       %.3f m, %u³ voxels, %.3f m a voxel\n", Run.Measure.Span, CoarseExtent,
                Run.Measure.Span / static_cast<float>(CoarseExtent));
    std::printf("  authored lattice  %d³, which is the fine field and not this one\n", Scene.Settings.LatticeResolution);

    if (Scene.Settings.EmitterEnabled)
        std::printf("  emitter           r %.3f m at (%.2f, %.2f, %.2f), %.1f smoke/s, %.1f K/s\n", Run.Emitter.Radius,
                    static_cast<double>(Run.Emitter.Position[0]), static_cast<double>(Run.Emitter.Position[1]),
                    static_cast<double>(Run.Emitter.Position[2]), static_cast<double>(Run.Emitter.SmokeRate),
                    static_cast<double>(Run.Emitter.TemperatureRate));
    else
        std::printf("  emitter           switched off by the scene\n");

    if (Run.Obstruction.Present)
    {
        std::printf("  obstruction       %s, r %.3f m at (%.2f, %.2f, %.2f)\n", Run.Obstruction.AuthoredShape,
                    static_cast<double>(Run.Obstruction.Collider.Dimensions[0]),
                    static_cast<double>(Run.Obstruction.Collider.Centre[0]),
                    static_cast<double>(Run.Obstruction.Collider.Centre[1]),
                    static_cast<double>(Run.Obstruction.Collider.Centre[2]));
        if (Run.Obstruction.ShapeApproximated)
            std::printf("                    🚩 admitted as its bounding box; no axis-2 primitive lies along X\n");
        if (Run.Obstruction.OrientationApproximated)
            std::printf("                    🚩 admitted about axis 2; the authored ring stands upright\n");
    }
    else
    {
        std::printf("  obstruction       none\n");
    }

    std::printf("  sun               %.1f° azimuth, %.1f° elevation → (%.2f, %.2f, %.2f)\n",
                static_cast<double>(Scene.Settings.SunAzimuth), static_cast<double>(Scene.Settings.SunElevation),
                static_cast<double>(Run.SunDirection[0]), static_cast<double>(Run.SunDirection[1]),
                static_cast<double>(Run.SunDirection[2]));

    // What the budget would grant this domain at a few distances. The host has no viewer, so it reports the
    //    ladder rather than pretending to a position in a level.
    std::printf("  budget            ");
    const float Distances[4] = { 5.0f, 20.0f, 50.0f, 120.0f };
    for (int Entry = 0; Entry < 4; ++Entry)
    {
        const GasQuality Quality = QualityForDistance(Distances[Entry]);
        std::printf("%.0f m → %s (%llu MB)%s", static_cast<double>(Distances[Entry]), QualityName(Quality),
                    static_cast<unsigned long long>(DeviceBytesFor(Quality) / (1024ull * 1024ull)),
                    Entry == 3 ? "\n" : "   ");
    }

    if (Scene.CameraPresent)
        std::printf("  camera            θ %.3f  φ %.3f  d %.3f\n", Scene.Camera.Theta, Scene.Camera.Phi,
                    Scene.Camera.Distance);
    else
        std::printf("  camera            the scene declines to frame itself; the default orbit is used\n");
}


//------------------------------------------------------------------------------------------------------------------------
//                                                       THE PICTURE
//------------------------------------------------------------------------------------------------------------------------

// The seam from the integration to the solver's reading, identical to the one the proof marches, because a
//    picture taken through a different seam would prove something about the seam instead of the scene.
VolumeSample ReadCoarseField(const void* Context, const float Coordinate[3])
{
    const CoarseGasField& Field = *static_cast<const CoarseGasField*>(Context);
    const float Lattice[3] = { Coordinate[0] * static_cast<float>(CoarseExtent) - 0.5f,
                               Coordinate[1] * static_cast<float>(CoarseExtent) - 0.5f,
                               Coordinate[2] * static_cast<float>(CoarseExtent) - 0.5f };
    VolumeSample Reading;
    Reading.Smoke       = ReadTrilinear(Field.Smoke,       Lattice[0], Lattice[1], Lattice[2]);
    Reading.Temperature = ReadTrilinear(Field.Temperature, Lattice[0], Lattice[1], Lattice[2]);
    return Reading;
}


struct Capture
{
    uint32_t Width  = 0u;
    uint32_t Height = 0u;
    std::vector<unsigned char> Pixels;
    float Coverage  = 0.0f;
};


// Renders the field through the scene's own camera. Square, because the authoring viewport's aspect is a
//    property of a browser window and not of the scene.
Capture RenderThroughEye(const CoarseGasField& Field, const GasSceneRun& Run, const GasSceneEye& Eye, uint32_t Extent)
{
    Capture Shot;
    Shot.Width = Shot.Height = Extent;
    Shot.Pixels.assign(static_cast<std::size_t>(Extent) * Extent * 3u, 0u);

    uint32_t Touched = 0u;
    for (uint32_t Row = 0u; Row < Extent; ++Row)
    for (uint32_t Column = 0u; Column < Extent; ++Column)
    {
        const float ScreenX = (static_cast<float>(Column) + 0.5f) / static_cast<float>(Extent) * 2.0f - 1.0f;
        const float ScreenY = 1.0f - (static_cast<float>(Row) + 0.5f) / static_cast<float>(Extent) * 2.0f;

        float Direction[3];
        for (int Axis = 0; Axis < 3; ++Axis)
            Direction[Axis] = Eye.Forward[Axis] + Eye.Right[Axis] * ScreenX * Eye.HalfViewTan
                                                + Eye.Up[Axis]    * ScreenY * Eye.HalfViewTan;
        const float Length = std::sqrt(Direction[0] * Direction[0] + Direction[1] * Direction[1]
                                     + Direction[2] * Direction[2]);
        for (int Axis = 0; Axis < 3; ++Axis) Direction[Axis] /= Length;

        const MarchedRay Marched = MarchVolume(&ReadCoarseField, &Field, Eye.Origin, Direction, Run.SunDirection,
                                               Run.Lighting, Run.Measure.Span);
        if (Marched.Transmittance < 0.999f) ++Touched;
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            const std::size_t Slot = (static_cast<std::size_t>(Row) * Extent + Column) * 3u
                                   + static_cast<std::size_t>(Channel);
            Shot.Pixels[Slot] = TransferToDisplay(Marched.Radiance[Channel], Run.Lighting.Exposure);
        }
    }
    Shot.Coverage = static_cast<float>(Touched) / static_cast<float>(Extent * Extent);
    return Shot;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                         THE VERBS
//------------------------------------------------------------------------------------------------------------------------

// Advances a resolved scene, injecting the blast on the first step when asked. Shared by `advance` and
//    `view`, so the picture is always of a field somebody could have described in words.
void RunAdvances(CoarseGasField& Field, const GasSceneRun& Run, const GasScene& Scene, uint32_t Advances, bool WithBlast)
{
    for (uint32_t Step = 0u; Step < Advances; ++Step)
    {
        if (Step == 0u && WithBlast)
        {
            const GasEmission Burst = ResolveBlast(Scene.Settings, Run.Measure);
            InjectEmission(Field, Burst, CoarseStepInterval);
        }
        AdvanceScene(Field, Run);
    }
}


int VerbRead(const char* Path)
{
    GasScene Scene;
    if (!OpenScene(Path, Scene)) return 1;
    CoarseGasField Field;
    const GasSceneRun Run = ResolveScene(Scene, Field);
    std::printf("\n%s\n", Path);
    ReportScene(Scene, Run);
    return 0;
}


int VerbCross(const char* Path, const char* Into)
{
    GasScene Scene;
    if (!OpenScene(Path, Scene)) return 1;

    std::string Original;
    if (!ReadWholeFile(Path, Original)) return 1;
    const std::string Written = WriteGasScene(Scene);

    if (Into != nullptr && !WriteWholeFile(Into, Written))
    {
        std::printf("  🔴 cannot write %s\n", Into);
        return 1;
    }

    if (Written == Original)
    {
        std::printf("  ✔️  %s crosses unchanged, %zu bytes\n", Path, Written.size());
        return 0;
    }

    // The useful part of a failure is where, not that. A whole-file diff of a sorted key list is unreadable;
    //    the first differing line is one line and names the setting.
    std::size_t Line = 1u, Slot = 0u, LineStart = 0u;
    while (Slot < Written.size() && Slot < Original.size() && Written[Slot] == Original[Slot])
    {
        if (Written[Slot] == '\n') { ++Line; LineStart = Slot + 1u; }
        ++Slot;
    }
    const std::size_t WrittenEnd  = Written.find('\n', LineStart);
    const std::size_t OriginalEnd = Original.find('\n', LineStart);
    std::printf("  🔴 %s does not cross unchanged\n", Path);
    std::printf("     line %zu: this build writes [%s], the file holds [%s]\n", Line,
                Written.substr(LineStart, WrittenEnd - LineStart).c_str(),
                Original.substr(LineStart, OriginalEnd - LineStart).c_str());
    return 1;
}


int VerbAdvance(const char* Path, uint32_t Advances, bool WithBlast)
{
    GasScene Scene;
    if (!OpenScene(Path, Scene)) return 1;

    CoarseGasField Field;
    const GasSceneRun Run = ResolveScene(Scene, Field);
    RunAdvances(Field, Run, Scene, Advances, WithBlast);

    float Hottest = 0.0f, Fastest = 0.0f;
    for (uint32_t Slot = 0u; Slot < CoarseVoxelCount; ++Slot)
    {
        if (Field.Temperature[Slot] > Hottest) Hottest = Field.Temperature[Slot];
        const float Speed = std::sqrt(Field.VelocityX[Slot] * Field.VelocityX[Slot]
                                    + Field.VelocityY[Slot] * Field.VelocityY[Slot]
                                    + Field.VelocityZ[Slot] * Field.VelocityZ[Slot]);
        if (Speed > Fastest) Fastest = Speed;
    }

    std::printf("\n%s — %u advances, %.3f simulated seconds%s\n", Path, Advances,
                static_cast<double>(static_cast<float>(Advances) * CoarseStepInterval),
                WithBlast ? ", detonated on the first" : "");
    std::printf("  total smoke       %.4f\n", static_cast<double>(TotalSmoke(Field)));
    std::printf("  hottest voxel     %.4f K above ambient\n", static_cast<double>(Hottest));
    std::printf("  fastest voxel     %.4f m/s\n", static_cast<double>(Fastest));
    std::printf("  obstructed        %u of %u voxels\n", OccupiedVoxels(Field), CoarseVoxelCount);
    return 0;
}


int VerbView(const char* Path, const char* Picture, uint32_t Extent, uint32_t Advances, bool WithBlast)
{
    GasScene Scene;
    if (!OpenScene(Path, Scene)) return 1;

    CoarseGasField Field;
    const GasSceneRun Run = ResolveScene(Scene, Field);
    RunAdvances(Field, Run, Scene, Advances, WithBlast);

    const GasSceneEye Eye = ResolveEye(Scene.Camera, Run.Measure);
    const Capture Shot = RenderThroughEye(Field, Run, Eye, Extent);

    if (PngWriteCounterpart::WritePng(Picture, static_cast<int>(Shot.Width), static_cast<int>(Shot.Height), 3,
                                      Shot.Pixels.data(), static_cast<int>(Shot.Width) * 3) == 0)
    {
        std::printf("  🔴 cannot write %s\n", Picture);
        return 1;
    }
    std::printf("  ✔️  %s → %s  (%ux%u, %.1f%% covered, %u advances)\n", Path, Picture, Shot.Width, Shot.Height,
                static_cast<double>(Shot.Coverage * 100.0f), Advances);
    return 0;
}


void Usage()
{
    std::printf(
        "Project-Gas — the standalone gas host. No window, no Vulkan, no SDK.\n"
        "\n"
        "  Project-Gas read    <scene%s>\n"
        "  Project-Gas cross   <scene%s> [--into <path>]\n"
        "  Project-Gas advance <scene%s> [--advances N] [--blast]\n"
        "  Project-Gas view    <scene%s> --picture <path.png> [--extent N] [--advances N] [--blast]\n"
        "\n"
        "  read     reports what this build understood, and names what it did not\n"
        "  cross    writes the scene back and compares it byte for byte with the file\n"
        "  advance  runs the coarse field and reports what is in it\n"
        "  view     renders it through the scene's own camera\n",
        GasSceneExtension.data(), GasSceneExtension.data(), GasSceneExtension.data(), GasSceneExtension.data());
}

}   // namespace


int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 2) { Usage(); return 2; }

    const char* Verb  = Arguments[1];
    const char* Scene = PositionalReading(ArgumentCount, Arguments);

    if (std::strcmp(Verb, "--help") == 0 || std::strcmp(Verb, "help") == 0) { Usage(); return 0; }
    if (Scene == nullptr) { std::printf("  🔴 no scene named\n\n"); Usage(); return 2; }

    const uint32_t Advances = static_cast<uint32_t>(std::atoi(OptionReading(ArgumentCount, Arguments, "--advances", "90")));
    const uint32_t Extent   = static_cast<uint32_t>(std::atoi(OptionReading(ArgumentCount, Arguments, "--extent", "320")));
    const bool     Blast    = OptionPresent(ArgumentCount, Arguments, "--blast");

    if (std::strcmp(Verb, "read") == 0)    return VerbRead(Scene);
    if (std::strcmp(Verb, "cross") == 0)   return VerbCross(Scene, OptionReading(ArgumentCount, Arguments, "--into", nullptr));
    if (std::strcmp(Verb, "advance") == 0) return VerbAdvance(Scene, Advances, Blast);
    if (std::strcmp(Verb, "view") == 0)
    {
        const char* Picture = OptionReading(ArgumentCount, Arguments, "--picture", nullptr);
        if (Picture == nullptr) { std::printf("  🔴 view needs --picture\n\n"); Usage(); return 2; }
        return VerbView(Scene, Picture, Extent, Advances, Blast);
    }

    std::printf("  🔴 no such verb: %s\n\n", Verb);
    Usage();
    return 2;
}
