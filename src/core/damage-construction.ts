import * as THREE from 'three';
import { RoadSpline } from './spline';
import { MaterialManager } from './materials';

export type SpeedBumpType = 'asphalt_hump' | 'sinusoidal_table' | 'rubber_cushion';

export class DamageConstructionBuilder {
  /**
   * Generates realistic speed bumps conforming to the road crown and alignment.
   */
  public static buildSpeedBump(
    spline: RoadSpline,
    distance: number,
    type: SpeedBumpType,
    roadWidth: number,
    materials: MaterialManager
  ): THREE.Group {
    const bumpGroup = new THREE.Group();
    const sp = spline.getSampleAtDistance(distance);
    bumpGroup.position.copy(sp.position);
    bumpGroup.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), sp.tangent);

    const halfW = roadWidth * 0.5 - 0.4;

    if (type === 'asphalt_hump') {
      // Parabolic Asphalt Speed Hump (Chord 3.7m, Height 85mm)
      const chord = 3.7;
      const height = 0.085;
      const segmentsZ = 12;
      const segmentsX = 20;

      const pos: number[] = [];
      const norm: number[] = [];
      const uv: number[] = [];
      const indices: number[] = [];

      for (let z = 0; z <= segmentsZ; z++) {
        const u = z / segmentsZ;
        const localZ = (u - 0.5) * chord;
        // Parabolic rise
        const y = (1.0 - Math.pow(u * 2 - 1, 2)) * height;

        for (let x = 0; x <= segmentsX; x++) {
          const v = x / segmentsX;
          const localX = (v - 0.5) * halfW * 2;
          pos.push(localX, y + 0.005, localZ);
          norm.push(0, 1, 0);
          uv.push(v, u);
        }
      }

      for (let z = 0; z < segmentsZ; z++) {
        for (let x = 0; x < segmentsX; x++) {
          const v00 = z * (segmentsX + 1) + x;
          const v01 = z * (segmentsX + 1) + x + 1;
          const v10 = (z + 1) * (segmentsX + 1) + x;
          const v11 = (z + 1) * (segmentsX + 1) + x + 1;
          indices.push(v00, v10, v11, v00, v11, v01);
        }
      }

      const geom = new THREE.BufferGeometry();
      geom.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
      geom.setAttribute('normal', new THREE.Float32BufferAttribute(norm, 3));
      geom.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
      geom.setIndex(indices);

      // Create hump texture with painted white chevron arrows
      const canvas = document.createElement('canvas');
      canvas.width = 512;
      canvas.height = 256;
      const ctx = canvas.getContext('2d')!;
      ctx.fillStyle = '#222326';
      ctx.fillRect(0, 0, 512, 256);

      // White chevron triangles
      ctx.fillStyle = '#ffffff';
      for (let c = 0; c < 4; c++) {
        const cx = 64 + c * 128;
        ctx.beginPath();
        ctx.moveTo(cx, 40);
        ctx.lineTo(cx - 35, 210);
        ctx.lineTo(cx + 35, 210);
        ctx.closePath();
        ctx.fill();
      }

      const tex = new THREE.CanvasTexture(canvas);
      const humpMat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.75, metalness: 0.05 });
      const humpMesh = new THREE.Mesh(geom, humpMat);
      humpMesh.castShadow = true;
      bumpGroup.add(humpMesh);

    } else if (type === 'sinusoidal_table') {
      // Raised Speed Table / Flat-top Platform (Ramp 1.5m, Flat top 3.0m, Ramp 1.5m)
      const tableW = halfW * 2;
      const tableH = 0.10;
      const rampL = 1.2;
      const flatL = 2.8;

      const geom = new THREE.BoxGeometry(tableW, tableH, flatL);
      const tableMat = materials.sidewalkMaterial; // Brick/paver flat top
      const tableMesh = new THREE.Mesh(geom, tableMat);
      tableMesh.position.set(0, tableH * 0.5, 0);
      tableMesh.castShadow = true;
      bumpGroup.add(tableMesh);

      // Sloped approach ramps
      for (const sign of [-1, 1]) {
        const rampGeom = new THREE.CylinderGeometry(0.01, tableW * 0.5, rampL, 4);
        rampGeom.rotateX(Math.PI * 0.5);
        const rampMesh = new THREE.Mesh(new THREE.BoxGeometry(tableW, tableH, rampL), materials.roadMaterial);
        rampMesh.position.set(0, tableH * 0.35, sign * (flatL * 0.5 + rampL * 0.5));
        rampMesh.rotation.x = sign * 0.08;
        bumpGroup.add(rampMesh);
      }

    } else {
      // Modular Rubber Speed Cushions (two cushions for car wheels while letting transit bus pass)
      const cushionW = 1.8;
      const cushionL = 2.0;
      const cushionH = 0.075;

      const cushionGeom = new THREE.BoxGeometry(cushionW, cushionH, cushionL);
      // Black vulcanized rubber with yellow reflective arrows
      const canvas = document.createElement('canvas');
      canvas.width = 256;
      canvas.height = 256;
      const ctx = canvas.getContext('2d')!;
      ctx.fillStyle = '#1a1b1d';
      ctx.fillRect(0, 0, 256, 256);
      ctx.fillStyle = '#ffcc00';
      ctx.fillRect(0, 0, 24, 256);
      ctx.fillRect(232, 0, 24, 256);
      // White arrow
      ctx.fillStyle = '#ffffff';
      ctx.beginPath();
      ctx.moveTo(128, 40);
      ctx.lineTo(80, 210);
      ctx.lineTo(176, 210);
      ctx.closePath();
      ctx.fill();

      const tex = new THREE.CanvasTexture(canvas);
      const cMat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.6, metalness: 0.1 });

      for (const cx of [-halfW * 0.5, halfW * 0.5]) {
        const cushion = new THREE.Mesh(cushionGeom, cMat);
        cushion.position.set(cx, cushionH * 0.5 + 0.005, 0);
        cushion.castShadow = true;
        bumpGroup.add(cushion);
      }
    }

    return bumpGroup;
  }

  /**
   * Pothole with depth, jagged asphalt rim, and exposed crushed stone base.
   */
  public static buildPothole(position: THREE.Vector3, radius: number = 0.65): THREE.Group {
    const group = new THREE.Group();
    group.position.copy(position);

    // 1. Recessed Base Floor (depth 55mm)
    const floorGeom = new THREE.CircleGeometry(radius * 0.85, 16);
    floorGeom.rotateX(-Math.PI * 0.5);
    const floorMat = new THREE.MeshStandardMaterial({
      color: 0x48423d, // rough exposed gravel aggregate
      roughness: 0.95,
      metalness: 0.05
    });
    const floorMesh = new THREE.Mesh(floorGeom, floorMat);
    floorMesh.position.y = -0.055;
    group.add(floorMesh);

    // 2. Jagged Broken Asphalt Rim
    const rimSegments = 24;
    const rimPos: number[] = [];
    const rimNorm: number[] = [];
    const rimIndices: number[] = [];

    for (let i = 0; i <= rimSegments; i++) {
      const angle = (i / rimSegments) * Math.PI * 2;
      // Perlin-like radius irregularity for natural jagged break
      const jaggedR = radius * (0.85 + Math.sin(angle * 5) * 0.12 + Math.cos(angle * 7) * 0.06);

      // Outer rim at road level
      rimPos.push(Math.cos(angle) * (jaggedR + 0.15), 0.005, Math.sin(angle) * (jaggedR + 0.15));
      rimNorm.push(0, 1, 0);

      // Inner broken edge down to cavity
      rimPos.push(Math.cos(angle) * jaggedR, -0.055, Math.sin(angle) * jaggedR);
      rimNorm.push(-Math.cos(angle), 0.3, -Math.sin(angle));
    }

    for (let i = 0; i < rimSegments; i++) {
      const v00 = i * 2;
      const v01 = i * 2 + 1;
      const v10 = (i + 1) * 2;
      const v11 = (i + 1) * 2 + 1;
      rimIndices.push(v00, v10, v11, v00, v11, v01);
    }

    const rimGeom = new THREE.BufferGeometry();
    rimGeom.setAttribute('position', new THREE.Float32BufferAttribute(rimPos, 3));
    rimGeom.setAttribute('normal', new THREE.Float32BufferAttribute(rimNorm, 3));
    rimGeom.setIndex(rimIndices);
    const rimMat = new THREE.MeshStandardMaterial({ color: 0x1f2022, roughness: 0.88 });
    const rimMesh = new THREE.Mesh(rimGeom, rimMat);
    group.add(rimMesh);

    return group;
  }

  /**
   * Construction Zone / Road Rebuilding Area:
   * Milled asphalt surface, traffic cones, water-filled jersey barricades, arrow board trailer.
   */
  public static buildConstructionZone(
    spline: RoadSpline,
    startDist: number,
    endDist: number,
    roadWidth: number,
    materials: MaterialManager
  ): THREE.Group {
    const zone = new THREE.Group();
    zone.name = 'construction_zone';

    const length = endDist - startDist;
    const halfW = roadWidth * 0.5;

    // 1. Milled Asphalt Surface (grooved scraped road lane)
    const milledGeom = new THREE.PlaneGeometry(halfW, length, 8, 16);
    milledGeom.rotateX(-Math.PI * 0.5);

    // Procedural grooved milling texture
    const canvas = document.createElement('canvas');
    canvas.width = 512;
    canvas.height = 512;
    const ctx = canvas.getContext('2d')!;
    ctx.fillStyle = '#404347';
    ctx.fillRect(0, 0, 512, 512);
    ctx.strokeStyle = '#282a2d';
    ctx.lineWidth = 3;
    for (let x = 0; x < 512; x += 6) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, 512);
      ctx.stroke();
    }
    const milledTex = new THREE.CanvasTexture(canvas);
    milledTex.wrapS = THREE.RepeatWrapping;
    milledTex.wrapT = THREE.RepeatWrapping;
    milledTex.repeat.set(1, length * 0.5);

    const milledMat = new THREE.MeshStandardMaterial({ map: milledTex, roughness: 0.95 });
    const milledMesh = new THREE.Mesh(milledGeom, milledMat);
    const midDist = (startDist + endDist) * 0.5;
    const midSp = spline.getSampleAtDistance(midDist);
    milledMesh.position.copy(midSp.position).addScaledVector(midSp.normal!, halfW * 0.5);
    milledMesh.position.y += 0.008;
    milledMesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), midSp.tangent);
    zone.add(milledMesh);

    // 2. High-Visibility Fluorescent Orange Traffic Cones with twin reflective collars
    const numCones = Math.max(4, Math.floor(length / 4.0));
    for (let c = 0; c < numCones; c++) {
      const d = startDist + c * (length / (numCones - 1));
      const sp = spline.getSampleAtDistance(d);
      // Place cone along lane separation line
      const conePos = new THREE.Vector3().copy(sp.position).addScaledVector(sp.normal!, 0);
      const cone = this.buildTrafficCone();
      cone.position.copy(conePos);
      zone.add(cone);
    }

    // 3. Water-filled plastic Jersey Barricades (alternating orange and white)
    const numBarricades = Math.max(2, Math.floor(length / 10.0));
    for (let b = 0; b < numBarricades; b++) {
      const d = startDist + 5.0 + b * 10.0;
      const sp = spline.getSampleAtDistance(d);
      const barPos = new THREE.Vector3().copy(sp.position).addScaledVector(sp.normal!, halfW - 0.6);
      const bar = this.buildJerseyBarricade(b % 2 === 0 ? 0xff6600 : 0xf0f0f0);
      bar.position.copy(barPos);
      bar.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), sp.tangent);
      zone.add(bar);
    }

    // 4. Solar LED Flashing Arrow Board Trailer at beginning of work zone
    const entrySp = spline.getSampleAtDistance(startDist - 8.0);
    const arrowTrailer = this.buildArrowBoardTrailer();
    arrowTrailer.position.copy(entrySp.position).addScaledVector(entrySp.normal!, halfW * 0.5);
    arrowTrailer.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), entrySp.tangent);
    zone.add(arrowTrailer);

    return zone;
  }

  /**
   * Traffic cone with rubber base and 2 reflective collars
   */
  public static buildTrafficCone(): THREE.Group {
    const cone = new THREE.Group();

    // Black square rubber base
    const baseGeom = new THREE.BoxGeometry(0.38, 0.04, 0.38);
    const baseMat = new THREE.MeshStandardMaterial({ color: 0x111111, roughness: 0.9 });
    const base = new THREE.Mesh(baseGeom, baseMat);
    base.position.y = 0.02;
    cone.add(base);

    // Orange cone body
    const bodyGeom = new THREE.ConeGeometry(0.14, 0.70, 16);
    const bodyMat = new THREE.MeshStandardMaterial({ color: 0xff5500, roughness: 0.4 });
    const body = new THREE.Mesh(bodyGeom, bodyMat);
    body.position.y = 0.37;
    cone.add(body);

    // Reflective white collars
    const collarMat = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.2 });
    for (const cy of [0.42, 0.54]) {
      const collarGeom = new THREE.CylinderGeometry(0.09, 0.10, 0.08, 16);
      const collar = new THREE.Mesh(collarGeom, collarMat);
      collar.position.y = cy;
      cone.add(collar);
    }

    return cone;
  }

  /**
   * Plastic water-filled Jersey barrier
   */
  public static buildJerseyBarricade(colorHex: number): THREE.Group {
    const bar = new THREE.Group();
    const geom = new THREE.BoxGeometry(0.55, 0.85, 2.0);
    const mat = new THREE.MeshStandardMaterial({ color: colorHex, roughness: 0.4 });
    const mesh = new THREE.Mesh(geom, mat);
    mesh.position.y = 0.425;
    mesh.castShadow = true;
    bar.add(mesh);
    return bar;
  }

  /**
   * Solar LED Arrow Board Trailer (Animated flashing yellow directional chevrons)
   */
  public static buildArrowBoardTrailer(): THREE.Group {
    const trailer = new THREE.Group();

    // Steel trailer chassis & wheels
    const chassisGeom = new THREE.BoxGeometry(1.8, 0.35, 2.8);
    const chassisMat = new THREE.MeshStandardMaterial({ color: 0x2e3436, metalness: 0.8 });
    const chassis = new THREE.Mesh(chassisGeom, chassisMat);
    chassis.position.y = 0.45;
    trailer.add(chassis);

    // Wheels
    const wheelGeom = new THREE.CylinderGeometry(0.35, 0.35, 0.22, 16);
    wheelGeom.rotateZ(Math.PI * 0.5);
    const wheelMat = new THREE.MeshStandardMaterial({ color: 0x111111, roughness: 0.9 });
    for (const wx of [-1.0, 1.0]) {
      const w = new THREE.Mesh(wheelGeom, wheelMat);
      w.position.set(wx, 0.35, 0);
      trailer.add(w);
    }

    // Mast
    const mastGeom = new THREE.CylinderGeometry(0.08, 0.08, 2.2, 12);
    const mast = new THREE.Mesh(mastGeom, chassisMat);
    mast.position.set(0, 1.6, 0);
    trailer.add(mast);

    // Yellow Display Board with blinking arrow
    const boardGeom = new THREE.BoxGeometry(2.4, 1.4, 0.15);
    const boardMat = new THREE.MeshStandardMaterial({ color: 0x0a0a0a, roughness: 0.6 });
    const board = new THREE.Mesh(boardGeom, boardMat);
    board.position.set(0, 2.6, 0);

    // Array of LED amber beacons
    const ledMat = new THREE.MeshBasicMaterial({ color: 0xffaa00 });
    const ledGeom = new THREE.CircleGeometry(0.06, 10);
    // Draw left-pointing chevron
    const arrowCoords = [
      [-0.8, 0], [-0.5, 0.3], [-0.5, -0.3], [-0.2, 0.45], [-0.2, -0.45],
      [0.2, 0], [0.5, 0.3], [0.5, -0.3], [0.8, 0.45], [0.8, -0.45]
    ];
    for (const [lx, ly] of arrowCoords) {
      const led = new THREE.Mesh(ledGeom, ledMat);
      led.position.set(lx, ly, 0.09);
      board.add(led);
    }

    trailer.add(board);
    return trailer;
  }
}
