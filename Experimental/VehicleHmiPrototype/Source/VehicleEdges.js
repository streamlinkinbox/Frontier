//============================================================================================================================================
//                                                            VEHICLEEDGES.JS
//============================================================================================================================================
// 🧩 The scan's wireframe: the car's feature edges, extracted once on the CPU exactly as THREE.EdgesGeometry does it for the reference
//    (Truck.tsx: body parts 14°, tyres 28°), one set per material family because the reference builds one mesh per part.

import { CourseMaterial } from "./MaterialSpecification.js";

// 📝 Which families carry a wireframe and at what crease angle [°]: the painted and clad body panels (BodyPart: EdgesGeometry(geo, 14)) and the
//    tyres (EdgesGeometry(tireGeo, 28)). Rims, hubs, glass, lamps and the interior have no wireframe in the reference.
export const EdgeFamilies = Object.freeze([
    { Family: CourseMaterial.BodyCoat,       Threshold: 14.0 },
    { Family: CourseMaterial.VehiclePlastic, Threshold: 14.0 },
    { Family: CourseMaterial.Tyre,           Threshold: 28.0 }
]);

const WeldPrecision = 1.0e4;   // [1/unit] EdgesGeometry: precisionPoints = 4

// 📐 THREE.EdgesGeometry, step for step: vertices weld by their position rounded to four decimals; an edge shared by two triangles in opposite
//    directions is kept when the angle between their normals reaches the threshold (n0·n1 ≤ cos threshold); an edge only one triangle has is
//    a boundary and is kept. Facets: [{ Corners: [[x,y,z] × 3] }]. Returns a flat array of segment end points: ax, ay, az, bx, by, bz.
export function ExtractFeatureEdges(Facets, ThresholdDegrees)
{
    const ThresholdDot = Math.cos(ThresholdDegrees * Math.PI / 180.0);
    const Pending = new Map();
    const Segments = [];
    for (const Facet of Facets)
    {
        const [A, B, C] = Facet.Corners;
        // 📝 THREE.Triangle.getNormal: (c − b) × (a − b), normalised; a degenerate triangle has none and contributes nothing.
        const U = [C[0] - B[0], C[1] - B[1], C[2] - B[2]];
        const V = [A[0] - B[0], A[1] - B[1], A[2] - B[2]];
        const Normal = [U[1] * V[2] - U[2] * V[1], U[2] * V[0] - U[0] * V[2], U[0] * V[1] - U[1] * V[0]];
        const Length = Math.hypot(Normal[0], Normal[1], Normal[2]);
        if (!(Length > 0.0))
        {
            continue;
        }
        Normal[0] /= Length; Normal[1] /= Length; Normal[2] /= Length;
        const Corners = [A, B, C];
        const Hashes = Corners.map((P) => `${Math.round(P[0] * WeldPrecision)},${Math.round(P[1] * WeldPrecision)},${Math.round(P[2] * WeldPrecision)}`);
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            const Next = (Corner + 1) % 3;
            const Key = `${Hashes[Corner]}_${Hashes[Next]}`;
            const Reverse = `${Hashes[Next]}_${Hashes[Corner]}`;
            const Sibling = Pending.get(Reverse);
            if (Sibling)
            {
                const Dot = Normal[0] * Sibling.Normal[0] + Normal[1] * Sibling.Normal[1] + Normal[2] * Sibling.Normal[2];
                if (Dot <= ThresholdDot)
                {
                    Segments.push(...Corners[Corner], ...Corners[Next]);
                }
                Pending.set(Reverse, null);
            }
            else if (!Pending.has(Key))
            {
                Pending.set(Key, { From: Corners[Corner], To: Corners[Next], Normal: Normal.slice() });
            }
        }
    }
    for (const Edge of Pending.values())
    {
        if (Edge)
        {
            Segments.push(...Edge.From, ...Edge.To);
        }
    }
    return Segments;
}

// 📝 The records the overlay shader reads: per segment two vec4s — A.xyz and the tag (occupant | family << 8, as raw bits), B.xyz and zero.
//    The hero's body is occupant 0 and its four wheels 1–4; the wheel set is shared, so its edges are extracted once and repeated per wheel.
export function ConstructScanEdges(Scene)
{
    const Records = [];
    const Tag = new Uint32Array(1);
    const Reader = new Float32Array(Tag.buffer);
    const Append = (Occupant, Family, Segments) =>
    {
        Tag[0] = Occupant | (Family << 8);
        for (let Index = 0; Index < Segments.length; Index += 6)
        {
            Records.push(Segments[Index], Segments[Index + 1], Segments[Index + 2], Reader[0],
                         Segments[Index + 3], Segments[Index + 4], Segments[Index + 5], 0.0);
        }
    };
    const Occupants = [Scene.VehicleOccupants.Body, ...Scene.VehicleOccupants.Wheels];
    const Extracted = new Map();
    for (const Occupant of Occupants)
    {
        const SetIndex = Scene.Occupants[Occupant].FacetSet;
        for (const { Family, Threshold } of EdgeFamilies)
        {
            const Key = `${SetIndex}:${Family}`;
            if (!Extracted.has(Key))
            {
                const Facets = Scene.FacetSets[SetIndex].Facets.filter((Facet) => Facet.Family === Family);
                Extracted.set(Key, ExtractFeatureEdges(Facets, Threshold));
            }
            Append(Occupant, Family, Extracted.get(Key));
        }
    }
    return { Records: new Float32Array(Records), Count: Records.length / 8 };
}
