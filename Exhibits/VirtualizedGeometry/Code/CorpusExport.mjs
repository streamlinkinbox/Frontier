//==========================================================================================================================================
//                                                             CORPUSEXPORT.MJS
//==========================================================================================================================================
// 🧩 Builds the VehicleHMI demo's four GeometryHierarchy corpus surfaces with the demo's own builder, measures every
//    DAG level, and exports the source and level facet soups for TierComparison.cpp and SurfaceDeviation.mjs.
//
// Usage: node CorpusExport.mjs <demo directory> <corpus directory>
//   <demo directory>   Experimental/VehicleHmiPrototype extracted from SultanAladin/Frontier @ 6afb6f00b5f2.
//   <corpus directory> scratch output; receives <surface>.soup, demo/demo-<surface>-level<k>.soup and the report JSON.

import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { performance } from "node:perf_hooks";
import { pathToFileURL } from "node:url";

//------------------------------------------------------------------------------------------------------------------------
//                                                     DEMO MODULES
//------------------------------------------------------------------------------------------------------------------------

const [DemoDirectory, CorpusDirectory] = process.argv.slice(2);
if (!DemoDirectory || !CorpusDirectory)
{
    console.error("usage: node CorpusExport.mjs <demo directory> <corpus directory>");
    process.exit(2);
}
const Builder = await import(pathToFileURL(join(DemoDirectory, "Source/GeometryHierarchy.js")).href);
const Corpus  = await import(pathToFileURL(join(DemoDirectory, "Tools/GeometryHierarchyCorpus.mjs")).href);
mkdirSync(join(CorpusDirectory, "demo"), { recursive: true });

// 📝 Exactly the surfaces and options of the demo's Tools/GeometryHierarchyProof.mjs.
const Surfaces = [
    ["sphere", Corpus.ConstructSphereFacets(4, -1), {}],
    ["glass-sphere", Corpus.ConstructSphereFacets(4, 9), { TransmissiveFamilies: [9] }],
    ["heightfield-cliff", Corpus.ConstructHeightfieldFacets(64, 40), {}],
    ["uv-creased-vessel", Corpus.ConstructUvSeamCreasedMeshFacets(48, 20, 6), {}],
];

//------------------------------------------------------------------------------------------------------------------------
//                                                    FACET SOUP FILES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Layout matches TierComparison.cpp: u32 facet count, u32 carries-UV; per facet 9 f64 corners, 6 f64 UVs when
//    carried, u32 classification, u32 sharp mask.
function EncodeSoup(Path, Corners, CornerUVs, Classification, SharpMask)
{
    const Count = Corners.length / 9;
    const Bytes = Buffer.alloc(8 + Count * (72 + (CornerUVs ? 48 : 0) + 8));
    let Offset = 0;
    Bytes.writeUInt32LE(Count, Offset); Offset += 4;
    Bytes.writeUInt32LE(CornerUVs ? 1 : 0, Offset); Offset += 4;
    for (let Facet = 0; Facet < Count; ++Facet)
    {
        for (let K = 0; K < 9; ++K) { Bytes.writeDoubleLE(Corners[Facet * 9 + K], Offset); Offset += 8; }
        if (CornerUVs)
            for (let K = 0; K < 6; ++K) { Bytes.writeDoubleLE(CornerUVs[Facet * 6 + K], Offset); Offset += 8; }
        Bytes.writeUInt32LE(Classification[Facet] ?? 0, Offset); Offset += 4;
        Bytes.writeUInt32LE(SharpMask ? SharpMask[Facet] : 0, Offset); Offset += 4;
    }
    writeFileSync(Path, Bytes);
}

function EncodeSourceSoup(Path, Source)
{
    const Corners = [], CornerUVs = [], Classification = [], SharpMask = [];
    const CarriesUv = Source.Facets.some((Facet) => Facet.UVs);
    for (const Facet of Source.Facets)
    {
        for (const Point of Facet.Corners) Corners.push(...Point);
        if (CarriesUv)
            for (let Corner = 0; Corner < 3; ++Corner) CornerUVs.push(...(Facet.UVs ? Facet.UVs[Corner] : [0, 0]));
        Classification.push(Facet.Family ?? 0);
        let Mask = 0;
        if (typeof Facet.SharpEdges === "number") Mask = Facet.SharpEdges & 7;
        else if (Array.isArray(Facet.SharpEdges))
            for (let Edge = 0; Edge < 3; ++Edge) if (Facet.SharpEdges[Edge]) Mask |= 1 << Edge;
        SharpMask.push(Mask);
    }
    EncodeSoup(Path, Corners, CarriesUv ? CornerUVs : null, Classification, SharpMask);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LEVEL METRICS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Same definitions as TierComparison.cpp: sharp-edge endpoints off every source crease plane, triangles whose U
//    span exceeds 0.5 (smeared across the meridian seam), and needles (longest² / (2·area) > 20).
function MeasureLevel(Soup, CreasePlanes)
{
    const Points = Soup.Positions, Uv = Soup.CornerUVs, Sharp = Soup.SharpMask;
    const Count = Points.length / 9;
    let OffCrease = 0, SeamCrossing = 0, Needles = 0;
    for (let Triangle = 0; Triangle < Count; ++Triangle)
    {
        if (CreasePlanes && Sharp && Sharp[Triangle])
            for (let Edge = 0; Edge < 3; ++Edge)
                if (Sharp[Triangle] & (1 << Edge))
                    for (const Corner of [Edge, (Edge + 1) % 3])
                    {
                        const Z = Points[Triangle * 9 + Corner * 3 + 2];
                        if (!CreasePlanes.some((Plane) => Math.abs(Plane - Z) < 1e-9)) ++OffCrease;
                    }
        if (Uv)
        {
            const U = [Uv[Triangle * 6], Uv[Triangle * 6 + 2], Uv[Triangle * 6 + 4]];
            if (Math.max(...U) - Math.min(...U) > 0.5) ++SeamCrossing;
        }
        const Corner = (K) => [0, 1, 2].map((Axis) => Points[Triangle * 9 + K * 3 + Axis]);
        const [A, B, C] = [Corner(0), Corner(1), Corner(2)];
        const Span = ([P, Q]) => Math.hypot(Q[0] - P[0], Q[1] - P[1], Q[2] - P[2]);
        const Longest = Math.max(Span([A, B]), Span([B, C]), Span([C, A]));
        const AB = [0, 1, 2].map((Axis) => B[Axis] - A[Axis]), AC = [0, 1, 2].map((Axis) => C[Axis] - A[Axis]);
        const DoubleArea = Math.hypot(AB[1] * AC[2] - AB[2] * AC[1], AB[2] * AC[0] - AB[0] * AC[2],
                                      AB[0] * AC[1] - AB[1] * AC[0]);
        if (DoubleArea > 0 && Longest * Longest / DoubleArea > 20) ++Needles;
    }
    return { OffCrease, SeamCrossing, Needles };
}

// 📝 The demo's GPU cut decides each cluster alone: selected iff Error ≤ τ < ParentError (the demo's field names).
//    Gaps = leaf clusters with no selected self-or-enclosing cluster; Overlaps = selected clusters enclosing another
//    selected cluster. Both zero means the per-cluster errors alone give a crack-free cut.
function MeasurePerClusterCut(Nesting, τ)
{
    const Clusters = Nesting.Clusters;
    const Selected = Clusters.map((Cluster) => Cluster.Error <= τ && τ < Cluster.ParentError);
    let Gaps = 0, Overlaps = 0;
    for (const Leaf of Clusters)
    {
        if (Leaf.Level !== 0) continue;
        const Pending = [Leaf.Identity], Visited = new Set();
        let Covered = false;
        while (Pending.length && !Covered)
        {
            const Index = Pending.pop();
            if (Visited.has(Index)) continue;
            Visited.add(Index);
            Covered = Selected[Index];
            Pending.push(...Clusters[Index].Parents);
        }
        if (!Covered) ++Gaps;
    }
    for (const Cluster of Clusters)
    {
        if (!Selected[Cluster.Identity]) continue;
        const Pending = [...Cluster.Children], Visited = new Set();
        let Doubled = false;
        while (Pending.length && !Doubled)
        {
            const Index = Pending.pop();
            if (Visited.has(Index)) continue;
            Visited.add(Index);
            Doubled = Selected[Index];
            Pending.push(...Clusters[Index].Children);
        }
        if (Doubled) ++Overlaps;
    }
    return { Tau: τ, Gaps, Overlaps };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          MAIN
//------------------------------------------------------------------------------------------------------------------------

const Report = {};
for (const [Name, Source, Options] of Surfaces)
{
    EncodeSourceSoup(join(CorpusDirectory, Name + ".soup"), Source);
    const Durations = [];
    let Nesting = null;
    for (let Repeat = 0; Repeat < 3; ++Repeat)
    {
        const Start = performance.now();
        Nesting = Builder.BuildGeometryHierarchy(Source, Options);
        Durations.push(performance.now() - Start);
    }
    Durations.sort((A, B) => A - B);
    const Validation = Builder.ValidateGeometryHierarchy(Source, Nesting, Options);
    const Leaf = Builder.CollectHierarchyLevel(Nesting, 0);
    let CreasePlanes = null;
    if (Leaf.SharpMask && Leaf.SharpMask.some((Mask) => Mask))
    {
        const Planes = new Set();
        for (let Triangle = 0; Triangle < Leaf.SharpMask.length; ++Triangle)
            for (let Edge = 0; Edge < 3; ++Edge)
                if (Leaf.SharpMask[Triangle] & (1 << Edge))
                    for (const Corner of [Edge, (Edge + 1) % 3])
                        Planes.add(Leaf.Positions[Triangle * 9 + Corner * 3 + 2]);
        CreasePlanes = [...Planes];
    }
    const Levels = Validation.Levels.map((Level, Index) =>
    {
        const Soup = Builder.CollectHierarchyLevel(Nesting, Index);
        EncodeSoup(join(CorpusDirectory, "demo", `demo-${Name}-level${Index}.soup`), Soup.Positions,
                   Soup.CornerUVs, Soup.Families, Soup.SharpMask);
        return { ...Level, ...MeasureLevel(Soup, CreasePlanes) };
    });
    const LargestError = Math.max(...Nesting.Clusters.map((Cluster) => Cluster.Error));
    Report[Name] = {
        Valid: Validation.Passed,
        BuildMsMedian: Number(Durations[1].toFixed(1)),
        SourceTriangles: Nesting.SourceTriangles,
        Levels,
        PerClusterCut: Array.from({ length: 12 }, (_, Step) =>
            MeasurePerClusterCut(Nesting, LargestError * Math.pow(10, -3 + 3.2 * Step / 11))),
    };
    console.error(Name, Levels.map((Level) => Level.Triangles).join(" -> "), "valid", Validation.Passed);
}
writeFileSync(join(CorpusDirectory, "DemoNesting.json"), JSON.stringify(Report, null, 1));
