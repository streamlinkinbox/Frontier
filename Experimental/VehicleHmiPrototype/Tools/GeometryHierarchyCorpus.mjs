//============================================================================================================================================
//                                                          GEOMETRYHIERARCHYCORPUS.MJS
//============================================================================================================================================
// 🧩 Deterministic hierarchy test geometry: a closed smooth solid, a heightmap cliff mesh, a two-class solid with a glass band,
//    and a closed UV-mapped vessel with a meridian UV seam, separate cap UV islands, and marked sharp crease rings.

function Normalize(P) { const L = Math.hypot(P[0], P[1], P[2]) || 1; return [P[0] / L, P[1] / L, P[2] / L]; }

// 📝 Subdivided icosahedron projected to the unit sphere, emitted as facet soup. An optional latitude band
//    takes the glass family so opaque/transmissive class separation is exercised on one connected solid.
export function ConstructSphereFacets(Subdivisions = 4, GlassFamily = -1)
{
    const T = (1 + Math.sqrt(5)) / 2;
    let Vertices = [[-1, T, 0], [1, T, 0], [-1, -T, 0], [1, -T, 0], [0, -1, T], [0, 1, T], [0, -1, -T], [0, 1, -T],
        [T, 0, -1], [T, 0, 1], [-T, 0, -1], [-T, 0, 1]].map(Normalize);
    let Faces = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2],
        [10, 7, 6], [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11],
        [6, 2, 10], [8, 6, 7], [9, 8, 1]];
    for (let Pass = 0; Pass < Subdivisions; ++Pass)
    {
        const Midpoints = new Map();
        const Midpoint = (A, B) =>
        {
            const Key = A < B ? A + ":" + B : B + ":" + A;
            if (!Midpoints.has(Key))
            {
                Midpoints.set(Key, Vertices.length);
                Vertices.push(Normalize([0, 1, 2].map((Axis) => (Vertices[A][Axis] + Vertices[B][Axis]) / 2)));
            }
            return Midpoints.get(Key);
        };
        const Next = [];
        for (const [A, B, C] of Faces)
        {
            const AB = Midpoint(A, B), BC = Midpoint(B, C), CA = Midpoint(C, A);
            Next.push([A, AB, CA], [B, BC, AB], [C, CA, BC], [AB, BC, CA]);
        }
        Faces = Next;
    }
    return {
        FirstVertex: 0,
        Facets: Faces.map(([A, B, C]) =>
        {
            const Corners = [Vertices[A], Vertices[B], Vertices[C]];
            const CentroidY = (Corners[0][1] + Corners[1][1] + Corners[2][1]) / 3;
            const Family = GlassFamily >= 0 && Math.abs(CentroidY) < 0.25 ? GlassFamily : 0;
            return { Corners: Corners.map((P) => [...P]), Family };
        })
    };
}

// 📝 Heightmap terrain as a triangulated grid: rolling deterministic relief with one hard cliff step, the same
//    shape a heightmap or an SDF-converted cliff mesh presents after meshing (open border, sharp crease).
export function ConstructHeightfieldFacets(Resolution = 96, Extent = 40)
{
    const Height = (X, Y) =>
    {
        const Rolling = Math.sin(X * 0.31) * Math.cos(Y * 0.27) * 1.4 + Math.sin(X * 0.09 + Y * 0.13) * 2.2;
        const Cliff = X > Math.sin(Y * 0.21) * 4 ? 7.0 : 0.0;
        return Rolling + Cliff;
    };
    const Point = (I, J) =>
    {
        const X = (I / Resolution - 0.5) * Extent, Y = (J / Resolution - 0.5) * Extent;
        return [X, Y, Height(X, Y)];
    };
    const Facets = [];
    for (let J = 0; J < Resolution; ++J)
        for (let I = 0; I < Resolution; ++I)
        {
            const A = Point(I, J), B = Point(I + 1, J), C = Point(I + 1, J + 1), D = Point(I, J + 1);
            Facets.push({ Corners: [A, B, C], Family: 1 }, { Corners: [A, C, D], Family: 1 });
        }
    return { FirstVertex: 0, Facets };
}

// 📝 Closed UV-mapped creased vessel:
//    • Barrel unwrapped onto U in [0, 1], V in [0.15, 0.75] with a longitudinal UV seam along theta = 0 (U=0 vs U=1).
//    • Top and bottom caps unwrapped onto two separate circular UV islands in [0.05, 0.45]x[0.78, 0.98] and
//      [0.55, 0.95]x[0.78, 0.98], creating circular UV seams along both rims.
//    • Explicit marked sharp edges (SharpEdges bitmask) on the top rim, bottom rim, and equatorial waist ridge.
export function ConstructUvSeamCreasedMeshFacets(Segments = 48, Rings = 20, CapRings = 6)
{
    const EquatorRing = Rings >> 1;
    const BarrelPoint = (Slice, Ring) =>
    {
        const Wrapped = ((Slice % Segments) + Segments) % Segments;
        const Angle = (Wrapped / Segments) * Math.PI * 2;
        const V = Ring / Rings;
        const Ridge = 1.0 + 0.22 * (1.0 - Math.abs(2.0 * V - 1.0));
        const Z = (V - 0.5) * 2.0;
        return [Math.cos(Angle) * Ridge, Math.sin(Angle) * Ridge, Z];
    };
    const BarrelUv = (Slice, Ring) => [Slice / Segments, 0.15 + 0.6 * (Ring / Rings)];
    const Facets = [];
    for (let Ring = 0; Ring < Rings; ++Ring)
        for (let Slice = 0; Slice < Segments; ++Slice)
        {
            const P00 = BarrelPoint(Slice, Ring), P10 = BarrelPoint(Slice + 1, Ring);
            const P11 = BarrelPoint(Slice + 1, Ring + 1), P01 = BarrelPoint(Slice, Ring + 1);
            const U00 = BarrelUv(Slice, Ring), U10 = BarrelUv(Slice + 1, Ring);
            const U11 = BarrelUv(Slice + 1, Ring + 1), U01 = BarrelUv(Slice, Ring + 1);
            // Triangle 1: (P00, P10, P11) — edge 0 is bottom ring (Ring), edge 1 is slice+1 meridian, edge 2 is diagonal.
            const SharpBottom = Ring === 0 || Ring === EquatorRing ? 1 : 0;
            // Triangle 2: (P00, P11, P01) — edge 0 is diagonal, edge 1 is top ring (Ring+1), edge 2 is slice meridian.
            const SharpTop = Ring + 1 === Rings || Ring + 1 === EquatorRing ? 2 : 0;
            Facets.push(
                { Corners: [P00, P10, P11], UVs: [U00, U10, U11], SharpEdges: SharpBottom, Family: 2 },
                { Corners: [P00, P11, P01], UVs: [U00, U11, U01], SharpEdges: SharpTop, Family: 2 }
            );
        }
    // Emit top and bottom multi-ring cap discs as separate UV islands with marked sharp rim edges.
    const EmitCap = (Top) =>
    {
        const Z = Top ? 1.0 : -1.0;
        const UvCenter = Top ? [0.75, 0.88] : [0.25, 0.88];
        const UvRadius = 0.09;
        const CapPoint = (Slice, Step) =>
        {
            if (Step === 0) return [0, 0, Z + (Top ? 0.06 : -0.06)];
            if (Step === CapRings) return BarrelPoint(Slice, Top ? Rings : 0);
            const Wrapped = ((Slice % Segments) + Segments) % Segments;
            const Angle = (Wrapped / Segments) * Math.PI * 2;
            const Radius = Step / CapRings;
            const Dome = (1.0 - Radius * Radius) * 0.06;
            return [Math.cos(Angle) * Radius, Math.sin(Angle) * Radius, Z + (Top ? Dome : -Dome)];
        };
        const CapUv = (Slice, Step) =>
        {
            if (Step === 0) return [...UvCenter];
            const Wrapped = ((Slice % Segments) + Segments) % Segments;
            const Angle = (Wrapped / Segments) * Math.PI * 2;
            const R = (Step / CapRings) * UvRadius;
            return [UvCenter[0] + Math.cos(Angle) * R, UvCenter[1] + Math.sin(Angle) * R];
        };
        for (let Step = 0; Step < CapRings; ++Step)
            for (let Slice = 0; Slice < Segments; ++Slice)
            {
                if (Step === 0)
                {
                    const CenterP = CapPoint(0, 0), P0 = CapPoint(Slice, 1), P1 = CapPoint(Slice + 1, 1);
                    const CenterU = CapUv(0, 0), U0 = CapUv(Slice, 1), U1 = CapUv(Slice + 1, 1);
                    Facets.push(Top
                        ? { Corners: [CenterP, P0, P1], UVs: [CenterU, U0, U1], SharpEdges: 0, Family: 2 }
                        : { Corners: [CenterP, P1, P0], UVs: [CenterU, U1, U0], SharpEdges: 0, Family: 2 });
                }
                else
                {
                    const P00 = CapPoint(Slice, Step), P10 = CapPoint(Slice + 1, Step);
                    const P11 = CapPoint(Slice + 1, Step + 1), P01 = CapPoint(Slice, Step + 1);
                    const U00 = CapUv(Slice, Step), U10 = CapUv(Slice + 1, Step);
                    const U11 = CapUv(Slice + 1, Step + 1), U01 = CapUv(Slice, Step + 1);
                    const OnRim = Step + 1 === CapRings;
                    if (Top)
                    {
                        Facets.push(
                            { Corners: [P00, P10, P11], UVs: [U00, U10, U11], SharpEdges: 0, Family: 2 },
                            { Corners: [P00, P11, P01], UVs: [U00, U11, U01], SharpEdges: OnRim ? 2 : 0, Family: 2 }
                        );
                    }
                    else
                    {
                        Facets.push(
                            { Corners: [P00, P11, P10], UVs: [U00, U11, U10], SharpEdges: 0, Family: 2 },
                            { Corners: [P00, P01, P11], UVs: [U00, U01, U11], SharpEdges: OnRim ? 2 : 0, Family: 2 }
                        );
                    }
                }
            }
    };
    EmitCap(false);
    EmitCap(true);
    return { FirstVertex: 0, Facets };
}

export function ConstructTetrahedronFacets()
{
    const V = [[1, 1, 1], [-1, -1, 1], [-1, 1, -1], [1, -1, -1]];
    const Faces = [[0, 1, 2], [0, 3, 1], [0, 2, 3], [1, 3, 2]];
    return { FirstVertex: 0, Facets: Faces.map(([A, B, C]) => ({ Corners: [[...V[A]], [...V[B]], [...V[C]]], Family: 0 })) };
}
