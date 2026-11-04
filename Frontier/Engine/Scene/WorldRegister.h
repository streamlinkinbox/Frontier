/*============================================================================================================================================
                                                             WORLDREGISTER.H
============================================================================================================================================*/
// 🧩 WorldRegister: central running inventory of every object in the world — scene records, physics bodies, vehicle
//    proxies, force sources — with live position bookmarks and revisions; for now the register stores itself as the
//    markdown document `a.md` (portable, diffable, human-readable) via ExportMarkdown.

#pragma once

#include "Engine/Scene/RecordToken.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    REGISTER ENTRY TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Coarse object category: what kind of world object this entry tracks.
enum class WorldObjectCategory : uint8_t
{
    SceneRecord  = 0,   // [-] SceneWorld record (any typed / composable record)
    PhysicsBody  = 1,   // [-] adapter-world rigid body (dynamic prop, static bound, fracture bound)
    VehicleProxy = 2,   // [-] vehicle solver with its kinematic collision proxy
    ForceSource  = 3    // [-] force source contributing to the field
};

// 📝 One tracked object. Position is world metres; Revision advances on every Touch (content or transform edit).
struct WorldRegisterEntry
{
    uint64_t           Identity = 0;             // [-] register-issued identity (dense, never reused)
    RecordToken        Record{};                 // [-] scene record identity when Category = SceneRecord
    uint64_t           ForeignIdentity = 0;      // [-] adapter body token / host identity for non-record categories
    WorldObjectCategory Category = WorldObjectCategory::SceneRecord;
    std::string        TypeIdentifier;           // [-] e.g. "fracture.static", "vehicle.gt04", "jolt.body"
    std::string        DisplayName;
    std::string        Note;                     // [-] free-form provenance/mark
    double             Position[3] = { 0.0, 0.0, 0.0 };  // [m] world
    uint64_t           Revision = 0;             // [-]
    double             RegisteredAt = 0.0;       // [s] world time at registration
    bool               Live = true;              // [-] false after Release (entry kept for the audit trail)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      WORLD REGISTER
//------------------------------------------------------------------------------------------------------------------------

class WorldRegister
{
public:
    WorldRegister() = default;

    // 📝 Track a world object; returns the register identity (stable for the process lifetime).
    uint64_t Track(WorldObjectCategory Category,
                   std::string_view     TypeIdentifier,
                   std::string          DisplayName,
                   const double         Position[3]     = nullptr,
                   std::string_view     Note            = {},
                   RecordToken          Record          = {},
                   uint64_t             ForeignIdentity = 0);

    // 📝 Position/content updates; both bump Revision.
    bool Touch(uint64_t Identity, std::string_view Note = {});
    bool Relocate(uint64_t Identity, const double Position[3]);
    bool Release(uint64_t Identity);               // object left the world (entry retained, Live = false)

    [[nodiscard]] size_t QueryEntryCount() const noexcept { return Entries.size(); }
    [[nodiscard]] size_t QueryLiveCount() const noexcept;
    [[nodiscard]] const WorldRegisterEntry* Find(uint64_t Identity) const noexcept;
    [[nodiscard]] const std::vector<WorldRegisterEntry>& QueryEntries() const noexcept { return Entries; }

    void AdvanceWorldTime(double Seconds) noexcept { WorldTime += Seconds; }
    [[nodiscard]] double QueryWorldTime() const noexcept { return WorldTime; }

    // 📝 The whole register as one markdown document; ExportMarkdown writes it (default filename "a.md").
    [[nodiscard]] std::string ComposeMarkdown() const;
    bool ExportMarkdown(const std::filesystem::path& DocumentPath = "a.md") const;

private:
    std::vector<WorldRegisterEntry> Entries;
    uint64_t                        NextIdentity = 1;   // [-]
    double                          WorldTime    = 0.0; // [s]
};

} // namespace Frontier
