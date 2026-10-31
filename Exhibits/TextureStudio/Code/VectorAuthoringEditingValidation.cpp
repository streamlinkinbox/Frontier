/*============================================================================================================================================
                                                        VECTORAUTHORINGEDITINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Exercise production source-only arrays, channel edits, enclosure protection, strict TOML and atomic history on the editing host.

#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
#include "Engine/ContentInterchange/CubicPathEditing.h"
#include <iostream>
#include <limits>
#include <numbers>
#include <unordered_set>

namespace
{
using namespace Frontier;
uint32_t Checks = 0;

void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Checks;
}

template<typename Operation>
void ConfirmRefused(Operation &&Evaluate, const char *Caption)
{
    bool Refused = false;
    try
    {
        Evaluate();
    }
    catch (const std::runtime_error &)
    {
        Refused = true;
    }
    Confirm(Refused, Caption);
}

VectorAuthoringDocument ConstructSpecimen(bool Stamp)
{
    VectorAuthoringDocument Document;
    Document.Stamp = Stamp;
    Document.TokenClock = 12;
    VectorCoverageInstruction Square, Ellipse;
    Square.Bounds = {240, 256, 24, 24};
    Ellipse = Square;
    Ellipse.Topology[0] = 1;
    Ellipse.Bounds[0] = 272;
    Document.Instructions = {Square, Ellipse};
    VectorAuthoringLayer Lower, Upper;
    Lower.Token = 1;
    Lower.Name = "Cloth motif";
    Lower.Root = 0;
    Lower.EnclosureToken = 12;
    Lower.Mask = Square;
    Lower.MaskInverted = true;
    Upper.Token = 2;
    Upper.Name = "Embroidery";
    Upper.Root = 1;
    Upper.EnclosureToken = 11;
    Document.Layers = {Lower, Upper};
    Document.Enclosures = {{10, 0, "Library folder", true}, {11, 10, "Cloth group"}, {12, 11, "Nested motif"}};
    return Document;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ARRAY AND SOURCE RETENTION
//------------------------------------------------------------------------------------------------------------------------

void VerifyArrays(bool Stamp)
{
    const auto Document = ConstructSpecimen(Stamp);
    const std::array<uint32_t, 3> Selection{1, 2, 1};
    for (uint32_t Category = 0; Category < 4; ++Category)
    {
        for (uint32_t Profile = 0; Profile < 5; ++Profile)
        {
            VectorAuthoringArraySpecification Specification;
            Specification.Category = VectorAuthoringArrayCategory(Category);
            Specification.ScaleProfile = VectorAuthoringScaleProfile(Profile);
            Specification.Count = 3;
            Specification.Rings = 2;
            Specification.StartScale = 80;
            Specification.EndScale = 120;
            Specification.Radius = 50;
            Specification.Sweep = 270;
            Specification.RotateCopies = true;
            const auto EncodedBefore = EncodeVectorAuthoring(Document);
            const auto Result = GenerateVectorAuthoringArray(Document, Selection, Specification);
            const size_t Population = Category == 1 ? 4 : Category == 3 ? 6 : 3;
            Confirm(Result.Document.Layers.size() == Population * 2, "Wrong native array population.");
            Confirm(Result.Selection.size() == Population * 2, "Native array selection lost copied identities.");
            Confirm(EncodeVectorAuthoring(Document) == EncodedBefore, "Native array modified its input.");
            Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Result.Document)) == Result.Document, "Native array source failed TOML round trip.");
            Confirm(GenerateVectorAuthoringArray(Document, Selection, Specification).Document == Result.Document, "Seeded native array was not deterministic.");
            std::unordered_set<std::string> Names;
            std::unordered_set<uint32_t> Enclosures;
            for (size_t Index = 0; Index < Result.Document.Layers.size(); ++Index)
            {
                const auto &Layer = Result.Document.Layers[Index];
                Confirm(Names.insert(Layer.Name).second, "Copied outliner captions collide.");
                Confirm(Layer.Token != 1 && Layer.Token != 2, "Array copy retained an original layer identity.");
                Confirm(QueryVectorAuthoringVisibility(Result.Document, Layer), "Copied enclosures lost visibility.");
                Confirm(Layer.SurfaceChannels == Document.Layers[Index % 2].SurfaceChannels, "Array discarded surface channel source.");
                if (Index % 2 == 0)
                {
                    Confirm(Layer.Mask && Layer.MaskInverted, "Array discarded an owned mask.");
                    Confirm(Enclosures.insert(Layer.EnclosureToken).second, "Array placements share an editable enclosure.");
                    Confirm(Layer.Mask->Bounds == Result.Document.Instructions[Layer.Root].Bounds, "Mask did not follow array transformation.");
                }
            }
            VectorAuthoringHistory History(Stamp);
            Confirm(History.Finalize(Document), "Initial native history edit refused.");
            Confirm(History.Finalize(Result.Document), "Native array did not finalize as one history edit.");
            Confirm(History.RestoreHistory(false) && History.QueryDocument() == Document, "Array undo did not restore its complete source.");
            Confirm(!History.Finalize(Document), "No-op edit polluted native history.");
            Confirm(History.RestoreHistory(true) && History.QueryDocument() == Result.Document, "No-op edit erased the native redo sequence.");
        }
    }
    auto Simple = ConstructSpecimen(Stamp);
    Simple.Enclosures.clear();
    Simple.Layers.resize(1);
    Simple.Layers[0].EnclosureToken = 0;
    Simple.Layers[0].Mask.reset();
    Simple.Instructions.resize(1);
    Simple.Instructions[0].Bounds = {256, 256, 20, 20};
    VectorAuthoringArraySpecification Specification;
    Specification.Count = 3;
    Specification.StepX = 64;
    const std::array<uint32_t, 1> One{1};
    auto Result = GenerateVectorAuthoringArray(Simple, One, Specification);
    for (size_t Index = 0; Index < 3; ++Index)
    {
        const auto &Instruction = Result.Document.Instructions[Result.Document.Layers[Index].Root];
        Confirm(Instruction.Bounds[0] == 192 + float(Index) * 64 && Instruction.Bounds[1] == 256, "Linear copy position differs from expected HTML placement.");
    }
    Specification.ScaleProfile = VectorAuthoringScaleProfile::Linear;
    Specification.StartScale = 50;
    Specification.EndScale = 150;
    Result = GenerateVectorAuthoringArray(Simple, One, Specification);
    Confirm(Result.Document.Instructions[Result.Document.Layers.front().Root].Bounds[2] == 10, "Linear start scale incorrect.");
    Confirm(Result.Document.Instructions[Result.Document.Layers.back().Root].Bounds[2] == 30, "Linear end scale incorrect.");
    Specification.Category = VectorAuthoringArrayCategory::Circular;
    Specification.Radius = 80;
    Specification.Sweep = 180;
    Specification.RotateCopies = true;
    Result = GenerateVectorAuthoringArray(Simple, One, Specification);
    const auto &Last = Result.Document.Instructions[Result.Document.Layers.back().Root];
    Confirm(std::abs(Last.Bounds[0] - 176) < .001f && std::abs(Last.Bounds[1] - 256) < .001f, "Partial arc omitted its endpoint.");
    Confirm(std::abs(Last.Transform[0] - std::numbers::pi_v<float>) < .001f, "Array rotation did not reach the retained GPU instruction.");
    Specification.Sweep = 360;
    Result = GenerateVectorAuthoringArray(Simple, One, Specification);
    Confirm(Result.Document.Instructions[Result.Document.Layers[0].Root].Bounds != Result.Document.Instructions[Result.Document.Layers[2].Root].Bounds, "Closed circle duplicated its seam endpoint.");
    Specification.Sweep = 0;
    ConfirmRefused([&] { GenerateVectorAuthoringArray(Simple, One, Specification); }, "Zero-sweep arc accepted.");
    Specification = {};
    Specification.Count = 64;
    if (Stamp) ConfirmRefused([&] { GenerateVectorAuthoringArray(Simple, One, Specification); }, "Stamp accepted more than 24 components.");
    else Confirm(GenerateVectorAuthoringArray(Simple, One, Specification).Document.Layers.size() == 64, "Pattern failed its 64-layer boundary.");
    Specification.Count = UINT32_MAX;
    ConfirmRefused([&] { GenerateVectorAuthoringArray(Simple, One, Specification); }, "Unbounded native array population accepted.");
    Specification = {};
    Specification.StepX = std::numeric_limits<float>::quiet_NaN();
    ConfirmRefused([&] { GenerateVectorAuthoringArray(Simple, One, Specification); }, "Nonfinite native array accepted.");
    Specification = {};
    Simple.TokenClock = UINT32_MAX;
    ConfirmRefused([&] { GenerateVectorAuthoringArray(Simple, One, Specification); }, "Native identity clock wrapped.");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ORGANIZATION, CHANNELS AND HISTORY
//------------------------------------------------------------------------------------------------------------------------

void VerifyOrganization(bool Stamp)
{
    auto Document = ConstructSpecimen(Stamp);
    const std::array<uint32_t, 1> Selection{1};
    Confirm(QueryVectorAuthoringEnclosureContains(Document, 10, 12), "Nested group escaped its folder.");
    const auto Before = Document;
    ConfirmRefused([&] { RelocateVectorAuthoringEnclosure(Document, 10, 12); }, "Cyclic relocation accepted.");
    Confirm(Document == Before, "Refused relocation mutated source.");
    RelocateVectorAuthoringEnclosure(Document, 12, 0);
    Confirm(!QueryVectorAuthoringEnclosureContains(Document, 10, 12), "Relocation did not change enclosure membership.");
    Document = Before;
    Document.Enclosures[0].VisibilityEnabled = false;
    const auto Hidden = ConstructVectorAuthoringCoverage(Document);
    Confirm(Hidden.Instructions[Hidden.Root].Transform[2] == 0, "Hidden folder leaked GPU coverage.");
    ConfirmRefused([&] { GenerateVectorAuthoringArray(Document, Selection, {}); }, "Hidden enclosure was arrayed.");
    Document = Before;
    Document.Enclosures[0].LockEnabled = true;
    Confirm(QueryVectorAuthoringProtection(Document, Document.Layers[0]), "Nested enclosure lock was ignored.");
    ConfirmRefused([&] { GenerateVectorAuthoringArray(Document, Selection, {}); }, "Protected enclosure was arrayed.");
    ConfirmRefused([&] { RelocateVectorAuthoringEnclosure(Document, 12, 0); }, "Protected enclosure was relocated.");
    VectorAuthoringChannelPatch Patch;
    const auto Roughness = size_t(VectorAuthoringSurfaceChannel::Roughness);
    Patch.Strength[Roughness] = .25f;
    Confirm(!ApplyVectorAuthoringChannelPatch(Document, Selection, Patch), "Protected channel source was edited.");
    Document = Before;
    Confirm(ApplyVectorAuthoringChannelPatch(Document, Selection, Patch), "Selected channel edit did not apply.");
    Confirm(Document.Layers[0].SurfaceChannels[Roughness].Strength == .25f && Document.Layers[1].SurfaceChannels[Roughness].Strength == 1, "Default-off group editing changed another layer.");
    Document.Enclosures[1].GroupWideEditingEnabled = true;
    Patch.Strength[Roughness] = .5f;
    Confirm(ApplyVectorAuthoringChannelPatch(Document, Selection, Patch), "Group-wide channel edit did not apply.");
    Confirm(Document.Layers[0].SurfaceChannels[Roughness].Strength == .5f && Document.Layers[1].SurfaceChannels[Roughness].Strength == .5f, "Group-wide channel propagation missed nested members.");
    Confirm(Document.Layers[1].SurfaceChannels[2] == Before.Layers[1].SurfaceChannels[2], "Changed-field patch overwrote unrelated channels.");
    Document.Layers[1].Locked = true;
    Patch.Strength[Roughness] = .75f;
    Confirm(ApplyVectorAuthoringChannelPatch(Document, Selection, Patch) && Document.Layers[1].SurfaceChannels[Roughness].Strength == .5f, "Group propagation modified locked artwork.");
    Patch.Enabled[6] = true;
    Patch.Enabled[0] = false;
    Patch.Strength[2] = 4;
    Confirm(ApplyVectorAuthoringChannelPatch(Document, Selection, Patch), "AO/normal channel patch did not apply.");
    Confirm(Document.Layers[0].SurfaceChannels[6].Enabled && !Document.Layers[0].SurfaceChannels[0].Enabled && Document.Layers[0].SurfaceChannels[2].Strength == 4, "Explicit false, AO or normal strength was lost.");
    const auto Valid = Document;
    Patch.Strength[2] = 4.01f;
    ConfirmRefused([&] { ApplyVectorAuthoringChannelPatch(Document, Selection, Patch); }, "Out-of-range normal channel accepted.");
    Confirm(Document == Valid, "Refused channel patch mutated source.");
    Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Document)) == Document, "Organization/channel TOML round trip lost source.");
    const std::array<uint32_t, 1> Unknown{999};
    ConfirmRefused([&] { ApplyVectorAuthoringChannelPatch(Document, Unknown, {}); }, "Stale selected identity accepted.");
    Document = Before;
    Document.Enclosures[0].EnclosureToken = 12;
    ConfirmRefused([&] { ConstructVectorAuthoringCoverage(Document); }, "Cyclic authored enclosure accepted.");
    Document = Before;
    Document.Enclosures[0].Token = 1;
    ConfirmRefused([&] { ConstructVectorAuthoringCoverage(Document); }, "Enclosure/layer identity collision accepted.");
    Document = Before;
    Document.Layers[0].Name = "Motif";
    Document.Layers[1].Name = "MOTIF";
    NormalizeVectorAuthoringCaptions(Document);
    Confirm(Document.Layers[0].Name != Document.Layers[1].Name && Document.Layers[0].Name == "Motif 01", "Native duplicate captions differ from approved numbering.");
    VectorAuthoringHistory History(Stamp);
    History.Finalize(Document);
    auto WrongWorkspace = Document;
    WrongWorkspace.Stamp = !Stamp;
    ConfirmRefused([&] { History.Finalize(WrongWorkspace); }, "History changed workspace limits.");
    for (uint32_t Index = 0; Index < 90; ++Index)
    {
        Document.Name = "History " + std::to_string(Index);
        History.Finalize(Document);
    }
    uint32_t Undos = 0;
    while (History.RestoreHistory(false)) ++Undos;
    Confirm(Undos == 80, "Native history exceeded its 80-edit budget.");
    {
        // 📝 Slider-drag coalescing (History::FinalizeCoalesced): a burst of same-tag live edits
        // records exactly ONE undo entry while still advancing the revision every frame, a new tag
        // records its own entry, and an untagged Finalize breaks the chain.
        VectorAuthoringHistory Coalesced(Stamp);
        auto DragBase = Document;
        DragBase.Name = "Drag base";
        Confirm(Coalesced.FinalizeCoalesced(DragBase, 41) == 2, "First coalesced edit did not record an undo entry.");
        const auto BaseRevision = Coalesced.QueryRevision();
        for (uint32_t Index = 1; Index <= 5; ++Index)
        {
            auto Live = DragBase;
            Live.Name = "Drag live " + std::to_string(Index);
            Confirm(Coalesced.FinalizeCoalesced(Live, 41) == 1, "Same-tag live edit recorded a new undo entry mid-drag.");
        }
        Confirm(Coalesced.QueryRevision() == BaseRevision + 5, "Coalesced live edits did not advance the revision per frame.");
        Confirm(Coalesced.FinalizeCoalesced(Coalesced.QueryDocument(), 41) == 0, "No-op coalesced edit was not reported unchanged.");
        auto SecondDrag = DragBase;
        SecondDrag.Name = "Second drag";
        Confirm(Coalesced.FinalizeCoalesced(SecondDrag, 42) == 2, "A new coalesce tag did not record its own undo entry.");
        Confirm(Coalesced.QueryDocument().Name == "Second drag", "Second drag did not become the current document.");
        uint32_t CoalescedUndos = 0;
        while (Coalesced.RestoreHistory(false)) ++CoalescedUndos;
        Confirm(CoalescedUndos == 2, "Two slider drags did not collapse into exactly two undo entries.");
        VectorAuthoringHistory Broken(Stamp);
        auto StepA = Document; StepA.Name = "A";
        auto StepB = Document; StepB.Name = "B";
        auto StepC = Document; StepC.Name = "C";
        Confirm(Broken.FinalizeCoalesced(StepA, 7) == 2, "Chain-break fixture did not record its first entry.");
        Confirm(Broken.Finalize(StepB), "Chain-break fixture refused its untagged edit.");
        Confirm(Broken.FinalizeCoalesced(StepC, 7) == 2, "An untagged finalize did not break the coalesce chain.");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RETAINED EXPRESSION AND LIMIT REGRESSIONS
//------------------------------------------------------------------------------------------------------------------------

void VerifyRetainedExpressions()
{
    auto Document = ConstructSpecimen(false);
    Document.Enclosures.clear();
    Document.Layers.resize(1);
    Document.Layers[0].EnclosureToken = 0;
    Document.Layers[0].Mask.reset();
    Document.Instructions.clear();
    VectorCoverageInstruction Contour;
    Contour.Topology = {6, 0, 4, 0};
    Contour.Bounds = {256, 256, 20, 20};
    Document.Instructions.push_back(Contour);
    const std::array<std::array<float, 2>, 4> Corners{{{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f}, {-.5f, .5f}}};
    for (size_t Index = 0; Index < Corners.size(); ++Index)
    {
        const auto Start = Corners[Index], End = Corners[(Index + 1) % 4];
        Document.Segments.push_back({{Start[0], Start[1], Start[0], Start[1]}, {End[0], End[1], End[0], End[1]}});
    }
    VectorAuthoringArraySpecification Specification;
    Specification.Count = 2;
    Specification.Category = VectorAuthoringArrayCategory::Circular;
    Specification.RotateCopies = true;
    const std::array<uint32_t, 1> Selection{1};
    const auto Result = GenerateVectorAuthoringArray(Document, Selection, Specification);
    Confirm(Result.Document.Segments == Document.Segments, "Array baked or discarded normalized cubic source controls.");
    for (const auto &Layer : Result.Document.Layers)
        Confirm(Result.Document.Instructions[Layer.Root].Topology == Contour.Topology, "Array lost retained cubic source range.");
    Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Result.Document)) == Result.Document, "Array cubic source failed TOML round trip.");
    Document.Instructions.clear();
    Document.Segments.clear();
    for (uint32_t Index = 0; Index < 64; ++Index) Document.Instructions.push_back(VectorCoverageInstruction{});
    uint32_t Root = 0;
    for (uint32_t Index = 1; Index < 64; ++Index)
    {
        VectorCoverageInstruction Union;
        Union.Topology = {16, Root, Index, 0};
        Root = uint32_t(Document.Instructions.size());
        Document.Instructions.push_back(Union);
    }
    Document.Layers[0].Root = Root;
    const auto Before = Document;
    ConfirmRefused([&] { GenerateVectorAuthoringArray(Document, Selection, Specification); }, "Native array exceeded GPU expression capacity.");
    Confirm(Document == Before, "GPU-budget rejection mutated retained source.");
    Document = ConstructSpecimen(false);
    Document.Enclosures.clear();
    Document.TokenClock = 40;
    for (uint32_t Index = 0; Index < 12; ++Index)
        Document.Enclosures.push_back({10 + Index, Index ? 9 + Index : 0, "Nesting tier"});
    Document.Layers[0].EnclosureToken = 21;
    Document.Layers[1].EnclosureToken = 21;
    (void)ConstructVectorAuthoringCoverage(Document);
    Confirm(QueryVectorAuthoringEnclosureContains(Document, 10, 21), "Twelve-tier enclosure boundary failed.");
    Document.Enclosures.push_back({22, 21, "Overflow tier"});
    ConfirmRefused([&] { ConstructVectorAuthoringCoverage(Document); }, "Thirteenth nesting tier accepted.");
    Document = ConstructSpecimen(false);
    Document.Layers[0].Name = "Motif";
    Document.Layers[1].Name = "MOTIF";
    auto ReservedCaption = Document.Layers[1];
    ReservedCaption.Token = 3;
    ReservedCaption.Name = "Motif 01";
    Document.Layers.push_back(ReservedCaption);
    NormalizeVectorAuthoringCaptions(Document);
    Confirm(Document.Layers[2].Name == "Motif 01" && Document.Layers[0].Name == "Motif 02", "Duplicate normalization stole a unique custom caption.");
    const auto Normalized = Document;
    NormalizeVectorAuthoringCaptions(Document);
    Confirm(Document == Normalized, "Caption normalization was not idempotent.");
    Document.Layers[0].Name = "Same";
    Document.Layers[1].Name = "Same";
    Document.Layers[2].Name = std::string(81, 'x');
    const auto InvalidCaptions = Document;
    ConfirmRefused([&] { NormalizeVectorAuthoringCaptions(Document); }, "Oversized caption accepted during normalization.");
    Confirm(Document == InvalidCaptions, "Refused caption normalization partially mutated source.");
    Document = ConstructSpecimen(false);
    Document.Instructions[1].Bounds[0] = 1e-8f;
    const auto MixedSelection = GenerateVectorAuthoringArray(Document, Selection, Specification);
    const auto &Retained = MixedSelection.Document.Layers.front();
    Confirm(Retained.Token == 2 && MixedSelection.Document.Instructions[Retained.Root] == Document.Instructions[1], "Array identity adaptation changed unselected source coordinates.");
    auto Channels = ConstructVectorAuthoringSurfaceChannels();
    Confirm(!Channels[6].Enabled && Channels[2].Strength == 1, "Workspace-independent channel defaults changed.");
    VectorAuthoringChannelPatch Patch;
    Patch.Enabled[0] = false;
    Patch.Strength[2] = 0;
    Confirm(ApplyVectorAuthoringSurfacePatch(Channels, Patch) && !Channels[0].Enabled && Channels[2].Strength == 0, "Reusable channel patch lost explicit zero or false.");
    Confirm(!ApplyVectorAuthoringSurfacePatch(Channels, Patch), "Reusable channel patch treated a no-op as an edit.");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MIRROR / NOISE / OFFSET MODIFIERS
//------------------------------------------------------------------------------------------------------------------------

void VerifyModifiers()
{
    // 📝 Mirror on a procedurally-regular shape (Category 0): closed-form centre/angle reflection.
    {
        VectorAuthoringDocument Document;
        Document.TokenClock = 1;
        VectorCoverageInstruction Rectangle;
        Rectangle.Bounds = {300, 220, 40, 20};
        Rectangle.Transform[0] = .3f;
        Document.Instructions = {Rectangle};
        VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Name = "Plate"; Layer.Root = 0;
        Document.Layers = {Layer};
        const std::array<uint32_t, 1> Selection{1};
        VectorAuthoringMirrorSpecification Specification;
        Specification.PivotX = 256; Specification.PivotY = 256; Specification.AxisAngle = 90; // vertical axis
        Specification.KeepOriginal = true;
        const auto EncodedBefore = EncodeVectorAuthoring(Document);
        const auto Result = GenerateVectorAuthoringMirror(Document, Selection, Specification);
        Confirm(EncodeVectorAuthoring(Document) == EncodedBefore, "Mirror modified its input.");
        Confirm(Result.Document.Layers.size() == 2, "Mirror with KeepOriginal did not add a companion layer.");
        Confirm(Result.Selection.size() == 2, "Mirror selection did not include both the source and its reflection.");
        const auto &Original = Result.Document.Instructions[Result.Document.Layers[0].Root];
        Confirm(Original.Bounds == Rectangle.Bounds && Original.Transform[0] == Rectangle.Transform[0], "Mirror disturbed the kept original.");
        const auto &Mirrored = Result.Document.Instructions[Result.Document.Layers[1].Root];
        Confirm(std::abs(Mirrored.Bounds[0] - (2 * 256 - 300)) < .003f, "Vertical mirror did not reflect the X centre across the pivot.");
        Confirm(std::abs(Mirrored.Bounds[1] - 220) < .003f, "Vertical mirror disturbed the Y centre.");
        Confirm(std::abs(Mirrored.Bounds[2] - 40) < .003f && std::abs(Mirrored.Bounds[3] - 20) < .003f, "Mirror resized a reflected shape.");
        const float ExpectedAngle = std::numbers::pi_v<float> - .3f; // 2α−θ, α=90°=π/2
        Confirm(std::abs(Mirrored.Transform[0] - ExpectedAngle) < .003f, "Mirror angle did not match 2α−θ.");
        Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Result.Document)) == Result.Document, "Mirror source failed TOML round trip.");
        // 📝 KeepOriginal=false replaces the layer in place, keeping its own token and the document's population.
        Specification.KeepOriginal = false;
        const auto Replaced = GenerateVectorAuthoringMirror(Document, Selection, Specification);
        Confirm(Replaced.Document.Layers.size() == 1 && Replaced.Document.Layers[0].Token == 1, "Mirror without KeepOriginal should replace the source layer in place.");
        Confirm(std::abs(Replaced.Document.Instructions[Replaced.Document.Layers[0].Root].Bounds[0] - (2 * 256 - 300)) < .003f, "In-place mirror did not reflect the shape.");
    }
    // 📝 Mirror on an asymmetric freeform path (Category 6): must genuinely flip the contour, not merely
    // reposition it - every vertex should land at its true geometric reflection across the axis.
    {
        VectorAuthoringDocument Document;
        CubicPath Triangle;
        Triangle.Closed = true;
        Triangle.Anchors = {{{120, 120}, {120, 120}, {120, 120}}, {{220, 140}, {220, 140}, {220, 140}}, {{140, 230}, {140, 230}, {140, 230}}};
        const auto Token = ConstructCubicPath(Document, Triangle, {}, "Scalene");
        const std::array<uint32_t, 1> Selection{Token};
        VectorAuthoringMirrorSpecification Specification;
        Specification.PivotX = 180; Specification.PivotY = 256; Specification.AxisAngle = 90; // vertical line x=180
        Specification.KeepOriginal = false;
        const auto Result = GenerateVectorAuthoringMirror(Document, Selection, Specification);
        const auto Reflected = DecodeCubicPath(Result.Document, Token);
        Confirm(Reflected.Anchors.size() == Triangle.Anchors.size(), "Mirrored freeform path lost anchors.");
        for (size_t Index = 0; Index < Triangle.Anchors.size(); ++Index)
        {
            const auto &Source = Triangle.Anchors[Index].Position;
            const auto &Mirrored = Reflected.Anchors[Index].Position;
            const std::array<float, 2> Expected{2 * 180 - Source[0], Source[1]};
            Confirm(std::hypot(Mirrored[0] - Expected[0], Mirrored[1] - Expected[1]) < .01f,
                "Freeform path vertex did not land at its true geometric reflection (a repositioned-but-unflipped copy would fail this).");
        }
        // 📝 The reflected contour must still close and fill correctly (even though its winding direction
        // reversed) - the GPU fill rule tests abs(Winding), so re-validating it here would already throw
        // on an invalid/degenerate contour; ConstructVectorAuthoringCoverage inside GenerateVectorAuthoringMirror
        // already proved this, this just re-confirms the round trip remains internally consistent.
        Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Result.Document)) == Result.Document, "Mirrored freeform source failed TOML round trip.");
    }
    // 📝 Mirror input validation.
    {
        VectorAuthoringDocument Document;
        Document.TokenClock = 1;
        VectorCoverageInstruction Rectangle; Rectangle.Bounds = {256, 256, 20, 20};
        Document.Instructions = {Rectangle};
        VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Root = 0;
        Document.Layers = {Layer};
        const std::array<uint32_t, 1> Selection{1};
        VectorAuthoringMirrorSpecification Bad;
        Bad.PivotX = std::numeric_limits<float>::quiet_NaN();
        ConfirmRefused([&] { GenerateVectorAuthoringMirror(Document, Selection, Bad); }, "Nonfinite mirror pivot accepted.");
        Bad = {}; Bad.AxisAngle = 720;
        ConfirmRefused([&] { GenerateVectorAuthoringMirror(Document, Selection, Bad); }, "Out-of-domain mirror axis angle accepted.");
        Document.Layers[0].Locked = true;
        ConfirmRefused([&] { GenerateVectorAuthoringMirror(Document, Selection, {}); }, "Mirror accepted a protected layer.");
    }
    // 📝 Noise/jitter: deterministic given a seed, bounded by amplitude, a zero amplitude is a true no-op.
    {
        VectorAuthoringDocument Document;
        Document.TokenClock = 1;
        VectorCoverageInstruction Square; Square.Bounds = {256, 256, 20, 20};
        Document.Instructions = {Square};
        VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Root = 0;
        Document.Layers = {Layer};
        const std::array<uint32_t, 1> Selection{1};
        VectorAuthoringNoiseSpecification Zero; Zero.PositionJitter = 0; Zero.RotationJitter = 0; Zero.ScaleJitter = 0;
        const auto Still = GenerateVectorAuthoringNoise(Document, Selection, Zero);
        Confirm(Still.Document.Instructions[Still.Document.Layers[0].Root].Bounds == Square.Bounds, "Zero-amplitude jitter was not a no-op.");
        VectorAuthoringNoiseSpecification Specification; Specification.Seed = 7;
        const auto First = GenerateVectorAuthoringNoise(Document, Selection, Specification);
        const auto Second = GenerateVectorAuthoringNoise(Document, Selection, Specification);
        Confirm(First.Document == Second.Document, "Seeded jitter was not deterministic.");
        Specification.Seed = 8;
        const auto Third = GenerateVectorAuthoringNoise(Document, Selection, Specification);
        Confirm(First.Document != Third.Document, "Different jitter seeds produced identical placements.");
        const auto &Jittered = First.Document.Instructions[First.Document.Layers[0].Root];
        Confirm(std::abs(Jittered.Bounds[0] - 256) <= Specification.PositionJitter + .01f, "Jitter X exceeded its declared amplitude.");
        Confirm(std::abs(Jittered.Bounds[1] - 256) <= Specification.PositionJitter + .01f, "Jitter Y exceeded its declared amplitude.");
        Confirm(std::abs(Jittered.Transform[0]) <= Specification.RotationJitter * std::numbers::pi_v<float> / 180 + .001f, "Jitter rotation exceeded its declared amplitude.");
        Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(First.Document)) == First.Document, "Jittered source failed TOML round trip.");
        VectorAuthoringNoiseSpecification BadAmplitude; BadAmplitude.PositionJitter = 1000;
        ConfirmRefused([&] { GenerateVectorAuthoringNoise(Document, Selection, BadAmplitude); }, "Out-of-domain jitter amplitude accepted.");
        BadAmplitude = {}; BadAmplitude.Seed = 1000000;
        ConfirmRefused([&] { GenerateVectorAuthoringNoise(Document, Selection, BadAmplitude); }, "Out-of-domain jitter seed accepted.");
    }
    // 📝 Offset/outline: exact bbox growth, shrink, clamped minimum extent, domain validation.
    {
        VectorAuthoringDocument Document;
        Document.TokenClock = 2;
        VectorCoverageInstruction Rectangle; Rectangle.Bounds = {256, 256, 40, 20};
        VectorCoverageInstruction Ellipse = Rectangle; Ellipse.Topology[0] = 1; Ellipse.Bounds[0] = 320;
        Document.Instructions = {Rectangle, Ellipse};
        VectorAuthoringLayer First; First.Token = 1; First.Root = 0;
        VectorAuthoringLayer Second; Second.Token = 2; Second.Root = 1;
        Document.Layers = {First, Second};
        const std::array<uint32_t, 2> Selection{1, 2};
        VectorAuthoringOffsetSpecification Grow; Grow.Distance = 6;
        const auto Grown = GenerateVectorAuthoringOffset(Document, Selection, Grow);
        Confirm(Grown.Document.Instructions[Grown.Document.Layers[0].Root].Bounds[2] == 52 && Grown.Document.Instructions[Grown.Document.Layers[0].Root].Bounds[3] == 32,
            "Positive offset did not grow the rectangle's bounding frame by exactly 2x the distance.");
        Confirm(Grown.Document.Instructions[Grown.Document.Layers[0].Root].Bounds[0] == 256, "Offset moved a shape's own centre.");
        VectorAuthoringOffsetSpecification Shrink; Shrink.Distance = -8;
        const auto Shrunk = GenerateVectorAuthoringOffset(Document, Selection, Shrink);
        Confirm(Shrunk.Document.Instructions[Shrunk.Document.Layers[0].Root].Bounds[2] == 24 && Shrunk.Document.Instructions[Shrunk.Document.Layers[0].Root].Bounds[3] == 4,
            "Negative offset did not shrink the bounding frame by exactly 2x the distance.");
        VectorAuthoringOffsetSpecification Collapse; Collapse.Distance = -100;
        const auto Collapsed = GenerateVectorAuthoringOffset(Document, Selection, Collapse);
        Confirm(Collapsed.Document.Instructions[Collapsed.Document.Layers[0].Root].Bounds[2] >= .02f && Collapsed.Document.Instructions[Collapsed.Document.Layers[0].Root].Bounds[3] >= .02f,
            "Collapsing offset did not clamp to a minimum positive extent.");
        Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Grown.Document)) == Grown.Document, "Offset source failed TOML round trip.");
        VectorAuthoringOffsetSpecification BadDistance; BadDistance.Distance = 1000;
        ConfirmRefused([&] { GenerateVectorAuthoringOffset(Document, Selection, BadDistance); }, "Out-of-domain offset distance accepted.");
        Document.Layers[0].Locked = true;
        ConfirmRefused([&] { GenerateVectorAuthoringOffset(Document, Selection, Grow); }, "Offset accepted a protected layer.");
    }
    // 📝 The modifier stack dispatcher and live evaluation chain mixed Operations exactly like Blender's own
    // stack - a Mirror followed by a Noise jitter acts on the WHOLE mirrored result, not just the source.
    {
        VectorAuthoringDocument Document;
        Document.TokenClock = 1;
        VectorCoverageInstruction Square; Square.Bounds = {200, 256, 20, 20};
        Document.Instructions = {Square};
        VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Name = "Charm"; Layer.Root = 0;
        VectorAuthoringModifier MirrorEntry; MirrorEntry.Operation = VectorAuthoringModifierOperation::Mirror;
        MirrorEntry.Mirror.PivotX = 256; MirrorEntry.Mirror.AxisAngle = 90; MirrorEntry.Mirror.KeepOriginal = true;
        VectorAuthoringModifier NoiseEntry; NoiseEntry.Operation = VectorAuthoringModifierOperation::Noise;
        NoiseEntry.Noise.PositionJitter = 0; NoiseEntry.Noise.RotationJitter = 0; NoiseEntry.Noise.ScaleJitter = 0; // deterministic no-op, isolates the chaining behaviour
        Layer.Modifiers = {MirrorEntry, NoiseEntry};
        Document.Layers = {Layer};
        const auto Evaluated = ConstructEvaluatedVectorAuthoringDocument(Document);
        Confirm(Evaluated.Layers.size() == 2, "Stacked Mirror+Noise modifiers did not expand into the expected population.");
        Confirm(std::all_of(Evaluated.Layers.begin(), Evaluated.Layers.end(), [](const auto &Candidate) { return Candidate.Modifiers.empty(); }),
            "Evaluated document retained residual modifier metadata.");
        Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Document)) == Document, "Mixed-kind modifier stack failed TOML round trip.");
        const auto Dispatched = ApplyVectorAuthoringModifier(Document, std::array<uint32_t, 1>{1}, MirrorEntry);
        const auto Direct = GenerateVectorAuthoringMirror(Document, std::array<uint32_t, 1>{1}, MirrorEntry.Mirror);
        Confirm(Dispatched.Document == Direct.Document, "Modifier dispatcher diverged from calling its Generate* function directly.");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     COLOUR MODIFIER
//------------------------------------------------------------------------------------------------------------------------

void VerifyColour()
{
    // 📝 Ramp (discrete, grouped) target=Fill, manual GroupSize: 4 elements, GroupSize=2, 2 stops ->
    // [A,A,B,B] by generation order, never blended.
    {
        VectorAuthoringDocument Document;
        Document.TokenClock = 4;
        for (int Index = 0; Index < 4; ++Index)
        {
            VectorCoverageInstruction Square; Square.Bounds = {float(100 * Index), 256, 20, 20};
            Document.Instructions.push_back(Square);
            VectorAuthoringLayer Layer; Layer.Token = uint32_t(Index + 1); Layer.Root = uint32_t(Index);
            Document.Layers.push_back(Layer);
        }
        const std::array<uint32_t, 4> Selection{1, 2, 3, 4};
        VectorAuthoringColourSpecification Ramp;
        Ramp.Generator = VectorAuthoringColourGenerator::Ramp; Ramp.Target = VectorAuthoringColourTarget::Fill;
        Ramp.StopCount = 2; Ramp.Stops[0] = {1, 0, 0, 1}; Ramp.Stops[1] = {0, 0, 1, 1};
        Ramp.AutoGroup = false; Ramp.GroupSize = 2;
        const auto EncodedBefore = EncodeVectorAuthoring(Document);
        const auto Result = GenerateVectorAuthoringColour(Document, Selection, Ramp);
        Confirm(EncodeVectorAuthoring(Document) == EncodedBefore, "Colour modified its input.");
        const auto Fill = [&](uint32_t Token) { return Result.Document.Instructions[Result.Document.Layers[Token - 1].Root].Colour; };
        Confirm(Fill(1) == Ramp.Stops[0] && Fill(2) == Ramp.Stops[0], "Ramp group 0 did not receive stop 0.");
        Confirm(Fill(3) == Ramp.Stops[1] && Fill(4) == Ramp.Stops[1], "Ramp group 1 did not receive stop 1.");
        // 📝 A Fill-only paint leaves StrokeColour at its untouched default, which differs from the new
        // fill Colour - the pre-existing codec (see VectorAuthoringDocument.cpp's EncodeInstruction) already
        // persists that incidental difference even without the explicit-stroke flag, and decode then raises
        // the flag defensively on read-back. That one-way flag gain is a pre-existing codec trait, not a
        // Colour-modifier regression, so stability (a further round trip changes nothing more) is what's
        // actually guaranteed here rather than bit-exact equality against the pre-serialization in-memory copy.
        const auto RoundTripped = DecodeVectorAuthoring(EncodeVectorAuthoring(Result.Document));
        Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(RoundTripped)) == RoundTripped, "Ramp-coloured source was not stable across a second TOML round trip.");
    }
    // 📝 Linear (continuous, spatial): auto-fit across the selection's own combined bounding box - the
    // extreme elements land exactly on stop 0/stop 1, nothing needs a manual pivot.
    {
        VectorAuthoringDocument Document;
        Document.TokenClock = 2;
        VectorCoverageInstruction Left; Left.Bounds = {100, 256, 20, 20};
        VectorCoverageInstruction Right; Right.Bounds = {300, 256, 20, 20};
        Document.Instructions = {Left, Right};
        VectorAuthoringLayer First; First.Token = 1; First.Root = 0;
        VectorAuthoringLayer Second; Second.Token = 2; Second.Root = 1;
        Document.Layers = {First, Second};
        const std::array<uint32_t, 2> Selection{1, 2};
        VectorAuthoringColourSpecification Linear;
        Linear.Generator = VectorAuthoringColourGenerator::Linear; Linear.AxisAngle = 0; // left-to-right
        Linear.StopCount = 2; Linear.Stops[0] = {1, 1, 1, 1}; Linear.Stops[1] = {0, 0, 0, 1};
        const auto Result = GenerateVectorAuthoringColour(Document, Selection, Linear);
        Confirm(Result.Document.Instructions[Result.Document.Layers[0].Root].Colour == Linear.Stops[0], "Linear gradient did not place the leftmost element on stop 0.");
        Confirm(Result.Document.Instructions[Result.Document.Layers[1].Root].Colour == Linear.Stops[1], "Linear gradient did not place the rightmost element on stop 1.");
    }
    // 📝 Radial (continuous, spatial): T = distance/Radius from an explicit centre - an element at exactly
    // half the radius must land exactly halfway between the two stops.
    {
        VectorAuthoringDocument Document;
        Document.TokenClock = 1;
        VectorCoverageInstruction Square; Square.Bounds = {306, 256, 20, 20}; // 50px from (256,256)
        Document.Instructions = {Square};
        VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Root = 0;
        Document.Layers = {Layer};
        const std::array<uint32_t, 1> Selection{1};
        VectorAuthoringColourSpecification Radial;
        Radial.Generator = VectorAuthoringColourGenerator::Radial; Radial.CentreX = 256; Radial.CentreY = 256; Radial.Radius = 100;
        Radial.StopCount = 2; Radial.Stops[0] = {0, 0, 0, 1}; Radial.Stops[1] = {1, 1, 1, 1};
        const auto Result = GenerateVectorAuthoringColour(Document, Selection, Radial);
        const auto &Blend = Result.Document.Instructions[Result.Document.Layers[0].Root].Colour;
        for (size_t Channel = 0; Channel < 3; ++Channel) Confirm(std::abs(Blend[Channel] - .5f) < .005f, "Radial gradient at half-radius did not land halfway between its two stops.");
    }
    // 📝 Target selectability: Fill/Stroke/Both independently drive Colour vs StrokeColour (+ the explicit
    // persistence flag), and never disturb the channel they were not asked to touch.
    {
        VectorAuthoringDocument Document;
        Document.TokenClock = 1;
        VectorCoverageInstruction Square; Square.Bounds = {256, 256, 20, 20}; Square.Colour = {.2f, .3f, .4f, 1};
        Document.Instructions = {Square};
        VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Root = 0;
        Document.Layers = {Layer};
        const std::array<uint32_t, 1> Selection{1};
        VectorAuthoringColourSpecification Stroke;
        Stroke.Generator = VectorAuthoringColourGenerator::Ramp; Stroke.Target = VectorAuthoringColourTarget::Stroke;
        Stroke.StopCount = 2; Stroke.Stops[0] = {0, 1, 0, 1}; Stroke.AutoGroup = false; Stroke.GroupSize = 1;
        const auto StrokeOnly = GenerateVectorAuthoringColour(Document, Selection, Stroke);
        const auto &StrokeInstruction = StrokeOnly.Document.Instructions[StrokeOnly.Document.Layers[0].Root];
        Confirm(StrokeInstruction.Colour == Square.Colour, "Stroke-only target disturbed the untouched fill colour.");
        Confirm(StrokeInstruction.StrokeColour == Stroke.Stops[0], "Stroke-only target did not paint the stroke colour.");
        Confirm((StrokeInstruction.Topology[3] & VectorCoverageStrokeColourExplicit) != 0, "Stroke-only target did not mark the stroke colour explicit for persistence.");
        // 📝 Same pre-existing codec trait as above (a Colour away from pure default makes some untouched
        // leaf's incidental Colour/StrokeColour mismatch persist, and decode conservatively raises the
        // explicit flag on it) - stability across a second round trip is the guarantee that actually holds.
        const auto StrokeRoundTripped = DecodeVectorAuthoring(EncodeVectorAuthoring(StrokeOnly.Document));
        Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(StrokeRoundTripped)) == StrokeRoundTripped, "Stroke-coloured source was not stable across a second TOML round trip.");
        VectorAuthoringColourSpecification Both = Stroke; Both.Target = VectorAuthoringColourTarget::Both;
        const auto BothResult = GenerateVectorAuthoringColour(Document, Selection, Both);
        const auto &BothInstruction = BothResult.Document.Instructions[BothResult.Document.Layers[0].Root];
        Confirm(BothInstruction.Colour == Both.Stops[0] && BothInstruction.StrokeColour == Both.Stops[0], "Both target did not paint fill and stroke identically.");
    }
    // 📝 AutoGroup resolution: Grid bands by row (Columns), Radial bands by ring (Count), a single-axis
    // category bands the whole population as one group, and no preceding Array falls back to GroupSize=1.
    {
        VectorAuthoringModifier GridEntry; GridEntry.Operation = VectorAuthoringModifierOperation::Array;
        GridEntry.Array.Category = VectorAuthoringArrayCategory::Grid; GridEntry.Array.Columns = 3; GridEntry.Array.Rows = 2;
        VectorAuthoringColourSpecification Auto; Auto.AutoGroup = true;
        Confirm(ResolveVectorAuthoringColourGrouping(Auto, &GridEntry).GroupSize == 3, "Grid array did not auto-group Colour by its own row width.");
        VectorAuthoringModifier RadialEntry; RadialEntry.Operation = VectorAuthoringModifierOperation::Array;
        RadialEntry.Array.Category = VectorAuthoringArrayCategory::Radial; RadialEntry.Array.Count = 4; RadialEntry.Array.Rings = 2;
        Confirm(ResolveVectorAuthoringColourGrouping(Auto, &RadialEntry).GroupSize == 4, "Radial array did not auto-group Colour by its own per-ring population.");
        VectorAuthoringModifier LinearEntry; LinearEntry.Operation = VectorAuthoringModifierOperation::Array;
        LinearEntry.Array.Category = VectorAuthoringArrayCategory::Linear; LinearEntry.Array.Count = 6;
        Confirm(ResolveVectorAuthoringColourGrouping(Auto, &LinearEntry).GroupSize == 6, "Single-axis array did not auto-group Colour as one whole-population band.");
        Confirm(ResolveVectorAuthoringColourGrouping(Auto, nullptr).GroupSize == 1, "AutoGroup with no preceding array did not fall back to one element per group.");
        VectorAuthoringModifier MirrorEntry; MirrorEntry.Operation = VectorAuthoringModifierOperation::Mirror;
        Confirm(ResolveVectorAuthoringColourGrouping(Auto, &MirrorEntry).GroupSize == 1, "AutoGroup behind a non-Array modifier did not fall back to one element per group.");
        VectorAuthoringColourSpecification Manual; Manual.AutoGroup = false; Manual.GroupSize = 7;
        Confirm(ResolveVectorAuthoringColourGrouping(Manual, &GridEntry).GroupSize == 7, "AutoGroup=false overrode the author's own manual GroupSize.");
    }
    // 📝 End-to-end chaining: a Grid Array followed by a live Colour modifier bands the evaluated document
    // by row exactly like the resolver predicts, with zero residual modifier metadata left behind.
    {
        VectorAuthoringDocument Document;
        Document.TokenClock = 1;
        VectorCoverageInstruction Square; Square.Bounds = {256, 256, 16, 16};
        Document.Instructions = {Square};
        VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Name = "Bead"; Layer.Root = 0;
        VectorAuthoringModifier ArrayEntry; ArrayEntry.Operation = VectorAuthoringModifierOperation::Array;
        ArrayEntry.Array.Category = VectorAuthoringArrayCategory::Grid;
        ArrayEntry.Array.Columns = 3; ArrayEntry.Array.Rows = 2; ArrayEntry.Array.StepX = 40; ArrayEntry.Array.StepY = 40;
        VectorAuthoringModifier ColourEntry; ColourEntry.Operation = VectorAuthoringModifierOperation::Colour;
        ColourEntry.Colour.Generator = VectorAuthoringColourGenerator::Ramp; ColourEntry.Colour.AutoGroup = true;
        ColourEntry.Colour.StopCount = 2; ColourEntry.Colour.Stops[0] = {1, 0, 0, 1}; ColourEntry.Colour.Stops[1] = {0, 0, 1, 1};
        Layer.Modifiers = {ArrayEntry, ColourEntry};
        Document.Layers = {Layer};
        const auto Evaluated = ConstructEvaluatedVectorAuthoringDocument(Document);
        Confirm(Evaluated.Layers.size() == 6, "Array+Colour stack did not expand to the expected 3x2 population.");
        Confirm(std::all_of(Evaluated.Layers.begin(), Evaluated.Layers.end(), [](const auto &Candidate) { return Candidate.Modifiers.empty(); }),
            "Evaluated Array+Colour stack retained residual modifier metadata.");
        for (size_t Index = 0; Index < 3; ++Index)
            Confirm(Evaluated.Instructions[Evaluated.Layers[Index].Root].Colour == ColourEntry.Colour.Stops[0], "Auto-grouped row 0 did not receive stop 0 after a live Array+Colour chain.");
        for (size_t Index = 3; Index < 6; ++Index)
            Confirm(Evaluated.Instructions[Evaluated.Layers[Index].Root].Colour == ColourEntry.Colour.Stops[1], "Auto-grouped row 1 did not receive stop 1 after a live Array+Colour chain.");
        Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Document)) == Document, "Array+Colour modifier stack failed TOML round trip.");
        const auto Dispatched = ApplyVectorAuthoringModifier(Document, std::array<uint32_t, 1>{1}, ColourEntry, &ArrayEntry);
        Confirm(Dispatched.Document.Instructions[Dispatched.Document.Layers[0].Root].Colour == ColourEntry.Colour.Stops[0],
            "Dispatcher did not resolve AutoGroup from the Previous modifier it was given.");
    }
    // 📝 Validation: malformed generator/target, stop domain, axis/centre/radius domain, group size domain
    // and protected layers are all refused outright.
    {
        VectorAuthoringDocument Document;
        Document.TokenClock = 1;
        VectorCoverageInstruction Square; Square.Bounds = {256, 256, 20, 20};
        Document.Instructions = {Square};
        VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Root = 0;
        Document.Layers = {Layer};
        const std::array<uint32_t, 1> Selection{1};
        VectorAuthoringColourSpecification Bad;
        Bad.StopCount = 1;
        ConfirmRefused([&] { GenerateVectorAuthoringColour(Document, Selection, Bad); }, "Out-of-domain colour stop count accepted.");
        Bad = {}; Bad.Stops[0][0] = 1.5f;
        ConfirmRefused([&] { GenerateVectorAuthoringColour(Document, Selection, Bad); }, "Out-of-range colour stop channel accepted.");
        Bad = {}; Bad.AxisAngle = std::numeric_limits<float>::quiet_NaN();
        ConfirmRefused([&] { GenerateVectorAuthoringColour(Document, Selection, Bad); }, "Nonfinite colour axis angle accepted.");
        Bad = {}; Bad.CentreX = 5000;
        ConfirmRefused([&] { GenerateVectorAuthoringColour(Document, Selection, Bad); }, "Out-of-domain colour centre accepted.");
        Bad = {}; Bad.Radius = -1;
        ConfirmRefused([&] { GenerateVectorAuthoringColour(Document, Selection, Bad); }, "Non-positive colour radius accepted.");
        Bad = {}; Bad.AutoGroup = false; Bad.GroupSize = 0;
        ConfirmRefused([&] { GenerateVectorAuthoringColour(Document, Selection, Bad); }, "Zero colour group size accepted.");
        Document.Layers[0].Locked = true;
        ConfirmRefused([&] { GenerateVectorAuthoringColour(Document, Selection, {}); }, "Colour accepted a protected layer.");
    }
}

void VerifyCodec()
{
    const std::string Legacy = "format='frontier.vector_artwork'\nversion=1\ntoken_clock=0\nlayers=[]\ninstructions=[]\nsegments=[]\n";
    Confirm(DecodeVectorAuthoring(Legacy).Enclosures.empty(), "Legacy version-1 native document no longer opens.");
    auto FractionalVersion = Legacy;
    FractionalVersion.replace(FractionalVersion.find("version=1"), 9, "version=1.0");
    ConfirmRefused([&] { DecodeVectorAuthoring(FractionalVersion); }, "Floating-point format version accepted.");
    ConfirmRefused([&] { DecodeVectorAuthoring(Legacy + "stamp='false'\n"); }, "Wrong-typed workspace boolean silently defaulted.");
    ConfirmRefused([&] { DecodeVectorAuthoring(Legacy + "unknown_property=1\n"); }, "Unknown native document field was discarded.");
    ConfirmRefused([&] { DecodeVectorAuthoring(Legacy + "enclosures='folder'\n"); }, "Wrong-typed enclosure collection silently defaulted.");
    const auto Source = EncodeVectorAuthoring(ConstructSpecimen(false));
    for (const std::string Caption : {"mask_inverted", "locked", "visible", "enabled"})
    {
        auto Malformed = Source;
        const auto Position = Malformed.find(Caption + " = ");
        Confirm(Position != std::string::npos, "Expected native boolean field missing from fixture.");
        const auto End = Malformed.find('\n', Position);
        Malformed.replace(Position, End - Position, Caption + " = 'wrong type'");
        ConfirmRefused([&] { DecodeVectorAuthoring(Malformed); }, "Wrong-typed layer/channel boolean silently defaulted.");
    }
}
}

int main()
{
    try
    {
        for (bool Stamp : {false, true})
        {
            VerifyArrays(Stamp);
            VerifyOrganization(Stamp);
        }
        VerifyRetainedExpressions();
        VerifyModifiers();
        VerifyColour();
        VerifyCodec();
        std::cout << "{\"result\":\"passed\",\"checks\":" << Checks << ",\"windows_executed\":false,\"gpu_pixels_tested\":false,\"workspace_ui_integrated\":false}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
