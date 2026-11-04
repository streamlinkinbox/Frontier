/*====================================================================================================================================
                                                       SOLIDARCDRAWINGSHEET.CPP
====================================================================================================================================*/
// 🧩 Drawing sheet (Alt+D): shown solid bodies → kernel ComposeDrawingSheet (metres → millimetres) → SVG / DXF R12

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Document/SolidArcDrawingSheet.h"

#include "Engine/GeometryProcessing/Drawing/DrawingExport.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] bool ShownSolidCondition(const SolidArcDocument& Document, const GeometryProcessing::RegeneratedBody& Body, int& Sheets)
{
    if (Document.Appearance.HiddenBodies.contains(Body.Key) || !Body.Body.PresentCondition()) return false;
    for (const SolidArcBodyFigures& Figures : Document.Summary.Bodies)
    {
        if (Figures.Key == Body.Key && Figures.SheetCondition) { ++Sheets; return false; }
    }
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const char* SolidArcProjectionAngleLabel(GeometryProcessing::DrawingProjectionAngle Angle) noexcept
{
    return Angle == GeometryProcessing::DrawingProjectionAngle::First ? "First" : "Third";
}

bool ParseSolidArcPaperSize(const std::string& Label, GeometryProcessing::DrawingPaperSize& Paper) noexcept
{
    for (int Index = 0; Index < 5; ++Index)
    {
        if (Label == GeometryProcessing::DrawingPaperLabel(static_cast<GeometryProcessing::DrawingPaperSize>(Index))) { Paper = static_cast<GeometryProcessing::DrawingPaperSize>(Index); return true; }
    }
    return false;
}

bool ParseSolidArcProjectionAngle(const std::string& Label, GeometryProcessing::DrawingProjectionAngle& Angle) noexcept
{
    if (Label == "First") { Angle = GeometryProcessing::DrawingProjectionAngle::First; return true; }
    if (Label == "Third") { Angle = GeometryProcessing::DrawingProjectionAngle::Third; return true; }
    return false;
}

std::string SolidArcDrawingSheetKey(const SolidArcDrawingSheetSettings& Settings)
{
    const SolidArcTitleFields& Title = Settings.Title;
    char Text[160];
    std::snprintf(Text, sizeof(Text), "%s|%s|%.17g|%d%d%d%d%d|", GeometryProcessing::DrawingPaperLabel(Settings.Paper), SolidArcProjectionAngleLabel(Settings.Angle), Settings.Scale,
                  Settings.IsometricEnabled, Settings.SectionEnabled, Settings.HiddenLinesEnabled, Settings.TangentEdgesEnabled, Settings.DimensionsEnabled);
    std::string Key = Text;
    if (Settings.SectionEnabled) Key += SolidArcSectionKey(Settings.Section);
    for (const std::string* Field : { &Title.Title, &Title.DrawingNumber, &Title.Author, &Title.Date, &Title.Material, &Title.Revision, &Title.Company }) Key += "|" + *Field;
    return Key;
}

std::string SolidArcDrawingSheetCacheKey(const SolidArcDocument& Document, const SolidArcDrawingSheetSettings& Settings)
{
    std::string Key = SolidArcDrawingSheetKey(Settings) + "#";
    for (const std::string& Hidden : Document.Appearance.HiddenBodies) Key += Hidden + ";";
    return Key;
}

bool DefaultSolidArcDrawingSheetCondition(const SolidArcDrawingSheetSettings& Settings)
{
    return SolidArcDrawingSheetKey(Settings) == SolidArcDrawingSheetKey(SolidArcDrawingSheetSettings{}) && SolidArcSectionKey(Settings.Section) == SolidArcSectionKey(SolidArcDrawingSheetSettings{}.Section);
}

GeometryProcessing::OrthonormalFrame SolidArcSheetSectionFrame(const GeometryProcessing::OrthonormalFrame& Plane)
{
    return GeometryProcessing::ConstructFrame(Plane.Origin, Plane.ZAxis * -1.0, Plane.XAxis);
}

SolidArcDrawingSheet ComposeSolidArcDrawingSheet(const SolidArcDocument& Document, const SolidArcDrawingSheetSettings& Settings)
{
    const auto Start = std::chrono::steady_clock::now();
    SolidArcDrawingSheet Out;
    Out.Revision    = Document.Revision;
    Out.SettingsKey = SolidArcDrawingSheetCacheKey(Document, Settings);
    const auto Finish = [&]()
    {
        Out.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        return Out;
    };
    std::vector<GeometryProcessing::BodyToken> Bodies;
    if (Document.Partition)
    {
        for (const GeometryProcessing::RegeneratedBody& Body : Document.Regeneration.Bodies) if (ShownSolidCondition(Document, Body, Out.Sheets)) Bodies.push_back(Body.Body);
    }
    if (Bodies.empty())
    {
        Out.Refusal = Out.Sheets ? "Drawing sheet: no shown solid body (sheet bodies are not drawn)" : "Drawing sheet: no shown solid body";
        return Finish();
    }
    Out.Bodies = static_cast<int>(Bodies.size());

    GeometryProcessing::DrawingSheetRequest Request;
    Request.Paper                = Settings.Paper;
    Request.Angle                = Settings.Angle;
    Request.Scale                = Settings.Scale;
    Request.ModelUnitMillimetres = SolidArcModelUnitMillimetres;
    Request.IsometricEnabled     = Settings.IsometricEnabled;
    Request.HiddenLinesEnabled   = Settings.HiddenLinesEnabled;
    Request.TangentEdgesEnabled  = Settings.TangentEdgesEnabled;
    Request.DimensionsEnabled    = Settings.DimensionsEnabled;
    const SolidArcTitleFields& Title = Settings.Title;
    Request.Title = { Title.Title.empty() ? Document.Title : Title.Title, Title.DrawingNumber, Title.Author, Title.Date, Title.Material, Title.Revision, Title.Company };
    if (Settings.SectionEnabled)
    {
        const SolidArcSection Plane = ResolveSolidArcSectionPlane(Document, Settings.Section);
        if (!Plane.PlaneCondition)
        {
            Out.Refusal = "Drawing sheet: section A-A — " + (Plane.Refusal.empty() ? std::string("the plane does not resolve") : Plane.Refusal);
            return Finish();
        }
        Request.Section = SolidArcSheetSectionFrame(Plane.Plane);
    }
    Out.Sheet   = GeometryProcessing::ComposeDrawingSheet(*Document.Partition, Bodies, Request);
    Out.Refusal = Out.Sheet.Refusal;
    return Finish();
}

std::string SolidArcDrawingSheetExportPath(const std::string& DocumentPath, const std::string& Extension)
{
    if (DocumentPath.empty()) return "Untitled" + Extension;
    std::string Stem = DocumentPath;
    for (const char* Suffix : { ".solidarc", ".drafting.toml", ".toml" })
    {
        const std::string Tail = Suffix;
        if (Stem.size() > Tail.size() && Stem.compare(Stem.size() - Tail.size(), Tail.size(), Tail) == 0) { Stem.resize(Stem.size() - Tail.size()); break; }
    }
    return Stem + Extension;
}

std::string ExportSolidArcDrawingSheet(const SolidArcDrawingSheet& Sheet, const std::string& Path)
{
    if (!Sheet.Refusal.empty()) return "Export: the drawing sheet was refused (" + Sheet.Refusal + ")";
    return GeometryProcessing::ExportDrawingFile(Sheet.Sheet, Path);
}

} // namespace Frontier::SolidArc
