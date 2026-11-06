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

#include <toml++/toml.hpp>

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
//                                          ⓪ THE SETTINGS AGAINST THE PROTOTYPE'S OWN ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

// 📝 Reference/PresetParameters.toml is ConstructPresetParameters(identity) of each preset, and InitialParameters, as the prototype's own code computes
//    them (Code/DumpPresetParameters.mjs). The native presets are built from the generated header: a default that was taken from the wrong place would
//    open every preset on another grid, quality and overlay, which no round trip through the file format can show.
void ValidatePresetsAgainstPrototype()
{
    toml::table Reference;
    try
    {
        Reference = toml::parse(ReadWhole("Exhibits/FluidEditor/Reference/PresetParameters.toml"));
    }
    catch (const toml::parse_error&)
    {
        Verify(false, "Exhibits/FluidEditor/Reference/PresetParameters.toml is readable (run from the repository root)");
        return;
    }
    const auto Expect = [&](const char* Title, GasSettings& Expected, int& Read, int& Unread) {
        const toml::table* Table = Reference[Title].as_table();
        if (Table == nullptr) { ++Unread; return; }
        for (const auto& [Key, Entry] : *Table)
        {
            if (GasSceneDetail::CrossSetting(Expected, Key.str(), Entry) == GasSceneDetail::Crossing::Read) ++Read;
            else ++Unread;
        }
    };
    int         Read = 0, Unread = 0, Differing = 0;
    std::string First;
    GasSettings Initial{};
    int         InitialRead = 0, InitialUnread = 0;
    Expect("initial", Initial, InitialRead, InitialUnread);
    GasSettings Native{};
    // 📝 The defaults of the struct are the editor's InitialParameters, in all 85 settings.
    Verify(InitialRead == 85 && InitialUnread == 0 && SpellAll(Native) == SpellAll(Initial),
           "GasSettings' defaults equal the prototype's InitialParameters in all 85 settings (grid " + std::to_string(Native.LatticeResolution) + ", " +
               std::to_string(Native.RaymarchSteps) + " march steps, no minimap, no grid lines, no bounding box, no collider motion)");
    for (uint32_t Slot = 0; Slot < GasPresetCount; ++Slot)
    {
        const char* Identity = GasPresetLibrary()[Slot].Identity;
        GasSettings Expected{};
        Expect(Identity, Expected, Read, Unread);
        if (SpellAll(ConstructPresetSettings(Identity)) != SpellAll(Expected))
        {
            ++Differing;
            if (First.empty()) First = Identity;
        }
    }
    Verify(Read == static_cast<int>(GasPresetCount * GasSettingCount) && Unread == 0,
           "the reference holds all 18 presets with all 85 settings each, every one read by the codec");
    Verify(Differing == 0, "all 18 presets built natively equal the prototype's ConstructPresetParameters in all 85 settings" + (First.empty() ? std::string() : " (first to differ: " + First + ")"));
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

// 📝 The nth reading of a TOML array of numbers (integers and decimals alike).
std::vector<double> ReadNumbers(const toml::table& Table, const char* Key)
{
    std::vector<double> Readings;
    if (const toml::array* Array = Table[Key].as_array())
    {
        for (const toml::node& Entry : *Array)
        {
            if (const auto Decimal = Entry.value<double>()) Readings.push_back(*Decimal);
            else if (const auto Whole = Entry.value<int64_t>()) Readings.push_back(static_cast<double>(*Whole));
        }
    }
    return Readings;
}

double ReadNumber(const toml::table& Table, const char* Key)
{
    return Table[Key].value_or(0.0);
}

bool Near(double Native, double Prototype, double Tolerance)
{
    return std::fabs(Native - Prototype) <= Tolerance * std::max(1.0, std::fabs(Prototype));
}

bool NearAll(const std::array<float, 3>& Native, const std::vector<double>& Prototype, double Tolerance)
{
    if (Prototype.size() != 3) return false;
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        if (!Near(Native[Axis], Prototype[Axis], Tolerance)) return false;
    }
    return true;
}

// 📝 Reference/CameraTrace.toml is the prototype's OrbitCamera driven through a script by its own code (Code/DumpCameraTrace.mjs). The native camera is driven
//    through the same script and held to every number it wrote down: the eye, the frame, the view and the aim after each step, the ray hits and the projected
//    bounds.
void ValidateCameraAgainstPrototype()
{
    toml::table Trace;
    try
    {
        Trace = toml::parse(ReadWhole("Exhibits/FluidEditor/Reference/CameraTrace.toml"));
    }
    catch (const toml::parse_error&)
    {
        Verify(false, "Exhibits/FluidEditor/Reference/CameraTrace.toml is readable (run from the repository root)");
        return;
    }
    const toml::array* Steps = Trace["step"].as_array();
    Verify(Steps != nullptr && Steps->size() > 100, "the camera trace holds the prototype's steps (" + std::to_string(Steps != nullptr ? Steps->size() : 0) + ")");
    if (Steps == nullptr) return;

    PyroOrbitCamera Camera;
    size_t          Compared = 0, Hits = 0, Misses = 0, Projections = 0, Mismatched = 0;
    std::string     FirstMismatch;
    const auto      Note = [&](size_t Index, const std::string& Op, const char* What)
    {
        ++Mismatched;
        if (FirstMismatch.empty()) FirstMismatch = "step " + std::to_string(Index) + " (" + Op + "): " + What;
    };
    for (size_t Index = 0; Index < Steps->size(); ++Index)
    {
        const toml::table* Step = (*Steps)[Index].as_table();
        if (Step == nullptr) continue;
        const std::string         Op        = std::string(Step->at("op").value_or(std::string_view{}));
        const std::vector<double> Arguments = ReadNumbers(*Step, "arguments");
        const auto                Argument  = [&](size_t At) { return At < Arguments.size() ? static_cast<float>(Arguments[At]) : 0.0f; };
        std::vector<double>       Hit       = ReadNumbers(*Step, "hit");
        bool                      Ray       = false;
        std::array<float, 3>      Uvw{};
        bool                      Landed    = false;
        std::vector<std::array<float, 4>> Segments;
        if (Op == "orbit") Camera.OrbitByPixels(Argument(0), Argument(1));
        else if (Op == "pan") Camera.PanByPixels(Argument(0), Argument(1));
        else if (Op == "zoom") Camera.ZoomByWheel(Argument(0));
        else if (Op == "update") Camera.Advance(Argument(0));
        else if (Op == "reset") Camera.Reset();
        else if (Op == "aim")
        {
            Camera.TargetTheta = Argument(0);
            Camera.TargetPhi   = Argument(1);
        }
        else if (Op == "focus") Camera.FocusVolume(Argument(0), Argument(1));
        else if (Op == "place")
        {
            Camera.Target         = { Argument(0), Argument(1), Argument(2) };
            Camera.TargetCentre   = Camera.Target;
            Camera.Distance       = Argument(3);
            Camera.TargetDistance = Argument(3);
            Camera.Snap();
        }
        else if (Op == "ray")
        {
            Ray    = true;
            Landed = Camera.RaycastVolumeUvw(Argument(0), Argument(1), { Argument(2), Argument(3), Argument(4) }, { Argument(5), Argument(6), Argument(7) }, Uvw);
        }
        else if (Op == "project")
        {
            Camera.Aspect = 16.0f / 9.0f;
            ProjectBounds(Camera.ComputeBasis(), { Argument(2), Argument(3), Argument(4) }, { Argument(5), Argument(6), Argument(7) }, Argument(0), Argument(1), Segments);
        }
        else if (Op != "start") Note(Index, Op, "an operation the native camera has no counterpart for");

        const PyroCameraBasis Basis = Camera.ComputeBasis();
        const double          Tight = 2.0e-4;
        bool                  Same  = Near(Camera.Theta, ReadNumber(*Step, "theta"), Tight) && Near(Camera.Phi, ReadNumber(*Step, "phi"), Tight) &&
                     Near(Camera.Distance, ReadNumber(*Step, "distance"), Tight) && NearAll(Camera.Target, ReadNumbers(*Step, "target"), Tight) &&
                     Near(Camera.TargetTheta, ReadNumber(*Step, "aim_theta"), Tight) && Near(Camera.TargetPhi, ReadNumber(*Step, "aim_phi"), Tight) &&
                     Near(Camera.TargetDistance, ReadNumber(*Step, "aim_distance"), Tight) && NearAll(Camera.TargetCentre, ReadNumbers(*Step, "aim_centre"), Tight);
        if (!Same) Note(Index, Op, "the view or the aim differs from the prototype's");
        if (!NearAll(Basis.Position, ReadNumbers(*Step, "position"), Tight) || !NearAll(Basis.Forward, ReadNumbers(*Step, "forward"), Tight) ||
            !NearAll(Basis.Right, ReadNumbers(*Step, "right"), Tight) || !NearAll(Basis.Up, ReadNumbers(*Step, "up"), Tight))
            Note(Index, Op, "the eye or the basis differs from the prototype's");
        if (Ray)
        {
            if (Landed != (Hit.size() == 3)) Note(Index, Op, "the ray hit or missed where the prototype's did not");
            else if (Landed && !NearAll(Uvw, Hit, Tight)) Note(Index, Op, "the point the ray lands on differs");
            (Landed ? Hits : Misses) += 1;
        }
        if (Op == "project")
        {
            ++Projections;
            const toml::array* Expected = Step->at("segments").as_array();
            bool               Match    = Expected != nullptr && Expected->size() == Segments.size();
            for (size_t Line = 0; Match && Line < Segments.size(); ++Line)
            {
                const toml::array* Pair = (*Expected)[Line].as_array();
                if (Pair == nullptr || Pair->size() != 4) { Match = false; break; }
                for (size_t Coordinate = 0; Coordinate < 4; ++Coordinate)
                    Match = Match && Near(Segments[Line][Coordinate], (*Pair)[Coordinate].value_or(0.0), 1.0e-3);
            }
            if (!Match) Note(Index, Op, "the projected bounds differ from the prototype's");
        }
        ++Compared;
    }
    Verify(Mismatched == 0, "the native camera follows the prototype's through all " + std::to_string(Compared) + " scripted steps" +
                                (FirstMismatch.empty() ? std::string{} : " (first difference: " + FirstMismatch + ")"));
    Verify(Hits >= 6 && Misses >= 3 && Projections == 3, "the script exercises hits (" + std::to_string(Hits) + "), misses (" + std::to_string(Misses) + ") and clipped projections (" +
                                                              std::to_string(Projections) + ")");
}

void ValidateCamera()
{
    PyroOrbitCamera Camera;
    const PyroCameraBasis Basis = Camera.ComputeBasis();
    Verify(std::fabs(Dot(Basis.Forward, Basis.Forward) - 1.0f) < 1.0e-5f && std::fabs(Dot(Basis.Right, Basis.Right) - 1.0f) < 1.0e-5f && std::fabs(Dot(Basis.Up, Basis.Up) - 1.0f) < 1.0e-5f &&
               std::fabs(Dot(Basis.Forward, Basis.Right)) < 1.0e-5f && std::fabs(Dot(Basis.Forward, Basis.Up)) < 1.0e-5f && std::fabs(Dot(Basis.Right, Basis.Up)) < 1.0e-5f,
           "the basis forward, right and up are unit vectors at right angles");
    const float Away = std::sqrt(Dot({ Basis.Position[0] - Camera.Target[0], Basis.Position[1] - Camera.Target[1], Basis.Position[2] - Camera.Target[2] },
                                     { Basis.Position[0] - Camera.Target[0], Basis.Position[1] - Camera.Target[1], Basis.Position[2] - Camera.Target[2] }));
    Verify(std::fabs(Away - Camera.Distance) < 1.0e-4f, "the eye lies one orbit distance from the target");

    // 📝 What the prototype's camera starts as, and what a drag, the wheel and a pan are worth (camera.js).
    Verify(Camera.Theta == 0.62f && Camera.Phi == 1.26f && Camera.Distance == 3.35f && Camera.Target[1] == 0.22f, "the camera opens where the prototype's does");
    PyroOrbitCamera Dragged;
    Dragged.OrbitByPixels(100.0f, 0.0f);
    Verify(std::fabs((Dragged.TargetTheta - 0.62f) + 0.75f) < 1.0e-5f && Dragged.Theta == 0.62f, "a drag of 100 px aims the camera 0.75 rad round (0.0075 rad a pixel); the view eases after it");
    Dragged.OrbitByPixels(0.0f, -100000.0f);
    Verify(Dragged.TargetPhi <= 3.14159265f - 0.08f + 1.0e-6f, "the orbit stops short of the pole");
    Dragged.OrbitByPixels(0.0f, 100000.0f);
    Verify(Dragged.TargetPhi >= 0.12f - 1.0e-6f, "and of the other pole");
    PyroOrbitCamera Zoomed;
    for (int Notch = 0; Notch < 100; ++Notch) Zoomed.ZoomByWheel(100.0f);
    const float Farthest = Zoomed.TargetDistance;
    for (int Notch = 0; Notch < 200; ++Notch) Zoomed.ZoomByWheel(-100.0f);
    Verify(std::fabs(Farthest - 9.5f) < 1.0e-5f && std::fabs(Zoomed.TargetDistance - 0.85f) < 1.0e-5f, "the wheel zooms between 0.85 and 9.5 domain units");
    PyroOrbitCamera Eased;
    Eased.OrbitByPixels(100.0f, 0.0f);
    Eased.Advance(1.0f / 60.0f);
    Verify(Eased.Theta < 0.62f && Eased.Theta > Eased.TargetTheta, "the view eases toward the aim and does not jump");
    Eased.Advance(1.0f);
    Verify(std::fabs(Eased.Theta - Eased.TargetTheta) < 1.0e-6f, "and arrives when a long refresh has passed");

    // 📝 The camera-view select: Front, Side and Top turn the aim; Perspective is Focus volume, which stands back 2.3 widths (or heights), at most 9.5.
    PyroOrbitCamera Views[4];
    for (int View = 0; View < 4; ++View) Views[View].ApplyPresetView(View, 1.85f, 2.1f);
    Verify(Views[1].TargetTheta == 0.0f && std::fabs(Views[1].TargetPhi - 1.57079633f) < 1.0e-6f && std::fabs(Views[2].TargetTheta - 1.57079633f) < 1.0e-6f &&
               std::fabs(Views[3].TargetPhi - 0.03f) < 1.0e-6f && std::fabs(Views[0].TargetDistance - 2.1f * 2.3f) < 1.0e-5f,
           "the four views: perspective (focus), front, side, top");
    PyroOrbitCamera Wide;
    Wide.FocusVolume(8.0f, 3.0f);
    Verify(Wide.TargetDistance == 9.5f, "Focus volume stands back at most 9.5");
    PyroOrbitCamera Turntable;
    Turntable.AdvanceTurntable(1.0f);
    Verify(std::fabs(Turntable.TargetTheta - (0.62f + 0.05f * 0.35f)) < 1.0e-6f, "the turntable turns 0.35 rad a second, a long refresh counted as 0.05 s");

    ValidateCameraAgainstPrototype();
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
    return Solver.RenderStandaloneViewport(Context, Settings, Camera.ComputeBasis(16.0f / 9.0f), 320, 180, false) && Solver.ReadViewportRgba(Context, Rgba, Width, Height) && Width == 320 && Height == 180;
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
        Solver.TriggerBurstSequence(Context, Burst, "mushroom");
        for (int Step = 0; Step < 30; ++Step) Solver.AdvanceSimulation(Context, Burst, 1.0f / 30.0f);
        const PyroBoundsBox Surged = Solver.ComputeBoundsBox(Burst);
        Verify(!Burst.DynamicBounds || Surged.EffectiveWidth >= Rest.EffectiveWidth, "a burst leaves the domain no smaller (" + std::to_string(Rest.EffectiveWidth) + " m to " + std::to_string(Surged.EffectiveWidth) + " m)");

        // 📝 The burst patterns are the prototype's (FluidPanel.js TriggerBurst): its centres, delays and overrides, read back from the queue.
        Burst.TimeScale = 1.0f;
        const auto Near = [](float Left, float Right) { return std::fabs(Left - Right) < 1.0e-5f; };
        const auto At   = [&](size_t Index) -> PyroVolumeSolver::PendingBlastRecord { return Solver.QueryPendingBlasts().at(Index); };
        Solver.ClearLattice(Context);
        Solver.TriggerBurstSequence(Context, Burst, "salvo");
        Verify(Solver.QueryPendingBlasts().size() == 4u, "salvo queues four bursts");
        Verify(Near(At(1).Center[0], 0.68f) && Near(At(1).Center[1], 0.18f) && Near(At(1).Center[2], 0.33f) && Near(At(3).Center[0], 0.66f) && Near(At(3).Center[2], 0.66f), "salvo's centres are the prototype's four");
        Verify(Near(At(0).Delay, 0.0f) && Near(At(1).Delay, 0.17f) && Near(At(2).Delay, 0.34f) && Near(At(3).Delay, 0.51f), "salvo fires every 0.17 s");
        Verify(Near(At(2).Radius, Burst.BlastRadius * 0.85f) && Near(At(2).Strength, Burst.BlastStrength * 0.95f) && Near(At(2).Temperature, Burst.BlastTemperature * 1.05f) && Near(At(2).Smoke, Burst.BlastSmoke * 1.1f) && At(2).Lobes == 7, "salvo scales the burst settings by 0.85, 0.95, 1.05 and 1.1 and uses seven lobes");
        for (int Step = 0; Step < 7; ++Step) Solver.AdvanceSimulation(Context, Burst, 1.0f / 30.0f);
        Verify(Solver.QueryPendingBlasts().size() == 2u, "after seven steps of 1/30 s the second round has fired (0.17 s had passed before the seventh) and two wait (" + std::to_string(Solver.QueryPendingBlasts().size()) + " do)");

        Solver.ClearLattice(Context);
        Solver.TriggerBurstSequence(Context, Burst, "collide");
        Verify(Solver.QueryPendingBlasts().size() == 2u && Near(At(0).Center[0], 0.22f) && Near(At(1).Center[0], 0.78f) && Near(At(0).Center[1], 0.3f) && Near(At(0).DirectionalVel[0], 14.0f) && Near(At(1).DirectionalVel[0], -14.0f) && Near(At(0).DirectionalVel[1], 1.5f), "collide is two jets at 0.22 and 0.78 driven at each other at 14");
        Verify(Near(At(1).Delay, 0.03f) && Near(At(0).Radius, 0.16f) && Near(At(0).Strength, 7.5f) && Near(At(0).Temperature, 6.2f) && Near(At(0).Fuel, 4.8f) && Near(At(0).Smoke, 2.5f) && At(0).Lobes == 6, "collide's second jet is 0.03 s late; both are radius 0.16, strength 7.5, temperature 6.2, fuel 4.8, smoke 2.5, six lobes");

        Solver.ClearLattice(Context);
        Solver.TriggerBurstSequence(Context, Burst, "single");
        Solver.TriggerBurstSequence(Context, Burst, "mushroom");
        Verify(Solver.QueryPendingBlasts().size() == 2u, "mushroom restarts first: the single burst queued before it is gone");
        Verify(Near(At(0).Center[1], 0.14f) && Near(At(0).Radius, 0.23f) && Near(At(0).Strength, 12.5f) && Near(At(0).Temperature, 6.5f) && Near(At(0).Fuel, 5.5f) && Near(At(0).Smoke, 3.2f) && At(0).Lobes == 8, "mushroom's cap is at height 0.14: radius 0.23, strength 12.5, temperature 6.5, fuel 5.5, smoke 3.2, eight lobes");
        Verify(Near(At(1).Delay, 0.15f) && Near(At(1).Center[1], 0.29f) && Near(At(1).Radius, 0.19f) && Near(At(1).Strength, 9.5f) && Near(At(1).Temperature, 6.8f) && Near(At(1).Fuel, 4.8f) && Near(At(1).Smoke, 2.4f) && At(1).Lobes == 6, "mushroom's stem comes 0.15 s later at height 0.29: radius 0.19, strength 9.5, temperature 6.8, fuel 4.8, smoke 2.4, six lobes");

        Solver.ClearLattice(Context);
        GasSettings Quiet = Burst;
        Quiet.ShrapnelEnabled = false;
        Solver.TriggerBurstSequence(Context, Quiet, "single");
        Solver.TriggerBurstSequence(Context, Quiet, "shrapnel");
        const std::array<float, 3> Click = { 0.3f, 0.4f, 0.7f };
        Solver.TriggerBurstSequence(Context, Quiet, "single", &Click);
        Verify(Solver.QueryPendingBlasts().size() == 3u && Near(At(0).Center[0], 0.5f) && Near(At(0).Center[1], 0.18f) && Near(At(0).Center[2], 0.5f), "single is one burst at the default centre (0.5, 0.18, 0.5)");
        Verify(!At(0).SpawnShrapnel && At(1).SpawnShrapnel, "shrapnel forces the projectiles; single follows the setting");
        Verify(Near(At(2).Center[0], 0.3f) && Near(At(2).Center[1], 0.4f) && Near(At(2).Center[2], 0.7f) && Near(At(2).Strength, Quiet.BlastStrength), "a clicked centre is one burst there with the settings' own strength");
        Solver.ClearLattice(Context);

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

    ValidatePresetsAgainstPrototype();
    ValidateCatalogue();
    ValidateCamera();
    ValidateSolver(RequireVulkan);
    std::printf("[PyroVolumeValidation] %d assertions, %d failed\n", Assertions, Failures);
    return Failures == 0 ? 0 : 1;
}
