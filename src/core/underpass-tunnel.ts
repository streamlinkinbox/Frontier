import * as THREE from 'three';
import { RoadSpline } from './spline';
import { MaterialManager } from './materials';

export interface TunnelConfig {
  id: string;
  name: string;
  startDistance: number;
  endDistance: number;
  width: number;
  height: number; // e.g. 6.2m
  portalType: 'horseshoe' | 'box';
  hasLighting: boolean;
  hasVentilationFans: boolean;
}

export class UnderpassTunnelBuilder {
  /**
   * Generates underpass retaining walls or underground tunnel portals and interior tube.
   */
  public static buildTunnel(
    spline: RoadSpline,
    config: TunnelConfig,
    materials: MaterialManager
  ): THREE.Group {
    const group = new THREE.Group();
    group.name = `tunnel_${config.id}`;

    const length = config.endDistance - config.startDistance;
    if (length < 8) return group;

    const halfW = config.width * 0.5;
    const tunnelH = config.height;
    const stepSize = 2.0;
    const numSlices = Math.max(3, Math.ceil(length / stepSize) + 1);

    // 1. Tunnel Interior Tube (Horseshoe Arch Vault)
    const vaultSegments = 12;
    const vaultPos: number[] = [];
    const vaultNorm: number[] = [];
    const vaultUV: number[] = [];
    const vaultIndices: number[] = [];

    for (let s = 0; s < numSlices; s++) {
      const d = config.startDistance + (s / (numSlices - 1)) * length;
      const sp = spline.getSampleAtDistance(d);
      const norm = sp.normal!;
      const binorm = sp.binormal!;

      for (let j = 0; j <= vaultSegments; j++) {
        // Arch profile from left road edge up over the vault to right road edge
        const angle = (j / vaultSegments) * Math.PI; // 0 to PI
        const archX = -halfW * Math.cos(angle);
        const archY = Math.sin(angle) * (tunnelH * 0.75) + 1.2;

        const vx = sp.position.x + norm.x * archX + binorm.x * archY;
        const vy = sp.position.y + norm.y * archX + binorm.y * archY;
        const vz = sp.position.z + norm.z * archX + binorm.z * archY;

        vaultPos.push(vx, vy, vz);
        // Normal points inward into tunnel
        vaultNorm.push(-norm.x * Math.cos(angle), -Math.sin(angle), -norm.z * Math.cos(angle));
        vaultUV.push(j / vaultSegments, d * 0.2);
      }
    }

    // Build Vault Quads
    const ptsPerSlice = vaultSegments + 1;
    for (let s = 0; s < numSlices - 1; s++) {
      const rowA = s * ptsPerSlice;
      const rowB = (s + 1) * ptsPerSlice;
      for (let j = 0; j < vaultSegments; j++) {
        vaultIndices.push(rowA + j, rowB + j, rowB + j + 1);
        vaultIndices.push(rowA + j, rowB + j + 1, rowA + j + 1);
      }
    }

    const vaultGeom = new THREE.BufferGeometry();
    vaultGeom.setAttribute('position', new THREE.Float32BufferAttribute(vaultPos, 3));
    vaultGeom.setAttribute('normal', new THREE.Float32BufferAttribute(vaultNorm, 3));
    vaultGeom.setAttribute('uv', new THREE.Float32BufferAttribute(vaultUV, 2));
    vaultGeom.setIndex(vaultIndices);
    const vaultMesh = new THREE.Mesh(vaultGeom, materials.tunnelWallMaterial);
    vaultMesh.castShadow = true;
    vaultMesh.receiveShadow = true;
    group.add(vaultMesh);

    // 2. Heavy Portal Structures at Start and End of Tunnel
    for (const dist of [config.startDistance, config.endDistance]) {
      const sp = spline.getSampleAtDistance(dist);
      const portalGroup = this.buildPortalStructure(sp, config, materials);
      group.add(portalGroup);
    }

    // 3. Overhead Continuous LED Strip Lighting
    if (config.hasLighting) {
      const lightStrip = this.buildCeilingLightStrip(spline, config.startDistance, config.endDistance, tunnelH);
      group.add(lightStrip);
    }

    // 4. Ceiling Jet Fans (Ventilation)
    if (config.hasVentilationFans) {
      const numFans = Math.max(1, Math.floor(length / 28));
      for (let f = 1; f <= numFans; f++) {
        const fanDist = config.startDistance + f * (length / (numFans + 1));
        const sp = spline.getSampleAtDistance(fanDist);
        const fanGroup = this.buildJetFan(sp, tunnelH);
        group.add(fanGroup);
      }
    }

    return group;
  }

  /**
   * Massive reinforced concrete portal arch with clearance sign
   */
  private static buildPortalStructure(
    sp: any,
    config: TunnelConfig,
    materials: MaterialManager
  ): THREE.Group {
    const portal = new THREE.Group();
    portal.position.copy(sp.position);
    portal.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), sp.tangent);

    const portalW = config.width + 4.0;
    const portalH = config.height + 2.5;
    const portalThick = 2.0;

    // Portal facade frame
    const facadeGeom = new THREE.BoxGeometry(portalW, portalH, portalThick);
    const facadeMesh = new THREE.Mesh(facadeGeom, materials.bridgeConcreteMaterial);
    facadeMesh.position.set(0, portalH * 0.5 - 0.5, 0);
    facadeMesh.castShadow = true;
    portal.add(facadeMesh);

    // Flared Retaining Wing Walls
    const wingGeom = new THREE.BoxGeometry(1.2, portalH, 5.0);
    for (const sign of [-1, 1]) {
      const wingMesh = new THREE.Mesh(wingGeom, materials.bridgeConcreteMaterial);
      wingMesh.position.set(sign * (portalW * 0.5 + 0.6), portalH * 0.5 - 0.5, -2.0);
      wingMesh.rotation.y = sign * 0.25;
      portal.add(wingMesh);
    }

    // Clearance Sign Board
    const signCanvas = document.createElement('canvas');
    signCanvas.width = 512;
    signCanvas.height = 128;
    const sctx = signCanvas.getContext('2d')!;
    sctx.fillStyle = '#006633'; // Highway green
    sctx.fillRect(0, 0, 512, 128);
    sctx.strokeStyle = '#ffffff';
    sctx.lineWidth = 6;
    sctx.strokeRect(6, 6, 500, 116);
    sctx.fillStyle = '#ffffff';
    sctx.font = 'bold 36px "DM Sans", sans-serif';
    sctx.textAlign = 'center';
    sctx.fillText('TUNNEL · CLEARANCE 5.5m', 256, 54);
    sctx.font = '24px "DM Sans", sans-serif';
    sctx.fillText('TURN ON HEADLIGHTS · 60 km/h', 256, 96);

    const signTex = new THREE.CanvasTexture(signCanvas);
    const signMat = new THREE.MeshStandardMaterial({ map: signTex, roughness: 0.4 });
    const signGeom = new THREE.PlaneGeometry(6.0, 1.5);
    const signMesh = new THREE.Mesh(signGeom, signMat);
    signMesh.position.set(0, config.height + 0.6, portalThick * 0.5 + 0.05);
    portal.add(signMesh);

    return portal;
  }

  /**
   * Continuous LED strip lighting running along the tunnel crown
   */
  private static buildCeilingLightStrip(
    spline: RoadSpline,
    startDist: number,
    endDist: number,
    tunnelH: number
  ): THREE.Group {
    const group = new THREE.Group();
    const len = endDist - startDist;
    const numSlices = Math.max(3, Math.ceil(len / 3.0) + 1);

    const pos: number[] = [];
    for (let s = 0; s < numSlices; s++) {
      const d = startDist + (s / (numSlices - 1)) * len;
      const sp = spline.getSampleAtDistance(d);
      const lightPos = new THREE.Vector3().copy(sp.position).addScaledVector(sp.binormal!, tunnelH - 0.2);
      pos.push(lightPos.x, lightPos.y, lightPos.z);
    }

    // Emissive line strip
    const geom = new THREE.BufferGeometry();
    geom.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    const lineMat = new THREE.LineBasicMaterial({ color: 0xfff0dd, linewidth: 2 });
    const line = new THREE.Line(geom, lineMat);
    group.add(line);

    // Place actual point lights every 18m
    const numLights = Math.max(2, Math.floor(len / 18.0));
    for (let i = 1; i <= numLights; i++) {
      const d = startDist + i * (len / (numLights + 1));
      const sp = spline.getSampleAtDistance(d);
      const p = new THREE.Vector3().copy(sp.position).addScaledVector(sp.binormal!, tunnelH - 0.4);

      const light = new THREE.PointLight(0xffecd0, 2.5, 24, 1.5);
      light.position.copy(p);
      group.add(light);
    }

    return group;
  }

  /**
   * Suspended Jet Fan Unit
   */
  private static buildJetFan(sp: any, tunnelH: number): THREE.Group {
    const fan = new THREE.Group();
    fan.position.copy(sp.position).addScaledVector(sp.binormal!, tunnelH - 0.75);
    fan.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), sp.tangent);

    // Fan cylindrical duct
    const ductGeom = new THREE.CylinderGeometry(0.45, 0.45, 2.2, 16);
    ductGeom.rotateX(Math.PI * 0.5);
    const ductMat = new THREE.MeshStandardMaterial({ color: 0x4a4d52, metalness: 0.8, roughness: 0.3 });
    const ductMesh = new THREE.Mesh(ductGeom, ductMat);
    fan.add(ductMesh);

    // Mounting brackets
    const bracketGeom = new THREE.BoxGeometry(0.08, 0.5, 0.8);
    const bracketMat = new THREE.MeshStandardMaterial({ color: 0x222, metalness: 0.9, roughness: 0.2 });
    const bMesh = new THREE.Mesh(bracketGeom, bracketMat);
    bMesh.position.set(0, 0.45, 0);
    fan.add(bMesh);

    return fan;
  }
}
