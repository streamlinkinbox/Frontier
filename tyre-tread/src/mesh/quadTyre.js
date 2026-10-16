/**
 * Low-poly, pure-quad tyre mesh with the tread pattern carried by the topology.
 *
 * Cross-section: ONE closed loop in (axial x, radial r), swept around the axis:
 *   tread centre -> right shoulder -> right sidewall -> right bead ->
 *   right inner liner -> inner centre -> left inner liner -> left bead ->
 *   left sidewall -> left shoulder -> tread centre
 * Every face is a quad (i, j) -> (i+1, j) -> (i+1, j+1) -> (i, j+1), so there are no triangles,
 * no open beads and no Steiner points.
 *
 * Pattern: the grid lines are placed on the pattern's own breakpoints (groove edges, sipe edges,
 * vertices; see patternBreakpoints). Groove walls therefore run along mesh edges, and quads are
 * spent only where the pattern needs them. A uniform base grid covers the rest.
 *
 * Depth: each quad gets the pattern depth at its centre (weighted toward the shoulder). A vertex
 * takes the mean of its four neighbouring quads, so a groove wall becomes a one-quad slope and the
 * groove floor is at full depth. Depth is applied as vertex displacement only. There is no
 * heightmap and no texture.
 *
 * Units: metres for the mesh, mm inside the pattern. Axis is +x. Radial is r; the loop is swept
 * around x.
 */

import { depthAt, fitPitch, patternBreakpoints } from '../tread/pattern.js';

/** Default geometry: 205/55 R16 class. All values in metres. */
export const TYRE_DEFAULTS = Object.freeze({
    rOut: 0.316,          // crown outer radius
    rRim: 0.2032,         // bead seat radius (16 in rim)
    halfWidth: 0.1025,    // max section half width (sidewall bulge)
    treadHalf: 0.065,     // tread half width (flat crown)
    crownDrop: 0.0015,    // crown drop from centre to shoulder
    shoulderR: 0.030,     // shoulder radius
    beadHalf: 0.0715,     // bead heel half width
    treadDepth: 0.008,    // max groove depth
    thickTread: 0.014,    // rubber + belt thickness under the tread
    thickSide: 0.006,     // sidewall thickness
});

/**
 * Budgets. Counts are base nodes; breakpoints add more. Quads = around x loop segments.
 * `low` is the default. `medium` is for checking whether the pattern needs more detail.
 */
export const BUDGET = Object.freeze({
    // low: ~2.9 % smooth-tyre volume error vs the fine reference (see tests). Coarser inner-liner
    // strides and fewer sidewall samples were measured to be far worse (218 % at stride 6).
    low: { aroundBase: 12, treadBase: 3, shoulder: 3, side: 5, innerStride: 2, minGapMm: 1.0 },
    medium: { aroundBase: 24, treadBase: 4, shoulder: 4, side: 6, innerStride: 2, minGapMm: 0.75 },
    // Reference only, for tests. Not for the editor.
    fine: { aroundBase: 96, treadBase: 16, shoulder: 8, side: 16, innerStride: 1, minGapMm: 0.5 },
});

function cubic(p0, p1, p2, p3, t) {
    const u = 1 - t;
    const a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    return [
        a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
        a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1],
    ];
}

/**
 * Outer right-hand profile from the tread centre (index 0) to the bead (last index).
 * `treadNodes` are axial positions (m) in [0, treadHalf). Each sample carries the tread weight w
 * in [0,1] used to scale groove depth (1 across the tread, falling to 0 at the sidewall).
 */
export function outerRightProfile(g, { treadNodes, shoulder, side }) {
    const pts = [];
    const { treadHalf, rOut, crownDrop, shoulderR, halfWidth, beadHalf, rRim } = g;
    for (const x of treadNodes) {
        const r = rOut - crownDrop * (x / treadHalf) ** 2;
        pts.push({ x, r, w: 1 });
    }
    // Shoulder arc, centre (treadHalf, rOut - crownDrop - shoulderR), from 0 to 80 degrees.
    const cx = treadHalf, cr = rOut - crownDrop - shoulderR;
    const phiMax = (80 * Math.PI) / 180;
    for (let i = 0; i < shoulder; i++) {
        const phi = (i / shoulder) * phiMax;
        const x = cx + shoulderR * Math.sin(phi);
        const r = cr + shoulderR * Math.cos(phi);
        const w = Math.max(0, (Math.cos(phi) - Math.cos(phiMax)) / (1 - Math.cos(phiMax)));
        pts.push({ x, r, w });
    }
    // Sidewall: cubic Bezier from shoulder end to bead heel.
    const p0 = [cx + shoulderR * Math.sin(phiMax), cr + shoulderR * Math.cos(phiMax)];
    const H = rOut - rRim;
    const p1 = [p0[0] + (halfWidth - p0[0]) * 1.35 + 0.004, p0[1] - H * 0.22];
    const p2 = [beadHalf + (halfWidth - beadHalf) * 0.55, rRim + H * 0.2];
    const p3 = [beadHalf, rRim];
    for (let i = 1; i <= side; i++) {
        const [x, r] = cubic(p0, p1, p2, p3, i / side);
        pts.push({ x, r, w: 0 });
    }
    return pts;
}

/** Inward unit normal at each profile sample. (tr, -tx) is the cavity-side normal everywhere on the section. */
function inwardNormals(prof) {
    const n = prof.length;
    const out = [];
    for (let i = 0; i < n; i++) {
        const a = prof[Math.max(0, i - 1)], b = prof[Math.min(n - 1, i + 1)];
        let tx = b.x - a.x, tr = b.r - a.r;
        const l = Math.hypot(tx, tr) || 1;
        tx /= l; tr /= l;
        out.push([tr, -tx]);
    }
    return out;
}

/**
 * Full closed loop, in order. Samples: {x, r, w, outer, side}.
 * Right outer runs centre -> bead, right inner runs bead -> centre, left inner runs centre -> bead,
 * left outer runs bead -> centre. The inner liner is sampled every `innerStride` profile nodes.
 */
export function buildCrossSectionLoop(g = TYRE_DEFAULTS, B = BUDGET.low, treadNodes = null) {
    const nodes = treadNodes ?? uniformTreadNodes(g, B.treadBase);
    const R = outerRightProfile(g, { treadNodes: nodes, shoulder: B.shoulder, side: B.side });
    const N = inwardNormals(R);
    const n = R.length - 1; // index of bead
    const stride = B.innerStride ?? 1;
    const thick = (k) => g.thickSide + (g.thickTread - g.thickSide) * R[k].w;
    const innerAt = (k) => ({ x: R[k].x + thick(k) * N[k][0], r: R[k].r + thick(k) * N[k][1] });
    // Inner sample indices: every stride-th profile node, always including the centre (0) and the bead (n).
    const idx = [];
    for (let k = 0; k <= n; k++) if (k % stride === 0 || k === n) idx.push(k);
    const loop = [];
    for (let k = 0; k <= n; k++) loop.push({ x: R[k].x, r: R[k].r, w: R[k].w, outer: true, side: 1 });
    for (let m = idx.length - 1; m >= 0; m--) {
        const p = innerAt(idx[m]);
        loop.push({ x: p.x, r: p.r, w: 0, outer: false, side: 1 });
    }
    for (let m = 1; m < idx.length; m++) {
        const p = innerAt(idx[m]);
        loop.push({ x: -p.x, r: p.r, w: 0, outer: false, side: -1 });
    }
    for (let k = n; k >= 1; k--) loop.push({ x: -R[k].x, r: R[k].r, w: R[k].w, outer: true, side: -1 });
    return loop;
}

/** Sorted values on [lo, hi) with values closer than `gap` merged. Used for both rings and lines. */
function mergeNodes(values, gap, period = null) {
    let v = values.map((x) => (period ? ((x % period) + period) % period : x)).sort((a, b) => a - b);
    const out = [];
    for (const x of v) if (!out.length || x - out[out.length - 1] >= gap) out.push(x);
    if (period && out.length > 1 && period - out[out.length - 1] + out[0] < gap) out.pop();
    return out;
}

function uniformTreadNodes(g, count) {
    const out = [];
    for (let k = 0; k < count; k++) out.push((k / count) * g.treadHalf);
    return out;
}

/**
 * Tread grid lines (m, from the centre to the shoulder) for the given pattern.
 * Uniform base lines plus the pattern's across-breakpoints.
 */
function treadLines(g, B, bp) {
    const th = g.treadHalf * 1000;
    const vals = [];
    for (let k = 0; k < B.treadBase; k++) vals.push((k / B.treadBase) * th);
    for (const y of bp?.ys ?? []) {
        const a = Math.abs(y);
        if (a > 0.5 && a < th - 0.5) vals.push(a);
    }
    return mergeNodes([0, ...vals], B.minGapMm).filter((v) => v < th - 0.5).map((v) => v / 1000);
}

/**
 * Build the low-poly quad mesh.
 * @param {object} opts
 *   pattern   tread pattern (pattern.js). Optional; smooth tyre if omitted.
 *   geometry  overrides for TYRE_DEFAULTS
 *   budget    'low' (default) | 'medium' | a BUDGET-shaped object
 * @returns {{positions, quads, vertexCount, quadCount, pitch, loopLength, aroundCount, treadLines}}
 */
export function buildQuadTyre({ pattern = null, geometry = {}, budget = 'low' } = {}) {
    const g = { ...TYRE_DEFAULTS, ...geometry };
    const B = typeof budget === 'string' ? BUDGET[budget] : budget;
    if (!B) throw new Error(`quadTyre: unknown budget "${budget}"`);

    const Cmm = 2 * Math.PI * g.rOut * 1000;
    const fit = pattern ? fitPitch(pattern, Cmm) : null;
    const pat = pattern ? { ...pattern, pitchMm: fit.pitchMm } : null;
    const bp = pat ? patternBreakpoints(pat) : null;

    // Around: uniform arc-length base plus the pattern's x-breakpoints in every repeat.
    const arcNodes = [];
    for (let k = 0; k < B.aroundBase; k++) arcNodes.push((k / B.aroundBase) * Cmm);
    if (pat) for (let r = 0; r < fit.repeats; r++) for (const x of bp.xs) arcNodes.push(r * fit.pitchMm + x);
    const arc = mergeNodes(arcNodes, B.minGapMm, Cmm);     // mm of arc, sorted, ring-merged
    const NA = arc.length;
    const thetaOf = (s) => (2 * Math.PI * s) / Cmm;

    // Across: tread lines from the breakpoints; profile built from them.
    const tNodes = treadLines(g, B, bp);
    const loop = buildCrossSectionLoop(g, B, tNodes);
    const L = loop.length;

    // Quad depth: pattern at the quad centre times tread weight. Quad (i, j) spans around i..i+1, loop j..j+1.
    const quadDepth = new Float32Array(NA * L);
    if (pat) {
        for (let i = 0; i < NA; i++) {
            const s0 = arc[i], s1 = i + 1 < NA ? arc[i + 1] : arc[0] + Cmm;
            const sMid = (s0 + s1) / 2;
            for (let j = 0; j < L; j++) {
                const j1 = (j + 1) % L;
                if (!(loop[j].outer && loop[j1].outer)) continue;
                const yMid = ((loop[j].x + loop[j1].x) / 2) * 1000;
                const w = (loop[j].w + loop[j1].w) / 2;
                if (w <= 0) continue;
                quadDepth[i * L + j] = depthAt(pat, sMid, yMid) * w;
            }
        }
    }

    // Vertex depth: mean of the four quads around it (missing quads count as zero).
    const positions = new Float32Array(NA * L * 3);
    for (let i = 0; i < NA; i++) {
        const theta = thetaOf(arc[i]);
        const c = Math.cos(theta), s = Math.sin(theta);
        const im1 = (i - 1 + NA) % NA;
        for (let j = 0; j < L; j++) {
            const p = loop[j];
            let r = p.r;
            if (pat && p.outer) {
                const jm1 = (j - 1 + L) % L;
                const sum = quadDepth[im1 * L + jm1] + quadDepth[im1 * L + j] + quadDepth[i * L + jm1] + quadDepth[i * L + j];
                if (sum > 0) r -= (sum / 4) * g.treadDepth;
            }
            const v = (i * L + j) * 3;
            positions[v] = p.x;
            positions[v + 1] = r * c;
            positions[v + 2] = r * s;
        }
    }

    const quadCount = NA * L;
    const quads = new Uint32Array(quadCount * 4);
    let q = 0;
    for (let i = 0; i < NA; i++) {
        const i1 = (i + 1) % NA;
        for (let j = 0; j < L; j++) {
            const j1 = (j + 1) % L;
            const a = i * L + j, b = i1 * L + j, cc = i1 * L + j1, d = i * L + j1;
            // Outward winding (checked by tests: positive enclosed volume).
            quads[q++] = a; quads[q++] = b; quads[q++] = cc; quads[q++] = d;
        }
    }
    return {
        positions, quads,
        vertexCount: NA * L,
        quadCount,
        pitch: fit,
        loopLength: L,
        aroundCount: NA,
        treadLines: tNodes.map((x) => x * 1000),
    };
}
