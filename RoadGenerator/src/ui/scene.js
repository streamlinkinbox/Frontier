// Scene: turns build outputs into renderer draws, keeps the layer → tag mapping, pick ids and dynamic traffic.
import { FLAG_PAINT, FLAG_EMISSIVE, FLAG_NODECAL, FLAG_VARIED } from "../gpu/renderer.js";
import { prop } from "../core/props.js";
import { railProp } from "../core/rail.js";
import { basis } from "../core/furnish.js";
import { mat4 } from "../core/math.js";

const KIT_MATERIALS = new Set(["curb", "paving", "concrete", "soil", "grass", "tactile", "gravel"]);
const PROP_LAYER = {
  streetlight: "lighting", trafficLight: "signals", signPost: "signals", bollard: "barriers", guardrailPanel: "barriers", wbeamSection: "barriers", jerseyBarrier: "barriers",
  trafficCone: "construction", waterBarrier: "construction", barricade: "construction", roadworkSign: "construction", billboard: "billboards", carBody: "parking", treeStump: "furniture", manholeRing: "furniture",
};

export const LAYERS = [
  { id: "ground", name: "Ground", icon: "ground", tags: ["ground", "embank"], desc: "Terrain grid, embankments and cuttings" },
  { id: "roads", name: "Road network", icon: "road", tags: ["roads"], desc: "Carriageways and merged junction slabs" },
  { id: "kits", name: "Curbs & footways", icon: "grid", tags: ["kits"], desc: "Kerbs, pavements, planting strips" },
  { id: "markings", name: "Road markings", icon: "marking", tags: ["markings"], desc: "Lines, arrows, zebras, gores" },
  { id: "furniture", name: "Medians & bumps", icon: "ruler", tags: ["furniture", "furnish-paint"], desc: "Raised medians, speed bumps, parking paint" },
  { id: "damage", name: "Damage decals", icon: "paint", tags: ["decals"], desc: "Potholes, cracks, patches, manholes" },
  { id: "signals", name: "Signals & signs", icon: "lamp", tags: ["signals", "lenses"], desc: "Traffic lights, stop / yield signs" },
  { id: "lighting", name: "Street lighting", icon: "sun", tags: ["lighting"], desc: "Lamp columns" },
  { id: "barriers", name: "Barriers & bollards", icon: "cone", tags: ["barriers"], desc: "Guardrails, W-beam, jersey, bollards" },
  { id: "construction", name: "Roadworks", icon: "cone", tags: ["construction"], desc: "Cones, barricades, signage" },
  { id: "billboards", name: "Billboards", icon: "texture", tags: ["billboards"], desc: "Roadside advertising" },
  { id: "parking", name: "Parked cars", icon: "parking", tags: ["parking"], desc: "On-street parking occupancy" },
  { id: "rail", name: "Railway", icon: "rail", tags: ["rail", "railprops"], desc: "Track, ballast, level crossings" },
  { id: "subway", name: "Subway tunnels", icon: "rail", tags: ["subway", "ground-xray"], desc: "Bored tunnels (X-ray to inspect)" },
  { id: "structures", name: "Bridges & trenches", icon: "bridge", tags: ["structures"], desc: "Decks, piers, retaining walls, cut-and-cover" },
  { id: "traffic", name: "AI traffic", icon: "car", tags: ["traffic", "trains"], desc: "Up to 25 vehicles + trains" },
];

export class Scene {
  constructor(renderer) {
    this.R = renderer;
    this.pickIds = new Map(); this.owners = new Map(); this.nextPick = 1;
    this.vis = Object.fromEntries(LAYERS.map((l) => [l.id, true]));
    this.vehicleMeshes = {}; this.trainMeshes = []; this.lensMeshes = {};
    this.xray = false;
  }
  pickId(owner) { if (!owner) return 0; let id = this.pickIds.get(owner); if (!id) { id = this.nextPick++; this.pickIds.set(owner, id); this.owners.set(id, owner); } return id; }
  ownerOf(id) { return this.owners.get(id) || null; }

  _addBatches(mb, tagFn, opts = {}) {
    if (!mb) return;
    for (const b of mb.toGPU()) {
      if (!b.indices.length) continue;
      const tag = typeof tagFn === "function" ? tagFn(b) : tagFn;
      const o = { ...opts, material: b.material, tag, pickId: opts.pick === false ? 0 : this.pickId(b.owner) };
      if (b.material === "carPaint") o.flags = (o.flags || 0) | FLAG_VARIED;
      if (b.material.startsWith("lamp") && b.material !== "lampOff") { o.flags = (o.flags || 0) | FLAG_EMISSIVE; o.tint = [1, 1, 1, 2.5]; }
      const h = this.R.addMesh(b, o);
      this.R.setVisible(h, this._tagVisible(tag));
    }
  }
  _addInstanced(prop, variant, mats, tag, extra = {}) {
    if (!(mats instanceof Float32Array) && !mats.length) return;
    const arr = mats instanceof Float32Array ? mats : (() => { const a = new Float32Array(mats.length * 16); mats.forEach((m, i) => a.set(m, i * 16)); return a; })();
    const handles = [];
    for (const b of prop.toGPU()) {
      const o = { material: b.material, tag, instances: arr, pickId: 0, ...extra };
      if (b.material === "carPaint") o.flags = (o.flags || 0) | FLAG_VARIED;
      if (b.material.startsWith("lamp") && b.material !== "lampOff") { o.flags = (o.flags || 0) | FLAG_EMISSIVE; o.tint = extra.tint || [1, 1, 1, 2.0]; }
      const h = this.R.addMesh(b, o);
      this.R.setVisible(h, this._tagVisible(tag));
      handles.push(h);
    }
    return handles;
  }
  _tagVisible(tag) {
    if (tag === "ground-xray") return this.vis.ground && !this.xray;
    const L = LAYERS.find((l) => l.tags.includes(tag));
    return L ? this.vis[L.id] : true;
  }
  setLayer(id, on) {
    this.vis[id] = on;
    const L = LAYERS.find((l) => l.id === id); if (!L) return;
    for (const t of L.tags) this.R.setTagVisible(t, this._tagVisible(t));
    if (id === "ground") this.R.setTagVisible("ground-xray", this._tagVisible("ground-xray"));
    if (id === "damage") this._applyDecals();
  }
  setXray(on) { this.xray = on; this.R.setTagVisible("ground-xray", this._tagVisible("ground-xray")); }
  _applyDecals() { this.R.setDecals(this.vis.damage ? this.decals || [] : []); }

  clear() {
    for (const m of [...this.R.meshes.values()]) this.R.removeMesh(m.handle);
    this.pickIds.clear(); this.owners.clear(); this.nextPick = 1;
    this.vehicleMeshes = {}; this.trainMeshes = []; this.lensMeshes = {};
  }

  /** build: { net, plan, mk, F, rails, st } */
  setBuild(B) {
    this.clear();
    this.build = B;
    const { net, plan, mk, F, rails, st } = B;
    // ground
    if (st?.ground) this._addBatches(st.ground, (b) => (b.owner === "ground-xray" ? "ground-xray" : "ground"), { pick: false, decals: false });
    // roads: split kit materials into their own layer
    this._addBatches(plan.mesh, (b) => (KIT_MATERIALS.has(b.material) ? "kits" : "roads"), { wear: 0.6 });
    // markings (paint, depth-biased)
    if (mk) this._addBatches(mk, "markings", { flags: FLAG_PAINT, wear: B.settings?.markings?.wear ?? 0.45, castShadow: false, pick: false });
    if (F) {
      this._addBatches(F.solid, "furniture", {});
      this._addBatches(F.paint, "furnish-paint", { flags: FLAG_PAINT, castShadow: false, pick: false, wear: 0.35 });
      // merge colour sub-groups (per-instance palette handles body colours) → one draw set per prop/variant
      const merged = new Map();
      for (const g of F.instances.values()) { const k = g.prop + "|" + g.variant; const m = merged.get(k) || merged.set(k, { prop: g.prop, variant: g.variant, mats: [] }).get(k); m.mats.push(...g.mats); }
      for (const g of merged.values()) {
        const layer = PROP_LAYER[g.prop] || "furniture";
        this._addInstanced(prop(g.prop, g.variant), g.variant, g.mats, layer);
      }
      this.decals = F.decals || [];
    } else this.decals = [];
    this._applyDecals();
    if (rails) {
      this._addBatches(rails.solid, (b) => { const line = net.rails.get(b.owner); return line?.tunnel ? "subway" : "rail"; }, {});
      for (const g of rails.instances.values()) this._addInstanced(railProp(g.prop), g.variant, g.mats, "railprops");
    }
    if (st?.solid) {
      this._addBatches(st.solid, (b) => (b.material === "grass" ? "embank" : "structures"), { decals: false });
      for (const g of st.instances.values()) this._addInstanced(prop(g.prop, g.variant), g.variant, g.mats, "structures");
    }
    // dynamic: vehicles + trains + signal lenses
    this.vehicleMeshes = {};
    this.lensMeshes = { red: this._addInstanced(prop("lens"), "", new Float32Array(0), "lenses", { tint: [1, 0.12, 0.08, 3.5], castShadow: false }), amber: this._addInstanced(prop("lens"), "", new Float32Array(0), "lenses", { tint: [1, 0.6, 0.1, 3.0], castShadow: false }), green: this._addInstanced(prop("lens"), "", new Float32Array(0), "lenses", { tint: [0.15, 1, 0.4, 3.0], castShadow: false }) };
  }
  /** Per-frame traffic sync. */
  updateTraffic(traffic) {
    if (!traffic) return;
    const poses = traffic.poses();
    for (const kind in poses) {
      if (!this.vehicleMeshes[kind]) this.vehicleMeshes[kind] = this._addInstanced(prop("carBody", kind), kind, poses[kind], "traffic");
      else for (const h of this.vehicleMeshes[kind]) this.R.updateInstances(h, poses[kind]);
    }
    const tp = traffic.trainPoses();
    if (tp.length) {
      if (!this.trainMeshes.length) this.trainMeshes = this._addInstanced(prop("trainCar"), "", tp, "trains");
      else for (const h of this.trainMeshes) this.R.updateInstances(h, tp);
    }
    if (traffic.signals.nodes.length && this.lensMeshes.red) {
      const L = traffic.signals.lenses();
      const key = L.red.length + ":" + L.green.length + ":" + L.amber.length + ":" + traffic.signals.nodes.map((n) => n.phase).join(",");
      if (key !== this._lensKey) {
        this._lensKey = key;
        for (const c of ["red", "amber", "green"]) {
          const arr = new Float32Array(L[c].length * 16);
          L[c].forEach((l, i) => arr.set(basis(l.p, l.facing), i * 16));
          for (const h of this.lensMeshes[c] || []) this.R.updateInstances(h, arr);
        }
      }
    }
  }
}
