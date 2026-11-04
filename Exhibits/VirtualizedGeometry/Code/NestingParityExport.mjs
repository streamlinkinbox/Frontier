//============================================================================================================================================
//                                                       NESTINGPARITYEXPORT.MJS
//============================================================================================================================================
// 🧩 Reference export for the C++ port of the demo's virtualized geometry (Frontier/Engine/Rendering/VirtualizedGeometry):
//    the demo's own corpus surfaces as indexed, zlib-compressed soups, and SHA-256 digests of every section of the demo
//    builder's output, plus the CPU cut at fixed camera distances, for the bit-exact parity validation.

// 📝 Usage: node NestingParityExport.mjs <VehicleHmiPrototype folder> <Exhibits/VirtualizedGeometry folder> [--dump <folder>]
//    Surfaces land in <exhibit>/Surfaces/<Name>.soup and digests in <exhibit>/Results/NestingParity.txt. --dump also writes
//    every serialized section raw, so a mismatch reported by NestingParityValidation can be diffed byte by byte.
//    Soup layout (little-endian, zlib-compressed): 'FSU1', u32 facet count, u32 position count, u32 flags (bit 0 UVs,
//    bit 1 corner normals), then f64 xyz per unique position, then per facet: u32 x3 position indices, u32 family,
//    u8 sharp-edge mask, and f64 x6 corner UVs / f64 x9 corner normals when flagged. Unique positions are keyed on their
//    exact bits, so the decoded soup carries the very doubles the demo generator produced.

import { createHash } from "node:crypto";
import { mkdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { deflateSync } from "node:zlib";

const [DemoFolder, ExhibitFolder] = process.argv.slice(2);
const DumpIndex = process.argv.indexOf("--dump");
const DumpFolder = DumpIndex > 0 ? process.argv[DumpIndex + 1] : null;
if (!DemoFolder || !ExhibitFolder)
{
    console.error("usage: node NestingParityExport.mjs <VehicleHmiPrototype folder> <exhibit folder> [--dump <folder>]");
    process.exit(2);
}
const Import = (Relative) => import(pathToFileURL(resolve(DemoFolder, Relative)).href);
const Builder = await Import("Source/GeometryHierarchy.js");
const Runtime = await Import("Source/GeometryHierarchyRuntime.js");
const Corpus = await Import("Tools/GeometryHierarchyCorpus.mjs");

// 📝 Every surface the demo's tests and the comparison corpus build, once each.
const Surfaces = [
    ["SphereSubdivision2", Corpus.ConstructSphereFacets(2), {}],
    ["SphereSubdivision3", Corpus.ConstructSphereFacets(3), {}],
    ["SphereSubdivision3Glass", Corpus.ConstructSphereFacets(3, 9), { TransmissiveFamilies: [9] }],
    ["SphereSubdivision4", Corpus.ConstructSphereFacets(4), {}],
    ["SphereSubdivision4Glass", Corpus.ConstructSphereFacets(4, 9), { TransmissiveFamilies: [9] }],
    ["HeightfieldCliff48", Corpus.ConstructHeightfieldFacets(48, 30), {}],
    ["HeightfieldCliff64", Corpus.ConstructHeightfieldFacets(64, 40), {}],
    ["Tetrahedron", Corpus.ConstructTetrahedronFacets(), {}],
    ["CreasedVessel32", Corpus.ConstructUvSeamCreasedMeshFacets(32, 14, 4), {}],
    ["CreasedVessel48", Corpus.ConstructUvSeamCreasedMeshFacets(48, 20, 6), {}]
];
const CutDistances = [1.5, 3, 6, 12, 24, 48, 96, 192];   // [m] eye distances of the parity cuts
const CutProjectionScale = 467;                          // [px] exact in float64, no tangent involved

//------------------------------------------------------------------------------------------------------------------------
//                                                     BYTE STREAM
//------------------------------------------------------------------------------------------------------------------------

class ByteStream
{
    constructor() { this.Bytes = new Uint8Array(1 << 16); this.Length = 0; this.View = new DataView(this.Bytes.buffer); }
    Reserve(Count)
    {
        if (this.Length + Count <= this.Bytes.length) return;
        let Capacity = this.Bytes.length;
        while (Capacity < this.Length + Count) Capacity *= 2;
        const Grown = new Uint8Array(Capacity);
        Grown.set(this.Bytes.subarray(0, this.Length));
        this.Bytes = Grown;
        this.View = new DataView(Grown.buffer);
    }
    U8(Value) { this.Reserve(1); this.View.setUint8(this.Length, Value); this.Length += 1; }
    U32(Value) { this.Reserve(4); this.View.setUint32(this.Length, Value >>> 0, true); this.Length += 4; }
    I32(Value) { this.Reserve(4); this.View.setInt32(this.Length, Value, true); this.Length += 4; }
    F64(Value) { this.Reserve(8); this.View.setFloat64(this.Length, Value, true); this.Length += 8; }
    U32List(Values) { this.U32(Values.length); for (const Value of Values) this.U32(Value); }
    F64Triple(Values) { for (let Axis = 0; Axis < 3; ++Axis) this.F64(Values[Axis]); }
    Content() { return this.Bytes.subarray(0, this.Length); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SURFACE SOUPS
//------------------------------------------------------------------------------------------------------------------------

function EncodeSoup(Source)
{
    const Keys = new Map(), Positions = [];
    const Bits = new DataView(new ArrayBuffer(24));
    const Identify = (P) =>
    {
        for (let Axis = 0; Axis < 3; ++Axis) Bits.setFloat64(Axis * 8, P[Axis], true);
        const Key = Array.from(new Uint8Array(Bits.buffer), (Byte) => Byte.toString(16).padStart(2, "0")).join("");
        let Index = Keys.get(Key);
        if (Index === undefined) { Index = Positions.length; Keys.set(Key, Index); Positions.push(P); }
        return Index;
    };
    const Indexed = Source.Facets.map((Facet) => Facet.Corners.map(Identify));
    const CarriesUv = Source.Facets.some((Facet) => Facet.UVs ?? Facet.Coordinates);
    const CarriesNormals = Source.Facets.some((Facet) => Facet.Normals);
    if (Source.SharpEdges) throw new Error("mesh-level SharpEdges are not part of the corpus encoding");
    const Stream = new ByteStream();
    for (const Character of "FSU1") Stream.U8(Character.charCodeAt(0));
    Stream.U32(Source.Facets.length);
    Stream.U32(Positions.length);
    Stream.U32((CarriesUv ? 1 : 0) | (CarriesNormals ? 2 : 0));
    for (const P of Positions) Stream.F64Triple(P);
    Source.Facets.forEach((Facet, Index) =>
    {
        for (const Corner of Indexed[Index]) Stream.U32(Corner);
        Stream.U32(Facet.Family ?? 0);
        let Mask = 0;
        if (typeof Facet.SharpEdges === "number") Mask = Facet.SharpEdges & 7;
        else if (Array.isArray(Facet.SharpEdges))
            for (let Edge = 0; Edge < 3; ++Edge) if (Facet.SharpEdges[Edge]) Mask |= 1 << Edge;
        Stream.U8(Mask);
        if (CarriesUv)
        {
            const UVs = Facet.UVs ?? Facet.Coordinates;
            if (!UVs) throw new Error("mixed UV presence is not part of the corpus encoding");
            for (const Uv of UVs) { Stream.F64(Uv[0]); Stream.F64(Uv[1]); }
        }
        if (CarriesNormals)
        {
            if (!Facet.Normals) throw new Error("mixed normal presence is not part of the corpus encoding");
            for (const N of Facet.Normals) Stream.F64Triple(N);
        }
    });
    return deflateSync(Stream.Content(), { level: 9 });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     BUILD SECTIONS
//------------------------------------------------------------------------------------------------------------------------

function SerializeClusters(Nesting)
{
    const Stream = new ByteStream();
    for (const Node of Nesting.Clusters)
    {
        Stream.U32(Node.Identity);
        Stream.U32(Node.Level);
        Stream.U32List(Node.Children);
        Stream.U32List(Node.Parents);
        Stream.I32(Node.GroupId);
        Stream.U32(Node.FirstTriangle);
        Stream.U32List(Node.Triangles);
        Stream.F64Triple(Node.Minimum);
        Stream.F64Triple(Node.Maximum);
        Stream.F64Triple(Node.Sphere.Center);
        Stream.F64(Node.Sphere.Radius);
        Stream.F64Triple(Node.Cone.Axis);
        Stream.F64(Node.Cone.Cutoff);
        Stream.F64Triple(Node.TightMinimum);
        Stream.F64Triple(Node.TightMaximum);
        Stream.F64Triple(Node.TightSphere.Center);
        Stream.F64(Node.TightSphere.Radius);
        Stream.F64(Node.Error);
        Stream.F64(Node.ParentError);
        Stream.F64Triple(Node.ParentSphere.Center);
        Stream.F64(Node.ParentSphere.Radius);
        Stream.U32(Node.ClassMask);
        Stream.U32(Node.SharpEdgesInCluster);
    }
    return Stream.Content();
}

function SerializeTier(Level)
{
    const Stream = new ByteStream();
    Stream.U32(Level.Level);
    Stream.U32List(Level.Clusters);
    Stream.U32(Level.VertexCount);
    for (const Value of Level.Positions) Stream.F64(Value);
    Stream.U32(Level.TriangleCount);
    for (const Value of Level.Triangles) Stream.U32(Value);
    for (const Value of Level.Families) Stream.U32(Value);
    for (const Value of Level.CornerUVs) Stream.F64(Value);
    for (const Value of Level.SharpMask) Stream.U8(Value);
    Stream.U32(Level.SharpEdgeCount);
    Stream.U32(Level.UvSeamEdgeCount);
    return Stream.Content();
}

function SerializeSummary(Nesting)
{
    const Stream = new ByteStream();
    Stream.U32(Nesting.SourceTriangles);
    Stream.U32(Nesting.WeldedVertices);
    Stream.U32(Nesting.HasUVs ? 1 : 0);
    Stream.U32(Nesting.Levels.length);
    Stream.U32(Nesting.Clusters.length);
    return Stream.Content();
}

// 📝 CPU cut (SelectHierarchyCut, antichain pruning included) for an eye on the -Y axis, slightly raised.
function SerializeCuts(Nesting)
{
    const Stream = new ByteStream();
    for (const Distance of CutDistances)
    {
        const Camera = { Eye: [0, -Distance, Distance * 0.25], Near: 0.1, ProjectionScale: CutProjectionScale };
        const Cut = Runtime.SelectHierarchyCut(Nesting, Camera);
        Stream.F64(Distance);
        Stream.U32List(Cut.CutClusterIds);
        Stream.U32(Cut.CutTriangles);
        for (const Identity of Cut.CutClusterIds)
        {
            const Metric = Cut.Metrics.get(Identity);
            Stream.F64(Metric.SelfErrorPx);
            Stream.F64(Metric.ParentErrorPx);
        }
    }
    return Stream.Content();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     EXPORT
//------------------------------------------------------------------------------------------------------------------------

const Digest = (Bytes) => createHash("sha256").update(Bytes).digest("hex");
mkdirSync(join(ExhibitFolder, "Surfaces"), { recursive: true });
mkdirSync(join(ExhibitFolder, "Results"), { recursive: true });
if (DumpFolder) mkdirSync(DumpFolder, { recursive: true });
const Lines = [
    "# Demo builder output digests (Exhibits/VirtualizedGeometry/Code/NestingParityExport.mjs, Node " + process.versions.node + ").",
    "# <surface> <section> <bytes> <sha256> — sections: Summary, Clusters, Tier<k>, Cuts; options: TransmissiveFamilies [9]",
    "# on the *Glass surfaces, defaults otherwise; cut eyes (0, -d, d/4) for d in " + CutDistances.join(" ") +
        " m, projection scale " + CutProjectionScale + " px."
];
for (const [Name, Source, Options] of Surfaces)
{
    writeFileSync(join(ExhibitFolder, "Surfaces", Name + ".soup"), EncodeSoup(Source));
    const Started = performance.now();
    const Nesting = Builder.BuildGeometryHierarchy(Source, Options);
    const Elapsed = performance.now() - Started;
    const Sections = [["Summary", SerializeSummary(Nesting)], ["Clusters", SerializeClusters(Nesting)]];
    for (const Level of Nesting.Levels) Sections.push(["Tier" + Level.Level, SerializeTier(Level)]);
    Sections.push(["Cuts", SerializeCuts(Nesting)]);
    for (const [Section, Bytes] of Sections)
    {
        Lines.push(`${Name} ${Section} ${Bytes.length} ${Digest(Bytes)}`);
        if (DumpFolder) writeFileSync(join(DumpFolder, `${Name}.${Section}.bin`), Bytes);
    }
    console.log(`${Name}: ${Source.Facets.length} facets, ${Nesting.Levels.length} tiers, ${Nesting.Clusters.length} clusters, ` +
                `${Elapsed.toFixed(0)} ms`);
}
writeFileSync(join(ExhibitFolder, "Results", "NestingParity.txt"), Lines.join("\n") + "\n");
