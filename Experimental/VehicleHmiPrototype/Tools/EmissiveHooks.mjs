// Invariants of the emissive structure on the REAL scene, checked in-process after bring-up; prints one JSON line (consumed by EmissiveLights.test.mjs).
import { SampleEmissiveLight, CellWords, Slot, Luminance, IsListedEmitter } from "./EmissiveReference.mjs";
import { LightClusterBounds } from "../Source/LightClusterSpace.js";
export default {
    BeforeFrame() {},
    async Report(Host) {
        const Failures = [], E = Host.Emissive, Scene = Host.Scene, M = Host.Materials, Bvh = Host.RayTracing;
        const Check = (Condition, Text) => { if (!Condition && Failures.length < 12) Failures.push(Text); };
        Check(!!E && E.Count > 0, "the structure is built");
        const Emissive = new Set(); M.forEach((Material, Family) => { if (Luminance(Material.Emission) >= 0.5) Emissive.add(Family); });
        // 1. every emissive triangle of every represented set is in the table once; each table triangle IS the BVH triangle it names (A, E1, E2 and family)
        let Expected = 0, Found = 0, Matched = 0;
        for (const [SetIndex, Info] of E.Sets) {
            const Facets = Scene.FacetSets[SetIndex].Facets.filter((F) => Emissive.has(F.Family) && F.Corners);
            Expected += Facets.length; Found += Info.Clusters.reduce((S, C) => S + C.Count, 0);
            Check(Info.Clusters.every((C) => C.Count >= 1 && C.Count <= 8), `set ${SetIndex}: cluster size`);
        }
        Check(Expected === Found && Found === E.TriangleCount, `table covers every emissive triangle once (${Found} of ${Expected}, table ${E.TriangleCount})`);
        for (let Index = 0; Index < E.TriangleCount; ++Index) {
            const T = Index * 12, Slot3 = E.TriangleWords[T + 3];
            const B = Slot3 * 16;
            const Same = Slot3 !== 0xFFFFFFFF && Slot3 < Bvh.TotalTriangles
                && [0, 1, 2].every((K) => Bvh.AllTriScalars[B + K] === Math.fround(E.TriangleFloats[T + K]) && Bvh.AllTriScalars[B + 4 + K] === Math.fround(E.TriangleFloats[T + 4 + K]) && Bvh.AllTriScalars[B + 8 + K] === Math.fround(E.TriangleFloats[T + 8 + K]))
                && Bvh.AllTriIntegers[B + 3] === E.TriangleWords[T + 11];
            if (Same) Matched++; else Check(false, `table triangle ${Index} does not match BVH triangle ${Slot3}`);
        }
        // 2. every instance's bounding sphere contains its triangles' world corners; powers add up
        let PowerSum = 0, PowerExpected = 0;
        E.Instances.forEach((Instance, SlotIndex) => {
            const T = Scene.Occupants[Instance.Occupant].Transform, O = SlotIndex * 8, F = E.ClusterFloats;
            const Centre = [F[O], F[O + 1], F[O + 2]], Radius = F[O + 3];
            for (const Tri of Instance.Cluster.Triangles) for (const C of [Tri.A, Tri.A.map((V, K) => V + Tri.E1[K]), Tri.A.map((V, K) => V + Tri.E2[K])]) {
                const P = [T[0] * C[0] + T[4] * C[1] + T[8] * C[2] + T[12], T[1] * C[0] + T[5] * C[1] + T[9] * C[2] + T[13], T[2] * C[0] + T[6] * C[1] + T[10] * C[2] + T[14]];
                Check(Math.hypot(P[0] - Centre[0], P[1] - Centre[1], P[2] - Centre[2]) <= Radius * 1.001 + 1e-4, `instance ${SlotIndex}: sphere contains its triangles`);
            }
            PowerSum += F[O + 7];
            const S0 = Math.hypot(T[0], T[1], T[2]), S1 = Math.hypot(T[4], T[5], T[6]);
            for (const Tri of Instance.Cluster.Triangles) PowerExpected += Tri.Area * Luminance(M[Tri.Family].Emission) * S0 * S1 * Host.Settings.EmissionScale;
        });
        Check(Math.abs(PowerSum - PowerExpected) <= 1e-3 * Math.max(1, PowerExpected), `powers add up (${PowerSum} vs ${PowerExpected})`);
        // 3. an independent recomputation of the cell lists: candidates are the instances whose reach touches the cell box, the list is the eight strongest by power / (d² + r²) at the centre
        const B = LightClusterBounds, Size = B.Extent.map((V, A) => V / B.Resolution[A]);
        let CellsChecked = 0, CellsMatching = 0, ListLengths = 0, UsableCells = 0;
        const Reach = E.Instances.map((_, I) => { const O = I * 8, F = E.ClusterFloats; return Math.sqrt(F[O + 7] / 5.0e-4) + F[O + 3]; });
        for (let Cell = 0; Cell < 1024; ++Cell) {
            const X = Cell % 16, Y = Math.floor(Cell / 16) % 16, Z = Math.floor(Cell / 256);
            const Corner = [B.Minimum[0] + X * Size[0], B.Minimum[1] + Y * Size[1], B.Minimum[2] + Z * Size[2]];
            const Candidates = [];
            E.Instances.forEach((_, I) => {
                const O = I * 8, F = E.ClusterFloats, C = [F[O], F[O + 1], F[O + 2]];
                if (!(F[O + 7] > 0)) return;
                let Near2 = 0, Centre2 = 0;
                for (let A = 0; A < 3; ++A) { const Near = Math.min(Math.max(C[A], Corner[A]), Corner[A] + Size[A]); Near2 += (C[A] - Near) ** 2; Centre2 += (C[A] - (Corner[A] + 0.5 * Size[A])) ** 2; }
                if (Near2 < Reach[I] * Reach[I]) Candidates.push({ Id: I, Importance: F[O + 7] / (Centre2 + F[O + 3] * F[O + 3] + 1e-4) });
            });
            Candidates.sort((P, Q) => Q.Importance - P.Importance || P.Id - Q.Id);
            const Want = Candidates.slice(0, 8).map((C) => C.Id);
            const Words = [0, 1, 2, 3].map((K) => E.Cells[Cell * 4 + K]);
            const Got = [...Array(8).keys()].map((I) => Slot(Words, I)).filter((Id) => Id !== 0x7FFF);
            CellsChecked++; ListLengths += Got.length; UsableCells++;
            if (Want.length === Got.length && Want.every((Id, K) => Got[K] === Id)) CellsMatching++; else Check(false, `cell ${Cell}: list ${Got} vs recomputed ${Want}`);
        }
        // 4. sampling from real cells yields finite pdfs and radiances, and every sampled point is a listed emitter of its own cell
        let Rng = 99, Sampled = 0, ListedOk = 0;
        const Rand = () => { Rng = (Math.imul(Rng, 1664525) + 1013904223) >>> 0; return Rng / 4294967296; };
        for (let K = 0; K < 600; ++K) {
            const P = [B.Minimum[0] + Rand() * B.Extent[0], B.Minimum[1] + Rand() * B.Extent[1], B.Minimum[2] + Rand() * B.Extent[2]];
            const Words = CellWords(E, B, P);
            const S = SampleEmissiveLight(E, Scene.Occupants, M, 1.0, P, Words, [Rand(), Rand(), Rand(), Rand()]);
            if (!S.Valid) continue;
            Sampled++;
            Check(S.Pdf > 0 && Number.isFinite(S.Pdf) && S.Radiance.every(Number.isFinite), "sample pdf and radiance are finite");
            // the sampled cluster's triangles carry BVH ids, and the cell lists that cluster's occupant
            const Slots = [...Array(8).keys()].map((I) => Slot(Words, I));
            if (Slots.includes(S.Cluster) && IsListedEmitter(E, Words, S.Occupant, E.TriangleWords[E.ClusterWords[S.Cluster * 8 + 5] * 12 + 3])) ListedOk++;
        }
        Check(Sampled === ListedOk, "every sampled emitter is listed in the cell it was drawn from");
        console.log("EMISSIVE " + JSON.stringify({ Failures, Sets: E.Sets.size, Instances: E.Count, TableTriangles: E.TriangleCount, BvhMatched: Matched, TruncatedCells: E.Statistics.TruncatedCells,
            EmptyCells: E.Statistics.EmptyCells, CellsChecked, CellsMatching, UsableCells, MeanListLength: ListLengths / Math.max(1, UsableCells), Sampled, BuildMilliseconds: E.Statistics.BuildMilliseconds }));
    }
};
