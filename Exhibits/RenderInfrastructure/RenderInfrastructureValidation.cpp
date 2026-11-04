/*==========================================================================================================================================
                                              HEADLESS PROOF FOR THE RENDERING INFRASTRUCTURE
                                     ------------------------------------------------------------------
                                            Submission ordering, transient aliasing, threading.
==========================================================================================================================================*/

// 🧩 Headless proof for the rendering infrastructure: submission ordering, transient aliasing, threading.

#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Foundation/Math.h"
#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Rendering/BarrierDerivation.h"
#include "Engine/Rendering/InspectionUnits.h"
#include "Engine/Rendering/SubmissionOrder.h"
#include "Engine/Rendering/TransientTargetAtlas.h"
#include "Engine/Rendering/ViewportUnits.h"
#include "Projects/Project-ProvingGrounds/Source/ProvingGrounds.h"
#include "Engine/Threading/SnapshotExchange.h"
#include "Engine/Threading/WorkerPool.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <unordered_map>
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

uint32_t NextRandom(uint32_t& State)
{
    State = State * 1664525u + 1013904223u;
    return (State >> 16) & 0x7fffu;
}

Frontier::TargetTouch ReadTouch(const char* Target)
{
    return {Target, Frontier::TargetUsage::Sampled};
}

Frontier::TargetTouch WriteTouch(const char* Target)
{
    return {Target, Frontier::TargetUsage::StorageWrite};
}

Frontier::TargetTouch TouchTarget(std::string Target, Frontier::TargetUsage Usage)
{
    return {std::move(Target), Usage};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SUBMISSION ORDER
//------------------------------------------------------------------------------------------------------------------------

// Independent re-derivation of the dependency spec (scan semantics), used to cross-check the engine.
// Returns edges as (from, to) pairs the resolved order must respect.
std::vector<std::pair<uint32_t, uint32_t>> SpecEdges(const std::vector<Frontier::SubmissionUnitDeclaration>& Units)
{
    std::vector<std::pair<uint32_t, uint32_t>> Edges;
    std::unordered_map<std::string, int64_t>   LastWriter;
    std::unordered_map<std::string, std::vector<uint32_t>> Readers;

    const auto WriterOf = [&](const std::string& Target) -> int64_t {
        const auto Found = LastWriter.find(Target);
        return Found == LastWriter.end() ? -1 : Found->second;
    };

    for (uint32_t Index = 0; Index < Units.size(); ++Index)
    {
        for (const Frontier::TargetTouch& Use : Units[Index].Touches)
        {
            if (!Frontier::TargetUsageReads(Use.Usage))
            {
                continue;
            }
            const std::string& Target = Use.Target;
            const int64_t Writer = WriterOf(Target);
            if (Writer >= 0 && static_cast<uint32_t>(Writer) != Index)
            {
                Edges.emplace_back(static_cast<uint32_t>(Writer), Index);
            }
            Readers[Target].push_back(Index);
        }
        for (const Frontier::TargetTouch& Use : Units[Index].Touches)
        {
            if (!Frontier::TargetUsageWrites(Use.Usage))
            {
                continue;
            }
            const std::string& Target = Use.Target;
            const int64_t Writer = WriterOf(Target);
            if (Writer >= 0 && static_cast<uint32_t>(Writer) != Index)
            {
                Edges.emplace_back(static_cast<uint32_t>(Writer), Index);
            }
            for (uint32_t Reader : Readers[Target])
            {
                if (Reader != Index)
                {
                    Edges.emplace_back(Reader, Index);
                }
            }
            LastWriter[Target] = static_cast<int64_t>(Index);
            Readers[Target].clear();
        }
    }
    return Edges;
}

bool OrderRespects(const std::vector<uint32_t>& Order, const std::vector<std::pair<uint32_t, uint32_t>>& Edges, uint32_t Count)
{
    if (Order.size() != Count)
    {
        return false;
    }
    std::vector<uint32_t> Position(Count, 0);
    std::vector<char>     Seen(Count, 0);
    for (uint32_t At = 0; At < Order.size(); ++At)
    {
        if (Order[At] >= Count || Seen[Order[At]] != 0)
        {
            return false;
        }
        Seen[Order[At]]     = 1;
        Position[Order[At]] = At;
    }
    for (const auto& Edge : Edges)
    {
        if (Position[Edge.first] >= Position[Edge.second])
        {
            return false;
        }
    }
    return true;
}

void CheckSubmissionOrder()
{
    std::printf("[submission order]\n");
    using namespace Frontier;

    // Illumination-flavoured chain: surface -> reservoirs -> resolve, with a history import.
    const std::vector<SubmissionUnitDeclaration> Units = {
        {"TemporalReuse", {ReadTouch("ReservoirHistory"), ReadTouch("Surface"), WriteTouch("Reservoirs")}, {}},
        {"SurfaceRaster", {WriteTouch("Surface"), WriteTouch("Depth")}, {}},
        {"Resolve", {ReadTouch("Reservoirs"), ReadTouch("Surface"), WriteTouch("Radiance")}, {}},
        {"Present", {ReadTouch("Radiance"), WriteTouch("Swapchain")}, {}},
    };
    ResolvedSubmissionOrder Resolved;
    std::string             Diagnostic;
    Check(ResolveSubmissionOrder(Units, Resolved, Diagnostic), "chain resolves");
    // Program-order semantics (header contract): declaration order is submission order unless edges
    // forbid it; TemporalReuse is declared before SurfaceRaster, so its Surface read is last frame's.
    Check(Resolved.Order == std::vector<uint32_t>({0, 1, 2, 3}), "legal program order preserved");
    Check(Resolved.ImportedTargets == std::vector<std::string>({"ReservoirHistory", "Surface"}),
          "read-before-write targets reported as imported");

    // After is the explicit reorder tool: it may pull a later declaration earlier.
    {
        const std::vector<SubmissionUnitDeclaration> Reordered = {
            {"Late", {}, {"Early"}},
            {"Early", {}, {}},
        };
        Check(ResolveSubmissionOrder(Reordered, Resolved, Diagnostic) &&
              Resolved.Order == std::vector<uint32_t>({1, 0}), "After pulls its predecessor forward");
    }

    // Explicit After reorders independent units; data edges still hold.
    {
        std::vector<SubmissionUnitDeclaration> WithAfter = Units;
        WithAfter[3].After = {"SurfaceRaster"};
        Check(ResolveSubmissionOrder(WithAfter, Resolved, Diagnostic) &&
              OrderRespects(Resolved.Order, SpecEdges(WithAfter), 4), "After edges respected");
    }

    // Cycle via After is refused with the cycle named.
    {
        const std::vector<SubmissionUnitDeclaration> Cyclic = {
            {"Alpha", {WriteTouch("T1")}, {"Beta"}},
            {"Beta", {WriteTouch("T2")}, {"Alpha"}},
        };
        Check(!ResolveSubmissionOrder(Cyclic, Resolved, Diagnostic) &&
              Diagnostic.find("Alpha") != std::string::npos &&
              Diagnostic.find("Beta") != std::string::npos, "cycle refused and named");
    }

    // Unknown After + duplicate names are refused.
    {
        const std::vector<SubmissionUnitDeclaration> BadAfter = {{"Solo", {}, {"Ghost"}}};
        Check(!ResolveSubmissionOrder(BadAfter, Resolved, Diagnostic), "unknown After unit refused");
        const std::vector<SubmissionUnitDeclaration> Duplicate = {{"Twin", {}, {}}, {"Twin", {}, {}}};
        Check(!ResolveSubmissionOrder(Duplicate, Resolved, Diagnostic), "duplicate unit names refused");
    }

    // Property: 200 random declaration sets always resolve to a spec-respecting permutation.
    bool   PropertyHolds = true;
    uint32_t Random      = 2166136261u;
    for (uint32_t Trial = 0; Trial < 200 && PropertyHolds; ++Trial)
    {
        const uint32_t UnitCount = 2 + NextRandom(Random) % 11;
        std::vector<SubmissionUnitDeclaration> RandomUnits;
        for (uint32_t Index = 0; Index < UnitCount; ++Index)
        {
            SubmissionUnitDeclaration Unit;
            Unit.Name = "U" + std::to_string(Index);
            const uint32_t Reads  = NextRandom(Random) % 3;
            const uint32_t Writes = NextRandom(Random) % 3;
            for (uint32_t R = 0; R < Reads; ++R)
            {
                Unit.Touches.push_back(TouchTarget("T" + std::to_string(NextRandom(Random) % 6),
                                                   Frontier::TargetUsage::Sampled));
            }
            for (uint32_t W = 0; W < Writes; ++W)
            {
                Unit.Touches.push_back(TouchTarget("T" + std::to_string(NextRandom(Random) % 6),
                                                   Frontier::TargetUsage::StorageWrite));
            }
            RandomUnits.push_back(Unit);
        }
        ResolvedSubmissionOrder TrialOrder;
        std::string             TrialDiagnostic;
        PropertyHolds = ResolveSubmissionOrder(RandomUnits, TrialOrder, TrialDiagnostic) &&
                        OrderRespects(TrialOrder.Order, SpecEdges(RandomUnits), UnitCount);
    }
    Check(PropertyHolds, "200 random declaration sets resolve spec-exact");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TRANSIENT ATLAS
//------------------------------------------------------------------------------------------------------------------------

void CheckTransientAtlas()
{
    std::printf("[transient atlas]\n");
    using namespace Frontier;

    const std::vector<SubmissionUnitDeclaration> Units = {
        {"SurfaceRaster", {WriteTouch("Surface"), WriteTouch("Depth")}, {}},
        {"TemporalReuse", {ReadTouch("ReservoirHistory"), ReadTouch("Surface"), WriteTouch("Reservoirs")}, {}},
        {"Resolve", {ReadTouch("Reservoirs"), ReadTouch("Surface"), WriteTouch("Radiance")}, {}},
        {"Denoise", {ReadTouch("Radiance"), ReadTouch("Depth"), WriteTouch("Filtered")}, {}},
        {"Present", {ReadTouch("Filtered"), WriteTouch("Swapchain")}, {}},
    };
    ResolvedSubmissionOrder Order;
    std::string             Diagnostic;
    if (!ResolveSubmissionOrder(Units, Order, Diagnostic))
    {
        Check(false, "atlas fixture order resolves");
        return;
    }

    const std::vector<RenderTargetDeclaration> Targets = {
        {"Surface", TargetFormat::Rgba16Float, 8, 1, false, TargetUsage::Undefined},
        {"Depth", TargetFormat::Depth32Float, 8, 1, false, TargetUsage::Undefined},
        {"Reservoirs", TargetFormat::Rg32Uint, 6, 1, false, TargetUsage::Undefined},
        {"Radiance", TargetFormat::Rgba16Float, 8, 1, false, TargetUsage::Undefined},
        {"Filtered", TargetFormat::Rgba16Float, 8, 1, false, TargetUsage::Undefined},
        {"ReservoirHistory", TargetFormat::Rg32Uint, 6, 1, true, TargetUsage::StorageRead},
        {"Swapchain", TargetFormat::Bgra8Unorm, 16, 1, true, TargetUsage::Present},
    };

    ResolvedTransientAtlas Atlas;
    Check(ResolveTransientTargetAtlas(Units, Order, Targets, Atlas, Diagnostic), "atlas resolves");
    Check(Atlas.UnaliasedBytes == 64 + 32 + 48 + 64 + 64, "unaliased total counts transient targets only");
    Check(Atlas.TotalBytes < Atlas.UnaliasedBytes, "aliasing saves memory");
    Check(Atlas.Placements[5].Bin == 0xffffffffu, "persistent history never aliased");

    // Property: targets sharing a bin never have overlapping lifetimes.
    bool Disjoint = true;
    for (size_t A = 0; A < Atlas.Placements.size() && Disjoint; ++A)
    {
        for (size_t B = A + 1; B < Atlas.Placements.size(); ++B)
        {
            const AtlasPlacement& Left  = Atlas.Placements[A];
            const AtlasPlacement& Right = Atlas.Placements[B];
            if (Left.Bin == 0xffffffffu || Left.Bin != Right.Bin)
            {
                continue;
            }
            const bool Overlap = Left.FirstUse <= Right.LastUse && Right.FirstUse <= Left.LastUse;
            Disjoint           = !Overlap;
        }
    }
    Check(Disjoint, "bin-mates never overlap in submission time");

    // Refusals: unused transient, unsized used target.
    {
        std::vector<RenderTargetDeclaration> WithUnused = Targets;
        WithUnused.push_back({"Ghost", TargetFormat::R32Float, 2, 2, false, TargetUsage::Undefined});
        Check(!ResolveTransientTargetAtlas(Units, Order, WithUnused, Atlas, Diagnostic),
              "unused transient target refused");

        std::vector<RenderTargetDeclaration> Missing = Targets;
        Missing.erase(Missing.begin() + 3);   // Radiance
        Check(!ResolveTransientTargetAtlas(Units, Order, Missing, Atlas, Diagnostic),
              "used target without size refused");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THREADING
//------------------------------------------------------------------------------------------------------------------------

struct SnapshotPayload
{
    uint64_t Sequence = 0;
    uint64_t Words[8] = {};
};

SnapshotPayload MakePayload(uint64_t Sequence)
{
    SnapshotPayload Payload;
    Payload.Sequence = Sequence;
    for (uint64_t& Word : Payload.Words)
    {
        Word = Sequence * 0x9e3779b97f4a7c15ull + 0x1234567ull;
    }
    return Payload;
}

bool PayloadConsistent(const SnapshotPayload& Payload)
{
    const SnapshotPayload Expected = MakePayload(Payload.Sequence);
    return std::memcmp(&Payload, &Expected, sizeof(Payload)) == 0;
}

void CheckThreading()
{
    std::printf("[threading]\n");
    using namespace Frontier;

    // Snapshot exchange basics.
    {
        SnapshotExchange<SnapshotPayload> Exchange;
        SnapshotPayload                   Out;
        Check(!Exchange.Take(Out), "empty exchange takes nothing");
        Exchange.Publish(MakePayload(7));
        Exchange.Publish(MakePayload(9));
        Check(Exchange.Take(Out) && Out.Sequence == 9, "latest publish wins");
        Check(!Exchange.Take(Out), "consumed snapshot is not re-delivered");
    }

    // Snapshot exchange under fire: no torn payloads, monotone sequences, final value delivered.
    {
        SnapshotExchange<SnapshotPayload> Exchange;
        constexpr uint64_t                Publishes = 200000;
        std::atomic<bool>                 Consistent{true};
        std::atomic<bool>                 Monotone{true};
        std::atomic<uint64_t>             LastTaken{0};

        std::thread Producer([&]() {
            for (uint64_t Sequence = 1; Sequence <= Publishes; ++Sequence)
            {
                Exchange.Publish(MakePayload(Sequence));
            }
        });
        std::thread Consumer([&]() {
            SnapshotPayload Out;
            uint64_t        Previous = 0;
            while (Previous < Publishes)
            {
                if (!Exchange.Take(Out))
                {
                    std::this_thread::yield();
                    continue;
                }
                if (!PayloadConsistent(Out))
                {
                    Consistent.store(false);
                }
                if (Out.Sequence <= Previous)
                {
                    Monotone.store(false);
                }
                Previous = Out.Sequence;
                LastTaken.store(Out.Sequence);
            }
        });
        Producer.join();
        Consumer.join();

        Check(Consistent.load(), "no torn snapshots across 200k publishes");
        Check(Monotone.load(), "snapshot sequences strictly increase");
        Check(LastTaken.load() == Publishes, "final snapshot delivered");
    }

    // Worker pool: exact completion, reuse after idle, drain-on-destroy.
    {
        std::atomic<uint64_t> Counter{0};
        {
            WorkerPool Pool(4, "ProofPool");
            Check(Pool.QueryThreadCount() == 4, "pool starts the requested threads");
            for (uint32_t Job = 0; Job < 10000; ++Job)
            {
                Pool.Submit([&Counter]() { Counter.fetch_add(1, std::memory_order_relaxed); });
            }
            Pool.WaitIdle();
            Check(Counter.load() == 10000 && Pool.QueryCompleted() == 10000, "every job completes exactly once");

            Pool.Submit([&Counter]() { Counter.fetch_add(1, std::memory_order_relaxed); });
            Pool.WaitIdle();
            Check(Counter.load() == 10001, "pool reusable after idle");
        }

        std::atomic<uint64_t> Drained{0};
        {
            WorkerPool Pool(2, "DrainPool");
            for (uint32_t Job = 0; Job < 500; ++Job)
            {
                Pool.Submit([&Drained]() { Drained.fetch_add(1, std::memory_order_relaxed); });
            }
        }
        Check(Drained.load() == 500, "destruction drains queued jobs");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          MATH
//------------------------------------------------------------------------------------------------------------------------

bool NearlyEqual(float A, float B)
{
    const float Difference = A - B;
    return Difference > -1.0e-5f && Difference < 1.0e-5f;
}

bool NearlyEqual(Frontier::Vec3 A, Frontier::Vec3 B)
{
    return NearlyEqual(A.X, B.X) && NearlyEqual(A.Y, B.Y) && NearlyEqual(A.Z, B.Z);
}

void CheckMath()
{
    std::printf("[math]\n");
    using namespace Frontier;

    Check(NearlyEqual(TransformPoint(Mat4::Identity(), {3, -2, 7}), {3, -2, 7}), "identity leaves points alone");
    Check(NearlyEqual(TransformPoint(TranslationMatrix({1, 2, 3}), {4, 5, 6}), {5, 7, 9}), "translation offsets points");

    const Mat4 Quarter = RotationMatrix({0, 1, 0}, 1.5707963f);
    Check(NearlyEqual(TransformDirection(Quarter, {1, 0, 0}), {0, 0, -1}), "quarter turn about Y maps +X to -Z");

    const Vec3 ColumnX = TransformDirection(Quarter, {1, 0, 0});
    const Vec3 ColumnZ = TransformDirection(Quarter, {0, 0, 1});
    Check(NearlyEqual(Length(ColumnX), 1.0f) && NearlyEqual(Dot(ColumnX, ColumnZ), 0.0f),
          "rotation stays orthonormal");

    const Mat4 View = LookAtMatrix({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    Check(NearlyEqual(TransformPoint(View, {0, 0, 0}), {0, 0, -5}), "look-at puts the target five units ahead");

    const Mat4 Projection = PerspectiveMatrix(0.9f, 16.0f / 9.0f, 0.1f, 100.0f);
    Check(NearlyEqual(TransformPoint(Projection, {0, 0, -0.1f}).Z, 0.0f), "near plane lands at Vulkan depth 0");
    Check(NearlyEqual(TransformPoint(Projection, {0, 0, -100.0f}).Z, 1.0f), "far plane lands at Vulkan depth 1");
}

//------------------------------------------------------------------------------------------------------------------------
//                                            BARRIERS + FIRST VIEWPORT FRAME
//------------------------------------------------------------------------------------------------------------------------

void CheckBarriers()
{
    std::printf("[barriers + viewport frame]\n");
    using namespace Frontier;

    // The declared first frame resolves, aliases and derives a known barrier set.
    const ViewportFrameDeclaration Frame = DeclareViewportFrame(1920, 1080);
    ResolvedSubmissionOrder        Order;
    std::string                    Diagnostic;
    Check(ResolveSubmissionOrder(Frame.Units, Order, Diagnostic), "viewport frame resolves");

    ResolvedTransientAtlas Atlas;
    Check(ResolveTransientTargetAtlas(Frame.Units, Order, Frame.Targets, Atlas, Diagnostic),
          "viewport targets alias-resolve");
    Check(Atlas.BinBytes.size() == 2, "surface and depth occupy distinct-format bins");

    DerivedBarriers Barriers;
    Check(DeriveBarriers(Frame.Units, Order, Frame.Targets, Barriers, Diagnostic), "viewport barriers derive");

    uint32_t Layouts = 0;
    uint32_t Executions = 0;
    bool     SurfacePresent = false;
    bool     SwapchainToPresent = false;
    for (const DerivedBarrier& Barrier : Barriers.Barriers)
    {
        Layouts    += Barrier.Kind == BarrierKind::LayoutTransition ? 1 : 0;
        Executions += Barrier.Kind == BarrierKind::ExecutionDependency ? 1 : 0;
        SurfacePresent |= Barrier.Target == "Surface" && Barrier.From == TargetUsage::ColourWrite &&
                          Barrier.To == TargetUsage::Sampled && Barrier.BeforePosition == 1;
        SwapchainToPresent |= Barrier.Target == "Swapchain" && Barrier.To == TargetUsage::Present &&
                              Barrier.BeforePosition == 1;
    }
    Check(Layouts == 5 && Executions == 0, "viewport frame derives exactly five layout transitions");
    Check(SurfacePresent, "surface transitions write->sampled before DisplayPresent");
    Check(SwapchainToPresent, "swapchain returns to Present inside DisplayPresent");

    DumpBarriers(Barriers, Frame.Units, Order);   // evidence lines for the log-pipe check below

    // Same-usage write after write needs only an execution dependency; read after read needs none.
    {
        const std::vector<SubmissionUnitDeclaration> Units = {
            {"WriteA", {WriteTouch("T")}, {}},
            {"WriteB", {WriteTouch("T")}, {}},
            {"ReadA", {ReadTouch("T")}, {}},
            {"ReadB", {ReadTouch("T")}, {}},
        };
        const std::vector<RenderTargetDeclaration> Targets = {
            {"T", TargetFormat::Rgba16Float, 4, 4, false, TargetUsage::Undefined},
        };
        ResolvedSubmissionOrder ChainOrder;
        DerivedBarriers         Chain;
        Check(ResolveSubmissionOrder(Units, ChainOrder, Diagnostic) &&
              DeriveBarriers(Units, ChainOrder, Targets, Chain, Diagnostic), "dependency chain derives");
        uint32_t ChainLayouts = 0;
        uint32_t ChainExecutions = 0;
        for (const DerivedBarrier& Barrier : Chain.Barriers)
        {
            ChainLayouts    += Barrier.Kind == BarrierKind::LayoutTransition ? 1 : 0;
            ChainExecutions += Barrier.Kind == BarrierKind::ExecutionDependency ? 1 : 0;
        }
        Check(Chain.Barriers.size() == 3 && ChainLayouts == 2 && ChainExecutions == 1,
              "write->write costs one execution dependency, read->read costs nothing");
    }

    // Resting-usage contract + declaration refusals.
    {
        const std::vector<SubmissionUnitDeclaration> Units = {{"Only", {WriteTouch("T")}, {}}};
        ResolvedSubmissionOrder SingleOrder;
        DerivedBarriers         Out;
        (void)ResolveSubmissionOrder(Units, SingleOrder, Diagnostic);

        const std::vector<RenderTargetDeclaration> TransientResting = {
            {"T", TargetFormat::Rgba16Float, 4, 4, false, TargetUsage::Sampled}};
        Check(!DeriveBarriers(Units, SingleOrder, TransientResting, Out, Diagnostic),
              "transient target with a resting usage refused");

        const std::vector<RenderTargetDeclaration> PersistentUndefined = {
            {"T", TargetFormat::Rgba16Float, 4, 4, true, TargetUsage::Undefined}};
        Check(!DeriveBarriers(Units, SingleOrder, PersistentUndefined, Out, Diagnostic),
              "persistent target without a resting usage refused");

        Check(!DeriveBarriers(Units, SingleOrder, {}, Out, Diagnostic), "undeclared touched target refused");

        const std::vector<SubmissionUnitDeclaration> UndefinedUse = {
            {"Only", {TouchTarget("T", TargetUsage::Undefined)}, {}}};
        const std::vector<RenderTargetDeclaration> Fine = {
            {"T", TargetFormat::Rgba16Float, 4, 4, false, TargetUsage::Undefined}};
        ResolvedSubmissionOrder UndefinedOrder;
        (void)ResolveSubmissionOrder(UndefinedUse, UndefinedOrder, Diagnostic);
        Check(!DeriveBarriers(UndefinedUse, UndefinedOrder, Fine, Out, Diagnostic),
              "Undefined as a touch usage refused");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTION VIEWS
//------------------------------------------------------------------------------------------------------------------------

void CheckInspection()
{
    std::printf("[inspection views]\n");
    using namespace Frontier;

    Check(InspectionViewsEnabled() == BuildConfiguration::EditorMachineryEnabled,
          "inspection exists exactly where editor machinery exists");

    // Every demo debug view declares a distinct unit.
    const InspectionView Views[] = {InspectionView::Resolved, InspectionView::VisibilityIds,
                                    InspectionView::GlassLayers, InspectionView::HiZ,
                                    InspectionView::LodLevels, InspectionView::ClusterFallback,
                                    InspectionView::InspectionMarch};
    std::vector<std::string> Names;
    for (InspectionView View : Views)
    {
        Names.push_back(DeclareInspectionUnit(View, "Surface").Name);
    }
    bool Distinct = true;
    for (size_t A = 0; A < Names.size(); ++A)
    {
        for (size_t B = A + 1; B < Names.size(); ++B)
        {
            Distinct &= Names[A] != Names[B];
        }
    }
    Check(Distinct && Names.size() == 7, "all seven demo debug views declare distinct units");

    // Appended to the viewport frame, an inspection unit orders, aliases and barriers like any other.
    ViewportFrameDeclaration Frame = DeclareViewportFrame(1920, 1080);
    Frame.Units.push_back(DeclareInspectionUnit(InspectionView::VisibilityIds, "Surface"));
    Frame.Targets.push_back(DeclareInspectionTarget(1920, 1080));

    ResolvedSubmissionOrder Order;
    std::string             Diagnostic;
    Check(ResolveSubmissionOrder(Frame.Units, Order, Diagnostic), "frame + inspection resolves");
    Check(!Order.Order.empty() && Order.Order.back() == 2, "inspection submits after the present unit");

    DerivedBarriers Barriers;
    Check(DeriveBarriers(Frame.Units, Order, Frame.Targets, Barriers, Diagnostic),
          "frame + inspection barriers derive");
    bool SurfaceToStorageRead = false;
    bool InspectionBorn = false;
    for (const DerivedBarrier& Barrier : Barriers.Barriers)
    {
        SurfaceToStorageRead |= Barrier.Target == "Surface" && Barrier.From == TargetUsage::Sampled &&
                                Barrier.To == TargetUsage::StorageRead && Barrier.BeforePosition == 2;
        InspectionBorn |= Barrier.Target == "Inspection" && Barrier.From == TargetUsage::Undefined &&
                          Barrier.To == TargetUsage::StorageWrite;
    }
    Check(SurfaceToStorageRead, "inspection re-layouts its source like any consumer");
    Check(InspectionBorn, "inspection target is born Undefined (transient, aliasable)");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROVING GROUNDS
//------------------------------------------------------------------------------------------------------------------------

void CheckProvingGrounds()
{
    std::printf("[proving grounds]\n");
    using namespace Frontier;

    const ProvingGroundsGeometry Geometry = BuildProvingGroundsGeometry();
    Check(Geometry.Vertices.size() == 28 && Geometry.Indices.size() == 42, "cube + ground plane built");

    bool IndicesInRange = true;
    bool NormalsUnit = true;
    for (uint32_t Index : Geometry.Indices)
    {
        IndicesInRange &= Index < Geometry.Vertices.size();
    }
    for (const ProvingGroundsVertex& Vertex : Geometry.Vertices)
    {
        NormalsUnit &= NearlyEqual(Length(Vertex.Normal), 1.0f);
    }
    Check(IndicesInRange, "every index addresses a vertex");
    Check(NormalsUnit, "every normal is unit length");

    ProvingGroundsSimulation Simulation;
    Simulation.Advance(1.5707963 / ProvingGroundsSimulation::AngularRate);
    Check(Simulation.QueryFrameIndex() == 1, "simulation counts frames");
    const ProvingGroundsSnapshot Snapshot = Simulation.TakeSnapshot(1920, 1080);
    Check(NearlyEqual(TransformPoint(Snapshot.ModelToWorld, {0.5f, 0.55f, 0}), {0, 0.55f, -0.5f}),
          "quarter spin carries the +X face to -Z");

    // Game-side producer, render-side consumer, exact final delivery.
    SnapshotExchange<ProvingGroundsSnapshot> Exchange;
    constexpr uint64_t                       Frames = 5000;
    std::atomic<uint64_t>                    LastSeen{0};
    std::atomic<bool>                        Monotone{true};
    std::thread Game([&]() {
        ProvingGroundsSimulation Sim;
        for (uint64_t Frame = 0; Frame < Frames; ++Frame)
        {
            Sim.Advance(1.0 / 240.0);
            Exchange.Publish(Sim.TakeSnapshot(1280, 720));
        }
    });
    std::thread Render([&]() {
        ProvingGroundsSnapshot Latest;
        uint64_t Previous = 0;
        while (Previous < Frames)
        {
            if (!Exchange.Take(Latest))
            {
                std::this_thread::yield();
                continue;
            }
            if (Latest.FrameIndex <= Previous)
            {
                Monotone.store(false);
            }
            Previous = Latest.FrameIndex;
            LastSeen.store(Latest.FrameIndex);
        }
    });
    Game.join();
    Render.join();
    Check(Monotone.load() && LastSeen.load() == Frames, "spin snapshots stream game->render in order");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   LOG STREAM PIPE
//------------------------------------------------------------------------------------------------------------------------

void CheckLogPipe()
{
    std::printf("[log stream pipe]\n");
    using namespace Frontier;

    LogStreamPanel Panel;
    Panel.Synchronize(LogStore(), LogCategories(), 0.1);

    bool SawOrder = false;
    bool SawAtlas = false;
    bool SawRefusal = false;
    bool SawBarriers = false;
    for (const LogStreamVisibleEntry& Row : Panel.QueryVisible())
    {
        const std::string Message = Row.Entry.Message;
        SawOrder   |= Message.find("Submission order resolved") != std::string::npos;
        SawAtlas   |= Message.find("Transient atlas resolved") != std::string::npos;
        SawRefusal |= Message.find("refused") != std::string::npos;
        SawBarriers |= Message.find("Barriers derived") != std::string::npos;
    }
    Check(SawOrder && SawAtlas, "infrastructure resolutions arrive on the Render stream");
    Check(SawRefusal, "infrastructure refusals arrive on the Render stream");
    Check(SawBarriers, "barrier dump summary arrives on the Render stream");
    Check(Panel.QueryCategoryCount(1) > 0, "Render is engine channel token 1");
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    CheckSubmissionOrder();
    CheckTransientAtlas();
    CheckMath();
    CheckBarriers();
    CheckInspection();
    CheckProvingGrounds();
    CheckThreading();
    CheckLogPipe();

    std::printf("\nRender infrastructure validation: %d passed, %d failed\n", Passes, Failures);
    return Failures == 0 ? 0 : 1;
}
