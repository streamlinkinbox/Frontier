//============================================================================================================================================
//                                          📦 Engine/ContentInterchange/SpaceFormat.h — P1
//============================================================================================================================================
// 🧩 The Space family's bytes: one container format for every file in the family, TTF-shaped (a header, a directory of
//    tagged tables, payloads), little-endian throughout. `SpaceCodec` reads and writes it; this file is only the layout,
//    the tags and the two hashes.
//
// 🔑 ONE signature for the whole family, and the file's own type in a mandatory `TYPE` table. A reader walks any file in
//    the family with the same code and then asks "do I know this type?" — which is what makes a `.geometry` and a
//    `.projectspace` share a reader, and what makes an embedded part (a whole `.material` inside a `BLOB`) free: it is
//    the same bytes as a file on disk.
//
// ⚠️ WHERE THIS DIVERGES FROM Docs/ProjectFormat.md §3, and why (the plan is not sacred, the bytes are):
//    · the header is 20 B, not 16. The fields the plan lists — signature 4, two u16 versions, a u16 count, a u32
//      directory offset, a u32 total length — are 20 bytes once they are laid out with alignment. The plan's own
//      numbers did not add up; the size that does is the size that ships.
//    · the instance row is 112 B plus a 24 B `MaterialSlot`, not 96 B containing both. The plan's field list (name 32,
//      transform 64, reference 32, slot 24, level 4, flags 4) is 160 B; splitting the row from the reference table it
//      indexes is what lets REFS/MSLT dedup at all, and it is why every reference in this format is an INDEX into a
//      table rather than an inlined path.
//    · `ReferenceRecord` keeps the plan's 32 B with two reserved u32s: it is the record every file in the family carries
//      and it will grow (a second hash, a lock id) long before the format is at 2.0.
//    Everything else is the plan's: the signature, the 16 B table record, the tag set, FNV-1a, 4-byte alignment,
//    unknown tables skipped, a checksum mismatch named and fatal.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Frontier
{
    // ── the signature, the layout revision, and the rule about it ────────────────────────────────────────────────────
    //    A reader refuses a MAJOR it does not know, by name. A MINOR it does not know is fine: the directory is the
    //    contract, and an unknown table is skipped rather than refused.
    inline constexpr char kSpaceSignature[4] = { 'F', 'S', 'P', 'C' };
    inline constexpr uint16_t kSpaceMajor = 1u;
    inline constexpr uint16_t kSpaceMinor = 0u;
    inline constexpr uint32_t kSpaceHeaderBytes = 20u;   // see the header note above
    inline constexpr uint32_t kSpaceTableBytes  = 16u;
    inline constexpr uint32_t kSpaceAlign       = 4u;    // all std430 needs; every payload offset is a multiple of it

    // ── the tags. A tag is four ASCII bytes; `SpaceTag` is the only way the code spells one, so a typo is a compile ──
    //    error in a constexpr context rather than a table nobody can find.
    struct SpaceTag
    {
        char Letters[4] = { 0, 0, 0, 0 };

        constexpr SpaceTag() noexcept = default;
        constexpr explicit SpaceTag(const char (&Text)[5]) noexcept
            : Letters{ Text[0], Text[1], Text[2], Text[3] } {}

        [[nodiscard]] constexpr bool operator==(const SpaceTag& Other) const noexcept
        {
            return Letters[0] == Other.Letters[0] && Letters[1] == Other.Letters[1] &&
                   Letters[2] == Other.Letters[2] && Letters[3] == Other.Letters[3];
        }
        [[nodiscard]] constexpr bool operator!=(const SpaceTag& Other) const noexcept { return !(*this == Other); }
        [[nodiscard]] std::string Text() const noexcept { return std::string(Letters, Letters + 4); }
    };

    // TYPE is mandatory in every file; META is mandatory in every file; the rest are the family's vocabulary.
    inline constexpr SpaceTag kTagType  = SpaceTag("TYPE");   // what this file IS: a four-byte type tag + a revision
    inline constexpr SpaceTag kTagMeta  = SpaceTag("META");   // name, exporter, revisions, build config, sources
    inline constexpr SpaceTag kTagMesh  = SpaceTag("MESH");   // VertexRecord[] + u32 indices
    inline constexpr SpaceTag kTagClst  = SpaceTag("CLST");   // ClusterRecord[] — LOD/cluster ranges
    inline constexpr SpaceTag kTagUvsp  = SpaceTag("UVSP");   // UV islands: ranges, seams, texel density
    inline constexpr SpaceTag kTagMatl  = SpaceTag("MATL");   // MaterialRecord + MaterialSlabRecord[]
    inline constexpr SpaceTag kTagPigm  = SpaceTag("PIGM");   // paint layers, channels, resolution, stroke blobs
    inline constexpr SpaceTag kTagInst  = SpaceTag("INST");   // SpaceInstanceRow[] — transform + two table indices
    inline constexpr SpaceTag kTagScen  = SpaceTag("SCEN");   // level rows + placement rows
    inline constexpr SpaceTag kTagCama  = SpaceTag("CAMA");   // SpaceCameraRow[]
    inline constexpr SpaceTag kTagLite  = SpaceTag("LITE");   // LuminaireRecord[]
    inline constexpr SpaceTag kTagEnvr  = SpaceTag("ENVR");   // world staging + terrain + the baked sky probe
    inline constexpr SpaceTag kTagMslT  = SpaceTag("MSLT");   // MaterialSlot[] — one per instance row
    inline constexpr SpaceTag kTagFlow  = SpaceTag("FLOW");   // workflow steps
    inline constexpr SpaceTag kTagStat  = SpaceTag("STAT");   // the state family's one payload {domain, revision, …}
    inline constexpr SpaceTag kTagArch  = SpaceTag("ARCH");   // archive directory rows (chunked for streaming)
    inline constexpr SpaceTag kTagRefs  = SpaceTag("REFS");   // ReferenceRecord[] + its string block
    inline constexpr SpaceTag kTagTexr  = SpaceTag("TEXR");   // texture index rows: ref slot or blob index
    inline constexpr SpaceTag kTagBlob  = SpaceTag("BLOB");   // raw payloads, addressed by 64-bit content hash

    // The family's file types. The tag is what goes in `TYPE`; the required table is what a reader insists on before it
    //    will call the file well-formed, and the extension is what a human sees (§2 of the plan).
    struct SpaceFileType
    {
        SpaceTag    Tag;             // [-] TYPE payload's first four bytes
        SpaceTag    Required;        // [-] the table a file of this type must have
        const char* Extension;       // [-] ".geometry", ".projectspace", …
        const char* Name;            // [-] "Geometry", "Project Space", …
    };

    inline constexpr SpaceFileType kSpaceFileTypes[] = {
        { SpaceTag("PROJ"), kTagScen, ".projectspace", "Project Space" },
        { SpaceTag("SOLN"), kTagMeta, ".solution",     "Solution" },
        { SpaceTag("GEOM"), kTagMesh, ".geometry",     "Geometry" },
        { SpaceTag("UVSP"), kTagUvsp, ".uvspace",      "UV Space" },
        { SpaceTag("MATL"), kTagMatl, ".material",     "Material" },
        { SpaceTag("PIGM"), kTagPigm, ".pigment",      "Pigment" },
        { SpaceTag("INST"), kTagInst, ".instance",     "Instance" },
        { SpaceTag("ENVR"), kTagEnvr, ".environment",  "Environment" },
        { SpaceTag("SCRP"), kTagMeta, ".script",       "Script" },
        { SpaceTag("FLOW"), kTagFlow, ".workflow",     "Workflow" },
        { SpaceTag("ARCH"), kTagArch, ".archive",      "Archive" },
        { SpaceTag("RUNT"), kTagMeta, ".runtime",      "Runtime" },
        { SpaceTag("STAT"), kTagStat, ".state",        "State" },
    };
    inline constexpr size_t kSpaceFileTypeCount = sizeof(kSpaceFileTypes) / sizeof(kSpaceFileTypes[0]);

    [[nodiscard]] const SpaceFileType* FindSpaceFileType(const SpaceTag& Tag) noexcept;          // by TYPE tag
    [[nodiscard]] const SpaceFileType* FindSpaceFileTypeByExtension(const char* Extension) noexcept;

    // ── the hashes. FNV-1a: 32 for a table checksum (the plan's choice — it is what sfnt's record carries), 64 for a ──
    //    content address (dedup, identity, and "same object twice" for free).
    [[nodiscard]] uint32_t SpaceHash32(const void* Data, size_t Bytes) noexcept;
    [[nodiscard]] uint64_t SpaceHash64(const void* Data, size_t Bytes) noexcept;

    // ── the header, and the record the directory is made of ──────────────────────────────────────────────────────────
#pragma pack(push, 4)
    struct SpaceHeader
    {
        char     Signature[4];    // "FSPC"
        uint16_t Major;           // layout revision — a reader refuses a major it does not know
        uint16_t Minor;
        uint16_t TableCount;
        uint16_t Reserved;        // 0
        uint32_t TableOffset;     // where the directory starts (usually kSpaceHeaderBytes)
        uint32_t TotalLength;     // self-report — a mismatch against the file's real size means a truncated file
    };
    static_assert(sizeof(SpaceHeader) == 20u, "the header is 20 B — see the divergence note at the top");

    struct SpaceTableRecord
    {
        SpaceTag Tag;
        uint32_t Checksum;        // FNV-1a 32 over the payload's bytes
        uint32_t Offset;          // byte offset into the file, 4-byte aligned
        uint32_t Length;          // payload bytes
    };
    static_assert(sizeof(SpaceTableRecord) == 16u, "the table record is the plan's 16 B sfnt record");

    // ── TYPE: what the file is. Eight bytes, and the first thing every reader looks at ───────────────────────────────
    struct SpaceTypeRecord
    {
        SpaceTag Tag;             // "MATL", "PROJ", …
        uint32_t Revision;        // schema revision of that type
    };
    static_assert(sizeof(SpaceTypeRecord) == 8u, "TYPE is a tag and a revision");

    // ── META: identity, provenance, and the pack policy the artefact answers for itself ─────────────────────────────
    inline constexpr uint32_t kSpaceNameBytes    = 64u;
    inline constexpr uint32_t kSpaceExporterBytes = 32u;
    inline constexpr uint32_t kSpaceConfigBytes  = 16u;

    // Pack policy bits (the plan's §5 defaults, recorded rather than assumed).
    enum SpacePackPolicy : uint32_t
    {
        kSpacePackEmbedSmall    = 1u << 0u,   // < 64 KB payloads are embedded
        kSpacePackEmbedAssets   = 1u << 1u,   // the packager was told -Embed=Assets
        kSpacePackEmbedFonts    = 1u << 2u,   // …-Embed=Fonts
        kSpacePackEmbedEngine   = 1u << 3u,   // …-Embed=Engine
        kSpacePackStateEmbedded = 1u << 4u    // MUST stay clear: state is never embedded in an asset
    };

    struct SpaceMetaRecord
    {
        char     Name[kSpaceNameBytes];            // the project/asset name
        char     Exporter[kSpaceExporterBytes];    // "SlateEditor", "PackProject.sh", …
        char     BuildConfig[kSpaceConfigBytes];   // "Development", "Shipping", …
        uint32_t ExporterRevision;                 // the exporter's own build number
        uint32_t LayoutRevision;                   // the layout this file was written with
        uint32_t PackPolicy;                       // SpacePackPolicy bits
        uint32_t SourceCount;                      // how many source paths follow the fixed part
        // then SourceCount NUL-terminated path strings, in order, each 4-byte aligned
    };
    static_assert(sizeof(SpaceMetaRecord) == 64u + 32u + 16u + 16u, "META's fixed part");

    // ── REFS: one record shape answers embedded, sibling, project content, engine content and external space ────────
    enum SpaceRefMode : uint8_t
    {
        kSpaceRefEmbedded      = 0u,   // the payload is a blob in this file
        kSpaceRefSibling       = 1u,   // a file next to this one
        kSpaceRefProjectContent = 2u,  // a file under the project's content root
        kSpaceRefEngineContent = 3u,   // a file under the engine's content root
        kSpaceRefExternalSpace = 4u    // anywhere else, by path
    };
    enum SpaceRefFlags : uint8_t
    {
        kSpaceRefRequired      = 1u << 0u,   // the load fails, by name, if this does not resolve
        kSpaceRefPreferEmbedded = 1u << 1u   // an embedded copy is the authority even if a sibling exists
    };

    struct SpaceReferenceRecord
    {
        SpaceTag Type;            // which member of the family this names
        uint8_t  Mode;            // SpaceRefMode
        uint8_t  Flags;           // SpaceRefFlags
        uint16_t Reserved;
        uint64_t ContentHash;     // FNV-1a 64 of the payload — dedup, integrity, and identity
        uint32_t PathOffset;      // into this table's string block (Mode ≥ 1)
        uint32_t BlobIndex;       // into this file's BLOB table (Mode = Embedded)
        uint32_t Reserved0;       // 0 — growth room, which is why this record is the plan's 32 B and not 28
        uint32_t Reserved1;       // 0
    };
    static_assert(sizeof(SpaceReferenceRecord) == 32u, "REFS records are the plan's 32 B");

    // ── MSLT: the object's ONE material, and how it got it (plan §4.2) ──────────────────────────────────────────────
    enum SpaceMaterialMode : uint8_t
    {
        kSpaceMaterialShared       = 0u,   // a .material file, referenced
        kSpaceMaterialCopied       = 1u,   // the object carries its own copy, as a nested container in a blob
        kSpaceMaterialCopyOnWrite  = 2u   // starts shared (PathOffset) and the first edit forks a blob (BlobIndex) — the default
    };
    enum SpaceMaterialFlags : uint8_t
    {
        kSpaceMaterialSharedOnly = 1u << 0u,   // never fork this material (engine content, a palette)
        kSpaceMaterialMapsEmbedded = 1u << 1u
    };

    struct SpaceMaterialSlot
    {
        uint8_t  Mode;            // 0 Shared · 1 Copied · 2 CopyOnWrite
        uint8_t  Flags;
        uint16_t Reserved;
        uint64_t MaterialHash;    // the content address of the material's bytes: identity AND dedup
        uint32_t PathOffset;      // string block (Mode = Shared / CopyOnWrite: where the shared file is)
        uint32_t BlobIndex;       // this file's BLOB table (Mode = Copied, or a CoW slot after its first fork)
        uint32_t Reserved0;       // [-] 0 — rounds the row to the plan's 24 B

    };
    static_assert(sizeof(SpaceMaterialSlot) == 24u, "MaterialSlot is the plan's 24 B");

    // ── INST: one placed object. Transform + ONE geometry reference + ONE material slot ─────────────────────────────
    enum SpaceInstanceFlags : uint32_t
    {
        kSpaceInstanceHidden      = 1u << 0u,
        kSpaceInstanceLocked      = 1u << 1u,
        kSpaceInstanceCastShadow  = 1u << 2u,
        kSpaceInstanceDynamic     = 1u << 3u,   // the object moves — D10's temporal validation keys on this
        kSpaceInstanceDefault     = kSpaceInstanceCastShadow | kSpaceInstanceDynamic
    };

    struct SpaceInstanceRow
    {
        char     Name[32];        // the outliner's label
        float    Transform[16];   // object → world, column-major (InstanceRecord's World)
        uint32_t GeometryRef;     // index into REFS
        uint32_t MaterialSlot;    // index into MSLT
        uint32_t LevelIndex;      // which SCEN level owns this row
        uint32_t Flags;           // SpaceInstanceFlags
    };
    static_assert(sizeof(SpaceInstanceRow) == 112u, "INST rows are name + transform + two indices + level + flags");

    // ── SCEN: the level table, and the placement rows it owns ───────────────────────────────────────────────────────
    struct SpaceLevelRow
    {
        char     Name[32];
        uint32_t FirstInstance, InstanceCount;
        uint32_t FirstPlacement, PlacementCount;
        uint32_t FirstCamera, CameraCount;
        uint32_t FirstLuminaire, LuminaireCount;
        uint32_t DefaultCamera;   // index into the camera rows, 0xFFFFFFFF = the file's first camera
        uint32_t Reserved;        // [-] 0 — rounds the row to 64 B

    };
    static_assert(sizeof(SpaceLevelRow) == 72u, "SCEN level rows are name + four u32 ranges + a camera index + a pad");

    // PlacementRecord is CPU-only and carries a std::string, so its on-disk form is a fixed-name row.
    struct SpacePlacementRow
    {
        char     Name[32];
        uint32_t Ancestor, FirstDescendant, NextPeer;
        float    LocalTransform[16];
        float    WorldTransform[16];
        uint32_t FirstInstance, InstanceCount;
        uint32_t Camera, Luminaire;
        uint32_t Dynamic;         // 0/1
    };
    static_assert(sizeof(SpacePlacementRow) == 32u + 12u + 64u + 64u + 8u + 8u + 4u, "placement rows");

    // ── CAMA / LITE ────────────────────────────────────────────────────────────────────────────────────────────────
    struct SpaceCameraRow
    {
        char     Name[32];
        float    VerticalFieldOfView, AspectRatio, NearPlane, FarPlane;
        uint32_t Orthographic, Reserved;
        float    OrthographicHalfHeight, Reserved1;
    };
    static_assert(sizeof(SpaceCameraRow) == 64u, "camera rows are name + six floats + two u32");

    // LITE is `PunctualLuminaireRecord` in its on-disk form — the CPU record carries a std::string and a category enum, so
    //    the fixed row names its own category as a number. The alias table follows the row table: an alias row says "this
    //    luminaire row is the same light as that one", which is how a level with forty identical downlights stores one.
    struct SpaceLuminaireRow
    {
        char     Name[32];
        float    Transform[16];     // light → world, column-major
        uint32_t Category;          // 0 directional · 1 point · 2 spot
        float    Colour[3];         // [-] linear Rec.709
        float    Intensity;         // [cd] point/spot, [lux] directional
        float    Range;             // [m] 0 = infinite
        float    InnerConeAngle;    // [rad]
        float    OuterConeAngle;    // [rad]
        uint32_t Flags;
    };
    static_assert(sizeof(SpaceLuminaireRow) == 32u + 64u + 4u + 12u + 4u * 4u + 4u, "luminaire rows");

    struct SpaceLuminaireAliasRow
    {
        uint32_t Target;            // [-] the LITE row this alias stands in for
        uint32_t Flags;
        uint32_t Reserved0, Reserved1;
    };
    static_assert(sizeof(SpaceLuminaireAliasRow) == 16u, "alias rows are 16 B");

    // ── TEXR: a texture index row. It is either a reference slot or a blob index — the same choice REFS makes ──────
    struct SpaceTextureRow
    {
        char     Uri[96];         // the human-readable name/path (may be empty when Index is enough)
        uint32_t RefIndex;        // into REFS, 0xFFFFFFFF = none
        uint32_t BlobIndex;       // into BLOB, 0xFFFFFFFF = none
        uint32_t Flags;           // bit0 = srgb, bit1 = normal map, … (the material's own slot semantics)
        uint32_t Reserved;
    };
    static_assert(sizeof(SpaceTextureRow) == 112u, "texture rows are 112 B");

    // ── UVSP / PIGM / ENVR / FLOW / STAT / ARCH: the P6 vocabulary. Each is a small fixed row per entry, and the ─────
    //    variable parts (strokes, heightfields, step operands) are blobs — so a row is always a fixed-size index into
    //    something content-addressed, which is what keeps every table in this format walkable without its payload.
    struct SpaceUvIslandRow
    {
        uint32_t FirstVertex, VertexCount;   // the island's span over the geometry's vertices
        uint32_t Seam;                        // 1 = this island's border is a UV seam
        float    TexelDensity;                // [px/m]
        float    UvBounds[4];                 // u0, v0, u1, v1 in the atlas
        uint32_t Reserved;        // [-] 0

    };
    static_assert(sizeof(SpaceUvIslandRow) == 36u, "UV island rows are two spans, a seam, a density, four bounds and a pad");

    struct SpacePigmentRow
    {
        char     Channel[16];                 // "basecolor", "roughness", "normal", …
        uint32_t Resolution[2];               // [px]
        uint32_t LayerCount;
        uint32_t FirstLayer;                  // into the layer rows that follow
        uint32_t BakedTexture;                // TEXR row this channel bakes into, 0xFFFFFFFF = not baked
    };
    static_assert(sizeof(SpacePigmentRow) == 36u, "pigment channel rows are a name, a resolution, a layer range and a bake target");

    struct SpacePigmentLayerRow
    {
        char     Name[32];
        uint32_t Blend;                       // 0 normal · 1 multiply · 2 add · …
        float    Opacity;
        uint32_t StrokeBlob;                  // BLOB index holding the stroke list, 0xFFFFFFFF = empty layer
        uint32_t StrokeCount;
    };
    static_assert(sizeof(SpacePigmentLayerRow) == 48u, "pigment layer rows are 48 B");

    struct SpaceEnvironmentRow
    {
        char     Name[32];
        float    SunHour;                     // [h] local hour the staging was authored at
        float    FogDensity;
        float    AtmosphereScale;
        float    MoonPhase;                   // [0..1]
        uint32_t TerrainRef;                  // REFS row of the heightfield's geometry, 0xFFFFFFFF = none
        uint32_t TerrainBlob;                 // BLOB index of the heightfield payload
        uint32_t SkyProbeBlob;                // BLOB index of the baked probe, 0xFFFFFFFF = not baked
        uint32_t SkyProbeLevels;              // mip levels in the probe
        uint32_t Flags;
        uint32_t Reserved;
    };
    static_assert(sizeof(SpaceEnvironmentRow) == 72u, "environment rows are a name, five staging floats and four blob/ref indices");

    struct SpaceFlowStepRow
    {
        uint32_t Kind;                        // 0 import · 1 bake · 2 validate · 3 package · 4 custom
        uint32_t ScriptRef;                   // REFS row of the step's .script, 0xFFFFFFFF = none
        uint32_t InputBlob, OutputBlob;       // BLOB indices, 0xFFFFFFFF = none
        uint32_t NameOffset;                  // into the table's string block
        uint32_t OperandOffset;               // …the step's operand string
        uint32_t Flags;
    };
    static_assert(sizeof(SpaceFlowStepRow) == 28u, "flow step rows");

    struct SpaceStateRow
    {
        char     Domain[16];                  // "session" · "work" · "save" · "capture"
        uint32_t Revision;
        uint32_t PayloadBytes;                // the payload follows the fixed part, 4-byte aligned
        uint32_t Flags;
    };
    static_assert(sizeof(SpaceStateRow) == 28u, "state rows");

    struct SpaceArchiveEntry
    {
        uint64_t Hash;                        // the content address
        uint32_t Offset, Length;              // into the archive's BLOB table
        uint32_t Flags;                       // bit0 = the last chunk of a streamed entry
        uint32_t Reserved;                    // [-] 0 — the plan's 24 B
    };
    static_assert(sizeof(SpaceArchiveEntry) == 24u, "archive directory rows are a hash, an offset/length pair and flags");

    // ── BLOB: raw payloads, addressed by content hash, deduplicated by construction ─────────────────────────────────
    //    The payload is: uint32_t Count, then Count × SpaceBlobEntry (sorted by Hash), then the blob bytes. Every
    //    Offset is relative to the payload's own start, so a table can be memcpy'd out of a file and stay valid.
    struct SpaceBlobEntry
    {
        uint64_t Hash;                        // FNV-1a 64 of the bytes
        uint32_t Offset, Length;              // relative to the BLOB payload's start
    };
    static_assert(sizeof(SpaceBlobEntry) == 16u, "blob directory rows are 16 B");

#pragma pack(pop)   // ⚠️ the whole family's records are 4-byte packed; leaving this on would re-pack every header that
                    //    follows this one in a translation unit (the engine's 16-byte-aligned records included).

    // ── "the plan said 16, the fields are 20" — checked here, once, so the divergence is a comment that cannot rot ──
    static_assert(kSpaceHeaderBytes == sizeof(SpaceHeader), "the header's size is the constant's");
    static_assert(kSpaceTableBytes == sizeof(SpaceTableRecord), "the table record's size is the constant's");

    // A string that fits a fixed row's field, copied and NUL-terminated — the only way the code writes a name field.
    void SpaceSetName(char* Field, size_t Bytes, const std::string& Value) noexcept;
    [[nodiscard]] std::string SpaceGetName(const char* Field, size_t Bytes) noexcept;

    // Align an offset up to kSpaceAlign (the padding bytes are zero, and the writer zeroes them — a deterministic file
    //    is what makes Pack(Explode(X)) == X true rather than nearly true).
    [[nodiscard]] constexpr uint32_t SpaceAlignUp(uint32_t Value) noexcept
    {
        return (Value + kSpaceAlign - 1u) / kSpaceAlign * kSpaceAlign;
    }
}
