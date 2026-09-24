//============================================================================================================================================
//                                          📦 Engine/ContentInterchange/SpaceCodec.h — P1/P3
//============================================================================================================================================
// 🧩 Read and write a Frontier Space container. One class for the whole family: what makes a `.material` different from
//    a `.projectspace` is its TYPE tag and which tables it must have (SpaceFormat.h's registry), not a second codec.
//
// 🔑 The property everything else is built on: **a container is deterministic given its tables and blobs.** The table
//    order is insertion order with TYPE first and META second, blobs are sorted by content hash, every offset is aligned
//    and every pad byte is zero. So `Write(Read(X)) == X` byte for byte, and `Pack(Explode(X)) == X` for the same reason
//    rather than by luck — which is what the P1/P3 gate checks.
#pragma once

#include "SpaceFormat.h"

#include <climits>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace Frontier
{
    // ── writing ──────────────────────────────────────────────────────────────────────────────────────────────────────
    class SpaceWriter
    {
    public:
        static constexpr uint32_t kNoPayload = 0xFFFFFFFFu;

        // TYPE and META first (that is the order every file in the family has), then tables, then Finish().
        [[nodiscard]] bool Begin(const SpaceTag& TypeTag, uint32_t TypeRevision, const SpaceMetaRecord& Meta) noexcept;
        void SetSources(const std::vector<std::string>& InSources) noexcept { Sources = InSources; }

        // A table is a u32 count and then rows. BeginTable starts it; the FIRST WriteRow lays down the count
        //    placeholder and EndTable patches it. ⚠️ WriteBytes is the raw form: a caller that hands over a pre-built
        //    payload (rows and count together, as the re-emit does) gets no placeholder and no patch — the writer cannot
        //    tell a pre-built count from a row's first four bytes, so it does not try.
        void BeginTable(const SpaceTag& Tag) noexcept;
        void WriteRow(const void* Row, size_t Bytes) noexcept;
        template <typename T> void WriteRow(const T& Row) noexcept { WriteRow(&Row, sizeof(T)); }
        void WriteBytes(const void* Data, size_t Bytes) noexcept;
        // A table's variable part is its string block, and it comes AFTER the rows — so a row that names a file is
        //    written first (with its path field zeroed) and the string is attached to it here. EndTable appends the
        //    block and patches the field with the string's offset inside the payload.
        // 🔑 There is deliberately no "write a string now" call: a string written before its row is a payload whose
        //    first four bytes are text, and a reader reads those four bytes as the row count. That mistake has already
        //    cost this format one debugging session (see the REFS work in SpaceEmitWithReferences).
        void WriteRowWithString(const void* Row, size_t Bytes, size_t FieldOffset, const std::string& Value) noexcept;
        template <typename T> void WriteRowWithString(const T& Row, size_t FieldOffset, const std::string& Value) noexcept
        {
            WriteRowWithString(&Row, sizeof(T), FieldOffset, Value);
        }
        // The same, for a row that names two things (a workflow step names itself AND its operand).
        void WriteRowWithStrings(const void* Row, size_t Bytes, const size_t* FieldOffsets, const std::string* Values,
                                 size_t Count) noexcept;
        template <typename T> void WriteRowWithStrings(const T& Row, const size_t* FieldOffsets, const std::string* Values,
                                                       size_t Count) noexcept
        {
            WriteRowWithStrings(&Row, sizeof(T), FieldOffsets, Values, Count);
        }
        void EndTable() noexcept;

        // The content-addressed blob pool. Identical bytes are one blob, which is what makes copies free (§8 claim 8).
        uint32_t AddBlob(const std::string& Type, const std::vector<uint8_t>& Bytes) noexcept;
        [[nodiscard]] const std::vector<uint8_t>& BlobAt(uint32_t Index) const noexcept;
        [[nodiscard]] uint32_t BlobCount() const noexcept { return static_cast<uint32_t>(Blobs.size()); }

        [[nodiscard]] bool Finish(std::vector<uint8_t>& Out, std::string& OutError) noexcept;

        struct Table { SpaceTag Tag; uint32_t Payload = kNoPayload; };
        struct Blob  { std::string Type; std::vector<uint8_t> Bytes; uint64_t Hash = 0u; };

    private:
        [[nodiscard]] Table& EnsureTable(const SpaceTag& Tag, bool& OutFresh);

        SpaceTag Type{};
        uint32_t Revision = 0u;
        std::vector<Table> Tables;
        std::vector<std::vector<uint8_t>> Payloads;
        std::vector<Blob> Blobs;
        std::vector<std::string> Sources;
        uint32_t ActivePayload = kNoPayload;
        uint32_t ActiveRows = 0u;
        uint32_t ActiveCountAt = 0u;      // where the placeholder went — patched by EndTable
        bool     ActiveCounted = false;   // false until a WriteRow has reserved the count
        // (payload offset of the u32 path field, the text to append there) — resolved by EndTable
        std::vector<std::pair<uint32_t, std::string>> ActiveStrings;
        uint32_t PackPolicy = 0u;         // META's policy, kept so Finish can refuse the one combination the plan forbids
    };

    // ── reading ──────────────────────────────────────────────────────────────────────────────────────────────────────
    class SpaceReader
    {
    public:
        [[nodiscard]] bool Open(const std::vector<uint8_t>& Bytes, std::string& OutError) noexcept;
        [[nodiscard]] bool OpenFile(const std::string& Path, std::string& OutError) noexcept;

        [[nodiscard]] const std::vector<SpaceTableRecord>& Tables() const noexcept { return Directory; }
        [[nodiscard]] const SpaceTableRecord* Find(const SpaceTag& Tag) const noexcept;
        [[nodiscard]] const SpaceTypeRecord& TypeRecord() const noexcept { return TypeRecord_; }
        [[nodiscard]] const SpaceFileType*   Type() const noexcept { return KnownType; }
        [[nodiscard]] size_t ByteCount() const noexcept { return Data.size(); }
        [[nodiscard]] const std::vector<std::string>& ChecksumFailures() const noexcept { return FailedChecksums; }
        [[nodiscard]] std::vector<uint8_t> Payload(const SpaceTableRecord& Table) const noexcept;

        [[nodiscard]] uint32_t RowCount(const SpaceTag& Tag, std::string& OutError) const noexcept;
        [[nodiscard]] std::string TableString(const SpaceTag& Tag, uint32_t Offset, std::string& OutError) const noexcept;
        [[nodiscard]] std::string MetaName(std::string& OutError) const noexcept;

        [[nodiscard]] bool ReadBlobs(std::string& OutError) noexcept;
        [[nodiscard]] bool BlobBytes(uint32_t Index, std::vector<uint8_t>& Out, std::string& OutError) const noexcept;
        [[nodiscard]] uint32_t BlobCount() const noexcept { return static_cast<uint32_t>(Blobs.size()); }
        [[nodiscard]] const SpaceBlobEntry& BlobEntry(uint32_t Index) const noexcept { return Blobs[Index]; }

        [[nodiscard]] std::vector<SpaceReferenceRecord> References(std::string& OutError) const noexcept;
        [[nodiscard]] std::vector<SpaceMaterialSlot>    MaterialSlots(std::string& OutError) const noexcept;
        [[nodiscard]] std::vector<SpaceInstanceRow>     Instances(std::string& OutError) const noexcept;
        [[nodiscard]] std::vector<SpaceLevelRow>        Levels(std::string& OutError) const noexcept;
        [[nodiscard]] std::vector<SpacePlacementRow>    Placements(std::string& OutError) const noexcept;

        // A generic typed row reader, so a table this file has never heard of is still walkable by whoever wrote it.
        template <typename T>
        [[nodiscard]] std::vector<T> Rows(const SpaceTag& Tag, std::string& OutError) const noexcept
        {
            std::vector<T> Out;
            const SpaceTableRecord* Record = nullptr;
            if (!FindChecked(Tag, Record, OutError) || !Record) return Out;
            const std::vector<uint8_t> Bytes = Payload(*Record);
            if (Bytes.size() < 4u) { OutError = Tag.Text() + " has no row count"; return Out; }
            uint32_t Count = 0u;
            std::memcpy(&Count, Bytes.data(), 4u);
            for (uint32_t I = 0u; I < Count; ++I)
            {
                T Row{};
                const size_t At = 4u + size_t(I) * sizeof(T);
                if (At + sizeof(T) > Bytes.size()) { OutError = Tag.Text() + " is short"; return Out; }
                std::memcpy(&Row, Bytes.data() + At, sizeof(T));
                Out.push_back(Row);
            }
            return Out;
        }

    private:
        [[nodiscard]] bool Parse(std::string& OutError) noexcept;
        [[nodiscard]] bool FindChecked(const SpaceTag& Tag, const SpaceTableRecord*& Out, std::string& OutError) const noexcept;
        template <typename T> [[nodiscard]] bool ReadTable(const SpaceTag& Tag, T& Out) const noexcept
        {
            const SpaceTableRecord* Record = Find(Tag);
            if (!Record || Record->Length < sizeof(T)) return false;
            std::memcpy(&Out, Data.data() + Record->Offset, sizeof(T));
            return true;
        }

        std::vector<uint8_t> Data;
        std::vector<SpaceTableRecord> Directory;
        std::vector<std::string> FailedChecksums;
        SpaceTypeRecord TypeRecord_{};
        const SpaceFileType* KnownType = nullptr;
        uint16_t VersionMajor = 0u;
        uint16_t VersionMinor = 0u;
        std::vector<uint8_t> BlobPayload;
        std::vector<SpaceBlobEntry> Blobs;
        void ResetState() noexcept;
    };

    // ── the two tools (§5). Both are total: they either produce a container or an error that names the file. ─────────
    [[nodiscard]] bool SpacePack(const std::vector<uint8_t>& Input, const std::string& BaseDirectory,
                                 const std::vector<std::pair<std::string, std::string>>& Roots,
                                 std::vector<uint8_t>& Out, std::string& OutError, uint32_t* OutInlined = nullptr) noexcept;
    // Explode writes each embedded payload out as its own file and returns the container rewritten to reference them
    //    as siblings — the two halves of "an individual file, or a part of a bigger file".
    [[nodiscard]] bool SpaceExplode(const std::vector<uint8_t>& Input, const std::string& OutputDirectory,
                                    std::vector<uint8_t>& Out, std::vector<std::string>& OutWritten,
                                    std::string& OutError) noexcept;

    // What a tool hands the re-emit: the reference rows as they should appear, the file each one should name, and the
    //    payloads the tool itself inlined.
    // 🔑 BlobIndex values BELOW Reader.BlobCount() address the original container's blobs — which the re-emit copies
    //    first, in their own order, so such an index still means what it meant. Values at or above it address ExtraBlobs,
    //    in order. That one rule is what lets -Pack inline a sibling without invalidating every blob the file already had
    //    (and it is checked, not assumed: a mismatch is refused rather than written).
    struct SpaceEmitPlan
    {
        std::vector<SpaceReferenceRecord> References;     // row for row
        std::vector<std::string>          Paths;          // parallel; "" = keep the original's string for that row
        std::vector<std::vector<uint8_t>> ExtraBlobs;     // payloads the tool inlined, in the order it inlined them
        std::vector<std::string>          ExtraBlobTypes; // parallel to ExtraBlobs (a label, for diagnostics)
    };

    // The re-emit both tools share: TYPE/META carried, tables carried verbatim, REFS rewritten, blobs re-hashed and
    //    re-checked. Public because the gate uses it to prove a rewritten container is deterministic.
    [[nodiscard]] bool SpaceEmitWithReferences(const SpaceReader& Reader, const SpaceEmitPlan& Plan, SpaceWriter& Writer,
                                               std::vector<uint8_t>& Out, std::string& OutError) noexcept;

    // ── resolution (§5's order, and §5's error message) ─────────────────────────────────────────────────────────────
    //    Roots are (name, path) pairs — "ProjectContent", "EngineContent" — so the error can say WHICH root it tried.
    [[nodiscard]] bool SpaceResolvePath(const std::string& Path, const std::string& BaseDirectory,
                                        const std::vector<std::pair<std::string, std::string>>& Roots,
                                        std::string& OutFound) noexcept;

    [[nodiscard]] bool SpaceLoadReference(const SpaceReader& Reader, const SpaceReferenceRecord& Reference,
                                          const std::string& BaseDirectory,
                                          const std::vector<std::pair<std::string, std::string>>& Roots,
                                          std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept;
}
