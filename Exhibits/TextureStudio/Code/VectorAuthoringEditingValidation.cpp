/*============================================================================================================================================
                                                        VECTORAUTHORINGEDITINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Exercise production source-only arrays, channel edits, enclosure protection, strict TOML and atomic history on the editing host.

#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
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
