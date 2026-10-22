//============================================================================================================================================
//                                                        REPLAYREFERENCE.MJS
//============================================================================================================================================
// 🧩 Rebuild the HTML reference's level-0 clusters for the K1 corpora, so the C++ port can be compared against the behaviour it
//    claims to implement rather than against expectations written into the port's own validation program.
//
// 📝 The reference sources live on the HTML line, not here:
//      git show c4ef131:Experimental/VehicleHmiPrototype/Source/GeometryHierarchy.js          > Reference/GeometryHierarchy.mjs
//      git show c4ef131:Experimental/VehicleHmiPrototype/Tools/GeometryHierarchyCorpus.mjs    > Reference/GeometryHierarchyCorpus.mjs
//    Usage: node Exhibits/FacetCluster/Reference/ReplayReference.mjs <output path>
//
//    The heightfield entry is emitted for information only: V8 and glibc transcendental functions may differ in the final bit,
//    and heights are the only values in these corpora that come from sin/cos. Welding is unaffected because every duplicate
//    corner is produced by the identical expression; the emitted welded-vertex count reflects that.

import { BuildGeometryHierarchy } from "./GeometryHierarchy.mjs";
import { ConstructSphereFacets, ConstructHeightfieldFacets } from "./GeometryHierarchyCorpus.mjs";
import { writeFileSync } from "node:fs";

const Destination = process.argv[2] ?? "level0-reference.json";
const Dump =
{
    sphere:
    {
        source: ConstructSphereFacets(4),
        triangles: 5120
    },
    heightfield:
    {
        source: ConstructHeightfieldFacets(96, 40),
        triangles: 18432
    }
};

const Output = {};
for (const [Name, Entry] of Object.entries(Dump))
{
    const Built = BuildGeometryHierarchy(Entry.source, {});
    const Leaf = Built.Levels[0];
    Output[Name] =
    {
        triangles: Leaf.TriangleCount,
        vertices: Leaf.VertexCount,
        sharp: Leaf.SharpEdgeCount,
        uvseam: Leaf.UvSeamEdgeCount,
        clusters: Built.Clusters.filter((Cluster) => Cluster.Level === 0).map((Cluster) => ({
            first: Cluster.FirstTriangle,
            count: Cluster.TriangleCount,
            minimum: Cluster.TightMinimum,
            maximum: Cluster.TightMaximum,
            centre: Cluster.Sphere.Center,
            radius: Cluster.Sphere.Radius,
            axis: Cluster.Cone.Axis,
            cutoff: Cluster.Cone.Cutoff
        }))
    };
    console.log(`${Name}: ${Output[Name].triangles} facets, ${Output[Name].vertices} welded vertices, ` +
        `${Output[Name].clusters.length} level-0 clusters, ${Output[Name].sharp} sharp, ${Output[Name].uvseam} uv seams`);
}
writeFileSync(Destination, JSON.stringify(Output));
console.log(`wrote ${Destination}`);
