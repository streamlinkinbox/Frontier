/*==========================================================================================================================================
                                               HEADLESS PROOF FOR RECORD REGISTRATION
                                     ------------------------------------------------------------------
                  Constructor registers, destructor unregisters: store (every profile) + directory (editor machinery).
==========================================================================================================================================*/

// 🧩 Headless proof for record registration: constructor registers, destructor unregisters — store + directory.

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Scene/RecordEntry.h"
#include "Engine/Scene/RecordStore.h"
#include "Engine/Scene/SceneRecord.h"
#include "Engine/Scene/SceneDirectory.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace
{

int Failures = 0;
int Passes   = 0;

void Check(bool Condition, const char* Name)
{
    if (Condition)
    {
        ++Passes;
        std::printf("  pass  %s\n", Name);
    }
    else
    {
        ++Failures;
        std::printf("  FAIL  %s\n", Name);
    }
}

// A stand-in scene object: deriving from RecordEntry is the *only* step an engine object takes.
class TestRecord final : public Frontier::RecordEntry
{
  public:
    explicit TestRecord(Frontier::RecordClassification Classification) : RecordEntry(Classification) {}
};

static_assert(!std::is_copy_constructible_v<Frontier::RecordEntry>, "records are identity objects: no copies");
static_assert(!std::is_move_constructible_v<Frontier::RecordEntry>, "records are identity objects: no moves");

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
bool LogContainsSince(uint64_t AfterSequence, const char* Needle)
{
    using namespace Frontier;
    LogEntry  Buffer[256];
    uint64_t  Cursor  = AfterSequence;
    uint64_t  Skipped = 0;
    const uint32_t Read = LogStore().ReadSince(AfterSequence, Buffer, 256, Cursor, Skipped);
    for (uint32_t Index = 0; Index < Read; ++Index)
    {
        if (std::strstr(Buffer[Index].Message, Needle) != nullptr)
        {
            return true;
        }
    }
    return false;
}
#endif

} // namespace

int main()
{
    using namespace Frontier;

    const char* Profile = BuildConfiguration::EditorEnabled      ? "Editor"
                          : BuildConfiguration::ShippingEnabled ? "Shipping"
                                                                : "Development";
    std::printf("Scene directory validation (%s profile; editor machinery %s)\n", Profile,
                BuildConfiguration::EditorMachineryEnabled ? "on" : "OFF");

    RecordStore& Store = RecordStore::Instance();
    Check(Store.QueryCount() == 0, "store starts empty");
    Check(!RecordToken{}.IsValid(), "zero token is invalid");

    // Registration by construction, resolution by token.
    auto Vehicle = std::make_unique<TestRecord>(RecordClassification::Vehicle);
    Check(Vehicle->Token().IsValid(), "constructor issues a valid token");
    Check(Vehicle->Classification() == RecordClassification::Vehicle, "classification fixed at construction");
    Check(Store.QueryCount() == 1, "constructor inserts into the store");
    Check(Store.Find(Vehicle->Token()) == Vehicle.get(), "token resolves to the live record");
    Check(Store.Authenticate(Vehicle->Token(), *Vehicle), "token authenticates against its own record");

    auto Tyre        = std::make_unique<TestRecord>(RecordClassification::Tyre);
    auto Environment = std::make_unique<TestRecord>(RecordClassification::Environment);
    Check(Vehicle->Token() < Tyre->Token() && Tyre->Token() < Environment->Token(),
          "tokens issue monotonically (never reused)");
    Check(!Store.Authenticate(Vehicle->Token(), *Tyre), "token refuses a different record");
    Check(Store.QueryCount() == 3, "three live records in the store");

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    SceneDirectory& Directory = SceneDirectory::Instance();
    Check(Directory.QueryCount() == 3, "constructor also registers with the SceneDirectory");
    Check(Directory.QueryName(Vehicle->Token()) ==
              "Vehicle " + std::to_string(Vehicle->Token().Value),
          "default display name is '<Classification> <token>'");
    Check(Directory.ConfigureName(Tyre->Token(), "Front Left"), "directory accepts a display name");
    Check(Directory.QueryName(Tyre->Token()) == "Front Left", "configured name reads back");

    // Nesting: tyre under vehicle; cycles and self-parenting refused (and logged).
    Check(Directory.ConfigureParent(Tyre->Token(), Vehicle->Token()), "tyre nests under the vehicle");
    Check(Directory.QueryParent(Tyre->Token()) == Vehicle->Token(), "up-link reads back");
    Check(Directory.QueryChildCount(Vehicle->Token()) == 1, "vehicle reports one child");
    const uint64_t BeforeRefusals = LogStore().QueryWritten();
    Check(!Directory.ConfigureParent(Vehicle->Token(), Tyre->Token()), "cycle (vehicle under its tyre) refused");
    Check(!Directory.ConfigureParent(Vehicle->Token(), Vehicle->Token()), "self-parenting refused");
    Check(!Directory.ConfigureParent(Vehicle->Token(), RecordToken{999999}), "unknown parent refused");
    Check(LogContainsSince(BeforeRefusals, "cycle"), "cycle refusal is written to the log stream");

    // Panel projection: flattened tree, tabs, filter, inspector.
    SceneDirectoryPanel Panel;
    Panel.Synchronize(Directory);
    Check(Panel.QueryTotal() == 3 && Panel.QueryRows().size() == 3, "panel sees all three records");
    Check(Panel.QueryRows()[0].Token == Vehicle->Token() && Panel.QueryRows()[0].DepthTier == 0 &&
              Panel.QueryRows()[1].Token == Tyre->Token() && Panel.QueryRows()[1].DepthTier == 1 &&
              Panel.QueryRows()[2].Token == Environment->Token() && Panel.QueryRows()[2].DepthTier == 0,
          "rows flatten as vehicle, nested tyre, environment");
    uint32_t VehicleTab = 0, TyreTab = 0;
    for (const SceneClassificationTab& Tab : Panel.QueryTabs())
    {
        if (Tab.Classification == RecordClassification::Vehicle) VehicleTab = Tab.Count;
        if (Tab.Classification == RecordClassification::Tyre)    TyreTab    = Tab.Count;
    }
    Check(VehicleTab == 1 && TyreTab == 1, "category tabs carry live counts");
    Panel.ConfigureClassificationFilter(RecordClassification::Tyre);
    Check(Panel.QueryVisible().size() == 1 && Panel.QueryVisible()[0].Name == "Front Left",
          "classification filter shows only the tyre");
    Panel.ConfigureClassificationFilter(std::nullopt);
    Check(Panel.QueryVisible().size() == 3, "clearing the filter shows everything");
    Panel.Select(Tyre->Token());
    Panel.Synchronize(Directory);
    const SceneInspectorSummary& Inspector = Panel.QueryInspector();
    Check(Inspector.HasSelection && Inspector.Name == "Front Left" &&
              Inspector.Classification == RecordClassification::Tyre && Inspector.DepthTier == 1 &&
              Inspector.ParentName == Directory.QueryName(Vehicle->Token()) && Inspector.ChildCount == 0,
          "inspector summarises the selected tyre");

    // Visibility, folding, text filter, spawn flow (Phase 2 kit behaviours).
    Check(Directory.QueryVisible(Tyre->Token()), "records start visible");
    Check(Directory.ConfigureVisible(Tyre->Token(), false) && !Directory.QueryVisible(Tyre->Token()),
          "eye toggle hides a record without unregistering it");
    Panel.Synchronize(Directory);
    Check(!Panel.QueryRows()[1].Visible && Panel.QueryTotal() == 3, "row carries the hidden state; total unchanged");
    (void)Directory.ConfigureVisible(Tyre->Token(), true);
    Panel.ConfigureCollapsed(Vehicle->Token(), true);
    Check(Panel.QueryVisible().size() == 2 && Panel.QueryRows().size() == 3,
          "collapsing the vehicle folds its tyre out of the visible rows");
    Panel.ConfigureTextFilter("front");
    Check(Panel.QueryVisible().size() == 1 && Panel.QueryVisible()[0].Token == Tyre->Token(),
          "text search reveals the folded tyre by name (case-insensitive)");
    Panel.ConfigureTextFilter("");
    Panel.ConfigureCollapsed(Vehicle->Token(), false);
    Panel.Synchronize(Directory);
    Check(Panel.QueryVisible().size() == 3, "clearing search and fold restores the full tree");
    Panel.RequestSpawn(RecordClassification::Camera);
    const std::optional<RecordClassification> Spawn = Panel.TakeSpawnRequest();
    Check(Spawn.has_value() && *Spawn == RecordClassification::Camera && !Panel.TakeSpawnRequest().has_value(),
          "spawn request round-trips to the host exactly once");
    {
        SceneRecord Spawned(RecordClassification::Camera);
        Check(Directory.ConfigureName(Spawned.Token(), "Chase Camera") &&
                  Directory.QueryName(Spawned.Token()) == "Chase Camera",
              "host-serviced spawn appears by construction as a SceneRecord");
    }

    // Destruction unregisters everywhere; children re-root; stale selection clears.
    Vehicle.reset();
    Check(Directory.QueryParent(Tyre->Token()) == RecordToken{}, "destroyed parent's children re-root");
    Check(Directory.QueryCount() == 2, "destructor unregisters from the directory");
    Tyre.reset();
    Panel.Synchronize(Directory);
    Check(!Panel.QueryInspector().HasSelection, "destroying the selection empties the inspector");
#else
    Check(!BuildConfiguration::EditorMachineryEnabled, "Shipping compiled the directory registration out");
#endif

    // Store lifetime (every profile): destruction removes; stale tokens miss.
    Check(Store.Find(Vehicle ? Vehicle->Token() : RecordToken{1}) == nullptr || Vehicle != nullptr,
          "live lookups consistent");
    const RecordToken EnvironmentToken = Environment->Token();
    Environment.reset();
    Check(Store.Find(EnvironmentToken) == nullptr, "destructor removes from the store; stale token misses");
    Check(Store.QueryCount() == static_cast<size_t>((Vehicle ? 1 : 0) + (Tyre ? 1 : 0)),
          "store count tracks live records");
    Vehicle.reset();
    Tyre.reset();
    Check(Store.QueryCount() == 0, "store empties when the last record dies");

    // Churn: 500 construct/destroy cycles — tokens stay unique, nothing leaks in either registry.
    std::unordered_set<uint64_t> Seen;
    bool                         Unique = true;
    for (int Index = 0; Index < 500; ++Index)
    {
        TestRecord Record(RecordClassification::Geometry);
        Unique = Unique && Seen.insert(Record.Token().Value).second;
    }
    Check(Unique && Seen.size() == 500, "500 construct/destroy cycles issue 500 unique tokens");
    Check(Store.QueryCount() == 0, "no records leak under churn");
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    Check(SceneDirectory::Instance().QueryCount() == 0, "no directory entries leak under churn");
#endif

    std::printf("\nScene directory validation: %d passed, %d failed\n", Passes, Failures);
    return Failures == 0 ? 0 : 1;
}
