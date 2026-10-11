import * as THREE from 'three';
import { RoadSpline, SplinePoint } from './spline';
import { MaterialManager } from './materials';

export interface RoadConfig {
  id: string;
  name: string;
  lanes: number;
  laneWidth: number;
  sidewalkLeft: boolean;
  sidewalkRight: boolean;
  sidewalkWidth: number;
  curbHeight: number;
  curbWidth: number;
  crossfall: number; // 0.02 = 2%
  speedLimit: number;
  isHighway: boolean;
  isBridge: boolean;
  isUnderpass: boolean;
  bridgeHeight?: number;
}

export interface RoadMeshResult {
  group: THREE.Group;
  roadMesh: THREE.Mesh;
  curbMesh: THREE.Mesh;
  sidewalkMesh: THREE.Mesh;
  tactileMesh: THREE.Mesh;
  wireframeMesh: THREE.LineSegments;
  totalQuads: number;
  totalTriangles: number;
}

export class RoadTopologyBuilder {
  /**
   * Generates continuous quad-dominant meshes for a road segment between startDist and endDist.
   */
  public static buildRoadSegment(
    spline: RoadSpline,
    config: RoadConfig,
    materials: MaterialManager,
    startDist: number = 0,
    endDist: number = spline.totalLength,
    crossings: { distance: number; width: number }[] = []
  ): RoadMeshResult {
    const group = new THREE.Group();
    group.name = `road_${config.id}`;

    const effectiveLength = Math.max(1, endDist - startDist);
    // Dynamic slice spacing: ~1.0m intervals for high fidelity curvature
    const stepSize = 1.0;
    const numSlices = Math.max(3, Math.ceil(effectiveLength / stepSize) + 1);

    const halfRoadWidth = (config.lanes * config.laneWidth) / 2;
    const crownApex = halfRoadWidth * config.crossfall;

    // Cross-section relative offsets (lateral X in meters, relative Y elevation in meters)
    // Road surface is divided into N quad strips (at least 1 quad strip per lane)
    const numLanes = config.lanes;
    const roadPointsX: number[] = [];
    const roadPointsY: number[] = [];

    // Lane boundaries across road width
    for (let l = 0; l <= numLanes; l++) {
      const x = -halfRoadWidth + l * config.laneWidth;
      // Parabolic crown: apex at center (x=0), falling by crossfall to edges
      const distFromCenter = Math.abs(x);
      const y = crownApex - (distFromCenter / halfRoadWidth) * crownApex;
      roadPointsX.push(x);
      roadPointsY.push(y);
    }

    // Geometry buffers
    const roadPos: number[] = [];
    const roadNorm: number[] = [];
    const roadUV: number[] = [];
    const roadIndices: number[] = [];

    const curbPos: number[] = [];
    const curbNorm: number[] = [];
    const curbUV: number[] = [];
    const curbIndices: number[] = [];

    const swPos: number[] = [];
    const swNorm: number[] = [];
    const swUV: number[] = [];
    const swIndices: number[] = [];

    const tactilePos: number[] = [];
    const tactileNorm: number[] = [];
    const tactileUV: number[] = [];
    const tactileIndices: number[] = [];

    const wireframePositions: number[] = [];

    let totalQuads = 0;

    // Precalculate spline frames along alignment
    const slices: SplinePoint[] = [];
    for (let s = 0; s < numSlices; s++) {
      const d = startDist + (s / (numSlices - 1)) * effectiveLength;
      slices.push(spline.getSampleAtDistance(d));
    }

    // --- 1. BUILD ROAD CARRIAGEWAY (QUAD STRIPS) ---
    for (let s = 0; s < numSlices; s++) {
      const sp = slices[s];
      const norm = sp.normal!;
      const binorm = sp.binormal!;
      const bankAngle = sp.banking;

      // Rotation matrix for banking around tangent
      const cosB = Math.cos(bankAngle);
      const sinB = Math.sin(bankAngle);

      for (let p = 0; p < roadPointsX.length; p++) {
        const lx = roadPointsX[p];
        const ly = roadPointsY[p];

        // Apply banking to lateral vector
        const bankedX = lx * cosB - ly * sinB;
        const bankedY = lx * sinB + ly * cosB;

        const vx = sp.position.x + norm.x * bankedX + binorm.x * bankedY;
        const vy = sp.position.y + norm.y * bankedX + binorm.y * bankedY;
        const vz = sp.position.z + norm.z * bankedX + binorm.z * bankedY;

        roadPos.push(vx, vy, vz);
        roadNorm.push(binorm.x, binorm.y, binorm.z);

        // UV: u across road [0, 1], v along road distance in meters
        const u = p / numLanes;
        const v = sp.distance;
        roadUV.push(u, v);
      }
    }

    // Generate Quads for Road Carriageway
    const ptsPerSlice = roadPointsX.length;
    for (let s = 0; s < numSlices - 1; s++) {
      const rowA = s * ptsPerSlice;
      const rowB = (s + 1) * ptsPerSlice;

      for (let p = 0; p < numLanes; p++) {
        const v00 = rowA + p;
        const v01 = rowA + p + 1;
        const v10 = rowB + p;
        const v11 = rowB + p + 1;

        // Quad = 2 triangles
        roadIndices.push(v00, v10, v11);
        roadIndices.push(v00, v11, v01);
        totalQuads++;

        // Add to wireframe inspection lines
        const getV = (idx: number) => new THREE.Vector3(roadPos[idx * 3], roadPos[idx * 3 + 1], roadPos[idx * 3 + 2]);
        const p00 = getV(v00), p01 = getV(v01), p10 = getV(v10), p11 = getV(v11);
        wireframePositions.push(
          p00.x, p00.y, p00.z, p01.x, p01.y, p01.z,
          p01.x, p01.y, p01.z, p11.x, p11.y, p11.z,
          p11.x, p11.y, p11.z, p10.x, p10.y, p10.z,
          p10.x, p10.y, p10.z, p00.x, p00.y, p00.z
        );
      }
    }

    // --- 2. BUILD CURBS, GUTTERS & SIDEWALKS ---
    // Gutter width: 0.30m, sloped down 15mm
    // Curb width: config.curbWidth (0.20m)
    // Curb upstand: config.curbHeight (0.15m), with 15mm chamfer top
    const gutterWidth = 0.30;
    const gutterDrop = 0.015;
    const curbW = config.curbWidth;
    const curbH = config.curbHeight;
    const chamfer = 0.02;

    const buildEdgeFeatures = (isLeft: boolean) => {
      const sign = isLeft ? -1 : 1;
      const hasSidewalk = isLeft ? config.sidewalkLeft : config.sidewalkRight;
      const swW = config.sidewalkWidth;

      const curbSliceStartIdx = curbPos.length / 3;
      const swSliceStartIdx = swPos.length / 3;

      for (let s = 0; s < numSlices; s++) {
        const sp = slices[s];
        const norm = sp.normal!;
        const binorm = sp.binormal!;

        // Check if slice is within a pedestrian curb-cut ramp
        let curbUpstandFactor = 1.0;
        for (const cr of crossings) {
          const distToCrossing = Math.abs(sp.distance - cr.distance);
          if (distToCrossing < cr.width * 0.5) {
            curbUpstandFactor = 0.0; // flush dropped curb ramp!
            break;
          } else if (distToCrossing < cr.width * 0.5 + 1.2) {
            // 1:10 ramp slope
            curbUpstandFactor = (distToCrossing - cr.width * 0.5) / 1.2;
            break;
          }
        }

        const effectiveCurbH = curbH * curbUpstandFactor;

        // Base road edge attachment
        const baseEdgeX = sign * halfRoadWidth;
        const baseEdgeY = 0; // at road edge

        // 1. Gutter channel apron
        const gutterX = baseEdgeX + sign * gutterWidth;
        const gutterY = baseEdgeY - gutterDrop;

        // 2. Curb Face Bottom (at gutter invert)
        const curbBotX = gutterX;
        const curbBotY = gutterY;

        // 3. Curb Top Front (at chamfer start)
        const curbTopFrontX = curbBotX;
        const curbTopFrontY = gutterY + effectiveCurbH - chamfer;

        // 4. Curb Top Back (behind chamfer)
        const curbTopBackX = curbBotX + sign * chamfer;
        const curbTopBackY = gutterY + effectiveCurbH;

        // 5. Back of Curb (meeting sidewalk)
        const curbBackX = curbBotX + sign * curbW;
        const curbBackY = curbTopBackY;

        // Add Curb cross section vertices (4 vertices per slice = 3 quad strips: Gutter, Curb Face, Curb Chamfer Top)
        const curbPts = [
          { x: baseEdgeX, y: baseEdgeY, nx: 0, ny: 1 },
          { x: gutterX, y: gutterY, nx: 0, ny: 1 },
          { x: curbBotX, y: curbBotY, nx: -sign, ny: 0 },
          { x: curbTopFrontX, y: curbTopFrontY, nx: -sign, ny: 0.5 },
          { x: curbTopBackX, y: curbTopBackY, nx: 0, ny: 1 },
          { x: curbBackX, y: curbBackY, nx: 0, ny: 1 }
        ];

        for (const cp of curbPts) {
          const vx = sp.position.x + norm.x * cp.x + binorm.x * cp.y;
          const vy = sp.position.y + norm.y * cp.x + binorm.y * cp.y;
          const vz = sp.position.z + norm.z * cp.x + binorm.z * cp.y;
          curbPos.push(vx, vy, vz);
          curbNorm.push(norm.x * cp.nx, binorm.y * cp.ny, norm.z * cp.nx);
          curbUV.push(cp.x, sp.distance);
        }

        // Sidewalk (if enabled)
        if (hasSidewalk) {
          const swInnerX = curbBackX;
          const swInnerY = curbBackY;
          // Sidewalk has 1.5% crossfall towards curb for drainage
          const swOuterX = swInnerX + sign * swW;
          const swOuterY = swInnerY + 0.015 * swW;

          // Add tactile blister paving strip if near crossing
          const isAtCrossing = curbUpstandFactor < 0.2;

          // Sidewalk vertices: Inner edge, middle, Outer edge (2 quad strips across width)
          const swPts = [
            { x: swInnerX, y: swInnerY },
            { x: swInnerX + sign * (swW * 0.5), y: swInnerY + 0.0075 * swW },
            { x: swOuterX, y: swOuterY }
          ];

          for (const sw of swPts) {
            const vx = sp.position.x + norm.x * sw.x + binorm.x * sw.y;
            const vy = sp.position.y + norm.y * sw.x + binorm.y * sw.y;
            const vz = sp.position.z + norm.z * sw.x + binorm.z * sw.y;
            swPos.push(vx, vy, vz);
            swNorm.push(binorm.x, binorm.y, binorm.z);
            swUV.push(Math.abs(sw.x - swInnerX) / swW, sp.distance);
          }

          if (isAtCrossing) {
            // Tactile pad: 0.60m x crossing width strip behind curb cut
            const tacInnerX = swInnerX;
            const tacOuterX = swInnerX + sign * 0.60;
            const tacY = swInnerY + 0.005;
            for (const tx of [tacInnerX, tacOuterX]) {
              const vx = sp.position.x + norm.x * tx + binorm.x * tacY;
              const vy = sp.position.y + norm.y * tx + binorm.y * tacY;
              const vz = sp.position.z + norm.z * tx + binorm.z * tacY;
              tactilePos.push(vx, vy, vz);
              tactileNorm.push(binorm.x, binorm.y, binorm.z);
              tactileUV.push(tx, sp.distance);
            }
          }
        }
      }

      // Generate Curb Quads
      const ptsPerCurbSlice = 6;
      for (let s = 0; s < numSlices - 1; s++) {
        const rowA = curbSliceStartIdx + s * ptsPerCurbSlice;
        const rowB = curbSliceStartIdx + (s + 1) * ptsPerCurbSlice;

        for (let p = 0; p < ptsPerCurbSlice - 1; p++) {
          const v00 = rowA + p;
          const v01 = rowA + p + 1;
          const v10 = rowB + p;
          const v11 = rowB + p + 1;

          if (isLeft) {
            curbIndices.push(v00, v10, v11);
            curbIndices.push(v00, v11, v01);
          } else {
            curbIndices.push(v00, v11, v10);
            curbIndices.push(v00, v01, v11);
          }
          totalQuads++;
        }
      }

      // Generate Sidewalk Quads
      if (hasSidewalk) {
        const ptsPerSwSlice = 3;
        for (let s = 0; s < numSlices - 1; s++) {
          const rowA = swSliceStartIdx + s * ptsPerSwSlice;
          const rowB = swSliceStartIdx + (s + 1) * ptsPerSwSlice;

          for (let p = 0; p < ptsPerSwSlice - 1; p++) {
            const v00 = rowA + p;
            const v01 = rowA + p + 1;
            const v10 = rowB + p;
            const v11 = rowB + p + 1;

            if (isLeft) {
              swIndices.push(v00, v10, v11);
              swIndices.push(v00, v11, v01);
            } else {
              swIndices.push(v00, v11, v10);
              swIndices.push(v00, v01, v11);
            }
            totalQuads++;
          }
        }
      }
    };

    buildEdgeFeatures(true);  // Left Edge
    buildEdgeFeatures(false); // Right Edge

    // Assemble Three.js Meshes
    // 1. Road Carriageway
    const roadGeom = new THREE.BufferGeometry();
    roadGeom.setAttribute('position', new THREE.Float32BufferAttribute(roadPos, 3));
    roadGeom.setAttribute('normal', new THREE.Float32BufferAttribute(roadNorm, 3));
    roadGeom.setAttribute('uv', new THREE.Float32BufferAttribute(roadUV, 2));
    roadGeom.setIndex(roadIndices);
    const roadMesh = new THREE.Mesh(roadGeom, materials.roadMaterial);
    roadMesh.castShadow = true;
    roadMesh.receiveShadow = true;
    group.add(roadMesh);

    // 2. Curbs
    const curbGeom = new THREE.BufferGeometry();
    curbGeom.setAttribute('position', new THREE.Float32BufferAttribute(curbPos, 3));
    curbGeom.setAttribute('normal', new THREE.Float32BufferAttribute(curbNorm, 3));
    curbGeom.setAttribute('uv', new THREE.Float32BufferAttribute(curbUV, 2));
    curbGeom.setIndex(curbIndices);
    const curbMesh = new THREE.Mesh(curbGeom, materials.curbMaterial);
    curbMesh.castShadow = true;
    curbMesh.receiveShadow = true;
    group.add(curbMesh);

    // 3. Sidewalks
    const swGeom = new THREE.BufferGeometry();
    swGeom.setAttribute('position', new THREE.Float32BufferAttribute(swPos, 3));
    swGeom.setAttribute('normal', new THREE.Float32BufferAttribute(swNorm, 3));
    swGeom.setAttribute('uv', new THREE.Float32BufferAttribute(swUV, 2));
    swGeom.setIndex(swIndices);
    const sidewalkMesh = new THREE.Mesh(swGeom, materials.sidewalkMaterial);
    sidewalkMesh.castShadow = true;
    sidewalkMesh.receiveShadow = true;
    group.add(sidewalkMesh);

    // 4. Tactile warning pads
    const tactileGeom = new THREE.BufferGeometry();
    if (tactilePos.length > 0) {
      tactileGeom.setAttribute('position', new THREE.Float32BufferAttribute(tactilePos, 3));
      tactileGeom.setAttribute('normal', new THREE.Float32BufferAttribute(tactileNorm, 3));
      tactileGeom.setAttribute('uv', new THREE.Float32BufferAttribute(tactileUV, 2));
      // Connect consecutive tactile pairs
      const tacIndices: number[] = [];
      const numTacPairs = tactilePos.length / 6;
      for (let t = 0; t < numTacPairs - 1; t++) {
        const v00 = t * 2;
        const v01 = t * 2 + 1;
        const v10 = (t + 1) * 2;
        const v11 = (t + 1) * 2 + 1;
        tacIndices.push(v00, v10, v11, v00, v11, v01);
      }
      tactileGeom.setIndex(tacIndices);
    }
    const tactileMesh = new THREE.Mesh(tactileGeom, materials.tactileMaterial);
    group.add(tactileMesh);

    // 5. Wireframe mesh for topology inspection
    const wireGeom = new THREE.BufferGeometry();
    wireGeom.setAttribute('position', new THREE.Float32BufferAttribute(wireframePositions, 3));
    const wireframeMesh = new THREE.LineSegments(wireGeom, materials.wireframeMaterial);
    wireframeMesh.visible = false;
    group.add(wireframeMesh);

    const totalTriangles = (roadIndices.length + curbIndices.length + swIndices.length) / 3;

    return {
      group,
      roadMesh,
      curbMesh,
      sidewalkMesh,
      tactileMesh,
      wireframeMesh,
      totalQuads,
      totalTriangles
    };
  }
}
