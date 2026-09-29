// jsdom smoke test for the editor / inspector / chrome DOM wiring
import { JSDOM } from 'jsdom';

const dom = new JSDOM('<!doctype html><html><body><div id="app"></div></body></html>', { pretendToBeVisual: true });
const w = dom.window as unknown as typeof globalThis & { HTMLElement: typeof HTMLElement };
for (const k of ['window', 'document', 'navigator', 'HTMLElement', 'SVGElement', 'Element', 'Node', 'getComputedStyle', 'requestAnimationFrame', 'cancelAnimationFrame', 'CustomEvent', 'Event', 'MouseEvent', 'Blob', 'URL', 'DOMParser']) {
  Object.defineProperty(globalThis, k, { value: (w as never as Record<string, unknown>)[k], configurable: true, writable: true });
}
(globalThis as never as Record<string, unknown>).ResizeObserver = class { observe() {} unobserve() {} disconnect() {} };

import { defaultGraph, serialize, deserialize } from '../graph/graph';
import { NodeEditor, EditorApp, GuardBadge } from '../ui/editor';
import { Inspector, InspectorApp } from '../ui/inspector';
import { buildChrome } from '../ui/chrome';
import { cookGraph } from '../worker/eval';
import { defFor } from '../graph/registry';
import { evaluateResolution, ResolutionContext } from '../core/resolution';

function assert(cond: boolean, msg: string): void {
  if (!cond) { console.error('FAIL:', msg); process.exit(1); }
  console.log('ok  -', msg);
}

const graph = defaultGraph();
const badges = new Map<string, GuardBadge>();
let selected: string | null = null;
let dirtyCount = 0;

const app = document.querySelector('#app') as unknown as HTMLElement;
(app as never as { innerHTML: string }).innerHTML = `
  <div id="viewport-side"></div>
  <div id="splitter"></div>
  <div id="editor-side"><div id="canvas-wrap" class="dotted"></div></div>
`;
const vSide = document.querySelector('#viewport-side') as unknown as HTMLElement;
const wrap = document.querySelector('#canvas-wrap') as unknown as HTMLElement;

const editorApp: EditorApp = {
  graph,
  get selectedId() { return selected; },
  markDirty: () => { dirtyCount++; },
  onNodeMoved: () => { dirtyCount++; },
  onSelect: (id) => { selected = id; editor.updateSelection(); inspector.sync(); },
  badges,
  onRaiseRes: (r) => { graph.domain.res = r; },
  cook: () => { cook(); },
  toggleInspector: () => {},
  get inspectorOpen() { return true; },
};
const editor = new NodeEditor(wrap, editorApp);

const inspEl = document.createElement('div');
inspEl.id = 'inspector';
wrap.appendChild(inspEl);
const inspectorApp: InspectorApp = {
  graph,
  get selectedId() { return selected; },
  badges,
  onParamChange: () => { dirtyCount++; },
  onDomainChange: () => { dirtyCount++; },
  onRaiseRes: (r) => { graph.domain.res = r; },
};
const inspector = new Inspector(inspEl, inspectorApp);

let cooked = 0;
function cook(): void {
  const r = cookGraph(graph, () => {}, () => false);
  assert(!!r && !!r.mesh && r.mesh.triangleCount > 0, 'cook produced a mesh');
  cooked++;
}

// --- DOM structure
const nodeEls = wrap.querySelectorAll('.node');
assert(nodeEls.length === graph.nodes.length, `node cards rendered (${nodeEls.length})`);
const portDots = wrap.querySelectorAll('.port .dot');
assert(portDots.length > 20, `port dots rendered (${portDots.length})`);

// --- wires after layout (jsdom has no layout: rects are 0 — anchors still computed)
editor.updateWires();
const paths = wrap.querySelectorAll('#wires path');
assert(paths.length === graph.edges.length, `wires drawn (${paths.length} for ${graph.edges.length} edges)`);

// --- selection + inspector
editorApp.onSelect(graph.nodes.find((n) => n.type === 'hydraulic')!.id);
assert((wrap.querySelectorAll('.node.selected').length as number) === 1, 'selection highlights one node');
inspector.sync();
assert(inspEl.innerHTML.includes('Channel Width'), 'inspector shows hydraulic params');
assert(inspEl.innerHTML.includes('RESOLUTION BUDGET'), 'inspector shows resolution budget');

// --- guards: drop resolution until hydraulic blocks
graph.domain.res = 64;
const ctx: ResolutionContext = { domainSize: graph.domain.size, res: graph.domain.res, aspectY: graph.domain.size[1] / graph.domain.size[0] };
for (const n of graph.nodes) {
  const req = defFor(n.type).resRequirement?.(n.params, ctx) ?? null;
  if (req && req.featureMetres > 0) {
    const v = evaluateResolution(ctx, req);
    badges.set(n.id, { ok: v.ok, reason: v.reason, requiredRes: v.requiredRes });
  }
}
editor.updateBadges();
assert(wrap.querySelectorAll('.node-badge').length > 0, 'resolution guard badges appear at low res');
inspector.sync();
assert(inspEl.innerHTML.includes('Raise resolution'), 'inspector offers raise-resolution action');
graph.domain.res = 256;
badges.clear();
editor.updateBadges();

// --- library
editor.setLibraryOpen(true);
assert(wrap.querySelectorAll('.lib-item').length > 10, `library lists nodes (${wrap.querySelectorAll('.lib-item').length})`);

// --- chrome
let actions = 0;
buildChrome(vSide, {
  onNew: () => actions++, onSave: () => actions++, onOpen: () => actions++, onUndo: () => actions++, onRedo: () => actions++,
  onWireframe: () => actions++, onTextured: () => actions++, onSnapshot: () => actions++, onAutoCook: () => actions++,
  wireframe: false, textured: true, autoCook: true, setTitle: () => {},
});
assert(vSide.querySelectorAll('#tool-rail button').length === 10, 'tool rail rendered');
assert(!!vSide.querySelector('#brush-bar.disabled'), 'brush bar present and inert');
(vSide.querySelector('[data-act="undo"]') as unknown as HTMLElement).click();
assert(actions === 1, 'toolbar dispatches actions');

// --- serialize round trip
const g2 = deserialize(serialize(graph));
assert(!!g2 && g2.nodes.length === graph.nodes.length, 'graph serialize/deserialize round trip');

// --- progress pill
editor.setProgress(0.42, 'hydraulic 3/24');
assert((wrap.querySelector('#pp-pct') as unknown as HTMLElement).textContent === '42%', 'progress pill updates');

cook();
cook();
assert(cooked === 2, 'cook ran twice');
console.log('UI SMOKE OK');
