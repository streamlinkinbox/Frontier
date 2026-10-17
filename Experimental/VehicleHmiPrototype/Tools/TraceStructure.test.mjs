//============================================================================================================================================
//                                                          TRACESTRUCTURE.TEST.MJS
//============================================================================================================================================
// 🧩 The BVH builder against brute force: every ray's closest hit through the hierarchy must match the exhaustive one.

import test from "node:test";
import assert from "node:assert/strict";
import { BuildTriangleBlas, BuildHierarchy, CastBlas, NodeStride, TriangleStride } from "../Source/TraceStructure.js";

function ConstructStream(Seed)
{
    let State = Seed >>> 0;
    return () => { State = (State * 1664525 + 1013904223) >>> 0; return State / 4294967296; };
}

function BruteForce(Corners, Count, Origin, Direction)
{
    let Best = Infinity, Hit = -1;
    for (let Index = 0; Index < Count; ++Index)
    {
        const B = Index * 9;
        const V0 = [Corners[B], Corners[B + 1], Corners[B + 2]];
        const E1 = [Corners[B + 3] - V0[0], Corners[B + 4] - V0[1], Corners[B + 5] - V0[2]];
        const E2 = [Corners[B + 6] - V0[0], Corners[B + 7] - V0[1], Corners[B + 8] - V0[2]];
        const P = [Direction[1] * E2[2] - Direction[2] * E2[1], Direction[2] * E2[0] - Direction[0] * E2[2], Direction[0] * E2[1] - Direction[1] * E2[0]];
        const Det = E1[0] * P[0] + E1[1] * P[1] + E1[2] * P[2];
        if (Math.abs(Det) < 1.0e-12) continue;
        const Inv = 1.0 / Det;
        const T = [Origin[0] - V0[0], Origin[1] - V0[1], Origin[2] - V0[2]];
        const U = (T[0] * P[0] + T[1] * P[1] + T[2] * P[2]) * Inv;
        if (U < 0.0 || U > 1.0) continue;
        const Q = [T[1] * E1[2] - T[2] * E1[1], T[2] * E1[0] - T[0] * E1[2], T[0] * E1[1] - T[1] * E1[0]];
        const V = (Direction[0] * Q[0] + Direction[1] * Q[1] + Direction[2] * Q[2]) * Inv;
        if (V < 0.0 || U + V > 1.0) continue;
        const Travel = (E2[0] * Q[0] + E2[1] * Q[1] + E2[2] * Q[2]) * Inv;
        if (Travel > 1.0e-6 && Travel < Best) { Best = Travel; Hit = Index; }
    }
    return { Travel: Best, Triangle: Hit };
}

function ConstructSoup(Count, Random, Spread = 10.0, Size = 0.8)
{
    const Corners = new Float32Array(Count * 9);
    for (let Index = 0; Index < Count; ++Index)
    {
        const Cx = (Random() - 0.5) * Spread, Cy = (Random() - 0.5) * Spread, Cz = (Random() - 0.5) * Spread;
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            Corners[Index * 9 + Corner * 3] = Cx + (Random() - 0.5) * Size;
            Corners[Index * 9 + Corner * 3 + 1] = Cy + (Random() - 0.5) * Size;
            Corners[Index * 9 + Corner * 3 + 2] = Cz + (Random() - 0.5) * Size;
        }
    }
    return Corners;
}

test("BLAS closest hits agree with brute force on a random soup", () =>
{
    const Random = ConstructStream(12345);
    const Count = 1500;
    const Corners = ConstructSoup(Count, Random, 10.0, 2.5);
    const Blas = BuildTriangleBlas(Corners, Count, (Index) => ({ Family: Index % 7, OccupantOffset: 0 }), 100, 50);
    assert.equal(Blas.TriangleCount, Count);
    assert.ok(Blas.NodeCount <= 2 * Count - 1);
    assert.equal(Blas.Nodes.byteLength, Blas.NodeCount * NodeStride);
    assert.equal(Blas.Triangles.byteLength, Count * TriangleStride);
    // 📝 Rebase to zero for the CPU cast (the builder offset leaves by TriangleBase 100 and NodeBase 50).
    const Words = new Uint32Array(Blas.Nodes);
    for (let Node = 0; Node < Blas.NodeCount; ++Node)
        Words[Node * 8 + 3] -= Words[Node * 8 + 7] === 0 ? 50 : 100;
    let Hits = 0;
    for (let Ray = 0; Ray < 400; ++Ray)
    {
        const Origin = [(Random() - 0.5) * 14.0, (Random() - 0.5) * 14.0, (Random() - 0.5) * 14.0];
        let Direction = [Random() - 0.5, Random() - 0.5, Random() - 0.5];
        const Length = Math.hypot(...Direction) || 1.0;
        Direction = Direction.map((C) => C / Length);
        const Expected = BruteForce(Corners, Count, Origin, Direction);
        const Actual = CastBlas(Blas.Nodes, Blas.Triangles, 0, Origin, Direction);
        if (Expected.Triangle >= 0) Hits++;
        assert.equal(Actual.Triangle, Expected.Triangle, `ray ${Ray}`);
        if (Expected.Triangle >= 0)
            assert.ok(Math.abs(Actual.Travel - Expected.Travel) < 1.0e-4);
    }
    assert.ok(Hits > 40, `the soup should be hit often (${Hits})`);
});

test("every triangle appears exactly once in the leaves", () =>
{
    const Random = ConstructStream(777);
    const Count = 333;
    const Corners = ConstructSoup(Count, Random, 4.0, 1.5);
    const Blas = BuildTriangleBlas(Corners, Count, () => ({ Family: 1 }), 0, 0);
    const Words = new Uint32Array(Blas.Nodes);
    const Seen = new Uint8Array(Count);
    let Leaves = 0;
    for (let Node = 0; Node < Blas.NodeCount; ++Node)
    {
        const Leaf = Words[Node * 8 + 7];
        if (Leaf === 0) continue;
        Leaves++;
        for (let Slot = Words[Node * 8 + 3]; Slot < Words[Node * 8 + 3] + Leaf; ++Slot)
            Seen[Slot]++;
    }
    assert.ok(Seen.every((Times) => Times === 1));
    assert.ok(Leaves >= Count / 4);
    // leaf triangles reference their source
    const Tri = new Uint32Array(Blas.Triangles);
    const Sources = new Set();
    for (let Slot = 0; Slot < Count; ++Slot) Sources.add(Tri[Slot * 16 + 13]);
    assert.equal(Sources.size, Count);
});

test("degenerate centroids (coincident primitives) still produce a bounded tree", () =>
{
    const Count = 64;
    const Minimum = new Float32Array(Count * 3).fill(0.0), Maximum = new Float32Array(Count * 3).fill(1.0), Centroid = new Float32Array(Count * 3).fill(0.5);
    const Hierarchy = BuildHierarchy(Count, Minimum, Maximum, Centroid, 2);
    assert.ok(Hierarchy.NodeCount <= 2 * Count - 1);
    const Words = new Uint32Array(Hierarchy.Nodes);
    let Largest = 0;
    for (let Node = 0; Node < Hierarchy.NodeCount; ++Node) Largest = Math.max(Largest, Words[Node * 8 + 7]);
    assert.ok(Largest <= 4, `largest leaf ${Largest}`);
});

test("instance hierarchy (TLAS) over boxes reaches every instance", () =>
{
    const Random = ConstructStream(99);
    const Count = 500;
    const Minimum = new Float32Array(Count * 3), Maximum = new Float32Array(Count * 3), Centroid = new Float32Array(Count * 3);
    for (let Index = 0; Index < Count; ++Index)
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            const Low = (Random() - 0.5) * 200.0, Size = 0.5 + Random() * 8.0;
            Minimum[Index * 3 + Axis] = Low; Maximum[Index * 3 + Axis] = Low + Size; Centroid[Index * 3 + Axis] = Low + 0.5 * Size;
        }
    const Hierarchy = BuildHierarchy(Count, Minimum, Maximum, Centroid, 2);
    const Words = new Uint32Array(Hierarchy.Nodes), Scalars = new Float32Array(Hierarchy.Nodes);
    // every node's bounds contain its children's
    for (let Node = 0; Node < Hierarchy.NodeCount; ++Node)
    {
        if (Words[Node * 8 + 7] !== 0) continue;
        for (const Child of [Words[Node * 8 + 3], Words[Node * 8 + 3] + 1])
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                assert.ok(Scalars[Child * 8 + Axis] >= Scalars[Node * 8 + Axis] - 1.0e-5);
                assert.ok(Scalars[Child * 8 + 4 + Axis] <= Scalars[Node * 8 + 4 + Axis] + 1.0e-5);
            }
    }
    const Seen = new Uint8Array(Count);
    for (let Node = 0; Node < Hierarchy.NodeCount; ++Node)
    {
        const Leaf = Words[Node * 8 + 7];
        for (let Slot = 0; Slot < Leaf; ++Slot) Seen[Hierarchy.Order[Words[Node * 8 + 3] + Slot]]++;
    }
    assert.ok(Seen.every((Times) => Times === 1));
});
