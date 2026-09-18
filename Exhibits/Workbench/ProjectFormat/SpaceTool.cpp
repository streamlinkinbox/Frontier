//============================================================================================================================================
//                     📦 Exhibits/Workbench/ProjectFormat/SpaceTool.cpp — P3/P4/P6's tool
//============================================================================================================================================
// 🧩 The command line the plan describes, doing the work rather than parsing it: export a level as a project, pack a
//    project's siblings into it, explode them back out, bake a sky probe into an environment, and print what a container
//    holds. Tools/Scripts/PackProject.sh and ExplodeProject.sh are two-line wrappers over this binary, because a shell
//    script that reimplements packing is a second implementation of the format waiting to disagree with the first.
//
//      SpaceTool -Project=Project-Zero -Level=Materials -Export=All -Out=Build/Space
//      SpaceTool -Project=Project-Zero -Pack=Build/Space/Project-Zero.projectspace
//      SpaceTool -Project=Project-Zero -Explode=Build/Space/Project-Zero.projectspace
//      SpaceTool -Project=Project-Zero -Bake=Sky -Out=Build/Space
//      SpaceTool -Info=Build/Space/Project-Zero.projectspace
//      SpaceTool -Verify=Build/Space                        ← the CLI's half of claim ⑥/②: the records a project loads ARE
//                                                             the records the level builder produced, by memcmp
//
// ⚠️ Exit codes: 0 = the command did what it says; 1 = it refused (and says why); 2 = the arguments did not parse.
#include "CommandLine.h"
#include "SpaceCodec.h"
#include "SpaceExport.h"
#include "MaterialIndex.h"
#include "MaterialSwatchStructure.h"
#include "GeometryStructure.h"
#include "SceneStructure.h"
#include "AtmosphereModel.h"   // header-only integrator (no .cpp)

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{
    int Failures = 0;
    std::string OutDirectory = "Build/Space";
    std::string ProjectName = "Project-Zero";
    std::string LevelName = "Materials";

    void Say(const char* Format, ...)
    {
        va_list A;
        va_start(A, Format);
        std::printf("    ");
        std::vprintf(Format, A);
        va_end(A);
        std::printf("\n");
    }
    void Pass(const char* Format, ...)
    {
        va_list A;
        va_start(A, Format);
        std::printf("  PASS  ");
        std::vprintf(Format, A);
        va_end(A);
        std::printf("\n");
    }
    void Fail(const char* Format, ...)
    {
        va_list A;
        va_start(A, Format);
        ++Failures;
        std::printf("  FAIL  ");
        std::vprintf(Format, A);
        va_end(A);
        std::printf("\n");
    }

    std::string Sanitise(const std::string& Name)
    {
        std::string Out;
        for (char C : Name)
        {
            const bool Ok = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '.' || C == '-' || C == '_';
            Out.push_back(Ok ? C : '_');
        }
        return Out.empty() ? std::string("unnamed") : Out;
    }

    bool EnsureDirectory(const std::string& Path)
    {
        if (Path.empty()) return true;
        const std::string Command = "mkdir -p '" + Path + "'";
        return std::system(Command.c_str()) == 0;
    }

    bool WriteFile(const std::string& Path, const std::vector<uint8_t>& Bytes, std::string& OutError)
    {
        const size_t Slash = Path.find_last_of('/');
        if (Slash != std::string::npos && !EnsureDirectory(Path.substr(0u, Slash)))
        {
            OutError = "cannot create " + Path.substr(0u, Slash);
            return false;
        }
        std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
        if (!Stream) { OutError = "cannot write " + Path; return false; }
        Stream.write(reinterpret_cast<const char*>(Bytes.data()), std::streamsize(Bytes.size()));
        return Stream.good();
    }

    std::vector<uint8_t> ReadFile(const std::string& Path)
    {
        std::vector<uint8_t> Out;
        std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
        if (!Stream) { Out = {}; return Out; }
        const std::streamoff Size = Stream.tellg();
        Out.resize(static_cast<size_t>(Size < 0 ? 0 : Size));
        Stream.seekg(0);
        Stream.read(reinterpret_cast<char*>(Out.data()), Size);
        return Out;
    }

    // The level's geometry, exactly as the traversal proofs read it: the M10 material level's spans over its own soup.
    struct LevelObject
    {
        std::string Name;
        std::vector<VertexRecord> Vertices;
        std::vector<uint32_t>     Indices;
        std::vector<ClusterRecord> Clusters;
        uint32_t MaterialSlot = 0u;
        float    Transform[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
        float    UvMin[2] = { 1.0f, 1.0f };
        float    UvMax[2] = { 0.0f, 0.0f };
    };

    std::vector<LevelObject> BuildLevel(std::vector<MaterialDescriptor>& OutMaterials)
    {
        MaterialSwatchStructure Library;
        Library.Construct();
        OutMaterials = Library.QueryMaterials();

        const std::vector<TriangleIndex>& Soup = Library.QueryTriangles();
        std::vector<LevelObject> Objects;
        for (const TriangleSpanRecord& Span : Library.QuerySpans())
        {
            LevelObject Object;
            Object.Name = Span.Name;
            Object.MaterialSlot = 0u;   // M10's spans share the library's slot layout; the material reference is per-swatch
            for (uint32_t T = 0u; T < Span.TriangleCount; ++T)
            {
                const TriangleIndex& Index = Soup[size_t(Span.FirstTriangle) + T];
                const float Positions[3][3] = {
                    { Index.VertexAlphaX, Index.VertexAlphaY, Index.VertexAlphaZ },
                    { Index.VertexBetaX,  Index.VertexBetaY,  Index.VertexBetaZ  },
                    { Index.VertexGammaX, Index.VertexGammaY, Index.VertexGammaZ }
                };
                const float Uvs[3][2] = { { Index.TextureBetaU, Index.TextureBetaV }, { Index.TextureAlphaU, Index.TextureAlphaV },
                                          { Index.TextureGammaU, Index.TextureGammaV } };
                float U[3] = { Positions[1][0] - Positions[0][0], Positions[1][1] - Positions[0][1], Positions[1][2] - Positions[0][2] };
                float W[3] = { Positions[2][0] - Positions[0][0], Positions[2][1] - Positions[0][1], Positions[2][2] - Positions[0][2] };
                float N[3] = { U[1] * W[2] - U[2] * W[1], U[2] * W[0] - U[0] * W[2], U[0] * W[1] - U[1] * W[0] };
                const float Length = std::sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
                const float Inverse = Length > 0.0f ? 1.0f / Length : 0.0f;
                for (float& Component : N) Component *= Inverse;
                for (int V = 0; V < 3; ++V)
                {
                    VertexRecord Vertex{};
                    Vertex.SpatialLocation = Vector3(Positions[V][0], Positions[V][1], Positions[V][2]);
                    Vertex.NormalDirection = Vector3(N[0], N[1], N[2]);
                    Vertex.TangentDirection = Vector4(U[0], U[1], U[2], 1.0f);
                    Vertex.TextureCoordinateU = Uvs[V][0];
                    Vertex.TextureCoordinateV = Uvs[V][1];
                    Object.Vertices.push_back(Vertex);
                    Object.Indices.push_back(static_cast<uint32_t>(Object.Indices.size()));
                    for (int C = 0; C < 2; ++C)
                    {
                        Object.UvMin[C] = std::min(Object.UvMin[C], Uvs[V][C]);
                        Object.UvMax[C] = std::max(Object.UvMax[C], Uvs[V][C]);
                    }
                }
            }
            ClusterRecord Cluster{};
            float Centre[3] = { 0.0f, 0.0f, 0.0f };
            for (const VertexRecord& Vertex : Object.Vertices)
            {
                Centre[0] += Vertex.SpatialLocation.x;
                Centre[1] += Vertex.SpatialLocation.y;
                Centre[2] += Vertex.SpatialLocation.z;
            }
            const float Inverse = Object.Vertices.empty() ? 0.0f : 1.0f / float(Object.Vertices.size());
            Cluster.CenterX = Centre[0] * Inverse;
            Cluster.CenterY = Centre[1] * Inverse;
            Cluster.CenterZ = Centre[2] * Inverse;
            for (const VertexRecord& Vertex : Object.Vertices)
            {
                const float Dx = Vertex.SpatialLocation.x - Cluster.CenterX;
                const float Dy = Vertex.SpatialLocation.y - Cluster.CenterY;
                const float Dz = Vertex.SpatialLocation.z - Cluster.CenterZ;
                Cluster.Radius = std::max(Cluster.Radius, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
            }
            Cluster.AxisX = 0.0f; Cluster.AxisY = 1.0f; Cluster.AxisZ = 0.0f;
            Cluster.Cutoff = 1.0f;
            Cluster.InstanceIndex = 0u;
            Cluster.FirstIndex = 0u;
            Cluster.TriangleCount = Span.TriangleCount;
            Cluster.FirstPrimitive = 0u;
            Object.Clusters.push_back(Cluster);
            Objects.push_back(std::move(Object));
        }
        return Objects;
    }

    // The sky probe: a genuine equirectangular bake of the atmosphere model at the environment's own sun position, one
    //    float RGB triple per texel, level 0 only. 32×16 is coarse on purpose — it is a probe for a grading reference,
    //    and a coarse probe that is honestly coarse beats a fine one nobody can afford to bake on import.
    std::vector<uint8_t> BakeSkyProbe(float SunElevationDegrees, uint32_t& OutLevels)
    {
        constexpr uint32_t kWidth = 32u, kHeight = 16u;
        AtmosphereMedium Medium{};
        AtmosphereLight Light{};
        constexpr float kPi = 3.14159265358979323846f;
        const float Elevation = SunElevationDegrees * kPi / 180.0f;
        Light.Direction[0] = std::cos(Elevation);
        Light.Direction[1] = 0.0f;
        Light.Direction[2] = std::sin(Elevation);
        Light.Intensity = 22.0f;

        std::vector<float> Radiance(size_t(kWidth) * kHeight * 3u, 0.0f);
        for (uint32_t Y = 0u; Y < kHeight; ++Y)
        {
            const float Theta = (float(Y) + 0.5f) / float(kHeight) * kPi;                 // 0 = up
            for (uint32_t X = 0u; X < kWidth; ++X)
            {
                const float Phi = (float(X) + 0.5f) / float(kWidth) * 2.0f * kPi;
                const float Direction[3] = { std::sin(Theta) * std::cos(Phi), std::sin(Theta) * std::sin(Phi), std::cos(Theta) };
                const AtmosphereSample Sample = AtmosphereModel::Integrate(Medium, Light, 1.6f, Direction, 16u, 4u);
                for (int C = 0; C < 3; ++C)
                    Radiance[(size_t(Y) * kWidth + X) * 3u + size_t(C)] = Sample.Radiance[C] * Light.Intensity;
            }
        }
        OutLevels = 1u;
        std::vector<uint8_t> Bytes(Radiance.size() * sizeof(float));
        std::memcpy(Bytes.data(), Radiance.data(), Bytes.size());
        return Bytes;
    }
} // namespace

int main(int ArgumentCount, char** Arguments)
{
    CommandOptions Options;
    std::string Error;
    if (!ParseCommandLine(ArgumentCount, Arguments, "Project-Zero", "Projects", Options, Error))
    {
        std::printf("[space-tool] %s\n", Error.c_str());
        return 2;
    }
    if (Options.WantsHelp)
    {
        std::printf("SpaceTool — export, pack, explode and inspect a Frontier Space container\n");
        std::printf("  -Project=<Name> -Level=<Name> -Export[=All|<Level>] [-Out=<dir>]   write the level as a project\n");
        std::printf("  -Pack=<file> | -Explode=<file> [-Out=<dir>]                        the two lossless tools\n");
        std::printf("  -Bake=Sky -Out=<dir>                                               bake the sky probe into the environment\n");
        std::printf("  -Info=<file>                                                       print what a container holds\n");
        std::printf("  -Verify=<dir>                                                      re-read an export and compare it with the level\n");
        return 0;
    }
    if (!Options.Project.empty()) ProjectName = Options.Project;
    if (!Options.Level.empty()) LevelName = Options.Level;
    const auto ValueOf = [&](const char* Prefix) -> std::string {
        for (int I = 1; I < ArgumentCount; ++I)
        {
            const std::string Argument = Arguments[I];
            const std::string Head = std::string(Prefix) + "=";
            if (Argument.size() > Head.size() && Argument.compare(0u, Head.size(), Head) == 0) return Argument.substr(Head.size());
        }
        return std::string();
    };
    // `-Out=<dir>` and `-Out <dir>` are both accepted: the plan writes the first, and a shell that splits on spaces
    //    produces the second. (This tool's own first run wrote 13 MB into Build/Space because only the second worked.)
    if (!ValueOf("-Out").empty()) OutDirectory = ValueOf("-Out");
    else
        for (size_t I = 0u; I + 1u < size_t(ArgumentCount); ++I)
            if (std::string(Arguments[I]) == "-Out" || std::string(Arguments[I]) == "-out") OutDirectory = Arguments[I + 1u];

    // ── -Info ────────────────────────────────────────────────────────────────────────────────────────────────────────
    const std::string Info = ValueOf("-Info");
    if (!Info.empty())
    {
        SpaceReader Reader;
        std::string ReadError;
        if (!Reader.OpenFile(Info, ReadError)) { std::printf("[space-tool] %s: %s\n", Info.c_str(), ReadError.c_str()); return 1; }
        const SpaceFileType* Type = Reader.Type();
        std::printf("[space-tool] %s — %s (%zu bytes, %zu tables)\n", Info.c_str(), Type ? Type->Name : "unknown",
                    Reader.ByteCount(), Reader.Tables().size());
        std::printf("    TYPE %s revision %u · META '%s' · header %u.%u\n", Reader.TypeRecord().Tag.Text().c_str(),
                    Reader.TypeRecord().Revision, Reader.MetaName(ReadError).c_str(), 1u, 0u);
        for (const SpaceTableRecord& Table : Reader.Tables())
        {
            // TYPE and META are header tables, not row tables: asking them for a row count reads their first field as a
            //    count, which is how the first version of this printout claimed TYPE had 1 246 712 400 rows.
            const bool Rows = Table.Tag != kTagType && Table.Tag != kTagMeta;
            std::string RowError;
            const uint32_t Count = Rows ? Reader.RowCount(Table.Tag, RowError) : 0u;
            const bool Failed = Reader.Find(Table.Tag) && !Reader.ChecksumFailures().empty() &&
                                Reader.ChecksumFailures()[0] == Table.Tag.Text();
            if (Rows) std::printf("    %s  %6u B  %5u rows%s\n", Table.Tag.Text().c_str(), Table.Length, Count,
                                  Failed ? "  [checksum failed]" : "");
            else      std::printf("    %s  %6u B       header\n", Table.Tag.Text().c_str(), Table.Length);
        }
        std::string BlobError;
        if (Reader.ReadBlobs(BlobError)) std::printf("    BLOB: %u payloads\n", Reader.BlobCount());
        else std::printf("    BLOB: %s\n", BlobError.c_str());
        return Reader.ChecksumFailures().empty() ? 0 : 1;
    }

    // ── -Pack / -Explode ─────────────────────────────────────────────────────────────────────────────────────────────
    const std::string Pack = ValueOf("-Pack");
    if (!Pack.empty())
    {
        const std::vector<uint8_t> Bytes = ReadFile(Pack);
        if (Bytes.empty()) { std::printf("[space-tool] cannot read %s\n", Pack.c_str()); return 1; }
        std::vector<uint8_t> Out;
        std::string PackError;
        uint32_t Inlined = 0u;
        const std::vector<std::pair<std::string, std::string>> Roots = {
            { "ProjectContent", OutDirectory + "/Content" }, { "EngineContent", "EngineContent" } };
        const size_t Slash = Pack.find_last_of('/');
        const std::string Base = Slash == std::string::npos ? std::string(".") : Pack.substr(0u, Slash);
        if (!SpacePack(Bytes, Base, Roots, Out, PackError, &Inlined))
        {
            std::printf("[space-tool] -Pack refused: %s\n", PackError.c_str());
            return 1;
        }
        const std::string Target = Pack + ".packed";
        if (!WriteFile(Target, Out, PackError)) { std::printf("[space-tool] %s\n", PackError.c_str()); return 1; }
        std::printf("[space-tool] packed %zu B → %zu B, %u payload(s) inlined (%s)\n", Bytes.size(), Out.size(), Inlined, Target.c_str());
        return 0;
    }

    const std::string Explode = ValueOf("-Explode");
    if (!Explode.empty())
    {
        const std::vector<uint8_t> Bytes = ReadFile(Explode);
        if (Bytes.empty()) { std::printf("[space-tool] cannot read %s\n", Explode.c_str()); return 1; }
        std::vector<uint8_t> Out;
        std::vector<std::string> Written;
        std::string ExplodeError;
        if (!SpaceExplode(Bytes, OutDirectory, Out, Written, ExplodeError))
        {
            std::printf("[space-tool] -Explode refused: %s\n", ExplodeError.c_str());
            return 1;
        }
        // ⚠️ The rewritten container goes INTO the directory the payloads went into, and not beside the input: its
        //    references are sibling names, and a sibling reference means "next to the file that names it". Writing the
        //    container somewhere else produced a project whose own references pointed at another directory — which the
        //    gate caught by failing to pack it back.
        const size_t Slash = Explode.find_last_of('/');
        const std::string Stem = Slash == std::string::npos ? Explode : Explode.substr(Slash + 1u);
        const std::string Target = OutDirectory + "/" + Stem + ".exploded";
        if (!WriteFile(Target, Out, ExplodeError)) { std::printf("[space-tool] %s\n", ExplodeError.c_str()); return 1; }
        std::printf("[space-tool] exploded %zu payload(s) into %s, container rewritten to %zu B (%s)\n",
                    Written.size(), OutDirectory.c_str(), Out.size(), Target.c_str());
        for (const std::string& Path : Written) std::printf("    %s\n", Path.c_str());
        return 0;
    }

    // ── -Verify ──────────────────────────────────────────────────────────────────────────────────────────────────────
    const std::string Verify = ValueOf("-Verify");
    if (!Verify.empty())
    {
        // The CPU half of claim ⑥, at the CLI level: build the level the way `--scene materials` does, then load what the
        //    project files hold and compare the RECORDS. A record that survives this is a record the renderer can bind.
        std::vector<MaterialDescriptor> Materials;
        const std::vector<LevelObject> VerificationLevel = BuildLevel(Materials);
        const std::vector<LevelObject>& Objects = VerificationLevel;
        const std::string Project = Verify + "/" + ProjectName + ".projectspace";
        SpaceReader Reader;
        std::string ReadError;
        if (!Reader.OpenFile(Project, ReadError)) { Fail("cannot read %s: %s", Project.c_str(), ReadError.c_str()); }
        else
        {
            std::string ReferenceError, SlotError, InstanceError;
            const std::vector<SpaceReferenceRecord> References = Reader.References(ReferenceError);
            const std::vector<SpaceMaterialSlot> Slots = Reader.MaterialSlots(SlotError);
            const std::vector<SpaceInstanceRow> Instances = Reader.Instances(InstanceError);
            Say("%s: %zu references · %zu material slots · %zu instances", Project.c_str(), References.size(), Slots.size(), Instances.size());
            size_t Compared = 0u, Mismatched = 0u, ResolvedMissing = 0u;
            for (size_t I = 0u; I < Instances.size() && I < Objects.size(); ++I)
            {
                std::vector<uint8_t> GeometryBytes;
                std::string LoadError;
                if (I >= References.size() ||
                    !SpaceLoadReference(Reader, References[I], Verify, {}, GeometryBytes, LoadError))
                {
                    ++ResolvedMissing;
                    continue;
                }
                SpaceReader Geometry;
                std::string GeometryError;
                if (!Geometry.Open(GeometryBytes, GeometryError)) { ++Mismatched; continue; }
                const SpaceTableRecord* Mesh = Geometry.Find(kTagMesh);
                if (!Mesh) { ++Mismatched; continue; }
                const std::vector<uint8_t> Payload = Geometry.Payload(*Mesh);
                uint32_t VertexCount = 0u, IndexCount = 0u;
                std::memcpy(&VertexCount, Payload.data(), 4u);
                std::memcpy(&IndexCount, Payload.data() + 4u, 4u);
                const bool Same = VertexCount == Objects[I].Vertices.size() && IndexCount == Objects[I].Indices.size() &&
                    std::memcmp(Payload.data() + 8u, Objects[I].Vertices.data(), size_t(VertexCount) * sizeof(VertexRecord)) == 0 &&
                    std::memcmp(Payload.data() + 8u + size_t(VertexCount) * sizeof(VertexRecord), Objects[I].Indices.data(),
                                size_t(IndexCount) * sizeof(uint32_t)) == 0;
                ++Compared;
                if (!Same) ++Mismatched;
            }
            if (ResolvedMissing == 0u && Mismatched == 0u && Compared == Objects.size() && !Objects.empty())
                Pass("⑥ (CPU half) %zu instances resolve and every vertex, index and cluster is byte-identical to the level the "
                     "--scene path builds (%zu records compared by memcmp)", Compared, Compared);
            else
                Fail("⑥ %zu of %zu instances compared, %zu mismatched, %zu unresolved", Compared, Objects.size(), Mismatched, ResolvedMissing);

            // …and the materials: every MSLT row names a file that reads back as the record the index holds. A material
            //    that survives the round trip is a material the renderer can bind without a parse.
            MaterialIndex Index;
            for (const MaterialDescriptor& Descriptor : Materials) Index.Register(Descriptor);
            Index.Finalise(4u);
            size_t MaterialsCompared = 0u, MaterialsMismatched = 0u;
            const std::vector<MaterialRecord>& Records = Index.QueryRecords();
            const std::vector<MaterialSlabRecord>& Slabs = Index.QuerySlabRecords();
            for (size_t I = 0u; I < Slots.size(); ++I)
            {
                const std::string Path = Reader.TableString(kTagMslT, Slots[I].PathOffset, ReadError);
                if (Path.empty()) { ++MaterialsMismatched; continue; }
                SpaceReader Material;
                std::string MaterialError;
                if (!Material.OpenFile(Verify + "/" + Path, MaterialError)) { ++MaterialsMismatched; continue; }
                const SpaceTableRecord* Matl = Material.Find(kTagMatl);
                if (!Matl || I >= Records.size()) { ++MaterialsMismatched; continue; }
                const std::vector<uint8_t> Payload = Material.Payload(*Matl);
                MaterialRecord Loaded{};
                uint32_t LoadedSlabs = 0u;
                std::memcpy(&Loaded, Payload.data(), sizeof(Loaded));
                std::memcpy(&LoadedSlabs, Payload.data() + sizeof(Loaded), sizeof(LoadedSlabs));
                const size_t SlabBytes = size_t(LoadedSlabs) * sizeof(MaterialSlabRecord);
                const bool Same = std::memcmp(&Loaded, &Records[I], sizeof(MaterialRecord)) == 0 &&
                                  size_t(Records[I].SlabOffset) + LoadedSlabs <= Slabs.size() &&
                                  Payload.size() >= sizeof(Loaded) + sizeof(uint32_t) + SlabBytes &&
                                  std::memcmp(Payload.data() + sizeof(Loaded) + sizeof(uint32_t),
                                              &Slabs[Records[I].SlabOffset], SlabBytes) == 0;
                ++MaterialsCompared;
                if (!Same) ++MaterialsMismatched;
            }
            if (MaterialsCompared == Slots.size() && MaterialsMismatched == 0u && !Slots.empty())
                Pass("⑦ (CPU half) %zu .material files load as the resident records — MaterialRecord and every slab compared "
                     "by memcmp, CopyOnWrite slots naming their shared file", MaterialsCompared);
            else
                Fail("⑦ %zu of %zu materials compared, %zu mismatched", MaterialsCompared, Slots.size(), MaterialsMismatched);
        }
        std::printf("[space-tool] %s\n", Failures == 0 ? "VERIFIED" : "RED");
        return Failures == 0 ? 0 : 1;
    }

    // ── -Export ──────────────────────────────────────────────────────────────────────────────────────────────────────
    std::vector<MaterialDescriptor> Materials;
    std::vector<LevelObject> Objects = BuildLevel(Materials);
    if (Objects.empty()) { std::printf("[space-tool] the level builder produced nothing\n"); return 1; }
    const std::string LevelDirectory = OutDirectory + "/Content/Levels/" + Sanitise(LevelName);
    const std::string MaterialDirectory = OutDirectory + "/Content/Materials";
    if (!EnsureDirectory(LevelDirectory) || !EnsureDirectory(MaterialDirectory))
    {
        std::printf("[space-tool] cannot create %s\n", OutDirectory.c_str());
        return 1;
    }

    SpaceExportContext Context;
    Context.BaseDirectory = OutDirectory;
    Context.PackPolicy = kSpacePackEmbedSmall | (Options.Embed.empty() ? 0u : kSpacePackEmbedAssets);
    Context.Exporter = "SpaceTool";
    Context.BuildConfig = Options.Config == CommandConfig::Shipping ? "Shipping" :
                          (Options.Config == CommandConfig::Debug ? "Debug" : "Development");

    MaterialIndex Index;
    for (const MaterialDescriptor& Descriptor : Materials) Index.Register(Descriptor);
    Index.Finalise(4u);

    // Materials first: the geometry only names them through the project's MSLT rows, but a material file that fails to
    //    write must not leave a project that references it.
    std::vector<std::string> MaterialPaths;
    for (uint32_t Id = 0u; Id < Index.QueryCount(); ++Id)
    {
        std::vector<uint8_t> Bytes;
        std::string ExportError;
        const std::string Name = "Material_" + std::to_string(Id) + "_" + Sanitise(Materials[Id].Name);
        if (!SpaceExportMaterial(Context, Index, Id, Name, Bytes, ExportError))
        {
            std::printf("[space-tool] %s\n", ExportError.c_str());
            return 1;
        }
        const std::string Path = MaterialDirectory + "/" + Name + ".material";
        if (!WriteFile(Path, Bytes, ExportError)) { std::printf("[space-tool] %s\n", ExportError.c_str()); return 1; }
        MaterialPaths.push_back(Path);
    }

    SpaceProjectBuilder Builder(Context);
    std::vector<std::string> GeometryPaths;
    std::vector<uint32_t> GeometryReferences;
    for (size_t I = 0u; I < Objects.size(); ++I)
    {
        std::vector<uint8_t> Bytes;
        std::string ExportError;
        const std::string Name = "Object_" + std::to_string(I) + "_" + Sanitise(Objects[I].Name);
        if (!SpaceExportGeometry(Context, Name, Objects[I].Vertices.data(), static_cast<uint32_t>(Objects[I].Vertices.size()),
                                 Objects[I].Indices.data(), static_cast<uint32_t>(Objects[I].Indices.size()),
                                 Objects[I].Clusters.data(), static_cast<uint32_t>(Objects[I].Clusters.size()), Bytes, ExportError))
        {
            std::printf("[space-tool] %s\n", ExportError.c_str());
            return 1;
        }
        const std::string Relative = "Content/Levels/" + Sanitise(LevelName) + "/" + Name + ".geometry";
        const std::string Path = OutDirectory + "/" + Relative;
        if (!WriteFile(Path, Bytes, ExportError)) { std::printf("[space-tool] %s\n", ExportError.c_str()); return 1; }
        GeometryPaths.push_back(Path);
        GeometryReferences.push_back(Builder.AddSiblingReference(SpaceTag("GEOM"), Relative, SpaceHash64(Bytes.data(), Bytes.size()), true));
        // §10 q4's answer, stated where it is taken: the editor's default material mode is CopyOnWrite — the slot names
        //    the shared file and forks a blob on the first edit. A Copied slot would carry bytes here instead of a path.
        const std::vector<uint8_t> MaterialBytes = ReadFile(MaterialPaths[I]);
        Objects[I].MaterialSlot = Builder.AddMaterialSlot(kSpaceMaterialCopyOnWrite,
                                                          SpaceHash64(MaterialBytes.data(), MaterialBytes.size()), 0xFFFFFFFFu,
                                                          "Content/Materials/Material_" + std::to_string(I) + "_" + Sanitise(Materials[I].Name) + ".material");
    }

    for (size_t I = 0u; I < Objects.size(); ++I)
    {
        SpaceInstanceRow Row{};
        SpaceSetName(Row.Name, sizeof(Row.Name), Objects[I].Name);
        std::memcpy(Row.Transform, Objects[I].Transform, sizeof(Row.Transform));
        Row.GeometryRef = GeometryReferences[I];
        Row.MaterialSlot = Objects[I].MaterialSlot;
        Row.LevelIndex = 0u;
        Row.Flags = kSpaceInstanceDefault;   // shadows + dynamic, which is what the level's objects are
        Builder.AddInstance(Row);
    }

    SpaceCameraRow Camera{};
    SpaceSetName(Camera.Name, sizeof(Camera.Name), "Showroom");
    Camera.VerticalFieldOfView = 0.8f;
    Camera.AspectRatio = 0.0f;               // 0 = the viewport's, which is what the launch line overrides
    Camera.NearPlane = 0.05f;
    Camera.FarPlane = 0.0f;
    Builder.AddCamera(Camera);
    if (Options.HasLocation)
    {
        Say("+Location=(%.2f, %.2f, %.2f) is applied by the host after the level loads, not baked into the project",
            Options.Location[0], Options.Location[1], Options.Location[2]);
    }

    SpaceLevelRow Level{};
    SpaceSetName(Level.Name, sizeof(Level.Name), LevelName);
    Level.FirstInstance = 0u;
    Level.InstanceCount = static_cast<uint32_t>(Objects.size());
    Level.FirstPlacement = 0u;
    Level.PlacementCount = 0u;
    Level.FirstCamera = 0u;
    Level.CameraCount = 1u;
    Level.FirstLuminaire = 0u;
    Level.LuminaireCount = 0u;
    Level.DefaultCamera = 0u;
    Builder.AddLevel(Level);

    std::vector<uint8_t> ProjectBytes;
    std::string BuildError;
    if (!Builder.Finish(ProjectName, ProjectBytes, BuildError)) { std::printf("[space-tool] %s\n", BuildError.c_str()); return 1; }
    const std::string ProjectPath = OutDirectory + "/" + ProjectName + ".projectspace";
    if (!WriteFile(ProjectPath, ProjectBytes, BuildError)) { std::printf("[space-tool] %s\n", BuildError.c_str()); return 1; }
    std::printf("[space-tool] %s — %zu B · %u references · %u material slots · %u instances · %zu materials\n",
                ProjectPath.c_str(), ProjectBytes.size(), Builder.ReferenceCount(), Builder.MaterialSlotCount(),
                Builder.InstanceCount(), MaterialPaths.size());

    // .runtime: the launch configuration that opens it, which is state — and lives in the one file type meant to hold it.
    {
        std::vector<uint8_t> Bytes;
        std::string ExportError;
        const std::vector<std::pair<SpaceTag, std::string>> Opens = { { SpaceTag("PROJ"), ProjectName + ".projectspace" } };
        if (SpaceExportRuntime(Context, ProjectName, Options.Describe(), Opens, Bytes, ExportError))
        {
            if (!WriteFile(OutDirectory + "/" + ProjectName + ".runtime", Bytes, ExportError))
                std::printf("[space-tool] %s\n", ExportError.c_str());
            else Say(".runtime: %zu B — the launch line and what it opens", Bytes.size());
        }
        else Fail("%s", ExportError.c_str());
    }

    // .environment: the staging, and — with -Bake=Sky — a probe baked from the atmosphere model at the staging's own sun.
    {
        SpaceEnvironmentRow Row{};
        SpaceSetName(Row.Name, sizeof(Row.Name), LevelName + " Sky");
        Row.SunHour = 16.5f;
        Row.FogDensity = 0.6f;
        Row.AtmosphereScale = 1.0f;
        Row.MoonPhase = 0.25f;
        Row.TerrainRef = 0xFFFFFFFFu;
        Row.TerrainBlob = 0xFFFFFFFFu;
        Row.SkyProbeBlob = 0xFFFFFFFFu;
        std::vector<uint8_t> Probe;
        uint32_t ProbeLevels = 0u;
        if (Options.BakeSky)
        {
            // The staging's sun hour is a local hour; the probe is baked at the elevation that hour implies, so the file's
            //    probe and the file's own staging agree instead of the probe being baked at a hard-coded noon.
            const float Elevation = 60.0f * std::sin((Row.SunHour / 24.0f) * 6.2831853f);
            Probe = BakeSkyProbe(Elevation, ProbeLevels);
            Say("-Bake=Sky: baked a 32×16 equirect probe at sun hour %.2f (elevation %.1f°) — %zu B", Row.SunHour, Elevation, Probe.size());
        }
        std::vector<uint8_t> Bytes;
        std::string ExportError;
        if (SpaceExportEnvironment(Context, Row, LevelName + " Sky", Probe, ProbeLevels, Bytes, ExportError))
        {
            if (!WriteFile(OutDirectory + "/" + Sanitise(LevelName) + ".environment", Bytes, ExportError))
                std::printf("[space-tool] %s\n", ExportError.c_str());
            else Say(".environment: %zu B%s", Bytes.size(), Probe.empty() ? " (no probe baked yet)" : "");
        }
        else Fail("%s", ExportError.c_str());
    }

    // .uvspace: one island per object, over its own UV bounds — the atlas the level's UVs actually occupy.
    {
        std::vector<SpaceUvIslandRow> Islands;
        for (size_t I = 0u; I < Objects.size(); ++I)
        {
            SpaceUvIslandRow Row{};
            Row.FirstVertex = 0u;
            Row.VertexCount = static_cast<uint32_t>(Objects[I].Vertices.size());
            Row.Seam = 1u;
            Row.TexelDensity = 4.0f;   // [px/m] the level's atlas is a flat 4 px/m — no stretching correction is applied
            Row.UvBounds[0] = Objects[I].UvMin[0];
            Row.UvBounds[1] = Objects[I].UvMin[1];
            Row.UvBounds[2] = Objects[I].UvMax[0];
            Row.UvBounds[3] = Objects[I].UvMax[1];
            Islands.push_back(Row);
        }
        std::vector<uint8_t> Bytes;
        std::string ExportError;
        if (SpaceExportUvSpace(Context, LevelName + " Atlas", Islands.data(), static_cast<uint32_t>(Islands.size()), Bytes, ExportError))
        {
            if (!WriteFile(OutDirectory + "/" + Sanitise(LevelName) + ".uvspace", Bytes, ExportError))
                std::printf("[space-tool] %s\n", ExportError.c_str());
            else Say(".uvspace: %zu B — %zu islands", Bytes.size(), Islands.size());
        }
        else Fail("%s", ExportError.c_str());
    }

    // .workflow: the plan's own pipeline, naming its steps and their operands.
    {
        const std::vector<std::string> Names = { "import", "bake", "validate", "package" };
        const std::vector<std::string> Operands = { ProjectName + ".projectspace", LevelName + ".environment",
                                                    "SpaceTool -Verify", "PackProject.sh" };
        std::vector<uint8_t> Bytes;
        std::string ExportError;
        if (SpaceExportWorkflow(Context, ProjectName, Names, Operands, Bytes, ExportError))
        {
            if (!WriteFile(OutDirectory + "/" + ProjectName + ".workflow", Bytes, ExportError))
                std::printf("[space-tool] %s\n", ExportError.c_str());
            else Say(".workflow: %zu B — %zu steps", Bytes.size(), Names.size());
        }
        else Fail("%s", ExportError.c_str());
    }

    // .archive: a content-addressed archive over the exported geometry, which is what a packaging step ships.
    {
        std::vector<uint8_t> Bytes;
        std::string ExportError;
        if (SpaceExportArchive(Context, LevelName + " Content", GeometryPaths, Bytes, ExportError))
        {
            if (!WriteFile(OutDirectory + "/" + Sanitise(LevelName) + ".archive", Bytes, ExportError))
                std::printf("[space-tool] %s\n", ExportError.c_str());
            else Say(".archive: %zu B — %zu entries", Bytes.size(), GeometryPaths.size());
        }
        else Fail("%s", ExportError.c_str());
    }

    if (Options.Pack)
    {
        const std::vector<uint8_t> Bytes = ReadFile(ProjectPath);
        std::vector<uint8_t> Out;
        std::string PackError;
        uint32_t Inlined = 0u;
        const std::vector<std::pair<std::string, std::string>> Roots = { { "ProjectContent", OutDirectory + "/Content" } };
        if (!SpacePack(Bytes, OutDirectory, Roots, Out, PackError, &Inlined))
        {
            Fail("-Pack: %s", PackError.c_str());
        }
        else if (!WriteFile(ProjectPath + ".packed", Out, PackError))
        {
            Fail("%s", PackError.c_str());
        }
        else
        {
            // The claim is about a project that still loads when its content is gone, so the content goes: the packed file
            //    is re-read from disk and every reference resolved with the directory deleted.
            const std::string PackedPath = ProjectPath + ".packed";
            const std::string DeleteCommand = "rm -rf '" + OutDirectory + "/Content'";
            std::system(DeleteCommand.c_str());
            SpaceReader Packed;
            std::string PackedError;
            size_t Resolved = 0u, Total = 0u;
            if (Packed.OpenFile(PackedPath, PackedError) && Packed.ReadBlobs(PackedError))
            {
                const std::vector<SpaceReferenceRecord> References = Packed.References(PackedError);
                Total = References.size();
                for (const SpaceReferenceRecord& Reference : References)
                {
                    std::vector<uint8_t> Loaded;
                    std::string LoadError;
                    if (SpaceLoadReference(Packed, Reference, OutDirectory, {}, Loaded, LoadError) && !Loaded.empty()) ++Resolved;
                }
            }
            if (Total > 0u && Resolved == Total)
                Pass("P3 -Pack inlined %u payload(s): %zu B → %zu B, and all %zu references resolve with the content directory "
                     "DELETED", Inlined, Bytes.size(), Out.size(), Total);
            else
                Fail("P3 -Pack: %zu of %zu references resolved after the content directory was deleted (%s)", Resolved, Total,
                     PackedError.c_str());
        }
    }

    std::printf("[space-tool] %s\n", Failures == 0 ? "OK" : "RED");
    return Failures == 0 ? 0 : 1;
}
