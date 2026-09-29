// ---------------------------------------------------------------------------
// Frontier — SDF terrain generator
// App controller: graph state, resolution guard, cook scheduling, undo/redo,
// save/load, and the split viewport / node-editor shell.
// ---------------------------------------------------------------------------

import './style.css';
import { Graph } from './graph/types';
import { defaultGraph, deserialize, serialize } from './graph/graph';
import { defFor } from './graph/registry';
import { evaluateResolution, ResolutionContext, ResolutionVerdict } from './core/resolution';
import { Cooker } from './worker/bridge';
import { Viewport } from './ui/viewport';
import { NodeEditor, GuardBadge, EditorApp } from './ui/editor';
import { Inspector, InspectorApp } from './ui/inspector';
import { buildChrome, toast } from './ui/chrome';

class App {
  graph: Graph = defaultGraph();
  selectedId: string | null = null;
  badges = new Map<string, GuardBadge>();
  inspectorOpen = true;
  wireframe = false;
  textured = true;
  autoCook = true;
  dirty = false;
  private undoStack: string[] = [];
  private redoStack: string[] = [];
  private viewport!: Viewport;
  private editor!: NodeEditor;
  private inspector!: Inspector;
  private cooker!: Cooker;
  private cookTimer: number | undefined;
  private stats!: HTMLElement;

  constructor() {
    const app = document.querySelector('#app')!;
    app.innerHTML = `
      <div id="viewport-side"></div>
      <div id="splitter"></div>
      <div id="editor-side"><div id="canvas-wrap" class="dotted"></div></div>
    `;
    const vSide = app.querySelector('#viewport-side') as HTMLElement;
    const wrap = app.querySelector('#canvas-wrap') as HTMLElement;

    this.viewport = new Viewport({ container: vSide, domainSize: this.graph.domain.size });
    this.viewport.frameTerrain();

    const self = this;
    buildChrome(vSide, {
      onNew: () => this.newGraph(),
      onSave: () => this.save(),
      onOpen: () => this.open(),
      onUndo: () => this.undo(),
      onRedo: () => this.redo(),
      onWireframe: () => { this.wireframe = !this.wireframe; this.viewport.setWireframe(this.wireframe); },
      onTextured: () => { this.textured = !this.textured; this.viewport.setTextured(this.textured); this.cook(true); },
      onSnapshot: () => this.snapshot(),
      onAutoCook: () => {
        this.autoCook = !this.autoCook;
        toast(`Auto-cook ${this.autoCook ? 'on' : 'off'} — ${this.autoCook ? 'edits re-cook automatically' : 'press ▷ or Ctrl+Enter to cook'}`);
      },
      get wireframe() { return self.wireframe; },
      get textured() { return self.textured; },
      get autoCook() { return self.autoCook; },
      setTitle: () => { /* handled internally */ },
    });
    this.stats = vSide.querySelector('#hud-stats')!;

    const editorApp: EditorApp = {
      graph: this.graph,
      get selectedId() { return self.selectedId; },
      markDirty: (s) => this.markDirty(s),
      onNodeMoved: () => { this.dirty = true; this.setTitle('Untitled', true); },
      onSelect: (id) => this.select(id),
      badges: this.badges,
      onRaiseRes: (r) => this.raiseRes(r),
      cook: () => this.cook(true),
      toggleInspector: (open) => this.toggleInspector(open),
      get inspectorOpen() { return self.inspectorOpen; },
    };
    this.editor = new NodeEditor(wrap, editorApp);

    const inspEl = document.createElement('div');
    inspEl.id = 'inspector';
    wrap.appendChild(inspEl);
    const inspectorApp: InspectorApp = {
      graph: this.graph,
      get selectedId() { return self.selectedId; },
      badges: this.badges,
      onParamChange: () => this.markDirty(false),
      onDomainChange: () => this.onDomainChange(),
      onRaiseRes: (r) => this.raiseRes(r),
    };
    this.inspector = new Inspector(inspEl, inspectorApp);

    this.cooker = new Cooker({
      onProgress: (f, s) => this.editor.setProgress(f * 0.98, s),
      onDone: (r) => {
        this.viewport.updateMesh(r.mesh, this.textured ? r.splat : null);
        this.editor.setProgress(1, 'done');
        this.stats.innerHTML =
          `${r.stats.res}³ · h ${r.stats.h.toFixed(2)} m<br/>` +
          `${(r.stats.triangles / 1000).toFixed(0)}k tris · band ${(r.stats.bandVoxels / 1e6).toFixed(2)}M vox<br/>` +
          `cooked in ${(r.stats.cookMs / 1000).toFixed(2)} s`;
        const ae = document.activeElement;
        if (!ae || !ae.closest('#inspector')) this.inspector.sync();
        hideBoot();
      },
      onError: (m) => {
        this.editor.setError(m);
        toast(`Cook error: ${m}`, true, 8000);
      },
    });

    this.bindSplitter(app.querySelector('#splitter') as HTMLElement);
    this.bindKeys();

    this.refreshGuards();
    this.setTitle('Untitled', false);
    this.toggleInspector(true);
    this.select(this.graph.nodes.find((n) => n.type === 'output')?.id ?? null);
    this.editor.fitGraph();
    this.cook(true);
    setTimeout(hideBoot, 9000);   // safety: never leave the boot overlay up
  }

  // ------------------------------------------------------------------ guards
  private ctx(): ResolutionContext {
    const d = this.graph.domain;
    return { domainSize: d.size, res: d.res, aspectY: d.size[1] / d.size[0] };
  }

  refreshGuards(): void {
    this.recomputeBadges();
    this.editor.updateBadges();
    this.inspector.sync();
    const blocked: ResolutionVerdict[] = [];
    for (const b of this.badges.values()) if (!b.ok) blocked.push({ ok: false, h: 0, featureVoxels: 0, requiredRes: b.requiredRes, requirement: { featureMetres: 0, label: '' }, reason: b.reason });
    if (blocked.length) toast(`Resolution guard: ${blocked[0].reason}`, true, 9000);
  }

  private recomputeBadges(): void {
    this.badges.clear();
    const ctx = this.ctx();
    for (const n of this.graph.nodes) {
      const req = defFor(n.type).resRequirement?.(n.params, ctx) ?? null;
      if (!req || req.featureMetres <= 0) continue;
      const v = evaluateResolution(ctx, req);
      this.badges.set(n.id, { ok: v.ok, reason: v.reason, requiredRes: v.requiredRes });
    }
  }

  raiseRes(res: number): void {
    this.graph.domain.res = Math.max(res, this.graph.domain.res);
    this.viewport.setDomain(this.graph.domain.size);
    this.markDirty(true);
    this.cook(true);
  }

  onDomainChange(): void {
    this.viewport.setDomain(this.graph.domain.size);
    this.viewport.frameTerrain();
    this.markDirty(true);
    this.cook(true);
  }

  // ------------------------------------------------------------------ cook
  cook(force = false): void {
    if (!force && !this.autoCook) return;
    this.recomputeBadges();
    this.editor.updateBadges();
    const blocked = [...this.badges.values()].find((b) => !b.ok);
    if (blocked) {
      this.editor.setError(`blocked: ${blocked.reason}`);
      toast(`Cook blocked — ${blocked.reason}`, true, 9000);
      hideBoot();
      return;
    }
    this.editor.setProgress(0.02, 'queued');
    this.cooker.cook(this.graph);
  }

  scheduleCook(): void {
    if (!this.autoCook) return;
    window.clearTimeout(this.cookTimer);
    this.cookTimer = window.setTimeout(() => this.cook(true), 500);
  }

  // ------------------------------------------------------------------ state
  markDirty(structural: boolean): void {
    if (structural) {
      this.undoStack.push(serialize(this.graph));
      if (this.undoStack.length > 60) this.undoStack.shift();
      this.redoStack.length = 0;
    }
    this.dirty = true;
    this.setTitle('Untitled', true);
    this.scheduleCook();
  }

  select(id: string | null): void {
    this.selectedId = id;
    this.editor.updateSelection();
    this.inspector.sync();
  }

  toggleInspector(open?: boolean): void {
    this.inspectorOpen = open ?? !this.inspectorOpen;
    document.querySelector('#inspector')?.classList.toggle('open', this.inspectorOpen);
  }

  setTitle(t: string, dirty: boolean): void {
    const el = document.querySelector('#doc-title');
    if (!el) return;
    el.textContent = `${t}${dirty ? ' *' : ''}`;
    el.classList.toggle('dirty', dirty);
  }

  /** swap graph contents in place so editor/inspector bridges stay valid */
  private adoptGraph(g: Graph): void {
    Object.assign(this.graph, g);
    this.editor.app.graph = this.graph;
    this.inspector.app.graph = this.graph;
    this.selectedId = null;
    this.editor.render();
    this.viewport.setDomain(this.graph.domain.size);
  }

  newGraph(): void {
    this.undoStack.push(serialize(this.graph));
    this.adoptGraph(defaultGraph());
    this.viewport.frameTerrain();
    this.dirty = false;
    this.setTitle('Untitled', false);
    this.editor.fitGraph();
    this.cook(true);
  }

  save(): void {
    const blob = new Blob([serialize(this.graph)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'frontier-terrain.json';
    a.click();
    URL.revokeObjectURL(a.href);
    this.dirty = false;
    this.setTitle('Untitled', false);
    toast('Graph saved to frontier-terrain.json');
  }

  open(): void {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json,application/json';
    input.onchange = () => {
      const f = input.files?.[0];
      if (!f) return;
      f.text().then((txt) => {
        const g = deserialize(txt);
        if (!g) { toast('Could not parse graph file', true); return; }
        this.undoStack.push(serialize(this.graph));
        this.adoptGraph(g);
        this.editor.fitGraph();
        this.cook(true);
      });
    };
    input.click();
  }

  undo(): void {
    const prev = this.undoStack.pop();
    if (!prev) return;
    this.redoStack.push(serialize(this.graph));
    this.adoptGraph(deserialize(prev) ?? this.graph);
    this.cook(true);
  }

  redo(): void {
    const next = this.redoStack.pop();
    if (!next) return;
    this.undoStack.push(serialize(this.graph));
    this.adoptGraph(deserialize(next) ?? this.graph);
    this.cook(true);
  }

  snapshot(): void {
    const url = this.viewport.snapshot();
    const a = document.createElement('a');
    a.href = url;
    a.download = 'frontier-snapshot.png';
    a.click();
    toast('Snapshot saved');
  }

  // ------------------------------------------------------------------ misc
  private bindKeys(): void {
    window.addEventListener('keydown', (e) => {
      const target = e.target as HTMLElement;
      const typing = target.tagName === 'INPUT' || target.tagName === 'SELECT' || target.tagName === 'TEXTAREA';
      if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); this.cook(true); }
      else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z' && !e.shiftKey && !typing) { e.preventDefault(); this.undo(); }
      else if ((e.ctrlKey || e.metaKey) && (e.key.toLowerCase() === 'y' || (e.key.toLowerCase() === 'z' && e.shiftKey)) && !typing) { e.preventDefault(); this.redo(); }
      else if ((e.key === 'Delete' || e.key === 'Backspace') && this.selectedId && !typing) {
        const id = this.selectedId;
        if (id === this.graph.nodes.find((n) => n.type === 'start')?.id) { toast('The Start node cannot be deleted', true); return; }
        this.graph.nodes = this.graph.nodes.filter((n) => n.id !== id);
        this.graph.edges = this.graph.edges.filter((ed) => ed.from !== id && ed.to !== id);
        this.select(null);
        this.markDirty(true);
        this.editor.render();
      }
    });
  }

  private bindSplitter(el: HTMLElement): void {
    let dragging = false;
    el.addEventListener('mousedown', () => { dragging = true; });
    window.addEventListener('mouseup', () => { dragging = false; });
    window.addEventListener('mousemove', (e) => {
      if (!dragging) return;
      const pct = Math.max(20, Math.min(75, (e.clientX / window.innerWidth) * 100));
      (document.querySelector('#viewport-side') as HTMLElement).style.flex = `0 0 ${pct}%`;
      this.viewport.resize();
    });
  }
}

function hideBoot(): void {
  const b = document.querySelector('#boot');
  if (b) {
    (b as HTMLElement).style.transition = 'opacity .4s';
    (b as HTMLElement).style.opacity = '0';
    setTimeout(() => b.remove(), 450);
  }
}

const bootFill = document.querySelector('#boot-fill') as HTMLElement | null;
const bootMsg = document.querySelector('#boot-msg') as HTMLElement | null;
const bootSteps = ['allocating narrow-band volume', 'seeding noise fields', 'warming cook worker', 'compiling splat shader'];
let bi = 0;
const bootTimer = setInterval(() => {
  bi = Math.min(bootSteps.length - 1, bi + 1);
  if (bootFill) bootFill.style.width = `${20 + bi * 22}%`;
  if (bootMsg) bootMsg.textContent = bootSteps[bi];
}, 220);
setTimeout(() => clearInterval(bootTimer), 4000);

new App();
