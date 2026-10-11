//============================================================================================================================================
//                                        📦 Engine/ContentInterchange/SpaceExport.h — P2/P4/P6
//============================================================================================================================================
// 🧩 The WRITE side of the family: resident engine records → containers. SpaceCodec is the reader/writer of the bytes;
//    this is the layer that knows what the engine's record types are and which table each one belongs in, so no caller
//    has to re-derive "CLST rows are ClusterRecords, MESH is VertexRecords plus u32 indices" a second time.
//
// 🔑 Every function here takes RECORDS, not files and not parsed descriptions: the container stores what is resident, and
//    a load is a `memcpy`. That is the property that makes the P-gate's claim ② a memcmp rather than a parser test.
//
// ⚠️ Nothing here renders, uploads, or decides policy. In particular:
//      · a `.material` file carries the material's OWN slab range (Record.SlabOffset/SlabCount) — never the whole index;
//      · a `.projectspace`'s material slots record how the object got its material (Shared/Copied/CopyOnWrite), which is
//        the editor's provenance and not a rendering decision;
//      · an `.environment`'s sky probe is baked by the caller (the atmosphere model lives in DisplayPresentation): what
//        this file does is write the row and the blob that carry it.
#pragma once

#include "SpaceFormat.h"
#include "SpaceCodec.h"
#include "MaterialIndex.h"

#include <string>
#include <vector>

namespace Frontier
{
    struct VertexRecord;
    struct ClusterRecord;

    // The knobs every export shares. `BaseDirectory` is where sibling payloads are written/read, which is what makes an
    //    exported project relocatable: the references are relative to the file that names them.
    struct SpaceExportContext
    {
        std::string BaseDirectory;                       // "" = the container is self-contained
        uint32_t    PackPolicy = kSpacePackEmbedSmall;   // META's policy — recorded, so a file answers for itself
        std::string Exporter = "SlateEditor";
        std::string BuildConfig = "Development";
    };

    // ── P2: geometry, materials, instances ───────────────────────────────────────────────────────────────────────────
    // `MESH` = u32 vertex count, u32 index count, VertexRecord[], u32[]; `CLST` = ClusterRecord rows. One object per file:
    //    an object's identity is its file, and dedup happens at the reference (N instances, one geometry).
    [[nodiscard]] bool SpaceExportGeometry(const SpaceExportContext& Context, const std::string& Name,
                                           const VertexRecord* Vertices, uint32_t VertexCount,
                                           const uint32_t* Indices, uint32_t IndexCount,
                                           const ClusterRecord* Clusters, uint32_t ClusterCount,
                                           std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept;

    // One `.material` per material: the MaterialRecord, its own slabs, and one TEXR row per bound texture index with the
    //    blob that carries it (0xFFFFFFFF = the slot is not bound). MaterialId is the index into the MaterialIndex.
    [[nodiscard]] bool SpaceExportMaterial(const SpaceExportContext& Context, const MaterialIndex& Materials, uint32_t MaterialId,
                                           const std::string& Name, std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept;

    // One `.instance` per placed object: INST rows + MSLT rows (+ the REFS rows those two index). The material mode is
    //    the caller's provenance decision and is what §10 q4's default (CopyOnWrite) is stated as.
    [[nodiscard]] bool SpaceExportInstanceFile(const SpaceExportContext& Context, const std::string& Name,
                                               const SpaceInstanceRow* Rows, uint32_t RowCount,
                                               const SpaceMaterialSlot* Slots, uint32_t SlotCount,
                                               const SpaceReferenceRecord* References, uint32_t ReferenceCount,
                                               std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept;

    // ── P4: a level's project file, assembled from the records above ─────────────────────────────────────────────────
    // The builder exists so a caller adds RECORDS and gets a container: indices into REFS/MSLT/INST are the builder's
    //    business, and an object that shares a geometry shares its reference — which is the dedup claim, structurally.
    class SpaceProjectBuilder
    {
    public:
        explicit SpaceProjectBuilder(SpaceExportContext Context) noexcept;

        // A sibling reference names a file; an embedded one carries its bytes. Both return the row index a caller stores
        //    in an instance's GeometryRef. The same path (or the same bytes) added twice returns the SAME index.
        uint32_t AddSiblingReference(const SpaceTag& Type, const std::string& Path, uint64_t ContentHash, bool Required) noexcept;
        uint32_t AddEmbeddedReference(const SpaceTag& Type, const std::vector<uint8_t>& Bytes, bool Required) noexcept;

        // The material slot an instance points at. `Mode` is SpaceMaterialMode; Shared names a path, Copied/CoW a blob.
        uint32_t AddMaterialSlot(uint8_t Mode, uint64_t MaterialHash, uint32_t BlobIndex, const std::string& Path) noexcept;

        void AddInstance(const SpaceInstanceRow& Row) noexcept;
        void AddLevel(const SpaceLevelRow& Row) noexcept;
        void AddCamera(const SpaceCameraRow& Row) noexcept;
        void AddLuminaire(const SpaceLuminaireRow& Row) noexcept;
        void AddLuminaireAlias(const SpaceLuminaireAliasRow& Row) noexcept;

        [[nodiscard]] uint32_t ReferenceCount() const noexcept { return static_cast<uint32_t>(References.size()); }
        [[nodiscard]] uint32_t MaterialSlotCount() const noexcept { return static_cast<uint32_t>(Slots.size()); }
        [[nodiscard]] uint32_t InstanceCount() const noexcept { return static_cast<uint32_t>(Instances.size()); }

        [[nodiscard]] bool Finish(const std::string& Name, std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept;

    private:
        SpaceExportContext Context;
        std::vector<SpaceReferenceRecord> References;
        std::vector<std::string>          ReferencePaths;
        std::vector<std::vector<uint8_t>> EmbeddedBytes;   // parallel to References; empty for a sibling reference
        std::vector<SpaceMaterialSlot>    Slots;
        std::vector<std::string>          SlotPaths;
        std::vector<SpaceInstanceRow>     Instances;
        std::vector<SpaceLevelRow>        Levels;
        std::vector<SpaceCameraRow>       Cameras;
        std::vector<SpaceLuminaireRow>    Luminaires;
        std::vector<SpaceLuminaireAliasRow> LuminaireAliases;
    };

    // ── P5/P6: the rest of the family, one row per entry ─────────────────────────────────────────────────────────────
    // `.runtime` is the resolved launch configuration: the command line that produced it, plus references to what it opens.
    [[nodiscard]] bool SpaceExportRuntime(const SpaceExportContext& Context, const std::string& Name, const std::string& CommandLine,
                                          const std::vector<std::pair<SpaceTag, std::string>>& Opens,
                                          std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept;

    // `.environment` — the staging (sun hour, fog, atmosphere, moon) and, when the caller has one, the baked sky probe.
    [[nodiscard]] bool SpaceExportEnvironment(const SpaceExportContext& Context, const SpaceEnvironmentRow& Row, const std::string& Name,
                                              const std::vector<uint8_t>& SkyProbe, uint32_t SkyProbeLevels,
                                              std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept;

    // `.uvspace` — the atlas: one island row per island, over the geometry's own vertex range.
    [[nodiscard]] bool SpaceExportUvSpace(const SpaceExportContext& Context, const std::string& Name,
                                          const SpaceUvIslandRow* Islands, uint32_t IslandCount,
                                          std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept;

    // `.pigment` — a painted channel: the channel rows, and one blob per layer's stroke list.
    [[nodiscard]] bool SpaceExportPigment(const SpaceExportContext& Context, const std::string& Name,
                                          const SpacePigmentRow* Channels, uint32_t ChannelCount,
                                          const SpacePigmentLayerRow* Layers, uint32_t LayerCount,
                                          const std::vector<std::vector<uint8_t>>& StrokeBlobs,
                                          std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept;

    // `.workflow` — the import → bake → validate → package steps (§6's own list), each naming its operand and its files.
    [[nodiscard]] bool SpaceExportWorkflow(const SpaceExportContext& Context, const std::string& Name,
                                           const std::vector<std::string>& StepNames, const std::vector<std::string>& StepOperands,
                                           std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept;

    // `.archive` — a content-addressed archive over the files a caller names: one BLOB per file, one ARCH row per blob.
    [[nodiscard]] bool SpaceExportArchive(const SpaceExportContext& Context, const std::string& Name,
                                          const std::vector<std::string>& Paths,
                                          std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept;
}
