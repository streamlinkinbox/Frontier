import { RoofParams, RoofStyle, Region, TileSystem, Ornament, LampDesign, LampGroup, LampMount, PRESETS, STYLE_META, LAMP_META, STONE_DESIGNS } from '../lib/types';
import { Check, RoofStats } from '../lib/buildRoof';

interface Props {
  params: RoofParams;
  onChange: (patch: Partial<RoofParams>) => void;
  onLamp: (index: number, patch: Partial<LampGroup>) => void;
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

function Slider(p: { label: string; value: number; min: number; max: number; step: number; unit?: string; int?: boolean; onChange: (v: number) => void }) {
  const shown = p.int ? `${Math.round(p.value)}` : `${p.value.toFixed(2)}${p.unit ?? ''}`;
  return (
    <label className="slider-row">
      <span className="slider-label">{p.label}<em>{shown}</em></span>
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
  { c: '#d2792b', n: 'Glazed orange' },
  { c: '#c7982f', n: 'Imperial yellow' },
  { c: '#4a5e50', n: 'Moss green' },
  { c: '#2b2e33', n: 'Charcoal' },
];

const PAPER_SWATCHES = [
  { c: '#c33b2a', n: 'Aka red' },
  { c: '#f2e4c8', n: 'Washi cream' },
  { c: '#f6f2e8', n: 'White' },
  { c: '#2b2e33', n: 'Black' },
  { c: '#2f4a5e', n: 'Ai blue' },
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

const LAMP_LETTERS = ['A', 'B', 'C'];

function LampCard({ index, lamp, onLamp }: { index: number; lamp: LampGroup; onLamp: (patch: Partial<LampGroup>) => void }) {
  const paperDesigns = (Object.keys(LAMP_META) as LampDesign[]).filter((d) => LAMP_META[d].cat === 'paper');
  const stoneDesigns = (Object.keys(LAMP_META) as LampDesign[]).filter((d) => LAMP_META[d].cat === 'stone');
  const mounts: { id: LampMount; name: string }[] = [
    { id: 'hanging', name: 'Hanging' },
    { id: 'standing', name: 'Standing' },
    { id: 'stone', name: 'Stone' },
  ];
  const pickDesign = (d: LampDesign) => {
    if (STONE_DESIGNS.includes(d)) onLamp({ design: d, mount: 'stone' });
    else if (lamp.mount === 'stone') onLamp({ design: d, mount: 'standing' });
    else onLamp({ design: d });
  };
  return (
    <div className={`lamp-card ${lamp.enabled ? 'on' : ''}`}>
      <div className="lamp-head">
        <b>Group {LAMP_LETTERS[index] ?? index}</b>
        <button type="button" className={`switch ${lamp.enabled ? 'on' : ''}`} onClick={() => onLamp({ enabled: !lamp.enabled })} aria-pressed={lamp.enabled}>
          <i />
        </button>
      </div>
      {lamp.enabled && (
        <>
          <div className="design-label">Paper & wood 紙</div>
          <div className="design-grid">
            {paperDesigns.map((d) => (
              <button key={d} className={`design ${lamp.design === d ? 'on' : ''}`} onClick={() => pickDesign(d)} title={LAMP_META[d].sub}>
                {LAMP_META[d].name}
              </button>
            ))}
          </div>
          <div className="design-label">Stone & cement 石</div>
          <div className="design-grid cols2">
            {stoneDesigns.map((d) => (
              <button key={d} className={`design ${lamp.design === d ? 'on' : ''}`} onClick={() => pickDesign(d)} title={LAMP_META[d].sub}>
                {LAMP_META[d].name}
              </button>
            ))}
          </div>
          <div className="seg-row">
            {mounts.map((mt) => (
              <button key={mt.id} className={`seg ${lamp.mount === mt.id ? 'on' : ''}`} onClick={() => onLamp({ mount: mt.id })}>
                {mt.name}
              </button>
            ))}
          </div>
          <Slider label="Count" value={lamp.count} min={1} max={8} step={1} int onChange={(v) => onLamp({ count: Math.round(v) })} />
          <Slider label="Size" value={lamp.size} min={0.6} max={1.8} step={0.05} onChange={(v) => onLamp({ size: v })} />
          <Slider label="Glow" value={lamp.glow} min={0} max={3} step={0.1} onChange={(v) => onLamp({ glow: v })} />
          <ColorRow label="Paper" value={lamp.paperColor} swatches={PAPER_SWATCHES} onChange={(v) => onLamp({ paperColor: v })} />
          <ColorRow label="Frame" value={lamp.frameColor} onChange={(v) => onLamp({ frameColor: v })} />
          <label className="text-row">
            <span>Characters on paper</span>
            <input type="text" className="txt" maxLength={8} value={lamp.text} placeholder="祭" onChange={(e) => onLamp({ text: e.target.value })} />
          </label>
        </>
      )}
    </div>
  );
}

export default function Panel({ params, onChange, onLamp, onPreset, onRegion, checks, stats, onExportGLB, onExportJSON }: Props) {
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
  const ornaments: { id: Ornament; name: string; sub: string }[] = [
    { id: 'none', name: 'None', sub: '—' },
    { id: 'onigawara', name: 'Onigawara', sub: '鬼瓦' },
    { id: 'chiwen', name: 'Chiwen', sub: '螭吻' },
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
        <Slider label="Seed" value={params.seed} min={1} max={99} step={1} int onChange={(v) => onChange({ seed: Math.round(v) })} />
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
        <ColorRow label="Painted trim" value={params.trimColor} onChange={(v) => onChange({ trimColor: v })} />
        <Slider label="Rafter spacing" value={params.rafterSpacing} min={0.3} max={0.9} step={0.005} unit=" m" onChange={(v) => onChange({ rafterSpacing: v })} />
      </Section>

      <Section title="Structure & ornaments 構造">
        <div className="seg-row">
          {ornaments.map((o) => (
            <button key={o.id} className={`seg ${params.ornament === o.id ? 'on' : ''}`} onClick={() => onChange({ ornament: o.id })}>
              {o.name}<span className="seg-sub">{o.sub}</span>
            </button>
          ))}
        </div>
        <Toggle label="Rafters (taruki 垂木)" value={params.showRafters} onChange={(v) => onChange({ showRafters: v })} />
        <Toggle label="Beams & purlins" value={params.showStructure} onChange={(v) => onChange({ showStructure: v })} />
        <Toggle label="Walls (mounting context)" value={params.showWalls} onChange={(v) => onChange({ showWalls: v })} />
        <Toggle label="Bracket sets (dougong 斗拱)" value={params.dougong} onChange={(v) => onChange({ dougong: v })} />
        <Toggle label="Hip beasts (wenshou)" value={params.hipBeasts} onChange={(v) => onChange({ hipBeasts: v })} />
        {params.hipBeasts && (
          <Slider label="Beasts per hip" value={params.beastCount} min={3} max={9} step={2} int onChange={(v) => onChange({ beastCount: Math.round(v) })} />
        )}
        <Toggle label="Apex finial (hōgyō)" value={params.finial} onChange={(v) => onChange({ finial: v })} />
        <Toggle label="Karahafu cusped gables" value={params.karahafu} onChange={(v) => onChange({ karahafu: v })} />
      </Section>

      <Section title="Lanterns 提灯">
        <Toggle label="Real point lights (off = emissive only)" value={params.lampLights} onChange={(v) => onChange({ lampLights: v })} />
        {params.lamps.map((lamp, i) => (
          <LampCard key={i} index={i} lamp={lamp} onLamp={(patch) => onLamp(i, patch)} />
        ))}
        <p className="hint">Hanging groups tie cords under the slopes; standing rows plant by the entrance; stone groups set out in the garden. Try Night mode in the viewer.</p>
      </Section>

      <Section title="Verification 検証">
        {stats && (
          <div className="stat-grid">
            <div><b>{stats.tileCount.toLocaleString()}</b><span>tiles</span></div>
            <div><b>{stats.rafterCount}</b><span>rafters</span></div>
            <div><b>{stats.tileArea.toFixed(1)} m²</b><span>tile area</span></div>
            <div><b>{(stats.weightKg / 1000).toFixed(2)} t</b><span>tile weight</span></div>
            <div><b>{stats.eaveY.toFixed(2)} m</b><span>eave height</span></div>
            <div><b>{stats.lamps}</b><span>lanterns</span></div>
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
          <li className="done"><span>Phase 1 · Roofs + ornaments + lanterns</span><em>done</em></li>
          <li><span>Phase 2 · Walls, floors, building types (machiya / store / office)</span><em>next</em></li>
          <li><span>Phase 3 · Openings, lattices, noren & signboards</span><em>planned</em></li>
          <li><span>Phase 4 · Utility poles & wiring</span><em>planned</em></li>
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
            <li>CN hierarchy wudian › xieshan › xuanshan › yingshan; 45° hips → equal pitch — <i>ResearchGate (Shen et al.)</i></li>
            <li>Chiwen ridge-end beasts "swallow" the ridge, spout water against fire — <i>Prince Kung's Palace Museum (pgm.org.cn)</i></li>
            <li>Hip beasts in odd numbers 1–9 by building rank, max at Hall of Supreme Harmony — <i>ibiblio.org (Chinese Culture)</i></li>
            <li>Dougong: layered column-top + intermediate bracket sets under eaves — <i>Datong Guandi Temple (baike.baidu)</i></li>
            <li>Andon (Edo box lamp), chōchin (spiral bamboo, hung outside; aka = izakaya) — <i>HandWiki / Wikipedia</i></li>
            <li>Stone tōrō 6-part: base, pillar, platform, fire box, roof, hōju jewel — <i>Millennium Gallery JP</i></li>
            <li>Square paper lanterns on entrance racks; kago/odawara/bura/tsuri chōchin forms — <i>hayakawajunpei (Views of Japan)</i></li>
            <li>Bonbori: hexagonal festival lamp, hung from wire or stood on a post — <i>skdesu.com</i></li>
            <li>Zou-ma-deng: Song-dynasty carousel, convection-driven paper-horse wheel — <i>baike.baidu (Revolving Lanterns)</i></li>
            <li>Cheongsachorong: red-and-blue silk shade, weddings & rites — <i>paper-capers / Alamy</i></li>
            <li>Kasuga-dōrō: tall slender pedestal, hex/octagonal kasa; yukimi = broad snow roof — <i>magicstonegarden / Schneible Fine Arts</i></li>
            <li>Rafter pitch default 455 mm (1.5 shaku); wall plate + ridge beam (munagi) + king post framing.</li>
          </ul>
        </details>
      </Section>
    </div>
  );
}
