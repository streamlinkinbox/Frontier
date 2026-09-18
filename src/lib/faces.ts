import * as THREE from 'three';
import { clamp, easeSori, lerp } from './math';

export interface FaceFrame {
  pos: THREE.Vector3;
  /** across-slope axis */
  x: THREE.Vector3;
  /** surface normal (up) */
  y: THREE.Vector3;
  /** down-slope axis */
  z: THREE.Vector3;
}

export interface RoofFaceOpts {
  name: string;
  /** ridge-side corners (t=0). Equal for triangular faces. */
  topL: THREE.Vector3;
  topR: THREE.Vector3;
  /** eave-side corners (t=1) */
  botL: THREE.Vector3;
  botR: THREE.Vector3;
  yTop: number;
  yBot: number;
  sori: number;
  hipSori: number;
  cornerLift: number;
}

/**
 * One parametric roof slope. Plan (x,z) interpolates bilinearly between the
 * four corners; height follows the concave sorimashi profile in t.
 *
 * Adjacent faces that share a hip edge use identical corner endpoints and
 * identical (yTop, yBot, sori) so the shared edge curve matches exactly —
 * this is what keeps hips watertight.
 */
export class RoofFace {
  readonly name: string;
  readonly topL: THREE.Vector3;
  readonly topR: THREE.Vector3;
  readonly botL: THREE.Vector3;
  readonly botR: THREE.Vector3;
  readonly yTop: number;
  readonly yBot: number;
  readonly sori: number;
  readonly hipSori: number;
  readonly cornerLift: number;

  constructor(o: RoofFaceOpts) {
    this.name = o.name;
    this.topL = o.topL.clone();
    this.topR = o.topR.clone();
    this.botL = o.botL.clone();
    this.botR = o.botR.clone();
    this.yTop = o.yTop;
    this.yBot = o.yBot;
    this.sori = o.sori;
    this.hipSori = o.hipSori;
    this.cornerLift = o.cornerLift;
  }

  /** profile height at t (0 ridge → 1 eave), before edge/corer lifts */
  profY(t: number): number {
    return this.yTop - (this.yTop - this.yBot) * easeSori(t, this.sori);
  }

  point(u: number, t: number, target: THREE.Vector3): THREE.Vector3 {
    const uc = clamp(u, 0, 1);
    const tc = clamp(t, 0, 1);
    let x =
      lerp(lerp(this.topL.x, this.topR.x, uc), lerp(this.botL.x, this.botR.x, uc), tc);
    let z =
      lerp(lerp(this.topL.z, this.topR.z, uc), lerp(this.botL.z, this.botR.z, uc), tc);
    const e = Math.abs(2 * uc - 1);
    const edgeW = Math.pow(e, 6); // hip/barge edge line
    // wing-corner sweep: rises over the outer span, not just the tip
    const cw = e <= 0.25 ? 0 : Math.pow((e - 0.25) / 0.75, 1.5);
    let y = this.profY(tc);
    y += this.hipSori * Math.sin(Math.PI * tc) * edgeW; // curled hip rafter line
    // Traditional East Asian yijiao corner curl (冲三翘四 / Jiangnan nenqiang sweep):
    // smooth ramp in the body with progressive steepening towards the flying eave tip
    y += this.cornerLift * (0.28 * Math.pow(tc, 2.0) + 0.72 * Math.pow(tc, 4.2)) * cw;
    // corners also kick outward in plan (pure function of the shared edge
    // params, so both faces meeting at a hip compute the identical curve)
    const flare = this.cornerLift * 0.45 * Math.pow(tc, 3.8) * cw;
    if (flare > 1e-6) {
      const rl = Math.hypot(x, z) || 1;
      x += (x / rl) * flare;
      z += (z / rl) * flare;
    }
    return target.set(x, y, z);
  }

  /** orthonormal frame; z always points down-slope (for tile orientation) */
  frame(u: number, t: number): FaceFrame {
    const e = 0.006;
    const uA = clamp(u - e, 0, 1);
    const uB = clamp(u + e, 0, 1);
    const tA = clamp(t - e, 0, 1);
    const tB = clamp(t + e, 0, 1);
    const p = this.point(u, t, new THREE.Vector3());
    const puA = this.point(uA, t, new THREE.Vector3());
    const puB = this.point(uB, t, new THREE.Vector3());
    const ptA = this.point(u, tA, new THREE.Vector3());
    const ptB = this.point(u, tB, new THREE.Vector3());
    const du = puB.sub(puA);
    const dt = ptB.sub(ptA);
    const x = du.clone().normalize();
    const y = new THREE.Vector3().crossVectors(du, dt).normalize();
    if (y.y < 0) y.negate();
    const z = new THREE.Vector3().crossVectors(x, y).normalize();
    if (z.dot(dt) < 0) {
      x.negate();
      z.negate();
    }
    return { pos: p, x, y, z };
  }

  edgePoint(side01: number, t: number, target: THREE.Vector3): THREE.Vector3 {
    return this.point(side01, t, target);
  }

  widthAt(t: number): number {
    const topW = this.topL.distanceTo(this.topR);
    const botW = this.botL.distanceTo(this.botR);
    return lerp(topW, botW, clamp(t, 0, 1));
  }

  slopeLength(): number {
    const a = new THREE.Vector3();
    const b = new THREE.Vector3();
    let len = 0;
    const n = 24;
    this.point(0.5, 0, a);
    for (let i = 1; i <= n; i++) {
      this.point(0.5, i / n, b);
      len += a.distanceTo(b);
      a.copy(b);
    }
    return len;
  }

  area(): number {
    // average width × slope length (sampled)
    let w = 0;
    const n = 8;
    for (let i = 0; i <= n; i++) w += this.widthAt(i / n);
    return (w / (n + 1)) * this.slopeLength();
  }
}

/** Build a grid BufferGeometry draped on the face. */
export function faceGeometry(face: RoofFace, nu = 26, nt = 18): THREE.BufferGeometry {
  const pos: number[] = [];
  const uv: number[] = [];
  const idx: number[] = [];
  const p = new THREE.Vector3();
  for (let j = 0; j <= nt; j++) {
    for (let i = 0; i <= nu; i++) {
      face.point(i / nu, j / nt, p);
      pos.push(p.x, p.y, p.z);
      uv.push(i / nu, 1 - j / nt);
    }
  }
  for (let j = 0; j < nt; j++) {
    for (let i = 0; i < nu; i++) {
      const a = j * (nu + 1) + i;
      const b = a + 1;
      const c = a + nu + 1;
      const d = c + 1;
      idx.push(a, c, b, b, c, d);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}
