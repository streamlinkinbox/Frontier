import React, { useMemo, useState } from 'react';
import { X, Search, Route, Milestone, Signpost, Waypoints, Map as MapIcon, Footprints, PenTool, Check, LayoutGrid } from 'lucide-react';
import { useEditor, editor } from './useEditor.js';
import { PROFILES, CATEGORIES, LANE_TYPES, layout } from '../road/profiles.js';
import { CrossSection } from './CrossSection.jsx';

const CAT_ICON = { highway: Milestone, arterial: Route, urban: Signpost, residential: Waypoints, rural: MapIcon, active: Footprints };

function tags(p) {
  const t = new Set();
  for (const l of p.lanes) {
    if (l.type === 'bus') t.add('Bus lane');
    if (l.type === 'bike' || l.type === 'cycletrack') t.add('Cycling');
    if (l.type === 'parking') t.add('Parking');
    if (l.type === 'tram') t.add('Tram');
    if (l.type === 'median' || l.type === 'barrier') t.add('Divided');
    if (l.type === 'planter' || p.trees) t.add('Trees');
  }
  if (p.oneWay) t.add('One-way');
  return [...t].slice(0, 3);
}

export function RoadLibrary() {
  const st = useEditor();
  const [cat, setCat] = useState('all');
  const [q, setQ] = useState('');
  const [pick, setPick] = useState(st.drawProfile);
  const sel = st.selection;
  const selIds = sel?.type === 'edge' ? [sel.id] : sel?.type === 'street' ? sel.ids : null;
  const list = useMemo(() => Object.values(PROFILES).filter(p => (cat === 'all' || p.category === cat) && (!q || (p.name + ' ' + (p.desc || '')).toLowerCase().includes(q.toLowerCase()))), [cat, q]);
  if (!st.library) return null;
  const close = () => editor.set({ library: false });
  const draw = () => { editor.set({ drawProfile: pick, library: false }); editor.setTool('draw'); };
  const apply = () => { editor.setEdgeProfile(selIds, pick); close(); };
  const P = PROFILES[pick];
  return (
    <div className="construct-backdrop" onMouseDown={e => e.target === e.currentTarget && close()}>
      <div className="construct-panel">
        <header>
          <div className="construct-emblem"><LayoutGrid size={18} /></div>
          <div><h2>Road library</h2><p>{Object.keys(PROFILES).length} cross-section presets — every lane is editable after placing</p></div>
          <span className="construct-engine">FRONTIER / ROADS</span>
          <button className="construct-close" onClick={close}><X size={16} /></button>
        </header>
        <div className="construct-context">
          <span>Construct</span><span>/</span><span className="current">{cat === 'all' ? 'All roads' : CATEGORIES.find(c => c.id === cat)?.label}</span>
          <span className="construct-context-end"><span className="construct-dot" />{selIds ? `${selIds.length} segment${selIds.length > 1 ? 's' : ''} selected` : 'Nothing selected'}</span>
        </div>
        <div className="construct-layout">
          <nav>
            <button className={cat === 'all' ? 'active' : ''} onClick={() => setCat('all')}><LayoutGrid size={14} />All<small>{Object.keys(PROFILES).length}</small></button>
            <div className="construct-nav-caption">CATEGORIES</div>
            {CATEGORIES.map(c => { const I = CAT_ICON[c.id]; return <button key={c.id} className={cat === c.id ? 'active' : ''} onClick={() => setCat(c.id)}><I size={14} />{c.label}<small>{Object.values(PROFILES).filter(p => p.category === c.id).length}</small></button>; })}
            <div className="construct-nav-note">Tip: <kbd>Shift</kbd>+<kbd>A</kbd> opens this panel. Double-click a tile to start drawing.</div>
          </nav>
          <div className="construct-content">
            <label className="construct-search"><Search size={13} /><input autoFocus placeholder="Search road types…" value={q} onChange={e => setQ(e.target.value)} /><kbd>Esc</kbd></label>
            {(cat === 'all' ? CATEGORIES : CATEGORIES.filter(c => c.id === cat)).map(c => {
              const items = list.filter(p => p.category === c.id);
              if (!items.length) return null;
              return (
                <React.Fragment key={c.id}>
                  <div className="construct-section-title">{c.label}<span>{c.blurb}</span></div>
                  <div className="construct-grid">
                    {items.map(p => (
                      <button key={p.id} className={'construct-tile' + (pick === p.id ? ' selected' : '')} onClick={() => setPick(p.id)} onDoubleClick={() => { setPick(p.id); editor.set({ drawProfile: p.id, library: false }); editor.setTool('draw'); }}>
                        <div className="tile-art"><CrossSection lanes={p.lanes} compact /></div>
                        <strong>{p.name}</strong>
                        <span>{layout(p).total.toFixed(1)} m · {p.speed} km/h · {p.lanes.filter(l => LANE_TYPES[l.type].traffic && l.type !== 'bike').length} lanes</span>
                        <div className="tile-tags">{tags(p).map(t => <i key={t}>{t}</i>)}</div>
                      </button>
                    ))}
                  </div>
                </React.Fragment>
              );
            })}
          </div>
        </div>
        <footer>
          <span>{P?.desc}</span>
          {selIds && <button className="construct-secondary" onClick={apply}><Check size={13} />Apply to selection</button>}
          <button className="construct-submit" onClick={draw}><PenTool size={13} />Draw with {P?.name}</button>
        </footer>
      </div>
    </div>
  );
}
