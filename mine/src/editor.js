// Spline editor: drag junction nodes (orange) and tunnel control points (cyan).
// Every drag re-runs the procedural builder, so cave, road, grooves, supports,
// lamps, cart lanes and collision all follow the splines.
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { TransformControls } from 'three/examples/jsm/controls/TransformControls.js';
import { EdgeSpline } from './network.js';

export class Editor {
  constructor({ scene, camera, dom, getNet, onChange }) {
    this.scene = scene; this.camera = camera; this.dom = dom;
    this.getNet = getNet; this.onChange = onChange;
    this.group = new THREE.Group();
    this.group.visible = false;
    scene.add(this.group);
    this.orbit = new OrbitControls(camera, dom);
    this.orbit.enabled = false;
    this.orbit.enableDamping = true;
    this.orbit.maxPolarAngle = Math.PI * 0.49;
    this.tc = new TransformControls(camera, dom);
    this.tc.setSize(0.9);
    this.tcHelper = this.tc.getHelper();
    this.tcHelper.visible = false; // added to the scene only while editing
    this.tc.addEventListener('dragging-changed', (e) => {
      this.orbit.enabled = !e.value && this.active;
      if (!e.value) this.onChange(true); // full rebuild on release
    });
    this.tc.addEventListener('objectChange', () => this.handleMoved());
    this.handles = [];
    this.selected = null;
    this.active = false;
    this.ray = new THREE.Raycaster();
    this.mNode = new THREE.MeshStandardMaterial({ color: 0xff8a1c, emissive: 0xff6a00, emissiveIntensity: 1.2, depthTest: false });
    this.mCp = new THREE.MeshStandardMaterial({ color: 0x33ddff, emissive: 0x00aaff, emissiveIntensity: 1.2, depthTest: false });
    this.mSel = new THREE.MeshStandardMaterial({ color: 0xffffff, emissive: 0xffffff, emissiveIntensity: 1.5, depthTest: false });
    this.lineMat = new THREE.LineBasicMaterial({ color: 0x66e0ff, depthTest: false, transparent: true, opacity: 0.85 });
    this.badMat = new THREE.LineBasicMaterial({ color: 0xff3030, depthTest: false });
    dom.addEventListener('pointerdown', (e) => this.onPointer(e));
  }

  enable(on, focus) {
    this.active = on;
    this.group.visible = on;
    this.orbit.enabled = on;
    this.tcHelper.visible = on && !!this.selected;
    if (on) this.scene.add(this.tcHelper); else this.scene.remove(this.tcHelper);
    if (!on) { this.tc.detach(); this.selected = null; }
    if (on) {
      this.rebuildHandles();
      if (focus) {
        this.orbit.target.copy(focus);
        this.camera.position.copy(focus).add(new THREE.Vector3(60, 90, 60));
      }
    }
  }

  rebuildHandles(conflictEdges = new Set()) {
    this.conflictEdges = conflictEdges;
    this.group.clear();
    this.handles = [];
    const net = this.getNet();
    const sg = new THREE.SphereGeometry(1, 16, 12);
    net.nodes.forEach((n, i) => {
      const m = new THREE.Mesh(sg, this.mNode);
      m.scale.setScalar(2.2);
      m.position.fromArray(n.p);
      m.renderOrder = 10;
      m.userData = { type: 'node', index: i };
      this.group.add(m); this.handles.push(m);
    });
    net.edges.forEach((e, ei) => {
      e.cps.forEach((c, ci) => {
        const m = new THREE.Mesh(sg, this.mCp);
        m.scale.setScalar(1.4);
        m.position.fromArray(c);
        m.renderOrder = 10;
        m.userData = { type: 'cp', edge: ei, index: ci };
        this.group.add(m); this.handles.push(m);
      });
    });
    this.lines = [];
    net.edges.forEach((e, ei) => {
      const geo = new THREE.BufferGeometry();
      const l = new THREE.Line(geo, conflictEdges.has(ei) ? this.badMat : this.lineMat);
      l.renderOrder = 9;
      l.userData.edge = ei;
      this.group.add(l); this.lines.push(l);
    });
    this.updateLines();
    if (this.selected) {
      const again = this.handles.find((h) => h.userData.type === this.selected.userData.type && h.userData.index === this.selected.userData.index && h.userData.edge === this.selected.userData.edge);
      if (again) this.select(again); else { this.tc.detach(); this.selected = null; }
    }
  }

  updateLines() {
    const net = this.getNet();
    this.lines.forEach((l) => {
      const sp = new EdgeSpline(net, net.edges[l.userData.edge]);
      const pts = sp.curve.getSpacedPoints(Math.ceil(sp.length / 2)).map((p) => p.add(new THREE.Vector3(0, 0.4, 0)));
      l.geometry.dispose();
      l.geometry = new THREE.BufferGeometry().setFromPoints(pts);
    });
  }

  onPointer(e) {
    if (!this.active || e.button !== 0) return;
    if (this.tc.dragging || this.tc.axis) return;
    const r = this.dom.getBoundingClientRect();
    const ndc = new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
    this.ray.setFromCamera(ndc, this.camera);
    const hits = this.ray.intersectObjects(this.handles, false);
    if (hits.length) this.select(hits[0].object);
  }

  select(h) {
    if (this.selected) this.selected.material = this.selected.userData.type === 'node' ? this.mNode : this.mCp;
    this.selected = h;
    h.material = this.mSel;
    this.tc.attach(h);
    this.tcHelper.visible = true;
    this.onSelect && this.onSelect(h.userData);
  }

  handleMoved() {
    const h = this.selected;
    if (!h) return;
    const net = this.getNet();
    const p = h.position.toArray();
    if (h.userData.type === 'node') net.nodes[h.userData.index].p = p;
    else net.edges[h.userData.edge].cps[h.userData.index] = p;
    this.updateLines();
    this.onChange(false);
  }

  // insert a control point after the selected one (or at the middle of the edge of a node selection)
  addControlPoint() {
    const net = this.getNet();
    const s = this.selected && this.selected.userData;
    if (!s || s.type !== 'cp') return false;
    const e = net.edges[s.edge];
    const all = [net.nodes[e.a].p, ...e.cps, net.nodes[e.b].p];
    const a = all[s.index + 1], b = all[s.index + 2];
    e.cps.splice(s.index + 1, 0, [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2]);
    this.rebuildHandles(this.conflictEdges);
    this.onChange(true);
    return true;
  }
  deleteControlPoint() {
    const net = this.getNet();
    const s = this.selected && this.selected.userData;
    if (!s || s.type !== 'cp') return false;
    const e = net.edges[s.edge];
    if (e.cps.length <= 1) return false;
    e.cps.splice(s.index, 1);
    this.tc.detach(); this.selected = null;
    this.rebuildHandles(this.conflictEdges);
    this.onChange(true);
    return true;
  }
  update() { if (this.active) this.orbit.update(); }
}
