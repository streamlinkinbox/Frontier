/*============================================================================================================================================
                                                  PARTICLECODECCINTERCHANGEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase 2 validation: Two-Tier .ParticleSpace round-trip (TOML + 64B .ColourLut/.SurfaceAnchor/.StrandRest/.Revision) + 102 HTML presets import

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ParticleCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/Interchange/ParticleInterchange.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/Workspaces/Revision/RevisionTracker.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{

using namespace Frontier::Codecs;
using namespace Frontier::Interchange;
using namespace Frontier::ParticleSpace;
using Frontier::Exhibits::ResolveRepositoryRootPath;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ParticleCodecInterchangeValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

bool NearEqual(float A, float B, float Tol = 1.0e-3f) noexcept
{
    return std::fabs(A - B) <= Tol;
}

std::string HexToLinearRgbaArrayJson(std::string_view Hex)
{
    if (Hex.size() < 7u || Hex[0] != '#')
    {
        return "[1,1,1,1]";
    }
    auto ParseByte = [&](std::size_t Off) -> float {
        unsigned int Byte = 255u;
        std::sscanf(std::string(Hex.substr(Off, 2u)).c_str(), "%x", &Byte);
        const float S = static_cast<float>(Byte) / 255.0f;
        return (S <= 0.04045f) ? (S / 12.92f) : std::pow((S + 0.055f) / 1.055f, 2.4f);
    };
    float Alpha = 1.0f;
    if (Hex.size() >= 9u)
    {
        unsigned int Byte = 255u;
        std::sscanf(std::string(Hex.substr(7u, 2u)).c_str(), "%x", &Byte);
        Alpha = static_cast<float>(Byte) / 255.0f;
    }
    std::ostringstream Oss;
    Oss << "[" << ParseByte(1u) << "," << ParseByte(3u) << "," << ParseByte(5u) << "," << Alpha << "]";
    return Oss.str();
}

// Converts a JS P({...}) literal block from Experimental/ParticleEditor Final/Engine/presets.js into valid JSON
std::string NormalizeJsPresetObjectToJson(std::string_view RawJs)
{
    // 1. Strip // line comments (outside quotes)
    std::string NoComments;
    NoComments.reserve(RawJs.size());
    bool InStr = false;
    char Quote = '\0';
    for (std::size_t I = 0u; I < RawJs.size(); ++I)
    {
        const char C = RawJs[I];
        if (!InStr && (C == '"' || C == '\''))
        {
            InStr = true;
            Quote = C;
            NoComments.push_back('"');
            continue;
        }
        if (InStr)
        {
            if (C == '\\' && I + 1u < RawJs.size())
            {
                NoComments.push_back(C);
                NoComments.push_back(RawJs[++I]);
                continue;
            }
            if (C == Quote)
            {
                InStr = false;
                NoComments.push_back('"');
                continue;
            }
            NoComments.push_back(C);
            continue;
        }
        if (C == '/' && I + 1u < RawJs.size() && RawJs[I + 1u] == '/')
        {
            while (I < RawJs.size() && RawJs[I] != '\n') ++I;
            NoComments.push_back('\n');
            continue;
        }
        NoComments.push_back(C);
    }

    // 2. Replace hex("#rrggbb[aa]") calls
    std::string HexReplaced;
    HexReplaced.reserve(NoComments.size());
    for (std::size_t I = 0u; I < NoComments.size();)
    {
        if (NoComments.compare(I, 5u, "hex(\"") == 0)
        {
            const std::size_t EndQ = NoComments.find("\")", I + 5u);
            if (EndQ != std::string::npos)
            {
                HexReplaced += HexToLinearRgbaArrayJson(std::string_view(NoComments).substr(I + 5u, EndQ - (I + 5u)));
                I = EndQ + 2u;
                continue;
            }
        }
        HexReplaced.push_back(NoComments[I++]);
    }

    // 3. Quote unquoted JS keys, evaluate simple bitwise-OR expressions (1 | 2 | 4), strip trailing commas
    std::string Quoted;
    Quoted.reserve(HexReplaced.size() + 128u);
    InStr = false;
    for (std::size_t I = 0u; I < HexReplaced.size();)
    {
        const char C = HexReplaced[I];
        if (C == '"')
        {
            InStr = !InStr;
            Quoted.push_back(C);
            ++I;
            continue;
        }
        if (InStr)
        {
            if (C == '\\' && I + 1u < HexReplaced.size())
            {
                Quoted.push_back(C);
                Quoted.push_back(HexReplaced[++I]);
                ++I;
                continue;
            }
            Quoted.push_back(C);
            ++I;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(C)) || C == '_')
        {
            std::size_t End = I + 1u;
            while (End < HexReplaced.size() && (std::isalnum(static_cast<unsigned char>(HexReplaced[End])) || HexReplaced[End] == '_'))
            {
                ++End;
            }
            std::size_t Peek = End;
            while (Peek < HexReplaced.size() && std::isspace(static_cast<unsigned char>(HexReplaced[Peek]))) ++Peek;
            if (Peek < HexReplaced.size() && HexReplaced[Peek] == ':')
            {
                Quoted.push_back('"');
                Quoted.append(HexReplaced, I, End - I);
                Quoted.push_back('"');
                I = End;
                continue;
            }
            Quoted.append(HexReplaced, I, End - I);
            I = End;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(C)))
        {
            // Check for integer | integer | ...
            std::size_t Scan = I;
            uint32_t Accum = 0u;
            bool SawOr = false;
            std::size_t LastEnd = I;
            while (Scan < HexReplaced.size() && std::isdigit(static_cast<unsigned char>(HexReplaced[Scan])))
            {
                uint32_t Val = 0u;
                while (Scan < HexReplaced.size() && std::isdigit(static_cast<unsigned char>(HexReplaced[Scan])))
                {
                    Val = Val * 10u + static_cast<uint32_t>(HexReplaced[Scan++] - '0');
                }
                Accum |= Val;
                LastEnd = Scan;
                while (Scan < HexReplaced.size() && std::isspace(static_cast<unsigned char>(HexReplaced[Scan]))) ++Scan;
                if (Scan < HexReplaced.size() && HexReplaced[Scan] == '|')
                {
                    SawOr = true;
                    ++Scan;
                    while (Scan < HexReplaced.size() && std::isspace(static_cast<unsigned char>(HexReplaced[Scan]))) ++Scan;
                    continue;
                }
                break;
            }
            if (SawOr)
            {
                Quoted += std::to_string(Accum);
                I = LastEnd;
                continue;
            }
        }
        Quoted.push_back(C);
        ++I;
    }

    // 4. Remove trailing commas before } or ]
    std::string Clean;
    Clean.reserve(Quoted.size());
    InStr = false;
    for (std::size_t I = 0u; I < Quoted.size(); ++I)
    {
        const char C = Quoted[I];
        if (C == '"' && (I == 0u || Quoted[I - 1u] != '\\'))
        {
            InStr = !InStr;
            Clean.push_back(C);
            continue;
        }
        if (!InStr && C == ',')
        {
            std::size_t Peek = I + 1u;
            while (Peek < Quoted.size() && std::isspace(static_cast<unsigned char>(Quoted[Peek]))) ++Peek;
            if (Peek < Quoted.size() && (Quoted[Peek] == '}' || Quoted[Peek] == ']'))
            {
                continue;
            }
        }
        Clean.push_back(C);
    }
    return Clean;
}

struct ParsedHtmlPresetEntry
{
    std::string Id;
    std::string Name;
    std::string ParamsJson;
    bool        Quarantined = false;
};

std::vector<ParsedHtmlPresetEntry> LoadAllHtmlPresetsFromJs(const std::filesystem::path& PresetsJsPath)
{
    std::ifstream File(PresetsJsPath);
    std::ostringstream Buffer;
    Buffer << File.rdbuf();
    const std::string Content = Buffer.str();

    std::unordered_set<std::string> QuarantineSet;
    const std::size_t QPos = Content.find("PE.QUARANTINE_IDS = new Set([");
    if (QPos != std::string::npos)
    {
        const std::size_t QEnd = Content.find("]);", QPos);
        if (QEnd != std::string::npos)
        {
            std::size_t P = QPos;
            while ((P = Content.find('"', P)) < QEnd)
            {
                const std::size_t P2 = Content.find('"', P + 1u);
                if (P2 == std::string::npos || P2 >= QEnd) break;
                QuarantineSet.insert(Content.substr(P + 1u, P2 - P - 1u));
                P = P2 + 1u;
            }
        }
    }

    std::vector<ParsedHtmlPresetEntry> Results;
    std::size_t Cursor = Content.find("PE.Presets = [");
    if (Cursor == std::string::npos) return Results;

    while (true)
    {
        const std::size_t IdPos = Content.find("id: \"", Cursor);
        if (IdPos == std::string::npos) break;
        const std::size_t IdEnd = Content.find('"', IdPos + 5u);
        const std::string Id    = Content.substr(IdPos + 5u, IdEnd - (IdPos + 5u));

        const std::size_t NamePos = Content.find("name: \"", IdEnd);
        const std::size_t NameEnd = Content.find('"', NamePos + 7u);
        const std::string Name    = Content.substr(NamePos + 7u, NameEnd - (NamePos + 7u));

        const std::size_t PStart = Content.find("p: P({", NameEnd);
        if (PStart == std::string::npos) break;
        const std::size_t BraceStart = PStart + 5u;
        int Depth = 0;
        bool InStr = false;
        std::size_t BraceEnd = BraceStart;
        for (std::size_t I = BraceStart; I < Content.size(); ++I)
        {
            const char C = Content[I];
            if (C == '"' && (I == 0u || Content[I - 1u] != '\\')) InStr = !InStr;
            if (InStr) continue;
            if (C == '{') ++Depth;
            else if (C == '}')
            {
                --Depth;
                if (Depth == 0)
                {
                    BraceEnd = I;
                    break;
                }
            }
        }
        const std::string RawBlock = Content.substr(BraceStart, BraceEnd - BraceStart + 1u);
        Results.push_back({
            Id,
            Name,
            NormalizeJsPresetObjectToJson(RawBlock),
            QuarantineSet.count(Id) > 0u,
        });
        Cursor = BraceEnd + 1u;
    }
    return Results;
}

} // namespace

int main()
{
    bool AllPassed = true;
    const std::filesystem::path RepoRoot = ResolveRepositoryRootPath();

    // 1. Multi-rig ParticleDocument round-trip through .ParticleSpace Two-Tier Hybrid codec
    ParticleDocument SourceDoc{};
    SourceDoc.DocumentTitle   = "MultiRigValidation";
    SourceDoc.ActivePresetId  = "fireworks-spectacle";
    SourceDoc.RandomSeed      = 2026u;
    SourceDoc.TimeScale       = 1.25f;
    SourceDoc.PlaybackLoopSec = 14.0f;
    SourceDoc.AmbientWindMps  = { 1.4f, -0.6f, 0.2f };
    SourceDoc.Collider.BallPositionMeters   = { 0.5f, 0.2f, 1.3f };
    SourceDoc.Collider.BallRadiusMeters     = 0.45f;
    SourceDoc.Collider.SignedDistanceActive = true;

    SourceDoc.PopulateDefaultSparksRig();

    // Add a ClothGrid rig (.StrandRest) and a SurfaceAnchor rig (.SurfaceAnchor)
    ParticleRig ClothRig{};
    ClothRig.RigId    = 2u;
    ClothRig.RigTitle = "Signal Banner";
    ClothRig.Origin   = ClothGridOriginSpec{ { 0.0f, 0.0f, 3.0f }, 20u, 16u, 2.4f, 1.8f };
    ClothRig.Carrier  = RibbonStrandCarrier{ RibbonProfileCategory::WovenCloth, 16u, 0.06f, 1.0f, true, Frontier::ColourSpace::OpticalBlendEquation::PremultipliedAlpha };
    ClothRig.Dynamics.Strands.Mode         = StrandDynamicsMode::FlagSail;
    ClothRig.Dynamics.Strands.RowCount     = 20u;
    ClothRig.Dynamics.Strands.SegmentCount = 16u;
    (void)SourceDoc.AddRig(std::move(ClothRig));

    ParticleRig SurfaceRig{};
    SurfaceRig.RigId    = 3u;
    SurfaceRig.RigTitle = "Utah Teapot Lattice";
    SurfaceRig.Origin   = SurfaceAnchorOriginSpec{ SurfacePresetCategory::UtahTeapot, SurfaceSampleMode::EvenSurface, { 0.0f, 0.0f, 1.0f }, { 12.0f, 0.0f, 35.0f }, 1.8f, 24u, 24u, "" };
    SurfaceRig.Carrier  = OrientedSolidCarrier{ SolidGeometryCategory::MoGraphExtrudedPrism, 1.2f, 0.18f, Frontier::ColourSpace::OpticalBlendEquation::PremultipliedAlpha };
    SurfaceRig.Dynamics.EffectorGrid.Active = true;
    (void)SourceDoc.AddRig(std::move(SurfaceRig));

    // Attach inline ForceSource via VectorFieldCodec delegation
    Frontier::SimulationSpace::ForceFields::ForceSourceRecordEntry WindForce{};
    WindForce.UniqueToken = 10u;
    WindForce.Label       = "Crosswind Gust";
    WindForce.Model       = Frontier::SimulationSpace::ForceFields::ForceModel::AtmosphericGust;
    WindForce.Strength    = 12.5f;
    SourceDoc.ForceSources.push_back(WindForce);

    Frontier::Workspaces::RevisionTracker SourceRev;
    Frontier::Workspaces::ParametricOperationParameters RevParams{};
    RevParams.Scalars[0] = 520.0;
    std::snprintf(RevParams.OperationTag, sizeof(RevParams.OperationTag), "ParticleReleaseRate");
    AllPassed &= VerifyCheck(SourceRev.RecordParametricOperation(RevParams, 1ull), "RecordParametricOperation failed");

    const SpaceContainerPayload Encoded = ParticleEncoder::BuildPayload(SourceDoc, &SourceRev);
    AllPassed &= VerifyCheck(Encoded.ExtensionCategory == SpaceExtensionCategory::ParticleSpace, "Container category mismatch");
    bool HasLut = false, HasAnchors = false, HasStrands = false, HasRev = false;
    for (const BinarySectionBlock& Sec : Encoded.BinarySections)
    {
        if (Sec.SubFormatTag == ".ColourLut") HasLut = true;
        if (Sec.SubFormatTag == ".SurfaceAnchor") HasAnchors = true;
        if (Sec.SubFormatTag == ".StrandRest") HasStrands = true;
        if (Sec.SubFormatTag == ".Revision") HasRev = true;
    }
    AllPassed &= VerifyCheck(HasLut && HasAnchors && HasStrands && HasRev, "Expected Tier 2 binary sections missing");

    const std::filesystem::path TempSpacePath = std::filesystem::temp_directory_path() / "ParticleCodecValidation.ParticleSpace";
    std::string Diag;
    AllPassed &= VerifyCheck(ParticleEncoder::EncodeToFile(TempSpacePath, SourceDoc, Diag, &SourceRev), "EncodeToFile failed");

    ParticleDocument DecodedDoc{};
    Frontier::Workspaces::RevisionTracker DecodedRev;
    AllPassed &= VerifyCheck(ParticleDecoder::DecodeFromFile(TempSpacePath, DecodedDoc, Diag, &DecodedRev), "DecodeFromFile failed");
    std::filesystem::remove(TempSpacePath);

    AllPassed &= VerifyCheck(DecodedDoc.DocumentTitle == SourceDoc.DocumentTitle, "DocumentTitle mismatch");
    AllPassed &= VerifyCheck(DecodedDoc.Rigs.size() == 3u, "Rig count mismatch after decode");
    AllPassed &= VerifyCheck(DecodedDoc.ForceSources.size() == 1u && DecodedDoc.ForceSources[0].Label == "Crosswind Gust", "Inline ForceSource mismatch");
    AllPassed &= VerifyCheck(NearEqual(DecodedDoc.Collider.BallRadiusMeters, 0.45f) && DecodedDoc.Collider.SignedDistanceActive, "InteractiveCollider mismatch");
    AllPassed &= VerifyCheck(!DecodedDoc.Rigs[2].SampledAnchors.Anchors.empty(), "Decoded SurfaceAnchors empty");

    // 2. Import ALL 102 presets verbatim from Experimental/ParticleEditor Final/Engine/presets.js
    const std::filesystem::path PresetsJsPath = RepoRoot / "Experimental/ParticleEditor Final/Engine/presets.js";
    const auto HtmlPresets = LoadAllHtmlPresetsFromJs(PresetsJsPath);
    AllPassed &= VerifyCheck(HtmlPresets.size() == 102u, "Expected exactly 102 presets in presets.js");

    uint32_t ActiveCount = 0u, QuarantinedCount = 0u;
    for (const ParsedHtmlPresetEntry& Entry : HtmlPresets)
    {
        ParticleRig Rig{};
        std::string ImportDiag;
        const bool Ok = ParticleInterchange::ImportHtmlStudioPresetJson(
            Entry.ParamsJson,
            Entry.Id,
            Entry.Name,
            Entry.Quarantined,
            Rig,
            ImportDiag);
        if (!Ok)
        {
            std::fprintf(stderr, "[ParticleCodecInterchangeValidation] Failed importing preset %s: %s\n", Entry.Id.c_str(), ImportDiag.c_str());
        }
        AllPassed &= Ok;
        AllPassed &= VerifyCheck(Rig.PresetOriginId == Entry.Id && Rig.Sizing.CapacitySlots > 0u, "Imported rig sanity check failed");
        if (Rig.Quarantined) ++QuarantinedCount;
        else ++ActiveCount;

        // Verify .ParticleSpace codec round-trip + handoff packet on every 10th preset & all special modes
        ParticleDocument SingleDoc{};
        SingleDoc.ActivePresetId = Entry.Id;
        SingleDoc.Rigs.push_back(Rig);
        const SpaceContainerPayload SinglePayload = ParticleEncoder::BuildPayload(SingleDoc);
        ParticleDocument RoundTripped{};
        AllPassed &= VerifyCheck(ParticleDecoder::ExtractDocument(SinglePayload, RoundTripped, ImportDiag), "102-preset codec ExtractDocument failed");
        AllPassed &= VerifyCheck(RoundTripped.Rigs.size() == 1u && RoundTripped.Rigs[0].PresetOriginId == Entry.Id, "102-preset round-trip PresetOriginId mismatch");
    }
    AllPassed &= VerifyCheck(ActiveCount == 93u && QuarantinedCount == 9u, "Expected 93 active + 9 quarantined presets");

    // 3. Handoff packet + external OBJ surface import via GeometryInterchange
    const ParticleHandoffPacket Packet = ParticleInterchange::BuildHandoffPacket(SourceDoc.Rigs[0]);
    ParticleRig HandoffTarget{};
    AllPassed &= VerifyCheck(ParticleInterchange::ApplyHandoffPacket(Packet, HandoffTarget, Diag), "Handoff packet apply failed");
    AllPassed &= VerifyCheck(HandoffTarget.PresetOriginId == SourceDoc.Rigs[0].PresetOriginId, "Handoff PresetId mismatch");

    const std::filesystem::path TempObjPath = std::filesystem::temp_directory_path() / "ParticleCustomSurface.obj";
    {
        std::ofstream ObjOut(TempObjPath);
        ObjOut << "v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nf 1 2 3\nf 1 3 4\n";
    }
    ParticleRig CustomSurfRig{};
    AllPassed &= VerifyCheck(ParticleInterchange::ImportExternalBackingSurface(TempObjPath, CustomSurfRig, Diag), "External OBJ import failed");
    std::filesystem::remove(TempObjPath);
    AllPassed &= VerifyCheck(CustomSurfRig.BackingSurface.SurfacePreset == SurfacePresetCategory::CustomPolygonNetwork, "Custom surface preset not assigned");
    AllPassed &= VerifyCheck(!CustomSurfRig.SampledAnchors.Anchors.empty(), "Custom OBJ anchor table empty");

    if (!AllPassed)
    {
        return 1;
    }
    std::printf(
        "[ParticleCodecInterchangeValidation] All Phase 2 checks passed (%zu/102 HTML presets imported & round-tripped: %u active, %u quarantined).\n",
        HtmlPresets.size(),
        ActiveCount,
        QuarantinedCount);
    return 0;
}
