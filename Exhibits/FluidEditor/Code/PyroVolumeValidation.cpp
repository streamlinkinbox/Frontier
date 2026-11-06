/*==========================================================================================================================================
                                                       PYROVOLUMEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The Fluid editor's footing, checked without a window: the catalogue transcribed from Experimental/Fluid (18 presets, 85 settings, 82 controls and the
//    scene codec), the orbit camera, and the Pyro solver on a Vulkan device (initialised, stepped, rendered, cleared, baked). The device half needs a
//    Vulkan loader (FRONTIER_VULKAN_LIB, or the system's); --require-vulkan makes its absence a failure, without it the half is reported as skipped.
//
//        PyroVolumeValidation [--require-vulkan]        (run from the repository root: it reads Experimental/Fluid/Samples)

#include "Frontier/Engine/Interface/FluidEditor/FluidEditorControls.h"
#include "Frontier/Engine/Rendering/VulkanContext.h"
#include "Frontier/Engine/VolumetricDynamics/GasPresetLibrary.h"
#include "Frontier/Engine/VolumetricDynamics/GasSceneCodec.h"
#include "Frontier/Engine/VolumetricDynamics/PyroCamera.h"
#include "Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;

int Assertions = 0;
int Failures   = 0;

void Verify(bool Condition, const std::string& Caption)
{
    ++Assertions;
    if (!Condition) ++Failures;
    std::printf("%s\t%s\n", Condition ? "PASS" : "FAIL", Caption.c_str());
}

std::string ReadWhole(const std::string& Path)
{
    std::ifstream In(Path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(In), std::istreambuf_iterator<char>());
}

std::string SpellAll(const GasSettings& Settings)
{
    std::string Text;
    GasSceneDetail::SpellSettings(Settings, Text);
    return Text;
}

size_t CountLines(const std::string& Text)
{
    return static_cast<size_t>(std::count(Text.begin(), Text.end(), '\n'));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ① THE CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

void ValidateCatalogue()
{
    using namespace FluidEditor;
    Verify(GasPresetCount == 18u && FluidPresetCardCount == 18u, "18 presets in the library and 18 cards in the rail");
    Verify(GasSettingCount == 85u && CountLines(SpellAll(GasSettings{})) == 85u, "85 settings, and the codec spells exactly 85 of them");
    Verify(FluidControlCount == 82u, "82 inspector controls");

    // 📝 The rail and the library are two transcriptions of one list: the same eighteen identities (the rail keeps the prototype's display order, the
    //    library the order of presets.js).
    bool Matching = true, Categorised = true, Distinct = true;
    for (uint32_t Slot = 0; Slot < FluidPresetCardCount; ++Slot)
    {
        const PresetCard& Card = FluidPresetCards()[Slot];
        Matching    = Matching && FindGasPreset(Card.Identity) != nullptr;
        Categorised = Categorised && (!std::strcmp(Card.Category, "fire") || !std::strcmp(Card.Category, "smoke") || !std::strcmp(Card.Category, "blast"));
        for (uint32_t Other = 0; Other < Slot; ++Other) Distinct = Distinct && std::strcmp(FluidPresetCards()[Other].Identity, Card.Identity) != 0;
    }
    Verify(Matching && Distinct, "every card names a different preset of the library: the 18 cards cover the 18 presets");
    Verify(Categorised, "every card is a fire, a smoke or a blast");

    // 📝 Every control reads, and writes within its own range: a range reaches both ends, a toggle both states, a choice each of its options.
    int Ranges = 0, Toggles = 0, Choices = 0, Wrong = 0;
    for (uint32_t Index = 0; Index < FluidControlCount; ++Index)
    {
        const ControlRow& Row = FluidControls()[Index];
        GasSettings       Settings{};
        float             Reading = 0.0f;
        if (!ReadSetting(Settings, Row.Key, Reading)) { ++Wrong; continue; }
        if (Row.Form == ControlForm::Range)
        {
            ++Ranges;
            const bool Low  = WriteSetting(Settings, Row.Key, Row.Low - 100.0f) && ReadSetting(Settings, Row.Key, Reading) && std::fabs(Reading - Row.Low) < 1.0e-3f * std::max(1.0f, std::fabs(Row.Low));
            const bool High = WriteSetting(Settings, Row.Key, Row.High + 100.0f) && ReadSetting(Settings, Row.Key, Reading) && std::fabs(Reading - Row.High) < 1.0e-3f * std::max(1.0f, std::fabs(Row.High));
            if (!Low || !High) ++Wrong;
        }
        else if (Row.Form == ControlForm::Toggle)
        {
            ++Toggles;
            const bool On  = WriteSetting(Settings, Row.Key, 1.0f) && ReadSetting(Settings, Row.Key, Reading) && Reading == 1.0f;
            const bool Off = WriteSetting(Settings, Row.Key, 0.0f) && ReadSetting(Settings, Row.Key, Reading) && Reading == 0.0f;
            if (!On || !Off) ++Wrong;
        }
        else
        {
            ++Choices;
            for (uint32_t Option = 0; Option < Row.ChoiceCount; ++Option)
            {
                const float Ordinal = static_cast<float>(Row.Choices[Option].Ordinal);
                if (!WriteSetting(Settings, Row.Key, Ordinal) || !ReadSetting(Settings, Row.Key, Reading) || Reading != Ordinal) ++Wrong;
            }
        }
    }
    Verify(Wrong == 0 && Ranges + Toggles + Choices == static_cast<int>(FluidControlCount),
           "all 82 controls read and write inside their ranges (" + std::to_string(Ranges) + " ranges, " + std::to_string(Toggles) + " toggles, " + std::to_string(Choices) + " choices)");

    // 📝 A preset round-trips through the file format with every one of its 85 settings, not three of them.
    bool Lossless = true;
    for (uint32_t Slot = 0; Slot < GasPresetCount; ++Slot)
    {
        GasScene Scene;
        Scene.Name     = GasPresetLibrary()[Slot].Name;
        Scene.Settings = ConstructPresetSettingsBySlot(Slot);
        GasScene              Parsed;
        const GasSceneReading Reading = ReadGasScene(WriteGasScene(Scene), Parsed);
        Lossless = Lossless && Reading.Accepted && Reading.UnknownSettings.empty() && SpellAll(Parsed.Settings) == SpellAll(Scene.Settings) && Parsed.Name == Scene.Name;
    }
    Verify(Lossless, "all 18 presets round-trip through .gasscene.toml with all 85 settings equal");

    // 📝 The eight scenes the prototype ships: each is read, none carries a setting this build does not know.
    int          Read = 0, Unknown = 0, WithCamera = 0;
    const char*  Samples[] = { "camp_fire_steady", "cold_dust_fall", "deflector_obstacle", "far_cheap_plume", "fracture_dust_burst", "hero_detonation", "open_bounds_megaton", "tyre_burnout_ring" };
    for (const char* Sample : Samples)
    {
        GasScene              Scene;
        const GasSceneReading Reading = ReadGasScene(ReadWhole(std::string("Experimental/Fluid/Samples/") + Sample + ".gasscene.toml"), Scene);
        if (Reading.Accepted) ++Read;
        Unknown += static_cast<int>(Reading.UnknownSettings.size());
        if (Reading.Accepted && Scene.CameraPresent) ++WithCamera;
    }
    Verify(Read == 8 && Unknown == 0, "the 8 sample scenes of Experimental/Fluid are read, " + std::to_string(Unknown) + " unknown settings (" + std::to_string(WithCamera) + " frame their own camera)");

    // 📝 What the codec refuses, and what it only notes.
    GasScene     Scene;
    const auto   Good = WriteGasScene(GasScene{});
    Verify(!ReadGasScene("format = \"something-else\"\nversion = 1\nname = \"x\"\n", Scene).Accepted, "a file with another format marker is refused");
    Verify(!ReadGasScene("format = \"frontier-fluid-scene\"\nversion = 2\nname = \"x\"\n", Scene).Accepted, "a file of another version is refused");
    Verify(!ReadGasScene("format = \"frontier-fluid-scene\"\nversion = 1\nname = \"\"\n", Scene).Accepted, "a scene without a name is refused");
    {
        const GasSceneReading Wrong = ReadGasScene("format = \"frontier-fluid-scene\"\nversion = 1\nname = \"x\"\n[settings]\ngridResolution = 48.5\n", Scene);
        Verify(!Wrong.Accepted && Wrong.RefusedSettings.size() == 1u, "a float where a voxel count is wanted is refused, and named");
    }
    {
        const GasSceneReading Extra = ReadGasScene("format = \"frontier-fluid-scene\"\nversion = 1\nname = \"x\"\n[settings]\nnotASetting = 3\nbuoyancy = 2\n", Scene);
        Verify(Extra.Accepted && Extra.UnknownSettings.size() == 1u && Scene.Settings.Buoyancy == 2.0f, "an unknown setting is noted and the rest is read (an integer stands for a float)");
    }
    Verify(!Good.empty() && ReadGasScene(Good, Scene).Accepted, "a default scene is written and read back");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ② THE CAMERA
//------------------------------------------------------------------------------------------------------------------------

float Dot(const std::array<float, 3>& A, const std::array<float, 3>& B)
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

void ValidateCamera()
{
    PyroOrbitCamera Camera;
    const PyroCameraFrame Frame = Camera.ComputeFrame();
    Verify(std::fabs(Dot(Frame.Forward, Frame.Forward) - 1.0f) < 1.0e-5f && std::fabs(Dot(Frame.Right, Frame.Right) - 1.0f) < 1.0e-5f && std::fabs(Dot(Frame.Up, Frame.Up) - 1.0f) < 1.0e-5f &&
               std::fabs(Dot(Frame.Forward, Frame.Right)) < 1.0e-5f && std::fabs(Dot(Frame.Forward, Frame.Up)) < 1.0e-5f && std::fabs(Dot(Frame.Right, Frame.Up)) < 1.0e-5f,
           "the frame's forward, right and up are unit vectors at right angles");
    const float Away = std::sqrt(Dot({ Frame.Position[0] - Camera.Target[0], Frame.Position[1] - Camera.Target[1], Frame.Position[2] - Camera.Target[2] },
                                     { Frame.Position[0] - Camera.Target[0], Frame.Position[1] - Camera.Target[1], Frame.Position[2] - Camera.Target[2] }));
    Verify(std::fabs(Away - Camera.Distance) < 1.0e-4f, "the eye lies one orbit distance from the target");

    const float Before = Camera.Theta;
    Camera.OrbitByPixels(100.0f, 0.0f);
    Verify(std::fabs((Before - Camera.Theta) - 0.65f) < 1.0e-5f, "a drag of 100 px turns the camera 0.65 rad (0.0065 rad a pixel)");
    Camera.OrbitByPixels(0.0f, -100000.0f);
    Verify(Camera.Phi <= 3.14159265f - 0.08f + 1.0e-6f, "the orbit stops short of the pole");
    Camera.OrbitByPixels(0.0f, 100000.0f);
    Verify(Camera.Phi >= 0.08f - 1.0e-6f, "and of the other pole");

    PyroOrbitCamera Zoomed;
    for (int Notch = 0; Notch < 200; ++Notch) Zoomed.ZoomByWheel(1.0f);
    const float Nearest = Zoomed.Distance;
    for (int Notch = 0; Notch < 400; ++Notch) Zoomed.ZoomByWheel(-1.0f);
    Verify(std::fabs(Nearest - 0.85f) < 1.0e-5f && std::fabs(Zoomed.Distance - 12.0f) < 1.0e-5f, "the wheel zooms between 0.85 and 12 domain units");

    PyroOrbitCamera Pan;
    const std::array<float, 3> From = Pan.Target;
    Pan.PanByPixels(40.0f, 0.0f);
    Verify(std::fabs(Pan.Target[0] - From[0]) + std::fabs(Pan.Target[1] - From[1]) + std::fabs(Pan.Target[2] - From[2]) > 1.0e-3f, "a pan moves the target");

    PyroOrbitCamera Aimed;
    std::array<float, 3> Point{};
    Verify(Aimed.RaycastBrushPoint(0.0f, 0.0f, Point) && std::fabs(Point[0] - Aimed.Target[0]) + std::fabs(Point[1] - Aimed.Target[1]) + std::fabs(Point[2] - Aimed.Target[2]) < 1.0e-3f,
           "the ray through the middle of the view meets the target's plane at the target");

    PyroOrbitCamera Views[4];
    for (int View = 0; View < 4; ++View) Views[View].ApplyPresetView(View);
    Verify(Views[0].Theta == PyroOrbitCamera{}.Theta && Views[1].Theta == 0.0f && std::fabs(Views[2].Theta - 1.57079633f) < 1.0e-5f && Views[3].Phi < 0.2f, "the four views: perspective, front, side, top");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ③ THE SOLVER ON A DEVICE
//------------------------------------------------------------------------------------------------------------------------

// 📝 How many pixels carry colour a plain backdrop does not: bright and warm.
size_t CountLuminous(const std::vector<uint8_t>& Rgba)
{
    size_t Count = 0;
    for (size_t At = 0; At + 3 < Rgba.size(); At += 4)
        if (Rgba[At] > 150 && Rgba[At] > Rgba[At + 2] + 40) ++Count;
    return Count;
}

bool Varies(const std::vector<uint8_t>& Rgba)
{
    if (Rgba.size() < 8) return false;
    uint8_t Low = 255, High = 0;
    for (size_t At = 0; At + 3 < Rgba.size(); At += 4)
    {
        const uint8_t Level = static_cast<uint8_t>((Rgba[At] + Rgba[At + 1] + Rgba[At + 2]) / 3);
        Low  = std::min(Low, Level);
        High = std::max(High, Level);
    }
    return High - Low > 40;
}

bool Render(VulkanContext& Context, PyroVolumeSolver& Solver, const GasSettings& Settings, std::vector<uint8_t>& Rgba)
{
    PyroOrbitCamera Camera;
    int             Width = 0, Height = 0;
    return Solver.RenderStandaloneViewport(Context, Settings, Camera.ComputeFrame(16.0f / 9.0f), 320, 180, false) && Solver.ReadViewportRgba(Context, Rgba, Width, Height) && Width == 320 && Height == 180;
}

void ValidateSolver(bool RequireVulkan)
{
    VulkanContext Context;
    std::string   Diagnostic;
    if (!Context.InitializeHeadless(Diagnostic))
    {
        if (RequireVulkan) Verify(false, "a Vulkan 1.1 device comes up (" + Diagnostic + ")");
        else std::printf("INFO\tno Vulkan device (%s): the solver half is skipped\n", Diagnostic.c_str());
        return;
    }
    std::printf("INFO\tVulkan 1.1 on %s\n", Context.QueryDeviceSummary().c_str());

    GasSettings Settings       = ConstructPresetSettings("ue5_pyro_default");
    Settings.LatticeResolution = 32;
    Settings.PressureIterations = 12;
    Settings.RaymarchSteps     = 56;

    std::vector<uint8_t> First, Second, Empty;
    {
        PyroVolumeSolver Solver;
        Verify(Solver.Initialize(Context, Settings, Diagnostic), "the solver builds its 15 pipelines and its lattice (" + Diagnostic + ")");
        Verify(Solver.QueryLatticeResolution() == 32 && Solver.QueryTilesX() * Solver.QueryTilesY() >= 32, "a 32-cell lattice laid out in tiles of one atlas");
        Verify(Render(Context, Solver, Settings, Empty) && !Empty.empty(), "an empty lattice renders");
        const size_t EmptyLuminous = CountLuminous(Empty);
        const auto   Begun         = std::chrono::steady_clock::now();
        for (int Step = 0; Step < 20; ++Step) Solver.AdvanceSimulation(Context, Settings, 1.0f / 30.0f);
        const double StepMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begun).count() / 20.0;
        Verify(Solver.QueryStepCount() >= 20u && Solver.QuerySimulationSeconds() > 0.6f, "twenty steps advance the clock (" + std::to_string(Solver.QueryStepCount()) + " steps, " + std::to_string(Solver.QuerySimulationSeconds()) + " s)");
        std::printf("INFO\tone step at 32^3: %.1f ms (this device: a record, not a performance claim)\n", StepMilliseconds);
        Verify(Render(Context, Solver, Settings, First) && Varies(First), "the volume renders, and the picture varies");
        Verify(CountLuminous(First) > EmptyLuminous + 200u, "a flame lights the picture: " + std::to_string(CountLuminous(First)) + " warm bright pixels against " + std::to_string(EmptyLuminous) + " when empty");

        Solver.ClearLattice(Context);
        std::vector<uint8_t> Cleared;
        Verify(Render(Context, Solver, Settings, Cleared) && CountLuminous(Cleared) <= EmptyLuminous + 20u, "clearing the lattice takes the flame out of the picture");

        Solver.EnsureLatticeResolution(Context, 48);
        Verify(Solver.QueryLatticeResolution() == 48, "the lattice is rebuilt at 48 cells on request");
        Solver.EnsureLatticeResolution(Context, 32);
        Solver.ClearLattice(Context);

        // 📝 A burst enlarges a domain that expands around bursts, and the bounds say by how much.
        GasSettings Burst        = ConstructPresetSettings("megaton_open_bounds");
        Burst.LatticeResolution  = 32;
        Burst.PressureIterations = 12;
        const PyroBoundsBox Rest = Solver.ComputeBoundsBox(Burst);
        Solver.TriggerBurstSequence(Burst, "mushroom");
        for (int Step = 0; Step < 30; ++Step) Solver.AdvanceSimulation(Context, Burst, 1.0f / 30.0f);
        const PyroBoundsBox Surged = Solver.ComputeBoundsBox(Burst);
        Verify(!Burst.DynamicBounds || Surged.EffectiveWidth >= Rest.EffectiveWidth, "a burst leaves the domain no smaller (" + std::to_string(Rest.EffectiveWidth) + " m to " + std::to_string(Surged.EffectiveWidth) + " m)");

        std::vector<uint8_t> Sheet;
        Verify(Solver.BakeFlipbookSheet(Context, Settings, PyroOrbitCamera{}, 2, 2, 48, 1.0f / 25.0f, Sheet) && Sheet.size() == 96u * 96u * 4u, "a 2 x 2 flipbook of 48 px frames is baked: 96 x 96 RGBA");
        Solver.Shutdown(Context);
    }
    {
        // 📝 The same steps on a new solver give the same picture, byte for byte: nothing in the solver depends on the clock or on what ran before.
        PyroVolumeSolver Solver;
        (void)Solver.Initialize(Context, Settings, Diagnostic);
        for (int Step = 0; Step < 20; ++Step) Solver.AdvanceSimulation(Context, Settings, 1.0f / 30.0f);
        (void)Render(Context, Solver, Settings, Second);
        Verify(!First.empty() && First == Second, "a second solver given the same twenty steps renders the same picture, byte for byte");
        Solver.Shutdown(Context);
    }
    Context.Shutdown();
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    bool RequireVulkan = false;
    for (int At = 1; At < ArgumentCount; ++At)
        if (std::strcmp(Arguments[At], "--require-vulkan") == 0) RequireVulkan = true;

    ValidateCatalogue();
    ValidateCamera();
    ValidateSolver(RequireVulkan);
    std::printf("[PyroVolumeValidation] %d assertions, %d failed\n", Assertions, Failures);
    return Failures == 0 ? 0 : 1;
}
