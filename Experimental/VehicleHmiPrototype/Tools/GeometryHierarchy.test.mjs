//============================================================================================================================================
//                                                          GEOMETRYHIERARCHY.TEST.MJS
//============================================================================================================================================
// 🧩 Hierarchy gates: exact leaf coverage, caps, monotonic error, crack-free shared borders, class separation,
//    determinism, UV seams, marked sharp edges, and group-coherent parent error/sphere for the runtime cut.

import { test } from "node:test";
import assert from "node:assert/strict";
import { BuildGeometryHierarchy, ValidateGeometryHierarchy, CollectHierarchyLevel, WeldFacetSoup } from "../Source/GeometryHierarchy.js";
import { ConstructSphereFacets, ConstructHeightfieldFacets, ConstructTetrahedronFacets, ConstructUvSeamCreasedMeshFacets } from "./GeometryHierarchyCorpus.mjs";

test("welding removes soup duplication without touching triangle coverage", () =>
{
    const Set = ConstructSphereFacets(2);
    const Welded = WeldFacetSoup(Set);
    assert.equal(Welded.Triangles.length / 3, Set.Facets.length);
    assert.ok(Welded.Positions.length / 3 < Set.Facets.length, "closed sphere must weld below one vertex per facet");
});

test("sphere builds a multi-level crack-free DAG with monotonic error", () =>
{
    const Set = ConstructSphereFacets(4);   // 5120 facets
    const Hierarchy = BuildGeometryHierarchy(Set);
    const Checks = ValidateGeometryHierarchy(Set, Hierarchy);
    assert.deepEqual(Checks.Failures, []);
    assert.ok(Checks.Passed);
    assert.ok(Hierarchy.Levels.length >= 3, "expected at least three DAG levels, got " + Hierarchy.Levels.length);
    const Leaf = Hierarchy.Levels[0], Root = Hierarchy.Levels.at(-1);
    assert.equal(Root.Clusters.length, 1, "closed manifold sphere should simplify to a single root cluster");
    assert.ok(Root.TriangleCount <= Leaf.TriangleCount / 4,
        `root kept ${Root.TriangleCount} of ${Leaf.TriangleCount} triangles`);
    for (let Level = 1; Level < Checks.Levels.length; ++Level)
        assert.ok(Checks.Levels[Level].Triangles < Checks.Levels[Level - 1].Triangles, "levels must shrink toward the root");
    for (const Node of Hierarchy.Clusters)
        assert.ok(Node.Sphere.Radius >= 0 && Node.Cone.Cutoff <= 1 && Node.Cone.Cutoff >= -1);
});

test("heightmap cliff mesh (open border, hard crease) hierarchies without cracks", () =>
{
    const Set = ConstructHeightfieldFacets(64);
    const Hierarchy = BuildGeometryHierarchy(Set);
    const Checks = ValidateGeometryHierarchy(Set, Hierarchy);
    assert.deepEqual(Checks.Failures, []);
    assert.ok(Hierarchy.Levels.length >= 3);
});

test("opaque and transmissive families never share a cluster", () =>
{
    const Glass = 9;
    const Set = ConstructSphereFacets(3, Glass);
    assert.ok(Set.Facets.some((Facet) => Facet.Family === Glass));
    const Hierarchy = BuildGeometryHierarchy(Set, { TransmissiveFamilies: [Glass] });
    assert.deepEqual(ValidateGeometryHierarchy(Set, Hierarchy).Failures, []);
    for (const Node of Hierarchy.Clusters)
        assert.notEqual(Node.ClassMask, 3, "cluster " + Node.Identity + " mixes opaque and transmissive triangles");
    assert.ok(Hierarchy.Clusters.some((Node) => Node.ClassMask === 2), "glass band must produce transmissive clusters");
});

test("builds are deterministic", () =>
{
    const Set = ConstructSphereFacets(3);
    const First = BuildGeometryHierarchy(Set), Second = BuildGeometryHierarchy(Set);
    const Summarize = (Hierarchy) => Hierarchy.Levels.map((Level) => [Level.Clusters.length, Level.TriangleCount, Level.VertexCount]);
    assert.deepEqual(Summarize(First), Summarize(Second));
    for (let Index = 0; Index < First.Clusters.length; ++Index)
    {
        assert.equal(First.Clusters[Index].TriangleCount, Second.Clusters[Index].TriangleCount);
        assert.equal(First.Clusters[Index].Error, Second.Clusters[Index].Error);
        assert.equal(First.Clusters[Index].ParentError, Second.Clusters[Index].ParentError);
        assert.deepEqual([...First.Levels[0].Triangles], [...Second.Levels[0].Triangles]);
        assert.deepEqual([...First.Levels[0].CornerUVs], [...Second.Levels[0].CornerUVs]);
        assert.deepEqual([...First.Levels.at(-1).CornerUVs], [...Second.Levels.at(-1).CornerUVs]);
    }
});

test("tiny meshes stop at one honest leaf level", () =>
{
    const Set = ConstructTetrahedronFacets();
    const Hierarchy = BuildGeometryHierarchy(Set);
    assert.equal(Hierarchy.Levels.length, 1);
    assert.equal(Hierarchy.Clusters.length, 1);
    assert.deepEqual(ValidateGeometryHierarchy(Set, Hierarchy).Failures, []);
});

test("level collection returns a renderable soup owned by clusters", () =>
{
    const Set = ConstructSphereFacets(3);
    const Hierarchy = BuildGeometryHierarchy(Set);
    const Soup = CollectHierarchyLevel(Hierarchy, 1);
    assert.equal(Soup.TriangleCount, Hierarchy.Levels[1].TriangleCount);
    assert.equal(Soup.Positions.length, Soup.TriangleCount * 9);
    assert.equal(Soup.CornerUVs.length, Soup.TriangleCount * 6);
    assert.equal(Soup.SharpMask.length, Soup.TriangleCount);
    assert.equal(Soup.UvSeamMask.length, Soup.TriangleCount);
    assert.ok([...Soup.Errors].every((Error) => Error >= 0));
});

test("UV seams and marked sharp edges survive every hierarchy level", () =>
{
    const Mesh = ConstructUvSeamCreasedMeshFacets(48, 20, 6);
    const Hierarchy = BuildGeometryHierarchy(Mesh);
    const Checks = ValidateGeometryHierarchy(Mesh, Hierarchy);
    assert.deepEqual(Checks.Failures, []);
    assert.ok(Hierarchy.Levels.length >= 4, "vessel should build a deep DAG, got " + Hierarchy.Levels.length);
    assert.ok(Hierarchy.HasUVs, "vessel UVs must be recognised");

    // Level 0 weld saw the authored features: 3 sharp rings (top rim, bottom rim, waist) × 48 segments,
    // and UV seams along the meridian plus both cap rims.
    assert.equal(Hierarchy.Levels[0].SharpEdgeCount, 144, "three 48-segment marked sharp rings");
    assert.ok(Hierarchy.Levels[0].UvSeamEdgeCount >= 100, "meridian + cap-rim UV seams, got " + Hierarchy.Levels[0].UvSeamEdgeCount);

    const SharpZ = (Level) =>
    {
        const Zs = new Set();
        for (let T = 0; T < Level.TriangleCount; ++T)
            for (let Corner = 0; Corner < 3; ++Corner)
                if (Level.SharpMask[T] & (1 << Corner))
                {
                    const A = Level.Triangles[T * 3 + Corner], B = Level.Triangles[T * 3 + (Corner + 1) % 3];
                    Zs.add(Math.round(Level.Positions[A * 3 + 2] * 1000) / 1000);
                    Zs.add(Math.round(Level.Positions[B * 3 + 2] * 1000) / 1000);
                }
        return Zs;
    };

    for (const Level of Hierarchy.Levels)
    {
        assert.ok(Level.SharpEdgeCount > 0, `level ${Level.Level} lost every marked sharp edge`);
        assert.ok(Level.UvSeamEdgeCount > 0, `level ${Level.Level} lost every UV seam`);
        // Every surviving marked sharp vertex still lies exactly on one of the authored crease planes
        // (z = -1 bottom rim, z = 0 waist ridge, z = +1 top rim): creases were never smoothed away.
        for (const Z of SharpZ(Level))
            assert.ok(Math.abs(Z) < 1e-9 || Math.abs(Math.abs(Z) - 1) < 1e-9,
                `level ${Level.Level} sharp vertex drifted off its crease plane to z=${Z}`);
        // No triangle ever stretches across the barrel meridian seam or between separate UV islands:
        // chart-coherent collapses keep every triangle inside one chart region at every level.
        for (let T = 0; T < Level.TriangleCount; ++T)
        {
            const Us = [0, 1, 2].map((C) => Level.CornerUVs[T * 6 + C * 2]);
            const Vs = [0, 1, 2].map((C) => Level.CornerUVs[T * 6 + C * 2 + 1]);
            assert.ok(Math.max(...Us) - Math.min(...Us) <= 0.6,
                `level ${Level.Level} triangle ${T} crosses the meridian UV seam`);
            assert.ok(!(Vs.some((V) => V <= 0.755) && Vs.some((V) => V >= 0.775)),
                `level ${Level.Level} triangle ${T} crosses between UV islands`);
            for (let I = 0; I < 6; ++I)
                assert.ok(Number.isFinite(Level.CornerUVs[T * 6 + I]), "non-finite UV in level " + Level.Level);
        }
    }
});

test("every simplification group shares one ParentError and ParentSphere (runtime cut contract)", () =>
{
    const Mesh = ConstructSphereFacets(4);
    const Hierarchy = BuildGeometryHierarchy(Mesh);
    const Groups = new Map();
    for (const Node of Hierarchy.Clusters)
    {
        if (!Node.Parents.length) continue;
        assert.ok(Node.ParentError >= Node.Error, "ParentError must dominate the cluster's own error");
        assert.ok(Number.isFinite(Node.ParentError), "non-root clusters need a finite ParentError");
        const Key = Node.Level + ":" + Node.GroupId;
        if (!Groups.has(Key)) Groups.set(Key, Node);
        else
        {
            const First = Groups.get(Key);
            assert.equal(Node.ParentError, First.ParentError, "siblings of a group must share ParentError exactly");
            assert.deepEqual(Node.ParentSphere, First.ParentSphere, "siblings of a group must share ParentSphere exactly");
            assert.deepEqual([...Node.Parents].sort((A, B) => A - B), [...First.Parents].sort((A, B) => A - B),
                "siblings of a group must share the same parent clusters");
        }
        // ParentSphere must enclose this cluster's own sphere.
        const C = Node.ParentSphere.Center, S = Node.Sphere.Center;
        const D = Math.hypot(C[0] - S[0], C[1] - S[1], C[2] - S[2]);
        assert.ok(D + Node.Sphere.Radius <= Node.ParentSphere.Radius + 1e-9, "ParentSphere does not enclose cluster sphere");
    }
    assert.ok(Groups.size > 10, "expected many simplification groups, got " + Groups.size);
    for (const Node of Hierarchy.Clusters)
        if (Node.Level === Hierarchy.Levels.length - 1)
            assert.equal(Node.ParentError, Infinity, "root clusters keep an infinite ParentError");
});
