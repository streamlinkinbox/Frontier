/*============================================================================================================================================
                                                         CUBICPATHVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validate retained cubic topology, atomic point operations, source aliases and bounded segment reclamation.

#include "Engine/ContentInterchange/CubicPathEditing.h"
#include <iostream>
#include <limits>

using namespace Frontier;

namespace
{
uint32_t Assertions = 0;

void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Assertions;
}

void ConfirmNear(std::array<float, 2> First, std::array<float, 2> Second)
{
    Confirm(std::hypot(First[0] - Second[0], First[1] - Second[1]) < .003f, "Cubic coordinate drift.");
}

auto Evaluate(const CubicPath &Path, size_t Index, float Parameter)
{
    const auto &First = Path.Anchors[Index];
    const auto &Second = Path.Anchors[(Index + 1) % Path.Anchors.size()];
    const float Complement = 1 - Parameter;
    std::array<float, 2> Position;
    for (size_t Axis = 0; Axis < 2; ++Axis)
        Position[Axis] = Complement * Complement * Complement * First.Position[Axis]
            + 3 * Complement * Complement * Parameter * First.Outgoing[Axis]
            + 3 * Complement * Parameter * Parameter * Second.Incoming[Axis]
            + Parameter * Parameter * Parameter * Second.Position[Axis];
    return Position;
}

template<class Mutation>
void ConfirmAtomic(VectorAuthoringDocument &Document, Mutation Apply)
{
    const auto Before = Document;
    bool Rejected = false;
    try { Apply(); } catch (const std::exception &) { Rejected = true; }
    Confirm(Rejected && Document == Before, "Rejected cubic mutation was not atomic.");
}
}

int main()
{
    try
    {
        for (float θ : {0.f, .43f, -1.2f}) for (bool Closed : {false, true})
        {
            CubicPath Path;
            Path.Closed = Closed;
            Path.Anchors = {{{100, 100}, {100, 100}, {140, 60}},
                {{220, 170}, {180, 130}, {250, 210}}, {{310, 320}, {350, 280}, {310, 320}}};
            VectorAuthoringDocument Document;
            VectorCoverageInstruction Style;
            Style.Transform[0] = θ;
            const auto Token = ConstructCubicPath(Document, Path, Style, "Cubic contour");
            auto Decoded = DecodeCubicPath(Document, Token);
            Confirm(Decoded.Closed == Closed && Decoded.Anchors.size() == 3, "Cubic topology changed.");
            for (size_t Index = 0; Index < Path.Anchors.size(); ++Index)
            {
                ConfirmNear(Decoded.Anchors[Index].Position, Path.Anchors[Index].Position);
                ConfirmNear(Decoded.Anchors[Index].Incoming, Path.Anchors[Index].Incoming);
                ConfirmNear(Decoded.Anchors[Index].Outgoing, Path.Anchors[Index].Outgoing);
            }
            Confirm(Document.Instructions[0].Paint[3] == (Closed ? Style.Paint[3] : 0), "Open path retained a fill.");
            const auto Before = Document;
            ReplaceCubicPath(Document, Token, Decoded);
            Confirm(Document == Before, "Identity edit changed retained storage.");
            auto Reversed = Decoded;
            ReverseCubicPath(Reversed);
            const size_t SegmentCount = Decoded.Closed ? Decoded.Anchors.size() : Decoded.Anchors.size() - 1;
            for (size_t Segment = 0; Segment < SegmentCount; ++Segment)
            {
                const size_t OriginalSegment = (Decoded.Anchors.size() + SegmentCount - 2 - Segment) % SegmentCount;
                for (int Sample = 0; Sample <= 20; ++Sample)
                {
                    const float Parameter = float(Sample) / 20;
                    ConfirmNear(Evaluate(Reversed, Segment, Parameter), Evaluate(Decoded, OriginalSegment, 1 - Parameter));
                }
            }
            ReverseCubicPath(Reversed);
            Confirm(Reversed == Decoded, "Double path reversal changed retained controls.");
            auto Subdivision = Decoded;
            SubdivideCubicSegment(Subdivision, 0, .37f);
            for (int Sample = 0; Sample <= 100; ++Sample)
            {
                const float Parameter = float(Sample) / 100;
                ConfirmNear(Evaluate(Decoded, 0, Parameter), Parameter <= .37f
                    ? Evaluate(Subdivision, 0, Parameter / .37f)
                    : Evaluate(Subdivision, 1, (Parameter - .37f) / .63f));
            }
            if (Closed)
            {
                auto Closing = Decoded;
                SubdivideCubicSegment(Closing, 2);
                for (int Sample = 0; Sample <= 20; ++Sample)
                {
                    const float Parameter = float(Sample) / 20;
                    ConfirmNear(Evaluate(Decoded, 2, Parameter), Parameter <= .5f
                        ? Evaluate(Closing, 2, Parameter * 2) : Evaluate(Closing, 3, Parameter * 2 - 1));
                }
            }
            auto Translated = Decoded;
            TranslateCubicControl(Translated, 1, CubicControl::Anchor, {230, 190});
            ConfirmNear(Translated.Anchors[1].Incoming, {190, 150});
            ConfirmNear(Translated.Anchors[1].Outgoing, {260, 230});
            TranslateCubicControl(Translated, 1, CubicControl::Outgoing, {270, 200});
            ConfirmNear(Translated.Anchors[1].Incoming, {190, 180});
            TranslateCubicControl(Translated, 1, CubicControl::Outgoing, {280, 220}, true);
            ConfirmNear(Translated.Anchors[1].Incoming, {190, 180});
            ConfigureCubicTangent(Translated, 1, false);
            Confirm(Translated.Anchors[1].Incoming == Translated.Anchors[1].Position, "Corner tangent retained a handle.");
            ConfigureCubicTangent(Translated, 1, true);
            for (size_t Axis = 0; Axis < 2; ++Axis)
                Confirm(std::abs(Translated.Anchors[1].Incoming[Axis] + Translated.Anchors[1].Outgoing[Axis]
                    - 2 * Translated.Anchors[1].Position[Axis]) < .001f, "Smooth tangent is asymmetric.");
            PartitionCubicAnchor(Subdivision, 1);
            Confirm(Subdivision.Anchors.size() == 3, "Anchor removal failed.");
            // 📝 Preserve a second layer alias and an owned cubic mask through repeated copy-on-write edits.
            auto Alias = Document.Layers[0];
            Alias.Token = ++Document.TokenClock;
            Alias.Name = "Independent contour";
            Alias.Mask = Document.Instructions[Alias.Root];
            Document.Layers.push_back(Alias);
            const auto AliasPath = DecodeCubicPath(Document, Alias.Token);
            const auto MaskSource = *Document.Layers[1].Mask;
            const auto MaskSegment = Document.Segments[MaskSource.Topology[1]];
            for (int Iteration = 0; Iteration < 4100; ++Iteration)
            {
                auto Edited = DecodeCubicPath(Document, Token);
                TranslateCubicControl(Edited, 1, CubicControl::Anchor, {220 + float(Iteration % 2), 170});
                ReplaceCubicPath(Document, Token, Edited);
                Confirm(Document.Segments.size() <= 6, "Repeated point edits leaked segment storage.");
            }
            Confirm(DecodeCubicPath(Document, Alias.Token) == AliasPath, "Editing mutated another layer alias.");
            Confirm(Document.Segments[Document.Layers[1].Mask->Topology[1]] == MaskSegment, "Editing mutated owned mask controls.");
            auto Invalid = Path;
            Invalid.Anchors[0].Position[0] = std::numeric_limits<float>::quiet_NaN();
            ConfirmAtomic(Document, [&] { ReplaceCubicPath(Document, Token, Invalid); });
            ConfirmAtomic(Document, [&] { ReplaceCubicPath(Document, UINT32_MAX, Path); });
            Document.Layers[0].Locked = true;
            ConfirmAtomic(Document, [&] { ReplaceCubicPath(Document, Token, Path); });
            Document.Layers[0].Locked = false;
            Document.Layers[0].Visible = false;
            ConfirmAtomic(Document, [&] { ReplaceCubicPath(Document, Token, Path); });
            Document.Layers[0].Visible = true;
            VectorAuthoringHistory History;
            History.Finalize(Document);
            const auto Original = History.QueryDocument();
            auto Replacement = Original;
            ReplaceCubicPath(Replacement, Token, Translated);
            History.Finalize(Replacement);
            Confirm(History.RestoreHistory(false) && History.QueryDocument() == Original, "Cubic history undo failed.");
            Confirm(History.RestoreHistory(true) && History.QueryDocument() == Replacement, "Cubic history redo failed.");
        }
        for (uint32_t Category = 0; Category <= 5; ++Category)
        {
            VectorAuthoringDocument Document;
            VectorCoverageInstruction Style;
            Style.Bounds = {256, 256, 120, 80};
            Style.Topology[0] = Category;
            const auto Token = ConstructVectorAuthoringPrimitive(Document, Style, "Primitive contour");
            const auto Path = DerivePrimitiveCubicPath(Document, Token);
            ReplaceCubicPath(Document, Token, Path);
            Confirm(DecodeCubicPath(Document, Token).Closed, "Converted primitive is not closed.");
        }
        // 📝 Reject compound contours without flattening or dropping their retained subpaths.
        VectorAuthoringDocument Compound;
        CubicPath Triangle;
        Triangle.Closed = true;
        Triangle.Anchors = {{{100, 100}, {100, 100}, {100, 100}},
            {{200, 100}, {200, 100}, {200, 100}}, {{150, 200}, {150, 200}, {150, 200}}};
        const auto CompoundToken = ConstructCubicPath(Compound, Triangle, {}, "Compound contour");
        auto SecondContour = Compound.Segments;
        for (auto &Segment : SecondContour)
        {
            Segment.StartControl[0] += 1; Segment.StartControl[2] += 1;
            Segment.ControlEnd[0] += 1; Segment.ControlEnd[2] += 1;
        }
        Compound.Segments.insert(Compound.Segments.end(), SecondContour.begin(), SecondContour.end());
        Compound.Instructions[0].Topology[2] = 6;
        ConfirmAtomic(Compound, [&] { ReplaceCubicPath(Compound, CompoundToken, Triangle); });
        auto Invalid = Triangle;
        Invalid.Anchors.resize(401, Triangle.Anchors[0]);
        VectorAuthoringDocument Empty;
        ConfirmAtomic(Empty, [&] { ConstructCubicPath(Empty, Invalid, {}, "Excess anchors"); });
        Invalid = Triangle;
        Invalid.Winding = 2;
        ConfirmAtomic(Empty, [&] { ConstructCubicPath(Empty, Invalid, {}, "Invalid winding"); });
        Invalid = Triangle;
        Invalid.Anchors.resize(2);
        ConfirmAtomic(Empty, [&] { ConstructCubicPath(Empty, Invalid, {}, "Too few anchors"); });
        Invalid = Triangle;
        Invalid.Anchors[0].Position[0] = 20000;
        ConfirmAtomic(Empty, [&] { ConstructCubicPath(Empty, Invalid, {}, "Out of domain"); });
        auto Saturated = Empty;
        for (size_t Index = 0; Index < 64; ++Index)
            ConstructVectorAuthoringPrimitive(Saturated, {}, "Retained contour");
        ConfirmAtomic(Saturated, [&] { ConstructCubicPath(Saturated, Triangle, {}, "Too many layers"); });
        for (int Index = 0; Index < 360; ++Index)
        {
            const float θ = float(Index) * .01745329252f;
            const std::array<float, 2> Origin{120, 230};
            const auto Destination = ConstrainCubicDirection(Origin, {120 + 80 * std::cos(θ), 230 + 80 * std::sin(θ)});
            const float ΔX = Destination[0] - Origin[0], ΔY = Destination[1] - Origin[1];
            Confirm(std::abs(std::hypot(ΔX, ΔY) - 80) < .001f, "Angle constraint changed tangent length.");
            const float Angle = std::atan2(ΔY, ΔX) / .7853981634f;
            Confirm(std::abs(Angle - std::round(Angle)) < .0001f, "Angle constraint is not on a 45-degree direction.");
        }
        Confirm(ConstrainCubicDirection({120, 230}, {120, 230}) == std::array<float, 2>{120, 230}, "Zero-length constraint moved its origin.");
        for (float θ : {0.f, .47f}) for (bool RetainedStart : {false, true}) for (bool ConsumedEnd : {false, true})
        {
            VectorAuthoringDocument Source;
            CubicPath First, Second;
            First.Anchors = {{{80, 100}, {80, 100}, {110, 70}}, {{150, 160}, {120, 130}, {180, 190}}, {{220, 100}, {190, 130}, {220, 100}}};
            Second.Anchors = {{{280, 160}, {280, 160}, {310, 130}}, {{350, 220}, {320, 190}, {380, 250}}, {{420, 160}, {390, 190}, {420, 160}}};
            VectorCoverageInstruction Style; Style.Transform[0] = θ; Style.Transform[1] = 4;
            const auto Retained = ConstructCubicPath(Source, First, Style, "Retained contour");
            Style.Transform[0] = -.31f;
            const auto Consumed = ConstructCubicPath(Source, Second, Style, "Consumed contour");
            auto Alias = Source.Layers[0]; Alias.Token = ++Source.TokenClock; Alias.Name = "Independent first alias"; Alias.Visible = false;
            Source.Layers.push_back(Alias);
            auto OtherAlias = Source.Layers[1]; OtherAlias.Token = ++Source.TokenClock; OtherAlias.Name = "Independent second alias"; OtherAlias.Visible = false;
            OtherAlias.Mask = Source.Instructions[OtherAlias.Root]; Source.Layers.push_back(OtherAlias);
            const auto Before = Source;
            const auto FirstBefore = DecodeCubicPath(Source, Retained), SecondBefore = DecodeCubicPath(Source, Consumed);
            Confirm(IntegrateCubicPaths(Source, Retained, Consumed, RetainedStart, ConsumedEnd) == Retained, "Join returned a different retained token.");
            Confirm(Source.Layers.size() == 3 && Source.Layers[0].Token == Retained && Source.Layers[0].Name == Before.Layers[0].Name,
                "Join did not consume exactly one layer while retaining identity.");
            Confirm(Source.TokenClock == Before.TokenClock, "Join recycled or issued an unrelated identity.");
            Confirm(DecodeCubicPath(Source, Alias.Token) == FirstBefore && DecodeCubicPath(Source, OtherAlias.Token) == SecondBefore,
                "Join changed an unselected retained alias.");
            const auto Joined = DecodeCubicPath(Source, Retained);
            Confirm(Joined.Anchors.size() == 6 && !Joined.Closed, "Separated endpoints did not receive one bridge segment.");
            for (const auto &Original : {FirstBefore, SecondBefore})
                for (size_t Segment = 0; Segment + 1 < Original.Anchors.size(); ++Segment)
                {
                    bool Matched = false;
                    for (size_t Candidate = 0; Candidate + 1 < Joined.Anchors.size(); ++Candidate)
                    {
                        const auto Distance = [](auto A, auto B) { return std::hypot(A[0] - B[0], A[1] - B[1]); };
                        const bool Forward = Distance(Original.Anchors[Segment].Position, Joined.Anchors[Candidate].Position) < .003f
                            && Distance(Original.Anchors[Segment + 1].Position, Joined.Anchors[Candidate + 1].Position) < .003f;
                        const bool Reverse = Distance(Original.Anchors[Segment].Position, Joined.Anchors[Candidate + 1].Position) < .003f
                            && Distance(Original.Anchors[Segment + 1].Position, Joined.Anchors[Candidate].Position) < .003f;
                        if (!Forward && !Reverse) continue;
                        for (int Sample = 0; Sample <= 20; ++Sample)
                        {
                            const float Parameter = float(Sample) / 20;
                            ConfirmNear(Evaluate(Original, Segment, Parameter), Evaluate(Joined, Candidate, Forward ? Parameter : 1 - Parameter));
                        }
                        Matched = true;
                    }
                    Confirm(Matched, "Join lost an original cubic segment.");
                }
            Confirm(Source.Segments[Source.Layers[2].Mask->Topology[1]] == Before.Segments[Before.Layers[3].Mask->Topology[1]],
                "Join modified an unselected owned mask control.");
            VectorAuthoringHistory History; History.Finalize(Before); History.Finalize(Source);
            Confirm(History.RestoreHistory(false) && History.QueryDocument() == Before, "Join source-history undo failed.");
            Confirm(History.RestoreHistory(true) && History.QueryDocument() == Source, "Join source-history redo failed.");
            for (int Refusal = 0; Refusal < 10; ++Refusal)
            {
                auto Invalid = Before;
                if (Refusal == 0) Invalid.Layers[1].Locked = true;
                if (Refusal == 1) Invalid.Layers[1].Visible = false;
                if (Refusal == 2) Invalid.Layers[1].Mask = Invalid.Instructions[Invalid.Layers[1].Root];
                if (Refusal == 3) Invalid.Instructions[Invalid.Layers[1].Root].Colour[0] = .2f;
                if (Refusal == 4) Invalid.Instructions[Invalid.Layers[1].Root].Transform[1] = 8;
                if (Refusal == 5) for (auto &Instruction : Invalid.Instructions) Instruction.Transform[3] = 1;
                if (Refusal == 6) for (auto &Instruction : Invalid.Instructions) Instruction.Transform[2] = .5f;
                if (Refusal == 7) Invalid.Layers[1].SurfaceChannels[1].Strength = .3f;
                if (Refusal == 8) std::swap(Invalid.Layers[1], Invalid.Layers[2]);
                if (Refusal == 9)
                {
                    VectorAuthoringEnclosure Group; Group.Token = 70; Group.Name = "Independent group";
                    Invalid.Enclosures.push_back(Group); Invalid.TokenClock = 70; Invalid.Layers[1].EnclosureToken = 70;
                }
                (void)ConstructVectorAuthoringCoverage(Invalid);
                ConfirmAtomic(Invalid, [&] { IntegrateCubicPaths(Invalid, Retained, Consumed, RetainedStart, ConsumedEnd); });
            }
            ConfirmAtomic(Source, [&] { IntegrateCubicPaths(Source, Retained, Retained); });
            ConfirmAtomic(Source, [&] { IntegrateCubicPaths(Source, Retained, UINT32_MAX); });
        }
        {
            VectorAuthoringDocument Source;
            CubicPath First, Second;
            First.Anchors = {{{100, 100}, {100, 100}, {130, 80}}, {{200, 100}, {170, 80}, {200, 100}}};
            Second.Anchors = {{{200, 100}, {200, 100}, {230, 120}}, {{300, 200}, {270, 180}, {300, 200}}};
            VectorCoverageInstruction Style; Style.Transform[1] = 4;
            const auto A = ConstructCubicPath(Source, First, Style, "Coincident first");
            const auto B = ConstructCubicPath(Source, Second, Style, "Coincident second");
            IntegrateCubicPaths(Source, A, B);
            const auto Joined = DecodeCubicPath(Source, A);
            Confirm(Joined.Anchors.size() == 3 && Joined.Anchors[1].Position == std::array<float, 2>{200, 100}, "Coincident join did not merge its shared endpoint.");
            ConfirmNear(Joined.Anchors[1].Incoming, First.Anchors[1].Incoming);
            ConfirmNear(Joined.Anchors[1].Outgoing, Second.Anchors[0].Outgoing);
            VectorAuthoringDocument Closed;
            First.Anchors.push_back({{200, 200}, {200, 200}, {200, 200}});
            Second.Anchors = {{{200, 200}, {200, 200}, {200, 200}}, {{100, 100}, {100, 100}, {100, 100}}};
            const auto C = ConstructCubicPath(Closed, First, Style, "Closing first");
            const auto D = ConstructCubicPath(Closed, Second, Style, "Closing second");
            IntegrateCubicPaths(Closed, C, D);
            Confirm(DecodeCubicPath(Closed, C).Closed && DecodeCubicPath(Closed, C).Anchors.size() == 3, "Join did not produce a valid three-anchor closed contour.");
            Confirm(Closed.Instructions[Closed.Layers[0].Root].Paint[3] == 0, "Closing join silently enabled fill.");
        }
        {
            VectorAuthoringDocument Source;
            CubicPath First, Second;
            First.Anchors = {{{100, 100}, {100, 100}, {130, 70}}, {{200, 100}, {170, 70}, {200, 100}}};
            Second.Anchors = {{{200, 100}, {200, 100}, {170, 130}}, {{100, 100}, {130, 130}, {100, 100}}};
            VectorCoverageInstruction Style; Style.Transform[1] = 4;
            ConstructCubicPath(Source, First, Style, "Two-anchor closure first");
            ConstructCubicPath(Source, Second, Style, "Two-anchor closure second");
            ConfirmAtomic(Source, [&] { IntegrateCubicPaths(Source, 1, 2); });
        }
        {
            VectorAuthoringDocument Capacity;
            CubicPath Path;
            VectorCoverageInstruction Style; Style.Transform[1] = 4;
            for (int Index = 0; Index < 400; ++Index)
            {
                const std::array<float, 2> Point{50 + float(Index), 100}; Path.Anchors.push_back({Point, Point, Point});
            }
            for (int Index = 0; Index < 64; ++Index) ConstructCubicPath(Capacity, Path, Style, "API-maximum segment load");
            Confirm(Capacity.Segments.size() == 25536, "Join fixture missed the API-maximum segment load.");
            ConfirmAtomic(Capacity, [&] { IntegrateCubicPaths(Capacity, 1, 2); });
        }
        {
            // The GPU segment budget itself (32,768) exceeds what the 64-layer /
            // 400-anchor editing API can address, so its boundary is pinned here at
            // the coverage-validation level instead.
            VectorCoverageProgram Program;
            VectorCoverageInstruction Contour;
            Contour.Topology = {6, 0, 32768, 0};
            Contour.Bounds = {256, 256, 512, 512};
            Program.Instructions = {Contour};
            Program.Segments.resize(32768);
            Program.Root = 0;
            ValidateVectorCoverage(Program);
            Program.Segments.emplace_back();
            Program.Instructions[0].Topology[2] = 32769;
            bool Rejected = false;
            try { ValidateVectorCoverage(Program); } catch (const std::exception &) { Rejected = true; }
            Confirm(Rejected, "Vector coverage accepted segments beyond the GPU budget.");
        }
        for (bool Closed : {false, true})
        {
            CubicPath Path;
            Path.Closed = Closed;
            Path.Anchors = {{{80, 100}, {50, 80}, {130, 30}},
                {{300, 220}, {240, 100}, {350, 300}}, {{180, 380}, {280, 390}, {90, 330}}};
            const size_t Count = Closed ? 3 : 2;
            for (size_t Segment = 0; Segment < Count; ++Segment) for (float Parameter : {.07f, .23f, .5f, .77f, .93f})
            {
                const auto Point = Evaluate(Path, Segment, Parameter);
                const auto Projection = ProjectCubicSegment(Path, Point);
                Confirm(Projection.SegmentIndex == Segment && Projection.Distance < .003f, "Curve projection missed an interior or seam segment.");
                Confirm(std::abs(Projection.Parameter - Parameter) < .0001f, "Curve projection parameter drifted.");
                auto Bent = Path;
                BendCubicSegment(Bent, Segment, Parameter, {12, -17});
                ConfirmNear(Evaluate(Bent, Segment, Parameter), {Point[0] + 12, Point[1] - 17});
                for (size_t Index = 0; Index < Path.Anchors.size(); ++Index)
                {
                    Confirm(Bent.Anchors[Index].Position == Path.Anchors[Index].Position, "Bending moved an anchor.");
                    if (Index != Segment) Confirm(Bent.Anchors[Index].Outgoing == Path.Anchors[Index].Outgoing, "Bending touched an unrelated outgoing control.");
                    if (Index != (Segment + 1) % Path.Anchors.size()) Confirm(Bent.Anchors[Index].Incoming == Path.Anchors[Index].Incoming, "Bending touched an unrelated incoming control.");
                }
                auto Split = Path;
                SubdivideCubicSegment(Split, Segment, Projection.Parameter);
                ConfirmNear(Split.Anchors[Segment + 1].Position, Point);
            }
            for (float Parameter : {0.f, 1.f, std::numeric_limits<float>::quiet_NaN()})
            {
                auto Invalid = Path; bool Rejected = false;
                try { BendCubicSegment(Invalid, 0, Parameter, {5, 5}); } catch (const std::exception &) { Rejected = true; }
                Confirm(Rejected && Invalid == Path, "Invalid bend was not atomic.");
            }
            auto Invalid = Path; bool Rejected = false;
            try { BendCubicSegment(Invalid, 0, .5f, {std::numeric_limits<float>::infinity(), 0}); }
            catch (const std::exception &) { Rejected = true; }
            Confirm(Rejected && Invalid == Path, "Nonfinite bend was not atomic.");
        }
        for (bool Closed : {false, true})
        {
            CubicPath Path;
            Path.Closed = Closed;
            for (auto Position : {std::array<float, 2>{100, 120}, {220, 300}, {330, 160}, {410, 300}})
                Path.Anchors.push_back({Position, Position, Position});
            auto Automatic = Path;
            ConfigureAutomaticCubicTangents(Automatic, {0, 1, 2, 3});
            for (size_t Index = 0; Index < 4; ++Index)
            {
                Confirm(Automatic.Anchors[Index].Position == Path.Anchors[Index].Position, "Automatic curves moved anchors.");
                const auto &Anchor = Automatic.Anchors[Index];
                const float IncomingX = Anchor.Position[0] - Anchor.Incoming[0], IncomingY = Anchor.Position[1] - Anchor.Incoming[1];
                const float OutgoingX = Anchor.Outgoing[0] - Anchor.Position[0], OutgoingY = Anchor.Outgoing[1] - Anchor.Position[1];
                Confirm(std::abs(IncomingX * OutgoingY - IncomingY * OutgoingX) < .01f, "Automatic tangents are not aligned.");
            }
            auto Again = Automatic; ConfigureAutomaticCubicTangents(Again, {3, 1, 0, 2});
            Confirm(Again == Automatic, "Automatic tangents depend on index order or are not idempotent.");
            auto Selected = Path; ConfigureAutomaticCubicTangents(Selected, {1, 2});
            Confirm(Selected.Anchors[0] == Path.Anchors[0] && Selected.Anchors[3] == Path.Anchors[3], "Automatic selection touched unrelated anchors.");
            auto Moved = Automatic; TranslateCubicAnchors(Moved, {0, 2}, {13, -8});
            for (size_t Index = 0; Index < 4; ++Index)
            {
                if (Index == 0 || Index == 2)
                {
                    ConfirmNear(Moved.Anchors[Index].Position, {Automatic.Anchors[Index].Position[0] + 13, Automatic.Anchors[Index].Position[1] - 8});
                    ConfirmNear(Moved.Anchors[Index].Incoming, {Automatic.Anchors[Index].Incoming[0] + 13, Automatic.Anchors[Index].Incoming[1] - 8});
                    ConfirmNear(Moved.Anchors[Index].Outgoing, {Automatic.Anchors[Index].Outgoing[0] + 13, Automatic.Anchors[Index].Outgoing[1] - 8});
                }
                else Confirm(Moved.Anchors[Index] == Automatic.Anchors[Index], "Multipoint translation touched an unselected anchor.");
            }
            auto Removed = Path; PartitionCubicAnchors(Removed, {1});
            Confirm(Removed.Anchors.size() == 3 && Removed.Anchors[1] == Path.Anchors[2], "Batch deletion reordered remaining anchors.");
            for (const std::vector<size_t> &Indices : {std::vector<size_t>{}, {0, 0}, {4}})
                for (int Operation = 0; Operation < 3; ++Operation)
                {
                    auto Invalid = Path; bool Rejected = false;
                    try
                    {
                        if (Operation == 0) ConfigureAutomaticCubicTangents(Invalid, Indices);
                        if (Operation == 1) TranslateCubicAnchors(Invalid, Indices, {2, 2});
                        if (Operation == 2) PartitionCubicAnchors(Invalid, Indices);
                    }
                    catch (const std::exception &) { Rejected = true; }
                    Confirm(Rejected && Invalid == Path, "Batch invalid-index refusal was not atomic.");
                }
            auto Invalid = Path; bool Rejected = false;
            try { PartitionCubicAnchors(Invalid, {0, 1, 2}); } catch (const std::exception &) { Rejected = true; }
            Confirm(Rejected && Invalid == Path, "Batch deletion ignored minimum anchor count.");
            Rejected = false;
            try { TranslateCubicAnchors(Invalid, {0, 1}, {std::numeric_limits<float>::infinity(), 0}); }
            catch (const std::exception &) { Rejected = true; }
            Confirm(Rejected && Invalid == Path, "Batch nonfinite translation was not atomic.");
            auto Repeated = Path; Repeated.Anchors[2].Position = Repeated.Anchors[0].Position;
            ConfigureAutomaticCubicTangents(Repeated, {1}); ValidateCubicPath(Repeated);
            Confirm(Repeated.Anchors[1].Incoming == Repeated.Anchors[1].Position && Repeated.Anchors[1].Outgoing == Repeated.Anchors[1].Position,
                "Reversing/degenerate neighbour chord did not yield stable collapsed tangents.");
        }
        std::cout << "{\"assertions\":" << Assertions << ",\"result\":\"passed\"}\n";
    }
    catch (const std::exception &Exception)
    {
        std::cerr << Exception.what() << '\n';
        return 1;
    }
}
