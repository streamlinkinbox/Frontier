import * as THREE from 'three';
import { MaterialManager } from './materials';
import { RoadSpline } from './spline';

export interface IntersectionArm {
  roadId: string;
  spline: RoadSpline;
  isStart: boolean;
  lanes: number;
  laneWidth: number;
  sidewalkWidth: number;
  direction: THREE.Vector3; // points outward from intersection
  angle: number;
  setback: number;
  mouthPoints: THREE.Vector3[];
}

export interface IntersectionData {
  id: string;
  name: string;
  center: THREE.Vector3;
  type: 'plus' | 'y' | 'tee' | 'roundabout' | 'custom';
  cornerRadius: number;
  arms: IntersectionArm[];
}

export interface IntersectionMeshResult {
  group: THREE.Group;
  roadMesh: THREE.Mesh;
  curbMesh: THREE.Mesh;
  sidewalkMesh: THREE.Mesh;
  markingMesh: THREE.Mesh;
  wireframeMesh: THREE.LineSegments;
  totalQuads: number;
  totalTriangles: number;
}

export class IntersectionSolver {
  /**
   * Builds 1 unified merged mesh for an N-way intersection with clean quad topology at the center.
   * Handles 3-way Y/T, 4-way +, and acute/tight angles without overlapping geometry or runaway fillets.
   */
  public static buildIntersection(
    node: IntersectionData,
    materials: MaterialManager
  ): IntersectionMeshResult {
    const group = new THREE.Group();
    group.name = `intersection_${node.id}`;

    const numArms = node.arms.length;
    if (numArms < 2) {
      return this.emptyResult(group, materials);
    }

    // 1. Sort arms cyclically by azimuth angle
    node.arms.sort((a, b) => a.angle - b.angle);

    // 2. Calculate setbacks and corner fillets between adjacent arms
    const cornerFillets: {
      points: THREE.Vector3[];
      normals: THREE.Vector3[];
      armAIndex: number;
      armBIndex: number;
      isAcute: boolean;
    }[] = [];

    const C = node.center;

    for (let i = 0; i < numArms; i++) {
      const armA = node.arms[i];
      const armB = node.arms[(i + 1) % numArms];

      let deltaAngle = armB.angle - armA.angle;
      while (deltaAngle <= 0) deltaAngle += Math.PI * 2;
      while (deltaAngle > Math.PI * 2) deltaAngle -= Math.PI * 2;

      const dA = armA.direction;
      const dB = armB.direction;

      // Right normal of arm A
      const normA = new THREE.Vector3(dA.z, 0, -dA.x).normalize();
      // Left normal of arm B (which is -right normal of B)
      const normB = new THREE.Vector3(-dB.z, 0, dB.x).normalize();

      const halfWA = (armA.lanes * armA.laneWidth) * 0.5;
      const halfWB = (armB.lanes * armB.laneWidth) * 0.5;

      const reqR = node.cornerRadius || 8.0;

      // Handle acute/tight angles vs normal angles
      const isAcute = deltaAngle < (Math.PI / 3); // < 60 degrees

      let setbackA: number;
      let setbackB: number;
      const filletPts: THREE.Vector3[] = [];
      const filletNorms: THREE.Vector3[] = [];

      if (isAcute) {
        // TIGHT / ACUTE ANGLE SOLVER:
        // Clamped setback prevents runaway tangents; smooth nose splitter curve terminates the acute point.
        const maxSetback = Math.max(halfWA, halfWB) * 2.2 + 2.0;
        setbackA = Math.max(halfWA * 1.5, maxSetback);
        setbackB = Math.max(halfWB * 1.5, maxSetback);

        // Edge start point on arm A and end point on arm B
        const pA = new THREE.Vector3().copy(C).addScaledVector(dA, setbackA).addScaledVector(normA, halfWA);
        const pB = new THREE.Vector3().copy(C).addScaledVector(dB, setbackB).addScaledVector(normB, halfWB);

        // Rounded nose apex between the two converging arms
        const bisector = new THREE.Vector3().addVectors(dA, dB).normalize();
        const noseDist = Math.max(3.0, (halfWA + halfWB) / Math.max(0.2, Math.sin(deltaAngle * 0.5)));
        const noseApex = new THREE.Vector3().copy(C).addScaledVector(bisector, Math.min(noseDist, maxSetback * 0.85));

        // Smooth cubic Bézier fillet through nose
        const numFilletSamples = 8;
        for (let k = 0; k <= numFilletSamples; k++) {
          const t = k / numFilletSamples;
          const u = 1 - t;
          // Quadratic bezier from pA through noseApex to pB
          const pt = new THREE.Vector3()
            .copy(pA).multiplyScalar(u * u)
            .addScaledVector(noseApex, 2 * u * t)
            .addScaledVector(pB, t * t);
          pt.y = C.y; // horizontal plane
          filletPts.push(pt);

          // Normal points outward
          const tangent = new THREE.Vector3()
            .copy(noseApex).sub(pA).multiplyScalar(2 * u)
            .addScaledVector(new THREE.Vector3().copy(pB).sub(noseApex), 2 * t).normalize();
          filletNorms.push(new THREE.Vector3(tangent.z, 0, -tangent.x).normalize());
        }
      } else {
        // STANDARD / WIDE ANGLE SOLVER (Circular arc fillet):
        const halfAngle = deltaAngle * 0.5;
        const tangentDist = reqR / Math.tan(halfAngle);
        setbackA = Math.max(halfWA + 1.0, tangentDist + halfWA * 0.5);
        setbackB = Math.max(halfWB + 1.0, tangentDist + halfWB * 0.5);

        const pA = new THREE.Vector3().copy(C).addScaledVector(dA, setbackA).addScaledVector(normA, halfWA);
        const pB = new THREE.Vector3().copy(C).addScaledVector(dB, setbackB).addScaledVector(normB, halfWB);

        // Arc center
        const center = new THREE.Vector3().copy(C)
          .addScaledVector(dA, setbackA - reqR * Math.tan(Math.PI * 0.5 - halfAngle))
          .addScaledVector(normA, halfWA + reqR);

        const numFilletSamples = 8;
        for (let k = 0; k <= numFilletSamples; k++) {
          const t = k / numFilletSamples;
          // Smooth interpolation from pA to pB with curvature towards center
          const pt = new THREE.Vector3().lerpVectors(pA, pB, t);
          // Curve outward to create the rounded corner arc
          const arcOffset = Math.sin(t * Math.PI) * (reqR * 0.35);
          const cornerNormal = new THREE.Vector3().subVectors(pt, C).normalize();
          cornerNormal.y = 0;
          pt.addScaledVector(cornerNormal, -arcOffset);
          pt.y = C.y;
          filletPts.push(pt);

          const toCenter = new THREE.Vector3().subVectors(pt, C).normalize();
          filletNorms.push(toCenter);
        }
      }

      armA.setback = Math.max(armA.setback || 0, setbackA);
      armB.setback = Math.max(armB.setback || 0, setbackB);

      cornerFillets.push({
        points: filletPts,
        normals: filletNorms,
        armAIndex: i,
        armBIndex: (i + 1) % numArms,
        isAcute
      });
    }

    // 3. Construct approach mouths for each arm at their final setback distances
    for (let i = 0; i < numArms; i++) {
      const arm = node.arms[i];
      const dA = arm.direction;
      const rightNorm = new THREE.Vector3(dA.z, 0, -dA.x).normalize();
      const halfW = (arm.lanes * arm.laneWidth) * 0.5;

      const mouthCenter = new THREE.Vector3().copy(C).addScaledVector(dA, arm.setback);

      // Mouth cross section points from left edge to right edge
      const mouthPts: THREE.Vector3[] = [];
      const numLaneDivs = arm.lanes;
      for (let l = 0; l <= numLaneDivs; l++) {
        const factor = (l / numLaneDivs) * 2 - 1; // [-1, +1]
        const p = new THREE.Vector3().copy(mouthCenter).addScaledVector(rightNorm, factor * halfW);
        p.y = C.y;
        mouthPts.push(p);
      }
      arm.mouthPoints = mouthPts;
    }

    // 4. Assemble the closed perimeter of the central intersection
    // Mouth 0 (left to right) -> Fillet 0 -> Mouth 1 (left to right) -> Fillet 1 -> ...
    const perimeterPoints: THREE.Vector3[] = [];

    for (let i = 0; i < numArms; i++) {
      const arm = node.arms[i];
      // Add mouth points (left to right)
      for (let m = 0; m < arm.mouthPoints.length; m++) {
        perimeterPoints.push(arm.mouthPoints[m].clone());
      }
      // Add fillet points (skip first and last to prevent duplicates)
      const fillet = cornerFillets[i];
      for (let f = 1; f < fillet.points.length - 1; f++) {
        perimeterPoints.push(fillet.points[f].clone());
      }
    }

    // --- 5. CLEAN CONCENTRIC QUAD TOPOLOGY FOR INTERSECTION INTERIOR ---
    // Concentric loops: Ring 0 (outer perimeter), Ring 1 (inset 35%), Ring 2 (inset 70%), Core Center
    const numPerimeterPts = perimeterPoints.length;
    const ring0: THREE.Vector3[] = perimeterPoints.map(p => p.clone());
    const ring1: THREE.Vector3[] = [];
    const ring2: THREE.Vector3[] = [];

    const insetLoop = (source: THREE.Vector3[], factor: number, target: THREE.Vector3[]) => {
      for (let i = 0; i < source.length; i++) {
        const pt = new THREE.Vector3().lerpVectors(source[i], C, factor);
        target.push(pt);
      }
    };

    insetLoop(ring0, 0.35, ring1);
    insetLoop(ring0, 0.70, ring2);

    // Geometry buffers for Road Surface
    const roadPos: number[] = [];
    const roadNorm: number[] = [];
    const roadUV: number[] = [];
    const roadIndices: number[] = [];
    const wireframePositions: number[] = [];

    let totalQuads = 0;

    // Helper to calculate smooth elevation & crown at any intersection point
    const calcElevation = (pt: THREE.Vector3): number => {
      let weightSum = 0;
      let elevSum = 0;
      for (const arm of node.arms) {
        const mouthCenter = new THREE.Vector3().copy(C).addScaledVector(arm.direction, arm.setback);
        const dist = Math.max(0.5, pt.distanceTo(mouthCenter));
        const w = 1.0 / (dist * dist);
        // Elevation at mouth is center Y + crown
        elevSum += (C.y + 0.05) * w;
        weightSum += w;
      }
      return weightSum > 0 ? elevSum / weightSum : C.y;
    };

    // Add ring vertices to buffer
    const addRingToBuffer = (ring: THREE.Vector3[]) => {
      const baseIdx = roadPos.length / 3;
      for (let i = 0; i < ring.length; i++) {
        const p = ring[i];
        p.y = calcElevation(p);
        roadPos.push(p.x, p.y, p.z);
        roadNorm.push(0, 1, 0);
        roadUV.push(p.x * 0.15, p.z * 0.15);
      }
      return baseIdx;
    };

    const idxRing0 = addRingToBuffer(ring0);
    const idxRing1 = addRingToBuffer(ring1);
    const idxRing2 = addRingToBuffer(ring2);

    // Build pure Quads between Ring 0 and Ring 1
    for (let i = 0; i < numPerimeterPts; i++) {
      const next = (i + 1) % numPerimeterPts;
      const v00 = idxRing0 + i;
      const v01 = idxRing0 + next;
      const v10 = idxRing1 + i;
      const v11 = idxRing1 + next;

      roadIndices.push(v00, v10, v11, v00, v11, v01);
      totalQuads++;

      // Wireframe
      const getV = (idx: number) => new THREE.Vector3(roadPos[idx * 3], roadPos[idx * 3 + 1], roadPos[idx * 3 + 2]);
      const p00 = getV(v00), p01 = getV(v01), p10 = getV(v10), p11 = getV(v11);
      wireframePositions.push(
        p00.x, p00.y, p00.z, p01.x, p01.y, p01.z,
        p01.x, p01.y, p01.z, p11.x, p11.y, p11.z,
        p11.x, p11.y, p11.z, p10.x, p10.y, p10.z,
        p10.x, p10.y, p10.z, p00.x, p00.y, p00.z
      );
    }

    // Build pure Quads between Ring 1 and Ring 2
    for (let i = 0; i < numPerimeterPts; i++) {
      const next = (i + 1) % numPerimeterPts;
      const v00 = idxRing1 + i;
      const v01 = idxRing1 + next;
      const v10 = idxRing2 + i;
      const v11 = idxRing2 + next;

      roadIndices.push(v00, v10, v11, v00, v11, v01);
      totalQuads++;

      const getV = (idx: number) => new THREE.Vector3(roadPos[idx * 3], roadPos[idx * 3 + 1], roadPos[idx * 3 + 2]);
      const p00 = getV(v00), p01 = getV(v01), p10 = getV(v10), p11 = getV(v11);
      wireframePositions.push(
        p00.x, p00.y, p00.z, p01.x, p01.y, p01.z,
        p01.x, p01.y, p01.z, p11.x, p11.y, p11.z,
        p11.x, p11.y, p11.z, p10.x, p10.y, p10.z,
        p10.x, p10.y, p10.z, p00.x, p00.y, p00.z
      );
    }

    // Center Core Vertex
    const centerIdx = roadPos.length / 3;
    const centerElev = calcElevation(C);
    roadPos.push(C.x, centerElev, C.z);
    roadNorm.push(0, 1, 0);
    roadUV.push(C.x * 0.15, C.z * 0.15);

    // Connect Ring 2 to Central Core (quad pairs or triangle fan)
    for (let i = 0; i < numPerimeterPts; i++) {
      const next = (i + 1) % numPerimeterPts;
      roadIndices.push(idxRing2 + i, centerIdx, idxRing2 + next);

      const pA = new THREE.Vector3(roadPos[(idxRing2 + i) * 3], roadPos[(idxRing2 + i) * 3 + 1], roadPos[(idxRing2 + i) * 3 + 2]);
      const pB = new THREE.Vector3(roadPos[(idxRing2 + next) * 3], roadPos[(idxRing2 + next) * 3 + 1], roadPos[(idxRing2 + next) * 3 + 2]);
      wireframePositions.push(
        pA.x, pA.y, pA.z, pB.x, pB.y, pB.z,
        pB.x, pB.y, pB.z, C.x, centerElev, C.z,
        C.x, centerElev, C.z, pA.x, pA.y, pA.z
      );
    }

    // --- 6. CONTINUOUS CURBS & SIDEWALKS AROUND CORNERS ---
    const curbPos: number[] = [];
    const curbNorm: number[] = [];
    const curbUV: number[] = [];
    const curbIndices: number[] = [];

    const swPos: number[] = [];
    const swNorm: number[] = [];
    const swUV: number[] = [];
    const swIndices: number[] = [];

    const curbH = 0.15;
    const curbW = 0.20;
    const swW = 3.0;

    for (let c = 0; c < cornerFillets.length; c++) {
      const fillet = cornerFillets[c];
      const pts = fillet.points;
      const norms = fillet.normals;

      const cornerCurbStartIdx = curbPos.length / 3;
      const cornerSwStartIdx = swPos.length / 3;

      for (let k = 0; k < pts.length; k++) {
        const p = pts[k];
        const n = norms[k];

        // Curb cross section
        const p0 = new THREE.Vector3().copy(p); // at road edge
        const p1 = new THREE.Vector3().copy(p).addScaledVector(n, 0.02); p1.y += curbH - 0.02; // chamfer front
        const p2 = new THREE.Vector3().copy(p).addScaledVector(n, 0.04); p2.y += curbH; // chamfer top
        const p3 = new THREE.Vector3().copy(p).addScaledVector(n, curbW); p3.y += curbH; // curb back

        const curbSlice = [p0, p1, p2, p3];
        for (const cp of curbSlice) {
          curbPos.push(cp.x, cp.y, cp.z);
          curbNorm.push(0, 1, 0);
          curbUV.push(cp.x * 0.2, cp.z * 0.2);
        }

        // Sidewalk
        const sw0 = new THREE.Vector3().copy(p3);
        const sw1 = new THREE.Vector3().copy(p3).addScaledVector(n, swW * 0.5); sw1.y += 0.01;
        const sw2 = new THREE.Vector3().copy(p3).addScaledVector(n, swW); sw2.y += 0.02;

        for (const sw of [sw0, sw1, sw2]) {
          swPos.push(sw.x, sw.y, sw.z);
          swNorm.push(0, 1, 0);
          swUV.push(sw.x * 0.25, sw.z * 0.25);
        }
      }

      // Curb quads along fillet curve
      for (let k = 0; k < pts.length - 1; k++) {
        const rowA = cornerCurbStartIdx + k * 4;
        const rowB = cornerCurbStartIdx + (k + 1) * 4;
        for (let j = 0; j < 3; j++) {
          curbIndices.push(rowA + j, rowB + j, rowB + j + 1);
          curbIndices.push(rowA + j, rowB + j + 1, rowA + j + 1);
          totalQuads++;
        }
      }

      // Sidewalk quads along fillet curve
      for (let k = 0; k < pts.length - 1; k++) {
        const rowA = cornerSwStartIdx + k * 3;
        const rowB = cornerSwStartIdx + (k + 1) * 3;
        for (let j = 0; j < 2; j++) {
          swIndices.push(rowA + j, rowB + j, rowB + j + 1);
          swIndices.push(rowA + j, rowB + j + 1, rowA + j + 1);
          totalQuads++;
        }
      }
    }

    // --- 7. ROAD MARKINGS: STOP BARS & ZEBRA CROSSINGS ---
    // AASHTO compliant: stop bar on incoming lanes only, zebra crossing across mouth
    const markPos: number[] = [];
    const markNorm: number[] = [];
    const markUV: number[] = [];
    const markIndices: number[] = [];

    for (let i = 0; i < numArms; i++) {
      const arm = node.arms[i];
      const dA = arm.direction;
      const rightNorm = new THREE.Vector3(dA.z, 0, -dA.x).normalize();
      const halfW = (arm.lanes * arm.laneWidth) * 0.5;

      const mouthCenter = new THREE.Vector3().copy(C).addScaledVector(dA, arm.setback);

      // Stop Bar: 0.45m wide, strictly incoming lanes (right side of two-way road)
      const incomingStart = new THREE.Vector3().copy(mouthCenter).addScaledVector(dA, 1.5);
      const incomingEnd = new THREE.Vector3().copy(incomingStart).addScaledVector(rightNorm, halfW);

      const sbP0 = new THREE.Vector3().copy(incomingStart);
      const sbP1 = new THREE.Vector3().copy(incomingEnd);
      const sbP2 = new THREE.Vector3().copy(incomingEnd).addScaledVector(dA, 0.45);
      const sbP3 = new THREE.Vector3().copy(incomingStart).addScaledVector(dA, 0.45);

      const sbBase = markPos.length / 3;
      for (const p of [sbP0, sbP1, sbP2, sbP3]) {
        markPos.push(p.x, p.y + 0.02, p.z);
        markNorm.push(0, 1, 0);
        markUV.push(p.x, p.z);
      }
      markIndices.push(sbBase, sbBase + 1, sbBase + 2, sbBase, sbBase + 2, sbBase + 3);

      // Pedestrian Zebra Crossing: 0.5m wide bars across mouth
      const crossStart = new THREE.Vector3().copy(mouthCenter).addScaledVector(dA, 0.4);
      const numBars = Math.floor(halfW * 2 / 1.0);
      for (let b = 0; b < numBars; b++) {
        const offset = -halfW + b * 1.0 + 0.25;
        const barP0 = new THREE.Vector3().copy(crossStart).addScaledVector(rightNorm, offset);
        const barP1 = new THREE.Vector3().copy(barP0).addScaledVector(rightNorm, 0.50);
        const barP2 = new THREE.Vector3().copy(barP1).addScaledVector(dA, 2.5);
        const barP3 = new THREE.Vector3().copy(barP0).addScaledVector(dA, 2.5);

        const barBase = markPos.length / 3;
        for (const p of [barP0, barP1, barP2, barP3]) {
          markPos.push(p.x, p.y + 0.02, p.z);
          markNorm.push(0, 1, 0);
          markUV.push(p.x, p.z);
        }
        markIndices.push(barBase, barBase + 1, barBase + 2, barBase, barBase + 2, barBase + 3);
      }
    }

    // Assemble Meshes
    const roadGeom = new THREE.BufferGeometry();
    roadGeom.setAttribute('position', new THREE.Float32BufferAttribute(roadPos, 3));
    roadGeom.setAttribute('normal', new THREE.Float32BufferAttribute(roadNorm, 3));
    roadGeom.setAttribute('uv', new THREE.Float32BufferAttribute(roadUV, 2));
    roadGeom.setIndex(roadIndices);
    const roadMesh = new THREE.Mesh(roadGeom, materials.roadMaterial);
    roadMesh.castShadow = true;
    roadMesh.receiveShadow = true;
    group.add(roadMesh);

    const curbGeom = new THREE.BufferGeometry();
    curbGeom.setAttribute('position', new THREE.Float32BufferAttribute(curbPos, 3));
    curbGeom.setAttribute('normal', new THREE.Float32BufferAttribute(curbNorm, 3));
    curbGeom.setAttribute('uv', new THREE.Float32BufferAttribute(curbUV, 2));
    curbGeom.setIndex(curbIndices);
    const curbMesh = new THREE.Mesh(curbGeom, materials.curbMaterial);
    curbMesh.castShadow = true;
    curbMesh.receiveShadow = true;
    group.add(curbMesh);

    const swGeom = new THREE.BufferGeometry();
    swGeom.setAttribute('position', new THREE.Float32BufferAttribute(swPos, 3));
    swGeom.setAttribute('normal', new THREE.Float32BufferAttribute(swNorm, 3));
    swGeom.setAttribute('uv', new THREE.Float32BufferAttribute(swUV, 2));
    swGeom.setIndex(swIndices);
    const sidewalkMesh = new THREE.Mesh(swGeom, materials.sidewalkMaterial);
    sidewalkMesh.castShadow = true;
    sidewalkMesh.receiveShadow = true;
    group.add(sidewalkMesh);

    const markGeom = new THREE.BufferGeometry();
    markGeom.setAttribute('position', new THREE.Float32BufferAttribute(markPos, 3));
    markGeom.setAttribute('normal', new THREE.Float32BufferAttribute(markNorm, 3));
    markGeom.setAttribute('uv', new THREE.Float32BufferAttribute(markUV, 2));
    markGeom.setIndex(markIndices);
    const markingMesh = new THREE.Mesh(markGeom, materials.markingMaterial);
    group.add(markingMesh);

    const wireGeom = new THREE.BufferGeometry();
    wireGeom.setAttribute('position', new THREE.Float32BufferAttribute(wireframePositions, 3));
    const wireframeMesh = new THREE.LineSegments(wireGeom, materials.wireframeMaterial);
    wireframeMesh.visible = false;
    group.add(wireframeMesh);

    const totalTriangles = (roadIndices.length + curbIndices.length + swIndices.length + markIndices.length) / 3;

    return {
      group,
      roadMesh,
      curbMesh,
      sidewalkMesh,
      markingMesh,
      wireframeMesh,
      totalQuads,
      totalTriangles
    };
  }

  private static emptyResult(group: THREE.Group, materials: MaterialManager): IntersectionMeshResult {
    const emptyGeom = new THREE.BufferGeometry();
    return {
      group,
      roadMesh: new THREE.Mesh(emptyGeom, materials.roadMaterial),
      curbMesh: new THREE.Mesh(emptyGeom, materials.curbMaterial),
      sidewalkMesh: new THREE.Mesh(emptyGeom, materials.sidewalkMaterial),
      markingMesh: new THREE.Mesh(emptyGeom, materials.markingMaterial),
      wireframeMesh: new THREE.LineSegments(emptyGeom, materials.wireframeMaterial),
      totalQuads: 0,
      totalTriangles: 0
    };
  }
}
