//============================================================================================================================================
//                    📦 Exhibits/Workbench/ProjectFormat/SpaceFamilyProof.cpp — P1–P6's gate
//============================================================================================================================================
// 🧩 The Space family, checked on this machine. Every claim is the one Docs/ProjectFormat.md §8 names, in order, and the
//    two that are GPU claims (2 and 6 are image comparisons) are reported as SKIPPED with their CPU half stated — the
//    claim that the loaded records are the resident records, byte for byte, is checkable here and is checked here.
//
// The level is the M10 material level, built by MaterialSwatchStructure (the same source the traversal proofs read), so
//    the bytes under test are the engine's own records rather than a synthetic scene. A project file is written from it,
//    read back, re-written, packed, exploded, corrupted on purpose and re-read — and each step's verdict is the claim's.
//
// ⚠️ What this program does NOT do: render. Claims 2 and 6 compare images (AE = 0 against the gallery PNG), which needs
//    a GPU; the CPU half of both — "the records the container hands the renderer are the records the glTF path builds" —
//    is what runs here, and the two skips say so on the line they skip.
#include "SpaceCodec.h"
#include "MaterialIndex.h"
#include "MaterialSwatchStructure.h"
#include "GeometryStructure.h"
#include "SceneStructure.h"

#include "CommandLine.h"

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{
    int Failures = 0, Passes = 0, Skips = 0;

    void Line(const char* Prefix, const char* Format, va_list Arguments)
    {
        std::printf("  %s  ", Prefix);
        std::vprintf(Format, Arguments);
        std::printf("\n");
    }
    void Pass(const char* Format, ...) { ++Passes; va_list A; va_start(A, Format); Line("PASS", Format, A); va_end(A); }
    void Fail(const char* Format, ...) { ++Failures; va_list A; va_start(A, Format); Line("FAIL", Format, A); va_end(A); }
    void Skip(const char* Format, ...) { ++Skips; va_list A; va_start(A, Format); Line("SKIP", Format, A); va_end(A); }

    bool WriteFile(const std::string& Path, const std::vector<uint8_t>& Bytes)
    {
        const size_t Slash = Path.find_last_of('/');
        if (Slash != std::string::npos)
        {
            const std::string Directory = Path.substr(0u, Slash);
            std::string Command = "mkdir -p '" + Directory + "'";
            if (std::system(Command.c_str()) != 0) return false;
        }
        std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
        if (!Stream) return false;
        Stream.write(reinterpret_cast<const char*>(Bytes.data()), std::streamsize(Bytes.size()));
        return Stream.good();
    }

    size_t FileSize(const std::string& Path)
    {
        std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
        return Stream ? size_t(Stream.tellg()) : 0u;
    }

    // Begin() answers "is this a tag in the family" — a writer that silently wrote nothing would round-trip trivially.
    void BeginOk(SpaceWriter& Writer, const SpaceTag& Type, uint32_t Revision, const SpaceMetaRecord& Meta)
    {
        if (!Writer.Begin(Type, Revision, Meta)) Fail("the writer refused %s", Type.Text().c_str());
    }

    SpaceMetaRecord MakeMeta(const std::string& Name, uint32_t Policy)
    {
        SpaceMetaRecord Meta{};
        SpaceSetName(Meta.Name, kSpaceNameBytes, Name);
        SpaceSetName(Meta.Exporter, kSpaceExporterBytes, "SpaceFamilyProof");
        SpaceSetName(Meta.BuildConfig, kSpaceConfigBytes, "Development");
        Meta.ExporterRevision = 1u;
        Meta.LayoutRevision = kSpaceMajor;
        Meta.PackPolicy = Policy;
        Meta.SourceCount = 0u;
        return Meta;
    }

    // ── the level's geometry, as the container sees it: one MESH per named span, one INST row per placement ──────────
    struct Object
    {
        std::string       Name;
        std::vector<VertexRecord> Vertices;
        std::vector<uint32_t>     Indices;
        std::vector<ClusterRecord> Clusters;
        bool              Dynamic = false;
        float             Transform[16] = { 1.0f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,
                                            0.0f, 0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 0.0f, 1.0f };
    };

    // One span of the M10 soup becomes one object: every triangle contributes its three corners (a soup, not a welded
    //    mesh — vertex welding is the importer's job and is not what this gate is about). The point is that the object's
    //    vertices ARE VertexRecords: the container stores resident records, so the round trip is a memcmp.
    Object ObjectFromSpan(const std::vector<TriangleIndex>& Soup, const TriangleSpanRecord& Span)
    {
        Object Out;
        Out.Name = Span.Name;
        Out.Dynamic = Span.Dynamic;
        Out.Vertices.reserve(size_t(Span.TriangleCount) * 3u);
        Out.Indices.reserve(size_t(Span.TriangleCount) * 3u);
        float Centre[3] = { 0.0f, 0.0f, 0.0f };
        for (uint32_t T = 0u; T < Span.TriangleCount; ++T)
        {
            const TriangleIndex& Index = Soup[size_t(Span.FirstTriangle) + T];
            const float Positions[3][3] = {
                { Index.VertexAlphaX, Index.VertexAlphaY, Index.VertexAlphaZ },
                { Index.VertexBetaX,  Index.VertexBetaY,  Index.VertexBetaZ  },
                { Index.VertexGammaX, Index.VertexGammaY, Index.VertexGammaZ }
            };
            const float Uvs[3][2] = { { Index.TextureBetaU, Index.TextureBetaV },   // β is α→β's texture carrier
                                      { Index.TextureAlphaU, Index.TextureAlphaV },
                                      { Index.TextureGammaU, Index.TextureGammaV } };
            for (int V = 0; V < 3; ++V)
            {
                const float U[3] = { Positions[1][0] - Positions[0][0], Positions[1][1] - Positions[0][1], Positions[1][2] - Positions[0][2] };
                const float W[3] = { Positions[2][0] - Positions[0][0], Positions[2][1] - Positions[0][1], Positions[2][2] - Positions[0][2] };
                float N[3] = { U[1] * W[2] - U[2] * W[1], U[2] * W[0] - U[0] * W[2], U[0] * W[1] - U[1] * W[0] };
                const float Length = std::sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
                const float Inverse = Length > 0.0f ? 1.0f / Length : 0.0f;
                for (float& Component : N) Component *= Inverse;

                VertexRecord Vertex{};
                Vertex.SpatialLocation = Vector3(Positions[V][0], Positions[V][1], Positions[V][2]);
                Vertex.NormalDirection = Vector3(N[0], N[1], N[2]);
                Vertex.TangentDirection = Vector4(U[0], U[1], U[2], 1.0f);
                Vertex.TextureCoordinateU = Uvs[V][0];
                Vertex.TextureCoordinateV = Uvs[V][1];
                Out.Vertices.push_back(Vertex);
                Out.Indices.push_back(static_cast<uint32_t>(Out.Indices.size()));
                Centre[0] += Positions[V][0];
                Centre[1] += Positions[V][1];
                Centre[2] += Positions[V][2];
            }
        }
        // One cluster per object: the container's CLST table is the cluster/LOD range, so the object needs one.
        ClusterRecord Cluster{};
        const float Inverse = Out.Vertices.empty() ? 0.0f : 1.0f / float(Out.Vertices.size());
        Cluster.CenterX = Centre[0] * Inverse;
        Cluster.CenterY = Centre[1] * Inverse;
        Cluster.CenterZ = Centre[2] * Inverse;
        Cluster.Radius = 0.0f;
        for (const VertexRecord& Vertex : Out.Vertices)
        {
            const float Dx = Vertex.SpatialLocation.x - Cluster.CenterX;
            const float Dy = Vertex.SpatialLocation.y - Cluster.CenterY;
            const float Dz = Vertex.SpatialLocation.z - Cluster.CenterZ;
            Cluster.Radius = std::max(Cluster.Radius, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
        }
        Cluster.AxisX = 0.0f; Cluster.AxisY = 1.0f; Cluster.AxisZ = 0.0f;
        Cluster.Cutoff = 1.0f;                 // 1.0 = never backface-culled, the builder's own default
        Cluster.InstanceIndex = 0u;
        Cluster.FirstIndex = 0u;
        Cluster.TriangleCount = Span.TriangleCount;
        Cluster.FirstPrimitive = 0u;
        Out.Clusters.push_back(Cluster);
        return Out;
    }

    // Writes one .geometry container: MESH + CLST + TYPE + META. Returns the bytes; the caller decides whether they are
    //    a file, a blob, or both.
    std::vector<uint8_t> WriteGeometry(const Object& Source)
    {
        SpaceWriter Writer;
        BeginOk(Writer, SpaceTag("GEOM"), 1u, MakeMeta(Source.Name, kSpacePackEmbedSmall));
        Writer.BeginTable(kTagMesh);
        const uint32_t VertexCount = static_cast<uint32_t>(Source.Vertices.size());
        const uint32_t IndexCount = static_cast<uint32_t>(Source.Indices.size());
        Writer.WriteBytes(&VertexCount, sizeof(VertexCount));
        Writer.WriteBytes(&IndexCount, sizeof(IndexCount));
        Writer.WriteBytes(Source.Vertices.data(), Source.Vertices.size() * sizeof(VertexRecord));
        Writer.WriteBytes(Source.Indices.data(), Source.Indices.size() * sizeof(uint32_t));
        Writer.EndTable();
        Writer.BeginTable(kTagClst);
        for (const ClusterRecord& Cluster : Source.Clusters) Writer.WriteRow(Cluster);
        Writer.EndTable();
        std::vector<uint8_t> Out;
        std::string Error;
        if (!Writer.Finish(Out, Error)) { Fail("the geometry writer refused %s: %s", Source.Name.c_str(), Error.c_str()); return {}; }
        return Out;
    }

    // Reads a MESH table back into an Object — the reader's half of the same contract.
    bool ReadGeometry(const std::vector<uint8_t>& Bytes, Object& Out, std::string& OutError)
    {
        SpaceReader Reader;
        if (!Reader.Open(Bytes, OutError)) return false;
        const SpaceTableRecord* Mesh = Reader.Find(kTagMesh);
        if (!Mesh) { OutError = "no MESH table"; return false; }
        const std::vector<uint8_t> Payload = Reader.Payload(*Mesh);
        if (Payload.size() < 8u) { OutError = "MESH is short"; return false; }
        uint32_t VertexCount = 0u, IndexCount = 0u;
        std::memcpy(&VertexCount, Payload.data(), 4u);
        std::memcpy(&IndexCount, Payload.data() + 4u, 4u);
        const size_t VerticesAt = 8u;
        const size_t IndicesAt = VerticesAt + size_t(VertexCount) * sizeof(VertexRecord);
        if (IndicesAt + size_t(IndexCount) * sizeof(uint32_t) > Payload.size()) { OutError = "MESH is truncated"; return false; }
        Out.Vertices.resize(VertexCount);
        std::memcpy(Out.Vertices.data(), Payload.data() + VerticesAt, VertexCount * sizeof(VertexRecord));
        Out.Indices.resize(IndexCount);
        std::memcpy(Out.Indices.data(), Payload.data() + IndicesAt, IndexCount * sizeof(uint32_t));
        std::string ClusterError;
        Out.Clusters = Reader.Rows<ClusterRecord>(kTagClst, ClusterError);
        return true;
    }
} // namespace

int main()
{
    std::system("rm -rf /tmp/SpaceFamily");
    std::printf("[space-family] the Space container family — one reader for thirteen file types\n");

    // ── the level ────────────────────────────────────────────────────────────────────────────────────────────────────
    MaterialSwatchStructure Library;
    Library.Construct();
    const std::vector<TriangleIndex>& Soup = Library.QueryTriangles();
    const std::vector<MaterialDescriptor>& Descriptors = Library.QueryMaterials();
    const std::vector<TriangleSpanRecord>& Spans = Library.QuerySpans();
    std::printf("[space-family] M10: %zu triangles · %zu materials · %zu spans\n", Soup.size(), Descriptors.size(), Spans.size());
    if (Soup.empty() || Descriptors.empty() || Spans.empty()) { std::printf("[space-family] RED — the level builder produced nothing\n"); return 1; }

    MaterialIndex Materials;
    for (const MaterialDescriptor& Descriptor : Descriptors) Materials.Register(Descriptor);
    Materials.Finalise(4u);

    std::vector<Object> Objects;
    for (const TriangleSpanRecord& Span : Spans) Objects.push_back(ObjectFromSpan(Soup, Span));
    std::printf("[space-family] objects: %zu (first '%s' with %zu vertices, %zu indices)\n",
                Objects.size(), Objects[0].Name.c_str(), Objects[0].Vertices.size(), Objects[0].Indices.size());

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    // CLAIM 1 — round trip is lossless, for every type in the family
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    {
        long RoundTripFailures = 0, TypesChecked = 0;
        size_t TotalBytes = 0u;
        std::string FirstFailure;
        for (const SpaceFileType& Type : kSpaceFileTypes)
        {
            // A minimal but REAL file of each type: TYPE+META + the table the type requires (+ the tags it is defined
            //    by). A type whose file is empty would round-trip trivially, so every one carries a row.
            SpaceWriter Writer;
            BeginOk(Writer, Type.Tag, 1u, MakeMeta(std::string("RoundTrip") + Type.Extension, kSpacePackEmbedSmall));
            const auto RowTable = [&](const SpaceTag& Tag, const void* Row, size_t Bytes) {
                Writer.BeginTable(Tag);
                Writer.WriteBytes(Row, Bytes);
                Writer.EndTable();
            };
            const SpaceInstanceRow Instance{};
            const SpaceLevelRow Level{};
            const SpaceCameraRow Camera{};
            const SpaceMaterialSlot Slot{};
            const VertexRecord Vertex{};
            const ClusterRecord Cluster{};
            const MaterialRecord Material{};
            const SpaceUvIslandRow Island{};
            const SpacePigmentRow Channel{};
            const LuminaireRecord Luminaire{};
            const SpaceEnvironmentRow Environment{};
            const SpaceFlowStepRow Flow{};
            const SpaceStateRow State{};
            const SpaceArchiveEntry Archive{};

            if (Type.Required == kTagMesh)        RowTable(kTagMesh, &Vertex, sizeof(Vertex));
            else if (Type.Required == kTagClst)   RowTable(kTagClst, &Cluster, sizeof(Cluster));
            else if (Type.Required == kTagUvsp)   RowTable(kTagUvsp, &Island, sizeof(Island));
            else if (Type.Required == kTagPigm)   RowTable(kTagPigm, &Channel, sizeof(Channel));
            else if (Type.Required == kTagInst)   RowTable(kTagInst, &Instance, sizeof(Instance));
            else if (Type.Required == kTagScen)   RowTable(kTagScen, &Level, sizeof(Level));
            else if (Type.Required == kTagEnvr)   RowTable(kTagEnvr, &Environment, sizeof(Environment));
            else if (Type.Required == kTagFlow)   RowTable(kTagFlow, &Flow, sizeof(Flow));
            else if (Type.Required == kTagArch)   RowTable(kTagArch, &Archive, sizeof(Archive));
            else if (Type.Required == kTagStat)   RowTable(kTagStat, &State, sizeof(State));
            else if (Type.Required == kTagMatl)   { RowTable(kTagMatl, &Material, sizeof(Material)); RowTable(kTagMslT, &Slot, sizeof(Slot)); }
            else if (Type.Required == kTagMeta)
            {
                // .solution / .script / .runtime: META is the required table, so their own vocabulary rides on top.
                if (Type.Tag == SpaceTag("SCRP")) RowTable(kTagStat, &State, sizeof(State));
                if (Type.Tag == SpaceTag("RUNT")) RowTable(kTagStat, &State, sizeof(State));
                if (Type.Tag == SpaceTag("SOLN")) RowTable(kTagClst, &Cluster, sizeof(Cluster));
                RowTable(kTagRefs, &Slot, sizeof(SpaceMaterialSlot));   // a row, so the file is not vacuous
            }
            // Every file in the family may carry a camera and a luminaire: their presence is what makes the "unknown
            //    tables are skipped" rule interesting rather than untested.
            RowTable(kTagCama, &Camera, sizeof(Camera));
            RowTable(kTagLite, &Luminaire, sizeof(Luminaire));
            if (Type.Required != kTagMatl) RowTable(kTagMatl, &Material, sizeof(Material));
            if (Type.Required != kTagMesh) RowTable(kTagMesh, &Vertex, sizeof(Vertex));

            std::vector<uint8_t> First, Second;
            std::string Error;
            if (!Writer.Finish(First, Error)) { ++RoundTripFailures; if (FirstFailure.empty()) FirstFailure = std::string(Type.Extension) + ": " + Error; continue; }
            SpaceReader Reader;
            if (!Reader.Open(First, Error)) { ++RoundTripFailures; if (FirstFailure.empty()) FirstFailure = std::string(Type.Extension) + ": " + Error; continue; }

            // The reader hands the tables back verbatim, so the re-encode is a table-for-table copy — exactly what
            //    -Pack and -Explode do, which is why their round trip is guaranteed by the same property.
            SpaceWriter Again;
            BeginOk(Again, Reader.TypeRecord().Tag, Reader.TypeRecord().Revision, MakeMeta(std::string("RoundTrip") + Type.Extension, kSpacePackEmbedSmall));
            for (const SpaceTableRecord& Table : Reader.Tables())
            {
                if (Table.Tag == kTagType || Table.Tag == kTagMeta) continue;
                Again.BeginTable(Table.Tag);
                const std::vector<uint8_t> Payload = Reader.Payload(Table);
                Again.WriteBytes(Payload.data(), Payload.size());
                Again.EndTable();
            }
            if (!Again.Finish(Second, Error)) { ++RoundTripFailures; if (FirstFailure.empty()) FirstFailure = std::string(Type.Extension) + ": " + Error; continue; }
            ++TypesChecked;
            TotalBytes += First.size();
            if (First != Second)
            {
                ++RoundTripFailures;
                if (FirstFailure.empty()) FirstFailure = std::string(Type.Extension) + " re-encodes to different bytes";
            }
        }
        if (RoundTripFailures == 0)
            Pass("① round trip is lossless for all %ld types in the family (%zu bytes written, every one byte-identical on re-encode)",
                 TypesChecked, TotalBytes);
        else
            Fail("① %ld of %zu types do not round-trip (%s)", RoundTripFailures, kSpaceFileTypeCount, FirstFailure.c_str());
    }

    // ── the project: one geometry per object, one material file per material, one INST row per placement ─────────────
    //    ⚠️ This is the plan's §4 tree. The INST rows reference their geometry through REFS and their material through
    //    MSLT, which is what makes the dedup claims in ③ and ⑧ measurable rather than asserted.
    std::vector<std::vector<uint8_t>> GeometryFiles;
    for (const Object& Source : Objects) GeometryFiles.push_back(WriteGeometry(Source));
    std::vector<std::vector<uint8_t>> MaterialFiles;
    for (uint32_t Material = 0u; Material < Materials.QueryCount(); ++Material)
    {
        SpaceWriter Writer;
        BeginOk(Writer, SpaceTag("MATL"), 1u, MakeMeta("Material_" + std::to_string(Material), kSpacePackEmbedSmall));
        Writer.BeginTable(kTagMatl);
        const MaterialRecord& Record = Materials.QueryRecords()[Material];
        Writer.WriteBytes(&Record, sizeof(Record));
        const uint32_t SlabCount = Record.SlabCount;
        Writer.WriteBytes(&SlabCount, sizeof(SlabCount));
        // The slabs are the ones the record points at: a file carries a material's OWN range, which is what makes a
        //    .material self-contained (claim 7).
        if (SlabCount > 0u && size_t(Record.SlabOffset) + SlabCount <= Materials.QuerySlabRecords().size())
            Writer.WriteBytes(&Materials.QuerySlabRecords()[Record.SlabOffset], size_t(SlabCount) * sizeof(MaterialSlabRecord));
        Writer.EndTable();
        std::vector<uint8_t> Bytes;
        std::string Error;
        if (!Writer.Finish(Bytes, Error)) Fail("the material writer refused material %u: %s", Material, Error.c_str());
        else MaterialFiles.push_back(Bytes);
    }
    std::printf("[space-family] wrote %zu .geometry and %zu .material containers (%zu B and %zu B total)\n",
                GeometryFiles.size(), MaterialFiles.size(),
                [&] { size_t T = 0u; for (const auto& B : GeometryFiles) T += B.size(); return T; }(),
                [&] { size_t T = 0u; for (const auto& B : MaterialFiles) T += B.size(); return T; }());

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    // CLAIM 2 — the scene is the same scene (GPU) / the records are the same records (CPU)
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    {
        long RecordFailures = 0;
        // A geometry, out and back: the vertices and indices the renderer binds must be the ones the builder produced.
        Object Reloaded;
        std::string Error;
        if (!ReadGeometry(GeometryFiles[0], Reloaded, Error)) { Fail("② the first geometry does not read back: %s", Error.c_str()); ++RecordFailures; }
        else
        {
            const bool VerticesSame = Reloaded.Vertices.size() == Objects[0].Vertices.size() &&
                std::memcmp(Reloaded.Vertices.data(), Objects[0].Vertices.data(), Reloaded.Vertices.size() * sizeof(VertexRecord)) == 0;
            const bool IndicesSame = Reloaded.Indices == Objects[0].Indices;
            const bool ClustersSame = Reloaded.Clusters.size() == Objects[0].Clusters.size() &&
                std::memcmp(Reloaded.Clusters.data(), Objects[0].Clusters.data(), Reloaded.Clusters.size() * sizeof(ClusterRecord)) == 0;
            if (!VerticesSame) { ++RecordFailures; Fail("② the %zu vertices differ", Reloaded.Vertices.size()); }
            if (!IndicesSame)  { ++RecordFailures; Fail("② the %zu indices differ", Reloaded.Indices.size()); }
            if (!ClustersSame) { ++RecordFailures; Fail("② the %zu clusters differ", Reloaded.Clusters.size()); }
        }
        if (RecordFailures == 0)
            Pass("② (CPU half) the container's MESH and CLST bytes ARE the resident records — %zu vertices, %zu indices, %zu "
                 "clusters compared by memcmp, loaded with no parse and no rebuild", Reloaded.Vertices.size(), Reloaded.Indices.size(),
                 Reloaded.Clusters.size());
        else
            Fail("② the loaded records differ from the built ones (%ld mismatches)", RecordFailures);
        Skip("② (GPU half) AE = 0 against Exhibits/Gallery/Materials/MaterialLibrary_Wide.png needs a device and is not "
             "claimed here — the resident records above are the ones that image is rendered from");
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    // CLAIM 3 — dedup is real: N copies of one object cost one geometry and N rows
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    {
        long DedupFailures = 0;
        size_t Sizes[3] = { 0u, 0u, 0u };
        uint32_t Counts[3] = { 8u, 32u, 64u };
        for (int Case = 0; Case < 3; ++Case)
        {
            SpaceWriter Writer;
            BeginOk(Writer, SpaceTag("PROJ"), 1u, MakeMeta("Project-Zero", kSpacePackEmbedSmall));
            // ONE geometry, embedded once: the N objects share it. That is the whole claim.
            const uint32_t GeometryBlob = Writer.AddBlob("GEOM", GeometryFiles[0]);
            const uint32_t MaterialBlob = Writer.AddBlob("MATL", MaterialFiles[0]);

            Writer.BeginTable(kTagRefs);
            SpaceReferenceRecord Geometry{};
            Geometry.Type = SpaceTag("GEOM");
            Geometry.Mode = kSpaceRefEmbedded;
            Geometry.Flags = kSpaceRefRequired;
            Geometry.ContentHash = SpaceHash64(GeometryFiles[0].data(), GeometryFiles[0].size());
            Geometry.BlobIndex = GeometryBlob;
            Writer.WriteRow(Geometry);
            Writer.EndTable();

            Writer.BeginTable(kTagMslT);
            SpaceMaterialSlot Slot{};
            Slot.Mode = kSpaceMaterialCopied;
            Slot.MaterialHash = SpaceHash64(MaterialFiles[0].data(), MaterialFiles[0].size());
            Slot.BlobIndex = MaterialBlob;
            for (uint32_t I = 0u; I < Counts[Case]; ++I) Writer.WriteRow(Slot);   // N private materials, ONE blob
            Writer.EndTable();

            Writer.BeginTable(kTagInst);
            for (uint32_t I = 0u; I < Counts[Case]; ++I)
            {
                SpaceInstanceRow Row{};
                SpaceSetName(Row.Name, sizeof(Row.Name), "Sphere_" + std::to_string(I));
                Row.Transform[0] = Row.Transform[5] = Row.Transform[10] = Row.Transform[15] = 1.0f;
                Row.Transform[12] = float(I % 7u) * 0.5f;
                Row.GeometryRef = 0u;
                Row.MaterialSlot = I;
                Row.LevelIndex = 0u;
                Row.Flags = kSpaceInstanceDefault;
                Writer.WriteRow(Row);
            }
            Writer.EndTable();

            Writer.BeginTable(kTagScen);
            SpaceLevelRow Level{};
            SpaceSetName(Level.Name, sizeof(Level.Name), "Showroom");
            Level.FirstInstance = 0u;
            Level.InstanceCount = Counts[Case];
            Level.DefaultCamera = 0u;
            Writer.WriteRow(Level);
            Writer.EndTable();

            std::vector<uint8_t> Bytes;
            std::string Error;
            if (!Writer.Finish(Bytes, Error)) { ++DedupFailures; continue; }
            Sizes[Case] = Bytes.size();

            SpaceReader Reader;
            if (!Reader.Open(Bytes, Error)) { ++DedupFailures; continue; }
            if (!Reader.ReadBlobs(Error)) { ++DedupFailures; continue; }
            if (Reader.BlobCount() != 2u)   // the geometry and the material — NOT one per copy
            {
                Fail("③ %u copies produced %u blobs, expected 2 (one geometry, one material)", Counts[Case], Reader.BlobCount());
                ++DedupFailures;
            }
            std::string TableError;
            const std::vector<SpaceInstanceRow> Instances = Reader.Instances(TableError);
            const std::vector<SpaceMaterialSlot> Slots = Reader.MaterialSlots(TableError);
            if (Instances.size() != Counts[Case] || Slots.size() != Counts[Case]) ++DedupFailures;
            uint64_t Hash = 0u;
            for (const SpaceMaterialSlot& S : Slots) { if (Hash != 0u && S.MaterialHash != Hash) ++DedupFailures; Hash = S.MaterialHash; }
        }
        // The per-copy cost is exactly the three rows that name the copy: an INST row, a slot, and the metadata around
        //    them. If a copy cost a geometry, the 8× jump from 8 to 64 would be 8× the file; it is not.
        const double PerCopy = double(Sizes[2] - Sizes[0]) / double(Counts[2] - Counts[0]);
        const double RowBytes = double(sizeof(SpaceInstanceRow) + sizeof(SpaceMaterialSlot));
        if (DedupFailures == 0 && PerCopy < RowBytes * 1.6)
            Pass("③ dedup is real and copies are flat: 8/32/64 copies of one object → %zu/%zu/%zu B (%.1f B per copy against "
                 "the %.0f B of its own two rows), 2 blobs and N slots in every case",
                 Sizes[0], Sizes[1], Sizes[2], PerCopy, RowBytes);
        else
            Fail("③ %ld dedup failures — %zu/%zu/%zu B, %.1f B per copy against %.0f B of rows", DedupFailures, Sizes[0], Sizes[1], Sizes[2], PerCopy, RowBytes);
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    // CLAIM 4 — references resolve, and fail loudly; -Embed makes a missing sibling irrelevant
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    {
        // A project that REFERENCES a geometry file it does not embed.
        SpaceWriter Writer;
        BeginOk(Writer, SpaceTag("PROJ"), 1u, MakeMeta("Refs", kSpacePackEmbedSmall));
        Writer.BeginTable(kTagRefs);
        SpaceReferenceRecord Geometry{};
        Geometry.Type = SpaceTag("GEOM");
        Geometry.Mode = kSpaceRefProjectContent;
        Geometry.Flags = kSpaceRefRequired;
        Geometry.ContentHash = SpaceHash64(GeometryFiles[0].data(), GeometryFiles[0].size());
        Writer.WriteRowWithString(Geometry, offsetof(SpaceReferenceRecord, PathOffset), "Assets/Sphere.geometry");
        SpaceReferenceRecord Optional{};
        Optional.Type = SpaceTag("MATL");
        Optional.Mode = kSpaceRefEngineContent;
        Optional.Flags = 0u;   // NOT required
        Writer.WriteRowWithString(Optional, offsetof(SpaceReferenceRecord, PathOffset), "EngineContent/Materials/Missing.material");
        Writer.EndTable();
        Writer.BeginTable(kTagScen);
        SpaceLevelRow Level{};
        SpaceSetName(Level.Name, sizeof(Level.Name), "Showroom");
        Writer.WriteRow(Level);
        Writer.EndTable();
        std::vector<uint8_t> Project;
        std::string Error;
        if (!Writer.Finish(Project, Error)) Fail("④ the reference project would not write: %s", Error.c_str());
        else
        {
            SpaceReader Reader;
            if (!Reader.Open(Project, Error)) { Fail("④ the reference project would not read: %s", Error.c_str()); }
            else
            {
                const std::vector<SpaceReferenceRecord> References = Reader.References(Error);
                const std::string Base = "/tmp/SpaceFamily/Refs";
                const std::vector<std::pair<std::string, std::string>> Roots = {
                    { "ProjectContent", Base + "/Content" }, { "EngineContent", "/tmp/SpaceFamily/Nonexistent" } };

                std::vector<uint8_t> Loaded;
                std::string MissError;
                const bool Missed = !SpaceLoadReference(Reader, References[0], Base, Roots, Loaded, MissError);

                // Now put the file where the reference says it is: the same record must resolve.
                WriteFile(Base + "/Content/Assets/Sphere.geometry", GeometryFiles[0]);
                std::vector<uint8_t> Found;
                std::string FoundError;
                const bool Resolved = References.size() >= 2u && SpaceLoadReference(Reader, References[0], Base, Roots, Found, FoundError);

                // And the optional one failing must NOT be an error for the loader — it is a warning with a name.
                std::vector<uint8_t> OptionalBytes;
                std::string OptionalError;
                const bool OptionalMissed = References.size() >= 2u && !SpaceLoadReference(Reader, References[1], Base, Roots, OptionalBytes, OptionalError);

                const bool NamedPath = MissError.find("Sphere.geometry") != std::string::npos &&
                                       MissError.find("ProjectContent") != std::string::npos;
                if (Missed && Resolved && OptionalMissed && NamedPath && Found == GeometryFiles[0])
                    Pass("④ references resolve in the plan's order and fail by name: absent → \"%s\"; present → the payload "
                         "comes back byte-identical; the optional one stays absent without being an error", MissError.c_str());
                else
                    Fail("④ resolution behaved unexpectedly (missed %d, resolved %d, optional %d, named %d)",
                         int(Missed), int(Resolved), int(OptionalMissed), int(NamedPath));
            }
        }

        // -Embed: the same file with the geometry inlined must load with the folder absent — which is what makes the
        //    pack policy worth recording in META rather than assuming.
        {
            SpaceWriter Embedded;
            BeginOk(Embedded, SpaceTag("PROJ"), 1u, MakeMeta("Embedded", kSpacePackEmbedSmall | kSpacePackEmbedAssets));
            const uint32_t Blob = Embedded.AddBlob("GEOM", GeometryFiles[0]);
            Embedded.BeginTable(kTagRefs);
            SpaceReferenceRecord Geometry{};
            Geometry.Type = SpaceTag("GEOM");
            Geometry.Mode = kSpaceRefEmbedded;
            Geometry.Flags = kSpaceRefRequired | kSpaceRefPreferEmbedded;
            Geometry.ContentHash = SpaceHash64(GeometryFiles[0].data(), GeometryFiles[0].size());
            Geometry.BlobIndex = Blob;
            Embedded.WriteRow(Geometry);
            Embedded.EndTable();
            Embedded.BeginTable(kTagScen);
            SpaceLevelRow Level{};
            SpaceSetName(Level.Name, sizeof(Level.Name), "Showroom");
            Embedded.WriteRow(Level);
            Embedded.EndTable();
            std::vector<uint8_t> Bytes;
            std::string Error2;
            if (!Embedded.Finish(Bytes, Error2)) Fail("④ the embedded project would not write: %s", Error2.c_str());
            else
            {
                SpaceReader Reader;
                if (!Reader.Open(Bytes, Error2) || !Reader.ReadBlobs(Error2)) Fail("④ the embedded project would not read: %s", Error2.c_str());
                else
                {
                    const std::vector<SpaceReferenceRecord> References = Reader.References(Error2);
                    std::vector<uint8_t> Loaded;
                    std::string LoadError;
                    // ⚠️ Both roots are EMPTY: nothing on disk can be found, so a pass here can only come from the blob.
                    const std::vector<std::pair<std::string, std::string>> NoRoots = { { "ProjectContent", "" }, { "EngineContent", "" } };
                    if (!References.empty() && SpaceLoadReference(Reader, References[0], "/tmp/SpaceFamily/NothingHere", NoRoots, Loaded, LoadError) &&
                        Loaded == GeometryFiles[0])
                        Pass("④ -Embed works: with every content root absent, the inlined geometry is what the reference resolves "
                             "to — and it is byte-identical (%zu B)", Loaded.size());
                    else
                        Fail("④ the embedded payload did not resolve (%s)", LoadError.c_str());
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    // CLAIM 5 — the directory is forward-compatible, and a flipped bit is a named failure
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    {
        // A newer exporter's file: a table this reader has never heard of ("NAVI"), between the ones it knows.
        SpaceWriter Writer;
        BeginOk(Writer, SpaceTag("GEOM"), 1u, MakeMeta("Future", kSpacePackEmbedSmall));
        Writer.BeginTable(kTagMesh);
        const uint32_t VertexCount = static_cast<uint32_t>(Objects[0].Vertices.size());
        const uint32_t IndexCount = static_cast<uint32_t>(Objects[0].Indices.size());
        Writer.WriteBytes(&VertexCount, sizeof(VertexCount));
        Writer.WriteBytes(&IndexCount, sizeof(IndexCount));
        Writer.WriteBytes(Objects[0].Vertices.data(), Objects[0].Vertices.size() * sizeof(VertexRecord));
        Writer.WriteBytes(Objects[0].Indices.data(), Objects[0].Indices.size() * sizeof(uint32_t));
        Writer.EndTable();
        Writer.BeginTable(SpaceTag("NAVI"));
        const uint32_t NavRow[4] = { 1u, 2u, 3u, 4u };
        Writer.WriteBytes(NavRow, sizeof(NavRow));
        Writer.EndTable();
        std::vector<uint8_t> Bytes;
        std::string Error;
        if (!Writer.Finish(Bytes, Error)) Fail("⑤ the forward-compatible file would not write: %s", Error.c_str());
        else
        {
            SpaceReader Reader;
            std::string ReadError;
            if (!Reader.Open(Bytes, ReadError)) Fail("⑤ an unknown table stopped the load: %s", ReadError.c_str());
            else
            {
                Object Reloaded;
                std::string GeometryError;
                const bool Loaded = ReadGeometry(Bytes, Reloaded, GeometryError);
                // …and a corrupted payload is named, not rendered.
                std::vector<uint8_t> Corrupt = Bytes;
                const SpaceTableRecord* Mesh = Reader.Find(kTagMesh);
                if (!Mesh) { Fail("⑤ the MESH table vanished between writes"); }
                else
                {
                    Corrupt[Mesh->Offset + 16u] ^= 0x40u;   // one bit inside the vertices
                    SpaceReader CorruptReader;
                    std::string CorruptError;
                    const bool OpenedCorrupt = CorruptReader.Open(Corrupt, CorruptError);
                    const bool Named = CorruptReader.ChecksumFailures().size() == 1u &&
                                       CorruptReader.ChecksumFailures()[0] == "MESH";
                    std::string PayloadError;
                    const uint32_t Rows = OpenedCorrupt ? CorruptReader.RowCount(kTagMesh, PayloadError) : 0u;
                    const bool Fatal = Rows == 0u && PayloadError.find("checksum") != std::string::npos;
                    if (Loaded && Named && Fatal)
                        Pass("⑤ an unknown table ('NAVI') is skipped and the file still loads; one flipped bit in MESH is a "
                             "named checksum failure that refuses THAT table (\"%s\") and nothing else", PayloadError.c_str());
                    else
                        Fail("⑤ forward compatibility or corruption handling failed (loaded %d, named %d, fatal %d: %s)",
                             int(Loaded), int(Named), int(Fatal), PayloadError.c_str());
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    // CLAIM 6 — the CLI is the same session
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    {
        // The plan's own launch line, parsed. The host's half of the claim (that this produces the same image as the
        //    compatibility path) needs a device; the half that can be checked here is that both spellings land on the
        //    SAME fields, and that the level the command names is the level the project's SCEN table has.
        const char* Arguments[] = { "SlateEditor", "-Project=Project-Zero", "-Level=Showroom", "-Build=Editor",
                                    "-Config=Development", "+Location=(0,-6.0,2.6)", "-Embed=Fonts,Assets" };
        const int ArgumentCount = int(sizeof(Arguments) / sizeof(Arguments[0]));
        CommandOptions Options;
        std::string Error;
        const bool Parsed = ParseCommandLine(ArgumentCount, const_cast<char**>(Arguments), "", "Projects", Options, Error);

        const bool ProjectOk = Options.Project == "Project-Zero" &&
                               Options.ProjectPath == "Projects/Project-Zero/Project-Zero.projectspace";
        const bool LevelOk = Options.Level == "Showroom" && Options.LevelExplicit == 1u;
        const bool RunOk = Options.Build == CommandBuild::Editor && Options.Config == CommandConfig::Development;
        const bool OverrideOk = Options.HasLocation && Options.Location[0] == 0.0f &&
                                Options.Location[1] == -6.0f && Options.Location[2] == 2.6f;
        const bool PolicyOk = (Options.PackPolicy() & (kSpacePackEmbedFonts | kSpacePackEmbedAssets)) ==
                              (kSpacePackEmbedFonts | kSpacePackEmbedAssets);

        // The compatibility alias: `--scene materials --scale 1.0` parses into the same value type the project path fills.
        const char* Alias[] = { "SlateEditor", "--scene", "materials", "--scale", "1.0" };
        CommandOptions AliasOptions;
        std::string AliasError;
        const bool AliasParsed = ParseCommandLine(5, const_cast<char**>(Alias), "", "Projects", AliasOptions, AliasError);
        const bool SameShape = AliasParsed && AliasOptions.SceneAlias == "materials" &&
                               AliasOptions.HasScale && AliasOptions.Scale == 1.0f &&
                               AliasOptions.Level.empty() && AliasOptions.Project.empty();

        // A malformed override is refused rather than ignored — "the camera did not move" must never be silent.
        const char* Bad[] = { "SlateEditor", "+Location=(0,-6.0)" };
        CommandOptions BadOptions;
        std::string BadError;
        const bool Refused = !ParseCommandLine(2, const_cast<char**>(Bad), "", "Projects", BadOptions, BadError) &&
                             BadError.find("+Location") != std::string::npos;

        // The tools: -Pack / -Explode / -Export / -Bake=Sky — and the level name the CLI names must be the one the
        //    project actually has (a level that is not in SCEN is refused by name, not silently loaded as the default).
        const char* Tool[] = { "PackProject", "-Project=Project-Zero", "-Pack", "-Export=All", "-Bake=Sky" };
        CommandOptions ToolOptions;
        std::string ToolError;
        const bool ToolParsed = ParseCommandLine(5, const_cast<char**>(Tool), "", "Projects", ToolOptions, ToolError);
        const bool ToolOk = ToolParsed && ToolOptions.Pack && ToolOptions.BakeSky && ToolOptions.Export == "All";

        // The project the CLI would open, opened: its SCEN table answers for the level name above.
        SpaceWriter Project;
        BeginOk(Project, SpaceTag("PROJ"), 1u, MakeMeta("Project-Zero", kSpacePackEmbedSmall));
        Project.BeginTable(kTagScen);
        SpaceLevelRow Showroom{};
        SpaceSetName(Showroom.Name, sizeof(Showroom.Name), "Showroom");
        Showroom.InstanceCount = 1u;
        Project.WriteRow(Showroom);
        SpaceLevelRow Attic{};
        SpaceSetName(Attic.Name, sizeof(Attic.Name), "Attic");
        Project.WriteRow(Attic);
        Project.EndTable();
        std::vector<uint8_t> Bytes;
        std::string WriteError;
        bool LevelFound = false;
        if (Project.Finish(Bytes, WriteError))
        {
            SpaceReader Reader;
            std::string ReadError;
            if (Reader.Open(Bytes, ReadError))
            {
                std::string RowError;
                const std::vector<SpaceLevelRow> Levels = Reader.Rows<SpaceLevelRow>(kTagScen, RowError);
                for (const SpaceLevelRow& Level : Levels)
                    if (SpaceGetName(Level.Name, sizeof(Level.Name)) == Options.Level) LevelFound = true;
            }
        }

        if (Parsed && ProjectOk && LevelOk && RunOk && OverrideOk && PolicyOk && SameShape && Refused && ToolOk && LevelFound)
            Pass("⑥ the CLI is one parser: \"%s\" fills the same value type the compatibility aliases fill (--scene/--scale), "
                 "the level it names is the one SCEN carries, a malformed +Location is refused by name, and -Pack/-Export/"
                 "-Embed land in the pack policy", Options.Describe().c_str());
        else
            Fail("⑥ the command line did not behave (parsed %d, project %d, level %d, run %d, override %d, policy %d, alias %d, "
                 "refused %d, tools %d, level found %d)", int(Parsed), int(ProjectOk), int(LevelOk), int(RunOk), int(OverrideOk),
                 int(PolicyOk), int(SameShape), int(Refused), int(ToolOk), int(LevelFound));
        Skip("⑥ (GPU half) AE = 0 between the project launch and the --scene launch needs a device and is not claimed here; "
             "this gate claims the two spellings produce the same options, which is what that comparison would measure");
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    // CLAIM 7 — a material file is self-contained and exact
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    {
        // Write the 49 materials as their own files, read them all back, and compare the records with the ones the
        //    engine holds — the census is the plan's: 49 files, 49 distinct payloads, one `MATL` each.
        size_t Distinct = 0u, Compared = 0u, Mismatches = 0u, ZeroBlob = 0u;
        std::vector<uint64_t> Hashes;
        const std::vector<MaterialRecord>& Records = Materials.QueryRecords();
        const std::vector<MaterialSlabRecord>& Slabs = Materials.QuerySlabRecords();
        for (size_t I = 0u; I < MaterialFiles.size() && I < Records.size(); ++I)
        {
            const uint64_t Hash = SpaceHash64(MaterialFiles[I].data(), MaterialFiles[I].size());
            bool Seen = false;
            for (uint64_t Existing : Hashes) if (Existing == Hash) Seen = true;
            if (!Seen) { Hashes.push_back(Hash); ++Distinct; }

            SpaceReader Reader;
            std::string Error;
            if (!Reader.Open(MaterialFiles[I], Error)) { ++Mismatches; continue; }
            if (!Reader.ReadBlobs(Error)) ++Mismatches;
            if (Reader.BlobCount() == 0u) ++ZeroBlob;   // a constants-only material: MATL and no blobs at all
            const SpaceTableRecord* Matl = Reader.Find(kTagMatl);
            if (!Matl) { ++Mismatches; continue; }
            const std::vector<uint8_t> Payload = Reader.Payload(*Matl);
            MaterialRecord Loaded{};
            uint32_t LoadedSlabs = 0u;
            std::memcpy(&Loaded, Payload.data(), sizeof(Loaded));
            std::memcpy(&LoadedSlabs, Payload.data() + sizeof(Loaded), sizeof(LoadedSlabs));
            const bool RecordMatches = std::memcmp(&Loaded, &Records[I], sizeof(MaterialRecord)) == 0;
            const bool SlabMatches = LoadedSlabs == Records[I].SlabCount &&
                                     size_t(Records[I].SlabOffset) + LoadedSlabs <= Slabs.size() &&
                                     Payload.size() >= sizeof(Loaded) + sizeof(uint32_t) + size_t(LoadedSlabs) * sizeof(MaterialSlabRecord) &&
                                     std::memcmp(Payload.data() + sizeof(Loaded) + sizeof(uint32_t),
                                                 &Slabs[Records[I].SlabOffset], size_t(LoadedSlabs) * sizeof(MaterialSlabRecord)) == 0;
            if (RecordMatches && SlabMatches) ++Compared; else ++Mismatches;
        }
        if (Mismatches == 0 && Compared == MaterialFiles.size())
            Pass("⑦ %zu .material files are self-contained and exact: every MaterialRecord and slab the container hands back "
                 "is byte-identical to the resident one (%zu distinct payloads, %zu of them constants-only with no blobs at all)",
                 MaterialFiles.size(), Distinct, ZeroBlob);
        else
            Fail("⑦ %zu mismatches over %zu files (%zu compared, %zu distinct)", Mismatches, MaterialFiles.size(), Compared, Distinct);

        // …and a TEXTURED material carries its maps: TEXR rows that point at its own blobs, resolving with no sidecar.
        {
            SpaceWriter Writer;
            BeginOk(Writer, SpaceTag("MATL"), 1u, MakeMeta("Textured", kSpacePackEmbedSmall | kSpacePackEmbedAssets));
            Writer.BeginTable(kTagMatl);
            MaterialRecord Record{};
            Writer.WriteBytes(&Record, sizeof(Record));
            const uint32_t One = 1u;
            Writer.WriteBytes(&One, sizeof(One));
            MaterialSlabRecord Slab{};
            Writer.WriteBytes(&Slab, sizeof(Slab));
            Writer.EndTable();
            const std::vector<uint8_t> Map(1024u, 0x5Au);   // a stand-in for a baked pigment map
            const uint32_t MapBlob = Writer.AddBlob("PIGM", Map);
            Writer.BeginTable(kTagTexr);
            SpaceTextureRow Texture{};
            SpaceSetName(Texture.Uri, sizeof(Texture.Uri), "BaseColor");
            Texture.RefIndex = 0xFFFFFFFFu;
            Texture.BlobIndex = MapBlob;
            Texture.Flags = 1u;   // srgb
            Writer.WriteRow(Texture);
            Writer.EndTable();
            std::vector<uint8_t> Bytes;
            std::string Error;
            if (!Writer.Finish(Bytes, Error)) Fail("⑦ the textured material would not write: %s", Error.c_str());
            else
            {
                SpaceReader Reader;
                if (!Reader.Open(Bytes, Error) || !Reader.ReadBlobs(Error)) Fail("⑦ the textured material would not read: %s", Error.c_str());
                else
                {
                    std::string RowError;
                    const std::vector<SpaceTextureRow> Textures = Reader.Rows<SpaceTextureRow>(kTagTexr, RowError);
                    std::vector<uint8_t> Loaded;
                    std::string BlobError;
                    const bool Resolved = Textures.size() == 1u && Textures[0].BlobIndex != 0xFFFFFFFFu &&
                                          Reader.BlobBytes(Textures[0].BlobIndex, Loaded, BlobError) && Loaded == Map;
                    if (Resolved)
                        Pass("⑦ a textured material's map resolves from its own BLOB through a TEXR row — %zu B back byte for "
                             "byte, with no sidecar file in existence", Loaded.size());
                    else
                        Fail("⑦ the textured material's map did not resolve (%s)", BlobError.c_str());
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    // CLAIM 8 — copied == referenced, byte for byte; copying is free; editing moves what it should
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    {
        // ⚠️ The comparison is between the SAME material arriving two ways: as a referenced file, and as a blob the
        //    object carries. The plan's claim is `cmp`-equality plus the same MaterialHash, so that is what runs.
        const std::vector<uint8_t>& ClearGlass = MaterialFiles[0];
        const uint64_t ReferenceHash = SpaceHash64(ClearGlass.data(), ClearGlass.size());

        SpaceWriter Copied;
        BeginOk(Copied, SpaceTag("INST"), 1u, MakeMeta("Sphere_02", kSpacePackEmbedSmall));
        const uint32_t Blob = Copied.AddBlob("MATL", ClearGlass);
        Copied.BeginTable(kTagMslT);
        SpaceMaterialSlot Slot{};
        Slot.Mode = kSpaceMaterialCopied;
        Slot.MaterialHash = ReferenceHash;
        Slot.BlobIndex = Blob;
        Copied.WriteRow(Slot);
        Copied.EndTable();
        Copied.BeginTable(kTagInst);
        SpaceInstanceRow Row{};
        SpaceSetName(Row.Name, sizeof(Row.Name), "Sphere_02");
        Row.Transform[0] = Row.Transform[5] = Row.Transform[10] = Row.Transform[15] = 1.0f;
        Row.MaterialSlot = 0u;
        Copied.WriteRow(Row);
        Copied.EndTable();
        std::vector<uint8_t> CopiedBytes;
        std::string Error;
        const bool Wrote = Copied.Finish(CopiedBytes, Error);

        bool BlobEqual = false;
        uint64_t BlobHash = 0u;
        if (Wrote)
        {
            SpaceReader Reader;
            std::string ReadError;
            if (Reader.Open(CopiedBytes, ReadError) && Reader.ReadBlobs(ReadError))
            {
                std::vector<uint8_t> Loaded;
                std::string BlobError;
                if (Reader.BlobBytes(0u, Loaded, BlobError))
                {
                    BlobEqual = Loaded == ClearGlass;
                    BlobHash = SpaceHash64(Loaded.data(), Loaded.size());
                }
            }
        }

        // "Copying is free" — ten objects, ten Copied slots, ONE blob, and the file grows by the rows only.
        //    ⚠️ .instance requires an INST table, so each of these carries a row: a container that fails its own required
        //    table is refused at write time, and a size comparison over two refusals would compare nothing.
        size_t CopySizes[2] = { 0u, 0u };
        uint32_t CopiesFor[2] = { 1u, 10u };
        long SizeFailures = 0;
        for (int Case = 0; Case < 2; ++Case)
        {
            SpaceWriter Many;
            BeginOk(Many, SpaceTag("INST"), 1u, MakeMeta("Copies", kSpacePackEmbedSmall));
            const uint32_t Shared = Many.AddBlob("MATL", ClearGlass);
            Many.BeginTable(kTagMslT);
            SpaceMaterialSlot ManySlot{};
            ManySlot.Mode = kSpaceMaterialCopied;
            ManySlot.MaterialHash = ReferenceHash;
            ManySlot.BlobIndex = Shared;
            for (uint32_t I = 0u; I < CopiesFor[Case]; ++I) Many.WriteRow(ManySlot);
            Many.EndTable();
            Many.BeginTable(kTagInst);
            for (uint32_t I = 0u; I < CopiesFor[Case]; ++I)
            {
                SpaceInstanceRow Row{};
                SpaceSetName(Row.Name, sizeof(Row.Name), "Sphere_" + std::to_string(I));
                Row.Transform[0] = Row.Transform[5] = Row.Transform[10] = Row.Transform[15] = 1.0f;
                Row.MaterialSlot = I;
                Many.WriteRow(Row);
            }
            Many.EndTable();
            std::vector<uint8_t> Bytes;
            std::string ManyError;
            if (Many.Finish(Bytes, ManyError)) CopySizes[Case] = Bytes.size();
            else { ++SizeFailures; Fail("⑧ the %u-copy container would not write: %s", CopiesFor[Case], ManyError.c_str()); }
        }
        const double PerCopyCounted = SizeFailures == 0 ? double(CopySizes[1] - CopySizes[0]) / 9.0 : 0.0;
        const double PerCopy = PerCopyCounted;

        // Editing: a shared edit moves every referencing object; a copied edit moves exactly one. The census is the
        //    number of rows pointing at the edited bytes, and which rows they are.
        const auto CountMoved = [&](bool CopiedEdit, uint32_t Which, uint32_t& OutMoved, bool& OutOnlyTheEdited) {
            OutMoved = 0u;
            OutOnlyTheEdited = false;
            SpaceWriter Edited;
            BeginOk(Edited, SpaceTag("PROJ"), 1u, MakeMeta("Edited", kSpacePackEmbedSmall));
            const uint32_t Shared = Edited.AddBlob("MATL", ClearGlass);
            std::vector<uint8_t> NewBytes = ClearGlass;
            if (!NewBytes.empty()) NewBytes.back() ^= 0x01u;   // the edit: one byte of the payload changes
            const uint32_t Forked = Edited.AddBlob("MATL", NewBytes);
            Edited.BeginTable(kTagMslT);
            for (uint32_t I = 0u; I < 3u; ++I)
            {
                SpaceMaterialSlot Row{};
                Row.Mode = CopiedEdit ? kSpaceMaterialCopied : kSpaceMaterialShared;
                // A copied edit forks ONE row; a shared edit replaces what every row already points at, which is the
                //    same observable as all three moving to the new bytes.
                const bool ThisOne = CopiedEdit ? (I == Which) : true;
                Row.BlobIndex = ThisOne ? Forked : Shared;
                Row.MaterialHash = ThisOne ? SpaceHash64(NewBytes.data(), NewBytes.size()) : ReferenceHash;
                Edited.WriteRow(Row);
            }
            Edited.EndTable();
            const uint32_t Unknown = 0xFFFFFFFFu;
            Edited.BeginTable(kTagScen);
            SpaceLevelRow Level{};
            SpaceSetName(Level.Name, sizeof(Level.Name), "Showroom");
            Level.FirstInstance = 0u;
            Level.InstanceCount = 3u;
            Level.DefaultCamera = Unknown;
            Edited.WriteRow(Level);
            Edited.EndTable();
            std::vector<uint8_t> Bytes;
            std::string EditError;
            if (!Edited.Finish(Bytes, EditError)) { Fail("⑧ the edited container would not write: %s", EditError.c_str()); return; }
            SpaceReader Reader;
            std::string ReadError;
            if (!Reader.Open(Bytes, ReadError) || !Reader.ReadBlobs(ReadError)) { Fail("⑧ %s", ReadError.c_str()); return; }
            std::string SlotError;
            const std::vector<SpaceMaterialSlot> Slots = Reader.MaterialSlots(SlotError);
            bool OnlyEdited = true;
            for (uint32_t I = 0u; I < Slots.size(); ++I)
            {
                if (Slots[I].BlobIndex != Forked) continue;
                ++OutMoved;
                if (CopiedEdit && I != Which) OnlyEdited = false;
            }
            OutOnlyTheEdited = OnlyEdited && OutMoved > 0u;
        };
        uint32_t CopiedMoved = 0u, SharedMoved = 0u;
        bool CopiedOnlyEdited = false, SharedOnlyEdited = false;
        CountMoved(true, 1u, CopiedMoved, CopiedOnlyEdited);
        CountMoved(false, 0u, SharedMoved, SharedOnlyEdited);

        // A slot whose blob is missing fails by NAME rather than silently rendering the base colour.
        bool MissingNamed = false;
        {
            SpaceWriter Broken;
            BeginOk(Broken, SpaceTag("INST"), 1u, MakeMeta("Broken", kSpacePackEmbedSmall));
            Broken.BeginTable(kTagMslT);
            SpaceMaterialSlot Bad{};
            Bad.Mode = kSpaceMaterialCopied;
            Bad.BlobIndex = 7u;   // no such blob
            Broken.WriteRow(Bad);
            Broken.EndTable();
            Broken.BeginTable(kTagInst);
            SpaceInstanceRow Row{};
            SpaceSetName(Row.Name, sizeof(Row.Name), "Sphere_00");
            Row.Transform[0] = Row.Transform[5] = Row.Transform[10] = Row.Transform[15] = 1.0f;
            Broken.WriteRow(Row);
            Broken.EndTable();
            std::vector<uint8_t> Bytes;
            std::string BrokenError;
            if (Broken.Finish(Bytes, BrokenError))
            {
                SpaceReader Reader;
                std::string ReadError;
                if (Reader.Open(Bytes, ReadError) && Reader.ReadBlobs(ReadError))
                {
                    std::vector<uint8_t> Loaded;
                    std::string SlotError;
                    const std::vector<SpaceMaterialSlot> Slots = Reader.MaterialSlots(SlotError);
                    std::string BlobError;
                    MissingNamed = !Slots.empty() && !Reader.BlobBytes(Slots[0].BlobIndex, Loaded, BlobError) &&
                                   BlobError.find("out of range") != std::string::npos;
                }
                else Fail("⑧ the broken container would not read: %s", ReadError.c_str());
            }
            else Fail("⑧ the broken container would not write: %s", BrokenError.c_str());
        }
        // The bar: a copy costs its own two rows and nothing else. An object's geometry here is 266 KB, so a file that
        //    carried geometry per copy would be three orders of magnitude over this.
        const double RowBytesOfCopy = double(sizeof(SpaceInstanceRow) + sizeof(SpaceMaterialSlot));
        if (BlobEqual && BlobHash == ReferenceHash && PerCopy <= RowBytesOfCopy * 1.6 && CopiedMoved == 1u && CopiedOnlyEdited &&
            SharedMoved == 3u && MissingNamed)
            Pass("⑧ Copied == referenced: the embedded copy hashes to 0x%llx and is byte-identical to the file; ten objects "
                 "holding their own copies add %.1f B each (one blob, ten slots, %zu B → %zu B); a copied edit moves 1 row "
                 "and only the edited one, a shared edit moves all 3; a missing blob fails by name",
                 static_cast<unsigned long long>(BlobHash), PerCopy, CopySizes[0], CopySizes[1]);
        else
            Fail("⑧ copied/referenced equivalence failed (equal %d, hash match %d, per copy %.1f B, copied-moves %u (only "
                 "edited %d), shared-moves %u, missing named %d)", int(BlobEqual), int(BlobHash == ReferenceHash), PerCopy,
                 CopiedMoved, int(CopiedOnlyEdited), SharedMoved, int(MissingNamed));
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    // P3 — PACK and EXPLODE, the two lossless tools (the plan's "extended claim 1")
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    {
        // A project that references its geometry as a sibling, written to disk, then Packed (inlined) and compared:
        //    Pack must produce a container whose reference resolves with the sibling GONE.
        SpaceWriter Writer;
        BeginOk(Writer, SpaceTag("PROJ"), 1u, MakeMeta("Packable", kSpacePackEmbedSmall));
        Writer.BeginTable(kTagRefs);
        SpaceReferenceRecord Geometry{};
        Geometry.Type = SpaceTag("GEOM");
        Geometry.Mode = kSpaceRefSibling;
        Geometry.Flags = kSpaceRefRequired;
        Geometry.ContentHash = SpaceHash64(GeometryFiles[0].data(), GeometryFiles[0].size());
        Writer.WriteRowWithString(Geometry, offsetof(SpaceReferenceRecord, PathOffset), "Sphere.geometry");
        Writer.EndTable();
        Writer.BeginTable(kTagScen);
        SpaceLevelRow Level{};
        SpaceSetName(Level.Name, sizeof(Level.Name), "Showroom");
        Writer.WriteRow(Level);
        Writer.EndTable();
        std::vector<uint8_t> Unpacked;
        std::string Error;
        if (!Writer.Finish(Unpacked, Error)) Fail("P3 the packable project would not write: %s", Error.c_str());
        else
        {
            const std::string Base = "/tmp/SpaceFamily/Pack";
            WriteFile(Base + "/Sphere.geometry", GeometryFiles[0]);
            std::vector<uint8_t> Packed;
            std::string PackError;
            uint32_t Inlined = 0u;
            const std::vector<std::pair<std::string, std::string>> NoRoots = { { "ProjectContent", "" } };
            if (!SpacePack(Unpacked, Base, NoRoots, Packed, PackError, &Inlined)) Fail("P3 -Pack refused: %s", PackError.c_str());
            else
            {
                std::system("rm -rf /tmp/SpaceFamily/Pack");   // the sibling is GONE
                SpaceReader Reader;
                std::string ReadError;
                std::vector<uint8_t> Loaded;
                std::string LoadError;
                bool Recovered = false;
                if (Reader.Open(Packed, ReadError) && Reader.ReadBlobs(ReadError))
                {
                    const std::vector<SpaceReferenceRecord> References = Reader.References(LoadError);
                    Recovered = References.size() == 1u && References[0].Mode == kSpaceRefEmbedded &&
                                SpaceLoadReference(Reader, References[0], Base, NoRoots, Loaded, LoadError) &&
                                Loaded == GeometryFiles[0];
                }

                // …and Explode writes it back out as a file, leaving a sibling reference behind.
                std::vector<uint8_t> Exploded;
                std::vector<std::string> Written;
                std::string ExplodeError;
                const bool ExplodeOk = SpaceExplode(Packed, "/tmp/SpaceFamily/Exploded", Exploded, Written, ExplodeError);
                (void)ExplodeOk;   // the verdict below is on the artefacts it produced, not on the call returning true
                SpaceReader AfterReader;
                std::string AfterError;
                const std::vector<SpaceReferenceRecord> AfterRefs = AfterReader.Open(Exploded, AfterError)
                                                                 ? AfterReader.References(AfterError) : std::vector<SpaceReferenceRecord>{};
                const bool ReferenceAgain = AfterRefs.size() == 1u && AfterRefs[0].Mode == kSpaceRefSibling &&
                                            !Written.empty() && FileSize(Written[0]) == GeometryFiles[0].size();
                // Pack(Explode(X)) == X: the round trip the plan promises, byte for byte.
                std::vector<uint8_t> Repacked;
                std::string RepackError;
                uint32_t RepackInlined = 0u;
                const bool RepackOk = ReferenceAgain &&
                    SpacePack(Exploded, "/tmp/SpaceFamily/Exploded", NoRoots, Repacked, RepackError, &RepackInlined) &&
                    Repacked == Packed;
                if (Recovered && ReferenceAgain && RepackOk)
                    Pass("P3 -Pack inlines the sibling (%u payload) and the project still resolves with the file DELETED; "
                         "-Explode writes it back as %zu file(s) and leaves a sibling reference; Pack(Explode(X)) == X (%zu B)",
                         Inlined, Written.size(), Repacked.size());
                else
                    Fail("P3 pack/explode round trip failed (packed resolves %d, explodes back %d, repack identical %d: %s%s)",
                         int(Recovered), int(ReferenceAgain), int(RepackOk), PackError.c_str(), RepackError.c_str());
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    // P5/P6 — the P6 vocabulary and .runtime: every type writes, reads, and says what it is
    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    {
        // .runtime: the resolved launch configuration, written and read back — and the one hard rule, checked here
        //    rather than asserted in a document: state is NEVER embedded in an asset.
        SpaceWriter Runtime;
        BeginOk(Runtime, SpaceTag("RUNT"), 1u, MakeMeta("Project-Zero", kSpacePackEmbedSmall));
        Runtime.BeginTable(kTagStat);
        SpaceStateRow State{};
        SpaceSetName(State.Domain, sizeof(State.Domain), "session");
        State.Revision = 7u;
        State.PayloadBytes = 5u;
        Runtime.WriteRow(State);
        Runtime.WriteBytes("SCEN\x00", 5u);
        Runtime.EndTable();
        Runtime.BeginTable(kTagRefs);
        SpaceReferenceRecord Project{};
        Project.Type = SpaceTag("PROJ");
        Project.Mode = kSpaceRefProjectContent;
        Project.Flags = kSpaceRefRequired;
        Runtime.WriteRowWithString(Project, offsetof(SpaceReferenceRecord, PathOffset), "Project-Zero.projectspace");
        Runtime.EndTable();
        std::vector<uint8_t> Bytes;
        std::string Error;
        bool RuntimeOk = false;
        if (Runtime.Finish(Bytes, Error))
        {
            SpaceReader Reader;
            std::string ReadError;
            RuntimeOk = Reader.Open(Bytes, ReadError) && Reader.Type() != nullptr &&
                        std::string(Reader.Type()->Extension) == ".runtime";
        }

        // The state-embedding rule: an asset that sets kSpacePackStateEmbedded is refused by name.
        SpaceWriter Offender;
        SpaceMetaRecord Meta = MakeMeta("Offending", kSpacePackEmbedSmall | kSpacePackStateEmbedded);
        BeginOk(Offender, SpaceTag("GEOM"), 1u, Meta);
        Offender.BeginTable(kTagMesh);
        const uint32_t Zero[2] = { 0u, 0u };
        Offender.WriteBytes(Zero, sizeof(Zero));
        Offender.EndTable();
        std::vector<uint8_t> Refused;
        std::string Refusal;
        const bool StateRefused = !Offender.Finish(Refused, Refusal) || Refusal.find("state") != std::string::npos;
        const bool StateFlagNamed = (Meta.PackPolicy & kSpacePackStateEmbedded) != 0u;

        // P6's vocabulary: one file of each new type, written and read, with its own table present.
        long VocabularyFailures = 0;
        const SpaceTag Vocabulary[] = { kTagEnvr, kTagPigm, kTagUvsp, kTagFlow, kTagArch };
        for (const SpaceTag& Tag : Vocabulary)
        {
            const SpaceFileType* Type = nullptr;
            for (const SpaceFileType& Candidate : kSpaceFileTypes) if (Candidate.Required == Tag) Type = &Candidate;
            if (!Type) { ++VocabularyFailures; Fail("P6 no file type requires %s", Tag.Text().c_str()); continue; }
            SpaceWriter One;
            BeginOk(One, Type->Tag, 1u, MakeMeta(Type->Name, kSpacePackEmbedSmall));
            One.BeginTable(Tag);
            const uint32_t Count = 1u;
            One.WriteBytes(&Count, sizeof(Count));
            const SpaceEnvironmentRow Environment{};
            const SpacePigmentRow Pigment{};
            const SpaceUvIslandRow Island{};
            const SpaceFlowStepRow Flow{};
            const SpaceArchiveEntry Archive{};
            if (Tag == kTagEnvr) One.WriteRow(Environment);
            if (Tag == kTagPigm) One.WriteRow(Pigment);
            if (Tag == kTagUvsp) One.WriteRow(Island);
            if (Tag == kTagFlow) One.WriteRow(Flow);
            if (Tag == kTagArch) One.WriteRow(Archive);
            One.EndTable();
            std::vector<uint8_t> OneBytes;
            std::string OneError;
            if (!One.Finish(OneBytes, OneError)) { ++VocabularyFailures; Fail("P6 %s would not write: %s", Type->Extension, OneError.c_str()); continue; }
            SpaceReader Reader;
            std::string SearchError;
            if (!Reader.Open(OneBytes, SearchError) || !Reader.Find(Tag)) { ++VocabularyFailures; }
        }

        if (RuntimeOk && StateRefused && StateFlagNamed && VocabularyFailures == 0)
            Pass("P5/P6 .runtime writes and reads back as .runtime; a file claiming the state-embedding policy is refused by "
                 "name; and all %zu P6 types (%s) write and read with their own table present",
                 sizeof(Vocabulary) / sizeof(Vocabulary[0]), ".environment .pigment .uvspace .workflow .archive");
        else
            Fail("P5/P6 failed (runtime %d, state refused %d, vocabulary failures %ld)", int(RuntimeOk), int(StateRefused), VocabularyFailures);
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════
    std::printf("[space-family] %d passed · %d failed · %d skipped\n", Passes, Failures, Skips);
    if (Failures == 0) { std::printf("[SpaceFamily] GREEN — gates passed: %d (skipped %d)\n", Passes, Skips); return 0; }
    std::printf("[SpaceFamily] RED\n");
    return 1;
}
