/**
 * Structured pure-quad tyre mesh.
 *
 * The cross-section is ONE closed loop in the (axial x, radial r) plane:
 *
 *   tread centre -> right shoulder -> right sidewall -> right bead ->
 *   right inner liner -> inner centre -> left inner liner -> left bead ->
 *   left sidewall -> left shoulder -> tread centre
 *
 * Sweeping that loop around the axis gives a torus. Every face is a quad built from
 * (around index i, loop index j), so there are no triangles, no open beads, no Steiner points
 * and no T-junctions. The tread grooves are applied as vertex displacement from the vector pattern
 * (see pattern.js), evaluated per vertex. There is no heightmap and no texture.
 *
 * Units: metres for the mesh, mm inside the pattern.
 * Axis is +x (lateral). Radial direction in the cross-section is y/z, and the loop is swept around x.
 */

import { depthAt, fitPitch } from '../tread/pattern.js';

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

/** Presets: `around` = quads around the tyre, `across` = samples across the tread, `side` = samples per sidewall curve. */
export const RESOLUTION = Object.freeze({
    // innerStride: the inner liner is not displaced by the tread, so it is sampled more coarsely.
    draft: { around: 256, across: 16, shoulder: 4, side: 6, innerStride: 2 },
    standard: { around: 512, across: 32, shoulder: 8, side: 10, innerStride: 2 },
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
 * Each sample carries the tread weight w in [0,1] used to scale groove depth (1 across the tread, falling to 0 at the sidewall).
 */
export function outerRightProfile(g, res) {
    const pts = [];
    const { treadHalf, rOut, crownDrop, shoulderR, halfWidth, beadHalf, rRim } = g;
    // Tread: flat-ish crown, centre to shoulder.
    for (let i = 0; i < res.across; i++) {
        const t = i / res.across;
        const x = t * treadHalf;
        const r = rOut - crownDrop * (x / treadHalf) ** 2;
        pts.push({ x, r, w: 1 });
    }
    // Shoulder arc, centre (treadHalf, rOut - crownDrop - shoulderR), from 0 to 80 degrees.
    const cx = treadHalf, cr = rOut - crownDrop - shoulderR;
    const phiMax = (80 * Math.PI) / 180;
    for (let i = 0; i < res.shoulder; i++) {
        const phi = (i / res.shoulder) * phiMax;
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
    for (let i = 1; i <= res.side; i++) {
        const [x, r] = cubic(p0, p1, p2, p3, i / res.side);
        pts.push({ x, r, w: 0 });
    }
    return pts;
}

/** Inward unit normal at each profile sample, from neighbouring samples. */
function inwardNormals(prof) {
    const n = prof.length;
    const out = [];
    for (let i = 0; i < n; i++) {
        const a = prof[Math.max(0, i - 1)], b = prof[Math.min(n - 1, i + 1)];
        let tx = b.x - a.x, tr = b.r - a.r;
        const l = Math.hypot(tx, tr) || 1;
        tx /= l; tr /= l;
        // Travel runs centre -> bead on the outer surface. (tr, -tx) is the inward (cavity-side) normal everywhere on the section.
        out.push([tr, -tx]);
    }
    return out;
}

/**
 * Full closed loop, in order. Returns samples {x, r, w, outer, side:+1|-1|0} where side tells left/right/centre.
 * The loop has 4n+2 samples for n right-profile samples after the centre.
 */
export function buildCrossSectionLoop(g = TYRE_DEFAULTS, res = RESOLUTION.standard) {
    const R = outerRightProfile(g, res);
    const N = inwardNormals(R);
    const n = R.length - 1; // index of bead
    const stride = res.innerStride ?? 1;
    const thick = (k) => g.thickSide + (g.thickTread - g.thickSide) * R[k].w;
    const innerAt = (k) => ({ x: R[k].x + thick(k) * N[k][0], r: R[k].r + thick(k) * N[k][1] });
    // Inner sample indices: every `stride`-th profile sample, always including the centre (0) and the bead (n).
    const idx = [];
    for (let k = 0; k <= n; k++) if (k % stride === 0 || k === n) idx.push(k);
    const loop = [];
    // Right outer, centre to bead (indices 0..n).
    for (let k = 0; k <= n; k++) loop.push({ x: R[k].x, r: R[k].r, w: R[k].w, outer: true, side: 1 });
    // Right inner, bead back to centre.
    for (let m = idx.length - 1; m >= 0; m--) {
        const p = innerAt(idx[m]);
        loop.push({ x: p.x, r: p.r, w: 0, outer: false, side: 1 });
    }
    // Left inner, centre outward to bead (mirror; centre already placed above).
    for (let m = 1; m < idx.length; m++) {
        const p = innerAt(idx[m]);
        loop.push({ x: -p.x, r: p.r, w: 0, outer: false, side: -1 });
    }
    // Left outer, bead back to centre (mirror; centre is the loop start).
    for (let k = n; k >= 1; k--) loop.push({ x: -R[k].x, r: R[k].r, w: R[k].w, outer: true, side: -1 });
    return loop;
}

/**
 * Build the quad mesh.
 * @param {object} opts
 *   pattern   tread pattern (pattern.js). Optional; a smooth tyre if omitted.
 *   geometry  overrides for TYRE_DEFAULTS
 *   resolution  RESOLUTION preset name or object
 * @returns {{positions: Float32Array, quads: Uint32Array, vertexCount: number, quadCount: number, pitch: object, loopLength: number}}
 */
export function buildQuadTyre({ pattern = null, geometry = {}, resolution = 'standard' } = {}) {
    const g = { ...TYRE_DEFAULTS, ...geometry };
    const res = typeof resolution === 'string' ? RESOLUTION[resolution] : resolution;
    if (!res) throw new Error(`quadTyre: unknown resolution "${resolution}"`);
    const loop = buildCrossSectionLoop(g, res);
    const L = loop.length;
    const NA = res.around;

    const circumferenceMm = 2 * Math.PI * g.rOut * 1000;
    const fit = pattern ? fitPitch(pattern, circumferenceMm) : null;
    const pat = pattern ? { ...pattern, pitchMm: fit.pitchMm } : null;

    // Pre-compute per-sample displacement (mm -> m) for outer tread samples. The depth depends on theta, so it is per (i, j).
    const positions = new Float32Array(NA * L * 3);
    for (let i = 0; i < NA; i++) {
        const theta = (2 * Math.PI * i) / NA;
        const c = Math.cos(theta), s = Math.sin(theta);
        for (let j = 0; j < L; j++) {
            const p = loop[j];
            let x = p.x, r = p.r;
            if (pat && p.outer && p.w > 0) {
                // Arc length along the outer surface: theta * rOut, in mm.
                const xMm = theta * g.rOut * 1000;
                const yMm = x * 1000; // signed across position (negative on the left half)
                const d = depthAt(pat, xMm, yMm);
                if (d > 0) {
                    // Inward along the outer normal: approximate with the radial direction at this sample.
                    const off = d * g.treadDepth * p.w;
                    r -= off;
                }
            }
            const v = (i * L + j) * 3;
            positions[v] = x;
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
            // Winding chosen so the outer face normal points away from the axis (checked in tests).
            // Outward winding (negative-volume orientation fixed here).
            quads[q++] = a; quads[q++] = b; quads[q++] = cc; quads[q++] = d;
        }
    }
    return {
        positions, quads,
        vertexCount: NA * L,
        quadCount,
        pitch: fit,
        loopLength: L,
    };
}
