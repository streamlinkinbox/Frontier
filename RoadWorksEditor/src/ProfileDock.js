//============================================================================================================================================
//                                                            PROFILEDOCK.JS
//============================================================================================================================================
// The long section at the foot of the viewport: chainage left to right, elevation up, drawn on a plain 2D canvas.
//
// It is an editor, not a readout. Every control point of the selected corridor appears as a handle that can be
// dragged up and down to change its elevation, with the grade between handles printed on the line and any stretch
// that fails its design checks shaded behind it. Vertical exaggeration is a view setting — a 4 % grade is invisible
// at 1:1 — and the figures quoted are always the true ones.

import { profileOf, designChecks, profileSummary, DESIGN_SPEEDS } from './Alignment.js?v=7';

const PAD = { left: 46, right: 14, top: 14, bottom: 22 };

export class ProfileDock {
  constructor(elements, callbacks = {}) {
    this.canvas = elements.canvas;
    this.checks = elements.checks;
    this.subject = elements.subject;
    this.speedSelect = elements.speed;
    this.exaggerationSelect = elements.exaggeration;
    this.dock = elements.dock;
    this.toggle = elements.toggle;
    this.callbacks = callbacks;
    this.ctx = this.canvas?.getContext ? this.canvas.getContext('2d') : null;
    this.corridor = null;
    this.samples = null;
    this.handles = [];
    this.dragging = null;
    this.hover = -1;
    this.designSpeed = 60;
    this.exaggeration = 5;
    // Starts closed: an empty long section is a 172 px band of nothing across the foot of the viewport. The header
    // strip stays, so it is one click (or V) away.
    this.collapsed = true;

    if (this.speedSelect) {
      this.speedSelect.innerHTML = '';
      for (const speed of DESIGN_SPEEDS) {
        const option = document.createElement('option');
        option.value = String(speed);
        option.textContent = `${speed} km/h`;
        if (speed === this.designSpeed) option.selected = true;
        this.speedSelect.appendChild(option);
      }
      this.speedSelect.addEventListener('change', () => {
        this.designSpeed = Number(this.speedSelect.value) || 60;
        this.callbacks.onSpeed?.(this.designSpeed);
        this.draw();
      });
    }
    this.exaggerationSelect?.addEventListener('change', () => {
      this.exaggeration = Number(this.exaggerationSelect.value) || 5;
      this.draw();
    });
    this.toggle?.addEventListener('click', () => this.setCollapsed(!this.collapsed));

    if (this.canvas) {
      this.canvas.addEventListener('pointerdown', (e) => this.onDown(e));
      this.canvas.addEventListener('pointermove', (e) => this.onMove(e));
      this.canvas.addEventListener('pointerup', (e) => this.onUp(e));
      this.canvas.addEventListener('pointerleave', () => {
        this.hover = -1;
        this.draw();
      });
      window.addEventListener('resize', () => this.draw());
    }
  }

  setCollapsed(collapsed) {
    this.collapsed = collapsed;
    this.dock?.classList.toggle('Collapsed', collapsed);
    if (!collapsed) this.draw();
  }

  // `samples` is the corridor's sampled polyline — the same one the solver uses, so the section matches the mesh.
  setCorridor(corridor, samples) {
    this.corridor = corridor;
    this.samples = samples && samples.length >= 2 ? samples : null;
    // Nothing selected means nothing to plot, so fold the dock away rather than leaving an empty plot open.
    if (!this.corridor && !this.collapsed) this.setCollapsed(true);
    this.draw();
  }

  // ── geometry helpers ────────────────────────────────────────────────────────────────────────────────────────────

  metrics() {
    const w = this.canvas.clientWidth || this.canvas.width || 600;
    const h = this.canvas.clientHeight || this.canvas.height || 160;
    return { w, h, plotW: Math.max(10, w - PAD.left - PAD.right), plotH: Math.max(10, h - PAD.top - PAD.bottom) };
  }

  // Control points carry their own chainage: find where each one lands along the sampled polyline.
  controlStations(profile) {
    const out = [];
    if (!this.corridor || !profile.stations.length) return out;
    this.corridor.points.forEach((point, index) => {
      let best = 0;
      let bestDist = Infinity;
      for (const station of profile.stations) {
        const d = Math.hypot(station.point.x - point.x, station.point.y - point.y);
        if (d < bestDist) {
          bestDist = d;
          best = station.s;
        }
      }
      out.push({ index, s: best, z: point.z, point });
    });
    return out;
  }

  // ── drawing ─────────────────────────────────────────────────────────────────────────────────────────────────────

  draw() {
    if (!this.ctx || this.collapsed) return;
    const { w, h, plotW, plotH } = this.metrics();
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    if (this.canvas.width !== Math.round(w * dpr) || this.canvas.height !== Math.round(h * dpr)) {
      this.canvas.width = Math.round(w * dpr);
      this.canvas.height = Math.round(h * dpr);
    }
    const ctx = this.ctx;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = '#1a1d22';
    ctx.fillRect(0, 0, w, h);

    if (this.subject) {
      this.subject.textContent = this.corridor ? `${this.corridor.name} · long section` : 'No corridor selected';
    }
    if (!this.corridor || !this.samples) {
      ctx.fillStyle = '#6e737c';
      ctx.font = '11px "DM Sans", system-ui, sans-serif';
      ctx.fillText('Select a corridor to edit its vertical alignment.', PAD.left, PAD.top + 16);
      if (this.checks) this.checks.innerHTML = '';
      this.handles = [];
      return;
    }

    const profile = profileOf(this.samples);
    const issues = designChecks(profile, { designSpeed: this.designSpeed });
    const summary = profileSummary(profile);
    const length = Math.max(1, profile.length);

    // Elevation window, padded and scaled by the exaggeration so gentle grades are still readable.
    let minZ = Infinity;
    let maxZ = -Infinity;
    for (const station of profile.stations) {
      minZ = Math.min(minZ, station.z);
      maxZ = Math.max(maxZ, station.z);
    }
    const mid = (minZ + maxZ) / 2;
    const naturalSpan = Math.max(maxZ - minZ, 0.5);
    const visibleSpan = Math.max(naturalSpan, (length / this.exaggeration) * 0.26);
    const lo = mid - visibleSpan / 2;
    const hi = mid + visibleSpan / 2;
    this.lastSpan = hi - lo; // metres of elevation per plot height, used when dragging a handle

    const X = (s) => PAD.left + (s / length) * plotW;
    const Y = (z) => PAD.top + plotH - ((z - lo) / (hi - lo)) * plotH;

    // grid + axes
    ctx.strokeStyle = '#272b31';
    ctx.lineWidth = 1;
    ctx.font = '9px "DM Sans", system-ui, sans-serif';
    ctx.fillStyle = '#6b707a';
    const zStep = niceStep((hi - lo) / 4);
    for (let z = Math.ceil(lo / zStep) * zStep; z <= hi; z += zStep) {
      const y = Math.round(Y(z)) + 0.5;
      ctx.beginPath();
      ctx.moveTo(PAD.left, y);
      ctx.lineTo(PAD.left + plotW, y);
      ctx.stroke();
      ctx.fillText(`${z.toFixed(zStep < 1 ? 1 : 0)} m`, 6, y + 3);
    }
    const sStep = niceStep(length / 6);
    for (let s = 0; s <= length + 1e-6; s += sStep) {
      const x = Math.round(X(s)) + 0.5;
      ctx.strokeStyle = '#23262c';
      ctx.beginPath();
      ctx.moveTo(x, PAD.top);
      ctx.lineTo(x, PAD.top + plotH);
      ctx.stroke();
      ctx.fillStyle = '#6b707a';
      ctx.fillText(`${s.toFixed(0)}`, x - 8, PAD.top + plotH + 13);
    }

    // offending stretches, shaded behind the line
    for (const issue of issues) {
      const x0 = X(issue.from);
      const x1 = Math.max(x0 + 2, X(issue.to));
      ctx.fillStyle = issue.kind === 'radius' ? '#6b4b2322' : '#8a4a2a2e';
      ctx.fillRect(x0, PAD.top, x1 - x0, plotH);
      ctx.strokeStyle = '#b7823c55';
      ctx.beginPath();
      ctx.moveTo(x0 + 0.5, PAD.top);
      ctx.lineTo(x0 + 0.5, PAD.top + plotH);
      ctx.stroke();
    }

    // existing ground, if the document has one
    const groundZ = this.callbacks.groundZ?.() ?? 0;
    if (groundZ >= lo && groundZ <= hi) {
      ctx.strokeStyle = '#4a4336';
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(PAD.left, Y(groundZ));
      ctx.lineTo(PAD.left + plotW, Y(groundZ));
      ctx.stroke();
      ctx.setLineDash([]);
    }

    // the grade line itself
    ctx.strokeStyle = '#d6a665';
    ctx.lineWidth = 1.6;
    ctx.beginPath();
    profile.stations.forEach((station, i) => {
      const x = X(station.s);
      const y = Y(station.z);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();

    // handles + the grade of each leg
    const controls = this.controlStations(profile);
    this.handles = controls.map((c) => ({ ...c, x: X(c.s), y: Y(c.z) }));
    ctx.font = '9px "DM Sans", system-ui, sans-serif';
    for (let i = 0; i < this.handles.length - 1; i++) {
      const a = this.handles[i];
      const b = this.handles[i + 1];
      const run = b.s - a.s;
      if (run < 1) continue;
      const grade = ((b.z - a.z) / run) * 100;
      ctx.fillStyle = Math.abs(grade) > (issues.find((x) => x.kind === 'grade')?.limit ?? 99) ? '#e0a062' : '#79808b';
      ctx.fillText(`${grade >= 0 ? '+' : ''}${grade.toFixed(1)} %`, (a.x + b.x) / 2 - 12, (a.y + b.y) / 2 - 6);
    }
    this.handles.forEach((handle, i) => {
      const active = this.dragging?.index === handle.index || this.hover === i;
      ctx.fillStyle = active ? '#f0d3a2' : '#d6a665';
      ctx.strokeStyle = '#17191d';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.arc(handle.x, handle.y, active ? 5 : 3.6, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    });

    this.renderChecks(summary, issues);
  }

  renderChecks(summary, issues) {
    if (!this.checks) return;
    const fmt = (v, unit, digits = 1) => (Number.isFinite(v) ? `${v.toFixed(digits)} ${unit}`.trim() : '—');
    const rows = [
      ['Length', fmt(summary.length, 'm', 0)],
      ['Steepest grade', `${summary.maxGrade >= 0 ? '+' : ''}${summary.maxGrade.toFixed(1)} %`],
      ['Rise / fall', `${summary.rise.toFixed(1)} / ${summary.fall.toFixed(1)} m`],
      ['Tightest radius', fmt(summary.minRadius, 'm', 0)],
      ['Min crest K', fmt(summary.minCrest, '', 1)],
      ['Min sag K', fmt(summary.minSag, '', 1)],
    ];
    const list = issues.length
      ? issues.map((issue) => `<p class="Issue">${issue.label}</p>`).join('')
      : `<p class="Ok">Meets the ${this.designSpeed} km/h standards for grade, vertical curvature and plan radius.</p>`;
    this.checks.innerHTML = `<b>Design check</b><dl>${rows
      .map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`)
      .join('')}</dl>${list}`;
  }

  // ── interaction ─────────────────────────────────────────────────────────────────────────────────────────────────

  pick(event) {
    const rect = this.canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    let best = -1;
    let bestDist = 12;
    this.handles.forEach((handle, i) => {
      const d = Math.hypot(handle.x - x, handle.y - y);
      if (d < bestDist) {
        bestDist = d;
        best = i;
      }
    });
    return { index: best, x, y };
  }

  onDown(event) {
    const hit = this.pick(event);
    if (hit.index < 0) return;
    const handle = this.handles[hit.index];
    this.dragging = { index: handle.index, startY: hit.y, startZ: handle.z };
    this.canvas.setPointerCapture?.(event.pointerId);
    this.callbacks.onSelectPoint?.(handle.index);
    this.draw();
  }

  onMove(event) {
    if (!this.dragging) {
      const hit = this.pick(event);
      if (hit.index !== this.hover) {
        this.hover = hit.index;
        this.canvas.style.cursor = hit.index >= 0 ? 'ns-resize' : 'default';
        this.draw();
      }
      return;
    }
    const rect = this.canvas.getBoundingClientRect();
    const y = event.clientY - rect.top;
    const { plotH } = this.metrics();
    const span = this.lastSpan || 1;
    const dz = ((this.dragging.startY - y) / plotH) * span;
    const z = this.dragging.startZ + dz;
    this.callbacks.onElevation?.(this.dragging.index, Math.round(z * 100) / 100);
    this.draw();
  }

  onUp(event) {
    if (!this.dragging) return;
    this.dragging = null;
    this.canvas.releasePointerCapture?.(event.pointerId);
    this.callbacks.onCommit?.();
    this.draw();
  }
}

function niceStep(raw) {
  const pow = Math.pow(10, Math.floor(Math.log10(Math.max(1e-6, raw))));
  const n = raw / pow;
  const step = n <= 1 ? 1 : n <= 2 ? 2 : n <= 5 ? 5 : 10;
  return step * pow;
}
