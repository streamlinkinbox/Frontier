//============================================================================================================================================
//                                          📦 Engine/ContentInterchange/SpaceCodec.cpp — P1/P3
//============================================================================================================================================
// 🧩 The container's reader and writer. See SpaceFormat.h for the bytes and the plan divergences; see SpaceCodec.h for
//    the API's shape and for what `Pack`/`Explode` promise (round trip byte for byte, both directions).
#include "SpaceCodec.h"

#include <algorithm>
#include <cstdlib>

namespace Frontier
{
namespace
{
    void AppendU32(std::vector<uint8_t>& Out, uint32_t Value)
    {
        const uint8_t* Bytes = reinterpret_cast<const uint8_t*>(&Value);
        Out.insert(Out.end(), Bytes, Bytes + sizeof(Value));
    }

    void AppendBytes(std::vector<uint8_t>& Out, const void* Data, size_t Bytes)
    {
        if (Bytes == 0u) return;
        const uint8_t* BytesPtr = static_cast<const uint8_t*>(Data);
        Out.insert(Out.end(), BytesPtr, BytesPtr + Bytes);
    }

    void AppendAligned(std::vector<uint8_t>& Out, const void* Data, size_t Bytes)
    {
        AppendBytes(Out, Data, Bytes);
        while ((Out.size() % kSpaceAlign) != 0u) Out.push_back(0u);
    }

    template <typename T>
    void AppendRow(std::vector<uint8_t>& Out, const T& Row)
    {
        static_assert(std::is_trivially_copyable_v<T>, "a table row is written as bytes");
        AppendBytes(Out, &Row, sizeof(T));
    }

    uint32_t ReadU32(const std::vector<uint8_t>& In, size_t At, bool& OutOk)
    {
        uint32_t Value = 0u;
        if (At + sizeof(Value) > In.size()) { OutOk = false; return 0u; }
        std::memcpy(&Value, In.data() + At, sizeof(Value));
        return Value;
    }

    template <typename T>
    bool ReadRow(const std::vector<uint8_t>& In, size_t At, T& Out)
    {
        static_assert(std::is_trivially_copyable_v<T>, "a table row is read as bytes");
        if (At + sizeof(T) > In.size()) return false;
        std::memcpy(&Out, In.data() + At, sizeof(T));
        return true;
    }
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TWO HASHES
//------------------------------------------------------------------------------------------------------------------------
// FNV-1a, the plan's choice: 32 for a table checksum (what sfnt's record carries) and 64 for a content address. One
//    implementation, so a payload's checksum and its hash can never be computed by two different algorithms.
namespace
{
    constexpr uint32_t kFnv32Basis = 2166136261u;
    constexpr uint32_t kFnv32Prime = 16777619u;
    constexpr uint64_t kFnv64Basis = 1469598103934665603ull;
    constexpr uint64_t kFnv64Prime = 1099511628211ull;
}

uint32_t SpaceHash32(const void* Data, size_t Bytes) noexcept
{
    const uint8_t* BytesPtr = static_cast<const uint8_t*>(Data);
    uint32_t Hash = kFnv32Basis;
    for (size_t I = 0u; I < Bytes; ++I)
    {
        Hash ^= BytesPtr[I];
        Hash *= kFnv32Prime;
    }
    return Hash;
}

uint64_t SpaceHash64(const void* Data, size_t Bytes) noexcept
{
    const uint8_t* BytesPtr = static_cast<const uint8_t*>(Data);
    uint64_t Hash = kFnv64Basis;
    for (size_t I = 0u; I < Bytes; ++I)
    {
        Hash ^= BytesPtr[I];
        Hash *= kFnv64Prime;
    }
    return Hash;
}

const SpaceFileType* FindSpaceFileType(const SpaceTag& Tag) noexcept
{
    for (const SpaceFileType& Type : kSpaceFileTypes) if (Type.Tag == Tag) return &Type;
    return nullptr;
}

const SpaceFileType* FindSpaceFileTypeByExtension(const char* Extension) noexcept
{
    if (!Extension) return nullptr;
    for (const SpaceFileType& Type : kSpaceFileTypes)
        if (std::strcmp(Type.Extension, Extension) == 0) return &Type;
    return nullptr;
}

void SpaceSetName(char* Field, size_t Bytes, const std::string& Value) noexcept
{
    std::memset(Field, 0, Bytes);
    const size_t Copy = std::min(Bytes - 1u, Value.size());
    std::memcpy(Field, Value.data(), Copy);
}

std::string SpaceGetName(const char* Field, size_t Bytes) noexcept
{
    size_t Length = 0u;
    while (Length < Bytes && Field[Length] != '\0') ++Length;
    return std::string(Field, Field + Length);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE WRITER
//------------------------------------------------------------------------------------------------------------------------
bool SpaceWriter::Begin(const SpaceTag& TypeTag, uint32_t TypeRevision, const SpaceMetaRecord& Meta) noexcept
{
    Type = TypeTag;
    Revision = TypeRevision;
    PackPolicy = Meta.PackPolicy;
    Tables.clear();
    Payloads.clear();
    // TYPE and META come first, always, in that order: every reader looks at them first, and a deterministic order is
    //    half of what makes Pack(Explode(X)) == X byte-for-byte rather than "nearly".
    // ⚠️ They are TABLES like any other — registered through EnsureTable, not pushed straight into the payload array.
    //    Appending a payload without its directory entry writes bytes no reader can find: the file parses as "no TYPE
    //    table" and every container this writer produces is unreadable.
    {
        bool Fresh = false;
        const uint32_t PayloadIndex = EnsureTable(kTagType, Fresh).Payload;
        SpaceTypeRecord Record{};
        Record.Tag = TypeTag;
        Record.Revision = TypeRevision;
        AppendRow(Payloads[PayloadIndex], Record);
    }
    {
        // The fixed part, then the source list — strings that were accounted for in the record's own SourceCount.
        bool Fresh = false;
        const uint32_t PayloadIndex = EnsureTable(kTagMeta, Fresh).Payload;
        std::vector<uint8_t>& Payload = Payloads[PayloadIndex];
        AppendRow(Payload, Meta);
        for (size_t I = 0u; I < Sources.size(); ++I)
        {
            const std::string& Source = Sources[I];
            AppendAligned(Payload, Source.c_str(), Source.size() + 1u);
        }
    }
    return true;
}

SpaceWriter::Table& SpaceWriter::EnsureTable(const SpaceTag& Tag, bool& OutFresh)
{
    for (Table& Existing : Tables) if (Existing.Tag == Tag) { OutFresh = false; return Existing; }
    Table Created{};
    Created.Tag = Tag;
    Created.Payload = static_cast<uint32_t>(Payloads.size());   // the index THIS table's payload is about to have
    Payloads.push_back({});
    Tables.push_back(Created);
    OutFresh = true;
    return Tables.back();
}

void SpaceWriter::BeginTable(const SpaceTag& Tag) noexcept
{
    bool Fresh = false;
    Table& Target = EnsureTable(Tag, Fresh);
    // Re-beginning a table starts it over; the count placeholder is laid down by the first row, so a table's bytes
    //    depend only on the rows it ends up with and not on how many times it was entered.
    (void)Fresh;
    Payloads[Target.Payload].clear();
    ActivePayload = Target.Payload;
    ActiveRows = 0u;
    ActiveCounted = false;
    ActiveCountAt = 0u;
    ActiveStrings.clear();
}

void SpaceWriter::WriteRow(const void* Row, size_t Bytes) noexcept
{
    if (ActivePayload == kNoPayload || Bytes == 0u) return;
    std::vector<uint8_t>& Payload = Payloads[ActivePayload];
    if (!ActiveCounted)
    {
        // The writer's convention: an u32 count first, then the rows. Every table in the family obeys it, which is what
        //    lets a reader hand a payload to a typed accessor without knowing the table's meaning.
        ActiveCountAt = static_cast<uint32_t>(Payload.size());
        AppendU32(Payload, 0u);
        ActiveCounted = true;
    }
    AppendBytes(Payload, Row, Bytes);
    ++ActiveRows;
}

void SpaceWriter::WriteBytes(const void* Data, size_t Bytes) noexcept
{
    if (ActivePayload == kNoPayload) return;
    AppendAligned(Payloads[ActivePayload], Data, Bytes);
}

void SpaceWriter::WriteRowWithString(const void* Row, size_t Bytes, size_t FieldOffset, const std::string& Value) noexcept
{
    if (ActivePayload == kNoPayload || Bytes == 0u) return;
    if (FieldOffset + sizeof(uint32_t) > Bytes)
    {
        // A field that is not there would be a silent no-op, and a reference that names nothing is exactly the bug this
        //    call exists to prevent. The gate's static_asserts are the better place to catch it; this is the backstop.
        return;
    }
    // ⚠️ The field's position is measured AFTER the row is in the payload, not before: WriteRow lays the table's count
    //    placeholder down first when this is the table's first row, so a position computed beforehand lands four bytes
    //    early — inside the row, typically on the type tag. That corruption reads back as a reference whose path is one
    //    byte of the count, which is how this was found.
    WriteRow(Row, Bytes);
    std::vector<uint8_t>& Payload = Payloads[ActivePayload];
    const uint32_t RowAt = static_cast<uint32_t>(Payload.size() - Bytes);
    ActiveStrings.emplace_back(RowAt + static_cast<uint32_t>(FieldOffset), Value);
}

void SpaceWriter::WriteRowWithStrings(const void* Row, size_t Bytes, const size_t* FieldOffsets, const std::string* Values,
                                     size_t Count) noexcept
{
    if (ActivePayload == kNoPayload || Bytes == 0u) return;
    for (size_t I = 0u; I < Count; ++I)
        if (FieldOffsets[I] + sizeof(uint32_t) > Bytes) return;   // a field that is not there is a bug, not a no-op
    WriteRow(Row, Bytes);
    std::vector<uint8_t>& Payload = Payloads[ActivePayload];
    const uint32_t RowAt = static_cast<uint32_t>(Payload.size() - Bytes);
    for (size_t I = 0u; I < Count; ++I)
        ActiveStrings.emplace_back(RowAt + static_cast<uint32_t>(FieldOffsets[I]), Values[I]);
}

void SpaceWriter::EndTable() noexcept
{
    if (ActivePayload == kNoPayload) return;
    std::vector<uint8_t>& Payload = Payloads[ActivePayload];
    // The count is patched where the first row reserved it — and ONLY when a row reserved it. A payload the caller handed
    //    over whole (the re-emit's REFS block) carries its own count, and overwriting its first four bytes with 0 would
    //    silently truncate the table to nothing.
    if (ActiveCounted && size_t(ActiveCountAt) + sizeof(uint32_t) <= Payload.size())
    {
        uint32_t& Count = *reinterpret_cast<uint32_t*>(Payload.data() + ActiveCountAt);
        Count = ActiveRows;
    }
    // …then the string block, and the offset of each string patched into the row's path field. Offsets are payload
    //    relative: the same property SpaceBlobEntry has, and what lets a reader slice a payload out and keep reading it.
    for (const std::pair<uint32_t, std::string>& Entry : ActiveStrings)
    {
        const uint32_t StringAt = static_cast<uint32_t>(Payload.size());
        AppendBytes(Payload, Entry.second.c_str(), Entry.second.size() + 1u);
        while ((Payload.size() % kSpaceAlign) != 0u) Payload.push_back(0u);
        if (size_t(Entry.first) + sizeof(uint32_t) <= Payload.size())
        {
            uint32_t& Field = *reinterpret_cast<uint32_t*>(Payload.data() + Entry.first);
            Field = StringAt;
        }
    }
    ActiveStrings.clear();
    ActivePayload = kNoPayload;
    ActiveRows = 0u;
    ActiveCounted = false;
    ActiveCountAt = 0u;
}

uint32_t SpaceWriter::AddBlob(const std::string& Type, const std::vector<uint8_t>& Bytes) noexcept
{
    // Content-addressed dedup: the same bytes are the same blob, whether they arrived from two materials or from one
    //    material written twice. This is the plan's claim 3 in one function.
    const uint64_t Hash = SpaceHash64(Bytes.data(), Bytes.size());
    for (size_t I = 0u; I < Blobs.size(); ++I)
        if (Blobs[I].Hash == Hash) return static_cast<uint32_t>(I);
    Blobs.push_back(Blob{ Type, Bytes, Hash });
    return static_cast<uint32_t>(Blobs.size() - 1u);
}

const std::vector<uint8_t>& SpaceWriter::BlobAt(uint32_t Index) const noexcept
{
    static const std::vector<uint8_t> Empty;
    return Index < Blobs.size() ? Blobs[Index].Bytes : Empty;
}

bool SpaceWriter::Finish(std::vector<uint8_t>& Out, std::string& OutError) noexcept
{
    Out.clear();
    if (!FindSpaceFileType(Type)) { OutError = "TYPE is not a tag in the family: " + Type.Text(); return false; }
    const SpaceFileType* FileType = FindSpaceFileType(Type);
    bool HasRequired = false;
    for (const Table& Entry : Tables) if (Entry.Tag == FileType->Required) HasRequired = true;
    if (!HasRequired)
    {
        OutError = std::string("a ") + FileType->Extension + " file requires a " + FileType->Required.Text() +
                   " table and does not have one";
        return false;
    }
    // The plan's one outright prohibition: an ASSET must never carry embedded state. `.state` is the file type that exists
    //    to hold it, so the bit is legal there and refused everywhere else — by name, at write time, rather than discovered
    //    later as a saved level that replays someone's last session.
    if ((PackPolicy & kSpacePackStateEmbedded) != 0u && Type != SpaceTag("STAT"))
    {
        OutError = std::string("a ") + FileType->Extension + " file claims the state-embedding pack policy and state is "
                   "never embedded in an asset (only a .state file may carry it)";
        return false;
    }

    // The BLOB table is written LAST, over the tables, because it is what REFS/MSLT index by number — and its entries
    //    are sorted by hash so two writers that deduplicated the same payloads lay them out identically.
    if (!Blobs.empty())
    {
        bool Fresh = false;
        Table& BlobTable = EnsureTable(kTagBlob, Fresh);
        std::vector<uint32_t> Order(Blobs.size());
        for (uint32_t I = 0u; I < Blobs.size(); ++I) Order[I] = I;
        std::stable_sort(Order.begin(), Order.end(), [&](uint32_t A, uint32_t B) { return Blobs[A].Hash < Blobs[B].Hash; });

        std::vector<uint8_t> Payload;
        AppendU32(Payload, static_cast<uint32_t>(Blobs.size()));
        const uint32_t DirectoryBytes = static_cast<uint32_t>(Blobs.size()) * static_cast<uint32_t>(sizeof(SpaceBlobEntry));
        uint32_t Cursor = SpaceAlignUp(4u + DirectoryBytes);
        std::vector<SpaceBlobEntry> Entries(Blobs.size());
        for (size_t Slot = 0u; Slot < Order.size(); ++Slot)
        {
            const Blob& PayloadBlob = Blobs[Order[Slot]];
            Entries[Slot].Hash = PayloadBlob.Hash;
            Entries[Slot].Offset = Cursor;
            Entries[Slot].Length = static_cast<uint32_t>(PayloadBlob.Bytes.size());
            Cursor = SpaceAlignUp(Cursor + Entries[Slot].Length);
        }
        for (const SpaceBlobEntry& Entry : Entries) AppendRow(Payload, Entry);
        while (Payload.size() < 4u + DirectoryBytes) Payload.push_back(0u);
        while ((Payload.size() % kSpaceAlign) != 0u) Payload.push_back(0u);
        for (size_t Slot = 0u; Slot < Order.size(); ++Slot)
        {
            AppendAligned(Payload, Blobs[Order[Slot]].Bytes.data(), Blobs[Order[Slot]].Bytes.size());
        }
        Payloads[BlobTable.Payload] = std::move(Payload);
    }

    SpaceHeader Header{};
    std::memcpy(Header.Signature, kSpaceSignature, 4u);
    Header.Major = kSpaceMajor;
    Header.Minor = kSpaceMinor;
    Header.TableCount = static_cast<uint16_t>(Tables.size());
    Header.TableOffset = kSpaceHeaderBytes;

    Out.resize(kSpaceHeaderBytes);
    std::vector<SpaceTableRecord> Directory(Tables.size());
    for (size_t I = 0u; I < Tables.size(); ++I)
    {
        const std::vector<uint8_t>& Payload = Payloads[Tables[I].Payload];
        while ((Out.size() % kSpaceAlign) != 0u) Out.push_back(0u);
        Directory[I].Tag = Tables[I].Tag;
        Directory[I].Checksum = SpaceHash32(Payload.data(), Payload.size());
        Directory[I].Offset = static_cast<uint32_t>(Out.size());
        Directory[I].Length = static_cast<uint32_t>(Payload.size());
        AppendBytes(Out, Payload.data(), Payload.size());
    }
    // The directory goes at the END so a payload's offset never depends on the directory's size — the property that
    //    makes `-Explode`/`-Pack` shifts cheap, and the reason Header.TableOffset is a field rather than a constant.
    while ((Out.size() % kSpaceAlign) != 0u) Out.push_back(0u);
    Header.TableOffset = static_cast<uint32_t>(Out.size());
    std::vector<uint8_t> HeaderBytes;
    AppendRow(HeaderBytes, Header);
    std::copy(HeaderBytes.begin(), HeaderBytes.end(), Out.begin());
    for (const SpaceTableRecord& Entry : Directory) AppendRow(Out, Entry);
    while ((Out.size() % kSpaceAlign) != 0u) Out.push_back(0u);
    Header.TotalLength = static_cast<uint32_t>(Out.size());
    std::vector<uint8_t> FinalHeader;
    AppendRow(FinalHeader, Header);
    std::copy(FinalHeader.begin(), FinalHeader.end(), Out.begin());
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE READER
//------------------------------------------------------------------------------------------------------------------------
void SpaceReader::ResetState() noexcept
{
    Directory.clear();
    FailedChecksums.clear();
    TypeRecord_ = SpaceTypeRecord{};
    KnownType = nullptr;
    VersionMajor = 0u;
    VersionMinor = 0u;
    BlobPayload.clear();
    Blobs.clear();
}

bool SpaceReader::Open(const std::vector<uint8_t>& Bytes, std::string& OutError) noexcept
{
    Data = Bytes;
    ResetState();
    return Parse(OutError);
}

bool SpaceReader::OpenFile(const std::string& Path, std::string& OutError) noexcept
{
    std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
    if (!Stream) { OutError = "cannot open " + Path; return false; }
    const std::streamoff Size = Stream.tellg();
    if (Size <= 0) { OutError = Path + " is empty"; return false; }
    Data.resize(static_cast<size_t>(Size));
    Stream.seekg(0);
    Stream.read(reinterpret_cast<char*>(Data.data()), Size);
    ResetState();
    return Parse(OutError);
}

bool SpaceReader::Parse(std::string& OutError) noexcept
{
    if (Data.size() < kSpaceHeaderBytes) { OutError = "shorter than a header"; return false; }
    SpaceHeader Header{};
    std::memcpy(&Header, Data.data(), sizeof(Header));
    if (std::memcmp(Header.Signature, kSpaceSignature, 4u) != 0) { OutError = "not a Frontier Space container"; return false; }
    if (Header.Major != kSpaceMajor)
    {
        OutError = "layout revision " + std::to_string(Header.Major) + "." + std::to_string(Header.Minor) +
                   " and this reader knows " + std::to_string(kSpaceMajor) + "." + std::to_string(kSpaceMinor);
        return false;
    }
    if (Header.TotalLength != Data.size())
    {
        OutError = "the header says " + std::to_string(Header.TotalLength) + " bytes and the file is " +
                   std::to_string(Data.size()) + " — truncated or padded";
        return false;
    }
    if (Header.TableOffset + size_t(Header.TableCount) * kSpaceTableBytes > Data.size())
    {
        OutError = "the directory runs past the end of the file";
        return false;
    }
    VersionMajor = Header.Major;
    VersionMinor = Header.Minor;
    for (uint16_t I = 0u; I < Header.TableCount; ++I)
    {
        SpaceTableRecord Record{};
        std::memcpy(&Record, Data.data() + Header.TableOffset + I * kSpaceTableBytes, kSpaceTableBytes);
        if (size_t(Record.Offset) + Record.Length > Data.size())
        {
            OutError = "table " + Record.Tag.Text() + " runs past the end of the file";
            return false;
        }
        Directory.push_back(Record);
    }

    SpaceTypeRecord TypeRecord{};
    if (!ReadTable(kTagType, TypeRecord)) { OutError = "no TYPE table — not a member of the family"; return false; }
    TypeRecord_ = TypeRecord;
    const SpaceFileType* Known = FindSpaceFileType(TypeRecord.Tag);
    if (!Known)
    {
        OutError = "unknown file type " + TypeRecord.Tag.Text() + " (revision " + std::to_string(TypeRecord.Revision) + ")";
        return false;
    }
    KnownType = Known;
    if (!Find(kTagMeta))
    {
        OutError = std::string("a ") + Known->Extension + " file requires a META table and does not have one";
        return false;
    }
    if (!Find(Known->Required))
    {
        OutError = std::string("a ") + Known->Extension + " file requires a " + Known->Required.Text() +
                   " table and does not have one";
        return false;
    }
    // ⚠️ The checksum pass is deliberately LAST: a table that is missing is a better error message than a checksum that
    //    failed on a table the reader would have refused anyway.
    for (const SpaceTableRecord& Record : Directory)
    {
        const uint32_t Actual = SpaceHash32(Data.data() + Record.Offset, Record.Length);
        if (Actual != Record.Checksum)
        {
            FailedChecksums.push_back(Record.Tag.Text());
        }
    }
    return true;
}

const SpaceTableRecord* SpaceReader::Find(const SpaceTag& Tag) const noexcept
{
    for (const SpaceTableRecord& Record : Directory) if (Record.Tag == Tag) return &Record;
    return nullptr;
}

bool SpaceReader::FindChecked(const SpaceTag& Tag, const SpaceTableRecord*& Out, std::string& OutError) const noexcept
{
    for (const std::string& Failed : FailedChecksums)
        if (Failed == Tag.Text())
        {
            OutError = "table " + Tag.Text() + " failed its checksum — the payload is corrupt, and this is fatal for it";
            return false;
        }
    Out = Find(Tag);
    return true;
}

std::vector<uint8_t> SpaceReader::Payload(const SpaceTableRecord& Table) const noexcept
{
    return std::vector<uint8_t>(Data.begin() + Table.Offset, Data.begin() + Table.Offset + Table.Length);
}

uint32_t SpaceReader::RowCount(const SpaceTag& Tag, std::string& OutError) const noexcept
{
    const SpaceTableRecord* Record = nullptr;
    if (!FindChecked(Tag, Record, OutError)) return 0u;
    if (!Record) { OutError = "no " + Tag.Text() + " table"; return 0u; }
    std::vector<uint8_t> Bytes = Payload(*Record);
    bool Ok = true;
    const uint32_t Count = ReadU32(Bytes, 0u, Ok);
    if (!Ok) { OutError = Tag.Text() + " has no row count"; return 0u; }
    return Count;
}

bool SpaceReader::ReadBlobs(std::string& OutError) noexcept
{
    Blobs.clear();
    const SpaceTableRecord* Record = nullptr;
    if (!FindChecked(kTagBlob, Record, OutError)) return false;
    if (!Record) return true;   // no blobs is not an error: a constants-only material is exactly that
    BlobPayload = Payload(*Record);
    bool Ok = true;
    const uint32_t Count = ReadU32(BlobPayload, 0u, Ok);
    if (!Ok) { OutError = "BLOB has no count"; return false; }
    for (uint32_t I = 0u; I < Count; ++I)
    {
        SpaceBlobEntry Entry{};
        if (!ReadRow(BlobPayload, 4u + size_t(I) * sizeof(SpaceBlobEntry), Entry))
        {
            OutError = "BLOB's directory is short";
            return false;
        }
        if (size_t(Entry.Offset) + Entry.Length > BlobPayload.size())
        {
            OutError = "a blob entry runs past the BLOB payload";
            return false;
        }
        // ⚠️ The hash in the directory is CHECKED against the bytes, not trusted: a flipped bit in an embedded material
        //    has to be a named failure, because the alternative is rendering a corrupt material and calling it content.
        const uint64_t Actual = SpaceHash64(BlobPayload.data() + Entry.Offset, Entry.Length);
        if (Actual != Entry.Hash)
        {
            OutError = "blob " + std::to_string(I) + " does not hash to its directory entry";
            return false;
        }
        Blobs.push_back(Entry);
    }
    return true;
}

bool SpaceReader::BlobBytes(uint32_t Index, std::vector<uint8_t>& Out, std::string& OutError) const noexcept
{
    if (BlobPayload.empty() && Index < Blobs.size()) { OutError = "the blobs were never read (call ReadBlobs)"; return false; }
    if (Index >= Blobs.size()) { OutError = "blob index " + std::to_string(Index) + " is out of range"; return false; }
    Out.assign(BlobPayload.begin() + Blobs[Index].Offset, BlobPayload.begin() + Blobs[Index].Offset + Blobs[Index].Length);
    return true;
}

std::vector<SpaceReferenceRecord> SpaceReader::References(std::string& OutError) const noexcept
{
    std::vector<SpaceReferenceRecord> Out;
    const SpaceTableRecord* Record = nullptr;
    if (!FindChecked(kTagRefs, Record, OutError)) return Out;
    if (!Record) return Out;
    const std::vector<uint8_t> Bytes = Payload(*Record);
    bool Ok = true;
    const uint32_t Count = ReadU32(Bytes, 0u, Ok);
    for (uint32_t I = 0u; Ok && I < Count; ++I)
    {
        SpaceReferenceRecord Reference{};
        if (!ReadRow(Bytes, 4u + size_t(I) * sizeof(SpaceReferenceRecord), Reference)) { OutError = "REFS is short"; return Out; }
        Out.push_back(Reference);
    }
    return Out;
}

std::vector<SpaceMaterialSlot> SpaceReader::MaterialSlots(std::string& OutError) const noexcept
{
    std::vector<SpaceMaterialSlot> Out;
    const SpaceTableRecord* Record = nullptr;
    if (!FindChecked(kTagMslT, Record, OutError)) return Out;
    if (!Record) return Out;
    const std::vector<uint8_t> Bytes = Payload(*Record);
    bool Ok = true;
    const uint32_t Count = ReadU32(Bytes, 0u, Ok);
    for (uint32_t I = 0u; Ok && I < Count; ++I)
    {
        SpaceMaterialSlot Slot{};
        if (!ReadRow(Bytes, 4u + size_t(I) * sizeof(SpaceMaterialSlot), Slot)) { OutError = "MSLT is short"; return Out; }
        Out.push_back(Slot);
    }
    return Out;
}

std::vector<SpaceInstanceRow> SpaceReader::Instances(std::string& OutError) const noexcept
{
    std::vector<SpaceInstanceRow> Out;
    const SpaceTableRecord* Record = nullptr;
    if (!FindChecked(kTagInst, Record, OutError)) return Out;
    if (!Record) return Out;
    const std::vector<uint8_t> Bytes = Payload(*Record);
    bool Ok = true;
    const uint32_t Count = ReadU32(Bytes, 0u, Ok);
    for (uint32_t I = 0u; Ok && I < Count; ++I)
    {
        SpaceInstanceRow Row{};
        if (!ReadRow(Bytes, 4u + size_t(I) * sizeof(SpaceInstanceRow), Row)) { OutError = "INST is short"; return Out; }
        Out.push_back(Row);
    }
    return Out;
}

std::vector<SpaceLevelRow> SpaceReader::Levels(std::string& OutError) const noexcept
{
    std::vector<SpaceLevelRow> Out;
    const SpaceTableRecord* Record = nullptr;
    if (!FindChecked(kTagScen, Record, OutError)) return Out;
    if (!Record) return Out;
    const std::vector<uint8_t> Bytes = Payload(*Record);
    bool Ok = true;
    const uint32_t LevelCount = ReadU32(Bytes, 0u, Ok);
    for (uint32_t I = 0u; Ok && I < LevelCount; ++I)
    {
        SpaceLevelRow Row{};
        if (!ReadRow(Bytes, 4u + size_t(I) * sizeof(SpaceLevelRow), Row)) { OutError = "SCEN is short"; return Out; }
        Out.push_back(Row);
    }
    return Out;
}

std::vector<SpacePlacementRow> SpaceReader::Placements(std::string& OutError) const noexcept
{
    std::vector<SpacePlacementRow> Out;
    const SpaceTableRecord* Record = nullptr;
    if (!FindChecked(kTagScen, Record, OutError)) return Out;
    if (!Record) return Out;
    const std::vector<uint8_t> Bytes = Payload(*Record);
    bool Ok = true;
    const uint32_t LevelCount = ReadU32(Bytes, 0u, Ok);
    // ⚠️ The placement count sits AFTER the level rows, not next to the level count. SCEN's layout is
    //    [u32 levels][level rows][u32 placements][placement rows], so reading the count at offset 4 reads the LEVEL
    //    count and walks the level bytes as if they were placements.
    const size_t CountAt = 4u + size_t(LevelCount) * sizeof(SpaceLevelRow);
    const uint32_t Count = ReadU32(Bytes, CountAt, Ok);
    const size_t Rows = CountAt + sizeof(uint32_t);
    for (uint32_t I = 0u; Ok && I < Count; ++I)
    {
        SpacePlacementRow Row{};
        if (!ReadRow(Bytes, Rows + size_t(I) * sizeof(SpacePlacementRow), Row)) { OutError = "SCEN's placements are short"; return Out; }
        Out.push_back(Row);
    }
    return Out;
}

std::string SpaceReader::TableString(const SpaceTag& Tag, uint32_t Offset, std::string& OutError) const noexcept
{
    const SpaceTableRecord* Record = nullptr;
    if (!FindChecked(Tag, Record, OutError)) return {};
    if (!Record) { OutError = "no " + Tag.Text() + " table"; return {}; }
    const std::vector<uint8_t> Bytes = Payload(*Record);
    if (Offset >= Bytes.size()) { OutError = "a string offset in " + Tag.Text() + " is out of range"; return {}; }
    const char* Start = reinterpret_cast<const char*>(Bytes.data() + Offset);
    size_t Length = 0u;
    while (Offset + Length < Bytes.size() && Start[Length] != '\0') ++Length;
    return std::string(Start, Start + Length);
}

std::string SpaceReader::MetaName(std::string& OutError) const noexcept
{
    const SpaceTableRecord* Record = nullptr;
    if (!FindChecked(kTagMeta, Record, OutError) || !Record) { OutError = "no META table"; return {}; }
    SpaceMetaRecord Meta{};
    const std::vector<uint8_t> Bytes = Payload(*Record);
    if (!ReadRow(Bytes, 0u, Meta)) { OutError = "META is short"; return {}; }
    return SpaceGetName(Meta.Name, kSpaceNameBytes);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        PACK / EXPLODE — the two lossless tools
//------------------------------------------------------------------------------------------------------------------------
namespace
{
    bool ReadWholeFile(const std::string& Path, std::vector<uint8_t>& OutBytes, std::string& OutError)
    {
        std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
        if (!Stream) { OutError = "cannot read " + Path; return false; }
        const std::streamoff Size = Stream.tellg();
        if (Size < 0) { OutError = "cannot size " + Path; return false; }
        OutBytes.resize(static_cast<size_t>(Size));
        Stream.seekg(0);
        Stream.read(reinterpret_cast<char*>(OutBytes.data()), Size);
        return true;
    }

    bool WriteWholeFile(const std::string& Path, const std::vector<uint8_t>& Bytes, std::string& OutError)
    {
        const size_t Slash = Path.find_last_of('/');
        if (Slash != std::string::npos)
        {
            // The caller names a directory that may not exist yet — and "cannot write" is a worse error message than
            //    making it. mkdir -p is the portable-enough spelling (the tools are POSIX, like the shell scripts).
            const std::string Command = "mkdir -p '" + Path.substr(0u, Slash) + "'";
            if (std::system(Command.c_str()) != 0) { OutError = "cannot create " + Path.substr(0u, Slash); return false; }
        }
        std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
        if (!Stream) { OutError = "cannot write " + Path; return false; }
        Stream.write(reinterpret_cast<const char*>(Bytes.data()), std::streamsize(Bytes.size()));
        return Stream.good();
    }

    const char* ExtensionOf(const SpaceTag& Type)
    {
        const SpaceFileType* Found = FindSpaceFileType(Type);
        return Found ? Found->Extension : ".space";
    }

    // A hash in a file name has to be readable and stable: hex, no prefixes, no sign.
    std::string HexHash(uint64_t Hash)
    {
        static const char* Digits = "0123456789abcdef";
        std::string Out(16u, '0');
        for (int I = 15; I >= 0; --I) { Out[size_t(I)] = Digits[Hash & 0xFu]; Hash >>= 4u; }
        return Out;
    }
} // namespace

// ⚠️ The two tools both rewrite a container, and both have to answer the SAME question: what does a blob index mean in
//    the file being written? The rule (see SpaceEmitPlan) is that the original container's blobs come first, in their own
//    order, so an index that already pointed at one still does — and the tool's new payloads follow. Both tools therefore
//    compute their indices with the arithmetic below rather than letting the writer decide, and the writer CHECKS the
//    arithmetic instead of trusting it (`SpaceEmitWithReferences` refuses an index it did not reproduce).
bool SpacePack(const std::vector<uint8_t>& Input, const std::string& BaseDirectory,
               const std::vector<std::pair<std::string, std::string>>& Roots,
               std::vector<uint8_t>& Out, std::string& OutError, uint32_t* OutInlined) noexcept
{
    OutError.clear();
    SpaceReader Reader;
    if (!Reader.Open(Input, OutError)) return false;
    if (!Reader.ReadBlobs(OutError)) return false;

    SpaceEmitPlan Plan;
    Plan.References = Reader.References(OutError);
    if (!OutError.empty()) return false;

    uint32_t Inlined = 0u;
    for (SpaceReferenceRecord& Reference : Plan.References)
    {
        if (Reference.Mode == kSpaceRefEmbedded) continue;   // already inside: nothing to do, and its index already means it
        const std::string Path = Reader.TableString(kTagRefs, Reference.PathOffset, OutError);
        if (!OutError.empty()) return false;
        std::string Found;
        if (!SpaceResolvePath(Path, BaseDirectory, Roots, Found))
        {
            if ((Reference.Flags & kSpaceRefRequired) != 0u)
            {
                OutError = "required reference " + Reference.Type.Text() + " '" + Path + "' does not resolve (looked from " +
                           (BaseDirectory.empty() ? std::string(".") : BaseDirectory) + ")";
                return false;
            }
            continue;   // an optional reference that is not there stays a reference
        }
        std::vector<uint8_t> Payload;
        if (!ReadWholeFile(Found, Payload, OutError)) return false;

        // The index is computed by hand, so the dedup has to be the writer's dedup: the same bytes are one blob, and a
        //    payload identical to one the container already holds reuses that blob rather than adding a second copy.
        const uint64_t Hash = SpaceHash64(Payload.data(), Payload.size());
        uint32_t Index = 0xFFFFFFFFu;
        for (uint32_t I = 0u; I < Reader.BlobCount() && Index == 0xFFFFFFFFu; ++I)
            if (Reader.BlobEntry(I).Hash == Hash) Index = I;
        for (size_t I = 0u; I < Plan.ExtraBlobs.size() && Index == 0xFFFFFFFFu; ++I)
            if (SpaceHash64(Plan.ExtraBlobs[I].data(), Plan.ExtraBlobs[I].size()) == Hash)
                Index = Reader.BlobCount() + static_cast<uint32_t>(I);
        if (Index == 0xFFFFFFFFu)
        {
            Index = Reader.BlobCount() + static_cast<uint32_t>(Plan.ExtraBlobs.size());
            Plan.ExtraBlobs.push_back(Payload);
            Plan.ExtraBlobTypes.push_back(Reference.Type.Text());
        }
        Reference.BlobIndex = Index;
        Reference.Mode = kSpaceRefEmbedded;
        ++Inlined;
    }
    if (OutInlined) *OutInlined = Inlined;

    SpaceWriter Writer;
    return SpaceEmitWithReferences(Reader, Plan, Writer, Out, OutError);
}

bool SpaceExplode(const std::vector<uint8_t>& Input, const std::string& OutputDirectory,
                  std::vector<uint8_t>& Out, std::vector<std::string>& OutWritten, std::string& OutError) noexcept
{
    OutError.clear();
    SpaceReader Reader;
    if (!Reader.Open(Input, OutError)) return false;
    if (!Reader.ReadBlobs(OutError)) return false;

    SpaceEmitPlan Plan;
    Plan.References = Reader.References(OutError);
    if (!OutError.empty()) return false;
    Plan.Paths.resize(Plan.References.size());

    // Every embedded payload becomes a file named by its own content address, and its reference becomes a sibling
    //    reference to that file. The name is derived from the hash, so exploding twice produces the same names and
    //    packing those files back produces the same container — which is what makes Pack(Explode(X)) == X.
    for (size_t I = 0u; I < Plan.References.size(); ++I)
    {
        SpaceReferenceRecord& Reference = Plan.References[I];
        if (Reference.Mode != kSpaceRefEmbedded) continue;   // already a file: leave it where the author put it
        std::vector<uint8_t> Bytes;
        if (!Reader.BlobBytes(Reference.BlobIndex, Bytes, OutError)) return false;
        const std::string Name = Reference.Type.Text() + "_" + HexHash(Reference.ContentHash) + ExtensionOf(Reference.Type);
        const std::string Path = OutputDirectory.empty() ? Name : OutputDirectory + "/" + Name;
        if (!WriteWholeFile(Path, Bytes, OutError)) return false;
        OutWritten.push_back(Path);

        Reference.Mode = kSpaceRefSibling;
        Reference.BlobIndex = 0xFFFFFFFFu;   // a sibling is named by its path, not by a blob
        Plan.Paths[I] = Name;
    }
    // Nothing is left embedded, so the rewritten container has no BLOB table at all — and the pack that follows finds
    //    every payload as a file, which is exactly the state the original pack started from.
    SpaceWriter Writer;
    return SpaceEmitWithReferences(Reader, Plan, Writer, Out, OutError);
}

//------------------------------------------------------------------------------------------------------------------------
//                                       the shared re-emit both tools use
//------------------------------------------------------------------------------------------------------------------------
bool SpaceEmitWithReferences(const SpaceReader& Reader, const SpaceEmitPlan& Plan, SpaceWriter& Writer,
                             std::vector<uint8_t>& Out, std::string& OutError) noexcept
{
    OutError.clear();
    // TYPE and META are copied field for field; the source list is carried through, because it is provenance and a pack
    //    must not quietly rewrite history.
    const SpaceTableRecord* MetaTable = Reader.Find(kTagMeta);
    if (!MetaTable) { OutError = "no META table"; return false; }
    const std::vector<uint8_t> MetaBytes = Reader.Payload(*MetaTable);
    SpaceMetaRecord Meta{};
    if (MetaBytes.size() < sizeof(Meta)) { OutError = "META is short"; return false; }
    std::memcpy(&Meta, MetaBytes.data(), sizeof(Meta));
    std::vector<std::string> Sources;
    size_t At = sizeof(Meta);
    for (uint32_t I = 0u; I < Meta.SourceCount && At < MetaBytes.size(); ++I)
    {
        const std::string Source(reinterpret_cast<const char*>(MetaBytes.data() + At));
        Sources.push_back(Source);
        At += SpaceAlignUp(Source.size() + 1u);
    }
    Writer.SetSources(Sources);
    if (!Writer.Begin(Reader.TypeRecord().Tag, Reader.TypeRecord().Revision, Meta))
    {
        OutError = "the writer refused TYPE/META";
        return false;
    }

    // Every table except TYPE/META/BLOB/REFS is carried verbatim — the forward-compatibility rule is not suspended for
    //    the tools: a table neither tool understands passes through unchanged.
    for (const SpaceTableRecord& Table : Reader.Tables())
    {
        if (Table.Tag == kTagType || Table.Tag == kTagMeta || Table.Tag == kTagBlob || Table.Tag == kTagRefs) continue;
        Writer.BeginTable(Table.Tag);
        const std::vector<uint8_t> Payload = Reader.Payload(Table);
        Writer.WriteBytes(Payload.data(), Payload.size());
        Writer.EndTable();
    }

    // The original container's blobs are copied FIRST, in their own order, so a blob index that already pointed at one
    //    of them still means the same payload. This is the step whose absence used to drop every already-embedded
    //    payload the moment a pack rewrote a file — and it is why -Pack cannot lose a material that was already inside.
    for (uint32_t I = 0u; I < Reader.BlobCount(); ++I)
    {
        std::vector<uint8_t> Bytes;
        std::string BlobError;
        if (!Reader.BlobBytes(I, Bytes, BlobError)) { OutError = BlobError; return false; }
        const uint32_t Index = Writer.AddBlob(std::string(), Bytes);
        if (Index != I)
        {
            OutError = "the container's blob " + std::to_string(I) + " did not keep its index (" + std::to_string(Index) +
                       ") — its directory is not content-addressed cleanly, so rewriting it would change what its own references mean";
            return false;
        }
    }
    // …then the payloads this tool inlined, at the indices the tool computed.
    for (size_t I = 0u; I < Plan.ExtraBlobs.size(); ++I)
    {
        const uint32_t Expected = Reader.BlobCount() + static_cast<uint32_t>(I);
        const std::string Label = I < Plan.ExtraBlobTypes.size() ? Plan.ExtraBlobTypes[I] : std::string();
        const uint32_t Index = Writer.AddBlob(Label, Plan.ExtraBlobs[I]);
        if (Index != Expected)
        {
            OutError = "inlined payload " + std::to_string(I) + " landed at blob " + std::to_string(Index) + " and the "
                       "reference that asked for it says " + std::to_string(Expected);
            return false;
        }
    }

    // REFS is rebuilt from the plan's records: the rows, then the string block, with each PathOffset patched to where its
    //    text landed. A record whose path the plan leaves empty keeps the text the original container had (a moved-mode
    //    reference must not lose the file it names), and an embedded row has no text at all.
    const bool HadReferences = Reader.Find(kTagRefs) != nullptr;
    if (HadReferences || !Plan.References.empty())
    {
        Writer.BeginTable(kTagRefs);
        std::vector<std::string> Paths(Plan.References.size());
        for (size_t I = 0u; I < Plan.References.size(); ++I)
        {
            if (!Plan.Paths.empty() && I < Plan.Paths.size() && !Plan.Paths[I].empty()) { Paths[I] = Plan.Paths[I]; }
            else if (Plan.References[I].Mode != kSpaceRefEmbedded)
            {
                Paths[I] = Reader.TableString(kTagRefs, Plan.References[I].PathOffset, OutError);
                if (!OutError.empty()) return false;
            }
        }

        std::vector<SpaceReferenceRecord> Records = Plan.References;
        for (size_t I = 0u; I < Records.size(); ++I) Records[I].PathOffset = 0u;
        const size_t BlockHeader = 4u + Records.size() * sizeof(SpaceReferenceRecord);
        std::vector<uint8_t> Block(BlockHeader, 0u);
        std::vector<uint8_t> Strings;
        for (size_t I = 0u; I < Records.size(); ++I)
        {
            if (Records[I].Mode == kSpaceRefEmbedded) continue;
            Records[I].PathOffset = static_cast<uint32_t>(BlockHeader + Strings.size());
            Strings.insert(Strings.end(), Paths[I].begin(), Paths[I].end());
            Strings.push_back('\0');
            while ((Strings.size() % kSpaceAlign) != 0u) Strings.push_back(0u);
        }
        const uint32_t Count = static_cast<uint32_t>(Records.size());
        std::memcpy(Block.data(), &Count, sizeof(Count));
        for (size_t I = 0u; I < Records.size(); ++I)
            std::memcpy(Block.data() + 4u + I * sizeof(SpaceReferenceRecord), &Records[I], sizeof(SpaceReferenceRecord));
        Block.insert(Block.end(), Strings.begin(), Strings.end());
        Writer.WriteBytes(Block.data(), Block.size());
        Writer.EndTable();
    }

    if (!Writer.Finish(Out, OutError)) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              THE RESOLVER (P3)
//------------------------------------------------------------------------------------------------------------------------
// The plan's order, exactly: Embedded → Sibling → ProjectContent → EngineContent → ExternalSpace → error naming the
//    path it looked for. A mode ≥ 1 record is looked up in every root rather than only in the one its mode names,
//    because the mode says where the file PREFERS to be, not where it must be — and the error lists every root tried.
bool SpaceResolvePath(const std::string& Path, const std::string& BaseDirectory,
                      const std::vector<std::pair<std::string, std::string>>& Roots,
                      std::string& OutFound) noexcept
{
    if (Path.empty()) return false;
    const auto Exists = [](const std::string& Candidate) {
        std::ifstream Stream(Candidate, std::ios::binary);
        return Stream.good();
    };
    if (!BaseDirectory.empty())
    {
        std::string Candidate = BaseDirectory + "/" + Path;
        if (Exists(Candidate)) { OutFound = Candidate; return true; }
    }
    if (Exists(Path)) { OutFound = Path; return true; }
    for (const auto& Root : Roots)
    {
        if (Root.second.empty()) continue;
        std::string Candidate = Root.second + "/" + Path;
        if (Exists(Candidate)) { OutFound = Candidate; return true; }
    }
    OutFound.clear();
    return false;
}

bool SpaceLoadReference(const SpaceReader& Reader, const SpaceReferenceRecord& Reference,
                        const std::string& BaseDirectory,
                        const std::vector<std::pair<std::string, std::string>>& Roots,
                        std::vector<uint8_t>& OutBytes, std::string& OutError) noexcept
{
    // ① Embedded wins when it is there, and it is the authority when the record says so.
    if (Reference.Mode == kSpaceRefEmbedded || (Reference.Flags & kSpaceRefPreferEmbedded) != 0u)
    {
        std::vector<uint8_t> Bytes;
        std::string BlobError;
        if (Reader.BlobBytes(Reference.BlobIndex, Bytes, BlobError))
        {
            const uint64_t Hash = SpaceHash64(Bytes.data(), Bytes.size());
            if (Hash != Reference.ContentHash)
            {
                OutError = "the embedded payload for " + Reference.Type.Text() + " hashes to " + std::to_string(Hash) +
                           " and the reference says " + std::to_string(Reference.ContentHash);
                return false;
            }
            OutBytes = std::move(Bytes);
            return true;
        }
    }

    // ② …then the file it names, in the root order the plan fixes.
    const std::string Path = Reference.Mode == kSpaceRefEmbedded ? std::string()
                             : Reader.TableString(kTagRefs, Reference.PathOffset, OutError);
    std::string Found;
    if (!Path.empty() && SpaceResolvePath(Path, BaseDirectory, Roots, Found))
    {
        std::ifstream Stream(Found, std::ios::binary | std::ios::ate);
        if (Stream)
        {
            const std::streamoff Size = Stream.tellg();
            OutBytes.resize(static_cast<size_t>(Size));
            Stream.seekg(0);
            Stream.read(reinterpret_cast<char*>(OutBytes.data()), Size);
            return true;
        }
    }

    OutError = "could not resolve " + Reference.Type.Text() + " '" + Path + "' (looked beside " +
               (BaseDirectory.empty() ? std::string(".") : BaseDirectory);
    for (const auto& Root : Roots) if (!Root.second.empty()) OutError += ", in " + Root.first + "=" + Root.second;
    OutError += ")";
    return false;
}
}
