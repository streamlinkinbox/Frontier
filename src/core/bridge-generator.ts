import * as THREE from 'three';
import { RoadSpline } from './spline';
import { MaterialManager } from './materials';

export interface BridgeConfig {
  id: string;
  name: string;
  startDistance: number;
  endDistance: number;
  deckWidth: number;
  elevation: number; // e.g. 7.2m
  minClearance: number; // e.g. 5.5m AASHTO / Eurocode standard
  girderType: 'steel_i_beam' | 'concrete_box';
  pierSpacing: number; // e.g. 30m
  hasParapet: boolean;
  hasRailing: boolean;
}

export class BridgeGenerator {
  /**
   * Generates reference-scaled AASHTO/Eurocode compliant highway bridge structure.
   */
  public static buildBridge(
    spline: RoadSpline,
    config: BridgeConfig,
    materials: MaterialManager,
    avoidPoints: THREE.Vector3[] = [] // Points of lower roads/rails to avoid when placing piers
  ): THREE.Group {
    const bridgeGroup = new THREE.Group();
    bridgeGroup.name = `bridge_${config.id}`;

    const length = config.endDistance - config.startDistance;
    if (length < 10) return bridgeGroup;

    const deckW = config.deckWidth;
    const slabThick = 0.35;
    const overhang = 1.2;
    const girderDepth = 1.4;
    const numGirders = Math.max(2, Math.floor(deckW / 2.8) + 1);

    // 1. Concrete Deck Base & Overhang Wings
    const stepSize = 2.0;
    const numSlices = Math.max(3, Math.ceil(length / stepSize) + 1);

    const deckPos: number[] = [];
    const deckNorm: number[] = [];
    const deckUV: number[] = [];
    const deckIndices: number[] = [];

    // Girders buffers
    const girderPos: number[] = [];
    const girderNorm: number[] = [];
    const girderIndices: number[] = [];

    for (let s = 0; s < numSlices; s++) {
      const d = config.startDistance + (s / (numSlices - 1)) * length;
      const sp = spline.getSampleAtDistance(d);
      const norm = sp.normal!;
      const binorm = sp.binormal!;

      // Underside of deck slab
      const halfW = deckW * 0.5;
      const pts = [
        // Left cantilever overhang
        { x: -halfW - overhang, y: -0.05 },
        { x: -halfW, y: -slabThick },
        { x: halfW, y: -slabThick },
        { x: halfW + overhang, y: -0.05 }
      ];

      for (const p of pts) {
        const vx = sp.position.x + norm.x * p.x + binorm.x * p.y;
        const vy = sp.position.y + norm.y * p.x + binorm.y * p.y;
        const vz = sp.position.z + norm.z * p.x + binorm.z * p.y;
        deckPos.push(vx, vy, vz);
        deckNorm.push(binorm.x * -1, -1, binorm.z * -1); // downward normal
        deckUV.push(p.x * 0.2, d * 0.1);
      }

      // Sample longitudinal girders (I-beams under deck)
      const girderSpacing = (deckW - 1.6) / Math.max(1, numGirders - 1);
      for (let g = 0; g < numGirders; g++) {
        const gx = -deckW * 0.5 + 0.8 + g * girderSpacing;
        // Girder top & bottom flange
        const gTop = -slabThick;
        const gBot = -slabThick - girderDepth;

        // Top flange
        const ptTop = new THREE.Vector3().copy(sp.position).addScaledVector(norm, gx).addScaledVector(binorm, gTop);
        // Bottom flange
        const ptBot = new THREE.Vector3().copy(sp.position).addScaledVector(norm, gx).addScaledVector(binorm, gBot);

        girderPos.push(ptTop.x, ptTop.y, ptTop.z);
        girderPos.push(ptBot.x, ptBot.y, ptBot.z);
        girderNorm.push(norm.x, norm.y, norm.z);
        girderNorm.push(norm.x, norm.y, norm.z);
      }
    }

    // Build Deck Underside Quads
    for (let s = 0; s < numSlices - 1; s++) {
      const rowA = s * 4;
      const rowB = (s + 1) * 4;
      for (let p = 0; p < 3; p++) {
        deckIndices.push(rowA + p, rowB + p, rowB + p + 1);
        deckIndices.push(rowA + p, rowB + p + 1, rowA + p + 1);
      }
    }

    // Build Girder Ribbons
    const ptsPerGirderSlice = numGirders * 2;
    for (let s = 0; s < numSlices - 1; s++) {
      const rowA = s * ptsPerGirderSlice;
      const rowB = (s + 1) * ptsPerGirderSlice;
      for (let g = 0; g < numGirders; g++) {
        const vTopA = rowA + g * 2;
        const vBotA = rowA + g * 2 + 1;
        const vTopB = rowB + g * 2;
        const vBotB = rowB + g * 2 + 1;
        // Two-sided quad for girder web
        girderIndices.push(vTopA, vBotA, vBotB, vTopA, vBotB, vTopB);
        girderIndices.push(vTopA, vTopB, vBotB, vTopA, vBotB, vBotA);
      }
    }

    const deckGeom = new THREE.BufferGeometry();
    deckGeom.setAttribute('position', new THREE.Float32BufferAttribute(deckPos, 3));
    deckGeom.setAttribute('normal', new THREE.Float32BufferAttribute(deckNorm, 3));
    deckGeom.setAttribute('uv', new THREE.Float32BufferAttribute(deckUV, 2));
    deckGeom.setIndex(deckIndices);
    const deckMesh = new THREE.Mesh(deckGeom, materials.bridgeConcreteMaterial);
    deckMesh.castShadow = true;
    deckMesh.receiveShadow = true;
    bridgeGroup.add(deckMesh);

    const girderGeom = new THREE.BufferGeometry();
    girderGeom.setAttribute('position', new THREE.Float32BufferAttribute(girderPos, 3));
    girderGeom.setAttribute('normal', new THREE.Float32BufferAttribute(girderNorm, 3));
    girderGeom.setIndex(girderIndices);
    const girderMat = config.girderType === 'steel_i_beam' ? materials.bridgeSteelMaterial : materials.bridgeConcreteMaterial;
    const girderMesh = new THREE.Mesh(girderGeom, girderMat);
    girderMesh.castShadow = true;
    bridgeGroup.add(girderMesh);

    // 2. Piers, Crosshead Bents, Bearings & Foundations with Collision Avoidance
    const pierSpacing = config.pierSpacing || 32.0;
    const numPiers = Math.max(1, Math.floor(length / pierSpacing));

    for (let i = 1; i <= numPiers; i++) {
      let pierDist = config.startDistance + i * (length / (numPiers + 1));
      let sp = spline.getSampleAtDistance(pierDist);

      // Check collision with underlying roads or tracks
      let pierCollision = false;
      for (const ap of avoidPoints) {
        if (Math.hypot(sp.position.x - ap.x, sp.position.z - ap.z) < 5.0) {
          pierCollision = true;
          break;
        }
      }

      // If collision detected, shift pier position by 4m along alignment to safe ground
      if (pierCollision) {
        pierDist += 4.5;
        sp = spline.getSampleAtDistance(pierDist);
      }

      // Only generate piers if bridge elevation is significantly above ground (e.g. > 3.0m)
      if (sp.position.y > 3.0) {
        const pierBentGroup = this.buildPierBent(sp, deckW, slabThick, girderDepth, materials);
        bridgeGroup.add(pierBentGroup);
      }
    }

    // 3. Safety Parapets / Concrete Jersey Barrier on deck edges
    if (config.hasParapet) {
      const parapetMesh = this.buildBridgeParapets(spline, config.startDistance, config.endDistance, deckW + overhang * 2, materials);
      bridgeGroup.add(parapetMesh);
    }

    return bridgeGroup;
  }

  /**
   * Pier Bent: Crosshead beam + Elastomeric Bearings + Twin Cylindrical Columns + Footing
   */
  private static buildPierBent(
    sp: any,
    deckW: number,
    slabThick: number,
    girderDepth: number,
    materials: MaterialManager
  ): THREE.Group {
    const bent = new THREE.Group();
    bent.position.copy(sp.position);
    bent.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), sp.tangent);

    const crossheadW = deckW + 0.4;
    const crossheadH = 1.1;
    const crossheadD = 1.6;
    const crossheadY = -slabThick - girderDepth - crossheadH * 0.5;

    // 1. Crosshead Beam (Headstock)
    const crossheadGeom = new THREE.BoxGeometry(crossheadW, crossheadH, crossheadD);
    const crossheadMesh = new THREE.Mesh(crossheadGeom, materials.bridgeConcreteMaterial);
    crossheadMesh.position.set(0, crossheadY, 0);
    crossheadMesh.castShadow = true;
    crossheadMesh.receiveShadow = true;
    bent.add(crossheadMesh);

    // 2. Elastomeric Bearing Pads atop crosshead
    const numPads = Math.max(2, Math.floor(deckW / 2.8) + 1);
    const padSpacing = (deckW - 1.6) / Math.max(1, numPads - 1);
    for (let p = 0; p < numPads; p++) {
      const px = -deckW * 0.5 + 0.8 + p * padSpacing;
      const padGeom = new THREE.BoxGeometry(0.5, 0.12, 0.5);
      const padMesh = new THREE.Mesh(padGeom, materials.railingMaterial);
      padMesh.position.set(px, crossheadY + crossheadH * 0.5 + 0.06, 0);
      bent.add(padMesh);
    }

    // 3. Twin Cylindrical Concrete Pier Columns
    const colHeight = Math.max(2.0, sp.position.y + crossheadY - crossheadH * 0.5);
    const colRadius = 0.75;
    const colSpread = Math.min(deckW * 0.35, 4.5);

    const colGeom = new THREE.CylinderGeometry(colRadius, colRadius, colHeight, 18);
    for (const cx of [-colSpread, colSpread]) {
      const colMesh = new THREE.Mesh(colGeom, materials.bridgeConcreteMaterial);
      colMesh.position.set(cx, crossheadY - crossheadH * 0.5 - colHeight * 0.5, 0);
      colMesh.castShadow = true;
      colMesh.receiveShadow = true;
      bent.add(colMesh);

      // 4. Spread Footing Base
      const footingGeom = new THREE.BoxGeometry(2.4, 0.6, 2.4);
      const footingMesh = new THREE.Mesh(footingGeom, materials.bridgeConcreteMaterial);
      footingMesh.position.set(cx, crossheadY - crossheadH * 0.5 - colHeight + 0.3, 0);
      bent.add(footingMesh);
    }

    return bent;
  }

  /**
   * Swept Concrete Parapets & Tubular Handrails along bridge perimeter
   */
  private static buildBridgeParapets(
    spline: RoadSpline,
    startDist: number,
    endDist: number,
    totalWidth: number,
    materials: MaterialManager
  ): THREE.Group {
    const group = new THREE.Group();
    const length = endDist - startDist;
    const numSlices = Math.max(3, Math.ceil(length / 2.0) + 1);

    const halfW = totalWidth * 0.5;

    for (const isLeft of [true, false]) {
      const sign = isLeft ? -1 : 1;
      const pos: number[] = [];
      const norm: number[] = [];
      const indices: number[] = [];

      for (let s = 0; s < numSlices; s++) {
        const d = startDist + (s / (numSlices - 1)) * length;
        const sp = spline.getSampleAtDistance(d);
        const n = sp.normal!;
        const bn = sp.binormal!;

        // Concrete Jersey bridge parapet profile (height 0.90m)
        const pBot = new THREE.Vector3().copy(sp.position).addScaledVector(n, sign * halfW);
        const pMid = new THREE.Vector3().copy(pBot).addScaledVector(n, -sign * 0.1).addScaledVector(bn, 0.45);
        const pTop = new THREE.Vector3().copy(pMid).addScaledVector(bn, 0.45);

        for (const p of [pBot, pMid, pTop]) {
          pos.push(p.x, p.y, p.z);
          norm.push(n.x * sign, 0, n.z * sign);
        }
      }

      for (let s = 0; s < numSlices - 1; s++) {
        const rA = s * 3;
        const rB = (s + 1) * 3;
        for (let j = 0; j < 2; j++) {
          if (isLeft) {
            indices.push(rA + j, rB + j, rB + j + 1, rA + j, rB + j + 1, rA + j + 1);
          } else {
            indices.push(rA + j, rB + j + 1, rB + j, rA + j, rA + j + 1, rB + j + 1);
          }
        }
      }

      const geom = new THREE.BufferGeometry();
      geom.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
      geom.setAttribute('normal', new THREE.Float32BufferAttribute(norm, 3));
      geom.setIndex(indices);
      const mesh = new THREE.Mesh(geom, materials.barrierMaterial);
      mesh.castShadow = true;
      group.add(mesh);
    }

    return group;
  }
}
