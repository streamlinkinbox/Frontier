import React, { useMemo, useState } from 'react';
import { Layers, Search, Plus, ChevronRight, Route, Waypoints, TrainFront, TramFront, Waves, CircleDot, TrafficCone, Milestone, Footprints, Signpost, X, Map as MapIcon, Bike } from 'lucide-react';
import { useEditor, editor } from './useEditor.js';
import { CATEGORIES, getProfile, RANK } from '../road/profiles.js';

const CAT_ICON = { highway: Milestone, arterial: Route, urban: Signpost, residential: Waypoints, rural: MapIcon, active: Footprints };
const LIMIT = 120;

export function nodeName(net, id) {
  const es = net.nodeEdges(id);
  const names = [...new Set(es.sort((a, b) => (RANK[b.profile] ?? 3) - (RANK[a.profile] ?? 3)).map(e => e.name).filter(Boolean))];
  if (!names.length) return 'Junction ' + id;
  if (names.length === 1) return names[0] + (es.length === 1 ? ' (end)' : '');
  return names[0] + ' × ' + names[1];
}

function buildTree(net, junctions) {
  if (!net) return null;
  const cats = new Map(CATEGORIES.map(c => [c.id, { ...c, streets: new Map(), count: 0 }]));
  for (const e of net.edges.values()) {
    const p = getProfile(e), c = cats.get(p.category) || cats.get('urban');
    const key = e.name || '(unnamed ' + (p.name || 'road') + ')';
    if (!c.streets.has(key)) c.streets.set(key, { name: key, ids: [], profile: p.name });
    c.streets.get(key).ids.push(e.id); c.count++;
  }
  const jk = { signal: [], roundabout: [], stop: [], culdesac: [], other: [] };
  for (const [id, J] of junctions) {
    if (J.kind === 'roundabout') jk.roundabout.push(id);
    else if (J.kind === 'culdesac') jk.culdesac.push(id);
    else if (J.kind === 'junction') (J.ends.length >= 3 && J.ends.filter(e => e.rank >= 4).length >= 2 ? jk.signal : jk.stop).push(id);
  }
  return { cats: [...cats.values()].filter(c => c.count), jk };
}

export function Outliner() {
  const st = useEditor();
  const net = editor.net;
  const [q, setQ] = useState('');
  const [open, setOpen] = useState({ arterial: true, transit: true });
  const [openStreet, setOpenStreet] = useState({});
  const tree = useMemo(() => buildTree(net, editor.builder?.junctions || new Map()), [net, st.version]);
  const sel = st.selection;
  const ql = q.trim().toLowerCase();
  const toggle = k => setOpen(o => ({ ...o, [k]: !o[k] }));
  const isSel = (type, id) => sel && sel.type === type && sel.id === id;
  const streetSel = s => sel && sel.type === 'street' && sel.name === s.name;

  const Group = ({ k, icon: Icon, label, count, children }) => (
    <div className="group">
      <button className={'group-label' + (open[k] || ql ? ' open' : '')} onClick={() => toggle(k)}>
        <ChevronRight className="chev" size={11} /> {Icon && <Icon size={12} />} {label} <span className="group-count">{count}</span>
      </button>
      {(open[k] || ql) && children}
    </div>
  );

  return (
    <aside className="outliner">
      <div className="brand"><div className="brand-symbol"><Layers size={24} strokeWidth={1.5} /></div><span>frontier<span className="brand-dot">.</span></span><span className="version">ROADS / 01</span></div>
      <div className="scene-label"><span>WORKSPACE</span><span className="status-dot" /></div>
      <div className="scene-title">{net?.meta.name || 'No city'}<span className="scene-extension">.roads</span></div>
      <div className="outliner-heading">
        <h2>Outliner <span>{net ? net.edges.size : 0}</span></h2>
        <button className="icon-button" title="Road library (Shift+A)" onClick={() => editor.set({ library: true })}><Plus size={15} /></button>
      </div>
      <label className="search"><Search size={13} /><input placeholder="Search streets, junctions…" value={q} onChange={e => setQ(e.target.value)} />{q && <button onClick={() => setQ('')}><X size={12} /></button>}</label>
      <div className="tree">
        {tree && tree.cats.map(c => {
          const streets = [...c.streets.values()].filter(s => !ql || s.name.toLowerCase().includes(ql)).sort((a, b) => b.ids.length - a.ids.length);
          if (ql && !streets.length) return null;
          const Icon = CAT_ICON[c.id] || Route;
          return (
            <Group key={c.id} k={c.id} icon={Icon} label={c.label} count={streets.length}>
              {streets.slice(0, LIMIT).map(s => {
                const so = openStreet[c.id + s.name];
                return (
                  <React.Fragment key={s.name}>
                    <div className={'tree-row' + (streetSel(s) || (s.ids.length === 1 && isSel('edge', s.ids[0])) ? ' selected' : '')}>
                      <button className="object-button" onClick={() => editor.select(s.ids.length === 1 ? { type: 'edge', id: s.ids[0] } : { type: 'street', ids: s.ids, name: s.name }, { focus: true })}>
                        <Icon size={14} strokeWidth={1.6} /><span className="name">{s.name}</span><small>{s.ids.length > 1 ? s.ids.length + ' seg' : ''}</small>
                      </button>
                      {s.ids.length > 1 && <button className="expander" onClick={() => setOpenStreet(o => ({ ...o, [c.id + s.name]: !so }))}><ChevronRight size={12} style={{ transform: so ? 'rotate(90deg)' : '' }} /></button>}
                    </div>
                    {so && s.ids.slice(0, 60).map((id, i) => {
                      const e = net.edges.get(id); if (!e) return null;
                      return (
                        <div key={id} className={'tree-row sub' + (isSel('edge', id) ? ' selected' : '')}>
                          <button className="object-button" onClick={() => editor.select({ type: 'edge', id }, { focus: true })}><span className="name">Segment {i + 1}</span><small>{Math.round(net.centerline(e).length)} m</small></button>
                        </div>
                      );
                    })}
                  </React.Fragment>
                );
              })}
              {streets.length > LIMIT && <div className="more-row">+ {streets.length - LIMIT} more — refine search</div>}
            </Group>
          );
        })}
        {tree && (
          <Group k="junctions" icon={CircleDot} label="Junctions" count={tree.jk.signal.length + tree.jk.roundabout.length + tree.jk.stop.length + tree.jk.culdesac.length}>
            {[['roundabout', 'Roundabouts', CircleDot], ['signal', 'Signalised', TrafficCone], ['stop', 'Stop / priority', Signpost], ['culdesac', 'Cul-de-sacs', Waypoints]].map(([k, label, Icon]) => {
              const ids = tree.jk[k].filter(id => !ql || nodeName(net, id).toLowerCase().includes(ql));
              if (!ids.length) return null;
              const ok = openStreet['j' + k] || ql;
              return (
                <React.Fragment key={k}>
                  <div className="tree-row">
                    <button className="object-button" onClick={() => setOpenStreet(o => ({ ...o, ['j' + k]: !ok }))}><Icon size={14} strokeWidth={1.6} /><span className="name">{label}</span><small>{ids.length}</small></button>
                    <button className="expander" onClick={() => setOpenStreet(o => ({ ...o, ['j' + k]: !ok }))}><ChevronRight size={12} style={{ transform: ok ? 'rotate(90deg)' : '' }} /></button>
                  </div>
                  {ok && ids.slice(0, 80).map(id => (
                    <div key={id} className={'tree-row sub' + (isSel('node', id) ? ' selected' : '')}>
                      <button className="object-button" onClick={() => editor.select({ type: 'node', id }, { focus: true })}><span className="name">{nodeName(net, id)}</span></button>
                    </div>
                  ))}
                </React.Fragment>
              );
            })}
          </Group>
        )}
        {net && (net.rails.size > 0 || net.subways.size > 0) && (
          <Group k="transit" icon={TrainFront} label="Transit" count={net.rails.size + net.subways.size}>
            {[...net.rails.values()].map(r => (
              <div key={r.id} className={'tree-row' + (isSel('rail', r.id) ? ' selected' : '')}>
                <button className="object-button" onClick={() => editor.select({ type: 'rail', id: r.id }, { focus: true })}><TrainFront size={14} strokeWidth={1.6} /><span className="name">{r.name || 'Railway'}</span><small>{r.tracks} trk</small></button>
              </div>
            ))}
            {[...net.subways.values()].map(s => (
              <div key={s.id} className={'tree-row' + (isSel('subway', s.id) ? ' selected' : '')}>
                <button className="object-button" onClick={() => { if (editor.world.settings.view !== 'underground') { editor.world.setView('underground'); editor.set({ view: 'underground' }); } editor.select({ type: 'subway', id: s.id }, { focus: true }); }}><TramFront size={14} strokeWidth={1.6} style={{ color: s.color }} /><span className="name">{s.name}</span><small>{s.stations?.length || 0} stn</small></button>
              </div>
            ))}
          </Group>
        )}
        {net?.river && (
          <Group k="water" icon={Waves} label="Water" count={1}>
            <div className="tree-row"><button className="object-button" onClick={() => { const p = net.river.pts[Math.floor(net.river.pts.length / 2)]; editor.world.focus(p[0], 0, p[1], 260); }}><Waves size={14} strokeWidth={1.6} /><span className="name">{net.river.name || 'River'}</span><small>{Math.round(net.river.width)} m</small></button></div>
          </Group>
        )}
      </div>
      <div className="outliner-bottom">
        <div className="avatar"><Bike size={15} /></div>
        <div><strong>Road Editor</strong><span>{net ? `${net.nodes.size} nodes · ${net.edges.size} segments` : 'Idle'}</span></div>
      </div>
    </aside>
  );
}
