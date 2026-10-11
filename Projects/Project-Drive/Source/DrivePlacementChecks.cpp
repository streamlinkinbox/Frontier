//============================================================================================================================================
//                                                        DRIVEPLACEMENTCHECKS.CPP
//============================================================================================================================================
// 📦 Checks the actual imported vehicle placement, tyre ground clearance and editor roster ordering.

#include "../../../Engine/Host/EditorFeedSequence.h"
#include "../../../Engine/ContentInterchange/SceneCodec.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;
    assert(ArgumentCount == 2);
    SceneStructure Scene;
    std::string Refusal;
    assert(SceneCodec::Decode(Arguments[1], Scene, nullptr, {}, &Refusal));
    auto Rows = std::make_unique<EditorInstance[]>(kMaxEditorInstances);
    auto Spans = std::make_unique<HostRuntime::EditorFeedSequence::RosterSpan[]>(kMaxEditorInstances);
    HostRuntime::EditorFeedSequence Feed;
    const uint32_t Count = Feed.FillRoster(Rows.get(), Scene);
    assert(Feed.FillRosterSpans(Spans.get(), Scene) == Count);
    uint32_t VehiclePlacements = 0u;
    for (uint32_t Slot = 0u; Slot < Count; ++Slot)
    {
        const auto& Row = Rows[Slot];
        std::printf("%*s%s: %u instances\n", Row.Depth * 2, "", Row.Label, Spans[Slot].InstanceCount);
        if (std::strstr(Row.Label, "ControlVehicle") || std::strstr(Row.Label, "XPBD Tyre"))
        {
            assert(Row.Category == EditorInstanceCategory::Geometry);
            assert(Row.Depth == 1u);
            assert(Spans[Slot].InstanceCount > 0u);
            ++VehiclePlacements;
            if (std::strstr(Row.Label, "Tyre"))
            {
                float Bottom = 1e30f;
                for (uint32_t Offset = 0u; Offset < Spans[Slot].InstanceCount; ++Offset)
                {
                    const auto& Placement = Scene.QueryInstances()[Spans[Slot].FirstInstance + Offset];
                    for (uint32_t Triangle = 0u; Triangle < Placement.TriangleCount; ++Triangle)
                    {
                        const auto& Facet = Scene.QueryFlatTriangles()[Placement.FlatTriangleOffset + Triangle];
                        Bottom = std::min({ Bottom, Facet.VertexAlphaZ, Facet.VertexBetaZ, Facet.VertexGammaZ });
                    }
                }
                std::printf("  tyre clearance: %.6f m\n", Bottom);
                assert(Bottom >= 0.019f && Bottom <= 0.021f);
            }
        }
    }
    assert(VehiclePlacements == 5u);
    std::puts("PASS vehicle and all four tyres are geometry, and all tyres clear the ground by 20 mm");

    auto Original = std::make_unique<EditorInstance[]>(kMaxEditorInstances);
    auto Canonical = std::make_unique<HostRuntime::EditorFeedSequence::RosterSpan[]>(kMaxEditorInstances);
    std::copy_n(Rows.get(), Count, Original.get());
    std::copy_n(Spans.get(), Count, Canonical.get());
    std::reverse(Rows.get(), Rows.get() + Count);
    Feed.ResolveRosterSpans(Spans.get(), Rows.get(), Count, Canonical.get(), Count);
    for (uint32_t Slot = 0u; Slot < Count; ++Slot)
        assert(Spans[Slot].Placement == Canonical[Count - Slot - 1u].Placement);
    std::copy_n(Original.get(), Count, Rows.get());
    Feed.ResolveRosterSpans(Spans.get(), Rows.get(), Count, Canonical.get(), Count);
    uint32_t Picks[3]{};
    uint32_t Found = 0u;
    for (uint32_t Slot = 0u; Slot < Count && Found < 2u; ++Slot)
        if (std::strstr(Rows[Slot].Label, "XPBD Tyre")) Picks[Found++] = Slot;
    assert(Found == 2u);
    Picks[2] = Picks[0];
    auto Selection = Feed.CollectSelectionInstances(Rows.get(), Count, Spans.get(), Picks, 3u,
        static_cast<uint32_t>(Scene.QueryInstances().size()));
    assert(Selection.size() == 4u);
    Rows[Picks[0]].Locked = true;
    Selection = Feed.CollectSelectionInstances(Rows.get(), Count, Spans.get(), Picks, 3u,
        static_cast<uint32_t>(Scene.QueryInstances().size()));
    assert(Selection.size() == 2u);
    Rows[Picks[0]].Artwork = IconSymbol::Sun;
    Rows[Picks[0]].Glyph = EditorGlyph::Sun;
    Feed.FillRoster(Rows.get(), Scene);
    assert(Rows[Picks[0]].Artwork != IconSymbol::Sun && Rows[Picks[0]].Glyph != EditorGlyph::Sun);
    std::puts("PASS reordered keys retain geometry, multiselect is deduplicated, locked rows stay fixed, refill removes stale sun icons");

    SceneStructure Grouped;
    Matrix4x4 Identity{};
    for (uint32_t Cell = 0u; Cell < 4u; ++Cell) Identity.Columns[Cell][Cell] = 1.0f;
    const uint32_t Root = Grouped.RegisterPlacement("Assembly", kPlacementNone, Identity, Identity);
    const uint32_t Moving = Grouped.RegisterPlacement("Moving placement", Root, Identity, Identity);
    Grouped.AssignPlacementDynamic(Moving, true);
    const uint32_t Static = Grouped.RegisterPlacement("Static placement", Root, Identity, Identity);
    const uint32_t Nested = Grouped.RegisterPlacement("Nested placement", Moving, Identity, Identity);
    const uint32_t GroupCount = Feed.FillRoster(Rows.get(), Grouped);
    assert(Feed.FillRosterSpans(Spans.get(), Grouped) == GroupCount);
    uint32_t RootRow = kMaxEditorInstances;
    for (uint32_t Slot = 0u; Slot < GroupCount; ++Slot)
        if (Spans[Slot].Placement == Root) RootRow = Slot;
    assert(RootRow + 3u < GroupCount);
    assert(Spans[RootRow + 1u].Placement == Moving);
    assert(Spans[RootRow + 2u].Placement == Nested);
    assert(Spans[RootRow + 3u].Placement == Static);
    assert(Rows[RootRow].Depth == 1u && Rows[RootRow + 1u].Depth == 2u && Rows[RootRow + 2u].Depth == 3u);
    std::puts("PASS mixed dynamic/static branches remain contiguous and nested in preorder");
}
