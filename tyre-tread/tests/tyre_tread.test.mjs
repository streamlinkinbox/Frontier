import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createPattern, depthAt, parsePattern, serialisePattern, fitPitch } from '../src/tread/pattern.js';
import { buildQuadTyre, buildCrossSectionLoop, BUDGET, TYRE_DEFAULTS } from '../src/mesh/quadTyre.js';
import { patternBreakpoints } from '../src/tread/pattern.js';
import { checkQuadMesh, signedVolume } from '../src/mesh/quadCheck.js';
import { quadMeshToObj } from '../src/io/obj.js';
import { PRESET_PATTERNS } from '../src/tread/presets.js';

const straightGroove = createPattern({ pitchMm: 200, shapes: [
    { kind: 'stroke', pts: [[0, 0], [200, 0]], closed: true, width: 8, depth: 1 },
] });

test('pattern: groove covers its centre line and not 10 mm away', () => {
    assert.equal(depthAt(straightGroove, 50, 0), 1);
    assert.equal(depthAt(straightGroove, 50, 3.9), 1);
    assert.equal(depthAt(straightGroove, 50, 4.5), 0);
    assert.equal(depthAt(straightGroove, 50, 10), 0);
});

test('pattern: x wraps with the pitch', () => {
    assert.equal(depthAt(straightGroove, 50 + 200, 0), 1);
    assert.equal(depthAt(straightGroove, 50 - 400, 0), 1);
});

test('pattern: mirror flag mirrors across the centre line', () => {
    const p = createPattern({ pitchMm: 200, mirror: true, shapes: [
        { kind: 'dimple', c: [100, 30], r: 5, depth: 0.5 },
    ] });
    assert.equal(depthAt(p, 100, 30), 0.5);
    assert.equal(depthAt(p, 100, -30), 0.5);
    assert.equal(depthAt(p, 100, 0), 0);
});

test('pattern: fill and dimple cover their interiors', () => {
    const p = createPattern({ pitchMm: 300, shapes: [
        { kind: 'fill', pts: [[10, -20], [60, -20], [60, 20], [10, 20]], depth: 0.6 },
        { kind: 'dimple', c: [200, 0], r: 15, depth: 0.9 },
    ] });
    assert.equal(depthAt(p, 30, 0), 0.6);
    assert.equal(depthAt(p, 70, 0), 0);
    assert.equal(depthAt(p, 200 + 14, 0), 0.9);
    assert.equal(depthAt(p, 200 + 16, 0), 0);
});

test('pattern: deepest covering shape wins', () => {
    const p = createPattern({ pitchMm: 100, shapes: [
        { kind: 'fill', pts: [[0, -10], [50, -10], [50, 10]], depth: 0.3 },
        { kind: 'fill', pts: [[0, -10], [50, -10], [50, 10]], depth: 0.8 },
    ] });
    assert.equal(depthAt(p, 40, 0), 0.8); // (40,0) is inside the triangle
});

test('pattern: JSON round trip is lossless', () => {
    const text = serialisePattern(PRESET_PATTERNS[0].pattern);
    const back = parsePattern(text);
    assert.deepEqual(back, parsePattern(serialisePattern(back)));
    assert.equal(back.shapes.length, PRESET_PATTERNS[0].pattern.shapes.length);
});

test('pattern: invalid shapes are rejected', () => {
    assert.throws(() => createPattern({ shapes: [{ kind: 'blob', depth: 1 }] }));
    assert.throws(() => createPattern({ shapes: [{ kind: 'stroke', pts: [[0, 0]], width: 4, depth: 1 }] }));
    assert.throws(() => createPattern({ shapes: [{ kind: 'stroke', pts: [[0, 0], [1, 1]], width: 4, depth: NaN }] }));
});

test('pattern: fitPitch gives whole repeats around the circumference', () => {
    const fit = fitPitch(createPattern({ pitchMm: 240 }), 1987);
    assert.ok(Math.abs(fit.repeats * fit.pitchMm - 1987) < 1e-9);
});

test('loop: cross-section is a simple closed curve (no self-intersections)', () => {
    for (const b of [BUDGET.low, BUDGET.medium, BUDGET.fine]) {
        const loop = buildCrossSectionLoop(TYRE_DEFAULTS, b);
        const n = loop.length;
        const seg = (j) => [loop[j], loop[(j + 1) % n]];
        const cross = (o, a, b) => (a.x - o.x) * (b.r - o.r) - (a.r - o.r) * (b.x - o.x);
        let bad = 0;
        for (let i = 0; i < n; i++) for (let j = i + 2; j < n; j++) {
            if (i === 0 && j === n - 1) continue;
            const [a, b] = seg(i), [c, d] = seg(j);
            const d1 = cross(a, b, c), d2 = cross(a, b, d), d3 = cross(c, d, a), d4 = cross(c, d, b);
            if (d1 * d2 < 0 && d3 * d4 < 0) bad++;
        }
        assert.equal(bad, 0, `self-intersections at ${JSON.stringify(b)}`);
    }
});

test('mesh: smooth tyre is pure closed quad torus', () => {
    for (const preset of ['low', 'medium']) {
        const m = buildQuadTyre({ budget: preset });
        const r = checkQuadMesh(m.positions, m.quads);
        assert.equal(r.ok, true, JSON.stringify(r));
        assert.equal(r.triangles, 0);
        assert.equal(r.boundaryEdges, 0);
        assert.equal(r.eulerCharacteristic, 0);
        assert.ok(signedVolume(m.positions, m.quads) > 0, 'faces must point outward');
    }
});

test('mesh: tread pattern keeps the mesh a clean closed quad torus', () => {
    const pattern = PRESET_PATTERNS[0].pattern;
    const m = buildQuadTyre({ pattern, budget: 'low' });
    const r = checkQuadMesh(m.positions, m.quads);
    assert.equal(r.ok, true, JSON.stringify(r));
    assert.equal(r.nonManifoldEdges, 0);
    assert.equal(r.degenerateFaces, 0);
});

test('mesh: groove walls land on mesh edges and floors are at full depth', () => {
    // Single 8 mm circumferential groove on the centre line.
    const groove = createPattern({ pitchMm: 200, shapes: [
        { kind: 'stroke', pts: [[0, 0], [200, 0]], closed: true, width: 8, depth: 1 },
    ] });
    const m = buildQuadTyre({ pattern: groove, budget: 'low' });
    // Grid lines exist at the groove edges (y = +/-4 mm).
    assert.ok(m.treadLines.some((y) => Math.abs(y - 4) < 1e-6), 'grid line at +4 mm');
    const L = m.loopLength;
    const R = TYRE_DEFAULTS.treadDepth;
    // Undisplaced loop for the same grid lines, so each vertex is compared with its own rest radius.
    const rest = buildCrossSectionLoop(TYRE_DEFAULTS, BUDGET.low, m.treadLines.map((y) => y / 1000));
    const NA = m.aroundCount;
    let floorOk = 0, wallOk = 0, landOk = 0, bad = 0;
    for (let i = 0; i < NA; i++) for (let j = 0; j < L; j++) {
        const v = i * L + j;
        const p = rest[j];
        if (!p.outer || p.w === 0) continue;               // inner liner and bead are not displaced
        const r = Math.hypot(m.positions[3 * v + 1], m.positions[3 * v + 2]);
        const dr = p.r - r;
        const yMm = p.x * 1000;                            // across position of this vertex
        if (Math.abs(Math.abs(yMm) - 4) < 1e-6) { if (Math.abs(dr - R / 2) < 1e-7) wallOk++; else bad++; }
        else if (Math.abs(yMm) < 4 - 1e-6) { if (Math.abs(dr - R) < 1e-7) floorOk++; else bad++; }
        else if (Math.abs(yMm) > 4 + 1e-6) { if (Math.abs(dr) < 1e-7) landOk++; else bad++; }
    }
    assert.ok(floorOk > 0 && landOk > 0 && wallOk > 0, `counts floor=${floorOk} land=${landOk} wall=${wallOk}`);
    assert.equal(bad, 0, 'depth must be full in the groove, half on the wall edge, zero on the land');
});

test('mesh: budget holds — low pattern and smooth tyre stay low-poly', () => {
    const low = buildQuadTyre({ pattern: PRESET_PATTERNS[0].pattern, budget: 'low' });
    const smooth = buildQuadTyre({ budget: 'low' });
    assert.ok(low.quadCount <= 5000, `pattern quads ${low.quadCount}`);
    assert.ok(smooth.quadCount <= 500, `smooth quads ${smooth.quadCount}`);
});

test('mesh: low budget keeps the tyre volume within 5 % of the fine reference', () => {
    const ref = buildQuadTyre({ budget: 'fine' });
    const low = buildQuadTyre({ budget: 'low' });
    const err = Math.abs(signedVolume(low.positions, low.quads) / signedVolume(ref.positions, ref.quads) - 1);
    assert.ok(err < 0.05, `volume error ${(100 * err).toFixed(2)} %`);
});

test('pattern breakpoints include groove edges and skip straight loop ends', () => {
    const bp = patternBreakpoints(createPattern({ pitchMm: 240, shapes: [
        { kind: 'stroke', pts: [[0, -34], [240, -34]], closed: true, width: 8, depth: 1 },
    ] }));
    assert.ok(bp.ys.some((y) => Math.abs(y + 30) < 1e-9) && bp.ys.some((y) => Math.abs(y + 38) < 1e-9), 'edges at -30 and -38');
    assert.ok(!bp.ys.some((y) => Math.abs(y + 34) < 1e-9), 'no centre line');
    assert.equal(bp.xs.length, 0, 'straight closed loop adds no around lines');
});

test('mesh: OBJ export has only quad faces with valid indices', () => {
    const m = buildQuadTyre({ budget: 'low' });
    const text = quadMeshToObj(m, { name: 'test' });
    const lines = text.split('\n');
    const faces = lines.filter((l) => l.startsWith('f '));
    assert.equal(faces.length, m.quadCount);
    assert.ok(faces.every((l) => l.split(/\s+/).length === 5), 'every face has 4 vertices');
    assert.equal(lines.filter((l) => l.startsWith('v ')).length, m.vertexCount);
    const max = Math.max(...faces.flatMap((l) => l.split(/\s+/).slice(1).map(Number)));
    assert.equal(max, m.vertexCount);
});
