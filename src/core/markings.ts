import * as THREE from 'three';
import { RoadSpline } from './spline';
import { MaterialManager } from './materials';

export interface MarkingOptions {
  hasCenterline: boolean;
  centerlineType: 'double_yellow' | 'dashed_white' | 'solid_white';
  hasLaneDividers: boolean;
  hasEdgeLines: boolean;
  laneCount: number;
  laneWidth: number;
  crossings?: { distance: number; width: number }[];
  arrows?: { distance: number; lane: number; type: 'through' | 'left' | 'right' | 'combo' }[];
  speedLimitMarkings?: { distance: number; speed: number }[];
  bikeLane?: boolean;
  busLane?: boolean;
}

export class MarkingGenerator {
  /**
   * Generates precision AASHTO / MUTCD compliant vector road markings.
   */
  public static buildMarkings(
    spline: RoadSpline,
    options: MarkingOptions,
    materials: MaterialManager,
    startDist: number = 0,
    endDist: number = spline.totalLength
  ): THREE.Group {
    const group = new THREE.Group();
    group.name = 'road_markings';

    const length = endDist - startDist;
    if (length < 2) return group;

    const halfW = (options.laneCount * options.laneWidth) * 0.5;
    const stepSize = 1.0;
    const numSlices = Math.max(3, Math.ceil(length / stepSize) + 1);

    const whitePos: number[] = [];
    const whiteNorm: number[] = [];
    const whiteUV: number[] = [];
    const whiteIndices: number[] = [];

    const yellowPos: number[] = [];
    const yellowNorm: number[] = [];
    const yellowUV: number[] = [];
    const yellowIndices: number[] = [];

    // Helper to add a quad ribbon
    const addRibbon = (
      pLeftList: THREE.Vector3[],
      pRightList: THREE.Vector3[],
      isYellow: boolean
    ) => {
      const pos = isYellow ? yellowPos : whitePos;
      const norm = isYellow ? yellowNorm : whiteNorm;
      const uv = isYellow ? yellowUV : whiteUV;
      const indices = isYellow ? yellowIndices : whiteIndices;

      const baseIdx = pos.length / 3;
      const n = pLeftList.length;

      for (let i = 0; i < n; i++) {
        const pL = pLeftList[i];
        const pR = pRightList[i];

        // Slight vertical offset (0.015m) above asphalt to prevent z-fighting
        pos.push(pL.x, pL.y + 0.015, pL.z);
        pos.push(pR.x, pR.y + 0.015, pR.z);

        norm.push(0, 1, 0, 0, 1, 0);
        uv.push(0, i * 0.5, 1, i * 0.5);
      }

      for (let i = 0; i < n - 1; i++) {
        const v00 = baseIdx + i * 2;
        const v01 = baseIdx + i * 2 + 1;
        const v10 = baseIdx + (i + 1) * 2;
        const v11 = baseIdx + (i + 1) * 2 + 1;

        indices.push(v00, v10, v11, v00, v11, v01);
      }
    };

    // 1. Continuous Outer Edge Fog Lines (solid white 0.15m)
    if (options.hasEdgeLines) {
      const lineWidth = 0.15;
      for (const sign of [-1, 1]) {
        const pL: THREE.Vector3[] = [];
        const pR: THREE.Vector3[] = [];

        for (let s = 0; s < numSlices; s++) {
          const d = startDist + (s / (numSlices - 1)) * length;
          const sp = spline.getSampleAtDistance(d);
          const norm = sp.normal!;

          const edgeDist = sign * (halfW - lineWidth * 0.5);
          pL.push(new THREE.Vector3().copy(sp.position).addScaledVector(norm, edgeDist - lineWidth * 0.5));
          pR.push(new THREE.Vector3().copy(sp.position).addScaledVector(norm, edgeDist + lineWidth * 0.5));
        }
        addRibbon(pL, pR, false);
      }
    }

    // 2. Centerline
    if (options.hasCenterline) {
      if (options.centerlineType === 'double_yellow') {
        // Double solid yellow: two 0.15m stripes with 0.10m space
        for (const offset of [-0.10, 0.10]) {
          const pL: THREE.Vector3[] = [];
          const pR: THREE.Vector3[] = [];
          const lineWidth = 0.12;

          for (let s = 0; s < numSlices; s++) {
            const d = startDist + (s / (numSlices - 1)) * length;
            const sp = spline.getSampleAtDistance(d);
            const norm = sp.normal!;

            pL.push(new THREE.Vector3().copy(sp.position).addScaledVector(norm, offset - lineWidth * 0.5));
            pR.push(new THREE.Vector3().copy(sp.position).addScaledVector(norm, offset + lineWidth * 0.5));
          }
          addRibbon(pL, pR, true);
        }
      } else if (options.centerlineType === 'dashed_white') {
        // Dashed white (3m stripe / 9m gap)
        const stripeLen = 3.0;
        const gapLen = 9.0;
        const cycleLen = stripeLen + gapLen;
        const numCycles = Math.floor(length / cycleLen);
        const lineWidth = 0.15;

        for (let c = 0; c < numCycles; c++) {
          const cycleStart = startDist + c * cycleLen;
          const pL: THREE.Vector3[] = [];
          const pR: THREE.Vector3[] = [];

          for (let k = 0; k <= 4; k++) {
            const d = cycleStart + (k / 4) * stripeLen;
            const sp = spline.getSampleAtDistance(d);
            const norm = sp.normal!;

            pL.push(new THREE.Vector3().copy(sp.position).addScaledVector(norm, -lineWidth * 0.5));
            pR.push(new THREE.Vector3().copy(sp.position).addScaledVector(norm, lineWidth * 0.5));
          }
          addRibbon(pL, pR, false);
        }
      }
    }

    // 3. Lane Dividers (for multi-lane roads)
    if (options.hasLaneDividers && options.laneCount > 2) {
      const stripeLen = 3.0;
      const gapLen = 9.0;
      const cycleLen = stripeLen + gapLen;
      const numCycles = Math.floor(length / cycleLen);
      const lineWidth = 0.12;

      for (let l = 1; l < options.laneCount; l++) {
        // Skip centerline if already drawn
        if (l === options.laneCount / 2 && options.hasCenterline) continue;

        const laneOffset = -halfW + l * options.laneWidth;

        for (let c = 0; c < numCycles; c++) {
          const cycleStart = startDist + c * cycleLen;
          const pL: THREE.Vector3[] = [];
          const pR: THREE.Vector3[] = [];

          for (let k = 0; k <= 4; k++) {
            const d = cycleStart + (k / 4) * stripeLen;
            const sp = spline.getSampleAtDistance(d);
            const norm = sp.normal!;

            pL.push(new THREE.Vector3().copy(sp.position).addScaledVector(norm, laneOffset - lineWidth * 0.5));
            pR.push(new THREE.Vector3().copy(sp.position).addScaledVector(norm, laneOffset + lineWidth * 0.5));
          }
          addRibbon(pL, pR, false);
        }
      }
    }

    // 4. Directional Turn Arrows & Stencils
    if (options.arrows && options.arrows.length > 0) {
      for (const arrow of options.arrows) {
        const sp = spline.getSampleAtDistance(arrow.distance);
        const norm = sp.normal!;
        const laneOffset = -halfW + (arrow.lane + 0.5) * options.laneWidth;
        const arrowCenter = new THREE.Vector3().copy(sp.position).addScaledVector(norm, laneOffset);
        arrowCenter.y += 0.02;

        const arrowMesh = this.buildArrowStencil(arrowCenter, sp.tangent!, arrow.type, materials);
        group.add(arrowMesh);
      }
    }

    // 5. Bicycle Lane Stencils
    if (options.bikeLane) {
      const bikeSpacing = 40.0;
      const numBikes = Math.max(1, Math.floor(length / bikeSpacing));
      for (let i = 1; i <= numBikes; i++) {
        const d = startDist + i * (length / (numBikes + 1));
        const sp = spline.getSampleAtDistance(d);
        const norm = sp.normal!;
        const bikeCenter = new THREE.Vector3().copy(sp.position).addScaledVector(norm, halfW - 1.0);
        bikeCenter.y += 0.02;
        const bikeMesh = this.buildBikeStencil(bikeCenter, sp.tangent!, materials);
        group.add(bikeMesh);
      }
    }

    // Assemble marking meshes
    if (whitePos.length > 0) {
      const whiteGeom = new THREE.BufferGeometry();
      whiteGeom.setAttribute('position', new THREE.Float32BufferAttribute(whitePos, 3));
      whiteGeom.setAttribute('normal', new THREE.Float32BufferAttribute(whiteNorm, 3));
      whiteGeom.setAttribute('uv', new THREE.Float32BufferAttribute(whiteUV, 2));
      whiteGeom.setIndex(whiteIndices);
      const whiteMesh = new THREE.Mesh(whiteGeom, materials.markingMaterial);
      group.add(whiteMesh);
    }

    if (yellowPos.length > 0) {
      const yellowGeom = new THREE.BufferGeometry();
      yellowGeom.setAttribute('position', new THREE.Float32BufferAttribute(yellowPos, 3));
      yellowGeom.setAttribute('normal', new THREE.Float32BufferAttribute(yellowNorm, 3));
      yellowGeom.setAttribute('uv', new THREE.Float32BufferAttribute(yellowUV, 2));
      yellowGeom.setIndex(yellowIndices);
      const yellowMesh = new THREE.Mesh(yellowGeom, materials.yellowMarkingMaterial);
      group.add(yellowMesh);
    }

    return group;
  }

  /**
   * Directional turn arrow stencils
   */
  private static buildArrowStencil(
    pos: THREE.Vector3,
    tangent: THREE.Vector3,
    type: 'through' | 'left' | 'right' | 'combo',
    materials: MaterialManager
  ): THREE.Mesh {
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 512;
    const ctx = canvas.getContext('2d')!;

    ctx.fillStyle = 'rgba(0, 0, 0, 0)';
    ctx.fillRect(0, 0, 256, 512);

    ctx.fillStyle = '#ffffff';
    ctx.lineWidth = 24;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';

    if (type === 'through') {
      // Straight through arrow
      ctx.fillRect(116, 180, 24, 300);
      ctx.beginPath();
      ctx.moveTo(128, 40);
      ctx.lineTo(60, 200);
      ctx.lineTo(196, 200);
      ctx.closePath();
      ctx.fill();
    } else if (type === 'left') {
      // Curved left turn arrow
      ctx.beginPath();
      ctx.moveTo(128, 480);
      ctx.quadraticCurveTo(128, 250, 40, 250);
      ctx.strokeStyle = '#ffffff';
      ctx.stroke();
      // Arrowhead
      ctx.beginPath();
      ctx.moveTo(20, 250);
      ctx.lineTo(90, 190);
      ctx.lineTo(90, 310);
      ctx.closePath();
      ctx.fill();
    } else if (type === 'right') {
      // Curved right turn arrow
      ctx.beginPath();
      ctx.moveTo(128, 480);
      ctx.quadraticCurveTo(128, 250, 216, 250);
      ctx.strokeStyle = '#ffffff';
      ctx.stroke();
      // Arrowhead
      ctx.beginPath();
      ctx.moveTo(236, 250);
      ctx.lineTo(166, 190);
      ctx.lineTo(166, 310);
      ctx.closePath();
      ctx.fill();
    } else {
      // Combo through-left
      ctx.fillRect(116, 180, 24, 300);
      ctx.beginPath();
      ctx.moveTo(128, 40);
      ctx.lineTo(70, 190);
      ctx.lineTo(186, 190);
      ctx.closePath();
      ctx.fill();

      ctx.beginPath();
      ctx.moveTo(128, 360);
      ctx.quadraticCurveTo(128, 240, 40, 240);
      ctx.strokeStyle = '#ffffff';
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(20, 240);
      ctx.lineTo(80, 190);
      ctx.lineTo(80, 290);
      ctx.closePath();
      ctx.fill();
    }

    const tex = new THREE.CanvasTexture(canvas);
    const mat = new THREE.MeshStandardMaterial({
      map: tex,
      transparent: true,
      roughness: 0.4,
      polygonOffset: true,
      polygonOffsetFactor: -3
    });

    const geom = new THREE.PlaneGeometry(1.6, 3.8);
    geom.rotateX(-Math.PI * 0.5);
    const mesh = new THREE.Mesh(geom, mat);
    mesh.position.copy(pos);
    mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), tangent);
    mesh.rotateY(Math.PI);
    return mesh;
  }

  /**
   * Bicycle lane stencil
   */
  private static buildBikeStencil(
    pos: THREE.Vector3,
    tangent: THREE.Vector3,
    materials: MaterialManager
  ): THREE.Mesh {
    const canvas = document.createElement('canvas');
    canvas.width = 256;
    canvas.height = 256;
    const ctx = canvas.getContext('2d')!;

    ctx.fillStyle = 'rgba(0, 0, 0, 0)';
    ctx.fillRect(0, 0, 256, 256);

    ctx.strokeStyle = '#ffffff';
    ctx.fillStyle = '#ffffff';
    ctx.lineWidth = 14;

    // Bicycle wheels
    ctx.beginPath();
    ctx.arc(60, 180, 40, 0, Math.PI * 2);
    ctx.stroke();

    ctx.beginPath();
    ctx.arc(196, 180, 40, 0, Math.PI * 2);
    ctx.stroke();

    // Frame
    ctx.beginPath();
    ctx.moveTo(60, 180);
    ctx.lineTo(110, 130);
    ctx.lineTo(180, 130);
    ctx.lineTo(196, 180);
    ctx.lineTo(130, 180);
    ctx.lineTo(110, 130);
    ctx.stroke();

    // Cyclist head
    ctx.beginPath();
    ctx.arc(130, 70, 16, 0, Math.PI * 2);
    ctx.fill();

    const tex = new THREE.CanvasTexture(canvas);
    const mat = new THREE.MeshStandardMaterial({
      map: tex,
      transparent: true,
      roughness: 0.4,
      polygonOffset: true,
      polygonOffsetFactor: -3
    });

    const geom = new THREE.PlaneGeometry(1.4, 1.4);
    geom.rotateX(-Math.PI * 0.5);
    const mesh = new THREE.Mesh(geom, mat);
    mesh.position.copy(pos);
    mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), tangent);
    return mesh;
  }
}
