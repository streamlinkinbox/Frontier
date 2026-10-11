import * as THREE from 'three';

export interface SplinePoint {
  position: THREE.Vector3;
  tangent?: THREE.Vector3;
  normal?: THREE.Vector3;
  binormal?: THREE.Vector3;
  distance: number;
  elevation: number;
  grade: number; // percentage
  banking: number; // radians
}

export class RoadSpline {
  public points: THREE.Vector3[] = [];
  public closed: boolean = false;
  private samples: SplinePoint[] = [];
  public totalLength: number = 0;

  constructor(controlPoints: THREE.Vector3[] = [], closed: boolean = false) {
    this.points = controlPoints.map(p => p.clone());
    this.closed = closed;
    this.recompute();
  }

  public setPoints(points: THREE.Vector3[]) {
    this.points = points.map(p => p.clone());
    this.recompute();
  }

  public addPoint(p: THREE.Vector3) {
    this.points.push(p.clone());
    this.recompute();
  }

  public insertPoint(index: number, p: THREE.Vector3) {
    this.points.splice(index, 0, p.clone());
    this.recompute();
  }

  public removePoint(index: number) {
    if (this.points.length > 2) {
      this.points.splice(index, 1);
      this.recompute();
    }
  }

  public updatePoint(index: number, p: THREE.Vector3) {
    if (index >= 0 && index < this.points.length) {
      this.points[index].copy(p);
      this.recompute();
    }
  }

  /**
   * Catmull-Rom centripetal spline evaluation
   */
  public evaluate(t: number): THREE.Vector3 {
    if (this.points.length === 0) return new THREE.Vector3();
    if (this.points.length === 1) return this.points[0].clone();
    if (this.points.length === 2) {
      return new THREE.Vector3().lerpVectors(this.points[0], this.points[1], t);
    }

    const count = this.closed ? this.points.length : this.points.length - 1;
    const scaledT = Math.max(0, Math.min(0.999999, t)) * count;
    const segment = Math.floor(scaledT);
    const localT = scaledT - segment;

    const p0 = this.getPointClamped(segment - 1);
    const p1 = this.getPointClamped(segment);
    const p2 = this.getPointClamped(segment + 1);
    const p3 = this.getPointClamped(segment + 2);

    return this.catmullRom(p0, p1, p2, p3, localT);
  }

  private getPointClamped(i: number): THREE.Vector3 {
    const n = this.points.length;
    if (this.closed) {
      return this.points[((i % n) + n) % n];
    }
    return this.points[Math.max(0, Math.min(n - 1, i))];
  }

  private catmullRom(p0: THREE.Vector3, p1: THREE.Vector3, p2: THREE.Vector3, p3: THREE.Vector3, t: number): THREE.Vector3 {
    const t2 = t * t;
    const t3 = t2 * t;

    const v0 = (p2.x - p0.x) * 0.5;
    const v1 = (p3.x - p1.x) * 0.5;
    const x = (2 * p1.x - 2 * p2.x + v0 + v1) * t3 + (-3 * p1.x + 3 * p2.x - 2 * v0 - v1) * t2 + v0 * t + p1.x;

    const vy0 = (p2.y - p0.y) * 0.5;
    const vy1 = (p3.y - p1.y) * 0.5;
    const y = (2 * p1.y - 2 * p2.y + vy0 + vy1) * t3 + (-3 * p1.y + 3 * p2.y - 2 * vy0 - vy1) * t2 + vy0 * t + p1.y;

    const vz0 = (p2.z - p0.z) * 0.5;
    const vz1 = (p3.z - p1.z) * 0.5;
    const z = (2 * p1.z - 2 * p2.z + vz0 + vz1) * t3 + (-3 * p1.z + 3 * p2.z - 2 * vz0 - vz1) * t2 + vz0 * t + p1.z;

    return new THREE.Vector3(x, y, z);
  }

  /**
   * Recompute arc-length lookup table, Frenet/RMF frames, grade, and civil banking.
   */
  public recompute(numSamples: number = 300) {
    if (this.points.length < 2) {
      this.samples = [];
      this.totalLength = 0;
      return;
    }

    this.samples = [];
    let cumulativeDist = 0;
    let prevPos = this.evaluate(0);

    this.samples.push({
      position: prevPos,
      distance: 0,
      elevation: prevPos.y,
      grade: 0,
      banking: 0
    });

    for (let i = 1; i <= numSamples; i++) {
      const t = i / numSamples;
      const pos = this.evaluate(t);
      const stepDist = prevPos.distanceTo(pos);
      cumulativeDist += stepDist;

      this.samples.push({
        position: pos,
        distance: cumulativeDist,
        elevation: pos.y,
        grade: 0,
        banking: 0
      });
      prevPos = pos;
    }

    this.totalLength = cumulativeDist;

    // Compute tangents, grades, normals, and banking
    for (let i = 0; i <= numSamples; i++) {
      const prevIdx = Math.max(0, i - 1);
      const nextIdx = Math.min(numSamples, i + 1);
      const pPrev = this.samples[prevIdx].position;
      const pNext = this.samples[nextIdx].position;

      const tangent = new THREE.Vector3().subVectors(pNext, pPrev).normalize();
      if (tangent.lengthSq() < 1e-6) tangent.set(0, 0, 1);

      // Grade (% slope)
      const horizontalDist = Math.hypot(pNext.x - pPrev.x, pNext.z - pPrev.z);
      const dy = pNext.y - pPrev.y;
      const grade = horizontalDist > 1e-4 ? (dy / horizontalDist) * 100 : 0;

      // Planar normal (right side of road)
      const normal = new THREE.Vector3(tangent.z, 0, -tangent.x).normalize();

      // Binormal (up vector)
      const binormal = new THREE.Vector3().crossVectors(normal, tangent).normalize();

      // Civil banking (superelevation): compute curvature radius in horizontal plane
      let curvature = 0;
      if (i > 0 && i < numSamples) {
        const tPrev = new THREE.Vector3().subVectors(this.samples[i].position, this.samples[i - 1].position).normalize();
        const tNext = new THREE.Vector3().subVectors(this.samples[i + 1].position, this.samples[i].position).normalize();
        const angle = tPrev.angleTo(tNext);
        const ds = (this.samples[i + 1].distance - this.samples[i - 1].distance) * 0.5;
        if (ds > 1e-4) curvature = angle / ds;
      }
      // Speed 80 km/h nominal -> banking e = v^2 / (127 * R), max 6% (0.06 rad)
      const radius = curvature > 1e-5 ? 1 / curvature : 1e6;
      const banking = Math.min(0.06, Math.max(-0.06, (6400) / (127 * radius * 9.81)));

      this.samples[i].tangent = tangent;
      this.samples[i].normal = normal;
      this.samples[i].binormal = binormal;
      this.samples[i].grade = grade;
      this.samples[i].banking = banking;
    }
  }

  /**
   * Sample spline by exact distance along alignment (meters)
   */
  public getSampleAtDistance(dist: number): SplinePoint {
    if (this.samples.length === 0) {
      return {
        position: new THREE.Vector3(),
        tangent: new THREE.Vector3(0, 0, 1),
        normal: new THREE.Vector3(1, 0, 0),
        binormal: new THREE.Vector3(0, 1, 0),
        distance: 0,
        elevation: 0,
        grade: 0,
        banking: 0
      };
    }

    const clampedDist = Math.max(0, Math.min(this.totalLength, dist));

    // Binary search for sample interval
    let low = 0;
    let high = this.samples.length - 1;
    while (low <= high) {
      const mid = Math.floor((low + high) / 2);
      if (this.samples[mid].distance < clampedDist) {
        low = mid + 1;
      } else {
        high = mid - 1;
      }
    }

    const idx1 = Math.max(0, Math.min(this.samples.length - 1, low - 1));
    const idx2 = Math.min(this.samples.length - 1, idx1 + 1);

    if (idx1 === idx2) return this.samples[idx1];

    const s1 = this.samples[idx1];
    const s2 = this.samples[idx2];
    const segmentLen = s2.distance - s1.distance;
    const factor = segmentLen > 1e-5 ? (clampedDist - s1.distance) / segmentLen : 0;

    const pos = new THREE.Vector3().lerpVectors(s1.position, s2.position, factor);
    const tan = new THREE.Vector3().lerpVectors(s1.tangent!, s2.tangent!, factor).normalize();
    const norm = new THREE.Vector3().lerpVectors(s1.normal!, s2.normal!, factor).normalize();
    const binorm = new THREE.Vector3().lerpVectors(s1.binormal!, s2.binormal!, factor).normalize();

    return {
      position: pos,
      tangent: tan,
      normal: norm,
      binormal: binorm,
      distance: clampedDist,
      elevation: THREE.MathUtils.lerp(s1.elevation, s2.elevation, factor),
      grade: THREE.MathUtils.lerp(s1.grade, s2.grade, factor),
      banking: THREE.MathUtils.lerp(s1.banking, s2.banking, factor)
    };
  }

  public getPointAt(t: number): THREE.Vector3 {
    return this.getSampleAtDistance(t * this.totalLength).position;
  }

  public getTangentAt(t: number): THREE.Vector3 {
    return this.getSampleAtDistance(t * this.totalLength).tangent!;
  }
}
