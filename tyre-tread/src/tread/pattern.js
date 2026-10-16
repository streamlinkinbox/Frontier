/**
 * Tread pattern model: a list of vector shapes in tread coordinates (mm).
 *
 *   x  circumferential position along the tread, wraps every `pitchMm`
 *   y  across position, 0 at the tread centre line, positive toward the right shoulder
 *
 * Shape kinds:
 *   stroke  open or closed polyline with a width (grooves, sipes, channels)
 *   fill    closed polygon, the whole interior is cut (voids, blocks in reverse)
 *   dimple  circle, the whole disc is cut
 *
 * Every shape carries a depth fraction in (0, 1]. The groove depth on the tyre is depth * treadDepth.
 * The pattern is evaluated analytically at mesh vertices; nothing is rasterised to a texture.
 */

export const SHAPE_KINDS = Object.freeze(['stroke', 'fill', 'dimple']);

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

function finite(v, name) {
    if (!Number.isFinite(v)) throw new Error(`pattern: ${name} must be a finite number`);
    return v;
}

/** Validate and copy one shape. Returns a fresh object so callers can mutate the copy freely. */
export function normaliseShape(s) {
    if (!SHAPE_KINDS.includes(s.kind)) throw new Error(`pattern: unknown shape kind "${s.kind}"`);
    const depth = clamp(finite(s.depth ?? 1, 'depth'), 0.01, 1);
    if (s.kind === 'dimple') {
        return { id: s.id ?? null, kind: 'dimple', c: [finite(s.c[0], 'c.x'), finite(s.c[1], 'c.y')], r: Math.max(0.2, finite(s.r, 'r')), depth };
    }
    const pts = s.pts.map(([x, y]) => [finite(x, 'x'), finite(y, 'y')]);
    if (s.kind === 'stroke') {
        if (pts.length < 2) throw new Error('pattern: a stroke needs at least 2 points');
        return { id: s.id ?? null, kind: 'stroke', pts, width: Math.max(0.2, finite(s.width, 'width')), closed: !!s.closed, depth };
    }
    if (pts.length < 3) throw new Error('pattern: a fill needs at least 3 points');
    return { id: s.id ?? null, kind: 'fill', pts, depth };
}

/** New pattern document. `mirror` makes every shape also appear mirrored about the centre line. */
export function createPattern({ name = 'Untitled', pitchMm = 240, mirror = false, shapes = [] } = {}) {
    return {
        format: 'tyre-tread-pattern/1',
        name: String(name),
        pitchMm: clamp(finite(pitchMm, 'pitchMm'), 20, 2000),
        mirror: !!mirror,
        shapes: shapes.map(normaliseShape),
    };
}

/** JSON-safe serialisation (round-trips through parsePattern). */
export function serialisePattern(p) {
    return JSON.stringify(createPattern(p), null, 2);
}

export function parsePattern(text) {
    const o = typeof text === 'string' ? JSON.parse(text) : text;
    if (o.format !== 'tyre-tread-pattern/1') throw new Error('pattern: unsupported format');
    return createPattern(o);
}

function distPointSegment(px, py, ax, ay, bx, by) {
    const dx = bx - ax, dy = by - ay;
    const l2 = dx * dx + dy * dy;
    let t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    t = clamp(t, 0, 1);
    const qx = ax + t * dx - px, qy = ay + t * dy - py;
    return Math.hypot(qx, qy);
}

function insideRing(px, py, pts) {
    let inside = false;
    for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
        const [xi, yi] = pts[i], [xj, yj] = pts[j];
        if ((yi > py) !== (yj > py) && px < ((xj - xi) * (py - yi)) / (yj - yi) + xi) inside = !inside;
    }
    return inside;
}

/** Does one (un-mirrored) shape cover the point? Exported for editor hit-testing. */
export function shapeCovers(shape, px, py) {
    if (shape.kind === 'dimple') return Math.hypot(px - shape.c[0], py - shape.c[1]) <= shape.r;
    if (shape.kind === 'fill') return insideRing(px, py, shape.pts);
    const pts = shape.pts;
    const n = shape.closed ? pts.length : pts.length - 1;
    const half = shape.width / 2;
    for (let i = 0; i < n; i++) {
        const a = pts[i], b = pts[(i + 1) % pts.length];
        if (distPointSegment(px, py, a[0], a[1], b[0], b[1]) <= half) return true;
    }
    return false;
}

/**
 * Depth fraction of the tread at (xMm, yMm): the deepest shape covering the point, 0 when none does.
 * x wraps with the pattern pitch, so the pattern tiles seamlessly around the tyre.
 */
export function depthAt(pattern, xMm, yMm) {
    const pitch = pattern.pitchMm;
    const xw = ((xMm % pitch) + pitch) % pitch;
    let best = 0;
    for (const s of pattern.shapes) {
        if (s.depth <= best) continue;
        for (const k of [-1, 0, 1]) {
            if (shapeCovers(s, xw + k * pitch, yMm) || (pattern.mirror && shapeCovers(s, xw + k * pitch, -yMm))) {
                best = s.depth;
                break;
            }
        }
    }
    return best;
}

/** Number of whole pattern repeats that fit the tyre circumference, and the pitch that makes them fit exactly. */
export function fitPitch(pattern, circumferenceMm) {
    const repeats = Math.max(1, Math.round(circumferenceMm / pattern.pitchMm));
    return { repeats, pitchMm: circumferenceMm / repeats };
}
