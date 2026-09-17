import { RoofParams, RoofStyle, Region, TileSystem, PRESETS, STYLE_META } from '../lib/types';
import { Check, RoofStats } from '../lib/buildRoof';

interface Props {
  params: RoofParams;
  onChange: (patch: Partial<RoofParams>) => void;
  onPreset: (id: string) => void;
  onRegion: (r: Region) => void;
  checks: Check[];
  stats: RoofStats | null;
  onExportGLB: () => void;
  onExportJSON: () => void;
}

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <section className="panel-section">
      <h2>{title}</h2>
      {children}
    </section>
  );
}

function Slider(p: { label: string; value: number; min: number; max: number; step: number; unit?: string; onChange: (v: number) => void }) {
  return (
    <label className="slider-row">
      <span className="slider-label">{p.label}<em>{p.value.toFixed(2)}{p.unit ?? ''}</em></span>
      <input type="range" min={p.min} max={p.max} step={p.step} value={p.value} onChange={(e) => p.onChange(parseFloat(e.target.value))} />
    </label>
  );
}

function Toggle(p: { label: string; value: boolean; onChange: (v: boolean) => void }) {
  return (
    <label className="toggle-row">
      <span>{p.label}</span>
      <button type="button" className={`switch ${p.value ? 'on' : ''}`} onClick={() => p.onChange(!p.value)} aria-pressed={p.value}>
        <i />
      </button>
    </label>
  );
}

const TILE_SWATCHES = [
  { c: '#6e7278', n: 'Ibushi silver' },
  { c: '#2e3238', n: 'Glazed black' },
  { c: '#7a4a35', n: 'Sanshu red' },
  { c: '#2f4a5e', n: 'Ao blue' },
  { c: '#c7982f', n: 'Imperial yellow' },
  { c: '#4a5e50', n: 'Moss green' },
  { c: '#2b2e33', n: 'Charcoal' },
];

function ColorRow(p: { label: string; value: string; onChange: (v: string) => void; swatches?: { c: string; n: string }[] }) {
  return (
    <div className="color-row">
      <span className="slider-label">{p.label}</span>
      <div className="swatches">
        {p.swatches?.map((s) => (
          <button key={s.c} title={s.n} className={`sw ${p.value.toLowerCase() === s.c ? 'on' : ''}`} style={{ background: s.c }} onClick={() => p.onChange(s.c)} />
        ))}
        <input type="color" value={p.value} onChange={(e) => p.onChange(e.target.value)} title="Custom color" />
      </div>
    </div>
  );
}

export default function Panel({ params, onChange, onPreset, onRegion, checks, stats, onExportGLB, onExportJSON }: Props) {
  const styles: RoofStyle[] = ['kirizuma', 'yosemune', 'irimoya', 'hogyo'];
  const regions: { id: Region; kanji: string; name: string }[] = [
    { id: 'japan', kanji: '日本', name: 'Japan' },
    { id: 'china', kanji: '中国', name: 'China' },
    { id: 'korea', kanji: '韓国', name: 'Korea' },
  ];
  const tiles: { id: TileSystem; name: string; sub: string }[] = [
    { id: 'hongawara', name: 'Hongawara', sub: '本瓦葺' },
    { id: 'sangawara', name: 'Sangawara', sub: '桟瓦葺' },
    { id: 'modern', name: 'Modern flat', sub: '和モダン' },
  ];

  return (
    <div className="panel">
      <Section title="Presets">
        <div className="preset-grid">
          {PRESETS.map((pr) => (
            <button key={pr.id} className="preset" onClick={() => onPreset(pr.id)}>
              <b>{pr.label}</b>
              <span>{pr.sub}</span>
            </button>
          ))}
        </div>
      </Section>

      <Section title="Roof style 屋根">
        <div className="style-grid">
          {styles.map((s) => (
            <button key={s} className={`style-card ${params.style === s ? 'on' : ''}`} onClick={() => onChange({ style: s })}>
              <span className="kanji">{STYLE_META[s].kanji}</span>
              <b>{STYLE_META[s].name}</b>
              <span className="sub">{STYLE_META[s].sub}</span>
            </button>
          ))}
        </div>
        <div className="seg-row">
          {regions.map((r) => (
            <button key={r.id} className={`seg ${params.region === r.id ? 'on' : ''}`} onClick={() => onRegion(r.id)} title="Applies regional curve defaults">
              {r.kanji} {r.name}
            </button>
          ))}
        </div>
        <p className="hint">Region applies traditional curve defaults (sori · corner lift · overhang). CN 歇山 / KR 팔작 ≈ irimoya.</p>
      </Section>

      <Section title="Dimensions 寸法">
        <Slider label="Width" value={params.width} min={3} max={10} step={0.1} unit=" m" onChange={(v) => onChange({ width: v })} />
        <Slider label="Depth" value={params.depth} min={3} max={10} step={0.1} unit=" m" onChange={(v) => onChange({ depth: v })} />
        <Slider label="Wall height" value={params.wallHeight} min={2} max={4.2} step={0.05} unit=" m" onChange={(v) => onChange({ wallHeight: v })} />
        <Slider label="Eave overhang" value={params.overhang} min={0.15} max={1.6} step={0.05} unit=" m" onChange={(v) => onChange({ overhang: v })} />
        <Slider label="Seed" value={params.seed} min={1} max={99} step={1} onChange={(v) => onChange({ seed: Math.round(v) })} />
      </Section>

      <Section title="Roof geometry 反り">
        <Slider label="Pitch (rise/run)" value={params.pitch} min={0.2} max={1.2} step={0.01} onChange={(v) => onChange({ pitch: v })} />
        <Slider label="Slope curve · sori" value={params.sori} min={0} max={0.45} step={0.01} onChange={(v) => onChange({ sori: v })} />
        <Slider label="Corner lift" value={params.cornerLift} min={0} max={0.45} step={0.01} unit=" m" onChange={(v) => onChange({ cornerLift: v })} />
        <Slider label="Hip curl" value={params.hipSori} min={0} max={0.3} step={0.01} unit=" m" onChange={(v) => onChange({ hipSori: v })} />
        {params.style === 'irimoya' && (
          <Slider label="Gable share" value={params.gableFraction} min={0.2} max={0.75} step={0.01} onChange={(v) => onChange({ gableFraction: v })} />
        )}
      </Section>

      <Section title="Tiles 瓦">
        <div className="seg-row">
          {tiles.map((t) => (
            <button key={t.id} className={`seg ${params.tile === t.id ? 'on' : ''}`} onClick={() => onChange({ tile: t.id })}>
              {t.name}<span className="seg-sub">{t.sub}</span>
            </button>
          ))}
        </div>
        <ColorRow label="Tile color" value={params.roofColor} swatches={TILE_SWATCHES} onChange={(v) => onChange({ roofColor: v })} />
        <ColorRow label="Ridge color" value={params.ridgeColor} onChange={(v) => onChange({ ridgeColor: v })} />
        <Slider label="Glaze" value={params.tileGlaze} min={0} max={1} step={0.05} onChange={(v) => onChange({ tileGlaze: v })} />
        <Toggle label="Eave end caps (nokigawara)" value={params.eaveCaps} onChange={(v) => onChange({ eaveCaps: v })} />
      </Section>

      <Section title="Timber & walls 木材">
        <ColorRow label="Wood" value={params.woodColor} onChange={(v) => onChange({ woodColor: v })} />
        <ColorRow label="Plaster" value={params.wallColor} onChange={(v) => onChange({ wallColor: v })} />
        <Slider label="Rafter spacing" value={params.rafterSpacing} min={0.3} max={0.9} step={0.005} unit=" m" onChange={(v) => onChange({ rafterSpacing: v })} />
      </Section>

      <Section title="Structure & ornaments 構造">
        <Toggle label="Rafters (taruki 垂木)" value={params.showRafters} onChange={(v) => onChange({ showRafters: v })} />
        <Toggle label="Beams & purlins" value={params.showStructure} onChange={(v) => onChange({ showStructure: v })} />
        <Toggle label="Walls (mounting context)" value={params.showWalls} onChange={(v) => onChange({ showWalls: v })} />
        <Toggle label="Onigawara demon tiles" value={params.onigawara} onChange={(v) => onChange({ onigawara: v })} />
        <Toggle label="Apex finial (hōgyō)" value={params.finial} onChange={(v) => onChange({ finial: v })} />
        <Toggle label="Karahafu cusped gables" value={params.karahafu} onChange={(v) => onChange({ karahafu: v })} />
      </Section>

      <Section title="Verification 検証">
        {stats && (
          <div className="stat-grid">
            <div><b>{stats.tileCount.toLocaleString()}</b><span>tiles</span></div>
            <div><b>{stats.rafterCount}</b><span>rafters</span></div>
            <div><b>{stats.tileArea.toFixed(1)} m²</b><span>tile area</span></div>
            <div><b>{(stats.weightKg / 1000).toFixed(2)} t</b><span>tile weight</span></div>
            <div><b>{stats.eaveY.toFixed(2)} m</b><span>eave height</span></div>
            <div><b>{stats.topY.toFixed(2)} m</b><span>ridge height</span></div>
          </div>
        )}
        <ul className="checks">
          {checks.map((ch) => (
            <li key={ch.id} className={ch.status}>
              <span className={`dot ${ch.status}`} />
              <div><b>{ch.label}</b><p>{ch.detail}</p></div>
            </li>
          ))}
        </ul>
      </Section>

      <Section title="Export 出力">
        <div className="export-row">
          <button className="btn primary" onClick={onExportGLB}>Download .GLB</button>
          <button className="btn" onClick={onExportJSON}>Params .JSON</button>
        </div>
        <p className="hint">GLB imports directly into Blender (File → Import → glTF) for use with Geometry Nodes setups.</p>
      </Section>

      <Section title="Roadmap 工程">
        <ul className="roadmap">
          <li className="done"><span>Phase 1 · Roofs</span><em>done</em></li>
          <li><span>Phase 2 · Walls, floors, building types (machiya / store / office)</span><em>next</em></li>
          <li><span>Phase 3 · Openings, lattices, noren & signboards</span><em>planned</em></li>
          <li><span>Phase 4 · Lights, utility poles & wiring</span><em>planned</em></li>
          <li><span>Phase 5 · Furniture & street props</span><em>planned</em></li>
        </ul>
      </Section>

      <Section title="Research notes 考証">
        <details className="research">
          <summary>Sources & schematics used</summary>
          <ul>
            <li>4 canonical types: kirizuma / yosemune / irimoya / hōgyō — <i>adayofzen.com</i>, <i>meguri-japan.com</i></li>
            <li>hongawara = hiragawara + marugawara; sangawara S-pantile ~1650s; ~104 kg/m² — <i>gov-online.go.jp (Nihongawara)</i></li>
            <li>Onigawara ≈ 28 cm tall ridge-end ornament — <i>Japan Antique Roadshow</i></li>
            <li>Sori: hip rafters curled up from the straight line — <i>thecarpentryway.blog</i></li>
            <li>Nawadarumi slack-rope curve; concave juzhe + flying rafters + corner upturn — <i>engineerfix.com, MDPI Buildings 2025</i></li>
            <li>CN hierarchy wudian &gt; xieshan &gt; xuanshan &gt; yingshan; 45° hips → equal pitch — <i>ResearchGate (Shen et al.)</i></li>
            <li>Rafter pitch default 455 mm (1.5 shaku); wall plate + ridge beam (munagi) + king post framing.</li>
          </ul>
        </details>
      </Section>
    </div>
  );
}
