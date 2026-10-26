// Frontier Editor-style chrome: outliner (left) · viewport (centre) · inspector cards (right).
// Vanilla DOM so the page runs straight from a static host with no build step.

import { groups, presets, palettes, outlinerSections } from './params.js';
import { splineLength } from './features.js';
import { icon, groupIcons, cardIcons } from './icons.js';


export class Editor {
  constructor(root, { values, onChange, onPreset, onGenerate, onReset, onAction }) {
    this.root = root;
    this.values = values;
    this.onChange = onChange;
    this.onPreset = onPreset;
    this.onGenerate = onGenerate;
    this.onReset = onReset;
    this.onAction = onAction;
    this.selected = 'landform';
    this.pending = false;
    this.activePreset = null;
    this.buildShell();
    this.renderOutliner();
    this.renderInspector();
  }

  buildShell() {
    this.root.innerHTML = `
      <main class="shell">
        <aside class="outliner">
          <div class="brand"><div class="brand-symbol">${icon('layers', 24, 1.5)}</div><span>frontier<span class="brand-dot">.</span></span><span class="version">ENGINE / 01</span></div>
          <div class="scene-label">WORKSPACE <span class="status-dot"></span></div>
          <div class="scene-title"><span>Terrain generator</span><span class="scene-extension">.scene</span></div>
          <div class="outliner-heading"><h2>Outliner <span>${String(groups.length).padStart(2, '0')}</span></h2></div>
          <div class="outliner-tree"></div>
          <div class="outliner-bottom"><div class="world-icon">${icon('mountain', 20)}</div><div><strong>Escarpment study</strong><span>Local project</span></div><span class="little-dot"></span></div>
        </aside>
        <section class="viewport">
          <div class="viewport-top">
            <div class="crumbs">${icon('mountain', 15)} Terrain <span class="crumb-slash">/</span> <span class="crumb-current">Landform</span></div>
            <div class="progress-wrap"><span class="progress-phase"></span><div class="progress-track"><i></i></div></div>
            <div class="viewport-actions">
              <button class="icon-button" data-action="frame" title="Frame terrain">${icon('rotate', 16)}</button>
              <button class="icon-button" data-action="screenshot" title="Screenshot">${icon('camera', 16)}</button>
              <button class="generate" title="Ctrl + Enter">${icon('play', 13, 2)}<span>Generate</span></button>
            </div>
          </div>
          <div class="canvas-wrap"><canvas id="viewport-canvas"></canvas></div>
          <div class="draw-bar">
            <button data-tool="road" title="Draw a road: click points on the terrain, Enter / double-click to finish">${icon('route', 14)}<span>Road</span></button>
            <button data-tool="river" title="Draw a river: click from source to mouth, Enter / double-click to finish">${icon('waves', 14)}<span>River</span></button>
            <button data-tool="lake" title="Place a lake: click the terrain where it should fill">${icon('droplets', 14)}<span>Lake</span></button>
            <span class="draw-sep"></span>
            <button data-tool-action="finish" title="Finish (Enter)">${icon('check', 14)}<span>Finish</span></button>
            <button data-tool-action="undo" title="Remove last point (Backspace)">${icon('rotate', 14)}<span>Undo</span></button>
            <button data-tool-action="cancel" title="Cancel (Esc)">${icon('x', 14)}<span>Cancel</span></button>
            <span class="draw-hint"></span>
          </div>
          <div class="viewport-hud"></div>
        </section>
        <section class="inspector">
          <div class="inspector-top"><div class="inspector-crumbs"></div><div class="save-status saved"><span class="unsaved-dot"></span><span class="save-text">Saved</span></div></div>
          <div class="inspector-content"></div>
        </section>
      </main>`;
    this.tree = this.root.querySelector('.outliner-tree');
    this.content = this.root.querySelector('.inspector-content');
    this.canvas = this.root.querySelector('#viewport-canvas');
    this.hud = this.root.querySelector('.viewport-hud');
    this.progressPhase = this.root.querySelector('.progress-phase');
    this.progressBar = this.root.querySelector('.progress-track i');
    this.progressWrap = this.root.querySelector('.progress-wrap');
    this.drawBar = this.root.querySelector('.draw-bar');
    this.drawHint = this.root.querySelector('.draw-hint');
    this.tool = null;
    this.drawBar.querySelectorAll('[data-tool]').forEach((b) => b.addEventListener('click', () => this.onAction('tool', b.dataset.tool === this.tool ? null : b.dataset.tool)));
    this.drawBar.querySelectorAll('[data-tool-action]').forEach((b) => b.addEventListener('click', () => this.onAction(`tool-${b.dataset.toolAction}`)));
    this.generateButton = this.root.querySelector('.generate');
    this.generateButton.addEventListener('click', () => this.onGenerate());
    this.root.querySelectorAll('.viewport-actions [data-action]').forEach((b) => b.addEventListener('click', () => this.onAction(b.dataset.action)));
    window.addEventListener('keydown', (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); this.onGenerate(); }
    });
  }

  renderOutliner() {
    this.tree.innerHTML = outlinerSections.map((section) => `
      <div class="group">
        <div class="group-label">${section.label}<span class="group-count">${section.ids.length}</span></div>
        ${section.ids.map((id) => {
          const g = groups.find((x) => x.id === id);
          return `<div class="tree-row ${id === this.selected ? 'selected' : ''}" data-id="${id}">
            <button class="object-button" style="--accent:${g.color}">${icon(groupIcons[id], 17)}<span><span class="object-name">${g.name}</span><small>${g.type}</small></span>${id === this.selected ? '<span class="selected-dot"></span>' : ''}</button>
          </div>`;
        }).join('')}
      </div>`).join('');
    this.tree.querySelectorAll('.tree-row').forEach((row) => row.addEventListener('click', () => this.select(row.dataset.id)));
  }

  select(id) {
    this.selected = id;
    this.renderOutliner();
    this.renderInspector();
  }

  format(val, step) {
    const digits = step >= 1 ? 0 : step >= 0.1 ? 1 : step >= 0.01 ? 2 : 3;
    return Number(val).toLocaleString('en-US', { maximumFractionDigits: digits });
  }

  renderInspector() {
    const g = groups.find((x) => x.id === this.selected);
    const v = this.values;
    const controlCount = g.cards.reduce((n, c) => n + (c.controls ? c.controls.length : 1), 0);
    this.root.querySelector('.crumb-current').textContent = g.name;
    this.root.querySelector('.inspector-crumbs').innerHTML = `${icon(groupIcons[g.id], 15)} ${g.name} <span>/</span> <span>${g.type}</span>`;

    const cards = g.cards.map((card) => {
      let body = '';
      if (card.kind === 'presets') {
        body = `<p class="muted">Each preset reconfigures relief, strata, erosion, rocks and material together.</p>
          <div class="preset-list">${Object.keys(presets).map((name) => `<button class="preset ${this.activePreset === name ? 'active' : ''}" data-preset="${name}"><span>${name}</span>${icon('sparkles', 12)}</button>`).join('')}</div>`;
      } else if (card.kind === 'palette') {
        body = `<p class="muted">Lithology drives the colour family of beds, fresh breaks and oxide staining.</p>
          <div class="pill-row">${Object.entries(palettes).map(([key, p]) => `<button class="enabled-pill ${v.palette === key ? '' : 'disabled'}" data-palette="${key}"><span style="background:${p.rockA}"></span>${p.name}</button>`).join('')}</div>
          <p class="muted">Picking a type fills the colour swatches below — edit them freely afterwards.</p>`;
      } else if (card.kind === 'draw') {
        body = `<p class="muted">Pick a tool, then click on the terrain. Roads and rivers are splines — click points along the route, <b>Enter</b> or double-click to finish, <b>Backspace</b> removes the last point, <b>Esc</b> cancels. A lake is a single click: it fills to the clicked ground height plus the offset below. Rivers re-run erosion (the water carves its own bed); roads and lakes apply to the mesh immediately.</p>
          <div class="pill-row">
            <button class="enabled-pill ${this.tool === 'road' ? '' : 'disabled'}" data-pick-tool="road"><span></span>Draw road</button>
            <button class="enabled-pill ${this.tool === 'river' ? '' : 'disabled'}" data-pick-tool="river"><span></span>Draw river</button>
            <button class="enabled-pill ${this.tool === 'lake' ? '' : 'disabled'}" data-pick-tool="lake"><span></span>Place lake</button>
          </div>`;
      } else if (card.kind === 'featureList') {
        const f = v.features || { roads: [], rivers: [], lakes: [] };
        const rows = [];
        (f.roads || []).forEach((r, i) => rows.push(`<div class="feature-row"><span class="feature-swatch" style="background:#f2e6c8"></span><span>Road ${i + 1}<small>${r.points.length} pts · ${Math.round(splineLength(r.points))} m</small></span><button data-feature-delete="roads:${r.id}" title="Delete">${icon('x', 12)}</button></div>`));
        (f.rivers || []).forEach((r, i) => rows.push(`<div class="feature-row"><span class="feature-swatch" style="background:#6fc3ff"></span><span>River ${i + 1}<small>${r.points.length} pts · ${Math.round(splineLength(r.points))} m</small></span><button data-feature-delete="rivers:${r.id}" title="Delete">${icon('x', 12)}</button></div>`));
        (f.lakes || []).forEach((l, i) => rows.push(`<div class="feature-row lake"><span class="feature-swatch" style="background:#3f8fc4"></span><span>Lake ${i + 1}<small>level</small></span><input type="range" data-lake-level="${l.id}" min="${Math.round(l.level - 80)}" max="${Math.round(l.level + 80)}" step="1" value="${l.level}"><code>${Math.round(l.level)} m</code><button data-feature-delete="lakes:${l.id}" title="Delete">${icon('x', 12)}</button></div>`));
        body = rows.length ? `<div class="feature-list">${rows.join('')}</div><div class="pill-row"><button class="enabled-pill disabled" data-feature-clear><span></span>Clear all</button></div>`
          : '<p class="muted">Nothing drawn yet.</p>';
      } else if (card.kind === 'export') {
        body = `<p class="muted">Geometry exports include the displaced terrain block and every rock instance. Map exports share the same top-down frame so heightmap, satmap, splat masks and normal map line up pixel-for-pixel.</p>
          <div class="export-list">
            <button data-action="export-obj">${icon('download', 14)}<span>Export OBJ</span><small>Wavefront · metres · Y-up</small></button>
            <button data-action="export-glb">${icon('download', 14)}<span>Export GLB</span><small>Binary glTF</small></button>
            <button data-action="export-heightmap">${icon('download', 14)}<span>Heightmap PNG</span><small>16-bit packed · R·G high/low byte</small></button>
            <button data-action="export-satmap">${icon('download', 14)}<span>Satmap PNG</span><small>Top-down lit colour, 2048², rocks included</small></button>
            <button data-action="export-masks">${icon('download', 14)}<span>Splat masks PNG</span><small>R rock · G scree · B wetness · A hardness</small></button>
            <button data-action="export-normal">${icon('download', 14)}<span>Normal map PNG</span><small>Tangent-space, Y-up heightfield</small></button>
            <button data-action="screenshot">${icon('camera', 14)}<span>Screenshot</span><small>Viewport PNG</small></button>
          </div>`;
      } else {
        body = card.controls.map(([key, label, min, max, step, unit, hint]) => {
          if (unit === 'enum') {
            const opts = hint.split('|');
            return `<div class="control"><div class="control-line"><span>${label}</span></div><div class="pill-row">${opts.map((o, i) => `<button class="enabled-pill ${Number(v[key]) === i ? '' : 'disabled'}" data-enum="${key}" data-index="${i}"><span></span>${o}</button>`).join('')}</div><p class="muted">Renders a single layer on its own so its pattern and scale can be judged; set back to Off for the full material.</p></div>`;
          }
          if (unit === 'color') {
            return `<div class="control-line color-line"><span>${label}${hint ? `<small> · ${hint}</small>` : ''}</span><label class="color-chip" style="--chip:${v[key]}"><input type="color" data-color="${key}" value="${v[key]}" aria-label="${label}"><code>${v[key]}</code></label></div>`;
          }
          const isToggle = min === 0 && max === 1 && step === 1;
          if (isToggle) {
            return `<div class="control-line toggle-line"><span>${label}${hint ? `<small> · ${hint}</small>` : ''}</span><button class="toggle ${v[key] ? 'on' : ''}" data-key="${key}" aria-label="${label}"><span></span></button></div>`;
          }
          const pct = ((v[key] - min) / (max - min)) * 100;
          return `<div class="control">
            <div class="control-line"><span>${label}</span><span class="control-value" data-value="${key}">${this.format(v[key], step)}<small>${unit}</small></span></div>
            <input type="range" data-key="${key}" min="${min}" max="${max}" step="${step}" value="${v[key]}" style="--progress:${pct}%" aria-label="${label}">
            ${hint ? `<p class="muted">${hint}</p>` : ''}
          </div>`;
        }).join('');
      }
      return `<section class="card" style="--card-icon:${g.color}"><div class="card-heading"><span>${icon(cardIcons[card.title] || 'sliders', 16)}${card.title}</span>${card.controls ? `<span class="small-pill">${card.controls.length}</span>` : ''}</div><div class="card-body">${body}</div></section>`;
    }).join('');

    const stageNote = g.stage === 'terrain' ? 'Press Generate to rebuild the heightfield' : g.stage === 'rocks' ? 'Rocks re-scatter automatically' : g.stage === 'mesh' ? 'Mesh rebuilds automatically' : 'Changes apply in real time';
    this.content.innerHTML = `
      <div class="object-header">
        <div class="object-title"><div class="object-icon" style="--accent:${g.color}">${icon(groupIcons[g.id], 30, 1.25)}</div><div><div class="eyebrow">${g.type}</div><h1>${g.name}</h1></div></div>
        <div class="object-actions"><button class="reset" data-reset>${icon('rotate', 12)} Reset group</button><span class="enabled-pill"><span></span>${g.stage === 'terrain' ? 'Heightfield' : g.stage === 'rocks' ? 'Scatter' : g.stage === 'mesh' ? 'Mesh' : 'Live'}</span></div>
      </div>
      <div class="section-label"><span>PROPERTIES</span><span>${controlCount} controls</span></div>
      <div class="cards">${cards}</div>
      <footer class="inspector-footer"><span><span class="footer-dot"></span>${stageNote}</span><span>${g.name} <span class="footer-slash">/</span> ${g.type}</span></footer>`;

    this.content.querySelectorAll('input[type=range]').forEach((input) => {
      input.addEventListener('input', () => {
        const key = input.dataset.key;
        const val = Number(input.value);
        const min = Number(input.min), max = Number(input.max);
        input.style.setProperty('--progress', `${((val - min) / (max - min)) * 100}%`);
        const out = this.content.querySelector(`[data-value="${key}"]`);
        if (out) out.firstChild.textContent = this.format(val, Number(input.step));
        this.activePreset = null;
        this.onChange(key, val);
      });
    });
    this.content.querySelectorAll('.toggle').forEach((btn) => btn.addEventListener('click', () => {
      const key = btn.dataset.key;
      const val = this.values[key] ? 0 : 1;
      btn.classList.toggle('on', !!val);
      this.onChange(key, val);
    }));
    this.content.querySelectorAll('input[type=color]').forEach((input) => {
      input.addEventListener('input', () => {
        const key = input.dataset.color;
        input.parentElement.style.setProperty('--chip', input.value);
        input.parentElement.querySelector('code').textContent = input.value;
        this.activePreset = null;
        this.onChange(key, input.value);
      });
    });
    this.content.querySelectorAll('[data-pick-tool]').forEach((btn) => btn.addEventListener('click', () => this.onAction('tool', btn.dataset.pickTool === this.tool ? null : btn.dataset.pickTool)));
    this.content.querySelectorAll('[data-feature-delete]').forEach((btn) => btn.addEventListener('click', () => { this.onAction('feature-delete', btn.dataset.featureDelete); this.renderInspector(); }));
    const clear = this.content.querySelector('[data-feature-clear]');
    if (clear) clear.addEventListener('click', () => { this.onAction('feature-clear'); this.renderInspector(); });
    this.content.querySelectorAll('[data-lake-level]').forEach((input) => {
      input.addEventListener('input', () => {
        input.parentElement.querySelector('code').textContent = `${Math.round(Number(input.value))} m`;
        this.onAction('lake-level', { id: input.dataset.lakeLevel, level: Number(input.value) });
      });
    });
    this.content.querySelectorAll('[data-enum]').forEach((btn) => btn.addEventListener('click', () => {
      this.onChange(btn.dataset.enum, Number(btn.dataset.index));
      this.renderInspector();
    }));
    this.content.querySelectorAll('[data-preset]').forEach((btn) => btn.addEventListener('click', () => {
      this.activePreset = btn.dataset.preset;
      this.onPreset(btn.dataset.preset);
      this.renderInspector();
    }));
    this.content.querySelectorAll('[data-palette]').forEach((btn) => btn.addEventListener('click', () => {
      this.onChange('palette', btn.dataset.palette);
      this.renderInspector();
    }));
    this.content.querySelectorAll('[data-action]').forEach((btn) => btn.addEventListener('click', () => this.onAction(btn.dataset.action)));
    const reset = this.content.querySelector('[data-reset]');
    if (reset) reset.addEventListener('click', () => { this.onReset(g); this.renderInspector(); });
  }

  setTool(tool, draftCount = 0) {
    this.tool = tool;
    this.drawBar.classList.toggle('active', !!tool);
    this.drawBar.querySelectorAll('[data-tool]').forEach((b) => b.classList.toggle('on', b.dataset.tool === tool));
    this.canvas.classList.toggle('drawing', !!tool);
    const hints = {
      road: draftCount ? `${draftCount} point${draftCount > 1 ? 's' : ''} · Enter / double-click to finish` : 'Click the terrain to start the road',
      river: draftCount ? `${draftCount} point${draftCount > 1 ? 's' : ''} · draw downstream · Enter to finish` : 'Click at the source, then along the valley',
      lake: 'Click where the lake should fill',
    };
    this.drawHint.textContent = tool ? hints[tool] : '';
    if (this.selected === 'features') this.renderInspector();
  }

  setPending(pending) {
    this.pending = pending;
    this.generateButton.classList.toggle('pending', pending);
    const status = this.root.querySelector('.save-status');
    status.classList.toggle('saved', !pending);
    this.root.querySelector('.save-text').textContent = pending ? 'Heightfield out of date · Generate' : 'Up to date';
  }

  setProgress(phase, fraction) {
    if (phase == null) {
      this.progressWrap.classList.remove('active');
      return;
    }
    this.progressWrap.classList.add('active');
    this.progressPhase.textContent = phase;
    this.progressBar.style.width = `${Math.round(fraction * 100)}%`;
  }

  setStats(stats) {
    this.hud.innerHTML = Object.entries(stats).map(([k, val]) => `<div><span>${k}</span><strong>${val}</strong></div>`).join('');
  }
}
