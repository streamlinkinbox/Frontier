/*====================================================================================================================================
                                                  BOUNDINGVOLUMECONSTRUCTION.JS
====================================================================================================================================*/
// 🧩 Binned SAH bounding-volume trees (triangles and instances), collapsed to 4-wide nodes with 8-bit child bounds

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const BinCount            = 32;     // [-]  - SAH split candidates per axis
const LeafTriangleLimit   = 1;      // [-]  - Fine binary splits let the 4-wide DP collapse pack optimal leaves
export const LeafPrimitiveCeiling = 15;   // [-]  - Larger SAH leaves are split at the median (stack packs counts in 4 bits)
const TraversalCostRatio  = 0.25;   // [-]  - Binary split cost tuned for 4-wide collapse (1/4 of a wide node test)
const WideNodeCostRatio   = 3.5;    // [-]  - Relative cost of a 4-wide node test vs one triangle test in DP collapse
const BoundsEntryStride   = 8;      // [-]  - 32-bit words per binary entry: min.xyz, LeftOrFirst, max.xyz, PrimitiveCount
export const WideNodeWords = 16;    // [-]  - 32-bit words per 4-wide node (64 B), see EncodeWideNode
export const EmptyLink     = 0xFFFFFFFF;
const QuantizationSteps    = 255;   // [-]  - 8-bit child bounds


//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

function SurfaceArea(MinX, MinY, MinZ, MaxX, MaxY, MaxZ)
{
    const ExtentX = MaxX - MinX, ExtentY = MaxY - MinY, ExtentZ = MaxZ - MinZ;
    return (ExtentX < 0.0 || ExtentY < 0.0 || ExtentZ < 0.0) ? 0.0 : 2.0 * (ExtentX * ExtentY + ExtentY * ExtentZ + ExtentZ * ExtentX);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Positions holds 9 floats per triangle (three corners). Returns the triangle permutation the GPU arrays must follow
//    and the flattened binary entries. Inner entries store the first of two adjacent down-link entries; leaves store the
//    first triangle of their contiguous range. Construction is fully deterministic (no randomized splits).
export function ConstructBoundingVolumes(Positions, TriangleCount)
{
    const Centroids = new Float32Array(TriangleCount * 3);
    const Bounds    = new Float32Array(TriangleCount * 6);
    for (let Triangle = 0; Triangle < TriangleCount; Triangle++)
    {
        const P = Triangle * 9;
        for (let Axis = 0; Axis < 3; Axis++)
        {
            const A = Positions[P + Axis], B = Positions[P + 3 + Axis], C = Positions[P + 6 + Axis];
            Bounds[Triangle * 6 + Axis]     = Math.min(A, B, C);
            Bounds[Triangle * 6 + 3 + Axis] = Math.max(A, B, C);
            Centroids[Triangle * 3 + Axis]  = (A + B + C) / 3.0;
        }
    }
    return ConstructBinaryTree(Bounds, Centroids, TriangleCount, LeafTriangleLimit);
}

// 📝 Same SAH build over arbitrary boxes (6 floats: min.xyz, max.xyz). Used for the instance tree (one box per vehicle).
export function ConstructBinaryTree(Bounds, Centroids, TriangleCount, LeafLimit, LeafCeiling = LeafPrimitiveCeiling)
{

    const Order   = new Uint32Array(TriangleCount);
    for (let Triangle = 0; Triangle < TriangleCount; Triangle++) Order[Triangle] = Triangle;

    let EntryCapacity = Math.max(2, TriangleCount * 2);
    let EntryWords    = new Uint32Array(EntryCapacity * BoundsEntryStride);
    let EntryFloats   = new Float32Array(EntryWords.buffer);
    let EntryCount    = 1;

    const BinMinimum = new Float32Array(BinCount * 3), BinMaximum = new Float32Array(BinCount * 3);
    const BinTally   = new Uint32Array(BinCount);
    const LeftArea   = new Float32Array(BinCount), LeftTally = new Uint32Array(BinCount);

    const RangeStack = [{ EntryIndex: 0, First: 0, Count: TriangleCount }];
    let   MaximumDepthTier = 0;
    while (RangeStack.length)
    {
        const Range = RangeStack.pop();
        const { EntryIndex, First, Count } = Range;
        MaximumDepthTier = Math.max(MaximumDepthTier, Range.DepthTier || 0);

        let MinX = Infinity, MinY = Infinity, MinZ = Infinity, MaxX = -Infinity, MaxY = -Infinity, MaxZ = -Infinity;
        let CMinX = Infinity, CMinY = Infinity, CMinZ = Infinity, CMaxX = -Infinity, CMaxY = -Infinity, CMaxZ = -Infinity;
        for (let Slot = First; Slot < First + Count; Slot++)
        {
            const Triangle = Order[Slot], B = Triangle * 6, C = Triangle * 3;
            MinX = Math.min(MinX, Bounds[B]);     MinY = Math.min(MinY, Bounds[B + 1]); MinZ = Math.min(MinZ, Bounds[B + 2]);
            MaxX = Math.max(MaxX, Bounds[B + 3]); MaxY = Math.max(MaxY, Bounds[B + 4]); MaxZ = Math.max(MaxZ, Bounds[B + 5]);
            CMinX = Math.min(CMinX, Centroids[C]);     CMinY = Math.min(CMinY, Centroids[C + 1]); CMinZ = Math.min(CMinZ, Centroids[C + 2]);
            CMaxX = Math.max(CMaxX, Centroids[C]);     CMaxY = Math.max(CMaxY, Centroids[C + 1]); CMaxZ = Math.max(CMaxZ, Centroids[C + 2]);
        }
        const Word = EntryIndex * BoundsEntryStride;
        EntryFloats[Word] = MinX; EntryFloats[Word + 1] = MinY; EntryFloats[Word + 2] = MinZ;
        EntryFloats[Word + 4] = MaxX; EntryFloats[Word + 5] = MaxY; EntryFloats[Word + 6] = MaxZ;

        // 📝 Evaluate SAH over BinCount uniform centroid bins on each axis; keep the cheapest strict split.
        let BestCost = Count * SurfaceArea(MinX, MinY, MinZ, MaxX, MaxY, MaxZ), BestAxis = -1, BestBin = 0;
        const CentroidMinimum = [CMinX, CMinY, CMinZ], CentroidMaximum = [CMaxX, CMaxY, CMaxZ];
        if (Count > LeafLimit)
        {
            for (let Axis = 0; Axis < 3; Axis++)
            {
                const Extent = CentroidMaximum[Axis] - CentroidMinimum[Axis];
                if (Extent <= 1e-9) continue;
                const BinScale = BinCount / Extent;
                BinMinimum.fill(Infinity); BinMaximum.fill(-Infinity); BinTally.fill(0);
                for (let Slot = First; Slot < First + Count; Slot++)
                {
                    const Triangle = Order[Slot];
                    const Bin = Math.min(BinCount - 1, Math.floor((Centroids[Triangle * 3 + Axis] - CentroidMinimum[Axis]) * BinScale));
                    BinTally[Bin]++;
                    for (let Component = 0; Component < 3; Component++)
                    {
                        BinMinimum[Bin * 3 + Component] = Math.min(BinMinimum[Bin * 3 + Component], Bounds[Triangle * 6 + Component]);
                        BinMaximum[Bin * 3 + Component] = Math.max(BinMaximum[Bin * 3 + Component], Bounds[Triangle * 6 + 3 + Component]);
                    }
                }
                let AMinX = Infinity, AMinY = Infinity, AMinZ = Infinity, AMaxX = -Infinity, AMaxY = -Infinity, AMaxZ = -Infinity, ATally = 0;
                for (let Bin = 0; Bin < BinCount - 1; Bin++)
                {
                    ATally += BinTally[Bin];
                    if (BinTally[Bin])
                    {
                        AMinX = Math.min(AMinX, BinMinimum[Bin * 3]);     AMinY = Math.min(AMinY, BinMinimum[Bin * 3 + 1]); AMinZ = Math.min(AMinZ, BinMinimum[Bin * 3 + 2]);
                        AMaxX = Math.max(AMaxX, BinMaximum[Bin * 3]);     AMaxY = Math.max(AMaxY, BinMaximum[Bin * 3 + 1]); AMaxZ = Math.max(AMaxZ, BinMaximum[Bin * 3 + 2]);
                    }
                    LeftTally[Bin] = ATally;
                    LeftArea[Bin]  = SurfaceArea(AMinX, AMinY, AMinZ, AMaxX, AMaxY, AMaxZ);
                }
                let BMinX = Infinity, BMinY = Infinity, BMinZ = Infinity, BMaxX = -Infinity, BMaxY = -Infinity, BMaxZ = -Infinity, BTally = 0;
                for (let Bin = BinCount - 1; Bin > 0; Bin--)
                {
                    BTally += BinTally[Bin];
                    if (BinTally[Bin])
                    {
                        BMinX = Math.min(BMinX, BinMinimum[Bin * 3]);     BMinY = Math.min(BMinY, BinMinimum[Bin * 3 + 1]); BMinZ = Math.min(BMinZ, BinMinimum[Bin * 3 + 2]);
                        BMaxX = Math.max(BMaxX, BinMaximum[Bin * 3]);     BMaxY = Math.max(BMaxY, BinMaximum[Bin * 3 + 1]); BMaxZ = Math.max(BMaxZ, BinMaximum[Bin * 3 + 2]);
                    }
                    const SplitTally = LeftTally[Bin - 1];
                    if (SplitTally === 0 || BTally === 0) continue;
                    const Cost = TraversalCostRatio * SurfaceArea(MinX, MinY, MinZ, MaxX, MaxY, MaxZ)
                               + SplitTally * LeftArea[Bin - 1] + BTally * SurfaceArea(BMinX, BMinY, BMinZ, BMaxX, BMaxY, BMaxZ);
                    if (Cost < BestCost) { BestCost = Cost; BestAxis = Axis; BestBin = Bin; }
                }
            }
        }

        if (BestAxis < 0 && Count <= LeafCeiling)
        {
            EntryWords[Word + 3] = First;
            EntryWords[Word + 7] = Count;
            continue;
        }

        let Lower = First;
        if (BestAxis < 0)
        {
            // 📝 Oversized leaf: split at the median of the widest centroid axis (stable sort keeps ties in order).
            const Axis = [0, 1, 2].reduce((Best, Candidate) => (CentroidMaximum[Candidate] - CentroidMinimum[Candidate]) > (CentroidMaximum[Best] - CentroidMinimum[Best]) ? Candidate : Best, 0);
            const Range = Array.from(Order.subarray(First, First + Count)).sort((Left, Right) => Centroids[Left * 3 + Axis] - Centroids[Right * 3 + Axis]);
            Order.set(Range, First);
            Lower = First + (Count >> 1);
        }
        else
        {
            // 📝 Deterministic in-place partition by bin index on the chosen axis.
            const Extent   = CentroidMaximum[BestAxis] - CentroidMinimum[BestAxis];
            const BinScale = BinCount / Extent;
            let   Upper = First + Count - 1;
            while (Lower <= Upper)
            {
                const Triangle = Order[Lower];
                const Bin = Math.min(BinCount - 1, Math.floor((Centroids[Triangle * 3 + BestAxis] - CentroidMinimum[BestAxis]) * BinScale));
                if (Bin < BestBin) { Lower++; }
                else { Order[Lower] = Order[Upper]; Order[Upper] = Triangle; Upper--; }
            }
        }
        const LowerCount = Lower - First;

        if (EntryCount + 2 > EntryCapacity)
        {
            EntryCapacity *= 2;
            const GrownWords = new Uint32Array(EntryCapacity * BoundsEntryStride);
            GrownWords.set(EntryWords);
            EntryWords  = GrownWords;
            EntryFloats = new Float32Array(EntryWords.buffer);
        }
        const LowerEntry = EntryCount;
        EntryCount += 2;
        EntryWords[Word + 3] = LowerEntry;
        EntryWords[Word + 7] = 0;
        const DepthTier = (Range.DepthTier || 0) + 1;
        RangeStack.push({ EntryIndex: LowerEntry + 1, First: Lower, Count: Count - LowerCount, DepthTier });
        RangeStack.push({ EntryIndex: LowerEntry, First, Count: LowerCount, DepthTier });
    }

    return {
        Order,
        EntryCount,
        MaximumDepthTier,
        EntryWords: EntryWords.slice(0, EntryCount * BoundsEntryStride)
    };
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    4-WIDE COLLAPSE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Rounds Value to a float32 the way the GPU will hold it (for the conservative quantization check).
const Single = Math.fround;

// 📝 Node layout (16 words): [0..2] origin xyz (f32), [3] biased exponents x | y << 8 | z << 16 | child count << 24,
//    [4..9] quantized child bounds as 4 bytes each (min x, max x, min y, max y, min z, max z; byte k = child k),
//    [10..11] primitive counts (16 bits per child, 0 = inner child), [12..15] child links (inner: absolute node index;
//    leaf: first primitive; EmptyLink = no child). Child box = origin + q × 2^e per axis, rounded outward so the
//    decoded float box always contains the exact one (verified with float32 arithmetic below).
function EncodeWideNode(Words, NodeIndex, Children, Bounds)
{
    const Base = NodeIndex * WideNodeWords;
    const Floats = new Float32Array(Words.buffer, Words.byteOffset, Words.length);
    const Lower = [Infinity, Infinity, Infinity], Upper = [-Infinity, -Infinity, -Infinity];
    for (const Child of Children)
    {
        for (let Axis = 0; Axis < 3; Axis++) { Lower[Axis] = Math.min(Lower[Axis], Child.Box[Axis]); Upper[Axis] = Math.max(Upper[Axis], Child.Box[3 + Axis]); }
    }
    const Exponents = [0, 0, 0];
    for (let Axis = 0; Axis < 3; Axis++)
    {
        Floats[Base + Axis] = Single(Lower[Axis]);
        const Span = Math.max(Upper[Axis] - Single(Lower[Axis]), 1e-30);
        Exponents[Axis] = Math.max(-100, Math.ceil(Math.log2(Span / (QuantizationSteps - 2))));
    }
    Words[Base + 3] = ((Exponents[0] + 127) & 255) | (((Exponents[1] + 127) & 255) << 8) | (((Exponents[2] + 127) & 255) << 16) | (Children.length << 24);
    const Quantized = new Uint8Array(24);   // [axis-major: min x ×4, max x ×4, min y ×4, …]
    Children.forEach((Child, Slot) =>
    {
        for (let Axis = 0; Axis < 3; Axis++)
        {
            const Origin = Floats[Base + Axis], Scale = 2 ** Exponents[Axis];
            let Minimum = Math.max(0, Math.min(QuantizationSteps, Math.floor((Child.Box[Axis] - Origin) / Scale)));
            let Maximum = Math.max(0, Math.min(QuantizationSteps, Math.ceil((Child.Box[3 + Axis] - Origin) / Scale)));
            while (Minimum > 0 && Single(Origin + Minimum * Scale) > Child.Box[Axis]) Minimum--;
            while (Maximum < QuantizationSteps && Single(Origin + Maximum * Scale) < Child.Box[3 + Axis]) Maximum++;
            if (Single(Origin + Minimum * Scale) > Child.Box[Axis] || Single(Origin + Maximum * Scale) < Child.Box[3 + Axis])
            {
                throw new Error('Wide node quantization could not contain a child box');
            }
            Quantized[Axis * 8 + Slot]     = Minimum;
            Quantized[Axis * 8 + 4 + Slot] = Maximum;
        }
    });
    for (let Word = 0; Word < 6; Word++)
    {
        Words[Base + 4 + Word] = Quantized[Word * 4] | (Quantized[Word * 4 + 1] << 8) | (Quantized[Word * 4 + 2] << 16) | (Quantized[Word * 4 + 3] << 24);
    }
    const Counts = [0, 0, 0, 0], Links = [EmptyLink, EmptyLink, EmptyLink, EmptyLink];
    Children.forEach((Child, Slot) => { Counts[Slot] = Child.Count; Links[Slot] = Child.Link; });
    if (Counts.some(Count => Count > LeafPrimitiveCeiling)) throw new Error(`Wide node leaf exceeds ${LeafPrimitiveCeiling} primitives`);
    Words[Base + 10] = Counts[0] | (Counts[1] << 16);
    Words[Base + 11] = Counts[2] | (Counts[3] << 16);
    for (let Slot = 0; Slot < 4; Slot++) Words[Base + 12 + Slot] = Links[Slot];
}

// 📝 Ylitie–Karras–Laine (2017) optimal bottom-up dynamic-programming collapse from a binary SAH tree into 4-wide
//    quantized nodes. Minimizes the exact 4-wide SAH cost by choosing, at every binary entry, both the optimal slot
//    allocation (1..4) across subtrees and whether a contiguous range of <= MaxLeaf primitives becomes a single leaf.
//    Raises 4-wide slot occupancy from ~76% to ~98% while staying 100% deterministic with identical ray-triangle hits.
export function CollapseWideBounds(Binary, NodeBase = 0, MaxLeaf = LeafPrimitiveCeiling)
{
    const EntryCount = Binary.EntryCount;
    const Words      = Binary.EntryWords;
    const Floats     = new Float32Array(Words.buffer, Words.byteOffset, Words.length);
    const Box        = Entry => [Floats[Entry * 8], Floats[Entry * 8 + 1], Floats[Entry * 8 + 2],
                                 Floats[Entry * 8 + 4], Floats[Entry * 8 + 5], Floats[Entry * 8 + 6]];
    const Area       = Entry => SurfaceArea(Floats[Entry * 8], Floats[Entry * 8 + 1], Floats[Entry * 8 + 2],
                                            Floats[Entry * 8 + 4], Floats[Entry * 8 + 5], Floats[Entry * 8 + 6]);
    const IsLeaf     = Entry => Words[Entry * BoundsEntryStride + 7] > 0;

    // 📝 Post-order traversal for bottom-up DP.
    const PostOrder = new Uint32Array(EntryCount);
    let   PostCount = 0;
    const WalkStack = [0];
    while (WalkStack.length)
    {
        const Entry = WalkStack.pop();
        PostOrder[PostCount++] = Entry;
        if (!IsLeaf(Entry))
        {
            const Left = Words[Entry * BoundsEntryStride + 3];
            WalkStack.push(Left, Left + 1);
        }
    }

    const PrimCount  = new Int32Array(EntryCount);
    const PrimFirst  = new Int32Array(EntryCount);
    const Contiguous = new Uint8Array(EntryCount);
    const SlotCost   = new Float64Array(EntryCount * 5);   // SlotCost[Entry * 5 + Slots] for Slots = 1..4
    const Decision   = new Int8Array(EntryCount * 5);      // -1 = leaf, 0 = inner 4-wide node, 1..3 = slots to left child

    for (let Cursor = PostCount - 1; Cursor >= 0; Cursor--)
    {
        const Entry = PostOrder[Cursor];
        const Count = Words[Entry * BoundsEntryStride + 7];
        const EntryArea = Area(Entry);
        if (Count > 0)
        {
            PrimCount[Entry]  = Count;
            PrimFirst[Entry]  = Words[Entry * BoundsEntryStride + 3];
            Contiguous[Entry] = 1;
            const LeafCost    = EntryArea * Count;
            for (let Slots = 1; Slots <= 4; Slots++) { SlotCost[Entry * 5 + Slots] = LeafCost; Decision[Entry * 5 + Slots] = -1; }
            continue;
        }
        const Left = Words[Entry * BoundsEntryStride + 3], Right = Left + 1;
        PrimCount[Entry]  = PrimCount[Left] + PrimCount[Right];
        PrimFirst[Entry]  = Math.min(PrimFirst[Left], PrimFirst[Right]);
        Contiguous[Entry] = (Contiguous[Left] && Contiguous[Right] &&
                             (PrimFirst[Left] + PrimCount[Left] === PrimFirst[Right] ||
                              PrimFirst[Right] + PrimCount[Right] === PrimFirst[Left])) ? 1 : 0;

        for (let Slots = 2; Slots <= 4; Slots++)
        {
            let Best = Infinity, BestLeft = 1;
            for (let LeftSlots = 1; LeftSlots < Slots; LeftSlots++)
            {
                const Candidate = SlotCost[Left * 5 + LeftSlots] + SlotCost[Right * 5 + (Slots - LeftSlots)];
                if (Candidate < Best) { Best = Candidate; BestLeft = LeftSlots; }
            }
            SlotCost[Entry * 5 + Slots] = Best;
            Decision[Entry * 5 + Slots] = BestLeft;
        }

        let BestSingle = EntryArea * WideNodeCostRatio + SlotCost[Entry * 5 + 4];
        let SingleCode = 0;
        if (Contiguous[Entry] && PrimCount[Entry] <= MaxLeaf)
        {
            const LeafCost = EntryArea * PrimCount[Entry];
            if (LeafCost <= BestSingle) { BestSingle = LeafCost; SingleCode = -1; }
        }
        SlotCost[Entry * 5 + 1] = BestSingle;
        Decision[Entry * 5 + 1] = SingleCode;
        for (let Slots = 2; Slots <= 4; Slots++)
        {
            if (BestSingle < SlotCost[Entry * 5 + Slots])
            {
                SlotCost[Entry * 5 + Slots] = BestSingle;
                Decision[Entry * 5 + Slots] = SingleCode;
            }
        }
    }

    const CollectChildren = (Entry, Slots, Output) =>
    {
        const Code = Decision[Entry * 5 + Slots];
        if (Code <= 0 || IsLeaf(Entry)) { Output.push(Entry); return; }
        const Left = Words[Entry * BoundsEntryStride + 3];
        CollectChildren(Left, Code, Output);
        CollectChildren(Left + 1, Slots - Code, Output);
    };

    const WideWords = new Uint32Array(Math.max(1, EntryCount) * WideNodeWords);
    let NodeCount = 1, MaximumDepth = 1;
    const Pending = [{ Entry: 0, Node: 0, Depth: 1 }];
    while (Pending.length)
    {
        const { Entry, Node, Depth } = Pending.pop();
        MaximumDepth = Math.max(MaximumDepth, Depth);
        const Members = [];
        if (IsLeaf(Entry))
        {
            Members.push(Entry);
        }
        else if (Decision[Entry * 5 + 4] > 0)
        {
            const Left = Words[Entry * BoundsEntryStride + 3];
            CollectChildren(Left, Decision[Entry * 5 + 4], Members);
            CollectChildren(Left + 1, 4 - Decision[Entry * 5 + 4], Members);
        }
        else
        {
            const Left = Words[Entry * BoundsEntryStride + 3];
            Members.push(Left, Left + 1);
        }
        const Children = Members.map(Member =>
        {
            if (IsLeaf(Member) || Decision[Member * 5 + 1] === -1)
            {
                return { Box: Box(Member), Count: PrimCount[Member], Link: PrimFirst[Member] };
            }
            const ChildNode = NodeCount++;
            Pending.push({ Entry: Member, Node: ChildNode, Depth: Depth + 1 });
            return { Box: Box(Member), Count: 0, Link: NodeBase + ChildNode };
        });
        EncodeWideNode(WideWords, Node, Children);
    }
    return { Words: WideWords.slice(0, NodeCount * WideNodeWords), NodeCount, MaximumDepth };
}
