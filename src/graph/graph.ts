// ---------------------------------------------------------------------------
// Frontier graph / model
// ---------------------------------------------------------------------------

import { Graph, GraphNode, GraphEdge } from './types';
import { defFor, defaultParams } from './registry';

let idCounter = 0;
export const nid = (p: string) => `${p}${(++idCounter).toString(36)}${Math.floor(Math.random() * 1e4).toString(36)}`;

export function makeNode(type: string, x: number, y: number): GraphNode {
  return { id: nid('n'), type, x, y, params: defaultParams(type) };
}

export function makeEdge(from: string, fromPort: string, to: string, toPort: string): GraphEdge {
  return { id: nid('e'), from, fromPort, to, toPort };
}

/** topological order (Kahn). Returns null when the graph has a cycle. */
export function topoOrder(g: Graph): GraphNode[] | null {
  const indeg = new Map<string, number>();
  const adj = new Map<string, GraphEdge[]>();
  for (const n of g.nodes) { indeg.set(n.id, 0); adj.set(n.id, []); }
  for (const e of g.edges) {
    if (!indeg.has(e.to) || !adj.has(e.from)) continue;
    indeg.set(e.to, (indeg.get(e.to) ?? 0) + 1);
    adj.get(e.from)!.push(e);
  }
  const queue = g.nodes.filter((n) => (indeg.get(n.id) ?? 0) === 0);
  const out: GraphNode[] = [];
  while (queue.length) {
    const n = queue.shift()!;
    out.push(n);
    for (const e of adj.get(n.id) ?? []) {
      const d = (indeg.get(e.to) ?? 1) - 1;
      indeg.set(e.to, d);
      if (d === 0) {
        const dn = g.nodes.find((x) => x.id === e.to);
        if (dn) queue.push(dn);
      }
    }
  }
  return out.length === g.nodes.length ? out : null;
}

export function inputsOf(g: Graph, nodeId: string): GraphEdge[] {
  return g.edges.filter((e) => e.to === nodeId);
}

export function outputsOf(g: Graph, nodeId: string): GraphEdge[] {
  return g.edges.filter((e) => e.from === nodeId);
}

export function portType(g: Graph, nodeId: string, port: string, dir: 'in' | 'out') {
  const n = g.nodes.find((x) => x.id === nodeId);
  if (!n) return null;
  const def = defFor(n.type);
  const p = (dir === 'in' ? def.inputs : def.outputs).find((x) => x.name === port);
  return p?.type ?? null;
}

export function serialize(g: Graph): string {
  return JSON.stringify(g, null, 2);
}

export function deserialize(s: string): Graph | null {
  try {
    const g = JSON.parse(s) as Graph;
    if (!Array.isArray(g.nodes) || !Array.isArray(g.edges) || !g.domain) return null;
    for (const n of g.nodes) {
      const params = defaultParams(n.type);
      n.params = { ...params, ...(n.params ?? {}) };
    }
    return g;
  } catch {
    return null;
  }
}

/** the shipped starter graph: mountains -> rivers -> cliffs -> fans -> caves */
export function defaultGraph(): Graph {
  const g: Graph = {
    nodes: [],
    edges: [],
    domain: { size: [1024, 328, 1024], res: 256, seed: 1337 },
  };
  const add = (n: GraphNode) => { g.nodes.push(n); return n; };
  const link = (a: GraphNode, ap: string, b: GraphNode, bp: string) => g.edges.push(makeEdge(a.id, ap, b.id, bp));

  const start = add(makeNode('start', 60, 260));
  const mtn = add(makeNode('multifractal', 300, 120));
  const detail = add(makeNode('simplex', 300, 380));
  detail.params.amplitude = 26;
  detail.params.scale = 190;
  detail.params.octaves = 4;
  const mix = add(makeNode('mix', 540, 240));
  mix.params.op = 'add';
  const lift = add(makeNode('lift', 760, 240));
  const hyd = add(makeNode('hydraulic', 980, 200));
  const therm = add(makeNode('thermal', 1200, 200));
  const alluv = add(makeNode('alluvial', 1420, 200));
  const wind = add(makeNode('wind', 1640, 200));
  wind.params.strength = 0.35;
  const caves = add(makeNode('cave', 1420, 470));
  const carve = add(makeNode('carve', 1860, 260));
  const out = add(makeNode('output', 2080, 260));

  const mRock = add(makeNode('mask', 1200, 620));
  mRock.params.kind = 'slope'; mRock.params.lo = 0.35; mRock.params.hi = 2.5; mRock.params.feather = 0.35;
  const mGrass = add(makeNode('mask', 1200, 800));
  mGrass.params.kind = 'slope'; mGrass.params.lo = -1; mGrass.params.hi = 0.5; mGrass.params.feather = 0.3;
  const mFlow = add(makeNode('mask', 1420, 800));
  mFlow.params.kind = 'flow'; mFlow.params.lo = 0.55; mFlow.params.hi = 1.0; mFlow.params.feather = 0.2;
  const mSnow = add(makeNode('mask', 1420, 980));
  mSnow.params.kind = 'height'; mSnow.params.lo = 190; mSnow.params.hi = 320; mSnow.params.feather = 40;

  const lBase = add(makeNode('layer', 1640, 620));
  lBase.params.name = 'Base'; lBase.params.color = [0.36, 0.33, 0.3];
  const lRock = add(makeNode('layer', 1640, 760));
  lRock.params.name = 'Rock'; lRock.params.color = [0.42, 0.4, 0.38];
  const lGrass = add(makeNode('layer', 1640, 900));
  lGrass.params.name = 'Grass'; lGrass.params.color = [0.24, 0.36, 0.18];
  const lSand = add(makeNode('layer', 1640, 1040));
  lSand.params.name = 'Sand'; lSand.params.color = [0.62, 0.54, 0.36];
  const lSnow = add(makeNode('layer', 1640, 1180));
  lSnow.params.name = 'Snow'; lSnow.params.color = [0.92, 0.94, 0.97];

  const splat = add(makeNode('splat', 1860, 800));

  link(start, 'World', mtn, 'World');
  link(start, 'World', detail, 'World');
  link(mtn, 'Height', mix, 'A');
  link(detail, 'Height', mix, 'B');
  link(mix, 'Height', lift, 'Height');
  link(lift, 'SDF', hyd, 'SDF');
  link(hyd, 'SDF', therm, 'SDF');
  link(therm, 'SDF', alluv, 'SDF');
  link(alluv, 'SDF', wind, 'SDF');
  link(wind, 'SDF', carve, 'SDF');
  link(caves, 'Void', carve, 'Void');
  link(carve, 'SDF', out, 'SDF');

  link(wind, 'SDF', mRock, 'SDF');
  link(wind, 'SDF', mGrass, 'SDF');
  link(hyd, 'Flow', mFlow, 'Flow');
  link(wind, 'SDF', mFlow, 'SDF');
  link(wind, 'SDF', mSnow, 'SDF');

  link(mGrass, 'Mask', lBase, 'Mask');
  link(mRock, 'Mask', lRock, 'Mask');
  link(mGrass, 'Mask', lGrass, 'Mask');
  link(mFlow, 'Mask', lSand, 'Mask');
  link(mSnow, 'Mask', lSnow, 'Mask');

  link(lBase, 'Layer', splat, 'Base');
  link(lRock, 'Layer', splat, 'L2');
  link(lGrass, 'Layer', splat, 'L3');
  link(lSand, 'Layer', splat, 'L4');
  link(lSnow, 'Layer', splat, 'L5');
  link(splat, 'Splat', out, 'Splat');

  return g;
}
