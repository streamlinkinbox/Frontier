import React, { useEffect, useRef, useState } from 'react';
import { MousePointer2, PenTool, Eraser, Sun, Network, Layers3, Undo2, Redo2, Magnet, Keyboard, ChevronDown, Gauge, Moon } from 'lucide-react';
import { World, CATEGORY_COLORS } from '../render/world.js';
import { SPEED_STEPS } from '../render/camera.js';
import { useEditor, editor } from './useEditor.js';
import { PROFILES, getProfile } from '../road/profiles.js';
import { CrossSection } from './CrossSection.jsx';

export function Viewport() {
  const wrap = useRef(null), canvas = useRef(null);
  const st = useEditor();
  const [help, setHelp] = useState(true);

  useEffect(() => {
    const world = new World(canvas.current);
    editor.attach(world);
    window.__world = world; window.__editor = editor;
    const el = canvas.current;
    const down = e => { if (editor.pointerDown(e)) { e.stopPropagation(); world.controls.mode = null; } };
    const move = e => editor.pointerMove(e);
    const up = e => editor.pointerUp(e);
    const dbl = e => editor.doubleClick(e);
    el.addEventListener('pointerdown', down, true);
    el.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    el.addEventListener('dblclick', dbl);
    const key = e => editor.keyDown(e);
    window.addEventListener('keydown', key);
    editor.generate();
    return () => { el.removeEventListener('pointerdown', down, true); el.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); el.removeEventListener('dblclick', dbl); window.removeEventListener('keydown', key); world.dispose(); };
  }, []);

  const setView = v => { editor.world.setView(v); editor.set({ view: v }); };
  const prof = PROFILES[st.drawProfile];
  const night = st.settings.hour < 6.5 || st.settings.hour > 19;

  return (
    <section className="viewport-col">
      <div className="vp-strip">
        <div className="seg">
          <button className={st.tool === 'select' ? 'on' : ''} onClick={() => editor.setTool('select')} title="Select & edit (V)"><MousePointer2 size={13} />Select<kbd>V</kbd></button>
          <button className={st.tool === 'draw' ? 'on' : ''} onClick={() => editor.setTool('draw')} title="Draw road (B)"><PenTool size={13} />Draw<kbd>B</kbd></button>
          <button className={st.tool === 'bulldoze' ? 'on' : ''} onClick={() => editor.setTool('bulldoze')} title="Bulldoze (X)"><Eraser size={13} />Bulldoze<kbd>X</kbd></button>
        </div>
        <button className="profile-chip" onClick={() => editor.set({ library: true })} title="Road type for drawing (Shift+A)">
          <div className="mini"><CrossSection lanes={prof.lanes} compact /></div>
          <span>{prof.name}</span><ChevronDown size={12} />
        </button>
        {st.tool === 'draw' && (
          <label className="elev" title="Elevation of new points (m). Roads crossing at different heights become bridges.">
            Elev <input type="number" step="1" min="-4" max="30" value={st.drawElevation} onChange={e => editor.set({ drawElevation: +e.target.value || 0 })} /> m
          </label>
        )}
        <button className={'vbtn icon' + (st.snap ? ' on' : '')} onClick={() => editor.set({ snap: !st.snap })} title="Snap to 1 m grid · Shift = 15° angles (G)"><Magnet size={13} /></button>
        <div className="sep" />
        <button className="vbtn icon" disabled={!st.canUndo} onClick={() => editor.undo()} title="Undo (Ctrl+Z)"><Undo2 size={13} /></button>
        <button className="vbtn icon" disabled={!st.canRedo} onClick={() => editor.redo()} title="Redo (Ctrl+Shift+Z)"><Redo2 size={13} /></button>
        <div className="grow" />
        <div className="seg">
          <button className={st.view === 'lit' ? 'on' : ''} onClick={() => setView('lit')} title="Lit"><Sun size={13} />Lit</button>
          <button className={st.view === 'topology' ? 'on' : ''} onClick={() => setView('topology')} title="Topology: centrelines & junction kinds"><Network size={13} />Topology</button>
          <button className={st.view === 'underground' ? 'on' : ''} onClick={() => setView('underground')} title="Underground: subways"><Layers3 size={13} />Under</button>
        </div>
        <button className="vbtn icon" title="Toggle day / night" onClick={() => { const h = night ? 15.5 : 21.5; editor.world.setSetting('hour', h); editor.set({ settings: { ...st.settings, hour: h } }); }}>{night ? <Moon size={13} /> : <Sun size={13} />}</button>
        <button className={'vbtn icon' + (help ? ' on' : '')} onClick={() => setHelp(!help)} title="Controls"><Keyboard size={13} /></button>
      </div>
      <div ref={wrap} className={'vp-canvas-wrap tool-' + st.tool}>
        <canvas ref={canvas} tabIndex={0} />
        <CamHud />
        <Minimap />
        {st.tool === 'draw' && <div className="overlay-card draw-hud"><span><kbd>Click</kbd>add point</span><span><kbd>Dbl-click</kbd><kbd>Enter</kbd>finish</span><span><kbd>Shift</kbd>15° snap</span><span><kbd>⌫</kbd>undo point</span><span><kbd>Esc</kbd>cancel</span></div>}
        {st.tool === 'place' && <div className="overlay-card draw-hud"><span>Click a road to place <b style={{ color: '#fff', fontWeight: 500 }}>{({ hump: 'speed hump', table: 'speed table', crosswalk: 'crosswalk', busstop: 'bus stop' })[st.placeType]}</b></span><span><kbd>Esc</kbd>done</span></div>}
        {help && (
          <div className="overlay-card help-card">
            <h4>UNREAL-STYLE CAMERA</h4>
            <div className="k">
              <kbd>RMB + mouse</kbd><span>Look around</span>
              <kbd>W A S D</kbd><span>Fly (works with or without RMB)</span>
              <kbd>Q / E</kbd><span>Down / up</span>
              <kbd>RMB + wheel</kbd><span>Fly speed</span>
              <kbd>Shift</kbd><span>Boost ×4</span>
              <kbd>MMB drag</kbd><span>Pan</span>
              <kbd>Alt + LMB</kbd><span>Orbit point</span>
              <kbd>Wheel</kbd><span>Dolly to cursor</span>
              <kbd>F</kbd><span>Focus selection</span>
              <kbd>Dbl-click road</kbd><span>Add bend point</span>
            </div>
          </div>
        )}
        {st.toast && <div className={'overlay-card toast ' + st.toast.kind}><i />{st.toast.msg}</div>}
        {st.busy && (
          <div className="loading">
            <div className="loading-card">
              <div className="eyebrow">FRONTIER · ROAD BUILDER</div>
              <h3>{st.busy}…</h3>
              <div className="bar"><i style={{ width: Math.round(st.progress * 100) + '%' }} /></div>
              <small>Solving junctions, sweeping curbs, painting markings, placing street furniture</small>
            </div>
          </div>
        )}
      </div>
      <Footer />
    </section>
  );
}

function CamHud() {
  const st = useEditor();
  const [, tick] = useState(0);
  useEffect(() => { const t = setInterval(() => tick(x => x + 1), 250); return () => clearInterval(t); }, []);
  const w = editor.world; if (!w) return null;
  const p = w.camera.position;
  const dir = new (p.constructor)(); w.camera.getWorldDirection(dir);
  const hdg = (Math.atan2(dir.x, -dir.z) * 180 / Math.PI + 360) % 360;
  return (
    <div className="overlay-card cam-hud">
      <div className="row"><span><Gauge size={10} style={{ verticalAlign: -1 }} /> Speed</span><b>{SPEED_STEPS[st.cameraSpeed]} m/s</b></div>
      <div className="speed-bars">{SPEED_STEPS.map((_, i) => <i key={i} className={i <= st.cameraSpeed ? 'lit' : ''} onClick={() => w.controls.setSpeedIndex(i)} style={{ cursor: 'pointer' }} />)}</div>
      <div className="row" style={{ marginTop: 8 }}><span>Altitude</span><b>{p.y.toFixed(1)} m</b></div>
      <div className="row"><span>Heading</span><b>{hdg.toFixed(0)}°</b></div>
      <div className="row"><span>FPS · draws</span><b>{w.stats.fps} · {w.stats.calls}</b></div>
      <div className="row"><span>Triangles</span><b>{(w.stats.tris / 1e6).toFixed(2)} M</b></div>
    </div>
  );
}

function Footer() {
  const st = useEditor();
  const [, tick] = useState(0);
  useEffect(() => { const t = setInterval(() => tick(x => x + 1), 200); return () => clearInterval(t); }, []);
  const c = editor.cursorPos;
  const hover = st.hover && editor.net ? (st.hover.type === 'edge' ? (() => { const e = editor.net.edges.get(st.hover.id); return e ? `${e.name || 'Road'} · ${getProfile(e).name}` : ''; })() : st.hover.type === 'node' ? 'Junction ' + st.hover.id : st.hover.type === 'rail' ? 'Railway' : '') : '';
  const hints = {
    select: <>Click to select · drag handles to move · <kbd>Del</kbd> delete · <kbd>Ctrl</kbd>+click multi-select</>,
    draw: <>Snaps to junctions <span style={{ color: '#22c55e' }}>●</span> and roads <span style={{ color: '#f59e0b' }}>●</span> · crossings become junctions, different heights become bridges</>,
    bulldoze: <>Click a road or junction to remove it</>,
    place: <>Click on a road to place the feature</>,
  };
  return (
    <div className="vp-footer">
      <span>X <b>{c ? c.x.toFixed(1) : '—'}</b></span><span>Z <b>{c ? c.z.toFixed(1) : '—'}</b></span><span>Y <b>{c ? (c.y ?? 0).toFixed(2) : '—'}</b></span>
      {hover && <span style={{ color: '#9aa1aa' }}>{hover}</span>}
      <span className="hint">{hints[st.tool]}</span>
    </div>
  );
}

function Minimap() {
  const st = useEditor();
  const ref = useRef(null), bg = useRef(null);
  const S = 196;
  useEffect(() => {
    const net = editor.net; if (!net) return;
    const c = document.createElement('canvas'); c.width = c.height = S * 2;
    const g = c.getContext('2d'), ext = (net.meta.size || 4096) * 0.58, k = S * 2 / (ext * 2);
    const P = (x, z) => [(x + ext) * k, (z + ext) * k];
    g.fillStyle = '#14171b'; g.fillRect(0, 0, S * 2, S * 2);
    if (net.river) { g.strokeStyle = '#1f4a5a'; g.lineWidth = net.river.width * k; g.lineJoin = 'round'; g.beginPath(); net.river.pts.forEach(([x, z], i) => { const [a, b] = P(x, z); i ? g.lineTo(a, b) : g.moveTo(a, b); }); g.stroke(); }
    const order = ['active', 'rural', 'residential', 'urban', 'arterial', 'highway'];
    const by = new Map(order.map(o => [o, []]));
    for (const e of net.edges.values()) by.get(getProfile(e).category)?.push(e);
    for (const cat of order) {
      g.strokeStyle = CATEGORY_COLORS[cat]; g.globalAlpha = cat === 'active' ? 0.45 : 0.9;
      g.lineWidth = cat === 'highway' ? 2.6 : cat === 'arterial' ? 1.8 : 0.9;
      g.beginPath();
      for (const e of by.get(cat)) { const cl = net.centerline(e); for (let i = 0; i < cl.X.length; i += 3) { const [a, b] = P(cl.X[i], cl.Z[i]); i ? g.lineTo(a, b) : g.moveTo(a, b); } const [a, b] = P(cl.X[cl.X.length - 1], cl.Z[cl.Z.length - 1]); g.lineTo(a, b); }
      g.stroke();
    }
    g.globalAlpha = 1; g.setLineDash([4, 3]); g.strokeStyle = '#d0d0d0'; g.lineWidth = 1.4;
    for (const r of net.rails.values()) { g.beginPath(); r.pts.forEach((p, i) => { const [a, b] = P(p.x, p.z); i ? g.lineTo(a, b) : g.moveTo(a, b); }); g.stroke(); }
    g.setLineDash([]);
    bg.current = { c, ext, k };
  }, [st.version]);
  useEffect(() => {
    let raf;
    const draw = () => {
      raf = requestAnimationFrame(draw);
      const cv = ref.current, B = bg.current, w = editor.world; if (!cv || !B || !w) return;
      const g = cv.getContext('2d');
      g.drawImage(B.c, 0, 0);
      const p = w.camera.position, [x, y] = [(p.x + B.ext) * B.k, (p.z + B.ext) * B.k];
      const d = new p.constructor(); w.camera.getWorldDirection(d);
      const a = Math.atan2(d.z, d.x);
      g.fillStyle = 'rgba(79,216,224,.18)'; g.beginPath(); g.moveTo(x, y); g.arc(x, y, 36, a - 0.5, a + 0.5); g.closePath(); g.fill();
      g.fillStyle = '#4fd8e0'; g.beginPath(); g.arc(x, y, 4, 0, 7); g.fill();
      g.strokeStyle = '#0b0c0d'; g.lineWidth = 1.5; g.stroke();
    };
    draw();
    return () => cancelAnimationFrame(raf);
  }, []);
  const click = e => {
    const B = bg.current; if (!B) return;
    const r = e.currentTarget.getBoundingClientRect();
    const x = (e.clientX - r.left) / r.width * S * 2 / B.k - B.ext, z = (e.clientY - r.top) / r.height * S * 2 / B.k - B.ext;
    editor.world.focus(x, 0, z, Math.max(120, editor.world.camera.position.y * 1.2));
  };
  if (!editor.net) return null;
  return (
    <div className="overlay-card minimap">
      <div className="mm-label"><span>MAP</span><span>{((editor.net.meta.size || 4096) / 1000).toFixed(1)} KM</span></div>
      <canvas ref={ref} width={S * 2} height={S * 2} style={{ width: S, height: S }} onClick={click} />
    </div>
  );
}
