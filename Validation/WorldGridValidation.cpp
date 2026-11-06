/*==========================================================================================================================================
                                                       WORLDGRIDVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The driving course's tyre smoke is solved on a grid fixed to the world: the window follows the car in whole cells, and the smoke it holds stays where
//    the tyres laid it, so a trail is left behind and the car drives out of its own smoke. This checks that, rather than how far the car drove:
//
//      ① the window's rule (no device): the focus is always inside the window, the window moves in whole cells, only when the focus leaves its
//        middle half, and then onto the focus (Experimental/VehicleHmiPrototype/Source/PlumeSpace.js RecentreWindow);
//      ② a window that moves does not move the picture (device): the same smoke, seen from a camera fixed in the world, before and after the window
//        has been relocated, is the same smoke;
//      ③ a window that moves does not change the smoke (device): two solvers fed the same sources, one of which has its window relocated half way
//        through, show the same smoke from the same camera;
//      ④ the course (device): a car burns rubber, drives 14 m away, and the smoke it laid is still where it laid it with a whole trail to the car; it
//        was laid where the tyres are (its centre is the centre of the sources the intake puts into the window); the solid atlas the solver steps is
//        the one built for the window it steps in, after every tick; and the count of relocations counts relocations.
//
//        WorldGridValidation [--require-vulkan] [--only window|invariance|course] [--write-pictures <folder>]
//
//    Run it from the repository root: it reads Experimental/VehicleHmiPrototype/Archive. The pictures are the solver's standalone marcher (the one the
//    course's composite is made from, without the scene behind it) from a camera that stands in the world; --write-pictures keeps them. The thresholds are
//    read off this device (SwiftShader, Release) and set with room: nothing here is a claim about a GPU.

#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Rendering/PngImageCodec.h"
#include "Frontier/Engine/Rendering/VulkanContext.h"
#include "Frontier/Engine/Scene/WorldSpace.h"
#include "Frontier/Engine/VolumetricDynamics/FluidPlumeBridge.h"
#include "Frontier/Engine/VolumetricDynamics/GasPresetLibrary.h"
#include "Frontier/Engine/VolumetricDynamics/PyroCamera.h"
#include "Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.h"
#include "Frontier/Engine/VolumetricDynamics/TyreSmokeIntake.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

std::string Spell(double Value, int Decimals = 2)
{
    char Text[64];
    std::snprintf(Text, sizeof(Text), "%.*f", Decimals, Value);
    return Text;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                     PICTURES AND CAMERAS
//----------------------------------------------------------------------------------------------------------------------

struct Picture
{
    int                  Width  = 0;
    int                  Height = 0;
    std::vector<uint8_t> Rgba;
};

struct Crop
{
    int Left   = 0;
    int Top    = 0;
    int Right  = 0;   // [px] - exclusive
    int Bottom = 0;   // [px] - exclusive
};

float Dot(const std::array<float, 3>& A, const std::array<float, 3>& B)
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

// 📝 A camera that stands in the world and looks at a point of it: the editor's orbit camera, placed by hand.
PyroCameraBasis ConstructWorldCamera(const std::array<float, 3>& Target, float Distance, float Theta, float Phi, float Aspect)
{
    PyroOrbitCamera Orbit;
    Orbit.Target     = Target;
    Orbit.Theta      = Theta;
    Orbit.Phi        = Phi;
    Orbit.Distance   = Distance;
    Orbit.FovDegrees = 46.0f;
    Orbit.Aspect     = Aspect;
    return Orbit.ComputeBasis(Aspect);
}

// 📝 Where a point of the world lands in a picture of Width by Height (rows from the top, as ReadViewportRgba returns them).
bool ProjectToPicture(const PyroCameraBasis& Basis, const std::array<float, 3>& Point, int Width, int Height, float& OutX, float& OutY)
{
    const std::array<float, 3> Offset = { Point[0] - Basis.Position[0], Point[1] - Basis.Position[1], Point[2] - Basis.Position[2] };
    const float                Depth  = Dot(Offset, Basis.Forward);
    if (Depth <= Basis.Near) return false;
    const float HalfTangent = std::tan(Basis.FovRadians * 0.5f);
    const float Across      = Dot(Offset, Basis.Right) / (Depth * HalfTangent * Basis.Aspect);
    const float Up          = Dot(Offset, Basis.Up) / (Depth * HalfTangent);
    OutX                    = (Across * 0.5f + 0.5f) * static_cast<float>(Width);
    OutY                    = (0.5f - Up * 0.5f) * static_cast<float>(Height);
    return true;
}

std::string PictureDirectory;   // [-] where --write-pictures puts what the checks looked at; empty: nowhere
int         PictureOrdinal = 0;

bool Capture(VulkanContext& Context, PyroVolumeSolver& Solver, const GasSettings& Settings, const PyroCameraBasis& Basis, int Width, int Height, Picture& Out,
             const char* Name = nullptr)
{
    int ReadWidth = 0, ReadHeight = 0;
    if (!Solver.RenderStandaloneViewport(Context, Settings, Basis, Width, Height, false)) return false;
    if (!Solver.ReadViewportRgba(Context, Out.Rgba, ReadWidth, ReadHeight)) return false;
    Out.Width  = ReadWidth;
    Out.Height = ReadHeight;
    if (!PictureDirectory.empty() && Name != nullptr)
    {
        char Ordinal[16];
        std::snprintf(Ordinal, sizeof(Ordinal), "%02d-", ++PictureOrdinal);
        WriteRgbaPngFile(PictureDirectory + "/" + Ordinal + Name + ".png", Out.Rgba.data(), ReadWidth, ReadHeight);
    }
    return ReadWidth == Width && ReadHeight == Height;
}

// 📝 The mean of |a - b| over the colour channels of the pixels in the crop, in levels of 255.
double MeanDifference(const Picture& First, const Picture& Second, const Crop& Region)
{
    if (First.Width != Second.Width || First.Height != Second.Height) return 255.0;
    double Sum   = 0.0;
    size_t Count = 0;
    for (int Row = Region.Top; Row < Region.Bottom; ++Row)
    {
        for (int Column = Region.Left; Column < Region.Right; ++Column)
        {
            const size_t At = (static_cast<size_t>(Row) * First.Width + Column) * 4u;
            for (int Channel = 0; Channel < 3; ++Channel)
                Sum += std::abs(static_cast<int>(First.Rgba[At + Channel]) - static_cast<int>(Second.Rgba[At + Channel]));
            ++Count;
        }
    }
    return Count == 0 ? 0.0 : Sum / (3.0 * static_cast<double>(Count));
}

// 📝 How many pixels of the crop differ by more than Threshold levels in any colour channel.
size_t CountDifferences(const Picture& First, const Picture& Second, const Crop& Region, int Threshold)
{
    if (First.Width != Second.Width || First.Height != Second.Height) return static_cast<size_t>(First.Width) * First.Height;
    size_t Count = 0;
    for (int Row = Region.Top; Row < Region.Bottom; ++Row)
    {
        for (int Column = Region.Left; Column < Region.Right; ++Column)
        {
            const size_t At = (static_cast<size_t>(Row) * First.Width + Column) * 4u;
            for (int Channel = 0; Channel < 3; ++Channel)
            {
                if (std::abs(static_cast<int>(First.Rgba[At + Channel]) - static_cast<int>(Second.Rgba[At + Channel])) > Threshold)
                {
                    ++Count;
                    break;
                }
            }
        }
    }
    return Count;
}

// 📝 How many pixels within Radius of (CentreX, CentreY) hold smoke the empty picture does not.
size_t CountSmokeWithin(const Picture& Smoky, const Picture& Empty, float CentreX, float CentreY, float Radius, int Threshold)
{
    if (Smoky.Width != Empty.Width || Smoky.Height != Empty.Height) return 0;
    size_t Count = 0;
    for (int Row = 0; Row < Smoky.Height; ++Row)
    {
        for (int Column = 0; Column < Smoky.Width; ++Column)
        {
            const float DeltaX = static_cast<float>(Column) + 0.5f - CentreX;
            const float DeltaY = static_cast<float>(Row) + 0.5f - CentreY;
            if (DeltaX * DeltaX + DeltaY * DeltaY > Radius * Radius) continue;
            const size_t At = (static_cast<size_t>(Row) * Smoky.Width + Column) * 4u;
            for (int Channel = 0; Channel < 3; ++Channel)
            {
                if (std::abs(static_cast<int>(Smoky.Rgba[At + Channel]) - static_cast<int>(Empty.Rgba[At + Channel])) > Threshold)
                {
                    ++Count;
                    break;
                }
            }
        }
    }
    return Count;
}

// 📝 The centre of the pixels that differ from the empty window's by more than Threshold levels; the count of them.
size_t SmokeCentroid(const Picture& Smoky, const Picture& Empty, int Threshold, float& OutX, float& OutY)
{
    OutX = OutY = 0.0f;
    if (Smoky.Width != Empty.Width || Smoky.Height != Empty.Height) return 0u;
    double SumX = 0.0, SumY = 0.0;
    size_t Count = 0;
    for (int Y = 0; Y < Smoky.Height; ++Y)
    {
        for (int X = 0; X < Smoky.Width; ++X)
        {
            const size_t At = (static_cast<size_t>(Y) * static_cast<size_t>(Smoky.Width) + static_cast<size_t>(X)) * 4u;
            int Biggest = 0;
            for (size_t Channel = 0; Channel < 3u; ++Channel)
                Biggest = std::max(Biggest, std::abs(static_cast<int>(Smoky.Rgba[At + Channel]) - static_cast<int>(Empty.Rgba[At + Channel])));
            if (Biggest > Threshold)
            {
                SumX += X;
                SumY += Y;
                ++Count;
            }
        }
    }
    if (Count != 0u)
    {
        OutX = static_cast<float>(SumX / static_cast<double>(Count));
        OutY = static_cast<float>(SumY / static_cast<double>(Count));
    }
    return Count;
}

// 📝 Of Samples points along the segment from (FromX, FromY) to (ToX, ToY), the share that has smoke within Reach px: a trail that is whole has no gaps.
double CorridorCoverage(const Picture& Smoky, const Picture& Empty, float FromX, float FromY, float ToX, float ToY, float Reach, int Samples, int Threshold)
{
    if (Smoky.Width != Empty.Width || Smoky.Height != Empty.Height || Samples <= 0) return 0.0;
    int Covered = 0, Counted = 0;
    for (int Sample = 0; Sample < Samples; ++Sample)
    {
        const float Along   = (static_cast<float>(Sample) + 0.5f) / static_cast<float>(Samples);
        const float CentreX = FromX + (ToX - FromX) * Along;
        const float CentreY = FromY + (ToY - FromY) * Along;
        if (CentreX < 0.0f || CentreX >= static_cast<float>(Smoky.Width) || CentreY < 0.0f || CentreY >= static_cast<float>(Smoky.Height)) continue;
        ++Counted;
        if (CountSmokeWithin(Smoky, Empty, CentreX, CentreY, Reach, Threshold) > 0u) ++Covered;
    }
    return Counted == 0 ? 0.0 : static_cast<double>(Covered) / static_cast<double>(Counted);
}

//----------------------------------------------------------------------------------------------------------------------
//                                                    THE WINDOW AND THE COURSE
//----------------------------------------------------------------------------------------------------------------------

// 📝 The settings of the course's world solver (EverestPlumeSceneRuntime::StepWorldSimulation) on a smaller window, so that the checks run in a minute:
//    a closed box, no emitter of its own, a smoke that fades slowly.
GasSettings ConstructWindowSettings(int Lattice, float Width, float Height)
{
    GasSettings Settings        = ConstructPresetSettings("ue5_pyro_default");
    Settings.LatticeResolution  = Lattice;
    Settings.PressureIterations = 10;
    Settings.RaymarchSteps      = 64;
    Settings.BoundsWidth        = Width;
    Settings.BoundsHeight       = Height;
    Settings.EnclosedBox        = true;
    Settings.DynamicBounds      = false;
    Settings.EmitterEnabled     = false;
    Settings.EmitterRate        = 0.0f;
    Settings.SmokeDissipation   = std::min(Settings.SmokeDissipation, 0.08f);
    Settings.EmbersShown        = false;   // [-] embers are drawn about the window, not the world: a picture of the smoke has none
    return Settings;
}

// 📝 A point source that stays at one place of the world whatever the window does: its place is turned into the window's own 0 to 1 each step.
void AssignWorldSource(PyroVolumeSolver& Solver, const GasSettings& Settings, const std::array<float, 3>& PyroPoint, bool Emitting)
{
    if (!Emitting)
    {
        Solver.AssignExternalSources(nullptr, 0);
        return;
    }
    const PyroBoundsBox Box = Solver.ComputeBoundsBox(Settings);
    PyroExternalSource  Source{};
    Source.PositionUvw      = { (PyroPoint[0] - Box.BoxMin[0]) / (Box.BoxMax[0] - Box.BoxMin[0]), (PyroPoint[1] - Box.BoxMin[1]) / (Box.BoxMax[1] - Box.BoxMin[1]),
                                (PyroPoint[2] - Box.BoxMin[2]) / (Box.BoxMax[2] - Box.BoxMin[2]) };
    Source.NormalizedRadius = 0.9f / Box.EffectiveWidth;
    Source.VelocityPyro     = { 0.0f, 2.2f, 0.0f };
    Source.SmokeRate        = 3.2f;
    Source.Temperature      = 1.1f;
    Source.SootRate         = 0.12f;
    Solver.AssignExternalSources(&Source, 1);
}

//----------------------------------------------------------------------------------------------------------------------
//                                                 ① THE WINDOW'S RULE, NO DEVICE
//----------------------------------------------------------------------------------------------------------------------

// 📝 PlumeSpace.js RecentreWindow: the cell of the focus; the window's middle is its origin plus half its width; the window moves when the focus is more
//    than a quarter of its width from the middle on either axis, and then it is put on the focus (both axes).
void ValidateWindowRule()
{
    const int         Lattice = 32;
    const float       Width   = 12.8f;   // [m] - 0.4 m a cell
    const float       Cell    = Width / static_cast<float>(Lattice);
    const GasSettings Settings = ConstructWindowSettings(Lattice, Width, 6.4f);

    PyroVolumeSolver Solver;
    Solver.SetWorldGridMode(true);
    Solver.UpdateWorldGridFocus(3.7f, -2.2f, Settings);
    const auto First = Solver.QueryWorldGridOrigin();
    const int  FocusX = static_cast<int>(std::floor(3.7f / Cell)), FocusZ = static_cast<int>(std::floor(-2.2f / Cell));
    Verify(First[0] == FocusX - Lattice / 2 && First[1] == FocusZ - Lattice / 2, "the first focus puts the window's middle on the focus's cell: origin (" +
                                                                                      std::to_string(First[0]) + ", " + std::to_string(First[1]) + ")");

    // 📝 A focus that stays inside the middle half moves nothing.
    bool Still = true;
    for (int Step = -7; Step <= 7; ++Step)
    {
        Solver.UpdateWorldGridFocus(3.7f + static_cast<float>(Step) * Cell, -2.2f, Settings);
        Still = Still && Solver.QueryWorldGridOrigin() == First;
    }
    Verify(Still, "a focus within a quarter of the window of its middle (7 cells of 8) leaves the window where it is");

    // 📝 One cell past that, the window is put on the focus (the prototype's rule), not moved by the one cell.
    Solver.UpdateWorldGridFocus(3.7f + 9.5f * Cell, -2.2f, Settings);
    const auto Moved   = Solver.QueryWorldGridOrigin();
    const int  Beyond  = static_cast<int>(std::floor((3.7f + 9.5f * Cell) / Cell));
    Verify(Moved[0] == Beyond - Lattice / 2 && Moved[1] == First[1] + 0 + (FocusZ - Lattice / 2 - First[1]),
           "a focus past the middle half puts the window on the focus: origin x " + std::to_string(First[0]) + " -> " + std::to_string(Moved[0]) + " (focus cell " +
               std::to_string(Beyond) + ")");

    // 📝 Whatever the focus does, it is inside the window, the window moves in whole cells, and a long straight run moves it far less often than once a cell.
    PyroVolumeSolver Runner;
    Runner.SetWorldGridMode(true);
    Runner.UpdateWorldGridFocus(0.0f, 0.0f, Settings);
    bool                   Inside     = true;
    int                    Relocated  = 0;
    std::array<int32_t, 2> Previous   = Runner.QueryWorldGridOrigin();
    const float            Run        = 80.0f;   // [m]
    const int              Steps      = static_cast<int>(Run / 0.1f);
    for (int Step = 1; Step <= Steps; ++Step)
    {
        const float Focus = static_cast<float>(Step) * 0.1f;
        Runner.UpdateWorldGridFocus(Focus, 0.25f * Focus, Settings);
        const PyroBoundsBox    Box     = Runner.ComputeBoundsBox(Settings);
        const std::array<int32_t, 2> Now = Runner.QueryWorldGridOrigin();
        Inside = Inside && Focus >= Box.BoxMin[0] && Focus <= Box.BoxMax[0] && 0.25f * Focus >= Box.BoxMin[2] && 0.25f * Focus <= Box.BoxMax[2];
        if (Now != Previous) ++Relocated;
        Previous = Now;
    }
    Verify(Inside, "over an 80 m run the focus is inside the window's box at every step");
    // 📝 The window is 32 cells and moves when the focus is 8 cells off its middle, then is put on the focus: one relocation in about 8 cells of travel.
    const int Cells = static_cast<int>(Run / Cell);
    Verify(Relocated >= Cells / 12 && Relocated <= Cells / 6,
           "over " + std::to_string(Cells) + " cells of travel the window relocates " + std::to_string(Relocated) + " times (about once in 8 to 12 cells)");
    Verify(Relocated > 0, "the run relocates the window at all");
}

//----------------------------------------------------------------------------------------------------------------------
//                                          ② A WINDOW THAT MOVES DOES NOT MOVE THE PICTURE
//                                          ③ A WINDOW THAT MOVES DOES NOT CHANGE THE SMOKE
//----------------------------------------------------------------------------------------------------------------------

void ValidateRelocationInvariance(VulkanContext& Context)
{
    const int   Lattice = 32;
    const float Width   = 12.8f;
    const float Height  = 6.4f;
    GasSettings Settings = ConstructWindowSettings(Lattice, Width, Height);

    // 📝 The smoke comes from one place of the world: a metre above the floor, at the middle of the first window.
    const std::array<float, 3> Place   = { 0.0f, 0.2f, 0.0f };
    const PyroCameraBasis      Camera  = ConstructWorldCamera({ 0.0f, 1.2f, 0.0f }, 8.5f, 0.9f, 1.25f, 16.0f / 9.0f);
    const int                  PictureW = 320, PictureH = 180;
    const Crop                 Centre   = { PictureW / 4, PictureH / 6, PictureW * 3 / 4, PictureH * 5 / 6 };

    // ② One solver: smoke, a picture, the window relocated, a picture. No step lies between the two pictures.
    {
        PyroVolumeSolver Solver;
        std::string      Diagnostic;
        Verify(Solver.Initialize(Context, Settings, Diagnostic), "a world-grid solver starts (" + Diagnostic + ")");
        Solver.SetWorldGridMode(true);
        Solver.UpdateWorldGridFocus(0.0f, 0.0f, Settings);

        Picture Empty, Empty2;
        Verify(Capture(Context, Solver, Settings, Camera, PictureW, PictureH, Empty, "invariance-empty"), "an empty window is drawn");
        for (int Step = 0; Step < 45; ++Step)
        {
            AssignWorldSource(Solver, Settings, Place, Step < 30);
            Solver.AdvanceSimulation(Context, Settings, 1.0f / 30.0f);
        }
        Picture Before;
        Verify(Capture(Context, Solver, Settings, Camera, PictureW, PictureH, Before, "invariance-before"), "the smoke is drawn from a camera fixed in the world");
        const size_t Smoke = CountDifferences(Before, Empty, Centre, 12);
        Verify(Smoke > 400u, "the picture holds the smoke the source laid (" + std::to_string(Smoke) + " px differ from the empty window's)");
        float      PlaceX = 0.0f, PlaceY = 0.0f;
        const bool Projects = ProjectToPicture(Camera, { Place[0], Place[1], Place[2] }, PictureW, PictureH, PlaceX, PlaceY);
        Verify(Projects && PlaceX > 0 && PlaceY > 0, "the source's place projects into the picture, at (" + Spell(PlaceX, 0) + ", " + Spell(PlaceY, 0) + ")");

        const auto OriginBefore = Solver.QueryWorldGridOrigin();
        Solver.UpdateWorldGridFocus(5.0f, 0.0f, Settings);
        const auto OriginAfter = Solver.QueryWorldGridOrigin();
        Verify(OriginAfter != OriginBefore, "a focus 5 m off the window's middle relocates it: origin x " + std::to_string(OriginBefore[0]) + " -> " + std::to_string(OriginAfter[0]));

        Picture After;
        Verify(Capture(Context, Solver, Settings, Camera, PictureW, PictureH, After, "invariance-after"), "the relocated window is drawn from the same camera");
        const double Mean  = MeanDifference(Before, After, Centre);
        const size_t Moved = CountDifferences(Before, After, Centre, 12);
        std::printf("INFO\trelocation without a step: mean difference %.3f levels, %zu px of %d differ by more than 12\n", Mean, Moved,
                    (Centre.Right - Centre.Left) * (Centre.Bottom - Centre.Top));
        Verify(Mean < 0.5 && Moved < Smoke / 100u + 10u, "relocating the window does not move the smoke in the picture (mean " + Spell(Mean, 3) + " levels, " + std::to_string(Moved) +
                                                         " px beyond 12 levels, against " + std::to_string(Smoke) + " px of smoke)");

        // 📝 An empty window does not look different for being somewhere else.
        PyroVolumeSolver Blank;
        Verify(Blank.Initialize(Context, Settings, Diagnostic), "an empty world-grid solver starts");
        Blank.SetWorldGridMode(true);
        Blank.UpdateWorldGridFocus(0.0f, 0.0f, Settings);
        Picture BlankA, BlankB;
        Verify(Capture(Context, Blank, Settings, Camera, PictureW, PictureH, BlankA), "an empty window is drawn at its first place");
        Blank.UpdateWorldGridFocus(5.0f, 0.0f, Settings);
        Verify(Capture(Context, Blank, Settings, Camera, PictureW, PictureH, BlankB), "an empty window is drawn at its second place");
        Verify(CountDifferences(BlankA, BlankB, Centre, 6) == 0u, "an empty window draws the same picture wherever the window sits");
        Blank.Shutdown(Context);
        (void)Empty2;
        Solver.Shutdown(Context);
    }

    // ③ Two solvers fed the same sources: the first keeps its window, the second has it relocated after the sources have stopped. They are stepped on
    //    in step with each other from there, and drawn from the same camera.
    {
        PyroVolumeSolver Kept, Relocated;
        std::string      Diagnostic;
        GasSettings      KeptSettings = Settings, RelocatedSettings = Settings;
        Verify(Kept.Initialize(Context, KeptSettings, Diagnostic) && Relocated.Initialize(Context, RelocatedSettings, Diagnostic), "two world-grid solvers start");
        Kept.SetWorldGridMode(true);
        Relocated.SetWorldGridMode(true);
        Kept.UpdateWorldGridFocus(0.0f, 0.0f, KeptSettings);
        Relocated.UpdateWorldGridFocus(0.0f, 0.0f, RelocatedSettings);
        Picture Empty;
        Verify(Capture(Context, Kept, KeptSettings, Camera, PictureW, PictureH, Empty, "kept-empty"), "an empty window is drawn (the reference)");

        for (int Step = 0; Step < 30; ++Step)
        {
            AssignWorldSource(Kept, KeptSettings, Place, true);
            AssignWorldSource(Relocated, RelocatedSettings, Place, true);
            Kept.AdvanceSimulation(Context, KeptSettings, 1.0f / 30.0f);
            Relocated.AdvanceSimulation(Context, RelocatedSettings, 1.0f / 30.0f);
        }
        const auto Origin = Relocated.QueryWorldGridOrigin();
        Relocated.UpdateWorldGridFocus(5.0f, 0.0f, RelocatedSettings);
        Verify(Relocated.QueryWorldGridOrigin() != Origin && Kept.QueryWorldGridOrigin() == Origin, "one window is relocated, the other stays");
        for (int Step = 0; Step < 14; ++Step)
        {
            AssignWorldSource(Kept, KeptSettings, Place, false);
            AssignWorldSource(Relocated, RelocatedSettings, Place, false);
            Kept.AdvanceSimulation(Context, KeptSettings, 1.0f / 30.0f);
            Relocated.AdvanceSimulation(Context, RelocatedSettings, 1.0f / 30.0f);
        }
        Picture KeptPicture, RelocatedPicture;
        Verify(Capture(Context, Kept, KeptSettings, Camera, PictureW, PictureH, KeptPicture, "kept") && Capture(Context, Relocated, RelocatedSettings, Camera, PictureW, PictureH, RelocatedPicture, "relocated"),
               "both are drawn from the same camera");
        const size_t Smoke = CountDifferences(KeptPicture, Empty, Centre, 12);
        const double Mean  = MeanDifference(KeptPicture, RelocatedPicture, Centre);
        const size_t Apart = CountDifferences(KeptPicture, RelocatedPicture, Centre, 12);
        std::printf("INFO\tsmoke of the kept window: %zu px; relocated against kept after 14 more steps: mean %.3f levels, %zu px beyond 12\n", Smoke, Mean, Apart);
        Verify(Smoke > 400u, "the kept window holds smoke (" + std::to_string(Smoke) + " px)");
        Verify(Mean < 0.6 && Apart < Smoke / 100u + 10u,
               "a window relocated half way through shows the smoke a window that never moved shows (mean " + Spell(Mean, 3) + " levels, " + std::to_string(Apart) +
                   " px beyond 12 levels, against " + std::to_string(Smoke) + " px of smoke)");
        Relocated.Shutdown(Context);
        Kept.Shutdown(Context);
    }
}

//----------------------------------------------------------------------------------------------------------------------
//                                                        ④ THE COURSE
//----------------------------------------------------------------------------------------------------------------------

struct CourseTick
{
    uint64_t               Ordinal    = 0u;   // [-] - tick of the world simulation, from 0
    std::array<int32_t, 2> Origin{};          // [cell] - the solver's window after the tick
    std::array<int32_t, 3> AtlasWindow{};     // [-] - the window the solid atlas was built for
    int32_t                Lattice    = 0;    // [cell]
    float                  CarX       = 0.0f; // [m]
    float                  CarY       = 0.0f; // [m]
};

struct CourseRun
{
    std::vector<CourseTick> Ticks;
    int                     StandingTicks = 0;
    std::array<float, 3>    Burnout{};        // [m] - Pyro's axes: x across, y up, z along the world's y
    std::array<float, 3>    Arrival{};        // [m]
    float                   DrivenMetres  = 0.0f;
    uint64_t                Relocations   = 0u;
    uint64_t                RelocationsAtBurnoutEnd = 0u;
    Picture                 Empty, Laid, Left;
    PyroCameraBasis         Camera{};
    float                   SourceX = 0.0f, SourceY = 0.0f;   // [px] - where the tyres' sources are in the picture, as the intake projects them into the window
    int                     SourceCount = 0;
    int                     OddRelocations  = 0;   // [-] - window moves that fell on an odd tick
    int                     EvenRelocations = 0;
};

// 📝 A car burns rubber where it stands, then drives off in a straight line. The pictures are of the world solver from a camera fixed above the burnout.
//    StandingTicks stands the car still for a number of ticks first, so that a relocation falls on an odd tick in one drive and on an even one in another.
bool DriveCourse(VulkanContext& Context, EverestPlumeSceneRuntime& Runtime, WorldSpace& World, int StandingTicks, int PictureW, int PictureH, CourseRun& Run)
{
    Run.StandingTicks = StandingTicks;
    Runtime.ResetWorldSimulation(Context, World);
    Runtime.AccessTyreIntake().BurnoutLineLock = true;

    // 📝 The tyres are brought up to heat without the solver (as VolumetricAndEditorValidation does): the same inputs, the thermal model only.
    for (int Step = 0; Step < 810; ++Step)
    {
        InputPacket Packet{};
        Packet.ThrottleAxis  = 1.0f;
        Packet.BrakeAxis     = 0.85f;
        Packet.HandbrakeAxis = 1.0f;
        Packet.SteerAxis     = 0.16f;
        Runtime.AccessVehicleUnit().ApplyInputPacket(Packet, 1.0f / 60.0f);
        const auto At = Runtime.AccessVehicleUnit().QueryPositionMetres();
        Runtime.AccessTyreIntake().Advance(Runtime.AccessVehicleUnit(), 1.0f / 60.0f, WorldSdfScene::ResolveCourseHeight(At[0], At[1], true));
    }
    const auto Start = Runtime.AccessVehicleUnit().QueryPositionMetres();
    Run.Burnout      = { Start[0], 0.0f, Start[1] };

    // 📝 The camera: above the course, a little off the vertical, 17 m away and aimed 7.2 m ahead of the burnout, so the picture holds the burnout and the 14 m
    //    the car then drives (the car drives along +x from its spawn).
    Run.Camera = ConstructWorldCamera({ Start[0] + 7.2f, 0.3f, Start[1] }, 17.0f, 0.0f, 0.42f, static_cast<float>(PictureW) / static_cast<float>(PictureH));
    GasSettings Shown = Runtime.AccessWorldSettings();
    Shown.EmbersShown = false;
    Verify(Capture(Context, Runtime.AccessWorldSolver(), Shown, Run.Camera, PictureW, PictureH, Run.Empty, "course-empty"), "the course's empty window is drawn from the fixed camera");

    uint64_t Ordinal = 0u;
    const auto Step  = [&](float Throttle, float Brake, bool Handbrake) {
        const auto Before = Runtime.AccessWorldSolver().QueryWorldGridOrigin();
        Runtime.StepWorldSimulation(Context, World, 1.0 / 24.0, Throttle, Brake, 0.16f, Handbrake, false);
        CourseTick Tick;
        Tick.Ordinal     = Ordinal++;
        Tick.Origin      = Runtime.AccessWorldSolver().QueryWorldGridOrigin();
        Tick.AtlasWindow = Runtime.QuerySolidAtlasWindow();
        Tick.Lattice     = Runtime.AccessWorldSolver().QueryLatticeResolution();
        const auto At    = Runtime.AccessVehicleUnit().QueryPositionMetres();
        Tick.CarX        = At[0];
        Tick.CarY        = At[1];
        if (Tick.Origin != Before && Tick.Ordinal > 0u)
            ((Tick.Ordinal & 1u) != 0u ? Run.OddRelocations : Run.EvenRelocations)++;
        Run.Ticks.push_back(Tick);
    };

    for (int Tick = 0; Tick < StandingTicks; ++Tick) Step(0.0f, 0.0f, false);
    // 📝 The burnout: throttle and handbrake, the car held where it stands, the solver stepping.
    for (int Tick = 0; Tick < 30; ++Tick) Step(1.0f, 0.85f, true);
    Run.RelocationsAtBurnoutEnd = Runtime.QueryWorldGridRelocations();
    const auto Laid = Runtime.AccessVehicleUnit().QueryPositionMetres();
    Run.Burnout     = { Laid[0], 0.0f, Laid[1] };
    {
        // 📝 Where the tyres are: the sources the intake projects into the window now, seen from the camera (the call is the one the runtime makes each tick).
        PyroExternalSource            Sources[8]{};
        std::vector<DynamicFireLight> Lights;
        const PyroBoundsBox           Box   = Runtime.AccessWorldSolver().ComputeBoundsBox(Shown);
        const int                     Count = Runtime.AccessTyreIntake().ProjectIntoDomain({ 0.0f, 0.0f, 0.0f }, Box, Sources, 7, Lights);
        float                         SumX = 0.0f, SumY = 0.0f;
        int                           Projected = 0;
        for (int Index = 0; Index < Count; ++Index)
        {
            const std::array<float, 3> At = { Box.BoxMin[0] + Sources[Index].PositionUvw[0] * (Box.BoxMax[0] - Box.BoxMin[0]),
                                              Box.BoxMin[1] + Sources[Index].PositionUvw[1] * (Box.BoxMax[1] - Box.BoxMin[1]),
                                              Box.BoxMin[2] + Sources[Index].PositionUvw[2] * (Box.BoxMax[2] - Box.BoxMin[2]) };
            float                      X = 0.0f, Y = 0.0f;
            if (ProjectToPicture(Run.Camera, At, PictureW, PictureH, X, Y))
            {
                SumX += X;
                SumY += Y;
                ++Projected;
            }
        }
        Run.SourceCount = Projected;
        if (Projected > 0)
        {
            Run.SourceX = SumX / static_cast<float>(Projected);
            Run.SourceY = SumY / static_cast<float>(Projected);
        }
    }
    Verify(Capture(Context, Runtime.AccessWorldSolver(), Shown, Run.Camera, PictureW, PictureH, Run.Laid, "course-laid"), "the smoke the burnout laid is drawn");

    // 📝 Then the car drives off: full throttle, no brake, until it is 14 m from where it burned rubber (or 150 ticks).
    int Driven = 0;
    for (; Driven < 150; ++Driven)
    {
        Step(1.0f, 0.0f, false);
        const auto At = Runtime.AccessVehicleUnit().QueryPositionMetres();
        if (std::hypot(At[0] - Run.Burnout[0], At[1] - Run.Burnout[2]) >= 14.0f) break;
    }
    const auto Gone  = Runtime.AccessVehicleUnit().QueryPositionMetres();
    Run.Arrival      = { Gone[0], 0.0f, Gone[1] };
    Run.DrivenMetres = std::hypot(Gone[0] - Run.Burnout[0], Gone[1] - Run.Burnout[2]);
    Run.Relocations  = Runtime.QueryWorldGridRelocations();
    Verify(Capture(Context, Runtime.AccessWorldSolver(), Shown, Run.Camera, PictureW, PictureH, Run.Left, "course-left"), "the course is drawn after the car has gone");
    std::printf("INFO\tdrive with %d standing ticks: %d ticks, %.2f m, %llu relocations (burnout end: %llu), odd/even relocation ticks %d/%d\n", StandingTicks, Driven, Run.DrivenMetres,
                static_cast<unsigned long long>(Run.Relocations), static_cast<unsigned long long>(Run.RelocationsAtBurnoutEnd), Run.OddRelocations, Run.EvenRelocations);
    return true;
}

void ValidateCourse(VulkanContext& Context, const std::filesystem::path& ArchiveRoot)
{
    EverestPlumeSceneRuntime Runtime;
    std::string              Diagnostic;
    if (!Runtime.Initialize(Context, ArchiveRoot, Diagnostic))
    {
        Verify(false, "the course's runtime starts (" + Diagnostic + ")");
        return;
    }
    WorldSpace World;
    const int  PictureW = 320, PictureH = 180;

    CourseRun First;
    DriveCourse(Context, Runtime, World, 0, PictureW, PictureH, First);
    CourseRun Second;
    bool      SecondDrive = false;
    if (First.OddRelocations == 0)
    {
        DriveCourse(Context, Runtime, World, 1, PictureW, PictureH, Second);
        SecondDrive = true;
    }

    const auto Place = First.Burnout;
    float      PlaceX = 0.0f, PlaceY = 0.0f, GoneX = 0.0f, GoneY = 0.0f;
    const bool Visible    = ProjectToPicture(First.Camera, { Place[0], 0.3f, Place[2] }, PictureW, PictureH, PlaceX, PlaceY);
    const bool GoneInView = ProjectToPicture(First.Camera, { First.Arrival[0], 0.3f, First.Arrival[2] }, PictureW, PictureH, GoneX, GoneY);
    Verify(Visible && PlaceX > 0 && PlaceX < PictureW && PlaceY > 0 && PlaceY < PictureH && GoneInView && GoneX > 0 && GoneX < PictureW,
           "the burnout (" + Spell(PlaceX, 0) + ", " + Spell(PlaceY, 0) + ") and where the car ends (" + Spell(GoneX, 0) + ", " + Spell(GoneY, 0) + ") are in the picture of " +
               std::to_string(PictureW) + " x " + std::to_string(PictureH));

    const CourseRun* Runs[2] = { &First, SecondDrive ? &Second : nullptr };
    for (const CourseRun* Run : Runs)
    {
        if (!Run) continue;
        const std::string Name = "drive " + std::to_string(Run->StandingTicks) + " standing ticks: ";
        // 📝 The count of relocations counts relocations: the window's origin changed that many times, and no more.
        int Seen = 0;
        for (size_t At = 1; At < Run->Ticks.size(); ++At)
            if (Run->Ticks[At].Origin != Run->Ticks[At - 1].Origin) ++Seen;
        Verify(Run->Relocations == static_cast<uint64_t>(Seen), Name + "the runtime counted " + std::to_string(Run->Relocations) + " relocations, the window's origin changed " + std::to_string(Seen) + " times");
        Verify(Run->RelocationsAtBurnoutEnd == 0u, Name + "a car that burns rubber where it stands moves the window never (" + std::to_string(Run->RelocationsAtBurnoutEnd) + " counted)");
        Verify(Run->DrivenMetres >= 12.0f, Name + "the car drove " + Spell(Run->DrivenMetres, 1) + " m from the burnout");
        Verify(Run->Relocations >= 1u, Name + "driving that far relocates the window at least once (" + std::to_string(Run->Relocations) + ")");

        // 📝 The solid atlas on the device is the one for the window the solver stepped in, after every tick.
        size_t Stale = 0;
        for (const CourseTick& Tick : Run->Ticks)
            if (Tick.AtlasWindow != std::array<int32_t, 3>{ Tick.Origin[0], Tick.Origin[1], Tick.Lattice }) ++Stale;
        Verify(Stale == 0u, Name + "the solid atlas was built for the solver's own window after each of " + std::to_string(Run->Ticks.size()) + " ticks (" + std::to_string(Stale) + " were not)");

        // 📝 The smoke the burnout laid is where it was laid, and the car's trail is whole from there to the car.
        const size_t LaidAt = CountSmokeWithin(Run->Laid, Run->Empty, PlaceX, PlaceY, 45.0f, 12);
        const size_t LeftAt = CountSmokeWithin(Run->Left, Run->Empty, PlaceX, PlaceY, 45.0f, 12);
        const double Trail  = CorridorCoverage(Run->Left, Run->Empty, PlaceX, PlaceY, GoneX, GoneY, 14.0f, 40, 8);
        std::printf("INFO\t%ssmoke and fire within 45 px of the burnout: %zu px laid, %zu px left; the trail is covered along %.0f%% of its length\n", Name.c_str(), LaidAt, LeftAt, Trail * 100.0);
        Verify(LaidAt > 300u, Name + "the burnout laid smoke and fire at its place (" + std::to_string(LaidAt) + " px)");
        float        SmokeX = 0.0f, SmokeY = 0.0f;
        const size_t Laid     = SmokeCentroid(Run->Laid, Run->Empty, 12, SmokeX, SmokeY);
        const double Distance = std::hypot(static_cast<double>(SmokeX - Run->SourceX), static_cast<double>(SmokeY - Run->SourceY));
        std::printf("INFO\t%sthe laid smoke is centred at (%.0f, %.0f) of %zu px, the tyres' %d sources at (%.0f, %.0f): %.0f px apart\n", Name.c_str(), SmokeX, SmokeY, Laid, Run->SourceCount,
                    Run->SourceX, Run->SourceY, Distance);
        Verify(Run->SourceCount > 0 && Laid > 300u && Distance < 20.0, Name + "the smoke is laid where the tyres are (" + Spell(Distance, 0) + " px from the tyres' sources)");
        Verify(LeftAt * 100u >= LaidAt * 50u, Name + "it is still at the burnout's place after the car has gone 14 m (" + std::to_string(LeftAt) + " of " + std::to_string(LaidAt) + " px)");
        Verify(Trail >= 0.75, Name + "the trail from the burnout to the car is whole (" + Spell(Trail * 100.0, 0) + "% of 40 points have smoke within 14 px)");
    }
    Runtime.Shutdown(Context);
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    bool        RequireVulkan = false;
    std::string Only;   // [-] window, invariance or course: the one part to run (empty: all)
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        if (std::strcmp(Arguments[Index], "--require-vulkan") == 0) RequireVulkan = true;
        else if (std::strcmp(Arguments[Index], "--write-pictures") == 0 && Index + 1 < ArgumentCount)
        {
            PictureDirectory = Arguments[++Index];
            std::filesystem::create_directories(PictureDirectory);
        }
        else if (std::strcmp(Arguments[Index], "--only") == 0 && Index + 1 < ArgumentCount) Only = Arguments[++Index];
    }

    if (Only.empty() || Only == "window") ValidateWindowRule();

    VulkanContext Context;
    std::string   Diagnostic;
    if (!Context.InitializeHeadless(Diagnostic))
    {
        if (RequireVulkan) Verify(false, "a Vulkan 1.1 device comes up (" + Diagnostic + ")");
        else std::printf("INFO\tno Vulkan device (%s): the device half is skipped\n", Diagnostic.c_str());
        std::printf("[WorldGridValidation] %d assertions, %d failed\n", Assertions, Failures);
        return Failures == 0 ? 0 : 1;
    }
    std::printf("INFO\tVulkan 1.1 on %s\n", Context.QueryDeviceSummary().c_str());

    if (Only.empty() || Only == "invariance") ValidateRelocationInvariance(Context);
    const std::filesystem::path Archive = std::filesystem::path("Experimental/VehicleHmiPrototype/Archive");
    if (Only.empty() || Only == "course")
    {
        if (std::filesystem::exists(Archive / "ControlVehicleBody.facet"))
            ValidateCourse(Context, Archive);
        else
            Verify(false, "Experimental/VehicleHmiPrototype/Archive is reachable: run from the repository root");
    }

    std::printf("[WorldGridValidation] %d assertions, %d failed\n", Assertions, Failures);
    Context.Shutdown();
    return Failures == 0 ? 0 : 1;
}
