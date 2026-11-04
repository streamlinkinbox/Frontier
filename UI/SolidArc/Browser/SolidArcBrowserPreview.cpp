/*====================================================================================================================================
                                                     SOLIDARCBROWSERPREVIEW.CPP
====================================================================================================================================*/
// 🧩 Project browser preview of a .solidarc file: decode, regenerate, tessellate and place it in the browser frame

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Browser/SolidArcBrowserPreview.h"

#include "Engine/Render/BrowserPreviewGeometry.h"
#include "UI/SolidArc/Document/SolidArcDocument.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace Frontier::SolidArc
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr double                   FootprintWidth  = 5.6;                                    // [len] - Widest horizontal extent in the browser frame
constexpr double                   FootprintHeight = 3.2;                                    // [len] - Tallest vertical extent in the browser frame
constexpr double                   FloorClearance  = 0.02;                                   // [len] - Gap between the floor top and the lowest point
constexpr std::array<float, 5>     BodySurfaces    = { 0.0f, 2.0f, 4.0f, 1.0f, 3.0f };      // [-]   - Reference surface per body (cycled)

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<float> ConvertSolidArcSceneToBrowserFrame(const SolidArcScene& Scene)
{
    std::vector<float> Vertices;

    // ① reference floor + grid first (surfaces 6 / 5: the contour generator skips them, as for the fixture); an empty model
    //    (a new document from the browser's New dialog) is just this stage
    BrowserPreviewDetail::PushBox(Vertices, { 0, -.06, 0 }, { 16, .08, 16 }, 6);
    for (int Line = -8; Line <= 8; ++Line)
    {
        BrowserPreviewDetail::PushBox(Vertices, { double(Line), 0, 0 }, { .012, .004, 16 }, 5);
        BrowserPreviewDetail::PushBox(Vertices, { 0, 0, double(Line) }, { 16, .004, .012 }, 5);
    }
    if (Scene.Corners.empty()) return Vertices;

    // ② bounds in the browser axes (x, z, −y)
    std::array<double, 3> Low  = { 1e300, 1e300, 1e300 };
    std::array<double, 3> High = { -1e300, -1e300, -1e300 };
    for (const SolidArcSceneCorner& Corner : Scene.Corners)
    {
        const std::array<double, 3> Turned = { Corner.Position[0], Corner.Position[2], -Corner.Position[1] };
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            Low[Axis]  = std::min(Low[Axis], Turned[Axis]);
            High[Axis] = std::max(High[Axis], Turned[Axis]);
        }
    }
    const double Wide  = std::max({ High[0] - Low[0], High[2] - Low[2], 1e-9 });
    const double Tall  = std::max(High[1] - Low[1], 1e-9);
    const double Scale = std::min(FootprintWidth / Wide, FootprintHeight / Tall);                                  // [-] - Uniform
    const std::array<double, 3> Shift = { -(Low[0] + High[0]) * 0.5, -Low[1], -(Low[2] + High[2]) * 0.5 };

    // ③ document triangles: position, normal, surface, separation (none)
    Vertices.reserve(Vertices.size() + Scene.Corners.size() * 10);
    for (const SolidArcSceneCorner& Corner : Scene.Corners)
    {
        const uint32_t Body    = Corner.Face < Scene.Faces.size() ? Scene.Faces[Corner.Face].Body : 0;
        const float    Surface = BodySurfaces[Body % BodySurfaces.size()];
        const std::array<double, 3> Turned = { Corner.Position[0], Corner.Position[2], -Corner.Position[1] };
        Vertices.push_back(float((Turned[0] + Shift[0]) * Scale));
        Vertices.push_back(float((Turned[1] + Shift[1]) * Scale + FloorClearance));
        Vertices.push_back(float((Turned[2] + Shift[2]) * Scale));
        Vertices.push_back(Corner.Normal[0]);
        Vertices.push_back(Corner.Normal[2]);
        Vertices.push_back(-Corner.Normal[1]);
        Vertices.push_back(Surface);
        Vertices.push_back(0.0f);
        Vertices.push_back(0.0f);
        Vertices.push_back(0.0f);
    }
    return Vertices;
}

SolidArcBrowserPreview ComposeSolidArcBrowserPreview(const std::filesystem::path& Path)
{
    SolidArcBrowserPreview Preview;
    const auto             Started = std::chrono::steady_clock::now();
    std::error_code        Failure;
    const auto             Bytes   = std::filesystem::file_size(Path, Failure);
    const auto             Written = Failure ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(Path, Failure);
    if (Failure)
    {
        Preview.Refusal = "The file could not be read (" + Failure.message() + ").";
        return Preview;
    }
    const std::u8string Encoded  = Path.u8string();
    const std::string   Readable(Encoded.begin(), Encoded.end());
    Preview.Key  = Readable;
    // 📝 Microsecond ticks as long long: libc++ on Windows counts file_time_type in a 128-bit representation (no to_string
    //    overload) from 1601, so nanoseconds would not fit 64 bits either.
    const long long WrittenTicks = std::chrono::duration_cast<std::chrono::duration<long long, std::micro>>(Written.time_since_epoch()).count();
    Preview.Key += ":" + std::to_string(Bytes) + ":" + std::to_string(WrittenTicks);

    const SolidArcDocumentFile File = LoadSolidArcDocumentFile(Readable);
    if (!File.Refusal.empty())
    {
        Preview.Refusal = File.Refusal;
        return Preview;
    }
    SolidArcDocument Document = CreateSolidArcDocument(File.Title);
    Document.Tree             = File.Tree;
    Document.Captions         = File.Captions;
    RegenerateSolidArcDocument(Document, false);
    const SolidArcScene Scene = BuildSolidArcScene(Document);
    if (!Scene.Refusal.empty())
    {
        Preview.Refusal = Scene.Refusal;
        return Preview;
    }
    // 📝 No bodies is not a refusal: a model created by the browser's New dialog starts empty and previews as the bare stage.
    Preview.Title         = File.Title;
    Preview.StepCount     = File.Tree.Steps.size();
    Preview.BodyCount     = Scene.Bodies.size();
    Preview.FaceCount     = Scene.Faces.size();
    Preview.TriangleCount = Scene.Corners.size() / 3;
    Preview.Triangles     = std::make_shared<const std::vector<float>>(ConvertSolidArcSceneToBrowserFrame(Scene));
    Preview.Milliseconds  = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
    return Preview;
}

} // namespace Frontier::SolidArc
