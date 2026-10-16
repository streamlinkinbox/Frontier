/**
 * 2D tread editor. Vector shapes in mm, tile of one pattern pitch, ghost tiles on both sides.
 * Tools: select (V), groove polyline (G), fill polygon (F), dimple (D).
 * Right panel: properties, mesh stats, and a 3D preview (three.js, loaded lazily).
 */
import { createPattern, depthAt, shapeCovers, serialisePattern, parsePattern, fitPitch } from '../tread/pattern.js';
import { PRESET_PATTERNS } from '../tread/presets.js';
import { buildQuadTyre, TYRE_DEFAULTS, RESOLUTION } from '../mesh/quadTyre.js';
import { checkQuadMesh, signedVolume } from '../mesh/quadCheck.js';
import { quadMeshToObj } from '../io/obj.js';

const $ = (id) => document.getElementById(id);
const canvas = $('tread');
const ctx = canvas.getContext('2d');
const TREAD_HALF_MM = TYRE_DEFAULTS.treadHalf * 1000;
const HANDLE_PX = 7;

const clone = (o) => JSON.parse(JSON.stringify(o));
const mod = (a, m) => ((a % m) + m) % m;

const state = {
    pattern: clone(PRESET_PATTERNS[0].pattern),
    tool: 'select',
    sel: -1,            // selected shape index
    draft: null,        // in-progress polyline/polygon points [[x,y]...]
    dimple: null,       // in-progress dimple {c, r}
    drag: null,         // active pointer drag
    hover: null,        // hovered mm point
    undo: [],
    redo: [],
    view: { scale: 3, cx: 120, cy: 0 }, // px per mm, centre in mm
    snap: 2,
};

/* ---------- coordinates ---------- */

function toMm(ev) {
    const r = canvas.getBoundingClientRect();
    const sx = ev.clientX - r.left, sy = ev.clientY - r.top;
    return {
        x: (sx - canvas.clientWidth / 2) / state.view.scale + state.view.cx,
        y: (sy - canvas.clientHeight / 2) / state.view.scale + state.view.cy,
        sx, sy,
    };
}
function snapv(v) { return state.snap > 0 ? Math.round(v / state.snap) * state.snap : v; }
function sp(x, y, k = 0, mirror = false) {
    const X = (x + k * state.pattern.pitchMm - state.view.cx) * state.view.scale + canvas.clientWidth / 2;
    const Y = ((mirror ? -y : y) - state.view.cy) * state.view.scale + canvas.clientHeight / 2;
    return [X, Y];
}

/* ---------- undo ---------- */

function commit(mutate) {
    state.undo.push(JSON.stringify(state.pattern));
    if (state.undo.length > 200) state.undo.shift();
    state.redo = [];
    mutate();
    changed();
}
function undo() {
    if (!state.undo.length) return;
    state.redo.push(JSON.stringify(state.pattern));
    state.pattern = JSON.parse(state.undo.pop());
    state.sel = -1;
    changed();
}
function redo() {
    if (!state.redo.length) return;
    state.undo.push(JSON.stringify(state.pattern));
    state.pattern = JSON.parse(state.redo.pop());
    state.sel = -1;
    changed();
}

/* ---------- hit testing ---------- */

function hitShape(mx, my) {
    const xm = mod(mx, state.pattern.pitchMm);
    for (let i = state.pattern.shapes.length - 1; i >= 0; i--) {
        const s = state.pattern.shapes[i];
        for (const k of [-1, 0, 1]) {
            if (shapeCovers(s, xm + k * state.pattern.pitchMm, my)) return i;
            if (state.pattern.mirror && shapeCovers(s, xm + k * state.pattern.pitchMm, -my)) return i;
        }
    }
    return -1;
}

/** Handle under the pointer for the selected shape: {type:'pt', i} | {type:'c'} | {type:'r'} | null. */
function hitHandle(sx, sy) {
    const s = state.pattern.shapes[state.sel];
    if (!s) return null;
    const near = (x, y) => Math.hypot(x - sx, y - sy) <= HANDLE_PX;
    if (s.kind === 'dimple') {
        const [cx, cy] = sp(s.c[0], s.c[1]);
        if (near(cx, cy)) return { type: 'c' };
        const [rx, ry] = sp(s.c[0] + s.r, s.c[1]);
        if (near(rx, ry)) return { type: 'r' };
        return null;
    }
    for (let i = 0; i < s.pts.length; i++) {
        const [px, py] = sp(s.pts[i][0], s.pts[i][1]);
        if (near(px, py)) return { type: 'pt', i };
    }
    return null;
}

/* ---------- drawing ---------- */

function depthColour(d, alpha = 1) {
    // Shallow = cyan, deep = orange.
    const hue = 190 - 150 * d;
    return `hsla(${hue}, 80%, 58%, ${alpha})`;
}

function strokeShape(s, k, mirror, alpha, selected) {
    const width = (s.width ?? 0) * state.view.scale;
    ctx.beginPath();
    if (s.kind === 'dimple') {
        const [cx, cy] = sp(s.c[0], s.c[1], k, mirror);
        ctx.arc(cx, cy, s.r * state.view.scale, 0, Math.PI * 2);
        ctx.fillStyle = depthColour(s.depth, alpha);
        ctx.fill();
        if (selected) { ctx.lineWidth = 2; ctx.strokeStyle = '#fff'; ctx.stroke(); }
        return;
    }
    const pts = s.pts.map(([x, y]) => sp(x, y, k, mirror));
    ctx.moveTo(pts[0][0], pts[0][1]);
    for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i][0], pts[i][1]);
    if (s.kind === 'fill' || s.closed) ctx.closePath();
    if (s.kind === 'fill') {
        ctx.fillStyle = depthColour(s.depth, alpha);
        ctx.fill();
        if (selected) { ctx.lineWidth = 2; ctx.strokeStyle = '#fff'; ctx.stroke(); }
        return;
    }
    ctx.lineWidth = Math.max(2, width);
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.strokeStyle = depthColour(s.depth, alpha);
    ctx.stroke();
    if (selected) {
        ctx.lineWidth = 1.5;
        ctx.strokeStyle = '#fff';
        ctx.setLineDash([4, 3]);
        ctx.stroke();
        ctx.setLineDash([]);
    }
}

function drawHandles(s) {
    ctx.fillStyle = '#fff';
    if (s.kind === 'dimple') {
        for (const [x, y] of [s.c, [s.c[0] + s.r, s.c[1]]]) {
            const [px, py] = sp(x, y);
            ctx.beginPath(); ctx.arc(px, py, 4, 0, Math.PI * 2); ctx.fill();
        }
        return;
    }
    for (const [x, y] of s.pts) {
        const [px, py] = sp(x, y);
        ctx.fillRect(px - 4, py - 4, 8, 8);
    }
}

function draw() {
    const W = canvas.clientWidth, H = canvas.clientHeight;
    const dpr = window.devicePixelRatio || 1;
    if (canvas.width !== Math.round(W * dpr) || canvas.height !== Math.round(H * dpr)) {
        canvas.width = Math.round(W * dpr);
        canvas.height = Math.round(H * dpr);
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = '#0b0f14';
    ctx.fillRect(0, 0, W, H);

    const p = state.pattern;
    // Grid every 10 mm.
    ctx.strokeStyle = '#1b2430';
    ctx.lineWidth = 1;
    const x0 = Math.floor(state.view.cx - W / 2 / state.view.scale), x1 = Math.ceil(state.view.cx + W / 2 / state.view.scale);
    const y0 = Math.floor(state.view.cy - H / 2 / state.view.scale), y1 = Math.ceil(state.view.cy + H / 2 / state.view.scale);
    ctx.beginPath();
    for (let x = Math.floor(x0 / 10) * 10; x <= x1; x += 10) { const [X] = sp(x, 0); ctx.moveTo(X + 0.5, 0); ctx.lineTo(X + 0.5, H); }
    for (let y = Math.floor(y0 / 10) * 10; y <= y1; y += 10) { const [, Y] = sp(0, y); ctx.moveTo(0, Y + 0.5); ctx.lineTo(W, Y + 0.5); }
    ctx.stroke();

    // Ghost tiles and tile edges.
    for (const k of [-1, 0, 1]) {
        const alpha = k === 0 ? 1 : 0.28;
        for (const mir of p.mirror ? [false, true] : [false]) {
            p.shapes.forEach((s, i) => {
                strokeShape(s, k, mir, alpha, k === 0 && !mir && i === state.sel);
            });
        }
        const [Xa] = sp(0, 0, k), [Xb] = sp(p.pitchMm, 0, k);
        ctx.strokeStyle = k === 0 ? '#3a6f8f' : '#24404f';
        ctx.setLineDash([6, 4]);
        ctx.beginPath(); ctx.moveTo(Xa, 0); ctx.lineTo(Xa, H); ctx.moveTo(Xb, 0); ctx.lineTo(Xb, H); ctx.stroke();
        ctx.setLineDash([]);
    }
    // Tread width limits.
    ctx.strokeStyle = '#d7a64a';
    ctx.setLineDash([2, 4]);
    for (const y of [-TREAD_HALF_MM, TREAD_HALF_MM]) {
        const [, Y] = sp(0, y);
        ctx.beginPath(); ctx.moveTo(0, Y); ctx.lineTo(W, Y); ctx.stroke();
    }
    ctx.setLineDash([]);
    // Centre line.
    const [, Yc] = sp(0, 0);
    ctx.strokeStyle = '#2c3a48';
    ctx.beginPath(); ctx.moveTo(0, Yc); ctx.lineTo(W, Yc); ctx.stroke();

    // Selected handles.
    const sel = p.shapes[state.sel];
    if (sel && state.tool === 'select') drawHandles(sel);

    // In-progress shapes.
    if (state.draft && state.draft.length) {
        ctx.beginPath();
        state.draft.forEach(([x, y], i) => { const [X, Y] = sp(x, y); i ? ctx.lineTo(X, Y) : ctx.moveTo(X, Y); });
        if (state.hover) { const [X, Y] = sp(state.hover.x, state.hover.y); ctx.lineTo(X, Y); }
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 1.5;
        ctx.setLineDash([4, 3]);
        ctx.stroke();
        ctx.setLineDash([]);
        for (const [x, y] of state.draft) { const [X, Y] = sp(x, y); ctx.fillStyle = '#fff'; ctx.fillRect(X - 3, Y - 3, 6, 6); }
    }
    if (state.dimple) {
        const [cx, cy] = sp(state.dimple.c[0], state.dimple.c[1]);
        ctx.beginPath(); ctx.arc(cx, cy, state.dimple.r * state.view.scale, 0, Math.PI * 2);
        ctx.strokeStyle = '#fff'; ctx.setLineDash([4, 3]); ctx.stroke(); ctx.setLineDash([]);
    }
    // Snap cursor.
    if (state.hover) {
        const [X, Y] = sp(snapv(state.hover.x), snapv(state.hover.y));
        ctx.strokeStyle = '#7fd1ff';
        ctx.beginPath(); ctx.moveTo(X - 5, Y); ctx.lineTo(X + 5, Y); ctx.moveTo(X, Y - 5); ctx.lineTo(X, Y + 5); ctx.stroke();
    }
    // Scale bar and readout.
    ctx.fillStyle = '#8fa3b8';
    ctx.font = '12px system-ui, sans-serif';
    ctx.fillText(`pitch ${p.pitchMm.toFixed(1)} mm | tread width ${(2 * TREAD_HALF_MM).toFixed(0)} mm | snap ${state.snap} mm`, 10, H - 10);
    if (state.hover) ctx.fillText(`x ${state.hover.x.toFixed(1)}  y ${state.hover.y.toFixed(1)} mm`, W - 180, H - 10);
    updateToolHint();
}

/* ---------- pointer ---------- */

canvas.addEventListener('mousemove', (ev) => {
    const m = toMm(ev);
    state.hover = m;
    if (state.drag) moveDrag(m, ev);
    draw();
});
canvas.addEventListener('mouseleave', () => { state.hover = null; draw(); });

canvas.addEventListener('mousedown', (ev) => {
    if (ev.button === 1 || ev.altKey) {
        state.drag = { kind: 'pan', sx: ev.clientX, sy: ev.clientY, cx: state.view.cx, cy: state.view.cy };
        return;
    }
    if (ev.button !== 0) return;
    const m = toMm(ev);
    const x = snapv(m.x), y = snapv(m.y);
    if (state.tool === 'select') {
        const h = hitHandle(m.sx, m.sy);
        if (h) {
            state.undo.push(JSON.stringify(state.pattern)); state.redo = [];
            state.drag = { kind: 'handle', h, shape: state.sel, moved: false };
            return;
        }
        const i = hitShape(m.x, m.y);
        state.sel = i;
        if (i >= 0) {
            state.undo.push(JSON.stringify(state.pattern)); state.redo = [];
            state.drag = { kind: 'body', start: { x: m.x, y: m.y }, orig: clone(state.pattern.shapes[i]), shape: i };
        }
        updateProps();
        draw();
    } else if (state.tool === 'dimple') {
        state.dimple = { c: [x, y], r: 0 };
        state.drag = { kind: 'dimple' };
    } else if (state.tool === 'groove' || state.tool === 'fill') {
        if (!state.draft) state.draft = [];
        // Ignore the second click of a double-click; dblclick finishes.
        if (ev.detail >= 2) return;
        state.draft.push([x, y]);
    }
    draw();
});

canvas.addEventListener('dblclick', () => {
    if (state.tool === 'groove' || state.tool === 'fill') finishDraft();
});

window.addEventListener('mouseup', () => {
    const d = state.drag;
    state.drag = null;
    if (!d) return;
    if (d.kind === 'dimple' && state.dimple) {
        if (state.dimple.r >= 1) {
            commit(() => state.pattern.shapes.push({ kind: 'dimple', c: state.dimple.c, r: state.dimple.r, depth: depthValue() }));
            state.sel = state.pattern.shapes.length - 1;
        }
        state.dimple = null;
        updateProps();
    }
    if (d.kind === 'handle' || d.kind === 'body') {
        if (!d.moved) { state.undo.pop(); } else changed();
    }
    draw();
});

function moveDrag(m, ev) {
    const d = state.drag;
    if (d.kind === 'pan') {
        state.view.cx = d.cx - (ev.clientX - d.sx) / state.view.scale;
        state.view.cy = d.cy - (ev.clientY - d.sy) / state.view.scale;
        return;
    }
    if (d.kind === 'dimple' && state.dimple) {
        const cx = state.dimple.c[0], cy = state.dimple.c[1];
        state.dimple.r = Math.max(0, Math.hypot(snapv(m.x) - cx, snapv(m.y) - cy));
        return;
    }
    const s = state.pattern.shapes[d.shape];
    if (!s) return;
    if (d.kind === 'handle') {
        const x = snapv(m.x), y = snapv(m.y);
        if (d.h.type === 'pt') s.pts[d.h.i] = [x, y];
        else if (d.h.type === 'c') s.c = [x, y];
        else if (d.h.type === 'r') s.r = Math.max(0.5, Math.hypot(x - s.c[0], y - s.c[1]) || 0.5);
        d.moved = true;
    } else if (d.kind === 'body') {
        const dx = snapv(m.x - d.start.x), dy = snapv(m.y - d.start.y);
        if (dx === 0 && dy === 0) return;
        if (s.kind === 'dimple') s.c = [d.orig.c[0] + dx, d.orig.c[1] + dy];
        else s.pts = d.orig.pts.map(([x, y]) => [x + dx, y + dy]);
        d.moved = true;
    }
    scheduleMesh();
    updateProps();
}

function finishDraft() {
    if (!state.draft) return;
    let pts = state.draft.filter((p, i, a) => i === 0 || p[0] !== a[i - 1][0] || p[1] !== a[i - 1][1]);
    const isFill = state.tool === 'fill';
    const min = isFill ? 3 : 2;
    if (pts.length >= min) {
        const closed = isFill || $('closed').checked;
        commit(() => {
            state.pattern.shapes.push(isFill
                ? { kind: 'fill', pts, depth: depthValue() }
                : { kind: 'stroke', pts, width: Math.max(0.5, Number($('width').value) || 8), closed, depth: depthValue() });
        });
        state.sel = state.pattern.shapes.length - 1;
    }
    state.draft = null;
    updateProps();
    draw();
}

/* ---------- keyboard ---------- */

window.addEventListener('keydown', (ev) => {
    if (ev.target.tagName === 'INPUT' || ev.target.tagName === 'SELECT' || ev.target.tagName === 'TEXTAREA') return;
    const k = ev.key.toLowerCase();
    if ((ev.ctrlKey || ev.metaKey) && k === 'z' && !ev.shiftKey) { undo(); ev.preventDefault(); return; }
    if ((ev.ctrlKey || ev.metaKey) && (k === 'y' || (k === 'z' && ev.shiftKey))) { redo(); ev.preventDefault(); return; }
    if (k === 'v') setTool('select');
    else if (k === 'g') setTool('groove');
    else if (k === 'f') setTool('fill');
    else if (k === 'd') setTool('dimple');
    else if (k === 'enter') finishDraft();
    else if (k === 'escape') { state.draft = null; state.dimple = null; draw(); }
    else if (k === 'delete' || k === 'backspace') deleteSelected();
    else if (k === ' ') { ev.preventDefault(); }
});

function deleteSelected() {
    if (state.sel < 0) return;
    commit(() => state.pattern.shapes.splice(state.sel, 1));
    state.sel = -1;
    updateProps();
}

/* ---------- toolbar and panel ---------- */

function setTool(t) {
    state.tool = t;
    state.draft = null;
    state.dimple = null;
    for (const b of document.querySelectorAll('[data-tool]')) b.classList.toggle('on', b.dataset.tool === t);
    draw();
}
function updateToolHint() {
    const hints = {
        select: 'Click a shape to select. Drag a handle or the shape to edit. Alt-drag pans.',
        groove: 'Click to add points. Double-click or Enter to finish. Esc cancels.',
        fill: 'Click to add corners. Double-click or Enter to close the shape.',
        dimple: 'Drag from the centre to set the radius.',
    };
    $('hint').textContent = hints[state.tool];
}
function depthValue() { return Math.min(1, Math.max(0.01, Number($('depth').value) / 100)); }

function updateProps() {
    const s = state.pattern.shapes[state.sel];
    $('selpanel').hidden = !s;
    if (s) {
        $('seldepth').value = Math.round(s.depth * 100);
        $('seldepthOut').textContent = `${Math.round(s.depth * 100)} %`;
        $('selwidth').disabled = s.kind !== 'stroke';
        $('selwidth').value = s.kind === 'stroke' ? s.width : '';
        $('selclosed').checked = s.kind === 'stroke' ? s.closed : true;
        $('selclosed').disabled = s.kind !== 'stroke';
        $('selinfo').textContent = `${s.kind} #${state.sel + 1}, ${s.kind === 'dimple' ? '1 centre' : s.pts ? s.pts.length + ' points' : ''}`;
    }
    $('pitch').value = state.pattern.pitchMm;
    $('mirror').checked = state.pattern.mirror;
    $('count').textContent = `${state.pattern.shapes.length} shapes`;
    $('name').value = state.pattern.name;
}

function changed() {
    if (state.sel >= state.pattern.shapes.length) state.sel = -1;
    updateProps();
    draw();
    scheduleMesh();
}

for (const b of document.querySelectorAll('[data-tool]')) b.addEventListener('click', () => setTool(b.dataset.tool));
$('undo').addEventListener('click', undo);
$('redo').addEventListener('click', redo);
$('del').addEventListener('click', deleteSelected);
$('name').addEventListener('change', (e) => commit(() => { state.pattern.name = e.target.value; }));
$('pitch').addEventListener('change', (e) => commit(() => { state.pattern.pitchMm = Math.min(2000, Math.max(20, Number(e.target.value) || 240)); }));
$('mirror').addEventListener('change', (e) => commit(() => { state.pattern.mirror = e.target.checked; }));
$('snap').addEventListener('change', (e) => { state.snap = Number(e.target.value); draw(); });
$('seldepth').addEventListener('input', (e) => {
    const s = state.pattern.shapes[state.sel];
    if (!s) return;
    $('seldepthOut').textContent = `${e.target.value} %`;
    s.depth = Math.max(0.01, Number(e.target.value) / 100);
    scheduleMesh();
    draw();
});
// Snapshot before the slider moves so one drag is one undo step.
$('seldepth').addEventListener('pointerdown', () => { state.sliderSnap = JSON.stringify(state.pattern); });
$('seldepth').addEventListener('change', () => {
    if (state.sliderSnap) { state.undo.push(state.sliderSnap); state.redo = []; state.sliderSnap = null; }
    changed();
});
$('selwidth').addEventListener('change', (e) => commit(() => {
    const s = state.pattern.shapes[state.sel];
    if (s && s.kind === 'stroke') s.width = Math.max(0.5, Number(e.target.value) || s.width);
}));
$('selclosed').addEventListener('change', (e) => commit(() => {
    const s = state.pattern.shapes[state.sel];
    if (s && s.kind === 'stroke') s.closed = e.target.checked;
}));
$('selDelete').addEventListener('click', deleteSelected);
$('presets').addEventListener('change', (e) => {
    const i = Number(e.target.value);
    if (Number.isNaN(i)) return;
    commit(() => { state.pattern = clone(PRESET_PATTERNS[i].pattern); });
    state.sel = -1;
    e.target.value = '';
    updateProps();
});

$('save').addEventListener('click', () => download(`${slug(state.pattern.name)}.tread.json`, serialisePattern(state.pattern), 'application/json'));
$('load').addEventListener('change', async (e) => {
    const f = e.target.files[0];
    if (!f) return;
    try {
        const p = parsePattern(await f.text());
        commit(() => { state.pattern = p; });
        state.sel = -1;
        updateProps();
    } catch (err) {
        alert(`Could not load pattern: ${err.message}`);
    }
    e.target.value = '';
});
$('exportObj').addEventListener('click', () => {
    const res = $('resolution').value;
    const mesh = buildQuadTyre({ pattern: state.pattern, resolution: res });
    download(`${slug(state.pattern.name)}-${res}.obj`, quadMeshToObj(mesh, { name: slug(state.pattern.name) }), 'text/plain');
});
$('resolution').addEventListener('change', () => scheduleMesh());
$('wire').addEventListener('change', (e) => { ensureView3d().then((v) => { if (v) v.setWireframe(e.target.checked); }); });
$('fit').addEventListener('click', () => { state.view.cx = state.pattern.pitchMm / 2; state.view.cy = 0; state.view.scale = 3; draw(); });

function slug(s) { return (s || 'tread').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'tread'; }
function download(name, text, type) {
    const url = URL.createObjectURL(new Blob([text], { type }));
    const a = Object.assign(document.createElement('a'), { href: url, download: name });
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}

canvas.addEventListener('wheel', (ev) => {
    ev.preventDefault();
    const m = toMm(ev);
    const f = Math.exp(-ev.deltaY * 0.001);
    state.view.scale = Math.min(30, Math.max(1, state.view.scale * f));
    // Keep the point under the cursor fixed.
    state.view.cx = m.x - (m.sx - canvas.clientWidth / 2) / state.view.scale;
    state.view.cy = m.y - (m.sy - canvas.clientHeight / 2) / state.view.scale;
    draw();
}, { passive: false });

/* ---------- mesh stats and 3D preview ---------- */

let meshTimer = null;
function scheduleMesh() {
    clearTimeout(meshTimer);
    meshTimer = setTimeout(rebuildMesh, 180);
}

let view3d = null;
let lastMesh = null;
async function ensureView3d() {
    if (view3d !== null) return view3d;
    try {
        const mod3 = await import('./view3d.js');
        view3d = mod3.createView3d($('view3d'));
    } catch (err) {
        view3d = false;
        $('view3dMsg').textContent = `3D preview unavailable (${err.message}). The 2D editor and OBJ export still work.`;
    }
    return view3d;
}

function rebuildMesh() {
    const res = $('resolution').value;
    const t0 = performance.now();
    const mesh = buildQuadTyre({ pattern: state.pattern, resolution: res });
    const report = checkQuadMesh(mesh.positions, mesh.quads);
    const vol = signedVolume(mesh.positions, mesh.quads);
    const ms = performance.now() - t0;
    lastMesh = mesh;
    const fit = mesh.pitch;
    $('stats').innerHTML = [
        ['Quads', mesh.quadCount.toLocaleString()],
        ['Vertices', mesh.vertexCount.toLocaleString()],
        ['Triangles', report.triangles],
        ['Boundary edges', report.boundaryEdges],
        ['Non-manifold edges', report.nonManifoldEdges],
        ['Euler V-E+F', report.eulerCharacteristic],
        ['Orientation conflicts', report.orientationConflicts],
        ['Pattern repeats around', `${fit.repeats} (pitch ${fit.pitchMm.toFixed(1)} mm)`],
        ['Enclosed volume', `${(vol * 1000).toFixed(2)} L`],
        ['Build time', `${ms.toFixed(0)} ms`],
    ].map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
    const badge = $('topo');
    badge.textContent = report.ok ? 'Clean quad torus: closed, manifold, quads only' : 'Topology problem, see stats';
    badge.className = report.ok ? 'ok' : 'bad';
    ensureView3d().then((v) => { if (v) v.setMesh(mesh); });
}

/* ---------- start ---------- */

for (const [i, p] of PRESET_PATTERNS.entries()) {
    const o = document.createElement('option');
    o.value = String(i);
    o.textContent = p.name;
    $('presets').appendChild(o);
}
$('resolution').innerHTML = Object.keys(RESOLUTION).map((k) => `<option value="${k}">${k}</option>`).join('');
$('resolution').value = 'standard';
state.view.cx = state.pattern.pitchMm / 2;
setTool('select');
updateProps();
draw();
rebuildMesh();
window.addEventListener('resize', draw);
window.__treadEditor = { state, commit, setTool, rebuildMesh };
