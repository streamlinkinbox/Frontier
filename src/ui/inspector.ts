// ---------------------------------------------------------------------------
// Frontier UI / inspector — parameter panel for the selected node, plus the
// resolution budget + guard readout (the "don't blur my SDF" dashboard).
// ---------------------------------------------------------------------------

import { GraphNode, ParamValue } from '../graph/types';
import { defFor } from '../graph/registry';
import { Graph } from '../graph/types';
import { budget, evaluateResolution, ResolutionContext, voxelSize } from '../core/resolution';
import { GuardBadge } from './editor';

export interface InspectorApp {
  graph: Graph;
  selectedId: string | null;
  badges: Map<string, GuardBadge>;
  onParamChange(): void;
  onDomainChange(): void;
  onRaiseRes(res: number): void;
}

const RES_OPTIONS = [96, 128, 192, 256, 384, 512];

export class Inspector {
  el: HTMLElement;
  app: InspectorApp;

  constructor(el: HTMLElement, app: InspectorApp) {
    this.el = el;
    this.app = app;
  }

  sync(): void {
    const g = this.app.graph;
    const node = g.nodes.find((n) => n.id === this.app.selectedId) ?? null;
    const parts: string[] = [];

    if (node) {
      const def = defFor(node.type);
      parts.push(`<h3>${node.rename ?? def.label}</h3><div class="sub">${def.subtitle}</div>`);

      if (node.type === 'start') {
        parts.push(this.domainControls());
      } else {
        for (const p of def.params) {
          const v = node.params[p.key] ?? p.default;
          if (p.type === 'float' || p.type === 'int') {
            parts.push(`
              <div class="insp-row">
                <label><span>${p.label}</span><span class="val">${Number(v).toFixed(p.type === 'int' ? 0 : 2)}${p.unit ?? ''}</span></label>
                <input type="range" data-key="${p.key}" min="${p.min}" max="${p.max}" step="${p.step}" value="${v}" />
              </div>`);
          } else if (p.type === 'select') {
            parts.push(`
              <div class="insp-row">
                <label><span>${p.label}</span></label>
                <select data-key="${p.key}">${(p.options ?? []).map((o) => `<option value="${o.value}" ${o.value === v ? 'selected' : ''}>${o.label}</option>`).join('')}</select>
              </div>`);
          } else if (p.type === 'bool') {
            parts.push(`
              <label class="insp-check"><span class="switch ${v ? 'on' : ''}" data-key="${p.key}"></span>${p.label}</label>`);
          } else if (p.type === 'color') {
            const c = v as [number, number, number];
            const hex = '#' + c.map((x) => Math.round(x * 255).toString(16).padStart(2, '0')).join('');
            parts.push(`
              <div class="insp-row">
                <label><span>${p.label}</span></label>
                <input type="color" data-key="${p.key}" value="${hex}" />
              </div>`);
          }
        }
      }

      const badge = this.app.badges.get(node.id);
      if (badge && !badge.ok) {
        parts.push(`<div class="insp-warn">⚠ ${badge.reason}<br/><button data-raise="${badge.requiredRes}">Raise resolution to ${badge.requiredRes}</button></div>`);
      }
    } else {
      parts.push('<h3>Frontier</h3><div class="sub">Select a node to edit its parameters</div>');
    }

    parts.push(this.budgetBlock());
    this.el.innerHTML = parts.join('');
    this.bind();
  }

  private domainControls(): string {
    const d = this.app.graph.domain;
    return `
      <div class="insp-row"><label><span>Seed</span><span class="val">${d.seed}</span></label>
        <input type="range" data-domain="seed" min="1" max="99999" step="1" value="${d.seed}" /></div>
      <div class="insp-row"><label><span>Resolution (X/Z)</span><span class="val">${d.res}</span></label>
        <select data-domain="res">${RES_OPTIONS.map((r) => `<option value="${r}" ${r === d.res ? 'selected' : ''}>${r}³ grid</option>`).join('')}</select></div>
      <div class="insp-row"><label><span>Domain Size</span><span class="val">${d.size[0]} m</span></label>
        <select data-domain="size">${[512, 1024, 2048].map((s) => `<option value="${s}" ${s === d.size[0] ? 'selected' : ''}>${s} × ${s} m</option>`).join('')}</select></div>
      <div class="insp-row"><label><span>Domain Height</span><span class="val">${d.size[1]} m</span></label>
        <input type="range" data-domain="height" min="64" max="1024" step="8" value="${d.size[1]}" /></div>`;
  }

  private budgetBlock(): string {
    const g = this.app.graph;
    const ctx: ResolutionContext = { domainSize: g.domain.size, res: g.domain.res, aspectY: g.domain.size[1] / g.domain.size[0] };
    const b = budget(ctx, ctx.aspectY);
    const lines: string[] = [
      `<div class="insp-section">RESOLUTION BUDGET</div>`,
      `<div class="budget-line"><span>Voxel size</span><b>${b.h.toFixed(2)} m</b></div>`,
      `<div class="budget-line"><span>Grid</span><b>${g.domain.res} × ${Math.round(g.domain.res * ctx.aspectY)} × ${g.domain.res}</b></div>`,
      `<div class="budget-line"><span>Volume memory</span><b>${b.megabytes.toFixed(0)} MB</b></div>`,
      `<div class="budget-line"><span>Band voxels (est)</span><b>${(b.bandVoxelsEstimate / 1e6).toFixed(2)} M</b></div>`,
    ];
    const guarded = g.nodes
      .map((n) => ({ n, req: defFor(n.type).resRequirement?.(n.params, ctx) ?? null }))
      .filter((x) => x.req && x.req.featureMetres > 0);
    if (guarded.length) {
      lines.push(`<div class="insp-section">FEATURE GUARD</div>`);
      for (const { n, req } of guarded) {
        const v = evaluateResolution(ctx, req!);
        const def = defFor(n.type);
        lines.push(`<div class="budget-line"><span style="color:${v.ok ? '#8fbf95' : '#ffb347'}">${v.ok ? '✓' : ''} ${def.label}</span><b>${v.featureVoxels.toFixed(1)} vx</b></div>`);
      }
    }
    return lines.join('');
  }

  private bind(): void {
    const node: GraphNode | null = this.app.graph.nodes.find((n) => n.id === this.app.selectedId) ?? null;
    this.el.querySelectorAll('[data-key]').forEach((input) => {
      const key = (input as HTMLElement).dataset.key!;
      const row = (input as HTMLElement).closest('.insp-row');
      const label = row?.querySelector('.val') as HTMLElement | null;
      const readValue = (): ParamValue => {
        const def = defFor(node!.type);
        const pdef = def.params.find((p) => p.key === key)!;
        if (pdef.type === 'float' || pdef.type === 'int') return Number((input as HTMLInputElement).value);
        if (pdef.type === 'bool') return !(node!.params[key] === true);
        if (pdef.type === 'color') {
          const hex = (input as HTMLInputElement).value;
          return [parseInt(hex.slice(1, 3), 16) / 255, parseInt(hex.slice(3, 5), 16) / 255, parseInt(hex.slice(5, 7), 16) / 255];
        }
        return (input as HTMLSelectElement).value;
      };
      const apply = () => {
        if (!node) return;
        const v = readValue();
        node.params[key] = v;
        if (label && typeof v === 'number') label.textContent = v.toFixed((input as HTMLInputElement).step?.includes('.') ? 2 : 0);
        this.app.onParamChange();
      };
      if (pdefType(input) === 'bool') {
        input.addEventListener('click', () => { apply(); this.sync(); });
      } else if ((input as HTMLInputElement).tagName === 'SELECT' || (input as HTMLInputElement).type === 'color') {
        input.addEventListener('input', () => { apply(); this.sync(); });
      } else {
        // range: live-update value + schedule cook, but NEVER rebuild the DOM
        // mid-drag (that would kill the slider gesture); resync on release
        input.addEventListener('input', apply);
        input.addEventListener('change', () => this.sync());
      }
    });

    this.el.querySelectorAll('[data-domain]').forEach((input) => {
      const key = (input as HTMLElement).dataset.domain!;
      const row = (input as HTMLElement).closest('.insp-row');
      const label = row?.querySelector('.val') as HTMLElement | null;
      const isRange = (input as HTMLInputElement).type === 'range';
      const apply = (commit: boolean) => {
        const d = this.app.graph.domain;
        const val = Number((input as HTMLInputElement).value);
        if (key === 'seed') { d.seed = val; if (label) label.textContent = `${val}`; }
        else if (key === 'res') { d.res = val; if (label) label.textContent = `${val}`; }
        else if (key === 'size') { d.size = [val, d.size[1], val]; if (label) label.textContent = `${val} m`; }
        else if (key === 'height') { d.size = [d.size[0], val, d.size[0]]; if (label) label.textContent = `${val} m`; }
        if (commit || !isRange) this.app.onDomainChange();
      };
      input.addEventListener('input', () => apply(!isRange));
      if (isRange) input.addEventListener('change', () => { apply(true); this.sync(); });
    });

    this.el.querySelectorAll('[data-raise]').forEach((btn) => {
      btn.addEventListener('click', () => this.app.onRaiseRes(Number((btn as HTMLElement).dataset.raise)));
    });
  }
}

function pdefType(input: Element): string {
  return input.classList.contains('switch') ? 'bool' : (input as HTMLInputElement).type ?? '';
}

export function resOptions(): number[] { return RES_OPTIONS; }
export { voxelSize };
