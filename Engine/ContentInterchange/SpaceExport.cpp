//============================================================================================================================================
//                                        📦 Engine/ContentInterchange/SpaceExport.cpp — P2/P4/P6
//============================================================================================================================================
#include "SpaceExport.h"

#include "../GeometricRaster/GeometryStructure.h"
#include "../GeometricRaster/SceneStructure.h"

#include <cstddef>
#include <cstring>
#include <fstream>

namespace Frontier
{
namespace
{
    SpaceMetaRecord MakeMeta(const SpaceExportContext& Context, const std::string& Name, uint32_t Policy)
    {
        SpaceMetaRecord Meta{};
        SpaceSetName(Meta.Name, kSpaceNameBytes, Name);
        SpaceSetName(Meta.Exporter, kSpaceExporterBytes, Context.Exporter);
        SpaceSetName(Meta.BuildConfig, kSpaceConfigBytes, Context.BuildConfig);
        Meta.ExporterRevision = 1u;
        Meta.LayoutRevision = kSpaceMajor;
        Meta.PackPolicy = Policy;
        Meta.SourceCount = 0u;
        return Meta;
    }

    bool ReadWholeFile(const std::string& Path, std::vector<uint8_t>& OutBytes, std::string& OutError)
    {
        std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
        if (!Stream) { OutError = "cannot read " + Path; return false; }
        const std::streamoff Size = Stream.tellg();
        OutBytes.resize(static_cast<size_t>(Size < 0 ? 0 : Size));
        Stream.seekg(0);
        Stream.read(reinterpret_cast<char*>(OutBytes.data()), Size);
        return true;
    }
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  GEOMETRY  (.geometry)
//------------------------------------------------------------------------------------------------------------------------
bool SpaceExportGeometry(const SpaceExportContext& Context, const std::string& Name,
                         const VertexRecord* Vertices, uint32_t VertexCount,
                         const uint32_t* Indices, uint32_t IndexCount,
                         const ClusterRecord* Clusters, uint32_t ClusterCount,
                         std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept
{
    OutError.clear();
    if (VertexCount == 0u) { OutError = "a .geometry file needs vertices (" + Name + ")"; return false; }
    // ⚠️ A cluster's triangle range has to be inside the indices it claims: a file that names triangles it does not have
    //    is worse than a file that refuses to be written, because the walker finds out at run time.
    for (uint32_t I = 0u; I < ClusterCount; ++I)
    {
        const ClusterRecord& Cluster = Clusters[I];
        if (Cluster.FirstIndex + size_t(Cluster.TriangleCount) * 3u > IndexCount)
        {
            OutError = "cluster " + std::to_string(I) + " of " + Name + " claims indices " + std::to_string(Cluster.FirstIndex) +
                       "…" + std::to_string(Cluster.FirstIndex + size_t(Cluster.TriangleCount) * 3u) + " of " +
                       std::to_string(IndexCount);
            return false;
        }
    }

    SpaceWriter Writer;
    if (!Writer.Begin(SpaceTag("GEOM"), 1u, MakeMeta(Context, Name, Context.PackPolicy)))
    {
        OutError = "the writer refused the GEOM type tag";
        return false;
    }
    Writer.BeginTable(kTagMesh);
    Writer.WriteBytes(&VertexCount, sizeof(VertexCount));
    Writer.WriteBytes(&IndexCount, sizeof(IndexCount));
    Writer.WriteBytes(Vertices, size_t(VertexCount) * sizeof(VertexRecord));
    Writer.WriteBytes(Indices, size_t(IndexCount) * sizeof(uint32_t));
    Writer.EndTable();
    Writer.BeginTable(kTagClst);
    for (uint32_t I = 0u; I < ClusterCount; ++I) Writer.WriteRow(Clusters[I]);
    Writer.EndTable();
    if (!Writer.Finish(OutBytes, OutError)) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  MATERIAL  (.material)
//------------------------------------------------------------------------------------------------------------------------
bool SpaceExportMaterial(const SpaceExportContext& Context, const MaterialIndex& Materials, uint32_t MaterialId,
                         const std::string& Name, std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept
{
    OutError.clear();
    const std::vector<MaterialRecord>& Records = Materials.QueryRecords();
    if (MaterialId >= Records.size()) { OutError = "no material " + std::to_string(MaterialId); return false; }
    const MaterialRecord& Record = Records[MaterialId];
    const std::vector<MaterialSlabRecord>& Slabs = Materials.QuerySlabRecords();
    if (size_t(Record.SlabOffset) + Record.SlabCount > Slabs.size())
    {
        OutError = "material " + std::to_string(MaterialId) + " names slabs " + std::to_string(Record.SlabOffset) + "…" +
                   std::to_string(Record.SlabOffset + Record.SlabCount) + " of " + std::to_string(Slabs.size());
        return false;
    }
    const std::vector<MaterialDescriptor>& Descriptors = Materials.QueryDescriptors();

    SpaceWriter Writer;
    if (!Writer.Begin(SpaceTag("MATL"), 1u, MakeMeta(Context, Name, Context.PackPolicy)))
    {
        OutError = "the writer refused the MATL type tag";
        return false;
    }
    Writer.BeginTable(kTagMatl);
    Writer.WriteBytes(&Record, sizeof(Record));
    const uint32_t SlabCount = Record.SlabCount;
    Writer.WriteBytes(&SlabCount, sizeof(SlabCount));   // the file's own count: its slab range, never the whole index
    Writer.WriteBytes(&Slabs[Record.SlabOffset], size_t(SlabCount) * sizeof(MaterialSlabRecord));
    Writer.EndTable();

    // TEXR: the bound texture slots. Two slots exist on the record — base colour and normal — and a slot that is not
    //    bound is simply absent, which is the difference between "not bound" and "missing". The row's BlobIndex stays
    //    `none` when the export is not carrying the map's bytes: a reference record (RefIndex) is what a packed project
    //    uses for a map that lives beside it.
    Writer.BeginTable(kTagTexr);
    const uint32_t SlotValues[2] = { Record.BaseColourTexture, Record.NormalTexture };
    for (uint32_t I = 0u; I < 2u; ++I)
    {
        if (SlotValues[I] == kMaterialTextureNone) continue;
        SpaceTextureRow Row{};
        SpaceSetName(Row.Uri, sizeof(Row.Uri),
                     SpaceGetName(Descriptors[MaterialId].Name.c_str(), kSpaceNameBytes) + (I == 1u ? " normal" : " base"));
        Row.RefIndex = 0xFFFFFFFFu;
        Row.BlobIndex = 0xFFFFFFFFu;
        Row.Flags = I == 1u ? 2u : 1u;   // bit0 srgb · bit1 normal map — the slot's own semantics
        Writer.WriteRow(Row);
    }
    Writer.EndTable();
    if (!Writer.Finish(OutBytes, OutError)) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  INSTANCE  (.instance)
//------------------------------------------------------------------------------------------------------------------------
bool SpaceExportInstanceFile(const SpaceExportContext& Context, const std::string& Name,
                             const SpaceInstanceRow* Rows, uint32_t RowCount,
                             const SpaceMaterialSlot* Slots, uint32_t SlotCount,
                             const SpaceReferenceRecord* References, uint32_t ReferenceCount,
                             std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept
{
    OutError.clear();
    for (uint32_t I = 0u; I < RowCount; ++I)
    {
        if (Rows[I].GeometryRef >= ReferenceCount)
        {
            OutError = "instance " + std::to_string(I) + " names reference " + std::to_string(Rows[I].GeometryRef) + " of " +
                       std::to_string(ReferenceCount);
            return false;
        }
        if (Rows[I].MaterialSlot >= SlotCount)
        {
            OutError = "instance " + std::to_string(I) + " names material slot " + std::to_string(Rows[I].MaterialSlot) + " of " +
                       std::to_string(SlotCount);
            return false;
        }
    }

    SpaceWriter Writer;
    if (!Writer.Begin(SpaceTag("INST"), 1u, MakeMeta(Context, Name, Context.PackPolicy)))
    {
        OutError = "the writer refused the INST type tag";
        return false;
    }
    Writer.BeginTable(kTagRefs);
    for (uint32_t I = 0u; I < ReferenceCount; ++I)
    {
        const std::string Path = References[I].Mode == kSpaceRefEmbedded ? std::string() : std::string("payload") + std::to_string(I);
        Writer.WriteRowWithString(References[I], offsetof(SpaceReferenceRecord, PathOffset), Path);
    }
    Writer.EndTable();
    Writer.BeginTable(kTagMslT);
    for (uint32_t I = 0u; I < SlotCount; ++I) Writer.WriteRow(Slots[I]);
    Writer.EndTable();
    Writer.BeginTable(kTagInst);
    for (uint32_t I = 0u; I < RowCount; ++I) Writer.WriteRow(Rows[I]);
    Writer.EndTable();
    if (!Writer.Finish(OutBytes, OutError)) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          THE PROJECT BUILDER  (.projectspace)
//------------------------------------------------------------------------------------------------------------------------
SpaceProjectBuilder::SpaceProjectBuilder(SpaceExportContext InContext) noexcept : Context(std::move(InContext)) {}

uint32_t SpaceProjectBuilder::AddSiblingReference(const SpaceTag& Type, const std::string& Path, uint64_t ContentHash, bool Required) noexcept
{
    // The same file added twice is the same reference: two instances of one object share it, and so do two copies of a
    //    project that imported the same mesh. Dedup by (type, path) is what makes "N copies, one geometry" structural.
    for (uint32_t I = 0u; I < References.size(); ++I)
        if (References[I].Type == Type && References[I].Mode == kSpaceRefSibling && ReferencePaths[I] == Path) return I;
    SpaceReferenceRecord Record{};
    Record.Type = Type;
    Record.Mode = kSpaceRefSibling;
    Record.Flags = Required ? static_cast<uint8_t>(kSpaceRefRequired) : uint8_t(0u);
    Record.ContentHash = ContentHash;
    Record.BlobIndex = 0xFFFFFFFFu;
    References.push_back(Record);
    ReferencePaths.push_back(Path);
    return static_cast<uint32_t>(References.size() - 1u);
}

uint32_t SpaceProjectBuilder::AddEmbeddedReference(const SpaceTag& Type, const std::vector<uint8_t>& Bytes, bool Required) noexcept
{
    const uint64_t Hash = SpaceHash64(Bytes.data(), Bytes.size());
    for (uint32_t I = 0u; I < References.size(); ++I)
    {
        if (References[I].Type != Type || References[I].Mode != kSpaceRefEmbedded) continue;
        if (References[I].ContentHash != Hash) continue;
        if (EmbeddedBytes.size() > I && EmbeddedBytes[I] == Bytes) return I;
    }
    SpaceReferenceRecord Record{};
    Record.Type = Type;
    Record.Mode = kSpaceRefEmbedded;
    Record.Flags = Required ? static_cast<uint8_t>(kSpaceRefRequired | kSpaceRefPreferEmbedded)
                             : static_cast<uint8_t>(kSpaceRefPreferEmbedded);
    Record.ContentHash = Hash;
    Record.BlobIndex = 0xFFFFFFFFu;   // assigned by Finish, which owns the blob table
    References.push_back(Record);
    ReferencePaths.push_back(std::string());
    EmbeddedBytes.push_back(Bytes);
    return static_cast<uint32_t>(References.size() - 1u);
}

uint32_t SpaceProjectBuilder::AddMaterialSlot(uint8_t Mode, uint64_t MaterialHash, uint32_t BlobIndex, const std::string& Path) noexcept
{
    SpaceMaterialSlot Slot{};
    Slot.Mode = Mode;
    Slot.MaterialHash = MaterialHash;
    Slot.BlobIndex = BlobIndex;
    Slots.push_back(Slot);
    SlotPaths.push_back(Path);
    return static_cast<uint32_t>(Slots.size() - 1u);
}

void SpaceProjectBuilder::AddInstance(const SpaceInstanceRow& Row) noexcept { Instances.push_back(Row); }
void SpaceProjectBuilder::AddLevel(const SpaceLevelRow& Row) noexcept { Levels.push_back(Row); }
void SpaceProjectBuilder::AddCamera(const SpaceCameraRow& Row) noexcept { Cameras.push_back(Row); }
void SpaceProjectBuilder::AddLuminaire(const SpaceLuminaireRow& Row) noexcept { Luminaires.push_back(Row); }
void SpaceProjectBuilder::AddLuminaireAlias(const SpaceLuminaireAliasRow& Row) noexcept { LuminaireAliases.push_back(Row); }

bool SpaceProjectBuilder::Finish(const std::string& Name, std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept
{
    OutError.clear();
    SpaceWriter Writer;
    if (!Writer.Begin(SpaceTag("PROJ"), 1u, MakeMeta(Context, Name, Context.PackPolicy)))
    {
        OutError = "the writer refused the PROJ type tag";
        return false;
    }

    // Embedded payloads become blobs here, in reference order, so a reference's index is fixed by the order the caller
    //    added it — which is the same rule SpaceEmitPlan states for the tools.
    for (size_t I = 0u; I < References.size(); ++I)
        if (References[I].Mode == kSpaceRefEmbedded && I < EmbeddedBytes.size())
            References[I].BlobIndex = Writer.AddBlob(References[I].Type.Text(), EmbeddedBytes[I]);

    Writer.BeginTable(kTagRefs);
    for (size_t I = 0u; I < References.size(); ++I)
        Writer.WriteRowWithString(References[I], offsetof(SpaceReferenceRecord, PathOffset), ReferencePaths[I]);
    Writer.EndTable();

    Writer.BeginTable(kTagMslT);
    for (size_t I = 0u; I < Slots.size(); ++I)
        Writer.WriteRowWithString(Slots[I], offsetof(SpaceMaterialSlot, PathOffset), SlotPaths[I]);
    Writer.EndTable();

    Writer.BeginTable(kTagInst);
    for (const SpaceInstanceRow& Row : Instances) Writer.WriteRow(Row);
    Writer.EndTable();

    if (!Cameras.empty())
    {
        Writer.BeginTable(kTagCama);
        for (const SpaceCameraRow& Row : Cameras) Writer.WriteRow(Row);
        Writer.EndTable();
    }
    if (!Luminaires.empty())
    {
        Writer.BeginTable(kTagLite);
        for (const SpaceLuminaireRow& Row : Luminaires) Writer.WriteRow(Row);
        for (const SpaceLuminaireAliasRow& Row : LuminaireAliases) Writer.WriteRow(Row);
        Writer.EndTable();
    }

    // SCEN: the level rows, then the placement count and the placements. A project with no explicit placement still gets
    //    the count, because the reader's layout is fixed — an omitted count is a truncated SCEN, not an empty one.
    Writer.BeginTable(kTagScen);
    for (const SpaceLevelRow& Row : Levels) Writer.WriteRow(Row);
    Writer.EndTable();

    if (!Writer.Finish(OutBytes, OutError)) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                RUNTIME  (.runtime)
//------------------------------------------------------------------------------------------------------------------------
bool SpaceExportRuntime(const SpaceExportContext& Context, const std::string& Name, const std::string& CommandLine,
                        const std::vector<std::pair<SpaceTag, std::string>>& Opens,
                        std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept
{
    OutError.clear();
    SpaceWriter Writer;
    if (!Writer.Begin(SpaceTag("RUNT"), 1u, MakeMeta(Context, Name, Context.PackPolicy)))
    {
        OutError = "the writer refused the RUNT type tag";
        return false;
    }
    // The launch configuration IS state, so it lives in STAT — in a `.runtime` file, which is the one place state is the
    //    point rather than an accident.
    Writer.BeginTable(kTagStat);
    SpaceStateRow State{};
    SpaceSetName(State.Domain, sizeof(State.Domain), "session");
    State.Revision = 1u;
    State.PayloadBytes = static_cast<uint32_t>(CommandLine.size() + 1u);
    Writer.WriteRow(State);
    Writer.WriteBytes(CommandLine.c_str(), CommandLine.size() + 1u);
    Writer.EndTable();

    Writer.BeginTable(kTagRefs);
    for (const std::pair<SpaceTag, std::string>& Open : Opens)
    {
        SpaceReferenceRecord Record{};
        Record.Type = Open.first;
        Record.Mode = kSpaceRefProjectContent;
        Record.Flags = kSpaceRefRequired;
        Record.BlobIndex = 0xFFFFFFFFu;
        Writer.WriteRowWithString(Record, offsetof(SpaceReferenceRecord, PathOffset), Open.second);
    }
    Writer.EndTable();
    if (!Writer.Finish(OutBytes, OutError)) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              ENVIRONMENT  (.environment)
//------------------------------------------------------------------------------------------------------------------------
bool SpaceExportEnvironment(const SpaceExportContext& Context, const SpaceEnvironmentRow& Row, const std::string& Name,
                            const std::vector<uint8_t>& SkyProbe, uint32_t SkyProbeLevels,
                            std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept
{
    OutError.clear();
    SpaceWriter Writer;
    if (!Writer.Begin(SpaceTag("ENVR"), 1u, MakeMeta(Context, Name, Context.PackPolicy)))
    {
        OutError = "the writer refused the ENVR type tag";
        return false;
    }
    SpaceEnvironmentRow Written = Row;
    if (SkyProbe.empty())
    {
        Written.SkyProbeBlob = 0xFFFFFFFFu;   // not baked: a state the format has to be able to express
        Written.SkyProbeLevels = 0u;
    }
    else
    {
        Written.SkyProbeBlob = Writer.AddBlob("PROB", SkyProbe);
        Written.SkyProbeLevels = SkyProbeLevels;
    }
    Writer.BeginTable(kTagEnvr);
    Writer.WriteRow(Written);
    Writer.EndTable();
    if (!Writer.Finish(OutBytes, OutError)) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                UV SPACE  (.uvspace)
//------------------------------------------------------------------------------------------------------------------------
bool SpaceExportUvSpace(const SpaceExportContext& Context, const std::string& Name,
                        const SpaceUvIslandRow* Islands, uint32_t IslandCount,
                        std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept
{
    OutError.clear();
    SpaceWriter Writer;
    if (!Writer.Begin(SpaceTag("UVSP"), 1u, MakeMeta(Context, Name, Context.PackPolicy)))
    {
        OutError = "the writer refused the UVSP type tag";
        return false;
    }
    Writer.BeginTable(kTagUvsp);
    for (uint32_t I = 0u; I < IslandCount; ++I) Writer.WriteRow(Islands[I]);
    Writer.EndTable();
    if (!Writer.Finish(OutBytes, OutError)) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                PIGMENT  (.pigment)
//------------------------------------------------------------------------------------------------------------------------
bool SpaceExportPigment(const SpaceExportContext& Context, const std::string& Name,
                        const SpacePigmentRow* Channels, uint32_t ChannelCount,
                        const SpacePigmentLayerRow* Layers, uint32_t LayerCount,
                        const std::vector<std::vector<uint8_t>>& StrokeBlobs,
                        std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept
{
    OutError.clear();
    SpaceWriter Writer;
    if (!Writer.Begin(SpaceTag("PIGM"), 1u, MakeMeta(Context, Name, Context.PackPolicy)))
    {
        OutError = "the writer refused the PIGM type tag";
        return false;
    }
    std::vector<SpacePigmentLayerRow> Written(Layers, Layers + LayerCount);
    for (size_t I = 0u; I < Written.size() && I < StrokeBlobs.size(); ++I)
    {
        if (StrokeBlobs[I].empty()) { Written[I].StrokeBlob = 0xFFFFFFFFu; continue; }
        Written[I].StrokeBlob = Writer.AddBlob("STRK", StrokeBlobs[I]);
    }
    Writer.BeginTable(kTagPigm);
    for (uint32_t I = 0u; I < ChannelCount; ++I) Writer.WriteRow(Channels[I]);
    for (const SpacePigmentLayerRow& Row : Written) Writer.WriteRow(Row);
    Writer.EndTable();
    if (!Writer.Finish(OutBytes, OutError)) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                WORKFLOW  (.workflow)
//------------------------------------------------------------------------------------------------------------------------
bool SpaceExportWorkflow(const SpaceExportContext& Context, const std::string& Name,
                         const std::vector<std::string>& StepNames, const std::vector<std::string>& StepOperands,
                         std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept
{
    OutError.clear();
    SpaceWriter Writer;
    if (!Writer.Begin(SpaceTag("FLOW"), 1u, MakeMeta(Context, Name, Context.PackPolicy)))
    {
        OutError = "the writer refused the FLOW type tag";
        return false;
    }
    Writer.BeginTable(kTagFlow);
    for (size_t I = 0u; I < StepNames.size(); ++I)
    {
        SpaceFlowStepRow Row{};
        Row.Kind = static_cast<uint32_t>(I < 4u ? I : 4u);   // 0 import · 1 bake · 2 validate · 3 package · 4 custom
        Row.ScriptRef = 0xFFFFFFFFu;
        Row.InputBlob = 0xFFFFFFFFu;
        Row.OutputBlob = 0xFFFFFFFFu;
        const std::string Operand = I < StepOperands.size() ? StepOperands[I] : std::string();
        const size_t Fields[2] = { offsetof(SpaceFlowStepRow, NameOffset), offsetof(SpaceFlowStepRow, OperandOffset) };
        const std::string Values[2] = { StepNames[I], Operand };
        Writer.WriteRowWithStrings(Row, Fields, Values, 2u);
    }
    Writer.EndTable();
    if (!Writer.Finish(OutBytes, OutError)) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 ARCHIVE  (.archive)
//------------------------------------------------------------------------------------------------------------------------
bool SpaceExportArchive(const SpaceExportContext& Context, const std::string& Name,
                        const std::vector<std::string>& Paths,
                        std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept
{
    OutError.clear();
    SpaceWriter Writer;
    if (!Writer.Begin(SpaceTag("ARCH"), 1u, MakeMeta(Context, Name, Context.PackPolicy)))
    {
        OutError = "the writer refused the ARCH type tag";
        return false;
    }
    Writer.BeginTable(kTagArch);
    for (const std::string& Path : Paths)
    {
        std::vector<uint8_t> Bytes;
        if (!ReadWholeFile(Path, Bytes, OutError)) return false;
        SpaceArchiveEntry Entry{};
        Entry.Hash = SpaceHash64(Bytes.data(), Bytes.size());
        Entry.Offset = Writer.AddBlob("CHNK", Bytes);   // the row names a blob; the blob table names the bytes
        Entry.Length = static_cast<uint32_t>(Bytes.size());
        Entry.Flags = 1u;                                // bit0: the last (and here, only) chunk of the entry
        Writer.WriteRow(Entry);
    }
    Writer.EndTable();
    if (!Writer.Finish(OutBytes, OutError)) return false;
    return true;
}
}
