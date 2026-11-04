/*====================================================================================================================================
                                                         SOLIDARCEXCHANGE.CPP
====================================================================================================================================*/
// 🧩 Document ↔ STEP / STL: kernel WriteStep / WriteStl (metres → millimetres) out, ReadStep + canonical WriteStep in

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Document/SolidArcExchange.h"

#include "UI/SolidArc/Document/SolidArcDrawingSheet.h"

#include "Engine/GeometryProcessing/Exchange/StepExchange.h"
#include "Engine/GeometryProcessing/Exchange/StlExchange.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] double ElapsedMilliseconds(std::chrono::steady_clock::time_point Start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
}

// File name without folder and exchange / document extension ("Parts/Boss.step" → "Boss").
[[nodiscard]] std::string FileStem(const std::string& Path)
{
    std::string Stem = std::filesystem::path(Path).filename().string();
    for (const char* Suffix : { ".solidarc", ".drafting.toml", ".toml", ".step", ".stp", ".stl", ".STEP", ".STP", ".STL" })
    {
        const std::string Tail = Suffix;
        if (Stem.size() > Tail.size() && Stem.compare(Stem.size() - Tail.size(), Tail.size(), Tail) == 0) { Stem.resize(Stem.size() - Tail.size()); break; }
    }
    return Stem.empty() ? std::string("Untitled") : Stem;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SolidArcExchangeFormat SolidArcExchangeFormatOf(const std::string& Path)
{
    std::string Extension = std::filesystem::path(Path).extension().string();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char Letter) { return static_cast<char>(std::tolower(Letter)); });
    if (Extension == ".step" || Extension == ".stp") return SolidArcExchangeFormat::Step;
    if (Extension == ".stl") return SolidArcExchangeFormat::Stl;
    if (Extension == ".toml") return SolidArcExchangeFormat::Toml;
    return SolidArcExchangeFormat::Unknown;
}

const char* SolidArcExchangeFormatLabel(SolidArcExchangeFormat Format) noexcept
{
    switch (Format)
    {
    case SolidArcExchangeFormat::Step:    return "STEP";
    case SolidArcExchangeFormat::Stl:     return "STL";
    case SolidArcExchangeFormat::Toml:    return "TOML";
    case SolidArcExchangeFormat::Unknown: break;
    }
    return "unknown";
}

std::string SolidArcExportPath(const std::string& DocumentPath, const std::string& Extension)
{
    return SolidArcDrawingSheetExportPath(DocumentPath, Extension);
}

std::vector<std::string> SolidArcExportBodies(const SolidArcDocument& Document)
{
    std::vector<std::string> Keys;
    for (const std::string& Key : Document.Selection.Bodies)
    {
        const bool Present = std::any_of(Document.Regeneration.Bodies.begin(), Document.Regeneration.Bodies.end(),
                                         [&](const GeometryProcessing::RegeneratedBody& Body) { return Body.Key == Key && Body.Body.PresentCondition(); });
        if (Present && std::find(Keys.begin(), Keys.end(), Key) == Keys.end()) Keys.push_back(Key);
    }
    if (!Keys.empty()) return Keys;
    for (const GeometryProcessing::RegeneratedBody& Body : Document.Regeneration.Bodies)
        if (Body.Body.PresentCondition() && !Document.Appearance.HiddenBodies.contains(Body.Key) && !Document.Appearance.LiveCutters.contains(Body.Key)) Keys.push_back(Body.Key);
    return Keys;
}

SolidArcExportOutcome ExportSolidArcBodies(const SolidArcDocument& Document, const std::string& Path, const std::vector<std::string>& Keys)
{
    const auto Start = std::chrono::steady_clock::now();
    SolidArcExportOutcome Outcome;
    Outcome.Path   = Path;
    Outcome.Format = SolidArcExchangeFormatOf(Path);
    Outcome.Bodies = Keys.empty() ? SolidArcExportBodies(Document) : Keys;
    const auto Refuse = [&](const std::string& Reason) { Outcome.Refusal = "Export: " + Reason; Outcome.Milliseconds = ElapsedMilliseconds(Start); return Outcome; };
    if (Outcome.Format != SolidArcExchangeFormat::Step && Outcome.Format != SolidArcExchangeFormat::Stl) return Refuse(Path + " is neither .step / .stp nor .stl");
    if (!Document.Partition) return Refuse("the document has not been regenerated");
    if (Outcome.Bodies.empty()) return Refuse("no shown body to write");
    std::vector<GeometryProcessing::BodyToken> Tokens;
    for (const std::string& Key : Outcome.Bodies)
    {
        const auto Found = std::find_if(Document.Regeneration.Bodies.begin(), Document.Regeneration.Bodies.end(), [&](const GeometryProcessing::RegeneratedBody& Body) { return Body.Key == Key; });
        if (Found == Document.Regeneration.Bodies.end() || !Found->Body.PresentCondition()) return Refuse("no body named " + Key);
        Tokens.push_back(Found->Body);
    }
    std::error_code Error;
    const std::filesystem::path Folder = std::filesystem::path(Path).parent_path();
    if (!Folder.empty()) std::filesystem::create_directories(Folder, Error);                // Beside a document not saved yet
    std::string Failure;
    if (Outcome.Format == SolidArcExchangeFormat::Step)
    {
        GeometryProcessing::StepRequest Request;
        Request.ModelUnitMillimetres = SolidArcModelUnitMillimetres;
        Request.Name                 = FileStem(Path);
        const GeometryProcessing::StepExport Export = GeometryProcessing::WriteStep(*Document.Partition, Tokens, Request);
        if (!Export.Refusal.empty()) return Refuse(Export.Refusal);
        Failure              = GeometryProcessing::ExportStepFile(Export, Path);
        Outcome.Faces        = Export.Faces;
        Outcome.Approximated = Export.ApproximatedSurfaces + Export.ApproximatedCurves;
    }
    else
    {
        GeometryProcessing::StlRequest Request;
        Request.ModelUnitMillimetres = SolidArcModelUnitMillimetres;
        Request.Name                 = FileStem(Path);
        const GeometryProcessing::StlExport Export = GeometryProcessing::WriteStl(*Document.Partition, Tokens, Request);
        if (!Export.Refusal.empty()) return Refuse(Export.Refusal);
        Failure           = GeometryProcessing::ExportStlFile(Export, Path);
        Outcome.Triangles = Export.Triangles;
    }
    if (!Failure.empty()) return Refuse(Failure);
    Outcome.Bytes        = double(std::filesystem::file_size(Path, Error));
    Outcome.Milliseconds = ElapsedMilliseconds(Start);
    return Outcome;
}

SolidArcStepReading ReadSolidArcStepFile(const std::string& Path)
{
    const auto Start = std::chrono::steady_clock::now();
    SolidArcStepReading Reading;
    Reading.Stem = FileStem(Path);
    const auto Refuse = [&](const std::string& Reason) { Reading.Refusal = Reason; Reading.Texts.clear(); Reading.Milliseconds = ElapsedMilliseconds(Start); return Reading; };
    GeometryProcessing::ModelPartition    Scratch;
    GeometryProcessing::StepImportRequest Request;
    Request.ModelUnitMillimetres = SolidArcModelUnitMillimetres;
    Request.Label                = "Import " + std::filesystem::path(Path).filename().string();
    const GeometryProcessing::StepImport Imported = GeometryProcessing::ImportStepFile(Scratch, Path, Request);
    if (!Imported.Refusal.empty()) return Refuse(Imported.Refusal);
    if (Imported.Sheets > 0) return Refuse(Request.Label + ": the file holds " + std::to_string(Imported.Sheets) + " surface model(s) (🚧 solids only)");
    Reading.Faces               = Imported.Faces;
    Reading.FileUnitMillimetres = Imported.FileUnitMillimetres;
    Reading.ReorientedFaces     = Imported.ReorientedFaces;
    Reading.TolerantEdges       = Imported.TolerantEdges;
    Reading.Notes               = Imported.Notes;
    for (size_t Index = 0; Index < Imported.Bodies.size(); ++Index)
    {
        GeometryProcessing::StepRequest Canonical;
        Canonical.ModelUnitMillimetres = SolidArcModelUnitMillimetres;
        Canonical.Name                 = Imported.Bodies.size() == 1 ? Reading.Stem : Reading.Stem + "." + std::to_string(Index + 1);
        const GeometryProcessing::StepExport Export = GeometryProcessing::WriteStep(Scratch, { Imported.Bodies[Index] }, Canonical);
        if (!Export.Refusal.empty()) return Refuse(Request.Label + ": solid " + std::to_string(Index + 1) + " cannot be embedded: " + Export.Refusal);
        Reading.Texts.push_back(Export.Bytes);
    }
    Reading.Milliseconds = ElapsedMilliseconds(Start);
    return Reading;
}

} // namespace Frontier::SolidArc
