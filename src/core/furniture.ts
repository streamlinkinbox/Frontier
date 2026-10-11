import * as THREE from 'three';
import { RoadSpline } from './spline';
import { MaterialManager } from './materials';

export class FurnitureBuilder {
  /**
   * Streetlights: Modern Cobra-head, Urban Twin-arm, or Historic Victorian lantern.
   */
  public static buildStreetlight(
    style: 'cobra_head' | 'twin_arm' | 'victorian',
    materials: MaterialManager,
    isNight: boolean = false
  ): THREE.Group {
    const group = new THREE.Group();
    group.name = `streetlight_${style}`;

    if (style === 'cobra_head') {
      // 10m Galvanized Steel Highway Luminaire
      const poleGeom = new THREE.CylinderGeometry(0.12, 0.22, 9.5, 12);
      const poleMat = new THREE.MeshStandardMaterial({ color: 0x9ca3af, metalness: 0.8, roughness: 0.3 });
      const pole = new THREE.Mesh(poleGeom, poleMat);
      pole.position.y = 4.75;
      pole.castShadow = true;
      group.add(pole);

      // Curved mast arm outreach (2.5m)
      const armGeom = new THREE.CylinderGeometry(0.08, 0.10, 2.8, 10);
      armGeom.rotateZ(Math.PI * 0.35);
      const arm = new THREE.Mesh(armGeom, poleMat);
      arm.position.set(1.1, 9.8, 0);
      arm.castShadow = true;
      group.add(arm);

      // Optical luminaire head
      const headGeom = new THREE.BoxGeometry(0.9, 0.18, 0.45);
      const head = new THREE.Mesh(headGeom, poleMat);
      head.position.set(2.4, 10.4, 0);
      head.castShadow = true;
      group.add(head);

      // Emissive LED panel lens
      const lensGeom = new THREE.PlaneGeometry(0.7, 0.35);
      lensGeom.rotateX(Math.PI * 0.5);
      const lensMat = new THREE.MeshBasicMaterial({ color: isNight ? 0xfff6e6 : 0xd0d5dd });
      const lens = new THREE.Mesh(lensGeom, lensMat);
      lens.position.set(2.4, 10.3, 0);
      group.add(lens);

      if (isNight) {
        const spot = new THREE.SpotLight(0xfff3db, 18, 30, Math.PI * 0.28, 0.45, 1.2);
        spot.position.set(2.4, 10.2, 0);
        spot.target.position.set(2.4, 0, 0);
        spot.castShadow = true;
        group.add(spot);
        group.add(spot.target);
      }

    } else if (style === 'twin_arm') {
      // 8m Boulevard Twin-Arm Pole
      const poleGeom = new THREE.CylinderGeometry(0.14, 0.24, 8.0, 12);
      const poleMat = new THREE.MeshStandardMaterial({ color: 0x374151, metalness: 0.7, roughness: 0.3 });
      const pole = new THREE.Mesh(poleGeom, poleMat);
      pole.position.y = 4.0;
      pole.castShadow = true;
      group.add(pole);

      // Dual curved outreach arms
      for (const sign of [-1, 1]) {
        const armGeom = new THREE.CylinderGeometry(0.06, 0.08, 2.2, 8);
        armGeom.rotateZ(sign * Math.PI * 0.32);
        const arm = new THREE.Mesh(armGeom, poleMat);
        arm.position.set(sign * 0.9, 8.4, 0);
        group.add(arm);

        const lumGeom = new THREE.CylinderGeometry(0.24, 0.32, 0.35, 12);
        const lum = new THREE.Mesh(lumGeom, poleMat);
        lum.position.set(sign * 1.8, 8.7, 0);
        group.add(lum);

        const lensGeom = new THREE.CircleGeometry(0.22, 12);
        lensGeom.rotateX(Math.PI * 0.5);
        const lensMat = new THREE.MeshBasicMaterial({ color: isNight ? 0xffedd5 : 0xaaaaaa });
        const lens = new THREE.Mesh(lensGeom, lensMat);
        lens.position.set(sign * 1.8, 8.5, 0);
        group.add(lens);

        if (isNight) {
          const spot = new THREE.SpotLight(0xffedd5, 14, 25, Math.PI * 0.25, 0.4, 1.2);
          spot.position.set(sign * 1.8, 8.4, 0);
          spot.target.position.set(sign * 1.8, 0, 0);
          group.add(spot);
          group.add(spot.target);
        }
      }

    } else {
      // Historic Victorian Fluted Cast-Iron Lantern (4.5m)
      const baseGeom = new THREE.CylinderGeometry(0.28, 0.38, 0.9, 8);
      const ironMat = new THREE.MeshStandardMaterial({ color: 0x1f2421, metalness: 0.85, roughness: 0.3 });
      const base = new THREE.Mesh(baseGeom, ironMat);
      base.position.y = 0.45;
      group.add(base);

      const shaftGeom = new THREE.CylinderGeometry(0.10, 0.14, 3.4, 12);
      const shaft = new THREE.Mesh(shaftGeom, ironMat);
      shaft.position.y = 2.6;
      group.add(shaft);

      // Glass vintage lantern cage
      const cageGeom = new THREE.BoxGeometry(0.55, 0.75, 0.55);
      const cageMat = new THREE.MeshStandardMaterial({ color: 0x111111, metalness: 0.9 });
      const cage = new THREE.Mesh(cageGeom, cageMat);
      cage.position.y = 4.65;
      group.add(cage);

      // Warm glowing filament
      const glassMat = new THREE.MeshBasicMaterial({ color: isNight ? 0xffaa44 : 0xddddbb });
      const bulbGeom = new THREE.SphereGeometry(0.18, 12, 12);
      const bulb = new THREE.Mesh(bulbGeom, glassMat);
      bulb.position.y = 4.65;
      group.add(bulb);

      if (isNight) {
        const point = new THREE.PointLight(0xff9922, 3.5, 18, 1.4);
        point.position.set(0, 4.65, 0);
        group.add(point);
      }
    }

    return group;
  }

  /**
   * Guardrails & Railings: W-Beam, Thrie-Beam, Concrete Jersey, Pedestrian Tubular, Ornate Victorian.
   */
  public static buildGuardrail(
    type: 'w_beam' | 'thrie_beam' | 'concrete_jersey' | 'pedestrian_tube' | 'ornate_iron',
    length: number,
    materials: MaterialManager
  ): THREE.Group {
    const group = new THREE.Group();
    group.name = `guardrail_${type}`;

    if (type === 'concrete_jersey') {
      const numBlocks = Math.max(1, Math.ceil(length / 3.0));
      const geom = new THREE.BoxGeometry(0.55, 0.82, 2.9);
      for (let b = 0; b < numBlocks; b++) {
        const mesh = new THREE.Mesh(geom, materials.barrierMaterial);
        mesh.position.set(0, 0.41, b * 3.0 + 1.5);
        mesh.castShadow = true;
        group.add(mesh);
      }
      return group;
    }

    if (type === 'pedestrian_tube' || type === 'ornate_iron') {
      // Pedestrian Railing: top rail, bottom rail, posts every 2m, vertical spindles every 0.15m
      const postSpacing = 2.0;
      const numPosts = Math.ceil(length / postSpacing) + 1;
      const postGeom = new THREE.BoxGeometry(0.06, 1.1, 0.06);

      for (let p = 0; p < numPosts; p++) {
        const post = new THREE.Mesh(postGeom, materials.railingMaterial);
        post.position.set(0, 0.55, p * postSpacing);
        post.castShadow = true;
        group.add(post);
      }

      // Top rail
      const topRailGeom = new THREE.BoxGeometry(0.08, 0.06, length);
      const topRail = new THREE.Mesh(topRailGeom, materials.railingMaterial);
      topRail.position.set(0, 1.05, length * 0.5);
      group.add(topRail);

      // Bottom rail
      const botRailGeom = new THREE.BoxGeometry(0.06, 0.04, length);
      const botRail = new THREE.Mesh(botRailGeom, materials.railingMaterial);
      botRail.position.set(0, 0.18, length * 0.5);
      group.add(botRail);

      // Vertical spindles
      const spindleSpacing = 0.16;
      const numSpindles = Math.floor(length / spindleSpacing);
      const spindleGeom = new THREE.CylinderGeometry(0.012, 0.012, 0.82, 6);
      const instancedSpindles = new THREE.InstancedMesh(spindleGeom, materials.railingMaterial, numSpindles);
      const dummy = new THREE.Object3D();
      for (let i = 0; i < numSpindles; i++) {
        dummy.position.set(0, 0.61, i * spindleSpacing);
        dummy.updateMatrix();
        instancedSpindles.setMatrixAt(i, dummy.matrix);
      }
      instancedSpindles.instanceMatrix.needsUpdate = true;
      group.add(instancedSpindles);

      return group;
    }

    // W-Beam / Thrie-Beam Crash Barrier
    const postSpacing = 2.0;
    const numPosts = Math.ceil(length / postSpacing) + 1;
    const postGeom = new THREE.BoxGeometry(0.12, 0.95, 0.16); // I-beam steel post
    const postMat = materials.railingMaterial;

    for (let p = 0; p < numPosts; p++) {
      const post = new THREE.Mesh(postGeom, postMat);
      post.position.set(0, 0.47, p * postSpacing);
      post.castShadow = true;
      group.add(post);
    }

    // W-Beam corrugation ribbon (height 0.35m or 0.55m for thrie beam)
    const beamH = type === 'thrie_beam' ? 0.55 : 0.35;
    const beamGeom = new THREE.BoxGeometry(0.08, beamH, length);
    const beamMat = new THREE.MeshStandardMaterial({ color: 0x9fa3a9, metalness: 0.85, roughness: 0.3 });
    const beam = new THREE.Mesh(beamGeom, beamMat);
    beam.position.set(0.08, 0.65, length * 0.5);
    beam.castShadow = true;
    group.add(beam);

    return group;
  }

  /**
   * Bollards: Stainless steel, Historic cast iron bell, Illuminated LED, or Spring polyurethane.
   */
  public static buildBollard(
    type: 'stainless_cylinder' | 'cast_iron_bell' | 'illuminated_led' | 'spring_orange'
  ): THREE.Group {
    const group = new THREE.Group();
    group.name = `bollard_${type}`;

    if (type === 'stainless_cylinder') {
      const geom = new THREE.CylinderGeometry(0.11, 0.11, 0.90, 16);
      const mat = new THREE.MeshStandardMaterial({ color: 0xc5cad3, metalness: 0.9, roughness: 0.2 });
      const mesh = new THREE.Mesh(geom, mat);
      mesh.position.y = 0.45;
      mesh.castShadow = true;
      group.add(mesh);

      // Recessed reflective white band
      const bandGeom = new THREE.CylinderGeometry(0.112, 0.112, 0.08, 16);
      const bandMat = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.2 });
      const band = new THREE.Mesh(bandGeom, bandMat);
      band.position.y = 0.78;
      group.add(band);

    } else if (type === 'cast_iron_bell') {
      // Historic Westminster bell bollard
      const baseGeom = new THREE.CylinderGeometry(0.18, 0.24, 0.5, 12);
      const headGeom = new THREE.SphereGeometry(0.16, 12, 12);
      const ironMat = new THREE.MeshStandardMaterial({ color: 0x1a1e22, metalness: 0.8, roughness: 0.4 });

      const base = new THREE.Mesh(baseGeom, ironMat);
      base.position.y = 0.25;
      const head = new THREE.Mesh(headGeom, ironMat);
      head.position.y = 0.62;
      group.add(base, head);

    } else if (type === 'illuminated_led') {
      const geom = new THREE.CylinderGeometry(0.10, 0.10, 0.85, 16);
      const mat = new THREE.MeshStandardMaterial({ color: 0x22262a, metalness: 0.7, roughness: 0.3 });
      const mesh = new THREE.Mesh(geom, mat);
      mesh.position.y = 0.425;
      group.add(mesh);

      // LED slit
      const slitGeom = new THREE.CylinderGeometry(0.105, 0.105, 0.06, 16);
      const slitMat = new THREE.MeshBasicMaterial({ color: 0xffffff });
      const slit = new THREE.Mesh(slitGeom, slitMat);
      slit.position.y = 0.72;
      group.add(slit);

    } else {
      // Flexible orange spring rebound bollard
      const geom = new THREE.CylinderGeometry(0.08, 0.10, 0.80, 16);
      const mat = new THREE.MeshStandardMaterial({ color: 0xff6600, roughness: 0.5 });
      const mesh = new THREE.Mesh(geom, mat);
      mesh.position.y = 0.40;
      group.add(mesh);

      for (const by of [0.45, 0.62]) {
        const bandGeom = new THREE.CylinderGeometry(0.082, 0.082, 0.06, 16);
        const bandMat = new THREE.MeshStandardMaterial({ color: 0xffffff });
        const b = new THREE.Mesh(bandGeom, bandMat);
        b.position.y = by;
        group.add(b);
      }
    }

    return group;
  }

  /**
   * Autonomous Sidewalk Delivery Robot (6-wheeled rover with antenna flag and sensor visor)
   */
  public static buildDeliveryRobot(): THREE.Group {
    const rover = new THREE.Group();
    rover.name = 'delivery_robot';

    // Insulated white chassis body (0.7m long, 0.5m wide, 0.45m high)
    const bodyGeom = new THREE.BoxGeometry(0.50, 0.42, 0.70);
    const bodyMat = new THREE.MeshStandardMaterial({ color: 0xf5f6fa, metalness: 0.1, roughness: 0.3 });
    const body = new THREE.Mesh(bodyGeom, bodyMat);
    body.position.y = 0.38;
    body.castShadow = true;
    rover.add(body);

    // Front sensor visor / lidar band
    const visorGeom = new THREE.BoxGeometry(0.52, 0.12, 0.22);
    const visorMat = new THREE.MeshStandardMaterial({ color: 0x0f141c, roughness: 0.1, metalness: 0.9 });
    const visor = new THREE.Mesh(visorGeom, visorMat);
    visor.position.set(0, 0.46, 0.26);
    rover.add(visor);

    // Lidar dome atop roof
    const lidarGeom = new THREE.CylinderGeometry(0.06, 0.06, 0.08, 12);
    const lidarMat = new THREE.MeshStandardMaterial({ color: 0x22262e, metalness: 0.8 });
    const lidar = new THREE.Mesh(lidarGeom, lidarMat);
    lidar.position.set(0, 0.63, 0.10);
    rover.add(lidar);

    // Six all-terrain rubber wheels
    const wheelGeom = new THREE.CylinderGeometry(0.12, 0.12, 0.08, 16);
    wheelGeom.rotateZ(Math.PI * 0.5);
    const wheelMat = new THREE.MeshStandardMaterial({ color: 0x181a1c, roughness: 0.85 });
    for (const wx of [-0.29, 0.29]) {
      for (const wz of [-0.22, 0, 0.22]) {
        const wheel = new THREE.Mesh(wheelGeom, wheelMat);
        wheel.position.set(wx, 0.12, wz);
        rover.add(wheel);
      }
    }

    // Safety orange antenna flag
    const antennaGeom = new THREE.CylinderGeometry(0.008, 0.008, 1.1, 8);
    const antMat = new THREE.MeshStandardMaterial({ color: 0x333333 });
    const antenna = new THREE.Mesh(antennaGeom, antMat);
    antenna.position.set(0.20, 1.1, -0.28);
    rover.add(antenna);

    // Orange safety flag
    const flagGeom = new THREE.PlaneGeometry(0.16, 0.12);
    const flagMat = new THREE.MeshBasicMaterial({ color: 0xff5500, side: THREE.DoubleSide });
    const flag = new THREE.Mesh(flagGeom, flagMat);
    flag.position.set(0.28, 1.6, -0.28);
    rover.add(flag);

    // Blinking safety LED atop antenna
    const ledGeom = new THREE.SphereGeometry(0.025, 8, 8);
    const ledMat = new THREE.MeshBasicMaterial({ color: 0xffaa00 });
    const led = new THREE.Mesh(ledGeom, ledMat);
    led.position.set(0.20, 1.66, -0.28);
    rover.add(led);

    return rover;
  }

  /**
   * Highway Overhead Gantry Sign Truss spanning across lanes
   */
  public static buildHighwayGantry(spanWidth: number): THREE.Group {
    const gantry = new THREE.Group();
    gantry.name = 'highway_gantry';

    const steelMat = new THREE.MeshStandardMaterial({ color: 0x717984, metalness: 0.85, roughness: 0.3 });

    // Vertical truss towers on left and right
    const towerH = 7.5;
    for (const sign of [-1, 1]) {
      const colGeom = new THREE.CylinderGeometry(0.22, 0.26, towerH, 12);
      const col = new THREE.Mesh(colGeom, steelMat);
      col.position.set(sign * (spanWidth * 0.5 + 1.2), towerH * 0.5, 0);
      col.castShadow = true;
      gantry.add(col);
    }

    // Overhead Box Truss Beam
    const trussGeom = new THREE.BoxGeometry(spanWidth + 2.8, 1.2, 1.2);
    const truss = new THREE.Mesh(trussGeom, steelMat);
    truss.position.set(0, towerH + 0.6, 0);
    gantry.add(truss);

    // Green Highway Directional Exit Signs
    const signCanvas = document.createElement('canvas');
    signCanvas.width = 1024;
    signCanvas.height = 384;
    const sctx = signCanvas.getContext('2d')!;
    sctx.fillStyle = '#006633';
    sctx.fillRect(0, 0, 1024, 384);
    sctx.strokeStyle = '#ffffff';
    sctx.lineWidth = 12;
    sctx.strokeRect(12, 12, 1000, 360);

    sctx.fillStyle = '#ffffff';
    sctx.font = 'bold 64px "DM Sans", sans-serif';
    sctx.fillText('EXIT 14A · AIRPORT / DOWNTOWN', 60, 130);
    sctx.font = '48px "DM Sans", sans-serif';
    sctx.fillText('NORTHBOUND EXPRESS · 100 km/h', 60, 240);

    // Downward arrow
    sctx.beginPath();
    sctx.moveTo(880, 200);
    sctx.lineTo(840, 130);
    sctx.lineTo(870, 130);
    sctx.lineTo(870, 80);
    sctx.lineTo(890, 80);
    sctx.lineTo(890, 130);
    sctx.lineTo(920, 130);
    sctx.closePath();
    sctx.fill();

    const signTex = new THREE.CanvasTexture(signCanvas);
    const signMat = new THREE.MeshStandardMaterial({ map: signTex, roughness: 0.3 });
    const signGeom = new THREE.PlaneGeometry(spanWidth * 0.75, 2.2);
    const signMesh = new THREE.Mesh(signGeom, signMat);
    signMesh.position.set(0, towerH + 0.6, 0.65);
    gantry.add(signMesh);

    return gantry;
  }

  /**
   * Paved City Block (Infrastructure only - paved plaza with tree planter grates, NO buildings!)
   */
  public static buildPavedCityBlock(
    width: number,
    depth: number,
    materials: MaterialManager
  ): THREE.Group {
    const block = new THREE.Group();
    block.name = 'city_block_plaza';

    // Paved stone plaza surface (0.15m height above road grade)
    const plazaGeom = new THREE.BoxGeometry(width, 0.15, depth);
    const plazaMesh = new THREE.Mesh(plazaGeom, materials.sidewalkMaterial);
    plazaMesh.position.set(0, 0.075, 0);
    plazaMesh.receiveShadow = true;
    block.add(plazaMesh);

    // Cast-iron Tree Planter Openings (empty infrastructure, soil beds with grates)
    const numTreesX = Math.max(1, Math.floor(width / 16));
    const numTreesZ = Math.max(1, Math.floor(depth / 16));
    const grateGeom = new THREE.BoxGeometry(2.2, 0.02, 2.2);
    const grateMat = new THREE.MeshStandardMaterial({ color: 0x22262a, metalness: 0.85, roughness: 0.4 });
    const soilMat = new THREE.MeshStandardMaterial({ color: 0x3d2b1f, roughness: 0.95 });

    for (let x = 0; x < numTreesX; x++) {
      for (let z = 0; z < numTreesZ; z++) {
        const tx = -width * 0.35 + x * (width * 0.7 / Math.max(1, numTreesX - 1));
        const tz = -depth * 0.35 + z * (depth * 0.7 / Math.max(1, numTreesZ - 1));

        // Grate frame
        const grate = new THREE.Mesh(grateGeom, grateMat);
        grate.position.set(tx, 0.155, tz);
        block.add(grate);

        // Circular soil opening in center of grate
        const soilGeom = new THREE.CircleGeometry(0.7, 16);
        soilGeom.rotateX(-Math.PI * 0.5);
        const soil = new THREE.Mesh(soilGeom, soilMat);
        soil.position.set(tx, 0.156, tz);
        block.add(soil);
      }
    }

    return block;
  }
}
