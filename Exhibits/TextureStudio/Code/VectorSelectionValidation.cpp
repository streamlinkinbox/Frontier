/*============================================================================================================================================
                                                       VECTORSELECTIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Exercise shared vector selection regions and local-axis transforms without rasterizing authored pixels on CPU.

#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include "Engine/ContentInterchange/CubicPathEditing.h"
#include "Engine/ContentInterchange/VectorGradientStops.h"
#include "Engine/Render/VectorSurfaceCompilation.h"
#include <iostream>
#include <limits>
#include <numbers>

using namespace Frontier;

namespace
{
uint32_t Assertions = 0;

void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Assertions;
}

bool Near(float First, float Second)
{
    return std::abs(First - Second) < .003f;
}

std::array<float, 2> Project(const VectorCoverageInstruction &Source, float X, float Y)
{
    const float θ = Source.Transform[0];
    return {Source.Bounds[0] + X * std::cos(θ) - Y * std::sin(θ),
            Source.Bounds[1] + X * std::sin(θ) + Y * std::cos(θ)};
}
}

int main()
{
    try
    {
        constexpr float π = std::numbers::pi_v<float>;
        for (float θ : {0.f, .43f, π * .5f, -π * .7f})
        {
            for (int Y = -1; Y <= 1; ++Y) for (int X = -1; X <= 1; ++X)
            {
                if (!X && !Y) continue;
                for (bool Centred : {false, true}) for (bool Aspect : {false, true})
                {
                    VectorAuthoringDocument Document;
                    VectorCoverageInstruction Source;
                    Source.Bounds = {256, 230, 120, 80}; Source.Transform[0] = θ;
                    const auto Token = ConstructVectorAuthoringPrimitive(Document, Source, "Rotated motif");
                    // Aliased unselected references must remain independent after every gesture.
                    ConstructVectorAuthoringPrimitive(Document, Source, "Aliased motif");
                    Document.Layers[1].Root = Document.Layers[0].Root;
                    const auto Before = Document;
                    const std::array<uint32_t, 1> Selection{Token};
                    VectorAuthoringSourcePatch Identity;
                    Identity.ScaleBasis = θ;
                    TransformVectorAuthoringSelection(Document, Selection, Identity);
                    Confirm(Document == Before, "Identity transform reordered or changed source descriptors.");
                    const auto Extent = QueryVectorSelectionExtent(Document, Selection);
                    Confirm(Extent.Bounds == Source.Bounds && Extent.Rotation == θ, "Single-source extent lost its local axes.");
                    const auto Point = Project(Source, float(X) * 95, float(Y) * 70);
                    const auto Patch = EvaluateVectorSelectionResize(Document, Selection, {X, Y}, Point, Aspect, Centred);
                    TransformVectorAuthoringSelection(Document, Selection, Patch);
                    const auto &Resized = Document.Instructions[Document.Layers[0].Root];
                    Confirm(Resized.Bounds[2] >= 120 && Resized.Bounds[3] >= 80, "Resize unexpectedly shrank the source.");
                    Confirm(Near(Resized.Transform[0], θ), "Resize changed orientation.");
                    Confirm(Document.Instructions[Document.Layers[1].Root] == Source, "Resize mutated an unselected alias.");
                    if (Aspect) Confirm(Near(Resized.Bounds[2] / Resized.Bounds[3], 1.5f), "Constrained resize changed aspect ratio.");
                    if (Centred)
                    {
                        Confirm(Near(Resized.Bounds[0], 256) && Near(Resized.Bounds[1], 230), "Centred resize translated its pivot.");
                    }
                    else
                    {
                        const auto FixedBefore = Project(Source, -float(X) * Source.Bounds[2] * .5f, -float(Y) * Source.Bounds[3] * .5f);
                        const auto FixedAfter = Project(Resized, -float(X) * Resized.Bounds[2] * .5f, -float(Y) * Resized.Bounds[3] * .5f);
                        Confirm(Near(FixedBefore[0], FixedAfter[0]) && Near(FixedBefore[1], FixedAfter[1]), "Opposite resize grip drifted.");
                    }
                    Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Document)) == Document, "Resized source lost TOML precision.");
                    VectorAuthoringHistory History;
                    History.Reset(Before);
                    Confirm(History.Finalize(Document), "Resize was not accepted by source history.");
                    Confirm(History.RestoreHistory(false) && History.QueryDocument() == Before, "Resize history did not preserve the exact source.");
                    Confirm(History.RestoreHistory(true) && History.QueryDocument() == Document, "Resize history did not preserve its result.");
                }
            }
        }
        {
            VectorAuthoringDocument Rounded;
            Rounded.Stamp = true;
            VectorCoverageInstruction Rectangle;
            const auto Token = ConstructVectorAuthoringPrimitive(Rounded, Rectangle, "Rounded rectangle");
            const std::array<uint32_t, 1> Selection{Token};
            Rounded.Layers[0].Mask = Rectangle;
            VectorAuthoringSourcePatch Radius; Radius.CornerRadius = 12.5f;
            TransformVectorAuthoringSelection(Rounded, Selection, Radius);
            Confirm(Near(Rounded.Instructions[Rounded.Layers[0].Root].ShapeParameters[0], 12.5f), "Corner radius edit was lost.");
            Confirm(Rounded.Layers[0].Mask->ShapeParameters[0] == 0, "Corner radius edit changed the owned mask.");
            Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Rounded)) == Rounded, "Corner radius did not round trip.");
        }
        {
            VectorAuthoringDocument Combined;
            Combined.Stamp = true;
            VectorCoverageInstruction Rectangle, Union;
            Union.Topology = {16, 0, 1, 0};
            Combined.Instructions = {Rectangle, Rectangle, Union};
            VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Root = 2; Layer.Name = "Combined";
            Layer.Mask = Rectangle; Layer.ExpressionOpacity = .35f;
            Combined.Layers = {Layer}; Combined.TokenClock = 1;
            Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Combined)) == Combined, "Combined opacity did not round trip.");
            const auto Coverage = ConstructVectorAuthoringCoverage(Combined);
            Confirm(Coverage.Instructions[Coverage.Root].Topology[0] == 29 && Near(Coverage.Instructions[Coverage.Root].Transform[2], .35f), "Coverage lost post-mask opacity.");
            const auto Selected = ConstructVectorSelectionProgram(Combined);
            Confirm(Selected.Coverage.Instructions[Selected.Candidates[0][0]].Topology[0] == 29, "Selection lost combined opacity.");
            const auto Surface = CompileVectorSurface(Combined, VectorAuthoringSurfaceChannel::Colour);
            Confirm(Surface.Coverage.Instructions[Surface.Contributions[0].Root].Topology[0] == 29, "Surface lost combined opacity.");
            auto Recombined = Combined; Recombined.Layers[0].Mask.reset();
            const auto Other = ConstructVectorAuthoringPrimitive(Recombined, Rectangle, "Other");
            const std::array<uint32_t, 2> Both{1, Other};
            IntegrateVectorAuthoringBoolean(Recombined, Both, VectorCoverageCategory::Union);
            const auto &Root = Recombined.Instructions[Recombined.Layers[0].Root];
            const auto &Operand = Recombined.Instructions[Root.Topology[1]];
            Confirm(Operand.Topology[0] == 29 && Near(Operand.Transform[2], .35f) && Recombined.Layers[0].ExpressionOpacity == 1, "Recombining lost operand opacity.");
            Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Recombined)) == Recombined, "Retained opacity expression did not round trip.");
            Combined.Layers[0].ExpressionOpacity = 1.5f;
            bool Refused = false; try { (void)ConstructVectorAuthoringCoverage(Combined); } catch (const std::runtime_error &) { Refused = true; }
            Confirm(Refused, "Invalid combined opacity was accepted.");
        }
        {
            VectorAuthoringDocument Artwork; Artwork.Stamp = true;
            VectorCoverageInstruction Rectangle;
            Rectangle.Colour = {.1f, .2f, .3f, 1};
            Rectangle.StrokeColour = Rectangle.Colour;
            const auto Token = ConstructVectorAuthoringPrimitive(Artwork, Rectangle, "Gradient stroke");
            const std::array<uint32_t, 1> Selection{Token};
            Artwork.Layers[0].Mask = Rectangle;
            std::vector<VectorGradientStop> Stops{{0, {1, 0, 0, 1}}, {.4f, {0, 1, 0, .5f}}, {1, {0, 0, 1, 1}}};
            VectorAuthoringSourcePatch Patch; Patch.StrokePaintCategory = 1;
            ApplyVectorStrokeGradientStops(Patch, Stops);
            TransformVectorAuthoringSelection(Artwork, Selection, Patch);
            const auto &Edited = Artwork.Instructions[Artwork.Layers[0].Root];
            const auto Actual = QueryVectorStrokeGradientStops(Edited);
            Confirm(Edited.ShapeParameters[1] == 1 && Actual.size() == 3 && Near(Actual[1].Position, .4f), "Stroke ramp edit was lost.");
            Confirm(Edited.Colour == Rectangle.Colour && Artwork.Layers[0].Mask->StrokeGradientStopPositions[2] == -1, "Stroke gradient edit changed fill or mask.");
            Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Artwork)) == Artwork, "Stroke gradient did not round trip.");
            Patch = {}; Patch.StrokePaintCategory = 0;
            TransformVectorAuthoringSelection(Artwork, Selection, Patch);
            Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Artwork)) == Artwork, "Solid stroke lost its retained gradient.");
        }
        VectorAuthoringDocument Group;
        VectorCoverageInstruction Source;
        Source.Bounds = {180, 256, 120, 80}; Source.Transform[0] = .43f;
        const auto First = ConstructVectorAuthoringPrimitive(Group, Source, "Oriented source");
        Source.Bounds[0] = 340; Source.Transform[0] = 0;
        const auto Second = ConstructVectorAuthoringPrimitive(Group, Source, "Straight source");
        const std::array<uint32_t, 2> Both{First, Second};
        auto Patch = EvaluateVectorSelectionResize(Group, Both, {1, 1}, {470, 420}, false, false);
        Confirm(Patch.ScaleX == Patch.ScaleY, "Mixed rotated selection did not constrain its scale to avoid shear.");
        TransformVectorAuthoringSelection(Group, Both, Patch);
        Confirm(Near(Group.Instructions[Group.Layers[0].Root].Transform[0], .43f), "Mixed resize changed source orientation.");
        // A differently oriented mask must also constrain scaling rather than silently shear.
        Group.Layers[0].Mask = Source;
        const std::array<uint32_t, 1> Single{First};
        Patch = EvaluateVectorSelectionResize(Group, Single, {1, 0}, {500, 260}, false, true);
        Confirm(Patch.ScaleX == Patch.ScaleY, "Rotated mask did not constrain scaling.");
        const auto OriginalMask = Group.Layers[0].Mask;
        TransformVectorAuthoringSelection(Group, Single, Patch);
        Confirm(Group.Layers[0].Mask != OriginalMask, "Owned mask did not follow owner resize.");
        Group.Layers[0].Locked = true;
        const auto Protected = Group;
        bool Refused = false;
        try { TransformVectorAuthoringSelection(Group, Single, Patch); } catch (const std::exception &) { Refused = true; }
        Confirm(Refused && Group == Protected, "Protected transform was not atomic.");
        Group.Layers[0].Locked = false;
        const auto BeforeInvalid = Group;
        Patch.ScaleBasis = std::numeric_limits<float>::quiet_NaN();
        Refused = false;
        try { TransformVectorAuthoringSelection(Group, Single, Patch); } catch (const std::exception &) { Refused = true; }
        Confirm(Refused && Group == BeforeInvalid, "Nonfinite scale basis was accepted or modified source.");
        Refused = false;
        try { (void)EvaluateVectorSelectionResize(Group, Single, {0, 0}, {400, 300}, false, false); } catch (const std::exception &) { Refused = true; }
        Confirm(Refused, "Zero resize direction was accepted.");
        Refused = false;
        try { (void)QueryVectorSelectionRegion(Group, {0, 0}, {std::numeric_limits<float>::infinity(), 10}); } catch (const std::exception &) { Refused = true; }
        Confirm(Refused, "Nonfinite marquee endpoint was accepted.");
        VectorAuthoringDocument Rotation;
        Source.Bounds = {256, 256, 100, 80}; Source.Transform[0] = 22 * π / 180;
        const auto Rotating = ConstructVectorAuthoringPrimitive(Rotation, Source, "Rotation motif");
        const std::array<uint32_t, 1> RotatingSelection{Rotating};
        const auto Start = Project(Source, 100, 0);
        const auto Finish = Project(Source, 100 * std::cos(12 * π / 180), 100 * std::sin(12 * π / 180));
        Patch = EvaluateVectorSelectionRotation(Rotation, RotatingSelection, Start, Finish, true);
        TransformVectorAuthoringSelection(Rotation, RotatingSelection, Patch);
        Confirm(Near(Rotation.Instructions[Rotation.Layers[0].Root].Transform[0], 30 * π / 180), "Snapped rotation was not absolute to 15 degrees.");
        Confirm(Near(Rotation.Instructions[Rotation.Layers[0].Root].Bounds[0], 256), "Rotation translated a single-source pivot.");
        // Marquee is intersection-based and direction independent, with stable document ordering.
        VectorAuthoringDocument Regions;
        Source.Bounds = {100, 100, 100, 80}; Source.Transform[0] = 0;
        const auto Visible = ConstructVectorAuthoringPrimitive(Regions, Source, "Visible");
        Source.Bounds[0] = 350;
        const auto Additive = ConstructVectorAuthoringPrimitive(Regions, Source, "Additive");
        ConstructVectorAuthoringPrimitive(Regions, Source, "Locked"); Regions.Layers.back().Locked = true;
        ConstructVectorAuthoringPrimitive(Regions, Source, "Hidden"); Regions.Layers.back().Visible = false;
        const auto RegionBefore = Regions;
        Confirm(QueryVectorSelectionRegion(Regions, {149, 90}, {170, 110}) == std::vector<uint32_t>{Visible}, "Partial marquee intersection was lost.");
        Confirm(QueryVectorSelectionRegion(Regions, {170, 110}, {149, 90}) == std::vector<uint32_t>{Visible}, "Reverse marquee changed intersection semantics.");
        const std::array<uint32_t, 2> Existing{Additive, Additive};
        Confirm(QueryVectorSelectionRegion(Regions, {149, 90}, {170, 110}, Existing) == std::vector<uint32_t>({Visible, Additive}), "Additive marquee duplicated or lost tokens.");
        Confirm(QueryVectorSelectionRegion(Regions, {0, 0}, {512, 512}) == std::vector<uint32_t>({Visible, Additive}), "Marquee included protected/hidden sources.");
        Confirm(Regions == RegionBefore, "Marquee mutated source descriptors.");
        // Quarter-turn source axes are exactly representable under nonuniform world-axis scale.
        VectorAuthoringDocument QuarterTurn;
        Source.Bounds = {256, 256, 120, 80}; Source.Transform[0] = π * .5f;
        const auto Orthogonal = ConstructVectorAuthoringPrimitive(QuarterTurn, Source, "Quarter turn");
        const std::array<uint32_t, 1> OrthogonalSelection{Orthogonal};
        Patch = {}; Patch.ScaleX = 2; Patch.ScaleY = 3;
        TransformVectorAuthoringSelection(QuarterTurn, OrthogonalSelection, Patch);
        const auto &OrthogonalSource = QuarterTurn.Instructions[QuarterTurn.Layers[0].Root];
        Confirm(Near(OrthogonalSource.Bounds[2], 360) && Near(OrthogonalSource.Bounds[3], 160), "Quarter-turn scale axes were not exchanged.");
        VectorAuthoringDocument TransferSource;
        VectorCoverageInstruction Primitive;
        ConstructVectorAuthoringPrimitive(TransferSource, Primitive, "Unselected source");
        ConstructVectorAuthoringPrimitive(TransferSource, Primitive, "Expression source");
        ConstructVectorAuthoringPrimitive(TransferSource, Primitive, "Cubic source");
        TransferSource.Segments.resize(3);
        TransferSource.Segments[2] = {{-.5f, 0, -.25f, -.5f}, {.25f, .5f, .5f, 0}};
        TransferSource.Instructions[2].Topology = {6, 2, 1, 0};
        TransferSource.Instructions[2].Paint[3] = 0; TransferSource.Instructions[2].Transform[1] = 4;
        VectorCoverageInstruction Boolean; Boolean.Topology = {16, 0, 1, 0};
        TransferSource.Instructions.push_back(Boolean); TransferSource.Layers[1].Root = 3;
        TransferSource.Instructions[1].Transform[2] = .4f; TransferSource.Instructions[1].Transform[3] = 1;
        TransferSource.Layers[1].Mask = TransferSource.Instructions[2]; TransferSource.Layers[1].MaskLuminance = true;
        TransferSource.Layers[1].MaskInverted = true; TransferSource.Layers[1].SurfaceChannels[1].Strength = .3f;
        TransferSource.Enclosures = {{4, 0, "Outer"}, {5, 4, "Inner"}};
        TransferSource.Layers[1].EnclosureToken = TransferSource.Layers[2].EnclosureToken = 5;
        TransferSource.TokenClock = 5;
        const auto UntouchedTransfer = TransferSource;
        const std::array<uint32_t, 2> TransferTokens{3, 2};
        const auto Fragment = ExtractVectorAuthoringSelection(TransferSource, TransferTokens);
        Confirm(TransferSource == UntouchedTransfer, "Copy mutated the source document.");
        Confirm(Fragment.Layers.size() == 2 && Fragment.Layers[0].Token == 2 && Fragment.Layers[1].Token == 3,
            "Copy did not retain original stacking order.");
        Confirm(Fragment.Enclosures.size() == 2 && Fragment.Segments.size() == 1,
            "Copy lost enclosure ancestry or retained unused curve storage.");
        Confirm(Fragment.Layers[0].Mask->Topology[1] == 0 && Fragment.Instructions[Fragment.Layers[1].Root].Topology[1] == 0,
            "Copy failed to remap shared cubic/mask segment ranges.");
        Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Fragment)) == Fragment, "Copied fragment did not round trip TOML.");
        for (auto Offset : {std::array<float, 2>{0, 0}, std::array<float, 2>{16, 16}})
        {
            auto Destination = TransferSource;
            const auto Inserted = IntegrateVectorAuthoringSelection(Destination, Fragment, Offset);
            Confirm(Destination.Layers.size() == 5 && Inserted.size() == 2 && Inserted[0] > 5 && Inserted[1] > Inserted[0],
                "Paste did not issue fresh layer identities.");
            Confirm(Destination.Layers[0].Name == TransferSource.Layers[0].Name
                && Destination.Layers[1].Name == TransferSource.Layers[1].Name,
                "Paste renamed existing layers while allocating copied captions.");
            Confirm(Destination.Enclosures.size() == 4 && Destination.Layers[3].EnclosureToken == Destination.Layers[4].EnclosureToken
                && Destination.Layers[3].EnclosureToken != 5, "Paste aliased original group membership.");
            Confirm(Destination.Enclosures[3].EnclosureToken == Destination.Enclosures[2].Token,
                "Paste lost nested group ancestry.");
            Confirm(Destination.Layers[3].MaskLuminance && Destination.Layers[3].MaskInverted
                && Destination.Layers[3].SurfaceChannels == Fragment.Layers[0].SurfaceChannels,
                "Paste lost mask modes or channel contributions.");
            const auto &Cubic = Destination.Instructions[Destination.Layers[4].Root];
            const auto &OriginalCubic = Fragment.Instructions[Fragment.Layers[1].Root];
            Confirm(Cubic.Bounds[0] == OriginalCubic.Bounds[0] + Offset[0]
                && Destination.Layers[3].Mask->Bounds[1] == Fragment.Layers[0].Mask->Bounds[1] + Offset[1],
                "Paste did not translate source and owned mask together.");
            Confirm(Destination.Instructions[Destination.Layers[3].Root].Topology[0] == 16,
                "Paste flattened an editable Boolean expression.");
            Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Destination)) == Destination,
                "Pasted source did not round trip TOML.");
            VectorAuthoringSourcePatch Recolour; Recolour.Colour = std::array<float, 4>{.1f, .3f, .5f, 1};
            TransformVectorAuthoringSelection(Destination, Inserted, Recolour);
            Confirm(Destination.Instructions[Destination.Layers[0].Root].Colour == UntouchedTransfer.Instructions[0].Colour,
                "Editing pasted sources changed an original alias.");
        }
        {
            VectorAuthoringDocument Destination;
            auto SingleCurve = ExtractVectorAuthoringSelection(TransferSource, std::array<uint32_t, 1>{3});
            SingleCurve.Enclosures.clear(); SingleCurve.Layers[0].EnclosureToken = 0;
            for (int Repetition = 0; Repetition < 12; ++Repetition)
            {
                const auto Tokens = IntegrateVectorAuthoringSelection(Destination, SingleCurve);
                Confirm(Destination.Segments.size() == 1, "Repeated cut/paste leaked unreachable cubic storage.");
                PartitionVectorAuthoringSelection(Destination, Tokens);
            }
        }
        auto ProtectedCopy = TransferSource; ProtectedCopy.Enclosures[0].LockEnabled = true;
        Confirm(ExtractVectorAuthoringSelection(ProtectedCopy, TransferTokens).Enclosures[0].LockEnabled,
            "Read-only copy lost inherited protection metadata.");
        VectorAuthoringDocument UnlockedDestination;
        const auto ProtectedFragment = ExtractVectorAuthoringSelection(ProtectedCopy, TransferTokens);
        IntegrateVectorAuthoringSelection(UnlockedDestination, ProtectedFragment);
        Confirm(!UnlockedDestination.Enclosures[0].LockEnabled && !UnlockedDestination.Layers[0].Locked,
            "Copied sources inherited locks instead of becoming independently editable.");
        for (int Refusal = 0; Refusal < 6; ++Refusal)
        {
            auto Destination = TransferSource;
            auto Incoming = Fragment;
            std::array<float, 2> Offset{16, 16};
            if (Refusal == 0) Destination.TokenClock = UINT32_MAX;
            if (Refusal == 1) Offset[0] = std::numeric_limits<float>::infinity();
            if (Refusal == 2) Incoming.Layers.clear();
            if (Refusal == 3) Incoming.Layers[0].Root = UINT32_MAX;
            if (Refusal == 4) for (auto &Source : Incoming.Instructions) if (Source.Topology[0] < 7) Source.Bounds[0] = 16380;
            if (Refusal == 5)
            {
                while (Destination.Layers.size() < 64)
                {
                    auto Layer = Destination.Layers[0]; Layer.Token = ++Destination.TokenClock;
                    Destination.Layers.push_back(Layer);
                }
            }
            const auto Before = Destination;
            bool Refused = false;
            try { (void)IntegrateVectorAuthoringSelection(Destination, Incoming, Offset); }
            catch (const std::exception &) { Refused = true; }
            Confirm(Refused && Destination == Before, "Invalid paste partially mutated its destination.");
        }
        for (uint32_t Order = 0; Order < 4; ++Order)
        {
            VectorAuthoringDocument Stacking;
            for (int Index = 0; Index < 5; ++Index) ConstructVectorAuthoringPrimitive(Stacking, {}, "Stacked source");
            const auto Before = Stacking;
            const std::array<uint32_t, 2> Tokens{2, 4};
            ReorderVectorAuthoringSelection(Stacking, Tokens, VectorSelectionOrder(Order));
            const std::array<std::array<uint32_t, 5>, 4> Expected{{{{2, 4, 1, 3, 5}}, {{2, 1, 4, 3, 5}},
                {{1, 3, 2, 5, 4}}, {{1, 3, 5, 2, 4}}}};
            for (size_t Index = 0; Index < 5; ++Index)
                Confirm(Stacking.Layers[Index].Token == Expected[Order][Index], "Stack command lost relative selection order.");
            Confirm(Stacking.Instructions == Before.Instructions && Stacking.TokenClock == Before.TokenClock,
                "Stack command changed paint or identity allocation.");
            Stacking.Layers[0].Locked = true;
            const auto Protected = Stacking;
            const std::array<uint32_t, 1> Locked{Stacking.Layers[0].Token};
            bool Refused = false;
            try { ReorderVectorAuthoringSelection(Stacking, Locked, VectorSelectionOrder(Order)); }
            catch (const std::exception &) { Refused = true; }
            Confirm(Refused && Stacking == Protected, "Stack command changed a protected source.");
        }
        // 📝 A selected shape/pattern's gesture frame must always hug that shape's own bounds, never
        // the whole document tile, whether it was hand-drawn with the pen tool or carries cubic
        // segments normalized to the tile frame (as converted library paths do).
        {
            VectorAuthoringDocument Drawn;
            CubicPath Path;
            Path.Closed = true;
            Path.Anchors = {{{120, 80}, {120, 80}, {120, 80}}, {{160, 120}, {160, 120}, {160, 120}},
                {{120, 160}, {120, 160}, {120, 160}}, {{80, 120}, {80, 120}, {80, 120}}};
            const auto Token = ConstructCubicPath(Drawn, Path, {}, "Hand-drawn motif");
            const std::array<uint32_t, 1> Selection{Token};
            const auto Extent = QueryVectorSelectionExtent(Drawn, Selection);
            Confirm(Near(Extent.Bounds[0], 120) && Near(Extent.Bounds[1], 120) &&
                Near(Extent.Bounds[2], 80) && Near(Extent.Bounds[3], 80),
                "Hand-drawn motif's selection frame did not hug its own bounds.");
        }
        {
            VectorAuthoringDocument Imported;
            VectorCoverageInstruction Source;
            Source.Bounds = {256, 256, 512, 512}; // tile-sized stored bounds, as converted library paths carry
            const float CentreX = 160.f / 512.f - .5f, CentreY = 160.f / 512.f - .5f, Radius = 40.f / 512.f;
            Imported.Segments.push_back({{CentreX, CentreY - Radius, CentreX, CentreY - Radius}, {CentreX + Radius, CentreY, CentreX + Radius, CentreY}});
            Imported.Segments.push_back({{CentreX + Radius, CentreY, CentreX + Radius, CentreY}, {CentreX, CentreY + Radius, CentreX, CentreY + Radius}});
            Imported.Segments.push_back({{CentreX, CentreY + Radius, CentreX, CentreY + Radius}, {CentreX - Radius, CentreY, CentreX - Radius, CentreY}});
            Imported.Segments.push_back({{CentreX - Radius, CentreY, CentreX - Radius, CentreY}, {CentreX, CentreY - Radius, CentreX, CentreY - Radius}});
            Source.Topology = {6, 0, 4, 0};
            VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Name = "Imported library motif"; Layer.Root = 0;
            Imported.Instructions.push_back(Source);
            Imported.Layers.push_back(Layer);
            Imported.TokenClock = 1;
            const std::array<uint32_t, 1> Selection{1};
            const auto Extent = QueryVectorSelectionExtent(Imported, Selection);
            Confirm(Near(Extent.Bounds[0], 160) && Near(Extent.Bounds[1], 160) &&
                Near(Extent.Bounds[2], 80) && Near(Extent.Bounds[3], 80),
                "Tile-normalized library path's selection frame fell back to the full canvas instead of its traced motif.");
        }
        std::cout << "{\"result\":\"passed\",\"checks\":" << Assertions
                  << ",\"windows_executed\":false,\"gpu_pixels_tested\":false}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
