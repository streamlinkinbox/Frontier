//==========================================================================================================================================
//                                                           SURFACEDEVIATION.MJS
//==========================================================================================================================================
// 🧩 Two-sided surface deviation of every DAG level against its source surface, for the demo's builder and the
//    branch's C++ builder, as a percentage of the source bounding-box diagonal (Hausdorff maximum and RMS).
//
// Usage: node SurfaceDeviation.mjs <corpus directory>   (after CorpusExport.mjs and both TierComparison modes)

import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

//------------------------------------------------------------------------------------------------------------------------
//                                                    FACET SOUP FILES
//------------------------------------------------------------------------------------------------------------------------

function DecodeCorners(Path)
{
    const Bytes = readFileSync(Path);
    let Offset = 0;
    const Count = Bytes.readUInt32LE(Offset); Offset += 4;
    const CarriesUv = Bytes.readUInt32LE(Offset); Offset += 4;
    const Corners = new Float64Array(Count * 9);
    for (let Facet = 0; Facet < Count; ++Facet)
    {
        for (let K = 0; K < 9; ++K) { Corners[Facet * 9 + K] = Bytes.readDoubleLE(Offset); Offset += 8; }
        Offset += (CarriesUv ? 48 : 0) + 8;
    }
    return Corners;
}

function DistinctPoints(Corners)
{
    const Seen = new Set(), Points = [];
    for (let K = 0; K < Corners.length; K += 3)
    {
        const Key = Corners[K] + "," + Corners[K + 1] + "," + Corners[K + 2];
        if (!Seen.has(Key)) { Seen.add(Key); Points.push(Corners[K], Corners[K + 1], Corners[K + 2]); }
    }
    return new Float64Array(Points);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   POINT TO TRIANGLE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Closest point on a triangle by Voronoi region (Ericson, Real-Time Collision Detection §5.1.5), squared.
function SquaredDistance(Px, Py, Pz, T, I)
{
    const Ax = T[I], Ay = T[I + 1], Az = T[I + 2], Bx = T[I + 3], By = T[I + 4], Bz = T[I + 5];
    const Cx = T[I + 6], Cy = T[I + 7], Cz = T[I + 8];
    const ABx = Bx - Ax, ABy = By - Ay, ABz = Bz - Az, ACx = Cx - Ax, ACy = Cy - Ay, ACz = Cz - Az;
    const APx = Px - Ax, APy = Py - Ay, APz = Pz - Az;
    const D1 = ABx * APx + ABy * APy + ABz * APz, D2 = ACx * APx + ACy * APy + ACz * APz;
    let Qx = Ax, Qy = Ay, Qz = Az;
    if (!(D1 <= 0 && D2 <= 0))
    {
        const BPx = Px - Bx, BPy = Py - By, BPz = Pz - Bz;
        const D3 = ABx * BPx + ABy * BPy + ABz * BPz, D4 = ACx * BPx + ACy * BPy + ACz * BPz;
        const CPx = Px - Cx, CPy = Py - Cy, CPz = Pz - Cz;
        const D5 = ABx * CPx + ABy * CPy + ABz * CPz, D6 = ACx * CPx + ACy * CPy + ACz * CPz;
        const VC = D1 * D4 - D3 * D2, VB = D5 * D2 - D1 * D6, VA = D3 * D6 - D5 * D4;
        if (D3 >= 0 && D4 <= D3) { Qx = Bx; Qy = By; Qz = Bz; }
        else if (VC <= 0 && D1 >= 0 && D3 <= 0)
        { const V = D1 / (D1 - D3); Qx = Ax + V * ABx; Qy = Ay + V * ABy; Qz = Az + V * ABz; }
        else if (D6 >= 0 && D5 <= D6) { Qx = Cx; Qy = Cy; Qz = Cz; }
        else if (VB <= 0 && D2 >= 0 && D6 <= 0)
        { const W = D2 / (D2 - D6); Qx = Ax + W * ACx; Qy = Ay + W * ACy; Qz = Az + W * ACz; }
        else if (VA <= 0 && (D4 - D3) >= 0 && (D5 - D6) >= 0)
        {
            const W = (D4 - D3) / ((D4 - D3) + (D5 - D6));
            Qx = Bx + W * (Cx - Bx); Qy = By + W * (Cy - By); Qz = Bz + W * (Cz - Bz);
        }
        else
        {
            const Scale = 1 / (VA + VB + VC), V = VB * Scale, W = VC * Scale;
            Qx = Ax + ABx * V + ACx * W; Qy = Ay + ABy * V + ACy * W; Qz = Az + ABz * V + ACz * W;
        }
    }
    return (Px - Qx) ** 2 + (Py - Qy) ** 2 + (Pz - Qz) ** 2;
}

// One direction: every point to its nearest triangle (exhaustive; the corpus is small).
function MeasureOneSide(Points, Triangles)
{
    const Count = Triangles.length / 9;
    let Worst = 0, Total = 0;
    for (let P = 0; P < Points.length; P += 3)
    {
        let Nearest = Infinity;
        for (let Triangle = 0; Triangle < Count; ++Triangle)
        {
            const Distance = SquaredDistance(Points[P], Points[P + 1], Points[P + 2], Triangles, Triangle * 9);
            Nearest = Math.min(Nearest, Distance);
        }
        Worst = Math.max(Worst, Nearest);
        Total += Nearest;
    }
    return { Worst: Math.sqrt(Worst), Total, Count: Points.length / 3 };
}

function Diagonal(Corners)
{
    const Lowest = [Infinity, Infinity, Infinity], Highest = [-Infinity, -Infinity, -Infinity];
    for (let K = 0; K < Corners.length; K += 3)
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            Lowest[Axis]  = Math.min(Lowest[Axis], Corners[K + Axis]);
            Highest[Axis] = Math.max(Highest[Axis], Corners[K + Axis]);
        }
    return Math.hypot(Highest[0] - Lowest[0], Highest[1] - Lowest[1], Highest[2] - Lowest[2]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          MAIN
//------------------------------------------------------------------------------------------------------------------------

const CorpusDirectory = process.argv[2];
if (!CorpusDirectory)
{
    console.error("usage: node SurfaceDeviation.mjs <corpus directory>");
    process.exit(2);
}
const Builders = [
    ["demo", (Surface, Level) => join(CorpusDirectory, "demo", `demo-${Surface}-level${Level}.soup`)],
    ["cpp-marked", (Surface, Level) => join(CorpusDirectory, "marked", `cpp-${Surface}-tier${Level}.soup`)],
    ["cpp-crease", (Surface, Level) => join(CorpusDirectory, "crease", `cpp-${Surface}-tier${Level}.soup`)],
];
const Report = {};
for (const Surface of ["sphere", "glass-sphere", "heightfield-cliff", "uv-creased-vessel"])
{
    const Source = DecodeCorners(join(CorpusDirectory, Surface + ".soup"));
    const SourcePoints = DistinctPoints(Source);
    const Scale = 100 / Diagonal(Source);
    Report[Surface] = {};
    for (const [Builder, PathOf] of Builders)
    {
        const Rows = [];
        for (let Level = 1; existsSync(PathOf(Surface, Level)); ++Level)
        {
            const Approximation = DecodeCorners(PathOf(Surface, Level));
            const Forward  = MeasureOneSide(SourcePoints, Approximation);
            const Backward = MeasureOneSide(DistinctPoints(Approximation), Source);
            Rows.push({
                Level,
                Triangles: Approximation.length / 9,
                HausdorffPct: Number((Scale * Math.max(Forward.Worst, Backward.Worst)).toFixed(4)),
                RmsPct: Number((Scale * Math.sqrt((Forward.Total + Backward.Total) /
                                                  (Forward.Count + Backward.Count))).toFixed(4)),
            });
        }
        Report[Surface][Builder] = Rows;
    }
    console.error(Surface, "measured");
}
writeFileSync(join(CorpusDirectory, "SurfaceDeviation.json"), JSON.stringify(Report, null, 1));
