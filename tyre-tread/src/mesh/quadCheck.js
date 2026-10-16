/**
 * Topology report for a quad mesh. Used by the tests and shown in the editor.
 *
 * A clean closed quad mesh of a torus must satisfy:
 *   - every face is a quad with four distinct vertices
 *   - every undirected edge is shared by exactly two faces (no boundary, no non-manifold edges)
 *   - every directed edge appears at most once (consistent orientation)
 *   - Euler characteristic V - E + F = 0 (torus)
 *   - all positions are finite
 */
export function checkQuadMesh(positions, quads) {
    const vCount = positions.length / 3;
    const fCount = quads.length / 4;
    const report = {
        vertices: vCount,
        faces: fCount,
        edges: 0,
        triangles: 0,
        degenerateFaces: 0,
        boundaryEdges: 0,
        nonManifoldEdges: 0,
        orientationConflicts: 0,
        isolatedVertices: 0,
        nonFinite: 0,
        eulerCharacteristic: 0,
        ok: false,
    };
    for (let i = 0; i < positions.length; i++) if (!Number.isFinite(positions[i])) report.nonFinite++;

    // Numeric keys (a * vCount + b) keep this fast on large meshes. Safe while vCount^2 < 2^53.
    const edgeCount = new Map();   // undirected key -> number of faces using it
    const directed = new Set();    // directed key a -> b
    const used = new Uint8Array(vCount);

    for (let f = 0; f < fCount; f++) {
        const v0 = quads[4 * f], v1 = quads[4 * f + 1], v2 = quads[4 * f + 2], v3 = quads[4 * f + 3];
        if (v0 === v1 || v0 === v2 || v0 === v3 || v1 === v2 || v1 === v3 || v2 === v3) report.degenerateFaces++;
        used[v0] = used[v1] = used[v2] = used[v3] = 1;
        const v = [v0, v1, v2, v3];
        for (let e = 0; e < 4; e++) {
            const a = v[e], b = v[(e + 1) % 4];
            const key = a < b ? a * vCount + b : b * vCount + a;
            edgeCount.set(key, (edgeCount.get(key) ?? 0) + 1);
            const dkey = a * vCount + b;
            if (directed.has(dkey)) report.orientationConflicts++;
            directed.add(dkey);
        }
    }
    report.edges = edgeCount.size;
    for (const c of edgeCount.values()) {
        if (c === 1) report.boundaryEdges++;
        else if (c > 2) report.nonManifoldEdges++;
    }
    for (let i = 0; i < vCount; i++) if (!used[i]) report.isolatedVertices++;
    report.eulerCharacteristic = vCount - report.edges + fCount;
    report.ok =
        report.degenerateFaces === 0 &&
        report.boundaryEdges === 0 &&
        report.nonManifoldEdges === 0 &&
        report.orientationConflicts === 0 &&
        report.isolatedVertices === 0 &&
        report.nonFinite === 0 &&
        report.eulerCharacteristic === 0;
    return report;
}

/**
 * Mean signed volume check: a closed, consistently oriented mesh has a positive enclosed volume
 * when its face normals point out. Returns the volume in the position units cubed.
 */
export function signedVolume(positions, quads) {
    let vol = 0;
    const p = (i) => [positions[3 * i], positions[3 * i + 1], positions[3 * i + 2]];
    for (let f = 0; f < quads.length / 4; f++) {
        const a = p(quads[4 * f]), b = p(quads[4 * f + 1]), c = p(quads[4 * f + 2]), d = p(quads[4 * f + 3]);
        // Split the quad into two triangles (a,b,c) and (a,c,d).
        vol += tetraVolume(a, b, c) + tetraVolume(a, c, d);
    }
    return vol / 6;
}

function tetraVolume(a, b, c) {
    return (
        a[0] * (b[1] * c[2] - b[2] * c[1]) -
        a[1] * (b[0] * c[2] - b[2] * c[0]) +
        a[2] * (b[0] * c[1] - b[1] * c[0])
    );
}
