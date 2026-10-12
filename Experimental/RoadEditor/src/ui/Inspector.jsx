import React, { useMemo, useState } from 'react';
import {
  Route, CircleDot, TrainFront, TramFront, Building2, Crosshair, Trash2, Ruler, Mountain, ArrowLeftRight, Scissors, Spline, Plus, X,
  ArrowUp, ArrowDown, ArrowUpDown, Waves, TrafficCone, Footprints, Bus, Gauge, Dice5, Sparkles, Download, Upload, Eye, Library, RotateCcw, Layers, Info, Clock3,
} from 'lucide-react';
import { useEditor, editor } from './useEditor.js';
import { CrossSection } from './CrossSection.jsx';
import { PROFILES, LANE_TYPES, CATEGORIES, getProfile, layout } from '../road/profiles.js';
import { nodeName } from './Outliner.jsx';
import { nodeControl } from '../road/geom/control.js';
import { autoRadius } from '../road/junction.js';

const Toggle = ({ on, onClick }) => <button className={'toggle' + (on ? ' on' : '')} onClick={onClick} aria-pressed={on}><span /></button>;
function Slider({ label, value, min, max, step = 1, unit = '', onChange, onCommit, fmt }) {
  const pct = ((value - min) / (max - min)) * 100;
  return (
    <div className="slider-block">
      <div className="control-line"><span>{label}</span><b>{fmt ? fmt(value) : value}{unit}</b></div>
      <input type="range" min={min} max={max} step={step} value={value} style={{ '--progress': pct + '%' }}
        onChange={e => onChange(+e.target.value)} onPointerUp={e => onCommit?.(+e.target.value)} onKeyUp={e => onCommit?.(+e.target.value)} />
    </div>
  );
}
// Slider that edits local state while dragging and commits once (one undo step)
function CommitSlider({ value, onCommit, ...p }) {
  const [v, setV] = useState(null);
  return <Slider {...p} value={v ?? value} onChange={setV} onCommit={x => { setV(null); if (x !== value) onCommit(x); }} />;
}

function Header({ icon: Icon, eyebrow, title, onRename, pill, actions }) {
  const [draft, setDraft] = useState(null);
  return (
    <div className="object-header">
      <div className="object-title">
        <div className="object-icon"><Icon size={26} strokeWidth={1.4} /></div>
        <div style={{ minWidth: 0 }}>
          <div className="eyebrow">{eyebrow}</div>
          {onRename ? <input className="h1-input" value={draft ?? title} onChange={e => setDraft(e.target.value)} onBlur={() => { if (draft !== null && draft !== title) onRename(draft); setDraft(null); }} onKeyDown={e => e.key === 'Enter' && e.currentTarget.blur()} /> : <h1>{title}</h1>}
        </div>
      </div>
      <div className="object-actions">{pill}{actions}</div>
    </div>
  );
}

export function Inspector() {
  const st = useEditor();
  const s = st.selection, net = editor.net;
  let body = null;
  if (!net) body = <div className="empty-note">Generating…</div>;
  else if (s?.type === 'edge' && net.edges.has(s.id)) body = <EdgeInspector key={s.id} id={s.id} st={st} />;
  else if (s?.type === 'street') body = <StreetInspector sel={s} st={st} />;
  else if (s?.type === 'node' && net.nodes.has(s.id)) body = <NodeInspector key={s.id} id={s.id} st={st} />;
  else if (s?.type === 'rail' && net.rails.has(s.id)) body = <RailInspector id={s.id} />;
  else if (s?.type === 'subway' && net.subways.has(s.id)) body = <SubwayInspector id={s.id} />;
  else body = <CityInspector st={st} />;
  return (
    <aside className="inspector">
      <div className="inspector-top">
        <div><span>Inspector</span><span>/</span>{s ? ({ edge: 'Road segment', street: 'Street', node: 'Junction', rail: 'Railway', subway: 'Subway' })[s.type] : 'City'}</div>
        <div className="save-status"><span className="status-dot" />{st.canUndo ? editor.undoStack.length + ' edits' : 'Saved'}</div>
      </div>
      <div className="inspector-content">{body}
        <div className="inspector-footer"><span><Info size={10} />Frontier Road Editor</span><span>v{st.version}</span></div>
      </div>
    </aside>
  );
}

// ------------------------------------------------------------------ edge
function EdgeInspector({ id, st }) {
  const net = editor.net, e = net.edges.get(id);
  const prof = getProfile(e), lay = layout(prof), c = net.centerline(e);
  const [hl, setHl] = useState(-1);
  const A = net.nodes.get(e.a), B = net.nodes.get(e.b);
  const grade = ((B.y - A.y) / c.length) * 100;
  const maxY = Math.max(...Array.from(c.Y));
  const lanes = prof.lanes;
  const setLanes = L => editor.setEdgeLanes(id, L);
  const catProfiles = Object.values(PROFILES).filter(p => p.category === prof.category || p.id === e.profile);
  const featIcon = { hump: Gauge, crosswalk: Footprints, busstop: Bus, barrierGap: ArrowLeftRight };
  return (
    <>
      <Header icon={Route} eyebrow={`Road segment · ${CATEGORIES.find(x => x.id === prof.category)?.label || ''}`} title={e.name || 'Unnamed road'} onRename={v => editor.setEdgeField([id], 'name', v)}
        pill={<span className="enabled-pill"><span />{prof.speed || 50} km/h</span>}
        actions={<><button className="icon-button" title="Focus (F)" onClick={() => editor.focusSelection()}><Crosshair size={15} /></button><button className="icon-button" title="Delete (Del)" onClick={() => editor.deleteSelection()}><Trash2 size={15} /></button></>} />
      <div className="cards">
        <div className="card">
          <div className="card-heading"><span><Layers size={14} />Cross-section</span>{e.custom ? <button className="small-pill warn" onClick={() => editor.resetEdgeLanes(id)} title="Reset to preset"><RotateCcw size={9} style={{ verticalAlign: -1 }} /> custom</button> : <span className="small-pill">{prof.name}</span>}</div>
          <div className="xsec"><CrossSection lanes={lanes} highlight={hl} onLane={i => setHl(i === hl ? -1 : i)} /></div>
          <div className="lanes">
            {lanes.map((l, i) => (
              <div key={i} className={'lane-row' + (hl === i ? ' hl' : '')} onMouseEnter={() => setHl(i)} onMouseLeave={() => setHl(-1)}>
                <i className="chip" style={{ background: LANE_TYPES[l.type].color }} />
                <select value={l.type} onChange={ev => setLanes(lanes.map((x, k) => k === i ? { ...x, type: ev.target.value, width: LANE_TYPES[ev.target.value].w, ...(LANE_TYPES[ev.target.value].traffic ? { dir: x.dir ?? 1 } : {}) } : x))}>
                  {Object.entries(LANE_TYPES).map(([k, t]) => <option key={k} value={k}>{t.label}</option>)}
                </select>
                <input type="number" step="0.1" min="0.3" max="12" defaultValue={l.width} key={l.type + l.width} onBlur={ev => { const w = Math.max(0.3, +ev.target.value || l.width); if (w !== l.width) setLanes(lanes.map((x, k) => k === i ? { ...x, width: w } : x)); }} onKeyDown={ev => ev.key === 'Enter' && ev.currentTarget.blur()} />
                <button className="dir" title="Direction" disabled={!LANE_TYPES[l.type].traffic} onClick={() => setLanes(lanes.map((x, k) => k === i ? { ...x, dir: x.dir === 1 ? -1 : x.dir === -1 ? 0 : 1 } : x))}>
                  {!LANE_TYPES[l.type].traffic ? '·' : l.dir === 1 ? <ArrowUp size={12} /> : l.dir === -1 ? <ArrowDown size={12} /> : <ArrowUpDown size={12} />}
                </button>
                <button className="del" title="Remove lane" onClick={() => lanes.length > 1 && setLanes(lanes.filter((_, k) => k !== i))}><X size={12} /></button>
              </div>
            ))}
          </div>
          <div className="btn-row">
            <button className="ibtn" onClick={() => setLanes([{ type: 'drive', width: 3.3, dir: -1 }, ...lanes])}><Plus size={12} />Lane left</button>
            <button className="ibtn" onClick={() => setLanes([...lanes, { type: 'drive', width: 3.3, dir: 1 }])}><Plus size={12} />Lane right</button>
            <button className="ibtn" onClick={() => setLanes([...lanes].reverse().map(l => ({ ...l, ...(l.dir ? { dir: -l.dir } : {}) })))}><ArrowLeftRight size={12} />Mirror</button>
          </div>
        </div>

        <div className="card">
          <div className="card-heading"><span><Library size={14} />Road type</span><button className="small-pill accent" onClick={() => editor.set({ library: true })}>Library</button></div>
          <select className="sel" style={{ width: '100%' }} value={e.custom ? '' : e.profile} onChange={ev => editor.setEdgeProfile([id], ev.target.value)}>
            {e.custom && <option value="">Custom ({prof.name})</option>}
            {CATEGORIES.map(cat => <optgroup key={cat.id} label={cat.label}>{Object.values(PROFILES).filter(p => p.category === cat.id).map(p => <option key={p.id} value={p.id}>{p.name}</option>)}</optgroup>)}
          </select>
          <div className="pseg" style={{ marginTop: 12 }}>
            {catProfiles.slice(0, 8).map(p => <button key={p.id} className={!e.custom && e.profile === p.id ? 'on' : ''} onClick={() => editor.setEdgeProfile([id], p.id)}>{p.name}</button>)}
          </div>
          <p className="empty-note" style={{ marginBottom: 0 }}>{prof.desc}</p>
        </div>

        <div className="card">
          <div className="card-heading"><span><Ruler size={14} />Geometry</span>{maxY > 2 ? <span className="small-pill accent">Bridge · {maxY.toFixed(1)} m</span> : <span className="small-pill">At grade</span>}</div>
          <div className="metric-row">
            <div className="metric">{c.length.toFixed(0)}<small>m</small></div>
            <div className="metric-caption">{lay.total.toFixed(1)} m wide<br />{e.pts.length} bend point{e.pts.length === 1 ? '' : 's'} · {grade.toFixed(1)}% grade</div>
          </div>
          <CommitSlider label="Start elevation" unit=" m" min={-2} max={24} step={0.5} value={A.y} onCommit={v => editor.setNodeField(A.id, { y: v }, 'Elevation')} />
          <CommitSlider label="End elevation" unit=" m" min={-2} max={24} step={0.5} value={B.y} onCommit={v => editor.setNodeField(B.id, { y: v }, 'Elevation')} />
          {e.pts.length > 0 && <CommitSlider label="Bend elevation" unit=" m" min={-2} max={24} step={0.5} value={e.pts[0].y ?? 0} onCommit={v => editor.op('Elevation', () => { net.touchEdge(id); e.pts = e.pts.map(p => ({ ...p, y: v })); })} />}
          <div className="btn-row">
            <button className="ibtn" onClick={() => editor.reverseEdge(id)}><ArrowLeftRight size={12} />Reverse</button>
            <button className="ibtn" onClick={() => editor.splitEdge(id)}><Scissors size={12} />Split</button>
            <button className="ibtn" disabled={!e.pts.length} onClick={() => editor.op('Straighten', () => { net.touchEdge(id); e.pts = []; })}><Spline size={12} />Straighten</button>
          </div>
        </div>

        <div className="card">
          <div className="card-heading"><span><TrafficCone size={14} />Details & traffic calming</span><small>{e.features.length}</small></div>
          {e.features.length === 0 && <div className="empty-note">No features. Place speed humps, tables, mid-block crosswalks or bus stops.</div>}
          {e.features.map((f, i) => {
            const I = featIcon[f.type] || Info;
            const label = f.type === 'hump' ? (f.kind === 'table' ? 'Speed table' : 'Speed hump') : f.type === 'busstop' ? `Bus stop · ${f.side > 0 ? 'right' : 'left'}` : f.type === 'crosswalk' ? 'Crosswalk' : 'Median gap';
            const s0 = f.s ?? f.s0;
            return (
              <div key={i} className="feat-row">
                <I size={14} />
                <div><div>{label} <small>@ {s0.toFixed(0)} m</small></div>
                  {f.s !== undefined && <input type="range" min={8} max={Math.max(9, c.length - 8)} step={0.5} defaultValue={f.s} style={{ '--progress': ((f.s - 8) / Math.max(1, c.length - 16)) * 100 + '%' }} onPointerUp={ev => editor.updateFeature(id, i, { s: +ev.target.value })} />}
                </div>
                {f.type === 'busstop' ? <button className="small-pill" onClick={() => editor.updateFeature(id, i, { side: -f.side })}>flip</button> : <span />}
                <button className="icon-button" onClick={() => editor.removeFeature(id, i)}><X size={12} /></button>
              </div>
            );
          })}
          <div className="btn-row">
            {[['hump', 'Hump', Gauge], ['table', 'Table', Gauge], ['crosswalk', 'Crosswalk', Footprints], ['busstop', 'Bus stop', Bus]].map(([k, l, I]) => (
              <button key={k} className={'ibtn' + (st.tool === 'place' && st.placeType === k ? ' solid' : '')} onClick={() => editor.setTool('place', { placeType: k })}><I size={12} />{l}</button>
            ))}
          </div>
        </div>

        <div className="card">
          <div className="card-heading"><span><CircleDot size={14} />Endpoints</span></div>
          {[A, B].map((n, k) => (
            <button key={n.id} className="link-row" onClick={() => editor.select({ type: 'node', id: n.id }, { focus: true })}>
              <CircleDot size={13} />{k ? 'End' : 'Start'} · {nodeName(net, n.id)}<small>{editor.builder.junctions.get(n.id)?.kind}</small>
            </button>
          ))}
        </div>
      </div>
    </>
  );
}

function StreetInspector({ sel, st }) {
  const net = editor.net;
  const ids = sel.ids.filter(id => net.edges.has(id));
  const len = ids.reduce((a, id) => a + net.centerline(net.edges.get(id)).length, 0);
  const profs = [...new Set(ids.map(id => getProfile(net.edges.get(id)).name))];
  const first = net.edges.get(ids[0]);
  return (
    <>
      <Header icon={Route} eyebrow={`Street · ${ids.length} segments`} title={sel.name} onRename={v => { editor.setEdgeField(ids, 'name', v); editor.select({ ...sel, name: v }); }}
        actions={<><button className="icon-button" onClick={() => editor.focusSelection()}><Crosshair size={15} /></button><button className="icon-button" onClick={() => editor.deleteSelection()}><Trash2 size={15} /></button></>} />
      <div className="cards">
        <div className="card">
          <div className="card-heading"><span><Ruler size={14} />Street</span></div>
          <div className="metric">{(len / 1000).toFixed(2)}<small>km</small></div>
          <div className="legend">{profs.map(p => <span key={p} className="small-pill">{p}</span>)}</div>
          {first && <div className="xsec" style={{ marginTop: 14 }}><CrossSection lanes={getProfile(first).lanes} /></div>}
        </div>
        <div className="card">
          <div className="card-heading"><span><Library size={14} />Change road type for whole street</span></div>
          <select className="sel" style={{ width: '100%' }} value="" onChange={ev => ev.target.value && editor.setEdgeProfile(ids, ev.target.value)}>
            <option value="">Choose…</option>
            {CATEGORIES.map(cat => <optgroup key={cat.id} label={cat.label}>{Object.values(PROFILES).filter(p => p.category === cat.id).map(p => <option key={p.id} value={p.id}>{p.name}</option>)}</optgroup>)}
          </select>
        </div>
        <div className="card">
          <div className="card-heading"><span><Spline size={14} />Segments</span></div>
          {ids.slice(0, 40).map((id, i) => (
            <button key={id} className="link-row" onClick={() => editor.select({ type: 'edge', id }, { focus: true })}><Route size={13} />Segment {i + 1}<small>{Math.round(net.centerline(net.edges.get(id)).length)} m</small></button>
          ))}
        </div>
      </div>
    </>
  );
}

// ------------------------------------------------------------------ node
const CONTROL_LABEL = { auto: 'Auto', signal: 'Signals', allWayStop: 'All-way stop', twoWayStop: 'Two-way stop', yield: 'Yield', none: 'Uncontrolled' };
function NodeInspector({ id, st }) {
  const net = editor.net, n = net.nodes.get(id), J = editor.builder.junctions.get(id);
  const es = net.nodeEdges(id);
  const resolved = J ? nodeControl(J) : 'none';
  const auto = J && J.ends.length >= 2 ? autoRadius(J.ends) : 8;
  const kindLabel = { junction: 'Intersection', roundabout: 'Roundabout', culdesac: 'Cul-de-sac', flat: 'Dead end', flush: 'Open end', continuation: 'Continuation', none: 'Isolated' }[J?.kind] || J?.kind;
  return (
    <>
      <Header icon={J?.kind === 'roundabout' ? CircleDot : TrafficCone} eyebrow={`Junction · ${kindLabel} · ${es.length}-way`} title={nodeName(net, id)}
        pill={<span className="enabled-pill"><span />{CONTROL_LABEL[resolved] || resolved}</span>}
        actions={<><button className="icon-button" onClick={() => editor.focusSelection()}><Crosshair size={15} /></button><button className="icon-button" onClick={() => editor.deleteSelection()}><Trash2 size={15} /></button></>} />
      <div className="cards">
        {es.length >= 3 && (
          <div className="card">
            <div className="card-heading"><span><CircleDot size={14} />Junction type</span></div>
            <div className="pseg">
              <button className={n.kind !== 'roundabout' ? 'on' : ''} onClick={() => editor.setNodeField(id, { kind: 'junction' })}>Intersection</button>
              <button className={n.kind === 'roundabout' ? 'on' : ''} onClick={() => editor.setNodeField(id, { kind: 'roundabout' })}>Roundabout</button>
            </div>
            {n.kind === 'roundabout' && <CommitSlider label="Central island radius" unit=" m" min={10} max={45} step={0.5} value={n.rbRadius || 20} onCommit={v => editor.setNodeField(id, { rbRadius: v })} />}
          </div>
        )}
        {es.length === 1 && (
          <div className="card">
            <div className="card-heading"><span><CircleDot size={14} />Road end</span></div>
            <div className="pseg">
              {[['culdesac', 'Cul-de-sac'], ['flat', 'Curbed end'], ['flush', 'Open end']].map(([k, l]) => <button key={k} className={J?.kind === k ? 'on' : ''} onClick={() => editor.setNodeField(id, { end: k })}>{l}</button>)}
            </div>
            {J?.kind === 'culdesac' && <CommitSlider label="Turning circle radius" unit=" m" min={8} max={24} step={0.5} value={n.rbRadius || 12} onCommit={v => editor.setNodeField(id, { rbRadius: v })} />}
          </div>
        )}
        {J?.kind === 'junction' && (
          <div className="card">
            <div className="card-heading"><span><TrafficCone size={14} />Traffic control</span><small>resolved: {CONTROL_LABEL[resolved]}</small></div>
            <div className="pseg">
              {Object.entries(CONTROL_LABEL).map(([k, l]) => <button key={k} className={(n.control || 'auto') === k ? 'on' : ''} onClick={() => editor.setNodeField(id, { control: k })}>{l}</button>)}
            </div>
            <p className="empty-note" style={{ marginBottom: 0 }}>Signals add mast arms, signal heads, stop bars and zebra crosswalks with tactile paving; stop/yield add signs and give-way lines.</p>
          </div>
        )}
        <div className="card">
          <div className="card-heading"><span><Mountain size={14} />Geometry</span></div>
          {J?.kind === 'junction' && (
            <>
              <div className="control-line"><span>Curb radius</span><span style={{ display: 'flex', gap: 8, alignItems: 'center' }}><small>auto</small><Toggle on={n.radius == null} onClick={() => editor.setNodeField(id, { radius: n.radius == null ? Math.round(auto) : null })} /></span></div>
              {n.radius != null && <CommitSlider label="Radius" unit=" m" min={2} max={30} step={0.5} value={n.radius} onCommit={v => editor.setNodeField(id, { radius: v })} />}
            </>
          )}
          <CommitSlider label="Elevation" unit=" m" min={-2} max={24} step={0.5} value={n.y} onCommit={v => editor.setNodeField(id, { y: v }, 'Elevation')} />
          <div className="control-line"><span>Position</span><small>{n.x.toFixed(1)}, {n.z.toFixed(1)}</small></div>
        </div>
        <div className="card">
          <div className="card-heading"><span><Route size={14} />Approaches</span><small>{es.length}</small></div>
          {es.map(e => <button key={e.id} className="link-row" onClick={() => editor.select({ type: 'edge', id: e.id }, { focus: true })}><Route size={13} />{e.name || 'Road'}<small>{getProfile(e).name}</small></button>)}
        </div>
      </div>
    </>
  );
}

// ------------------------------------------------------------------ transit
function RailInspector({ id }) {
  const net = editor.net, r = net.rails.get(id);
  const len = r.pts.reduce((a, p, i) => (i ? a + Math.hypot(p.x - r.pts[i - 1].x, p.z - r.pts[i - 1].z) : 0), 0);
  const out = editor.builder.outs.get(id);
  const crossings = out?.props?.gateArm ? out.props.gateArm.length / 8 / 2 : 0;
  return (
    <>
      <Header icon={TrainFront} eyebrow="Railway · electrified" title={r.name || 'Railway'} actions={<button className="icon-button" onClick={() => editor.focusSelection()}><Crosshair size={15} /></button>} />
      <div className="cards">
        <div className="card">
          <div className="card-heading"><span><Ruler size={14} />Line</span></div>
          <div className="metric">{(len / 1000).toFixed(2)}<small>km</small></div>
          <div className="stat-grid"><div>Tracks<b>{r.tracks}</b></div><div>Level crossings<b>{Math.round(crossings)}</b></div></div>
          <div className="pseg" style={{ marginTop: 14 }}>
            {[1, 2].map(t => <button key={t} className={r.tracks === t ? 'on' : ''} onClick={() => { r.tracks = t; editor.builder.buildRailEl(r, editor.builder.ctx()); editor.world.applyUpdate({ updated: [id] }); editor.bump(); }}>{t} track{t > 1 ? 's' : ''}</button>)}
          </div>
          <p className="empty-note">Ballast bed, sleepers, rails, catenary masts and fencing; level crossings get full-width panels, crossbucks, flashing lights, half-barrier gates, RXR markings and advance warning signs. Over water and roads the line rises on girder bridges.</p>
        </div>
      </div>
    </>
  );
}
function SubwayInspector({ id }) {
  const net = editor.net, s = net.subways.get(id);
  return (
    <>
      <Header icon={TramFront} eyebrow={`Subway · depth ${s.depth} m`} title={s.name} actions={<button className="icon-button" onClick={() => editor.focusSelection()}><Crosshair size={15} /></button>} />
      <div className="cards">
        <div className="card">
          <div className="card-heading"><span><TramFront size={14} />Stations</span><small>{s.stations?.length || 0}</small></div>
          {(s.stations || []).map((x, i) => <div key={i} className="link-row"><CircleDot size={13} style={{ color: s.color }} />{x.name}<small>{Math.round(x.s)} m</small></div>)}
          <p className="empty-note">Twin-track box tunnel with island-platform stations, columns and street entrances on the sidewalk above. Switch to <b>Under</b> view to see it.</p>
        </div>
      </div>
    </>
  );
}

// ------------------------------------------------------------------ city
function CityInspector({ st }) {
  const net = editor.net;
  const stats = useMemo(() => {
    const s = net.stats(); const k = {};
    for (const J of editor.builder.junctions.values()) k[J.kind] = (k[J.kind] || 0) + 1;
    let bike = 0, bus = 0, park = 0;
    for (const e of net.edges.values()) { const L = net.centerline(e).length / 1000; for (const l of getProfile(e).lanes) { if (l.type === 'bike' || l.type === 'cycletrack') bike += L; if (l.type === 'bus') bus += L; if (l.type === 'parking') park += L; } }
    return { ...s, k, bike, bus, park };
  }, [net, st.version]);
  const g = st.gen;
  const setGen = patch => editor.set({ gen: { ...g, ...patch } });
  const setDisp = (k, v) => { editor.world.setSetting(k, v); editor.set({ settings: { ...st.settings, [k]: v } }); };
  const save = () => { const blob = new Blob([editor.exportJSON()], { type: 'application/json' }); const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = (net.meta.name || 'city').replace(/\s+/g, '-').toLowerCase() + '.roads.json'; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 1000); };
  const load = () => { const i = document.createElement('input'); i.type = 'file'; i.accept = '.json'; i.onchange = async () => { const f = i.files[0]; if (!f) return; try { await editor.importJSON(await f.text()); editor.toast('Loaded ' + f.name); } catch (err) { editor.set({ busy: null }); editor.toast(err.message, 'warn'); } }; i.click(); };
  const hh = Math.floor(st.settings.hour), mm = Math.round((st.settings.hour % 1) * 60);
  return (
    <>
      <Header icon={Building2} eyebrow={`City · seed ${net.meta.seed} · ${(net.meta.size / 1000).toFixed(1)} km`} title={net.meta.name} onRename={v => { net.meta.name = v; editor.bump(); }} pill={<span className="enabled-pill"><span />Roads only</span>} />
      <div className="cards">
        <div className="card">
          <div className="card-heading"><span><Route size={14} />Network</span><small>clean topology</small></div>
          <div className="metric-row"><div className="metric">{stats.km.toFixed(0)}<small>km</small></div><div className="metric-caption">{stats.lanesKm.toFixed(0)} lane-km<br />{stats.edges} segments</div></div>
          <div className="stat-grid">
            <div>Intersections<b>{stats.k.junction || 0}</b></div><div>Roundabouts<b>{stats.k.roundabout || 0}</b></div>
            <div>Cul-de-sacs<b>{stats.k.culdesac || 0}</b></div><div>Bus lanes<b>{stats.bus.toFixed(1)} km</b></div>
            <div>Cycle lanes<b>{stats.bike.toFixed(1)} km</b></div><div>Parking lanes<b>{stats.park.toFixed(1)} km</b></div>
            <div>Railway<b>{stats.railKm.toFixed(1)} km</b></div><div>Subway<b>{stats.subKm.toFixed(1)} km</b></div>
          </div>
        </div>

        <div className="card">
          <div className="card-heading"><span><Sparkles size={14} />City generator</span><small>NFS-scale</small></div>
          <div className="two">
            <label className="field">Seed<span style={{ display: 'flex', gap: 6 }}><input type="number" value={g.seed} onChange={e => setGen({ seed: +e.target.value || 0 })} style={{ flex: 1 }} /><button className="ibtn" title="Random seed" onClick={() => setGen({ seed: Math.floor(Math.random() * 99999) })}><Dice5 size={13} /></button></span></label>
            <label className="field">Map size<select value={g.size} onChange={e => setGen({ size: +e.target.value })}><option value={2048}>2 × 2 km</option><option value={4096}>4 × 4 km</option><option value={6144}>6 × 6 km</option><option value={8192}>8 × 8 km</option></select></label>
          </div>
          <Slider label="Street density" min={0.7} max={1.4} step={0.05} value={g.density} fmt={v => v.toFixed(2)} unit="×" onChange={v => setGen({ density: v })} />
          {[['river', 'River with quays & bridges'], ['freeway', 'Elevated freeway ring + interchanges'], ['rail', 'Railway with level crossings'], ['subway', 'Subway lines & stations'], ['tram', 'Tram avenues'], ['roundabouts', 'Roundabouts'], ['oldTown', 'Old town (cobblestone)'], ['suburbs', 'Suburbs with cul-de-sacs'], ['parks', 'Parks & paths']].map(([k, l]) => (
            <div key={k} className="control-line"><span>{l}</span><Toggle on={g[k]} onClick={() => setGen({ [k]: !g[k] })} /></div>
          ))}
          <div className="control-line"><span>Road markings</span><div className="pseg">{[['us', 'US'], ['eu', 'EU']].map(([k, l]) => <button key={k} className={g.markingStyle === k ? 'on' : ''} onClick={() => setGen({ markingStyle: k })}>{l}</button>)}</div></div>
          <div className="btn-row"><button className="ibtn solid wide" onClick={() => editor.generate(g)}><Sparkles size={13} />Generate city</button></div>
        </div>

        <div className="card">
          <div className="card-heading"><span><Eye size={14} />Display</span></div>
          {[['markings', 'Road markings & decals'], ['props', 'Street furniture & signals'], ['trees', 'Trees'], ['shadows', 'Sun shadows']].map(([k, l]) => (
            <div key={k} className="control-line"><span>{l}</span><Toggle on={st.settings[k]} onClick={() => setDisp(k, !st.settings[k])} /></div>
          ))}
          <Slider label={<span style={{ display: 'flex', gap: 6, alignItems: 'center' }}><Clock3 size={11} />Time of day</span>} min={0} max={24} step={0.25} value={st.settings.hour} fmt={() => `${String(hh).padStart(2, '0')}:${String(mm).padStart(2, '0')}`} onChange={v => setDisp('hour', v)} />
          <div className="range-labels"><span>00:00</span><span>12:00</span><span>24:00</span></div>
        </div>

        <div className="card">
          <div className="card-heading"><span><Download size={14} />File</span><small>.roads.json</small></div>
          <div className="btn-row" style={{ marginTop: 0 }}>
            <button className="ibtn wide" onClick={save}><Download size={13} />Save</button>
            <button className="ibtn wide" onClick={load}><Upload size={13} />Open</button>
          </div>
          <p className="empty-note" style={{ marginBottom: 0 }}>Graph format: nodes, edges with lane profiles and bend points, features, rail, subway and river layers.</p>
        </div>
      </div>
    </>
  );
}
