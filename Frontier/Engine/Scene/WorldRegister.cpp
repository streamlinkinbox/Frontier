/*============================================================================================================================================
                                                            WORLDREGISTER.CPP
============================================================================================================================================*/
// 🧩 WorldRegister mechanics: dense entries, revision-bumped transforms, and the markdown store (`a.md`) with the
//    summary table.

#include "Engine/Scene/WorldRegister.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       TRACKING
//------------------------------------------------------------------------------------------------------------------------

uint64_t WorldRegister::Track(WorldObjectCategory Category,
                              std::string_view     TypeIdentifier,
                              std::string          DisplayName,
                              const double         Position[3],
                              std::string_view     Note,
                              RecordToken          Record,
                              uint64_t             ForeignIdentity)
{
    WorldRegisterEntry Entry;
    Entry.Identity        = NextIdentity++;
    Entry.Record          = Record;
    Entry.ForeignIdentity = ForeignIdentity;
    Entry.Category        = Category;
    Entry.TypeIdentifier  = std::string(TypeIdentifier);
    Entry.DisplayName     = std::move(DisplayName);
    Entry.Note            = std::string(Note);
    Entry.RegisteredAt    = WorldTime;
    if (Position != nullptr)
    {
        Entry.Position[0] = Position[0];
        Entry.Position[1] = Position[1];
        Entry.Position[2] = Position[2];
    }
    Entries.push_back(std::move(Entry));
    return Entries.back().Identity;
}

bool WorldRegister::Touch(uint64_t Identity, std::string_view Note)
{
    WorldRegisterEntry* Entry = nullptr;
    for (WorldRegisterEntry& Candidate : Entries)
        if (Candidate.Identity == Identity) { Entry = &Candidate; break; }
    if (Entry == nullptr) return false;
    if (!Note.empty()) Entry->Note = std::string(Note);
    ++Entry->Revision;
    return true;
}

bool WorldRegister::Relocate(uint64_t Identity, const double Position[3])
{
    if (Position == nullptr) return false;
    WorldRegisterEntry* Entry = nullptr;
    for (WorldRegisterEntry& Candidate : Entries)
        if (Candidate.Identity == Identity) { Entry = &Candidate; break; }
    if (Entry == nullptr) return false;
    Entry->Position[0] = Position[0];
    Entry->Position[1] = Position[1];
    Entry->Position[2] = Position[2];
    ++Entry->Revision;
    return true;
}

bool WorldRegister::Release(uint64_t Identity)
{
    WorldRegisterEntry* Entry = nullptr;
    for (WorldRegisterEntry& Candidate : Entries)
        if (Candidate.Identity == Identity) { Entry = &Candidate; break; }
    if (Entry == nullptr) return false;
    Entry->Live = false;
    ++Entry->Revision;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        QUERIES
//------------------------------------------------------------------------------------------------------------------------

size_t WorldRegister::QueryLiveCount() const noexcept
{
    std::size_t Live = 0;
    for (const WorldRegisterEntry& Entry : Entries)
        if (Entry.Live) ++Live;
    return Live;
}

const WorldRegisterEntry* WorldRegister::Find(uint64_t Identity) const noexcept
{
    for (const WorldRegisterEntry& Entry : Entries)
        if (Entry.Identity == Identity) return &Entry;
    return nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MARKDOWN STORE (`a.md`)
//------------------------------------------------------------------------------------------------------------------------

std::string WorldRegister::ComposeMarkdown() const
{
    std::ostringstream Out;
    Out << "# World Register\n\n";
    Out << "Tracked objects: " << QueryLiveCount() << " live / " << Entries.size() << " total · world time "
        << WorldTime << " s\n\n";
    Out << "| Identity | Category | Type | Name | Position [m] | Revision | Registered [s] | Live |\n";
    Out << "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
    static const char* CategoryNames[] = { "scene.record", "physics.body", "vehicle.proxy", "force.source" };
    for (const WorldRegisterEntry& Entry : Entries)
    {
        const std::size_t CategoryIndex = static_cast<std::size_t>(Entry.Category) < 4
                                              ? static_cast<std::size_t>(Entry.Category) : 0;
        char Line[512];
        std::snprintf(Line, sizeof(Line), "| %llu | %s | %s | %s | (%.3f, %.3f, %.3f) | %llu | %.3f | %s |\n",
                      static_cast<unsigned long long>(Entry.Identity), CategoryNames[CategoryIndex],
                      Entry.TypeIdentifier.c_str(), Entry.DisplayName.c_str(),
                      Entry.Position[0], Entry.Position[1], Entry.Position[2],
                      static_cast<unsigned long long>(Entry.Revision), Entry.RegisteredAt,
                      Entry.Live ? "yes" : "no");
        Out << Line;
        if (!Entry.Note.empty())
            Out << "  (note: " << Entry.Note << ")\n";
    }
    Out << "\n";
    return Out.str();
}

bool WorldRegister::ExportMarkdown(const std::filesystem::path& DocumentPath) const
{
    std::ofstream Out(DocumentPath, std::ios::binary | std::ios::trunc);
    if (!Out.is_open()) return false;
    Out << ComposeMarkdown();
    return Out.good();
}

} // namespace Frontier
